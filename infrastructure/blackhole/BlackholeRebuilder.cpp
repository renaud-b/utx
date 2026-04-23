#include "BlackholeRebuilder.hpp"
#include "../../domain/graph/GraphElement.hpp"
#include "../../domain/graph/Action.hpp"
#include <nlohmann/json.hpp>

#include "../../common/Base64.hpp"
#include "../../common/Logger.hpp"
#include "../../domain/port/IChainRepository.hpp"

using namespace utx::infra::blackhole;
using namespace utx::domain::graph;
using json = nlohmann::json;

std::string decode_snapshot(const std::string& payload) {
    try {
        return utx::common::base64::decode(payload);
    } catch (...) {
        std::string padded = payload;
        while (padded.size() % 4 != 0) padded += '=';
        return utx::common::base64::decode(padded);
    }
}


/** Reconstruct a graph directly from the chain of transactions.
 * This method scans the blockchain in reverse order to find the latest snapshot and all subsequent actions.
 * It then applies these actions to reconstruct the current state of the graph.
 *
 * @param graph_id The UUID of the graph to rebuild.
 * @param repository A shared pointer to the chain repository to access blockchain data.
 * @returns A Result containing a shared pointer to the rebuilt Graph object, or an error message if rebuilding failed.
 */
std::expected<std::shared_ptr<Graph>, std::string>
BlackholeRebuilder::rebuild(const common::UUID &graph_id,
                            const std::shared_ptr<domain::port::IChainRepository>& repository) const {
    std::vector<std::string> txs;
    std::optional<std::string> snapshot_data;

    const domain::model::Address chain_address(graph_id.to_string());
    if (!repository->chain_exists(chain_address)) {
        return std::expected<std::shared_ptr<Graph>, std::string>(
            std::unexpected("Chain does not exist for graph " + graph_id.to_string()));
    }
    repository->traverse_reverse(chain_address, [&](const domain::model::AtomicBlock& block) {
        const auto& data = block.transaction.payload_data;

        const std::string snap_prefix = "urn:pi:graph:snap:";
        const auto action_prefix = "urn:pi:graph:action:";

        if (data.starts_with(snap_prefix)) {
            snapshot_data = data.substr(snap_prefix.size());
            return false;
        }

        if (data.starts_with(action_prefix))
            txs.push_back(data);
        return true;
    });


    auto graph = build_from_transactions(graph_id, txs, snapshot_data, true);
    return std::expected<std::shared_ptr<Graph>, std::string>(graph);
}


std::vector<Action> BlackholeRebuilder::extract_actions(const std::vector<std::string>& txs) {
    std::vector<Action> actions;
    for (const auto& raw : txs) {
        try {
            auto action = Action::decode_blockchain_action(raw);
            actions.push_back(action);
        } catch (const std::exception& e) {
            LOG_THIS_WARN("skipping invalid action in transaction: {}, error: {}", raw, e.what());
        }
    }
    return actions;
}

/** Apply an action to the graph.
 * This method dispatches the action to the appropriate handler based on its type (SET, DELETE, MOVE, GROUP, COMMIT_TAG).
 * For GROUP actions, it applies contained actions recursively.
 *
 * @param graph The graph to apply the action to.
 * @param action The action to apply.
 * @param depth The current recursion depth (used for GROUP actions).
 * @param timestamp The timestamp of the action (used for COMMIT_TAG actions).
 */
void BlackholeRebuilder::apply_action(const std::shared_ptr<Graph>& graph, const Action& action, const int depth, const uint64_t timestamp)  const {
    switch (action.type) {
        case ActionType::SET:
            handle_set(graph, action);
            break;
        case ActionType::DELETE:
            handle_delete(graph, action);
            break;
        case ActionType::MOVE:
            handle_move(graph, action);
            break;
        case ActionType::GROUP:
            handle_group(graph, action, depth+1);
            break;
        case ActionType::COMMIT_TAG:
            handle_commit_tag(graph, action, timestamp);
            break;
        default:
            break;
    }
}
/** Validate whether an action can be applied to the graph.
 * This method checks the validity of the action based on its type and the current state of the graph.
 * For GROUP actions, it validates contained actions recursively and ensures that nested GROUP actions are not allowed.
 *
 * @param graph The graph to validate the action against.
 * @param action The action to validate.
 * @param depth The current recursion depth (used for GROUP actions).
 * @returns true if the action is valid, false otherwise.
 */
bool BlackholeRebuilder::is_valid_action(const std::shared_ptr<Graph> &graph,
    const Action &action, int depth) const {
    if (action.type == ActionType::GROUP) {
        for (const auto& sub_encoded : json::parse(*action.payload)) {
            try {
                if (auto sub_action = Action::decode_action(sub_encoded.get<std::string>());
                    !is_valid_action(graph, sub_action, depth+1)) {
                    LOG_THIS_DEBUG("Sub action validation failed: {}", sub_action.encode());
                    return false;
                } else if (sub_action.type == ActionType::GROUP) {
                    LOG_THIS_WARN("Nested GROUP actions are not allowed");
                    return false;
                }
            } catch (...) {
                LOG_THIS_WARN("Invalid action in GROUP");
                return false;
            }
        }

        return true;
    }
    // TODO we should apply more validation here like, e.g., for MOVE actions, check if target exists, etc.
    return true;
}

/** Handle a COMMIT_TAG action on the graph.
 * This method extracts commit information from the action's payload and updates the graph's history or metadata accordingly.
 *
 * @param graph The graph to apply the commit tag to.
 * @param action The COMMIT_TAG action containing the commit information in its payload.
 * @param timestamp The timestamp of the action, which can be used for historical tracking.
 */
void BlackholeRebuilder::handle_commit_tag(const std::shared_ptr<Graph>& graph, const Action& action, const uint64_t timestamp) {
    if (!action.payload) return;

    // We parse the commit payload to extract commit information such as revision ID, message, author, etc.
    // This information can be used to update the graph's history or metadata, for example by adding an entry to a commit log.
    try {
        auto payload = nlohmann::json::parse(*action.payload).get<CommitPayload>();

        graph->add_to_history(payload.revision_id, payload.message, payload.author, timestamp);

        LOG_THIS_INFO("Graph {}: New commit applied: {} by {}",
                      graph->id().to_string(), payload.message, payload.author);
    } catch (const std::exception& e) {
        LOG_THIS_WARN("Failed to parse COMMIT_TAG: {}", e.what());
    }
}

void BlackholeRebuilder::handle_set(const std::shared_ptr<Graph>& graph, const Action& action) {
    if (!action.payload) return;
    const auto element = graph->find_element(action.element_id);
    if (!element) return;

    auto j = json::parse(*action.payload);
    const std::string property = j["property"];
    std::string value = j["value"];
    if (j.value("is_property_deleted", false)) {
        element->delete_property(property);
        return;
    }

    if (property == "children") {
        const auto child = std::make_shared<GraphElement>(value);

        if (j.contains("position")) {
            auto pos = j["position"];

            if (pos.contains("index")) {
                element->insert_child_at(child, pos["index"]);
            }
            else if (pos.contains("before")) {
                element->insert_child_before(child, pos["before"]);
            }
            else if (pos.contains("after")) {
                element->insert_child_after(child, pos["after"]);
            }
            else {
                element->add_child(child);
            }
        } else {
            element->add_child(child);
        }
    } else {
        element->set_property(property, value);
    }
}

void BlackholeRebuilder::handle_delete(const std::shared_ptr<Graph>& graph, const Action& action) {
    if (!graph) return;
    if (action.element_id == graph->root()->id()) return;

    const auto elem = graph->find_element(action.element_id);
    if (!elem) return;

    const auto parent = elem->parent();
    if (!parent) return;

    parent->remove_child(action.element_id);
}

void BlackholeRebuilder::handle_move(const std::shared_ptr<Graph>& graph, const Action& action)  {
    if (!action.payload) return;

    const auto moving = graph->find_element(action.element_id);
    if (!moving) return;

    auto j = json::parse(*action.payload);
    std::string target_id = j["target"];
    std::string direction = j["direction"];

    const auto target = graph->find_element(target_id);
    if (!target) return;

    const auto root = graph->root();

    // We move the element directly under root
    if (direction == "in") {
        root->move_child_in(action.element_id, target);
        return;
    }

    // Left/Right move
    // We need to find the parent of the target element
    std::function<bool(std::shared_ptr<GraphElement>)> apply_move = [&](auto current) -> bool {
        auto& children = current->children();
        auto found_target = std::any_of(children.begin(), children.end(),
                                        [&](auto& c){ return c->id() == target_id; });
        if (found_target) {
            if (direction == "left") {
                return current->move_child_before(action.element_id, target_id);
            }
            if (direction == "right") {
                return current->move_child_after(action.element_id, target_id);
            }
        }
        for (auto& c : children) {
            if (apply_move(c)) return true;
        }
        return false;
    };

    if (!apply_move(root)) {
        // If we didn't find the target, we do nothing
    }
}

void BlackholeRebuilder::handle_group(const std::shared_ptr<Graph>& graph, const Action& action, const int depth)  const {
    if (!action.payload) return;
    if (depth > 10) return; // limit to avoid too deep recursion

    auto j = json::parse(*action.payload);
    for (const auto& sub_encoded : j) {
        try {
            auto sub_action = Action::decode_action(sub_encoded.get<std::string>());
            apply_action(graph, sub_action, depth);
        } catch (...) {
            LOG_THIS_WARN("skipping invalid action in GROUP");
        }
    }
}

/** Build a graph directly from a list of transaction strings, optionally using snapshot data.
 * @param graph_id The UUID of the graph to build.
 * @param txs A vector of transaction data strings.
 * @param snapshot_data Optional snapshot data to initialize the graph.
 * @param reverse_order
 * @returns A shared pointer to the built Graph object.
 */
std::shared_ptr<Graph>
BlackholeRebuilder::build_from_transactions(const common::UUID& graph_id,
                                            const std::vector<std::string>& txs,
                                            const std::optional<std::string>& snapshot_data,
                                            bool reverse_order) const {
    auto graph = std::make_shared<Graph>(graph_id);

    if (snapshot_data.has_value()) {
        try {
            *graph = Graph::from_json(decode_snapshot(snapshot_data.value()));
        } catch (const std::exception& e) {
            LOG_THIS_WARN("failed to decode snapshot: {}", e.what());
        }
    }

    std::vector<Action> actions = extract_actions(txs);
    if (reverse_order) std::ranges::reverse(actions);

    for (const auto& action : actions) {
        try {
            apply_action(graph, action);
        } catch (const std::exception& e) {
            LOG_THIS_WARN("skipping action due to error: {}", e.what());
        }
    }

    return graph;
}

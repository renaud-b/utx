#include "Graph.hpp"
#include <nlohmann/json.hpp>

#include "../../common/Uuid.hpp"

using json = nlohmann::json;
using namespace utx::domain::graph;

Graph::Graph(const common::UUID &id) {
    id_ = id;
    root_ = std::make_shared<GraphElement>("root");
    root_->set_owner(this);
    index_[root_->id()] = root_;
}


const utx::common::UUID& Graph::id() const noexcept {
    return id_;
}

std::shared_ptr<GraphElement> Graph::root() const noexcept {
    return root_;
}

void Graph::register_element(const std::shared_ptr<GraphElement>& element) {
    index_[element->id()] = element;
    element->traverse([this](const auto& node) {
        index_[node->id()] = node;
    });
}

void Graph::unregister_element(const std::string& id) {
    index_.erase(id);
}

void Graph::parse_from(nlohmann::json::const_reference j) {
    if (!j.is_object() || j.empty()) {
        throw std::runtime_error("Invalid JSON: not an array or empty");
    }
    const auto graph_id = j.value("graph_id", "");
    if (graph_id.empty()) {
        throw std::runtime_error("Invalid JSON: missing graph_id");
    }
    id_ = common::UUID(graph_id);

    if (j.contains("root") && j["root"].is_object()) {
        root_ = GraphElement::from_json_object(j["root"]);
    }
}

void Graph::add_to_history(const std::string &revision, const std::string &message, const std::string &author, const uint64_t timestamp) {
    const GraphRevisionEntry entry{
        .revision_id = revision,
        .message = message,
        .author = author,
        .timestamp = timestamp
    };
    revisions_.push_back(entry);
}

void Graph::set_id(const model::Address &address) {
    id_ = common::UUID(address.to_string());
}

void Graph::adopt_tree(const std::shared_ptr<GraphElement>& node,
                       const std::shared_ptr<GraphElement>& parent) {
    if (!node) return;

    node->set_owner(this);
    node->set_parent(parent);           // <-- parent pointer, pas id

    index_[node->id()] = node;

    // idéalement: node->children() qui renvoie un vector<shared_ptr<GraphElement>>
    for (const auto& child : node->children()) {
        adopt_tree(child, node);
    }
}


std::shared_ptr<GraphElement> Graph::find_element(const std::string& element_id) const {
    if (const auto it = index_.find(element_id); it != index_.end()) {
        if (auto ptr = it->second.lock()) return ptr;
    }
    if (const auto child = root_->find_child_by_id(element_id)) return child;
    return nullptr;
}


Graph Graph::from_json(const std::string &json_str) {
    const auto j = nlohmann::json::parse(json_str);
    if (!j.is_object() || j.empty()) {
        throw std::runtime_error("Invalid JSON: not an array or empty");
    }
    Graph graph(common::generate_uuid_v7());
    graph.parse_from(j);
    return graph;
}

Graph Graph::from_raw_json(const nlohmann::json &j) {
    Graph graph{common::generate_uuid_v7()};
    graph.parse_from(j);
    return graph;
}
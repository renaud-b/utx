#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

#include "../../domain/graph/GraphElement.hpp"
#include "../../domain/graph/Action.hpp"
#include "../../common/Hash.hpp"

namespace utx::infra::blackhole {

/**
 * @brief Emits graph Actions from a GraphElement tree, using Utopixia Graph semantics.
 *
 * Semantics assumed:
 * - To create a child under parent_id:
 *      SET(parent_id, "children", child_name)
 *   which causes the engine to create a node child_path = parent_path + "/" + child_name
 *   and child_id = md5_hex(child_path).
 *
 * - Node technical properties ("id", "path", "parent") must NOT be emitted.
 * - "children" is not emitted as a property; children are created only via SET(parent,"children",name).
 */
class GraphActionEmitter final {
public:
    struct EmitState {
        std::unordered_map<std::string, std::string> id_to_path;      // deterministic id -> deterministic path
        std::unordered_map<std::string, uint32_t> next_index;         // parent_id -> sibling counter
        std::unordered_map<std::string, std::unordered_map<std::string, uint32_t>> used_names;

    };

    using ChildNamePolicy = std::function<std::string(
        const std::shared_ptr<utx::domain::graph::GraphElement>& node,
        const std::string& parent_id,
        EmitState& st
    )>;

    /**
     * @brief Emit subtree under an existing parent graph node.
     * @param subtree_root GraphElement subtree root (input structure)
     * @param parent_id Existing node id in target graph
     * @param parent_path Deterministic path of parent (required to compute deterministic child ids)
     * @param out_actions Actions appended here
     * @param naming Naming policy used to create deterministic node names
     */
    static void emit_under_parent(
        const std::shared_ptr<utx::domain::graph::GraphElement>& subtree_root,
        const std::string& parent_id,
        const std::string& parent_path,
        std::vector<utx::domain::graph::Action>& out_actions,
        ChildNamePolicy naming = preserve_name_policy()
    ) {
        if (!subtree_root) return;

        EmitState st;
        st.id_to_path[parent_id] = parent_path;

        emit_node_recursive(subtree_root, parent_id, out_actions, st, naming);
    }

    /**
     * @brief Default naming policy: nodeName_index per parent.
     * Example: identifier_0, call_expression_1, ...
     */
    static ChildNamePolicy default_naming_policy() {
        return [](const std::shared_ptr<utx::domain::graph::GraphElement>& node,
                  const std::string& parent_id,
                  EmitState& st) -> std::string {
            std::string base = node ? node->name() : "node";
            uint32_t idx = st.next_index[parent_id]++;
            return base + "_" + std::to_string(idx);
        };
    }

    static ChildNamePolicy preserve_name_policy() {
        return [](const std::shared_ptr<utx::domain::graph::GraphElement>& node,
                  const std::string& parent_id,
                  EmitState& st) -> std::string {

            std::string base = node ? node->name() : "node";
            auto& per_parent = st.used_names[parent_id];
            uint32_t& count = per_parent[base];

            // first time => keep base as-is
            if (count == 0) {
                count = 1;
                return base;
            }

            // collision => add suffix
            std::string out = base + "_" + std::to_string(count);
            count++;
            return out;
        };
    }
private:
    // -------- Path / ID helpers --------

    static std::string parent_path_or_default(const EmitState& st, const std::string& parent_id) {
        if (auto it = st.id_to_path.find(parent_id); it != st.id_to_path.end()) {
            return it->second;
        }
        return "//root";
    }

    static std::string child_path(const EmitState& st,
                                  const std::string& parent_id,
                                  const std::string& child_name) {
        return parent_path_or_default(st, parent_id) + "/" + child_name;
    }

    static std::string id_from_path(const std::string& path) {
        return utx::common::md5_hex(path);
    }

    // -------- Property emission --------

    static bool is_technical_property(const std::string& key) {
        // These are "technical" props that must never be emitted
        // children is excluded because children are created via SET(parent,"children",name)
        return key == "id" || key == "path" || key == "parent" || key == "children";
    }

    static void emit_properties_filtered(
        const std::shared_ptr<utx::domain::graph::GraphElement>& node,
        const std::string& target_id,
        std::vector<utx::domain::graph::Action>& out_actions
    ) {
        if (!node) return;

        for (const auto& [k, v] : node->properties()) {
            if (is_technical_property(k)) continue;
            out_actions.push_back(utx::domain::graph::build_set_action(target_id, k, v));
        }
    }

    // -------- Recursion --------

    static void emit_node_recursive(
        const std::shared_ptr<utx::domain::graph::GraphElement>& node,
        const std::string& parent_id,
        std::vector<utx::domain::graph::Action>& out_actions,
        EmitState& st,
        const ChildNamePolicy& naming
    ) {
        if (!node) return;

        // 1) Create deterministic child_name
        const std::string child_name = naming(node, parent_id, st);

        // 2) Emit SET(parent, "children", child_name)
        out_actions.push_back(utx::domain::graph::build_set_action(parent_id, "children", child_name));

        // 3) Compute deterministic child_id based on parent_path + child_name
        const std::string cp = child_path(st, parent_id, child_name);
        const std::string child_id = id_from_path(cp);

        st.id_to_path[child_id] = cp;

        // 4) Emit properties on child_id (filtered)
        emit_properties_filtered(node, child_id, out_actions);

        // 5) Recurse for its children
        for (const auto& c : node->children()) {
            emit_node_recursive(c, child_id, out_actions, st, naming);
        }
    }
};

} // namespace utx::infra::graph

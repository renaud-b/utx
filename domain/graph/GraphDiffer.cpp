#include <vector>
#include <string>
#include <unordered_map>
#include <ranges>
#include "../../common/Hash.hpp"
#include "GraphElement.hpp"
#include "Action.hpp"
#include "GraphDiffer.hpp"

#include <unordered_set>

namespace utx::domain::graph {
    /** Compute the difference between two graph elements.
      *
      * @param current The current graph element.
      * @param target The target graph element.
      * @returns A vector of actions required to transform the current graph into the target graph.
      */
    std::vector<Action> GraphDiffer::compute_diff(
        const std::shared_ptr<GraphElement>& current,
        const std::shared_ptr<GraphElement>& target
    ) {
        std::vector<Action> actions;
        diff_recursive(current, target, actions);
        return actions;
    }
    /** Recursive helper function to compute differences between two graph elements.
      * If differences are found, corresponding actions are added to the actions vector.
      *
      * @param a The current graph element.
      * @param b The target graph element.
      * @param actions A reference to the vector of actions to populate.
      */
    void GraphDiffer::diff_recursive(
    const std::shared_ptr<GraphElement>& a,
    const std::shared_ptr<GraphElement>& b,
    std::vector<Action>& actions
) {
        if (!a || !b) return;

        // 1️⃣ Compare properties

        const auto& propsA = a->properties();
        const auto& propsB = b->properties();

        // Set or update properties
        for (const auto& [key, valB] : propsB) {
            if (!propsA.contains(key) || propsA.at(key) != valB) {
                actions.push_back(build_set_action(a->id(), key, valB, false));
            }
        }

        // Delete removed properties
        for (const auto& [key, valA] : propsA) {
            if (!propsB.contains(key)) {
                actions.push_back(build_set_action(a->id(), key, "", true));
            }
        }

        // 2️⃣ Diff children using LCS (order-aware, no name collision)
        diff_children_lcs(a, b, actions);
    }
    /** Compute the path of a child element based on its parent's path and its own name.
      * @param parent_path The path of the parent element.
      * @param child_name The name of the child element.
      * @returns The computed path of the child element.
      */
    std::string GraphDiffer::compute_child_path(const std::string& parent_path, const std::string& child_name) {
        // Parent_path look like "//root/child1"
        // So child_path = parent_path + "/" + child_name
        return parent_path + "/" + child_name;
    }
    /** Compute the ID of a graph element based on its path.
      * @param path The path of the graph element.
      * @returns The computed ID of the graph element.
      */
    std::string GraphDiffer::compute_id_from_path(const std::string& path) {
        return common::md5_hex(path);
    }
    /** Create actions to add a graph element and its properties based on parent IDs.
      * This function generates the necessary actions to add a new element
      * under a specified parent element, including setting its properties
      * and recursively adding its children.
      *
      * @param parent_id The ID of the parent element.
      * @param parent_path The path of the parent element.
      * @param elemB The graph element to add.
      * @param actions A reference to the vector of actions to populate.
      */
    void GraphDiffer::create_element_actions_from_parent_ids(
        const std::string& parent_id,
        const std::string& parent_path,
        const std::shared_ptr<GraphElement>& elemB,
        std::vector<Action>& actions
    ) {
        actions.push_back(build_set_action(parent_id, "children", elemB->name(), false));

        const auto child_path = parent_path + "/" + elemB->name();
        const auto child_id = common::md5_hex(child_path);

        for (const auto& [key, val] : elemB->properties()) {
            actions.push_back(build_set_action(child_id, key, val, false));
        }

        for (const auto& childB : elemB->children()) {
            create_element_actions_from_parent_ids(child_id, child_path, childB, actions);
        }
    }

    /** Create actions to add a graph element and its properties.
      * This function generates the necessary actions to add a new element
      * under a specified parent element, including setting its properties
      * and recursively adding its children.
      *
      * @param parentA The parent graph element in the current graph.
      * @param elemB The graph element to add from the target graph.
      * @param actions A reference to the vector of actions to populate.
      */
    void GraphDiffer::create_element_actions(
        const std::shared_ptr<GraphElement>& parentA,
        const std::shared_ptr<GraphElement>& elemB,
        std::vector<Action>& actions
    ) {
        // 1) Create the new child under parentA
        actions.push_back(build_set_action(parentA->id(), "children", elemB->name(), false));

        // 2) Compute the id/path of the newly created child
        const auto child_path = compute_child_path(parentA->path(), elemB->name());
        const auto child_id = compute_id_from_path(child_path);

        // 3) Set all properties of elemB on the newly created child
        for (const auto& [key, val] : elemB->properties()) {
            // Skip "children" property as it's handled by add_child action
            if (key == "children") continue;
            actions.push_back(build_set_action(child_id, key, val, false));
        }

        // 4) Recursively process children of elemB considering the new child as their parent
        // Note: We use the newly created child's id/path for recursion
        for (const auto& childB : elemB->children()) {
            create_element_actions_from_parent_ids(child_id, child_path, childB, actions);
        }
    }


    void GraphDiffer::diff_children_lcs(
        const std::shared_ptr<GraphElement>& parentA,
        const std::shared_ptr<GraphElement>& parentB,
        std::vector<Action>& actions
    ) {
            const auto& childrenA = parentA->children();
            const auto& childrenB = parentB->children();

            auto matches = compute_lcs(childrenA, childrenB);

            std::unordered_set<int> matchedA;
            std::unordered_set<int> matchedB;

            for (auto [i, j] : matches) {
                matchedA.insert(i);
                matchedB.insert(j);
            }

            // 1️⃣ DELETE nodes not in LCS
            for (int i = childrenA.size() - 1; i >= 0; --i) {
                if (!matchedA.contains(i)) {
                    actions.push_back(Action{
                        ActionType::DELETE,
                        childrenA[i]->id(),
                        std::nullopt
                    });
                }
            }

            // 2️⃣ ADD nodes not in LCS (respect index)
            for (int j = 0; j < childrenB.size(); ++j) {
                if (!matchedB.contains(j)) {

                    create_element_actions_at_index(
                        parentA,
                        childrenB[j],
                        j,
                        actions
                    );
                }
            }

            // 3️⃣ Recurse on matched nodes
            for (auto [i, j] : matches) {
                diff_recursive(childrenA[i], childrenB[j], actions);
            }
        }

    void GraphDiffer::create_element_actions_at_index(
        const std::shared_ptr<GraphElement>& parentA,
        const std::shared_ptr<GraphElement>& elemB,
        int index,
        std::vector<Action>& actions
    ) {
        json payload = {
            {"property", "children"},
            {"value", elemB->name()},
            {"position", {
                {"index", index}
            }}
        };

        actions.push_back(Action{
            ActionType::SET,
            parentA->id(),
            payload.dump()
        });

        const auto child_path = compute_child_path(parentA->path(), elemB->name());
        const auto child_id = compute_id_from_path(child_path);

        for (const auto& [key, val] : elemB->properties()) {
            actions.push_back(build_set_action(child_id, key, val, false));
        }

        for (const auto& child : elemB->children()) {
            create_element_actions_from_parent_ids(child_id, child_path, child, actions);
        }
    }
};

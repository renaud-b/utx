#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <ranges>
#include "../../common/Hash.hpp"
#include "GraphElement.hpp"
#include "Action.hpp"

namespace utx::domain::graph {

class GraphDiffer {
public:
    /** Compute the difference between two graph elements.
     * @param current The current graph element.
     * @param target The target graph element.
     * @returns A vector of actions required to transform the current graph into the target graph.
     */
    static std::vector<Action> compute_diff(
        const std::shared_ptr<GraphElement>& current,
        const std::shared_ptr<GraphElement>& target
    );

private:
    /** Recursive helper function to compute differences between two graph elements.
     * @param a The current graph element.
     * @param b The target graph element.
     * @param actions A reference to the vector of actions to populate.
     */
    static void diff_recursive(
        const std::shared_ptr<GraphElement>& a, 
        const std::shared_ptr<GraphElement>& b, 
        std::vector<Action>& actions
    );
    /** Compute the path of a child element based on its parent's path and its own name.
     * @param parent_path The path of the parent element.
     * @param child_name The name of the child element.
     * @returns The computed path of the child element.
     */
    static std::string compute_child_path(const std::string& parent_path, const std::string& child_name);
    /** Compute the ID of a graph element based on its path.
     * @param path The path of the graph element.
     * @returns The computed ID of the graph element.
     */
    static std::string compute_id_from_path(const std::string& path);
    /** Create actions to add a graph element and its properties based on parent IDs.
     * @param parent_id The ID of the parent element.
     * @param parent_path The path of the parent element.
     * @param elemB The graph element to add.
     * @param actions A reference to the vector of actions to populate.
     */
    static void create_element_actions_from_parent_ids(
        const std::string& parent_id,
        const std::string& parent_path,
        const std::shared_ptr<GraphElement>& elemB,
        std::vector<Action>& actions);
    /** Create actions to add a graph element and its properties.
     * @param parentA The parent graph element in the current graph.
     * @param elemB The graph element to add from the target graph.
     * @param actions A reference to the vector of actions to populate.
     */
    static void create_element_actions(
        const std::shared_ptr<GraphElement>& parentA,
        const std::shared_ptr<GraphElement>& elemB,
        std::vector<Action>& actions
    );
    /** Compute the differences between the children of two graph elements using the longest common subsequence (LCS) algorithm.
     * This function identifies which children have been added, deleted, or modified based on their properties and generates corresponding actions.
     * @param parentA The parent graph element in the current graph.
     * @param parentB The parent graph element in the target graph.
     * @param actions A reference to the vector of actions to populate with the differences.
     */
    static void diff_children_lcs(
        const std::shared_ptr<GraphElement>& parentA,
        const std::shared_ptr<GraphElement>& parentB,
        std::vector<Action>& actions
    );
    /** Create actions to add a graph element and its properties at a specific index under a parent element.
     * This function generates the necessary actions to add a new element under a specified parent element at a given index,
     * including setting its properties and recursively adding its children.
     * @param parentA The parent graph element in the current graph.
     * @param elemB The graph element to add from the target graph.
     * @param index The index at which to add the new element under the parent.
     * @param actions A reference to the vector of actions to populate with the necessary actions for adding the element.
     */
    static void create_element_actions_at_index(
        const std::shared_ptr<GraphElement>& parentA,
        const std::shared_ptr<GraphElement>& elemB,
        int index,
        std::vector<Action>& actions
    );
    /** Check if two graph elements are equivalent based on their properties.
     * @param a The first graph element.
     * @param b The second graph element.
     * @returns true if the elements are equivalent, false otherwise.
     */
    static bool nodes_equivalent(
        const std::shared_ptr<GraphElement>& a,
        const std::shared_ptr<GraphElement>& b
    ) {
        if (!a || !b) return false;

        // Compare properties only (ignore children)
        return a->name() == b->name()
            && a->properties() == b->properties();
    }
    /** Compute the longest common subsequence (LCS) of two lists of graph elements based on their properties.
     * @param A The first list of graph elements.
     * @param B The second list of graph elements.
     * @returns A vector of pairs of indices representing the matching elements in A and B that form the LCS.
     */
    static std::vector<std::pair<int,int>> compute_lcs(
        const std::vector<std::shared_ptr<GraphElement>>& A,
        const std::vector<std::shared_ptr<GraphElement>>& B
    ) {
        const int n = A.size();
        const int m = B.size();

        std::vector dp(n + 1, std::vector(m + 1, 0));

        for (int i = n - 1; i >= 0; --i) {
            for (int j = m - 1; j >= 0; --j) {
                if (nodes_equivalent(A[i], B[j])) {
                    dp[i][j] = 1 + dp[i + 1][j + 1];
                } else {
                    dp[i][j] = std::max(dp[i + 1][j], dp[i][j + 1]);
                }
            }
        }

        // reconstruct LCS index pairs
        std::vector<std::pair<int,int>> matches;
        int i = 0, j = 0;
        while (i < n && j < m) {
            if (nodes_equivalent(A[i], B[j])) {
                matches.emplace_back(i, j);
                ++i;
                ++j;
            } else if (dp[i + 1][j] >= dp[i][j + 1]) {
                ++i;
            } else {
                ++j;
            }
        }

        return matches;
    }
};

} // namespace utx::domain::graph
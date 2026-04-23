#pragma once
#include "../../domain/graph/IGraphRebuilder.hpp"
#include "../../domain/port/IChainRepository.hpp"
#include "../../domain/graph/Action.hpp"
#include "../../domain/graph/Graph.hpp"
#include <vector>
#include <memory>

namespace utx::infra::blackhole {
    /** Implementation of IGraphRebuilder that reconstructs a graph from a series of actions.
     * This class processes actions such as SET, DELETE, MOVE, and GROUP to rebuild the graph structure.
     */
    class BlackholeRebuilder final : public domain::graph::IGraphRebuilder {
    public:
        /** Rebuild a graph from the provided transactions.
         * @param graph_id The UUID of the graph to rebuild.
         * @param repository A shared pointer to the chain repository to access blockchain data.
         * @returns A shared pointer to the rebuilt Graph object.
         */
        [[nodiscard]]
        std::expected<std::shared_ptr<domain::graph::Graph>, std::string>
        rebuild(const common::UUID &graph_id, const std::shared_ptr<domain::port::IChainRepository>& repository) const override;
        /** Apply a single action to the graph.
         * @param graph The graph to apply the action to.
         * @param action The action to apply.
         * @param depth The current recursion depth (used for GROUP actions).
         * @param timestamp The timestamp of the action (used for COMMIT_TAG actions).
         */
        void apply_action(const std::shared_ptr<domain::graph::Graph>& graph, const domain::graph::Action& action, int depth = 0, uint64_t timestamp = 0) const override;
        /** Validate whether an action can be applied to the graph.
         * @param graph The graph to validate the action against.
         * @param action The action to validate.
         * @param depth The current recursion depth (used for GROUP actions).
         * @returns true if the action is valid, false otherwise.
         */
        [[nodiscard]] bool is_valid_action(const std::shared_ptr<domain::graph::Graph> &graph,
            const domain::graph::Action &action, int depth) const override;

    private:
        /** Extract actions from the given transaction strings.:
         * @param txs A vector of transaction data strings.
         * @returns A vector of extracted Action objects.
         */
        [[nodiscard]] static std::vector<domain::graph::Action> extract_actions(const std::vector<std::string>& txs);
        /** Handle a COMMIT_TAG action on the graph.
         * @param graph The graph to apply the action to.
         * @param action The COMMIT_TAG action to handle.
         * @param timestamp The timestamp of the action.
         */
        static void handle_commit_tag(const std::shared_ptr<domain::graph::Graph>& graph, const domain::graph::Action& action, uint64_t timestamp);
        /** Handle a SET action on the graph.
         * @param graph The graph to apply the action to.
         * @param action The SET action to handle.
         */
        static void handle_set(const std::shared_ptr<domain::graph::Graph>& graph, const domain::graph::Action& action);
        /** Handle a DELETE action on the graph.
         * @param graph The graph to apply the action to.
         * @param action The DELETE action to handle.
         */
        static void handle_delete(const std::shared_ptr<domain::graph::Graph>& graph, const domain::graph::Action& action);
        /** Handle a MOVE action on the graph.
         * @param graph The graph to apply the action to.
         * @param action The MOVE action to handle.
         */
        static void handle_move(const std::shared_ptr<domain::graph::Graph>& graph, const domain::graph::Action& action) ;
        /** Handle a GROUP action on the graph, applying contained actions recursively.
         * @param graph The graph to apply the action to.
         * @param action The GROUP action to handle.
         * @param depth The current recursion depth (used to limit recursion).
         */
        void handle_group(const std::shared_ptr<domain::graph::Graph>& graph, const domain::graph::Action& action, int depth = 0) const ;
        /** Build a graph directly from a list of transaction strings, optionally using snapshot data.
         * @param graph_id The UUID of the graph to build.
         * @param txs A vector of transaction data strings.
         * @param snapshot_data Optional snapshot data to initialize the graph.
         * @param reverse_order If true, process actions in reverse order.
         * @returns A shared pointer to the built Graph object.
         */
        [[nodiscard]]
        std::shared_ptr<domain::graph::Graph> build_from_transactions(
        const common::UUID& graph_id,
        const std::vector<std::string>& txs,
        const std::optional<std::string>& snapshot_data = std::nullopt,
        bool reverse_order = false) const;

    };
} // namespace utx::infra::blackhole

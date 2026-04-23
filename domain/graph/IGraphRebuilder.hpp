#pragma once
#include <expected>

#include "Graph.hpp"
#include "../port/IChainRepository.hpp"
#include <memory>
#include <string>

#include "Action.hpp"

namespace utx::domain::graph {
    /** Interface for rebuilding a graph from a series of transactions.
     * Implementations should provide a method to reconstruct the graph based on the given transactions.
     */
    class IGraphRebuilder {
    protected:
        ~IGraphRebuilder() = default;

    public:
        /** Rebuild a graph from the provided transactions.
         * @param graph_id The UUID of the graph to rebuild.
         * @param repository A shared pointer to the chain repository to access blockchain data.
         * @returns A shared pointer to the rebuilt Graph object.
         */
        [[nodiscard]]
        virtual std::expected<std::shared_ptr<Graph>, std::string>
        rebuild(const common::UUID& graph_id, const std::shared_ptr<port::IChainRepository>& repository) const = 0;
        /** Apply a single action to the graph.
         * @param graph The graph to apply the action to.
         * @param action The action to apply.
         * @param timestamp The timestamp of the action (used for COMMIT_TAG actions).
         * @param depth The current recursion depth (used for GROUP actions).
         */
        virtual void apply_action(const std::shared_ptr<Graph>& graph, const Action& action, int depth = 0, uint64_t timestamp = 0) const = 0;
        /** Validate whether an action can be applied to the graph.
         * @param graph The graph to validate the action against.
         * @param action The action to validate.
         * @param depth The current recursion depth (used for GROUP actions).
         * @returns true if the action is valid, false otherwise.
         */
        [[nodiscard]]
        virtual bool is_valid_action(const std::shared_ptr<Graph>& graph, const Action& action, int depth = 0) const = 0;
    };

} // namespace utx::domain::graph

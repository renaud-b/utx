#pragma once

#include <set>
#include <string>
#include <tree_sitter/api.h>
#include "domain/graph/Graph.hpp"

namespace utx::infra::languages::javascript {
    /** JsParser uses Tree-sitter to parse JavaScript source code into a graph representation.
     * It processes the syntax tree and converts nodes into GraphElement objects.
     */
    class JsParser final {
    public:
        /** Construct a JsParser with optional debug mode.
         * @param debug_mode If true, includes additional debug information in the graph elements.
         */
        explicit JsParser(bool debug_mode);
        ~JsParser();
        /** Parse a JavaScript source string into a graph representation.
         * @param source The JavaScript source code to parse.
         * @returns A shared pointer to the root GraphElement representing the parsed structure.
         */
        [[nodiscard]]
        std::shared_ptr<domain::graph::GraphElement> parse(const std::string& source) const;
    private:
        /** Set of token types to ignore during parsing. */
        static const std::set<std::string> IGNORED_TOKENS;
        TSParser* parser_;
        bool is_debug_mode_;
        /** Process a Tree-sitter node and convert it into a GraphElement.
         * @param node The Tree-sitter node to process.
         * @param source The original source code for context.
         * @returns A shared pointer to the created GraphElement, or nullptr if ignored.
         */
        [[nodiscard]]
        std::shared_ptr<domain::graph::GraphElement> process_node(const TSNode &node, const std::string& source) const ;
        /** Process a Tree-sitter node into a GraphElement using registered handlers.
         * @param node The Tree-sitter node to process.
         * @param source The original source code for context.
         * @returns A shared pointer to the created GraphElement, or nullptr if the node is an error or ignored.
         */
        static std::shared_ptr<domain::graph::GraphElement> process_element(const TSNode &node, const std::string& source);


        static std::string get_node_text(const TSNode node, const std::string& source) {
            if (ts_node_is_null(node)) return {}; //
            const uint32_t start = ts_node_start_byte(node); //
            const uint32_t end = ts_node_end_byte(node); //

            // Sécurité pour éviter les dépassements de mémoire
            if (start >= end || end > source.size()) return {}; //

            return source.substr(start, end - start); //
        }
    };
} // namespace utx::infra::vm::javascript

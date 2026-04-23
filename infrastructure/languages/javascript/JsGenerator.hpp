#pragma once

#include <string>

#include "../../../../domain/graph/GraphElement.hpp"

namespace utx::infra::languages::javascript {
    /** JsGenerator provides utilities to convert graph elements into JavaScript code.
     * It includes methods to convert operators and recursively convert graph nodes to JS syntax.
     */
    class JsGenerator {
    public:
        JsGenerator();
        /** Convert a graph element representing an operator into its JavaScript equivalent.
         * @param op The operator string from the graph element.
         * @returns The corresponding JavaScript operator string.
         */
        static std::string operator_converter(const std::string& op);
        /** Convert a graph element and its children into JavaScript code.
         * @param element The graph element to convert.
         * @param out An output string stream to append the generated JavaScript code.
         * @returns A string containing the generated JavaScript code.
         */
        void convert_node_to_js(const domain::graph::GraphElement& element,
                                    std::ostringstream& out) const;

    private:
        static int binary_precedence(const std::string& op);
        static bool is_right_associative(const std::string& op);
        void emit_expression(const domain::graph::GraphElement& el, std::ostringstream& out, int parentPrec = 0, bool isRightChild = false) const;

    };
} // namespace utx::infra::languages::javascript

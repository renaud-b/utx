#pragma once
#include <functional>
#include <memory>
#include <tree_sitter/api.h>

#include "../../../domain/graph/GraphElement.hpp"

namespace utx::infra::languages::javascript {
    /** NodeHandler is a function type that takes a TSNode and source string,
     * and returns a shared pointer to a GraphElement.
     */
    using NodeHandler = std::function<std::shared_ptr<utx::domain::graph::GraphElement>(const TSNode&, const std::string&)>;
    /** JsNodeFactory manages the creation of GraphElement nodes from TSNode types.
     * It allows registering custom handlers for different node types and provides
     * a method to create nodes based on their type.
     */
    class JsNodeFactory {
    public:
        /** Get the singleton instance of JsNodeFactory.
         * @returns A reference to the singleton JsNodeFactory instance.
         */
        static JsNodeFactory& instance() {
            static JsNodeFactory inst;
            return inst;
        }
        /** Register a handler function for a specific node type.
         * @param type The TSNode type string.
         * @param handler The function to handle nodes of this type.
         */
        void register_handler(std::string type, const NodeHandler &handler) {
            handlers_[std::move(type)] = handler;
        }
        /** Create a GraphElement from a TSNode using the registered handler for its type.
         * If no handler is found, a default handler is used.
         * @param type The TSNode type string.
         * @param node The TSNode to convert.
         * @param src The original source code string for context.
         * @returns A shared pointer to the created GraphElement.
         */
        std::shared_ptr<utx::domain::graph::GraphElement> create(const std::string& type, const TSNode& node, const std::string& src) const {
            if (const auto it = handlers_.find(type); it != handlers_.end()) {
                return it->second(node, src);
            }
            return default_handler(node, src, type);
        }

    private:
        /** Default handler that creates a basic GraphElement with the node's text.
         * @param node The TSNode to convert.
         * @param src The original source code string for context.
         * @param type The TSNode type string.
         * @returns A shared pointer to the created GraphElement with type and text properties.
         */
        static std::shared_ptr<utx::domain::graph::GraphElement> default_handler(const TSNode& node, const std::string& src, const std::string& type) {
            auto element = std::make_shared<utx::domain::graph::GraphElement>(type);
            const auto start = ts_node_start_byte(node);
            if (const auto end   = ts_node_end_byte(node); end > start)
                element->set_property("text", src.substr(start, end - start));
            return element;
        }

        std::unordered_map<std::string, NodeHandler> handlers_;
    };

} // namespace utx::infra::vm::javascript
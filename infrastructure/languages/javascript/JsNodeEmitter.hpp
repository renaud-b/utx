#pragma once

#include <functional>
#include <iostream>

#include "../../../common/Logger.hpp"
#include "../../../domain/graph/GraphElement.hpp"

namespace utx::infra::languages::javascript {
    /** JsEmitter is a function type that takes a GraphElement and an output stream,
     * and emits JavaScript code representation of the element into the stream.
     */
    using JsEmitter = std::function<void(const domain::graph::GraphElement&, std::ostringstream&)>;
    /** JsNodeEmitter manages the emission of JavaScript code from graph elements.
     * It allows registering custom emitters for different element types and provides
     * a method to emit code based on the element's type.
     */
    class JsNodeEmitter {
    public:
        /** Get the singleton instance of JsNodeEmitter.
         * @returns A reference to the singleton JsNodeEmitter instance.
         */
        static JsNodeEmitter& instance() {
            static JsNodeEmitter inst;
            return inst;
        }
        /** Register an emitter function for a specific element type.
         * @param type The element type string.
         * @param emitter The function to emit JavaScript code for this type.
         */
        void register_handler(std::string type, JsEmitter emitter) {
            handlers_[std::move(type)] = std::move(emitter);
        }
        /** Check if an emitter is registered for a specific element type.
         * @param type The element type string.
         * @returns true if an emitter is registered, false otherwise.
         */
        bool has(const std::string& type) const {
            return handlers_.contains(type);
        }
        /** Emit JavaScript code for a given graph element based on its type.
         * If no emitter is found for the type, an error message is printed.
         * @param type The element type string.
         * @param el The graph element to emit code for.
         * @param out An output string stream to append the generated JavaScript code.
         */
        void emit(const std::string& type,
                  const domain::graph::GraphElement& el,
                  std::ostringstream& out) const {
            if (auto it = handlers_.find(type); it != handlers_.end()) {
                it->second(el, out);
            } else {
                LOG_THIS_ERROR("Unknown type: {}", type);
            }
        }

    private:
        std::unordered_map<std::string, JsEmitter> handlers_;

    };

} // utx::infra::vm::javascript
#pragma once
#include <functional>

#include "../../../../common/Base64.hpp"
#include "SandboxedVm.hpp"

namespace utx::infra::languages::javascript {
    typedef std::function<void(const SandboxedVm&)> VmHandler;

    class VmInfraBinder final {
    public:
        /** Construct a VmInfraBinder with a reference to the ObserverRegistry.
         */
        explicit VmInfraBinder() = default;
        /** Register all handlers to be bound to SandboxedVm instances.
         * This method should be called before any VMs are created, and will set up the necessary handlers for the JavaScript environment.
         */
        void register_handlers() {
            register_console_handler();
        }
        /** Bind all registered handlers to the given SandboxedVm instance.
         *  @param vm The SandboxedVm instance to bind handlers to.
         */
        void bind(const SandboxedVm& vm) const {
            for (const auto& handler : handlers_) {
                handler(vm);
            }
        }
        /** Register console handler to provide some basic console logging functionality (console.log and console.error) to the JavaScript environment.
         */
        void register_console_handler() {
            handlers_.emplace_back([this](const SandboxedVm& vm) {
                 this->bind_console_namespace(vm);
             });
        }
        /** Factory method to create a default VmInfraBinder with all standard handlers registered.
         *  @return A VmInfraBinder instance with console handlers registered.
         */
        static std::unique_ptr<VmInfraBinder> default_binder() {
            auto binder = std::make_unique<VmInfraBinder>();
            binder->register_console_handler();
            return binder;
        }

    private:
        std::vector<VmHandler> handlers_;


        void bind_console_namespace(const SandboxedVm& vm) const {
            JSContext* ctx = vm.get_context();
            JS_SetContextOpaque(ctx, const_cast<VmInfraBinder*>(this));

            JSValue global = JS_GetGlobalObject(ctx);
            // Create a console.log object
            JSValue console = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
                for (int i = 0; i < argc; ++i) {
                    const char* str = JS_ToCString(ctx, argv[i]);
                    if (str) {
                        LOG_THIS_INFO("[JS] {}", str);
                        JS_FreeCString(ctx, str);
                    }
                }
                return JS_UNDEFINED;
            }, "log", 1));
            // Create a console.error object
            JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
                for (int i = 0; i < argc; ++i) {
                    const char* str = JS_ToCString(ctx, argv[i]);
                    if (str) {
                        LOG_THIS_ERROR("[JS] {}", str);
                        JS_FreeCString(ctx, str);
                    }
                }
                return JS_UNDEFINED;
            }, "error", 1));

            JS_SetPropertyStr(ctx, global, "console", console);

            // Create the atob function
            JS_SetPropertyStr(ctx, global, "atob", JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
                if (argc < 1) return JS_UNDEFINED;
                const char* str = JS_ToCString(ctx, argv[0]);
                if (!str) return JS_UNDEFINED;

                std::string input(str);
                JS_FreeCString(ctx, str);

                std::string decoded;
                try {
                    decoded = common::base64::decode(input);
                } catch (const std::exception& e) {
                    LOG_THIS_ERROR("Invalid base64 input to atob: {}", e.what());
                    return JS_UNDEFINED;
                }

                return JS_NewStringLen(ctx, decoded.data(), decoded.size());
            }, "atob", 1));
            // Create the btoa function
            JS_SetPropertyStr(ctx, global, "btoa", JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
                if (argc < 1) return JS_UNDEFINED;
                const char* str = JS_ToCString(ctx, argv[0]);
                if (!str) return JS_UNDEFINED;

                std::string input(str);
                JS_FreeCString(ctx, str);

                std::string encoded = common::base64::encode(input);
                return JS_NewString(ctx, encoded.c_str());
            }, "btoa", 1));

            JS_FreeValue(ctx, global);
        }
    };

} // namespace utx::infra::vm::javascript

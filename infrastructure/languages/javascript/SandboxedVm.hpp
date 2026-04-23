#pragma once
#include <quickjs.h>
#include <chrono>
#include <expected>
#include <string>
#include <vector>

#include "../../../../common/Logger.hpp"

namespace utx::infra::languages::javascript {
    /** SandboxedVm encapsulates a QuickJS runtime and context,
     * providing methods to evaluate JavaScript code and call functions
     * within a sandboxed environment with resource limits.
     */
    class SandboxedVm final {
    public:
        /** Configuration limits for the SandboxedVm.
         *  - memory_bytes: Maximum memory usage in bytes (default 32MB).
         *  - timeout_ms: Maximum execution time in milliseconds (default 200ms).
         */
        struct Limits {
            size_t  memory_bytes = 32 * 1024 * 1024; // 32MB
            int     timeout_ms   = 200;              // 200ms wall time
            Limits(const size_t mem = 32 * 1024 * 1024, const int t = 200)
                : memory_bytes(mem), timeout_ms(t) {}
        };
        /** Create a new SandboxedVm with specified limits.
         *  @param lims The resource limits for the VM.
         *  @throws std::runtime_error if QuickJS initialization fails.
         */
        explicit SandboxedVm(const Limits lims = {})
        : limits_(lims),
          rt_(JS_NewRuntime()),
          ctx_(rt_ ? JS_NewContext(rt_) : nullptr),
          start_(std::chrono::steady_clock::now())
        {
            if (!rt_ || !ctx_) throw std::runtime_error("QuickJS init failed");
            JS_SetMemoryLimit(rt_, limits_.memory_bytes);
            JS_SetInterruptHandler(rt_, &SandboxedVm::InterruptHandler, this);
        }
        /** Move constructor */
        SandboxedVm(SandboxedVm&& o) noexcept
            : rt_(o.rt_), ctx_(o.ctx_), limits_(o.limits_), start_(o.start_) {
            o.rt_ = nullptr; o.ctx_ = nullptr;
        }
        /** Destructor to free QuickJS resources */
        ~SandboxedVm() {
            if (ctx_) JS_FreeContext(ctx_);
            if (rt_) JS_FreeRuntime(rt_);
        }
        /** Factory method to create a SandboxedVm instance.
         *  @param lims The resource limits for the VM.
         *  @returns A Result containing the SandboxedVm instance or an error message.
         */
        [[nodiscard]]
        static std::expected<SandboxedVm, std::string> create(Limits lims = {}) noexcept {
            JSRuntime* rt = JS_NewRuntime();
            if (!rt) return std::unexpected("QuickJS: failed to create runtime");

            JSContext* ctx = JS_NewContext(rt);
            if (!ctx) {
                JS_FreeRuntime(rt);
                return std::unexpected("QuickJS: failed to create context");
            }

            auto vm = SandboxedVm(rt, ctx, lims);

            JS_SetMemoryLimit(rt, lims.memory_bytes);
            return std::expected<SandboxedVm, std::string>(std::move(vm));
        }
        /** Evaluate JavaScript code in the global context.
         *  @param code The JavaScript code to evaluate.
         *  @param filename An optional filename for error reporting (default "<utx>").
         *  @returns A Result containing the evaluation result as a string or an error message.
         */
        [[nodiscard]]
        std::expected<std::string, std::string> eval_global(const std::string& code,
                                                const std::string& filename = "<utx>") noexcept {
            JSValue val;
            try {
                reset_timer_();
                LOG_THIS_INFO("Evaluating JS code ({} bytes) with limits: {} bytes memory, {} ms timeout",
                              code.size(), limits_.memory_bytes, limits_.timeout_ms);
                val = JS_Eval(ctx_, code.c_str(), code.size(), filename.c_str(),
                                      JS_EVAL_FLAG_STRICT | JS_EVAL_TYPE_GLOBAL);
            }catch (const std::exception& e) {
                LOG_THIS_ERROR("JS eval threw exception : {}", e.what());
                return std::unexpected(std::string("JS eval exception: ") + e.what());
            } catch (...) {
                LOG_THIS_ERROR("JS eval threw unknown exception");
                return std::unexpected("JS eval unknown exception");
            }

            try {
                LOG_THIS_INFO("Js eval completed in {} ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count());
                if (JS_IsException(val)) {
                    std::string msg = extract_js_exception(ctx_);
                    JS_FreeValue(ctx_, val);
                    return std::unexpected(msg);
                }

                JS_FreeValue(ctx_, val);
                return std::string{};

            } catch (const std::exception& e) {
                JS_FreeValue(ctx_, val);
                LOG_THIS_ERROR("Value conversion failed after JS eval : {}", e.what());
                return std::unexpected(std::string("Failed to convert result to string: ") + e.what());
            } catch (...) {
                JS_FreeValue(ctx_, val);
                LOG_THIS_ERROR("Value conversion threw unknown exception after JS eval");
                return std::unexpected("Unknown evaluation error");
            }
        }
        /** Call a JavaScript function with string arguments.
         *  @param fn The name of the JavaScript function to call.
         *  @param args A vector of string arguments to pass to the function.
         *  @returns A Result containing the function's return value as a string or an error message.
         */
        [[nodiscard]]
        std::expected<std::string, std::string> call_function(const std::string& fn,
                                                  const std::vector<std::string>& args) noexcept {
            reset_timer_();
            const JSValue global = JS_GetGlobalObject(ctx_);
            const JSValue func   = JS_GetPropertyStr(ctx_, global, fn.c_str());
            if (!JS_IsFunction(ctx_, func)) {
                JS_FreeValue(ctx_, func);
                JS_FreeValue(ctx_, global);
                return std::unexpected("Function '" + fn + "' not found or not a function");
            }

            std::vector<JSValue> jsArgs;
            jsArgs.reserve(args.size());
            for (auto& a : args) jsArgs.push_back(JS_NewString(ctx_, a.c_str()));

            const JSValue ret = JS_Call(ctx_, func, JS_UNDEFINED, static_cast<int>(jsArgs.size()), jsArgs.data());

            for (const auto& v : jsArgs) JS_FreeValue(ctx_, v);
            JS_FreeValue(ctx_, func);
            JS_FreeValue(ctx_, global);

            return value_to_string_and_free_(ret);
        }

        std::expected<nlohmann::json, std::string> call_function_json(const std::string& fn,
                                                                   const std::vector<nlohmann::json>& args) noexcept {
            JSContext* ctx = ctx_;
            JSValue global = JS_GetGlobalObject(ctx);

            JSValue fn_val = JS_GetPropertyStr(ctx, global, fn.c_str());
            if (!JS_IsFunction(ctx, fn_val)) {
                JS_FreeValue(ctx, global);
                return std::unexpected("JS function not found: " + fn);
            }

            std::vector<JSValue> js_args;
            js_args.reserve(args.size());
            for (const auto& arg : args) {
                js_args.push_back(json_to_js(arg));
            }

            JSValue result = JS_Call(
                ctx,
                fn_val,
                JS_UNDEFINED,
                js_args.size(),
                js_args.data()
            );

            for (auto& v : js_args) {
                JS_FreeValue(ctx, v);
            }

            JS_FreeValue(ctx, fn_val);
            JS_FreeValue(ctx, global);

            if (JS_IsException(result)) {
                std::string err = dump_exception();
                JS_FreeValue(ctx_, result);
                return std::unexpected(err);
            }

            JSValue json = JS_JSONStringify(ctx, result, JS_UNDEFINED, JS_UNDEFINED);
            JS_FreeValue(ctx, result);

            if (JS_IsException(json)) {
                std::string err = dump_exception();
                JS_FreeValue(ctx, json);
                return std::unexpected(err);
            }

            const char* cstr = JS_ToCString(ctx, json);
            size_t len = strlen(cstr);
            JSValue parsed = JS_ParseJSON(ctx, cstr, len, "<json>");
            JS_FreeCString(ctx, cstr);

            JS_FreeValue(ctx, json);

            nlohmann::json out = js_to_json(parsed);
            JS_FreeValue(ctx, parsed);

            return out;
        }

        JSValue json_to_js(const nlohmann::json& j) {
            if (j.is_null()) {
                return JS_NULL;
            }
            if (j.is_boolean()) {
                return JS_NewBool(ctx_, j.get<bool>());
            }
            if (j.is_number_integer()) {
                return JS_NewInt64(ctx_, j.get<int64_t>());
            }
            if (j.is_number_unsigned()) {
                return JS_NewBigUint64(ctx_, j.get<uint64_t>());
            }
            if (j.is_number_float()) {
                return JS_NewFloat64(ctx_, j.get<double>());
            }
            if (j.is_string()) {
                return JS_NewString(ctx_, j.get<std::string>().c_str());
            }
            if (j.is_array()) {
                JSValue arr = JS_NewArray(ctx_);
                uint32_t i = 0;
                for (const auto& v : j) {
                    JS_SetPropertyUint32(ctx_, arr, i++, json_to_js(v));
                }
                return arr;
            }
            if (j.is_object()) {
                JSValue obj = JS_NewObject(ctx_);
                for (const auto& [k, v] : j.items()) {
                    JS_SetPropertyStr(ctx_, obj, k.c_str(), json_to_js(v));
                }
                return obj;
            }

            return JS_UNDEFINED;
        }

        std::string dump_exception() {
            JSValue exc = JS_GetException(ctx_);

            JSValue msg = JS_GetPropertyStr(ctx_, exc, "message");
            JSValue stack = JS_GetPropertyStr(ctx_, exc, "stack");

            const char* msg_c = JS_ToCString(ctx_, msg);
            const char* stack_c = JS_ToCString(ctx_, stack);

            std::string out;
            if (msg_c) {
                out += msg_c;
            }
            if (stack_c) {
                out += "\n";
                out += stack_c;
            }

            JS_FreeCString(ctx_, msg_c);
            JS_FreeCString(ctx_, stack_c);
            JS_FreeValue(ctx_, msg);
            JS_FreeValue(ctx_, stack);
            JS_FreeValue(ctx_, exc);

            return out.empty() ? "Unknown JS exception" : out;
        }


        JSContext * get_context() const noexcept { return ctx_; }

        nlohmann::json js_to_json(JSValueConst v) {
            if (JS_IsNull(v) || JS_IsUndefined(v)) {
                return nullptr;
            }

            if (JS_IsBool(v)) {
                return JS_ToBool(ctx_, v);
            }

            if (JS_IsNumber(v)) {
                double d;
                JS_ToFloat64(ctx_, &d, v);
                return d;
            }

            if (JS_IsString(v)) {
                const char* str = JS_ToCString(ctx_, v);
                std::string out = str ? str : "";
                JS_FreeCString(ctx_, str);
                return out;
            }

            if (JS_IsArray(ctx_, v)) {
                nlohmann::json arr = nlohmann::json::array();

                JSValue len_val = JS_GetPropertyStr(ctx_, v, "length");
                uint32_t len = 0;
                JS_ToUint32(ctx_, &len, len_val);
                JS_FreeValue(ctx_, len_val);

                for (uint32_t i = 0; i < len; ++i) {
                    JSValue elem = JS_GetPropertyUint32(ctx_, v, i);
                    arr.push_back(js_to_json(elem));
                    JS_FreeValue(ctx_, elem);
                }

                return arr;
            }

            if (JS_IsObject(v)) {
                nlohmann::json obj = nlohmann::json::object();

                JSPropertyEnum* props;
                uint32_t count;
                JS_GetOwnPropertyNames(
                    ctx_, &props, &count, v,
                    JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY
                );

                for (uint32_t i = 0; i < count; ++i) {
                    JSAtom atom = props[i].atom;
                    const char* key = JS_AtomToCString(ctx_, atom);

                    JSValue val = JS_GetProperty(ctx_, v, atom);
                    obj[key] = js_to_json(val);

                    JS_FreeCString(ctx_, key);
                    JS_FreeValue(ctx_, val);
                    JS_FreeAtom(ctx_, atom);
                }

                js_free(ctx_, props);
                return obj;
            }

            return nullptr;
        }


    private:
        JSRuntime* rt_{nullptr};
        JSContext* ctx_{nullptr};
        Limits limits_;
        std::chrono::steady_clock::time_point start_;
        /** Private constructor used by the factory method and move constructor */
        SandboxedVm(JSRuntime* rt, JSContext* ctx, const Limits& lims)
            : rt_(rt), ctx_(ctx), limits_(lims), start_(std::chrono::steady_clock::now()) {
            if (!rt_) {
                LOG_THIS_ERROR("[SandboxedVm] JS_NewRuntime failed");
            }
            if (!ctx_) {
                LOG_THIS_ERROR("[SandboxedVm] JS_NewContext failed");
            }

            if (rt_ && ctx_) {
                JS_SetMemoryLimit(rt_, limits_.memory_bytes);
                JS_SetInterruptHandler(rt_, &SandboxedVm::InterruptHandler, this);
                JS_AddIntrinsicBaseObjects(ctx_);
                JS_AddIntrinsicDate(ctx_);
                JS_AddIntrinsicEval(ctx_);
                JS_AddIntrinsicStringNormalize(ctx_);
                JS_AddIntrinsicRegExp(ctx_);
                JS_AddIntrinsicJSON(ctx_);
                JS_AddIntrinsicProxy(ctx_);
                JS_AddIntrinsicMapSet(ctx_);
                JS_AddIntrinsicTypedArrays(ctx_);
                JS_AddIntrinsicPromise(ctx_);
            }
        }
        /** Interrupt handler to enforce execution time limits.
         *  @param rt The QuickJS runtime.
         *  @param opaque A pointer to the SandboxedVm instance.
         *  @returns 1 if the execution time has exceeded the limit, 0 otherwise.
         */
        static int InterruptHandler(JSRuntime*, void* opaque) {
            const auto* self = static_cast<SandboxedVm*>(opaque);
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - self->start_);
            return (elapsed.count() > self->limits_.timeout_ms) ? 1 : 0;
        }
        /** Reset the execution timer to the current time. */
        void reset_timer_() { start_ = std::chrono::steady_clock::now(); }
        /** Convert a JSValue to a C++ string and free the JSValue.
         *  If the JSValue is an exception, extract the error message.
         *  @param v The JSValue to convert.
         *  @returns A Result containing the string representation or an error message.
         */
        [[nodiscard]]
        std::expected<std::string, std::string>
        value_to_string_and_free_(JSValue v) const noexcept {
            if (JS_IsException(v)) {
                std::string msg = extract_js_exception(ctx_);
                JS_FreeValue(ctx_, v);
                return std::unexpected(std::move(msg));
            }

            JSValue str_val = JS_ToString(ctx_, v);
            JS_FreeValue(ctx_, v);

            if (JS_IsException(str_val)) {
                std::string msg = extract_js_exception(ctx_);
                return std::unexpected(std::move(msg));
            }

            const char* cstr = JS_ToCString(ctx_, str_val);
            std::string out = cstr ? cstr : "";
            if (cstr) JS_FreeCString(ctx_, cstr);
            JS_FreeValue(ctx_, str_val);

            return out;
        }


        std::string extract_js_exception(JSContext* ctx) const {
            JSValue exc = JS_GetException(ctx);

            std::string message = "JS exception";

            // message
            JSValue msg_val = JS_GetPropertyStr(ctx, exc, "message");
            if (!JS_IsUndefined(msg_val)) {
                if (const char* msg = JS_ToCString(ctx, msg_val)) {
                    message = msg;
                    JS_FreeCString(ctx, msg);
                }
            }
            JS_FreeValue(ctx, msg_val);

            // stack
            JSValue stack_val = JS_GetPropertyStr(ctx, exc, "stack");
            if (!JS_IsUndefined(stack_val)) {
                if (const char* stack = JS_ToCString(ctx, stack_val)) {
                    message += "\n";
                    message += stack;
                    JS_FreeCString(ctx, stack);
                }
            }
            JS_FreeValue(ctx, stack_val);

            JS_FreeValue(ctx, exc);
            return message;
        }

    };
} // namespace utx::infra::vm::javascript

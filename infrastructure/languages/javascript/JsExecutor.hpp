#pragma once
#include <string>
#include <vector>
#include "VmPool.hpp"
#include "../../../domain/port/IScriptVm.hpp"


namespace utx::infra::languages::javascript {
    /**
     * JsExecutorSession gère une session avec une VM JavaScript louée.
     */
    class JsExecutorSession {
    public:
        explicit JsExecutorSession(VmPool::Lease lease)
            : lease_(std::move(lease)) {}

        std::expected<std::string, std::string> eval(const std::string& code) noexcept {
            return wrap<std::string>([&]{ return lease_.vm().eval_global(code); });
        }

        std::expected<std::string, std::string> call(const std::string& fn, const std::vector<std::string>& args = {}) noexcept {
            return wrap<std::string>([&]{ return lease_.vm().call_function(fn, args); });
        }

        std::expected<nlohmann::json, std::string> call_json(const std::string& fn, const std::vector<nlohmann::json>& args) noexcept {
            return wrap<nlohmann::json>([&]{
                return lease_.vm().call_function_json(fn, args);
            });

        }

    private:
        VmPool::Lease lease_;

        static JSValue json_to_js(JSContext* ctx, const nlohmann::json& j) {
            if (j.is_null()) {
                return JS_NULL;
            }
            if (j.is_boolean()) {
                return JS_NewBool(ctx, j.get<bool>());
            }
            if (j.is_number_integer()) {
                return JS_NewInt64(ctx, j.get<int64_t>());
            }
            if (j.is_number_float()) {
                return JS_NewFloat64(ctx, j.get<double>());
            }
            if (j.is_string()) {
                return JS_NewString(ctx, j.get<std::string>().c_str());
            }
            if (j.is_array()) {
                JSValue arr = JS_NewArray(ctx);
                uint32_t i = 0;
                for (const auto& v : j) {
                    JS_SetPropertyUint32(ctx, arr, i++, json_to_js(ctx, v));
                }
                return arr;
            }
            if (j.is_object()) {
                JSValue obj = JS_NewObject(ctx);
                for (const auto& [k, v] : j.items()) {
                    JS_SetPropertyStr(ctx, obj, k.c_str(), json_to_js(ctx, v));
                }
                return obj;
            }

            return JS_UNDEFINED;
        }


        /**
         * Helper pour capturer les exceptions et les convertir en std::unexpected.
         */
        template<typename T, typename F>
        std::expected<T, std::string> wrap(F&& f) noexcept {
            try {
                return f();
            } catch (const std::exception& e) {
                return std::unexpected(std::string("Exception: ") + e.what());
            } catch (...) {
                return std::unexpected("Unknown exception occurred");
            }
        }
    };

    /**
     * JsExecutor gère le pool de VMs et implémente l'interface IScriptVm.
     */
    class JsExecutor final : public domain::port::IScriptVm {
    public:
        explicit JsExecutor(VmPool& pool)
            : pool_(pool) {}

        [[nodiscard]]
        JsExecutorSession create_session() const {
            return JsExecutorSession(pool_.acquire());
        }

        [[nodiscard]]
        std::expected<std::string, std::string>
        eval(const std::string& code) noexcept override {
            return execute([&](auto& vm) { return vm.eval_global(code); }, "eval");
        }

        [[nodiscard]]
        std::expected<std::string, std::string>
        call_function(const std::string& fn, const std::vector<std::string>& args) noexcept override {
            return execute([&](auto& vm) { return vm.call_function(fn, args); }, "call");
        }

    private:
        VmPool& pool_;

        /**
         * Logique commune pour acquérir une VM et gérer les erreurs.
         */
        template<typename F>
        std::expected<std::string, std::string> execute(F&& f, const std::string& context) noexcept {
            try {
                auto lease = pool_.acquire();
                return f(lease.vm());
            } catch (const std::exception& ex) {
                return std::unexpected("VM " + context + " error: " + ex.what());
            } catch (...) {
                return std::unexpected("VM " + context + " unknown error");
            }
        }


    };
} // namespace utx::infra::languages::javascript

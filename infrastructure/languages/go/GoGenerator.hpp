#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "../../../domain/graph/GraphElement.hpp"
#include "../../../domain/graph/IGraphVisitor.hpp"

namespace utx::infra::languages::go {

/**
 * GoGenerator reconstructs Go source by preserving the token stream emitted by
 * the Go parser. It mirrors the C++ generator strategy so round-trips stay
 * deterministic and whitespace-sensitive.
 */
class GoGenerator final : public domain::graph::IGraphVisitor {
public:
    struct Options {
        bool prefer_tokens = true;
        bool emit_fallback_value = true;
        bool skip_nil_values = true;
        Options() = default;
    };

    GoGenerator() = default;
    explicit GoGenerator(const Options& opt) : opt_(opt) {}

    void visit(const std::shared_ptr<domain::graph::GraphElement>& element) override {
        if (!element) return;
        emit_node(*element);
    }

    [[nodiscard]] std::string get_result() const override { return output_; }

private:
    void emit_node(const domain::graph::GraphElement& e) {
        const std::string type = prop(e, "type");
        if (type == "__token__") {
            const std::string v = prop(e, "value");
            if (!v.empty()) output_ += v;
            return;
        }

        const bool has_token_child = opt_.prefer_tokens && contains_token_child(e);

        if (!has_token_child && opt_.emit_fallback_value) {
            const std::string v = prop(e, "value");
            if (!v.empty()) {
                output_ += v;
                for (const auto& c : e.children()) emit_node(*c);
                return;
            }
        }

        for (const auto& c : e.children()) {
            if (c) emit_node(*c);
        }
    }

    bool contains_token_child(const domain::graph::GraphElement& e) const {
        for (const auto& c : e.children()) {
            if (!c) continue;
            const std::string ct = prop(*c, "type");
            if (ct == "__token__") return true;
        }
        return false;
    }

    std::string prop(const domain::graph::GraphElement& e, std::string_view key) const {
        std::string v = e.get_property(std::string(key));
        if (opt_.skip_nil_values && v == "nil") return {};
        return v;
    }

    Options opt_;
    std::string output_;
};

} // namespace utx::infra::languages::go

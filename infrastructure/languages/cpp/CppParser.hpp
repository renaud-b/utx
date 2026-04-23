#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/Hash.hpp"
#include "domain/graph/Action.hpp"

namespace utx::infra::languages::cpp {

class CppParser final {
public:
    struct Options {
        bool emit_unnamed_tokens = true;
        bool emit_ranges = true;
        bool trim_whitespace_tokens = false;
        uint32_t max_leaf_value_len = 256;
        Options() = default;
    };

    static std::vector<domain::graph::Action> parse(const std::string& source) {
        return parse(source, Options{});
    }

    static std::vector<domain::graph::Action> parse(const std::string& source, const Options&) {
        const std::string root_path = "//root";
        const std::string root_id = common::md5_hex(root_path);

        std::vector<domain::graph::Action> actions;
        actions.push_back(domain::graph::build_set_action(root_id, "utx.generator", "cpp-fallback"));
        actions.push_back(domain::graph::build_set_action(root_id, "source", source));
        return actions;
    }
};

} // namespace utx::infra::languages::cpp

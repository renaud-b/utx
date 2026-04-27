#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <tree_sitter/api.h>

#include "../../../common/Hash.hpp"
#include "../../../domain/graph/Action.hpp"

extern "C" const TSLanguage* tree_sitter_go();

namespace utx::infra::languages::go {

/**
 * Parse Go source into graph actions using the same deterministic structure
 * as the existing C++ parser.
 */
class GoParser final {
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

    static std::vector<domain::graph::Action> parse(const std::string& source, const Options& opt) {
        TSParser* parser = ts_parser_new();
        ts_parser_set_language(parser, tree_sitter_go());

        TSTree* tree = ts_parser_parse_string(
            parser,
            nullptr,
            source.c_str(),
            static_cast<uint32_t>(source.size())
        );

        const TSNode root_node = ts_tree_root_node(tree);

        std::vector<domain::graph::Action> actions;
        ParseState st;

        const std::string root_path = "//root";
        const std::string root_id = common::md5_hex(root_path);
        st.id_to_path.emplace(root_id, root_path);

        actions.push_back(domain::graph::build_set_action(root_id, "utx.generator", "go"));

        traverse(root_node, source, root_id, actions, st, opt, /*parent_named_id*/ root_id);

        ts_tree_delete(tree);
        ts_parser_delete(parser);

        return actions;
    }

private:
    struct ParseState {
        std::unordered_map<std::string, std::string> id_to_path;
        std::unordered_map<std::string, uint32_t> next_index;
    };

    static std::string node_text(const TSNode node, const std::string& source) {
        if (ts_node_is_null(node)) return {};
        const uint32_t start = ts_node_start_byte(node);
        const uint32_t end = ts_node_end_byte(node);
        if (start >= end || end > source.size()) return {};
        return source.substr(start, end - start);
    }

    static bool is_whitespace_only(std::string_view s) {
        for (char ch : s) {
            switch (ch) {
                case ' ':
                case '\n':
                case '\r':
                case '\t':
                    break;
                default:
                    return false;
            }
        }
        return true;
    }

    static void emit_gap_token(uint32_t from,
                               uint32_t to,
                               const std::string& source,
                               const std::string& parent_id,
                               const std::string& parent_path,
                               std::vector<domain::graph::Action>& actions,
                               ParseState& st,
                               const Options& opt) {
        if (!opt.emit_unnamed_tokens) return;
        if (from >= to || to > source.size()) return;

        std::string gap = source.substr(from, to - from);
        if (gap.empty()) return;
        if (opt.trim_whitespace_tokens && is_whitespace_only(gap)) return;

        const std::string child_name = make_child_name(st, parent_id, "gap", "__token__");
        const std::string child_path = parent_path + "/" + child_name;
        const std::string child_id = id_from_path(child_path);

        st.id_to_path.emplace(child_id, child_path);

        actions.push_back(domain::graph::build_set_action(parent_id, "children", child_name));
        actions.push_back(domain::graph::build_set_action(child_id, "type", "__token__"));
        actions.push_back(domain::graph::build_set_action(child_id, "named", "false"));

        if (gap.size() <= opt.max_leaf_value_len) {
            actions.push_back(domain::graph::build_set_action(child_id, "value", gap));
        } else {
            actions.push_back(domain::graph::build_set_action(child_id, "value", gap.substr(0, opt.max_leaf_value_len)));
            actions.push_back(domain::graph::build_set_action(child_id, "value.truncated", "true"));
        }
    }

    static std::string parent_path_or_root(const ParseState& st, const std::string& parent_id) {
        if (auto it = st.id_to_path.find(parent_id); it != st.id_to_path.end()) return it->second;
        return "//root";
    }

    static std::string make_child_path(const ParseState& st, const std::string& parent_id, const std::string& child_name) {
        return parent_path_or_root(st, parent_id) + "/" + child_name;
    }

    static std::string id_from_path(const std::string& path) {
        return utx::common::md5_hex(path);
    }

    static std::string make_child_name(ParseState& st,
                                       const std::string& parent_id,
                                       std::string_view scope,
                                       std::string_view type) {
        const uint32_t idx = st.next_index[parent_id]++;
        std::string name;
        name.reserve(scope.size() + 1 + type.size() + 1 + 10);
        name.append(scope);
        name.push_back('_');
        name.append(type);
        name.push_back('_');
        name.append(std::to_string(idx));
        return name;
    }

    static void emit_common_node_props(const std::string& id,
                                       const TSNode node,
                                       const std::string& source,
                                       std::vector<domain::graph::Action>& actions,
                                       const Options& opt,
                                       std::string_view type,
                                       bool named,
                                       std::string_view field_name) {
        actions.push_back(domain::graph::build_set_action(id, "type", std::string(type)));
        actions.push_back(domain::graph::build_set_action(id, "named", named ? "true" : "false"));
        if (!field_name.empty()) {
            actions.push_back(domain::graph::build_set_action(id, "field", std::string(field_name)));
        }

        if (opt.emit_ranges) {
            const uint32_t sb = ts_node_start_byte(node);
            const uint32_t eb = ts_node_end_byte(node);
            const TSPoint sp = ts_node_start_point(node);
            const TSPoint ep = ts_node_end_point(node);

            actions.push_back(domain::graph::build_set_action(id, "range.start_byte", std::to_string(sb)));
            actions.push_back(domain::graph::build_set_action(id, "range.end_byte", std::to_string(eb)));
            actions.push_back(domain::graph::build_set_action(id, "range.start_point",
                                                             std::to_string(sp.row) + ":" + std::to_string(sp.column)));
            actions.push_back(domain::graph::build_set_action(id, "range.end_point",
                                                             std::to_string(ep.row) + ":" + std::to_string(ep.column)));
        }

        if (named) {
            bool has_named_child = false;
            const uint32_t n = ts_node_child_count(node);
            for (uint32_t i = 0; i < n; ++i) {
                if (ts_node_is_named(ts_node_child(node, i))) {
                    has_named_child = true;
                    break;
                }
            }

            if (!has_named_child) {
                std::string raw = node_text(node, source);
                if (!raw.empty() && raw.size() <= opt.max_leaf_value_len) {
                    actions.push_back(domain::graph::build_set_action(id, "value", raw));
                }
            }
        }
    }

    static void emit_token_child(const TSNode token_node,
                                 const std::string& source,
                                 const std::string& parent_id,
                                 const std::string& parent_path,
                                 std::vector<domain::graph::Action>& actions,
                                 ParseState& st,
                                 const Options& opt) {
        std::string raw = node_text(token_node, source);
        if (raw.empty()) return;
        if (opt.trim_whitespace_tokens && is_whitespace_only(raw)) return;

        const std::string child_name = make_child_name(st, parent_id, "tok", "__token__");
        const std::string child_path = parent_path + "/" + child_name;
        const std::string child_id = id_from_path(child_path);

        st.id_to_path.emplace(child_id, child_path);

        actions.push_back(domain::graph::build_set_action(parent_id, "children", child_name));
        actions.push_back(domain::graph::build_set_action(child_id, "type", "__token__"));
        actions.push_back(domain::graph::build_set_action(child_id, "named", "false"));

        if (raw.size() <= opt.max_leaf_value_len) {
            actions.push_back(domain::graph::build_set_action(child_id, "value", raw));
        } else {
            actions.push_back(domain::graph::build_set_action(child_id, "value", raw.substr(0, opt.max_leaf_value_len)));
            actions.push_back(domain::graph::build_set_action(child_id, "value.truncated", "true"));
        }
    }

    static void traverse(const TSNode node,
                         const std::string& source,
                         const std::string& parent_id,
                         std::vector<domain::graph::Action>& actions,
                         ParseState& st,
                         const Options& opt,
                         const std::string& parent_named_id) {
        if (ts_node_is_null(node)) return;

        const bool named = ts_node_is_named(node);
        const std::string_view type = ts_node_type(node);

        if (!named) {
            if (opt.emit_unnamed_tokens) {
                const std::string pn_path = parent_path_or_root(st, parent_named_id);
                emit_token_child(node, source, parent_named_id, pn_path, actions, st, opt);
            }
            return;
        }

        traverse_child_named(node, source, parent_id, "", actions, st, opt, parent_named_id);
    }

    static void traverse_child_named(const TSNode node,
                                     const std::string& source,
                                     const std::string& parent_id,
                                     std::string_view field_name,
                                     std::vector<domain::graph::Action>& actions,
                                     ParseState& st,
                                     const Options& opt,
                                     const std::string& parent_named_id) {
        const std::string_view type = ts_node_type(node);

        const std::string scope = field_name.empty() ? "node" : std::string(field_name);
        const std::string child_name = make_child_name(st, parent_id, scope, type);
        const std::string child_path = make_child_path(st, parent_id, child_name);
        const std::string child_id = id_from_path(child_path);

        st.id_to_path.emplace(child_id, child_path);

        actions.push_back(domain::graph::build_set_action(parent_id, "children", child_name));
        emit_common_node_props(child_id, node, source, actions, opt, type, /*named*/ true, field_name);

        const uint32_t n = ts_node_child_count(node);
        uint32_t prev = ts_node_start_byte(node);

        for (uint32_t i = 0; i < n; ++i) {
            const TSNode c = ts_node_child(node, i);
            const uint32_t cs = ts_node_start_byte(c);
            const uint32_t ce = ts_node_end_byte(c);

            if (cs > prev) {
                emit_gap_token(prev, cs, source, child_id, child_path, actions, st, opt);
            }

            const char* fn = ts_node_field_name_for_child(node, i);
            std::string_view c_field = fn ? std::string_view(fn) : std::string_view{};

            if (ts_node_is_named(c)) {
                traverse_child_named(c, source, child_id, c_field, actions, st, opt, child_id);
            } else {
                emit_token_child(c, source, child_id, child_path, actions, st, opt);
            }

            prev = ce;
        }

        const uint32_t end = ts_node_end_byte(node);
        if (prev < end) {
            emit_gap_token(prev, end, source, child_id, child_path, actions, st, opt);
        }
    }

    static void traverse(const TSNode node,
                         const std::string& source,
                         const std::string& parent_id,
                         std::vector<domain::graph::Action>& actions,
                         ParseState& st,
                         const Options& opt,
                         const std::string& parent_named_id,
                         bool /*unused*/) {
        (void)parent_named_id;
        traverse(node, source, parent_id, actions, st, opt, parent_id);
    }

    static void traverse(const TSNode node,
                         const std::string& source,
                         const std::string& parent_id,
                         std::vector<domain::graph::Action>& actions,
                         ParseState& st,
                         const Options& opt,
                         const std::string& parent_named_id,
                         int /*unused*/) {
        (void)parent_named_id;
        traverse(node, source, parent_id, actions, st, opt, parent_id);
    }
};

} // namespace utx::infra::languages::go

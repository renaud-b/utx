#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include <tree_sitter/api.h>

#include "common/Hash.hpp"
#include "domain/graph/Action.hpp"
#include "../../blackhole/BlackholeRebuilder.hpp"
#include "../../blackhole/GraphActionEmitter.hpp"
#include "../javascript/JsParser.hpp"
#include "../css/CssParser.hpp"

extern "C" const TSLanguage *tree_sitter_html();

namespace utx::infra::languages::html {
    class HtmlParser final {
    public:
        static std::vector<domain::graph::Action> parse(const std::string &source) {
            TSParser *parser = ts_parser_new();
            ts_parser_set_language(parser, tree_sitter_html());

            TSTree *tree = ts_parser_parse_string(parser, nullptr, source.c_str(),
                                                  static_cast<uint32_t>(source.size()));
            const TSNode root_node = ts_tree_root_node(tree);

            std::vector<domain::graph::Action> actions;

            ParseState st;

            const std::string root_path = "//root";
            const std::string root_id = common::md5_hex(root_path);
            st.id_to_path.emplace(root_id, root_path);

            traverse_tree(root_node, source, root_id, actions, st);

            ts_tree_delete(tree);
            ts_parser_delete(parser);

            return actions;
        }

    private:
        struct ParseState {
            std::unordered_map<std::string, std::string> id_to_path;
            std::unordered_map<std::string, uint32_t> next_index;
            std::unordered_map<std::string, uint32_t> next_text_index;
        };

        static std::string get_node_text(const TSNode node, const std::string &source) {
            if (ts_node_is_null(node)) return {};
            const uint32_t start = ts_node_start_byte(node);
            const uint32_t end = ts_node_end_byte(node);
            if (start >= end || end > source.size()) return {};
            return source.substr(start, end - start);
        }

        static TSNode find_first_child_of_type(const TSNode node,
                                               const std::initializer_list<std::string_view> &types) {
            const uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                const TSNode c = ts_node_child(node, i);
                const std::string_view t = ts_node_type(c);
                for (const auto want: types) {
                    if (t == want) return c;
                }
            }
            return TSNode{};
        }

        static std::string extract_tag_name(const TSNode start_tag,
                                            const std::string &source) {
            const uint32_t count = ts_node_child_count(start_tag);
            for (uint32_t i = 0; i < count; ++i) {
                const TSNode c = ts_node_child(start_tag, i);
                if (std::string_view(ts_node_type(c)) == "tag_name") {
                    return get_node_text(c, source);
                }
            }
            return "div";
        }

        static std::string make_child_name(const std::string &tag,
                                           ParseState &st,
                                           const std::string &parent_id) {
            const uint32_t idx = st.next_index[parent_id]++;
            return tag + "_" + std::to_string(idx);
        }

        static std::string make_text_child_name(ParseState &st,
                                                const std::string &parent_id) {
            const uint32_t idx = st.next_text_index[parent_id]++;
            return "__text_" + std::to_string(idx);
        }

        static std::string get_parent_path(const ParseState &st,
                                           const std::string &parent_id) {
            if (auto it = st.id_to_path.find(parent_id); it != st.id_to_path.end())
                return it->second;
            return "//root";
        }

        static std::string make_child_path(const ParseState &st,
                                           const std::string &parent_id,
                                           const std::string &child_name) {
            return get_parent_path(st, parent_id) + "/" + child_name;
        }

        static std::string id_from_path(const std::string &path) {
            return utx::common::md5_hex(path);
        }

        static void emit_text_child(const std::string &parent_id,
                                    const std::string &raw,
                                    std::vector<domain::graph::Action> &actions,
                                    ParseState &st) {
            if (raw.empty()) return;

            const std::string child_name = make_text_child_name(st, parent_id);
            const std::string child_path = make_child_path(st, parent_id, child_name);
            const std::string child_id = id_from_path(child_path);

            st.id_to_path.emplace(child_id, child_path);

            actions.push_back(domain::graph::build_set_action(parent_id, "children", child_name));
            actions.push_back(domain::graph::build_set_action(child_id, "value", raw));
        }

        static void traverse_tree(const TSNode node,
                                  const std::string &source,
                                  const std::string &parent_id,
                                  std::vector<domain::graph::Action> &actions,
                                  ParseState &st) {
            const std::string_view type = ts_node_type(node);

            if (type == "document" || type == "fragment") {
                const uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; ++i)
                    traverse_tree(ts_node_child(node, i), source, parent_id, actions, st);
                return;
            }

            if (type == "element" || type == "script_element" || type == "style_element") {
                const TSNode start_tag = find_first_child_of_type(node,
                                                                  {
                                                                      "start_tag", "self_closing_tag",
                                                                      "script_start_tag", "style_start_tag"
                                                                  });

                if (ts_node_is_null(start_tag)) return;

                const std::string tag = extract_tag_name(start_tag, source);

                const std::string child_name = make_child_name(tag, st, parent_id);
                const std::string child_path = make_child_path(st, parent_id, child_name);
                const std::string child_id = id_from_path(child_path);

                st.id_to_path.emplace(child_id, child_path);

                actions.push_back(domain::graph::build_set_action(parent_id, "children", child_name));
                actions.push_back(domain::graph::build_set_action(child_id, "tag", tag));
                // --------- ATTRIBUTES PARSING (restore this) ----------
                const uint32_t st_count = ts_node_child_count(start_tag);
                for (uint32_t i = 0; i < st_count; ++i) {
                    const TSNode c = ts_node_child(start_tag, i);
                    if (std::string_view(ts_node_type(c)) == "attribute") {

                        std::string attr_name;
                        std::string attr_val;

                        const uint32_t ac = ts_node_child_count(c);
                        for (uint32_t j = 0; j < ac; ++j) {
                            const TSNode a = ts_node_child(c, j);
                            const std::string_view at = ts_node_type(a);

                            if (at == "attribute_name") {
                                attr_name = get_node_text(a, source);
                            }
                            else if (at == "attribute_value" ||
                                     at == "quoted_attribute_value" ||
                                     at == "unquoted_attribute_value") {

                                attr_val = get_node_text(a, source);

                                if (attr_val.size() >= 2 &&
                                    ((attr_val.front() == '"' && attr_val.back() == '"') ||
                                     (attr_val.front() == '\'' && attr_val.back() == '\''))) {
                                    attr_val = attr_val.substr(1, attr_val.size() - 2);
                                     }
                                     }
                        }

                        if (!attr_name.empty()) {
                            if (attr_val.empty()) {
                                // boolean attribute
                                actions.push_back(
                                    domain::graph::build_set_action(child_id, "html." + attr_name, "true"));
                            } else {
                                if (attr_name == "class")
                                    actions.push_back(
                                        domain::graph::build_set_action(child_id, "class", attr_val));
                                else
                                    actions.push_back(
                                        domain::graph::build_set_action(child_id, "html." + attr_name, attr_val));
                            }
                        }
                    }
                }

                // ---------- Mixed Content Handling ----------

                const TSNode end_tag = find_first_child_of_type(node,
                                                                {"end_tag", "script_end_tag", "style_end_tag"});

                const uint32_t content_start = ts_node_end_byte(start_tag);
                const uint32_t content_end =
                        ts_node_is_null(end_tag)
                            ? content_start
                            : ts_node_start_byte(end_tag);

                struct Span {
                    TSNode node;
                    uint32_t s;
                    uint32_t e;
                    std::string_view t;
                };

                std::vector<Span> spans;

                const uint32_t count = ts_node_child_count(node);
                for (uint32_t i = 0; i < count; ++i) {
                    const TSNode c = ts_node_child(node, i);
                    const std::string_view ct = ts_node_type(c);

                    if (ct == "start_tag" || ct == "end_tag" ||
                        ct == "self_closing_tag")
                        continue;

                    const uint32_t s = ts_node_start_byte(c);
                    const uint32_t e = ts_node_end_byte(c);

                    if (e <= content_start || s >= content_end) continue;

                    spans.push_back({c, s, e, ct});
                }

                std::sort(spans.begin(), spans.end(),
                          [](const Span &a, const Span &b) { return a.s < b.s; });

                bool seen_element = false;
                std::string prefix;
                std::string pending;

                auto feed = [&](std::string_view seg) {
                    if (seg.empty()) return;
                    if (!seen_element) prefix += seg;
                    else pending += seg;
                };

                auto flush = [&]() {
                    if (pending.empty()) return;
                    emit_text_child(child_id, pending, actions, st);
                    pending.clear();
                };

                uint32_t cursor = content_start;

                for (auto &sp: spans) {
                    if (sp.s > cursor)
                        feed(std::string_view(source).substr(cursor, sp.s - cursor));

                    if (sp.t == "element") {
                        flush();
                        seen_element = true;
                        traverse_tree(sp.node, source, child_id, actions, st);
                    } else {
                        feed(std::string_view(source).substr(sp.s, sp.e - sp.s));
                    }

                    cursor = sp.e;
                }

                if (cursor < content_end)
                    feed(std::string_view(source).substr(cursor, content_end - cursor));

                if (!prefix.empty())
                    actions.push_back(domain::graph::build_set_action(child_id, "value", prefix));

                flush();

                return;
            }

            const uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i)
                traverse_tree(ts_node_child(node, i), source, parent_id, actions, st);
        }
    };
}

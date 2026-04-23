#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <tree_sitter/api.h>
#include "../../../domain/graph/GraphElement.hpp"

extern "C" const TSLanguage* tree_sitter_css();

namespace utx::infra::languages::css {

class CssParser final {
public:
    explicit CssParser(bool debug_mode = false) : debug_(debug_mode) {
        parser_ = ts_parser_new();

        const TSLanguage* lang = tree_sitter_css();
        if (!lang) {
            throw std::runtime_error("tree_sitter_css() returned nullptr. Did you link ts-grammar-css ?");
        }

        if (!ts_parser_set_language(parser_, lang)) {
            throw std::runtime_error("ts_parser_set_language failed for CSS grammar");
        }
    }

    ~CssParser() {
        if (parser_) ts_parser_delete(parser_);
    }

    [[nodiscard]]
    std::shared_ptr<utx::domain::graph::GraphElement> parse(const std::string& source) const {
        TSTree* tree = ts_parser_parse_string(parser_, nullptr, source.c_str(),
                                              static_cast<uint32_t>(source.size()));
        const TSNode root = ts_tree_root_node(tree);

        auto out = std::make_shared<utx::domain::graph::GraphElement>("Stylesheet");
        out->set_property("tag", "stylesheet"); // Sémantique pour le rebuilder
        if (debug_) out->set_property("css.source", source);

        traverse_stylesheet(root, source, out);

        ts_tree_delete(tree);
        return out;
    }

private:
    TSParser* parser_;
    bool debug_;

    // --- Helpers ---

    static std::string node_text(const TSNode node, const std::string& source) {
        if (ts_node_is_null(node)) return {};
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        if (start >= end || end > source.size()) return {};
        return source.substr(start, end - start);
    }

    static std::string trim(std::string s) {
        auto is_space = [](unsigned char c){ return std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [is_space](int ch) {
            return !is_space(ch);
        }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [is_space](int ch) {
            return !is_space(ch);
        }).base(), s.end());
        return s;
    }

    static std::string normalize_css_value(std::string v) {
        v = trim(std::move(v));

        // Remove spaces around commas: "a, b, c" -> "a,b,c"
        std::string out;
        out.reserve(v.size());

        for (size_t i = 0; i < v.size(); ++i) {
            const char c = v[i];
            if (c == ',') {
                // trim trailing spaces already appended
                while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
                out.push_back(',');
                // skip following spaces
                while (i + 1 < v.size() && std::isspace(static_cast<unsigned char>(v[i + 1]))) ++i;
                continue;
            }
            out.push_back(c);
        }

        // Trim again (in case)
        return trim(std::move(out));
    }

    static std::string extract_value_from_declaration_text(const std::string& decl_text) {
        // decl_text is something like "font-family: a, b, c;"
        const auto colon = decl_text.find(':');
        if (colon == std::string::npos) return {};

        // stop at last ';' if present, else to end
        size_t end = decl_text.rfind(';');
        if (end == std::string::npos || end <= colon) end = decl_text.size();

        std::string rhs = decl_text.substr(colon + 1, end - (colon + 1));
        return normalize_css_value(std::move(rhs));
    }
    // --- Core Logic ---

    std::shared_ptr<utx::domain::graph::GraphElement>
parse_keyframes(const TSNode node, const std::string& source) const {

        auto kf = std::make_shared<utx::domain::graph::GraphElement>("Keyframes");
        kf->set_property("tag", "keyframes");

        // nom de l'animation
        TSNode name = ts_node_child_by_field_name(node, "name", 4);
        if (ts_node_is_null(name))
            name = ts_node_named_child(node, 0);

        kf->set_property("name", trim(node_text(name, source)));

        // bloc contenant les keyframes
        uint32_t count = ts_node_child_count(node);

        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);

            if (std::string_view(ts_node_type(child)) == "keyframe_block_list") {

                uint32_t inner = ts_node_child_count(child);

                for (uint32_t j = 0; j < inner; ++j) {
                    TSNode frame = ts_node_child(child, j);
                    std::string_view type = ts_node_type(frame);

                    if (type.find("keyframe") != std::string_view::npos) {
                        kf->add_child(parse_keyframe_block(frame, source));
                    }
                }
            }
        }

        return kf;
    }

    std::shared_ptr<utx::domain::graph::GraphElement>
parse_keyframe_block(const TSNode node, const std::string& source) const {

        auto frame = std::make_shared<utx::domain::graph::GraphElement>("Keyframe");
        frame->set_property("tag", "keyframe");

        TSNode selector = ts_node_child_by_field_name(node, "selector", 8);
        if (ts_node_is_null(selector))
            selector = ts_node_named_child(node, 0);

        frame->set_property("selector", trim(node_text(selector, source)));

        uint32_t count = ts_node_child_count(node);

        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);

            if (std::string_view(ts_node_type(child)) == "block") {

                uint32_t inner = ts_node_child_count(child);

                for (uint32_t j = 0; j < inner; ++j) {
                    TSNode decl = ts_node_child(child, j);

                    if (std::string_view(ts_node_type(decl)) == "declaration") {
                        parse_declaration_into_rule(decl, source, frame);
                    }
                }
            }
        }

        return frame;
    }
    void traverse_stylesheet(const TSNode node, const std::string& source,
                             const std::shared_ptr<utx::domain::graph::GraphElement>& parent) const {
        if (ts_node_is_null(node)) return;

        std::string_view type = ts_node_type(node);

        if (type == "stylesheet" || type == "source_file") {
            uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                traverse_stylesheet(ts_node_child(node, i), source, parent);
            }
        }
        else if (type == "rule_set") {
            parent->add_child(parse_rule_set(node, source));
        }
        else if (type == "media_statement" || type == "at_rule") {
            auto media = parse_media_like(node, source);
            if (media) parent->add_child(media);
        }else if (type == "keyframes_statement") {
            parent->add_child(parse_keyframes(node, source));
        }
    }


    std::shared_ptr<domain::graph::GraphElement>
    parse_rule_set(const TSNode rule_set, const std::string& source) const {
        auto rule = std::make_shared<utx::domain::graph::GraphElement>("Rule");
        rule->set_property("tag", "rule"); // Sémantique pour le rebuilder

        // Extraction robuste du sélecteur : tout ce qui précède le bloc '{'
        TSNode selectors = ts_node_child_by_field_name(rule_set, "selectors", 9); // "selectors" field
        if (ts_node_is_null(selectors)) {
            // Fallback: premier enfant nommé avant le bloc
            selectors = ts_node_named_child(rule_set, 0);
        }

        rule->set_property("selector", trim(node_text(selectors, source)));

        // Traitement du bloc de déclarations
        TSNode block = ts_node_child_by_field_name(rule_set, "block", 5);
        if (ts_node_is_null(block)) {
            // Recherche manuelle du nœud "block" ou "declaration_block"
            uint32_t count = ts_node_child_count(rule_set);
            for(uint32_t i=0; i<count; ++i) {
                if (std::string_view(ts_node_type(ts_node_child(rule_set, i))).find("block") != std::string::npos) {
                    block = ts_node_child(rule_set, i);
                    break;
                }
            }
        }

        if (!ts_node_is_null(block)) {
            uint32_t count = ts_node_child_count(block);
            for (uint32_t i = 0; i < count; ++i) {
                TSNode child = ts_node_child(block, i);
                if (std::string_view(ts_node_type(child)) == "declaration") {
                    parse_declaration_into_rule(child, source, rule);
                }
            }
        }

        return rule;
    }

    void parse_declaration_into_rule(const TSNode decl, const std::string& source,
                                     const std::shared_ptr<utx::domain::graph::GraphElement>& rule) const {
        TSNode prop_node = ts_node_child_by_field_name(decl, "name", 4);
        TSNode val_node = ts_node_child_by_field_name(decl, "value", 5);

        // Si les "fields" ne marchent pas avec ta version de la grammaire :
        if (ts_node_is_null(prop_node)) {
            prop_node = ts_node_child(decl, 0);
        }

        std::string name = trim(node_text(prop_node, source));

        // Pour la valeur, on prend souvent le reste après les ':'
        std::string value;

        {
            const std::string decl_text = node_text(decl, source);
            value = extract_value_from_declaration_text(decl_text);
        }

        // Fallback if something went weird (keep your previous logic as backup)
        if (value.empty() && !ts_node_is_null(val_node)) {
            value = normalize_css_value(node_text(val_node, source));
        }

        if (!name.empty()) {
            rule->set_property(name, value.empty() ? "inherit" : value);
        }
    }

    std::shared_ptr<utx::domain::graph::GraphElement>
    parse_media_like(const TSNode node, const std::string& source) const {
        std::string_view type = ts_node_type(node);
        std::string raw_text = node_text(node, source);

        if (type == "at_rule" && raw_text.find("@media") == std::string::npos) {
            return nullptr; // On ignore les @import, @charset, etc.
        }

        auto media = std::make_shared<utx::domain::graph::GraphElement>("Media");
        media->set_property("at", "media");
        media->set_property("tag", "media"); // Sémantique pour le rebuilder

        // Recherche de la condition (ex: screen and (max-width: 600px))
        uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);
            std::string_view ctype = ts_node_type(child);

            if (ctype.find("query") != std::string_view::npos || ctype == "feature_query") {
                media->set_property("condition", trim(node_text(child, source)));
            }

            if (ctype == "block" || ctype == "declaration_block") {
                // Récursivité : on traite les règles à l'intérieur du @media
                uint32_t inner_count = ts_node_child_count(child);
                for (uint32_t j = 0; j < inner_count; ++j) {
                    TSNode inner = ts_node_child(child, j);
                    if (std::string_view(ts_node_type(inner)) == "rule_set") {
                        media->add_child(parse_rule_set(inner, source));
                    }
                }
            }
        }

        return media;
    }
};

} // namespace utx::infra::languages::css

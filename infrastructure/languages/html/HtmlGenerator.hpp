#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <sstream>

#include "../../../domain/graph/GraphElement.hpp"
#include "../../../domain/graph/IGraphVisitor.hpp"
#include "../javascript/JsGenerator.hpp"
#include "../css/CssGenerator.hpp"

namespace utx::infra::languages::html {

class HtmlGenerator final : public domain::graph::IGraphVisitor {
public:
    HtmlGenerator() = default;

    void visit(const std::shared_ptr<domain::graph::GraphElement>& element) override {
        if (!element) return;

        std::string tag = element->get_property("tag");
        if (tag == "nil") tag.clear();

        std::string val = element->get_property("value");
        if (val == "nil") val.clear();

        // 1. Nœud Texte pur (Pas de tag, juste de la valeur)
        if (tag.empty() && !val.empty()) {
            output_ += val;
            for (const auto& child : element->children()) visit(child);
            return;
        }

        // 2. Cas particuliers : JS et CSS
        std::string gen = element->get_property("utx.generator");
        if (gen == "js" || gen == "css") {
            std::string real_tag = (gen == "js") ? "script" : "style";

            output_ += "<" + real_tag;
            render_attributes(element); // Gère les attributs une seule fois
            output_ += ">";

            if (gen == "js") {
                const auto js_generator = javascript::JsGenerator();
                std::ostringstream js_collector;
                js_generator.convert_node_to_js(*element, js_collector);
                output_ += js_collector.str();
            } else {
                const auto css_generator = css::CssGenerator();
                std::ostringstream css_collector;
                css_generator.convert_node_to_css(*element, css_collector);
                output_ += css_collector.str();
            }

            output_ += "</" + real_tag + ">";
            return;
        }

        // 3. Passthrough (Conteneur logique sans rendu)
        if (tag.empty() && should_passthrough(*element)) {
            for (const auto& child : element->children()) visit(child);
            return;
        }

        // 4. Rendu HTML Standard
        if (tag.empty()) tag = "div";
        if (tag == "html" && output_.empty()) {
            output_ += "<!DOCTYPE html>";
        }

        output_ += "<" + tag;
        render_attributes(element); // SEUL appel pour les attributs (class inclus)

        // Finalisation de la structure
        if (element->children().empty()) {
            if (val.empty() && auto_closing_tags_.contains(tag)) {
                output_ += "/>";
            } else {
                output_ += ">" + val + "</" + tag + ">";
            }
        } else {
            output_ += ">" + val;
            for (const auto& child : element->children()) {
                visit(child);
            }
            output_ += "</" + tag + ">";
        }
    }

    [[nodiscard]] std::string get_result() const override { return output_; }

private:
    /**
     * @brief Point unique de génération des attributs (Souveraineté des données)
     */
    void render_attributes(const std::shared_ptr<domain::graph::GraphElement>& element) {
        // Rendu de la classe
        std::string cls = element->get_property("class");
        if (!cls.empty() && cls != "nil") {
            output_ += " class=\"" + cls + "\"";
        }

        // Rendu des attributs html.xxx
        for (const auto& [key, value] : element->properties()) {
            if (value == "nil") continue;

            std::string attr_name;
            if (key.starts_with("html.")) attr_name = key.substr(5);
            else if (key.starts_with("html-")) attr_name = key.substr(5);
            else continue;

            if (attr_name == "class") continue; // Déjà géré au-dessus

            if (value == "true") {
                output_ += " " + attr_name;
            } else {
                const char quote = (value.find('"') != std::string::npos) ? '\'' : '"';
                output_ += " " + attr_name + "=" + quote + value + quote;
            }
        }
    }

    static bool should_passthrough(const domain::graph::GraphElement& e) {
        const std::string tag = e.get_property("tag");
        if (!tag.empty() && tag != "nil") return false;
        const std::string cls = e.get_property("class");
        if (!cls.empty() && cls != "nil") return false;
        const std::string val = e.get_property("value");
        if (!val.empty() && val != "nil") return false;

        for (const auto& [k, v] : e.properties()) {
            if (v == "nil") continue;
            if (k.starts_with("html.") || k.starts_with("html-")) return false;
        }
        return true;
    }

    std::string output_;
    const std::unordered_set<std::string> auto_closing_tags_ = {
        "area", "base", "br", "col", "embed", "hr", "img", "input",
        "link", "meta", "param", "source", "track", "wbr"
    };
};

} // namespace utx::infra::languages::html
#pragma once

#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include "../../../domain/graph/GraphElement.hpp"
#include "../../../domain/graph/IGraphVisitor.hpp"

namespace utx::infra::languages::css {

/**
 * @brief Génère du code CSS à partir d'un arbre de GraphElement.
 * * Conventions de structure :
 * - Nœud "Stylesheet" : Conteneur racine, on visite simplement les enfants.
 * - Nœud "Rule" : Représente un bloc de style (sélecteur + propriétés).
 * - Nœud "Media" : Représente une règle @media.
 */
class CssGenerator final : public domain::graph::IGraphVisitor {
public:
    CssGenerator() = default;

    std::string render_keyframes(const domain::graph::GraphElement& e) const {
        std::string name = e.get_property("name");

        if (name.empty()) return "";

        std::ostringstream oss;

        oss << "@keyframes " << name << "{";

        for (const auto& child : e.children()) {
            oss << render_element(child);
        }

        oss << "}";

        return oss.str();
    }

    std::string render_keyframe(const domain::graph::GraphElement& e) const {
        std::string selector = e.get_property("selector");
        if (selector.empty()) return "";

        std::ostringstream oss;

        oss << selector << "{";

        for (const auto& [prop, val] : e.properties()) {
            if (prop == "selector" || prop == "tag" || val == "nil")
                continue;

            oss << prop << ":" << val << ";";
        }

        oss << "}";

        return oss.str();
    }
    /**
     * @brief Point d'entrée pour convertir un nœud et ses enfants en CSS.
     */
    void convert_node_to_css(const domain::graph::GraphElement& element, std::ostream& os) const {
        // On récupère la sémantique via la propriété "tag"
        std::string tag = element.get_property("tag");

        if (tag == "stylesheet") {
            for (const auto& child : element.children()) {
                os << render_element(child);
            }
        } else if (tag == "rule") {
            os << render_rule(element);
        } else if (tag == "media") {
            os << render_media(element);
        } else if (tag == "keyframes") {
            os << render_keyframes(element);
        } else if (tag == "keyframe") {
            os << render_keyframe(element);
        } else {
            // Passthrough pour le root technique ou les fragments
            for (const auto& child : element.children()) {
                os << render_element(child);
            }
        }
    }

    /**
     * @brief Implémentation de l'interface IGraphVisitor.
     */
    void visit(const std::shared_ptr<domain::graph::GraphElement>& element) override {
        if (!element) return;
        std::ostringstream oss;
        convert_node_to_css(*element, oss);
        output_ += oss.str();
    }

    [[nodiscard]] std::string get_result() const override { return output_; }

private:
    std::string render_element(const std::shared_ptr<domain::graph::GraphElement>& e) const {
        if (!e) return "";
        std::ostringstream oss;
        convert_node_to_css(*e, oss);
        return oss.str();
    }

    std::string render_rule(const domain::graph::GraphElement& e) const {
        std::string selector = e.get_property("selector");
        if (selector.empty()) return "";

        std::ostringstream oss;
        oss << selector << "{";

        for (const auto& [prop, val] : e.properties()) {
            // On ignore les métadonnées techniques du graphe
            if (prop == "selector" || prop == "tag" || prop == "utx.generator" || val == "nil") {
                continue;
            }
            oss << prop << ":" << val << ";";
        }

        oss << "}";
        return oss.str();
    }

    std::string render_media(const domain::graph::GraphElement& e) const {
        std::string condition = e.get_property("condition");
        std::ostringstream oss;

        oss << "@media " << (condition.empty() ? "all" : condition) << "{";

        // Un @media contient généralement des Rules comme enfants
        for (const auto& child : e.children()) {
            oss << render_element(child);
        }

        oss << "}";
        return oss.str();
    }

    std::string output_;
};

} // namespace utx::infra::languages::css
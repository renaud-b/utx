#include "JsParser.hpp"
#include <tree_sitter/api.h>
#include "common/Logger.hpp"
#include "JsNodeFactory.hpp"

extern "C" {
    TSLanguage *tree_sitter_javascript();
}

using namespace utx::infra::languages::javascript;
using namespace utx::domain::graph;

/** Set of token types to ignore during parsing. */
const std::set<std::string> JsParser::IGNORED_TOKENS = {
    "var", "let", "const", "else", "finally", "catch", "try", "throw", "get",
    "for", "while", "in", "new", "if", "switch", "case", "break", "default",
    "static", "function", "return", "extends",
    "(", ")", "{", "}", "[", "]", "?",
    ":", ";", ",", ".", "...", "=>"
};
/** Construct a JsParser with optional debug mode.
 * @param debug_mode If true, includes additional debug information in the graph elements.
 */
JsParser::JsParser(const bool debug_mode) : is_debug_mode_(debug_mode) {
    parser_ = ts_parser_new();
    ts_parser_set_language(parser_, tree_sitter_javascript());


    auto gap_contains_optional = [this](const TSNode& left, const TSNode& right, const std::string& src) -> bool {
        if (ts_node_is_null(left) || ts_node_is_null(right)) return false;

        const auto l_end = ts_node_end_byte(left);
        const auto r_start = ts_node_start_byte(right);

        if (r_start <= l_end) return false;

        const std::string gap = src.substr(l_end, r_start - l_end);

        // On veut détecter exactement l’optional chaining operator
        // Ça peut être "?.", ou "?.[" ou "?.(" selon la forme.
        return gap.find("?.") != std::string::npos;
    };




    auto& f = JsNodeFactory::instance();

    f.register_handler("ternary_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ConditionalExpression");
        element->set_property("type", "ConditionalExpression");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const TSNode child = ts_node_child(node, i);
            if (const auto target = process_node(child, source)) element->add_child(target);
        }

        return element;
    });
    f.register_handler("augmented_assignment_expression", [this](const TSNode& node, const std::string& source) {
        auto element = std::make_shared<GraphElement>("AssignmentExpression");
        element->set_property("type", "AssignmentExpression");

        // Tree-sitter: left (0), operator (1), right (2)
        element->set_property("operator", get_node_text(ts_node_child(node, 1), source));

        TSNode l_node = ts_node_child_by_field_name(node, "left", 4);
        if (ts_node_is_null(l_node)) l_node = ts_node_child(node, 0);

        TSNode r_node = ts_node_child_by_field_name(node, "right", 5);
        if (ts_node_is_null(r_node)) r_node = ts_node_child(node, 2);

        if (auto left = process_node(l_node, source)) element->add_child(left);
        if (auto right = process_node(r_node, source)) element->add_child(right);

        return element;
    });
    f.register_handler("binary_expression", [this](const TSNode& node, const std::string& source) {
        auto element = std::make_shared<GraphElement>("BinaryExpression");
        element->set_property("type", "BinaryExpression");
        element->set_property("operator", get_node_text(ts_node_child(node, 1), source));

        TSNode l_node = ts_node_child_by_field_name(node, "left", 4);
        if (ts_node_is_null(l_node)) l_node = ts_node_child(node, 0);

        TSNode r_node = ts_node_child_by_field_name(node, "right", 5);
        if (ts_node_is_null(r_node)) r_node = ts_node_child(node, 2);

        if (auto left = process_node(l_node, source)) element->add_child(left);
        if (auto right = process_node(r_node, source)) element->add_child(right);
        return element;
    });
    f.register_handler("parenthesized_expression", [this](const TSNode& node, const std::string& source) {
         const auto element = std::make_shared<GraphElement>("ParenthesizedExpression");
         element->set_property("type", "ParenthesizedExpression");

         const uint32_t n = ts_node_child_count(node);
         for (uint32_t i = 0; i < n; ++i) {
             TSNode ch = ts_node_child(node, i);
             if (!ts_node_is_named(ch)) continue;
             if (auto inner = process_node(ch, source)) element->add_child(inner);
             break;
         }
         return element;
     });
    f.register_handler("formal_parameters", [this](const TSNode& node, const std::string& source) {
        const auto params = std::make_shared<GraphElement>("ParameterList");
        params->set_property("type", "ParameterList");

        const uint32_t n = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < n; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (auto target = process_node(child, source)) params->add_child(target);
        }
        return params;
    });

    f.register_handler("shorthand_property_identifier", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("PropertyShort");
        element->set_property("type", "PropertyShort");

        // FIX : On cherche le nœud 'identifier' à l'intérieur du shorthand
        TSNode target_node = ts_node_named_child(node, 0);
        if (ts_node_is_null(target_node)) target_node = node; // Fallback au cas où

        const auto start = ts_node_start_byte(target_node);
        const auto end   = ts_node_end_byte(target_node);

        // On s'assure que la plage est valide avant le substr
        if (end > start) {
            element->set_property("value", source.substr(start, end - start));
        } else {
            // Si c'est toujours vide, on tente un dernier recours via le texte du nœud parent
            element->set_property("value", source.substr(ts_node_start_byte(node),
                                                       ts_node_end_byte(node) - ts_node_start_byte(node)));
        }

        return element;
    });
    f.register_handler("shorthand_property_identifier_pattern", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("PropertyShort");
        element->set_property("type", "PropertyShort");

        // FIX : On cherche le nœud 'identifier' à l'intérieur du shorthand
        TSNode target_node = ts_node_named_child(node, 0);
        if (ts_node_is_null(target_node)) target_node = node; // Fallback au cas où

        const auto start = ts_node_start_byte(target_node);
        const auto end   = ts_node_end_byte(target_node);

        // On s'assure que la plage est valide avant le substr
        if (end > start) {
            element->set_property("value", source.substr(start, end - start));
        } else {
            // Si c'est toujours vide, on tente un dernier recours via le texte du nœud parent
            element->set_property("value", source.substr(ts_node_start_byte(node),
                                                       ts_node_end_byte(node) - ts_node_start_byte(node)));
        }

        return element;
    });
    f.register_handler("pair", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("PropertyKeyed");
        element->set_property("type", "PropertyKeyed");

        // On récupère les enfants par leurs noms définis dans la grammaire Tree-sitter
        const auto key_node = ts_node_child_by_field_name(node, "key", 3);
        const auto value_node = ts_node_child_by_field_name(node, "value", 5);

        const auto key = process_node(key_node, source);
        const auto value = process_node(value_node, source);

        if (key) element->add_child(key);
        if (value) element->add_child(value);

        element->set_property("computed", "false");
        element->set_property("kind", "init");
        return element;
    });
    // Correction du CallExpression pour être plus robuste
    f.register_handler("call_expression", [this, gap_contains_optional](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("CallExpression");
        element->set_property("type", "CallExpression");

        TSNode callee_node = ts_node_child_by_field_name(node, "function", 8);
        if (ts_node_is_null(callee_node)) callee_node = ts_node_child(node, 0);

        TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);

        if (!ts_node_is_null(args_node) && gap_contains_optional(callee_node, args_node, source)) {
            element->set_property("optional", "true");
        }

        if (auto callee = process_node(callee_node, source)) element->add_child(callee);

        if (!ts_node_is_null(args_node)) {
            const auto args_element = std::make_shared<GraphElement>("ArgumentList");
            args_element->set_property("type", "ArgumentList");

            const uint32_t arg_count = ts_node_child_count(args_node);
            for (uint32_t i = 0; i < arg_count; i++) {
                if (const auto arg = process_node(ts_node_child(args_node, i), source)) {
                    args_element->add_child(arg);
                }
            }
            element->add_child(args_element);
        }
        return element;
    });

    f.register_handler("undefined", [this](const TSNode&, const std::string&) {
        const auto element = std::make_shared<GraphElement>("UndefinedLiteral");
        element->set_property("type", "UndefinedLiteral");
        return element;
    });

    f.register_handler("function_expression", [this](const TSNode& node, const std::string& source) {
        const auto expr = std::make_shared<GraphElement>("FunctionExpression");
        expr->set_property("type", "FunctionExpression");

        // async ?
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const TSNode child = ts_node_child(node, i);
            if (std::string(ts_node_type(child)) == "async") {
                expr->set_property("async", "true");
                break;
            }
        }

        // name / params / body via fields if present (more robust than raw indices)
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);

        if (!ts_node_is_null(name_node)) {
            if (auto n = process_node(name_node, source)) expr->add_child(n);
        }

        if (!ts_node_is_null(params_node)) {
            if (auto p = process_node(params_node, source)) expr->add_child(p);
        }

        if (!ts_node_is_null(body_node)) {
            if (auto b = process_node(body_node, source)) expr->add_child(b);
        }

        return expr;
    });


    f.register_handler("await_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("AwaitExpression");
        element->set_property("type", "AwaitExpression");
        element->set_property("isAwait", "true");

        // L'expression attendue est l'enfant (généralement après le mot-clé 'await')
        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            TSNode child = ts_node_child(node, i);
            if (std::string(ts_node_type(child)) != "await") {
                if (const auto expr = process_node(child, source)) {
                    element->add_child(expr);
                }
            }
        }
        return element;
    });
    f.register_handler("number", [this](const TSNode& node, const std::string& source) {
        const std::string literal = source.substr(
            ts_node_start_byte(node),
            ts_node_end_byte(node) - ts_node_start_byte(node)
        );

        const auto number = std::make_shared<GraphElement>(literal);
        number->set_property("type", "NumberLiteral");

        if (literal.find('.') != std::string::npos) {
            number->set_property("value", literal);
            number->set_property("subType", "float64");
        } else {
            number->set_property("value", literal);
            number->set_property("subType", "int64");
        }
        return number;
    });
    f.register_handler("false", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("BooleanLiteral");
        element->set_property("type", "BooleanLiteral");
        element->set_property("value", "false");
        return element;
    });
    f.register_handler("string", [this](const TSNode& node, const std::string& source) {
        const std::string literal = source.substr(
            ts_node_start_byte(node),
            ts_node_end_byte(node) - ts_node_start_byte(node)
        );
        const auto element = std::make_shared<GraphElement>(literal);
        element->set_property("type", "StringLiteral");
        element->set_property("value", literal);
        return element;
    });
    f.register_handler("template_string", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("TemplateLiteral");
        element->set_property("type", "TemplateLiteral");

        const uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; i++) {
            const TSNode child = ts_node_child(node, i);
            const std::string child_type = ts_node_type(child);

            // On ignore les backticks de début et de fin
            if (child_type == "`") continue;

            if (child_type == "template_substitution") {
                for (uint32_t j = 0; j < ts_node_child_count(child); j++) {
                    TSNode sub_child = ts_node_child(child, j);
                    if (ts_node_is_named(sub_child)) {
                        if (auto expr = process_node(sub_child, source)) {
                            expr->set_property("isSubstitution", "true");
                            element->add_child(expr);
                        }
                    }
                }
            } else {
                // C'est du texte statique (template_chars, string_fragment, ou même un simple ":")
                // On le capture comme un TemplatePart
                auto part = std::make_shared<GraphElement>("TemplatePart");
                part->set_property("type", "TemplatePart");
                part->set_property("value", source.substr(
                    ts_node_start_byte(child),
                    ts_node_end_byte(child) - ts_node_start_byte(child)
                ));
                element->add_child(part);
            }
        }
        return element;
    });
    f.register_handler("member_expression", [this, gap_contains_optional](const TSNode& node, const std::string& source) {
        auto element = std::make_shared<GraphElement>("MemberExpression");
        element->set_property("type", "MemberExpression");

        TSNode obj_node = ts_node_child_by_field_name(node, "object", 6);
        if (ts_node_is_null(obj_node)) obj_node = ts_node_child(node, 0);

        TSNode prop_node = ts_node_child_by_field_name(node, "property", 8);
        if (ts_node_is_null(prop_node)) prop_node = ts_node_child(node, 2);

        if (gap_contains_optional(obj_node, prop_node, source)) {
            element->set_property("optional", "true");
        }

        if (auto obj = process_node(obj_node, source)) element->add_child(obj);
        if (auto prop = process_node(prop_node, source)) element->add_child(prop);

        return element;
    });



    f.register_handler("identifier", [this](const TSNode& node, const std::string& source) {
        const auto value = source.substr(
            ts_node_start_byte(node),
            ts_node_end_byte(node) - ts_node_start_byte(node)
        );

        const auto element = std::make_shared<GraphElement>("Identifier");
        element->set_property("type", "Identifier");
        element->set_property("value", value);

        const auto node_child_count = ts_node_child_count(node);
        if (node_child_count > 0) {
            for (uint32_t i = 0; i < node_child_count; i++) {
                TSNode child = ts_node_child(node, i);
                const auto child_element = process_node(child, source);
                if (child_element) element->add_child(child_element);
            }
        }
        return element;
    });
    f.register_handler("subscript_expression", [this, gap_contains_optional](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("BracketExpression");
        element->set_property("type", "BracketExpression");

        TSNode obj_node = ts_node_child_by_field_name(node, "object", 6);
        if (ts_node_is_null(obj_node)) obj_node = ts_node_child(node, 0);

        TSNode idx_node = ts_node_child_by_field_name(node, "index", 5);
        if (ts_node_is_null(idx_node)) {
            // fallback : souvent l’index est au milieu, on cherche le 1er enfant "nommé" qui n’est pas l’objet
            const uint32_t c = ts_node_child_count(node);
            for (uint32_t i = 0; i < c; ++i) {
                TSNode ch = ts_node_child(node, i);
                if (!ts_node_is_named(ch)) continue;
                if (ts_node_start_byte(ch) >= ts_node_end_byte(obj_node)) { idx_node = ch; break; }
            }
        }

        if (gap_contains_optional(obj_node, idx_node, source)) {
            element->set_property("optional", "true");
        }

        // on garde ton parsing existant (2 enfants: object + index)
        if (auto obj = process_node(obj_node, source)) element->add_child(obj);
        if (auto idx = process_node(idx_node, source)) element->add_child(idx);

        return element;
    });



    f.register_handler("unary_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("UnaryExpression");
        element->set_property("type", "UnaryExpression");

        const uint32_t n = ts_node_child_count(node);
        std::string op;
        TSNode operand{};

        if (n >= 2) {
            const TSNode c0 = ts_node_child(node, 0);
            const std::string c0t = ts_node_type(c0);

            if (c0t == "!" || c0t == "~" || c0t == "+" || c0t == "-" ||
                c0t == "typeof" || c0t == "void" || c0t == "delete") {
                op = c0t;
                operand = ts_node_child(node, 1);
                element->set_property("postfix", "false");
            }
        }

        if (op.empty() && n > 0) {
            const TSNode c0 = ts_node_child(node, 0);
            op = ts_node_type(c0);
            if (n >= 2) operand = ts_node_child(node, 1);
        }

        element->set_property("operator", op);
        if (!ts_node_is_null(operand)) {
            if (const auto arg = process_node(operand, source)) element->add_child(arg);
        }
        return element;
    });
    f.register_handler("update_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("UnaryExpression"); // rétro-compat Go
        element->set_property("type", "UnaryExpression");

        const uint32_t n = ts_node_child_count(node);
        std::string op;
        bool postfix = false;
        TSNode operand{};

        if (n >= 2) {
            const TSNode first = ts_node_child(node, 0);
            const TSNode last  = ts_node_child(node, n - 1);
            const std::string ft = ts_node_type(first);
            const std::string lt = ts_node_type(last);

            if (ft == "++" || ft == "--") {
                op = ft;
                postfix = false;
                operand = ts_node_child(node, 1);
            } else if (lt == "++" || lt == "--") {
                op = lt;
                postfix = true;
                operand = ts_node_child(node, 0);
            } else {
                operand = ts_node_child(node, 0);
            }
        }

        element->set_property("operator", op);
        element->set_property("postfix", postfix ? "true" : "false");

        if (!ts_node_is_null(operand)) {
            if (auto arg = process_node(operand, source)) element->add_child(arg);
        }
        return element;
    });
    f.register_handler("conditional_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ConditionalExpression");
        element->set_property("type", "ConditionalExpression");

        const auto test = process_node(ts_node_child(node, 0), source);
        const auto consequent = process_node(ts_node_child(node, 2), source);
        const auto alternate = process_node(ts_node_child(node, 4), source);

        element->add_child(test);
        element->add_child(consequent);
        element->add_child(alternate);

        return element;
    });
    f.register_handler("assignment_expression", [this](const TSNode& node, const std::string& source) {
        auto element = std::make_shared<GraphElement>("AssignmentExpression");
        element->set_property("type", "AssignmentExpression");

        // L'opérateur est toujours entre le left et le right
        element->set_property("operator", get_node_text(ts_node_child(node, 1), source));

        // On essaie par champ, sinon par index (0 et 2)
        TSNode l_node = ts_node_child_by_field_name(node, "left", 4);
        if (ts_node_is_null(l_node)) l_node = ts_node_child(node, 0);

        TSNode r_node = ts_node_child_by_field_name(node, "right", 5);
        if (ts_node_is_null(r_node)) r_node = ts_node_child(node, 2);

        if (auto left = process_node(l_node, source)) element->add_child(left);
        if (auto right = process_node(r_node, source)) element->add_child(right);

        return element;
    });
    // --------------------
    // Class fields (static BASE = 1;)
    // --------------------
    auto register_class_field = [&](const std::string& node_type) {
        JsNodeFactory::instance().register_handler(node_type, [this](const TSNode& node, const std::string& source) {
            const auto element = std::make_shared<GraphElement>("FieldDefinition");
            element->set_property("type", "FieldDefinition");

            // Detect modifiers / flags that are tokens (static, public, private, etc.)
            const uint32_t count = ts_node_child_count(node);
            for (uint32_t i = 0; i < count; ++i) {
                const TSNode ch = ts_node_child(node, i);
                const std::string t = ts_node_type(ch);

                if (t == "static") element->set_property("static", "true");
                if (t == "public" || t == "private" || t == "protected") element->set_property("access", t);
            }

            // Key / Name (field name)
            // tree-sitter JS typically exposes "property" field for field_definition / public_field_definition
            TSNode name_node = ts_node_child_by_field_name(node, "property", 8);
            if (ts_node_is_null(name_node)) name_node = ts_node_child_by_field_name(node, "name", 4);

            // If fields above are not available, fallback: first named child that looks like an identifier/property_identifier
            if (ts_node_is_null(name_node)) {
                const uint32_t named = ts_node_named_child_count(node);
                for (uint32_t i = 0; i < named; ++i) {
                    TSNode nch = ts_node_named_child(node, i);
                    const std::string nt = ts_node_type(nch);
                    if (nt == "property_identifier" || nt == "identifier" || nt == "private_property_identifier") {
                        name_node = nch;
                        break;
                    }
                }
            }

            if (!ts_node_is_null(name_node)) {
                if (auto n = process_node(name_node, source)) element->add_child(n);
            }

            // Initializer ( "= expr" ) may be in field "value" or as a named child expression.
            TSNode value_node = ts_node_child_by_field_name(node, "value", 5);
            if (ts_node_is_null(value_node)) value_node = ts_node_child_by_field_name(node, "initializer", 11);

            if (ts_node_is_null(value_node)) {
                // Fallback: pick the last named child that is NOT the name
                const uint32_t named = ts_node_named_child_count(node);
                for (int i = (int)named - 1; i >= 0; --i) {
                    TSNode nch = ts_node_named_child(node, (uint32_t)i);
                    if (ts_node_is_null(nch)) continue;
                    if (!ts_node_is_null(name_node) &&
                        ts_node_start_byte(nch) == ts_node_start_byte(name_node) &&
                        ts_node_end_byte(nch) == ts_node_end_byte(name_node)) {
                        continue;
                    }

                    // initializer is usually an expression node (number/string/call/member/etc.)
                    const std::string nt = ts_node_type(nch);
                    if (nt != "property_identifier" && nt != "identifier" && nt != "private_property_identifier") {
                        value_node = nch;
                        break;
                    }
                }
            }

            if (!ts_node_is_null(value_node)) {
                if (auto v = process_node(value_node, source)) element->add_child(v);
            }

            return element;
        });
    };

    // Register both variants depending on grammar version
    register_class_field("field_definition");
    register_class_field("public_field_definition");


    // --------------------
    // Method definition: add getter/setter/generator info
    // --------------------
    JsNodeFactory::instance().register_handler("method_definition", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("MethodDefinition");
        element->set_property("type", "MethodDefinition");

        // default method kind
        element->set_property("kind", "method");

        const uint32_t count = ts_node_child_count(node);
        // --- tokens / modifiers ---
        for (uint32_t i = 0; i < count; i++) {
            const TSNode ch = ts_node_child(node, i);
            const std::string t = ts_node_type(ch);

            if (t == "async")  element->set_property("async", "true");
            if (t == "static") element->set_property("static", "true");

            if (t == "get") element->set_property("kind", "get");
            if (t == "set") element->set_property("kind", "set");

            if (t == "*") element->set_property("generator", "true");

            if (t == "public" || t == "private" || t == "protected") {
                element->set_property("access", t);
            }
        }

        // --- fields (robust) ---
        // In tree-sitter-javascript, method_definition usually has fields:
        //  - name
        //  - parameters
        //  - body
        TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
        TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);

        // Fallbacks (some grammars)
        if (ts_node_is_null(params_node)) params_node = ts_node_child_by_field_name(node, "parameters", 9);
        if (ts_node_is_null(params_node)) params_node = ts_node_child_by_field_name(node, "parameter", 9);

        // Add children in strict order: name, params, body
        if (!ts_node_is_null(name_node)) {
            if (auto n = process_node(name_node, source)) element->add_child(n);
        }

        if (!ts_node_is_null(params_node)) {
            if (auto p = process_node(params_node, source)) element->add_child(p);
        }

        if (!ts_node_is_null(body_node)) {
            if (auto b = process_node(body_node, source)) element->add_child(b);
        }

        // Absolute fallback: scan named children to fill missing slots
        if (element->children().size() < 3) {
            TSNode fallback_name{}, fallback_params{}, fallback_body{};
            const uint32_t named = ts_node_named_child_count(node);
            for (uint32_t i = 0; i < named; ++i) {
                TSNode nch = ts_node_named_child(node, i);
                const std::string nt = ts_node_type(nch);

                if (ts_node_is_null(fallback_name) &&
                    (nt == "property_identifier" || nt == "identifier" || nt == "private_property_identifier")) {
                    fallback_name = nch;
                    continue;
                }
                if (ts_node_is_null(fallback_params) && (nt == "formal_parameters")) {
                    fallback_params = nch;
                    continue;
                }
                if (ts_node_is_null(fallback_body) && (nt == "statement_block" || nt == "block")) {
                    fallback_body = nch;
                    continue;
                }
            }

            // Fill missing in order
            if (element->children().empty() && !ts_node_is_null(fallback_name)) {
                if (auto n = process_node(fallback_name, source)) element->add_child(n);
            }
            if (element->children().size() < 2 && !ts_node_is_null(fallback_params)) {
                if (auto p = process_node(fallback_params, source)) element->add_child(p);
            }
            if (element->children().size() < 3 && !ts_node_is_null(fallback_body)) {
                if (auto b = process_node(fallback_body, source)) element->add_child(b);
            }
        }

        return element;
    });
    f.register_handler("new_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("NewExpression");
        element->set_property("type", "NewExpression");

        // callee = child 1 (après 'new')
        TSNode callee_node = ts_node_child(node, 1);
        if (auto callee = process_node(callee_node, source)) element->add_child(callee);

        // Cherche un node "arguments" (souvent child 2, mais on reste safe)
        TSNode args_node{};
        const uint32_t c = ts_node_child_count(node);
        for (uint32_t i = 0; i < c; ++i) {
            TSNode ch = ts_node_child(node, i);
            if (std::string(ts_node_type(ch)) == "arguments") { args_node = ch; break; }
        }

        if (!ts_node_is_null(args_node)) {
            // IMPORTANT: ne considère "has parens" que si on voit vraiment '(' après la fin du callee.
            const auto callee_end = ts_node_end_byte(callee_node);
            const auto args_start = ts_node_start_byte(args_node);

            bool has_parens = false;
            if (args_start > callee_end) {
                const std::string gap = source.substr(callee_end, args_start - callee_end);
                has_parens = (gap.find('(') != std::string::npos);
            } else {
                // fallback: regarde directement le texte de args_node
                const std::string args_txt = get_node_text(args_node, source);
                has_parens = args_txt.starts_with("("); // minimal
            }

            // Tree-sitter peut représenter des "arguments" vides; on ne les garde que si parens explicites.
            if (has_parens) {
                const auto args_element = std::make_shared<GraphElement>("ArgumentsList");
                args_element->set_property("type", "ArgumentsList");

                // idéalement: seulement les enfants NOMMÉS (sinon tu ramasses '(' ')' que tu ignores ailleurs)
                const uint32_t n = ts_node_named_child_count(args_node);
                for (uint32_t i = 0; i < n; ++i) {
                    if (auto arg = process_node(ts_node_named_child(args_node, i), source)) {
                        args_element->add_child(arg);
                    }
                }

                element->add_child(args_element);
            } else {
                element->set_property("omit_empty_parens", "true");
            }
        }

        return element;
    });

    f.register_handler("this", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ThisExpression");
        element->set_property("type", "ThisExpression");
        return element;
    });
    f.register_handler("array", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ArrayLiteral");
        element->set_property("type", "ArrayLiteral");

        const uint32_t itemCount = ts_node_child_count(node);
        for (uint32_t i = 0; i < itemCount; i++) {
            const auto item_node = ts_node_child(node, i);
            if (const auto child = process_node(item_node, source))
                element->add_child(child);
        }
        return element;
    });
    f.register_handler("null", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("NullLiteral");
        element->set_property("type", "NullLiteral");
        return element;
    });
    f.register_handler("spread_element", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("SpreadElement");
        element->set_property("type", "SpreadElement");

        // Option A : Utiliser l'enfant "nommé" (recommandé)
        // Cela récupère directement l'expression et ignore les points de suspension
        const auto expression_node = ts_node_named_child(node, 0);

        // Option B : Si tu veux rester sur les index bruts, c'est l'index 1
        // const auto expression_node = ts_node_child(node, 1);

        if (const auto expr = process_node(expression_node, source)) {
            element->add_child(expr);
        }

        return element;
    });
    f.register_handler("rest_pattern", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("RestElement");
        element->set_property("type", "RestElement");

        // rest_pattern: "..."+(identifier|pattern) -> on prend le 1er enfant nommé
        TSNode inner = ts_node_named_child(node, 0);
        if (!ts_node_is_null(inner)) {
            if (auto inner_el = process_node(inner, source)) {
                element->add_child(inner_el);
            }
        }
        return element;
    });
    f.register_handler("object", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ObjectLiteral");
        element->set_property("type", "ObjectLiteral");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            const std::string ctype = ts_node_type(child);

            if (ctype == "pair" ||
                ctype == "spread_element" ||
                ctype == "method_definition" || // ✅ ADD THIS
                ctype == "shorthand_property_identifier" ||
                ctype == "shorthand_property_identifier_pattern") {

                if (const auto arg_element = process_node(child, source)) {
                    element->add_child(arg_element);
                }
            }
        }
        return element;
    });

    f.register_handler("sequence_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("SequenceExpression");
        element->set_property("type", "SequenceExpression");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            if (const auto child_element = process_node(ts_node_child(node, i), source))
                element->add_child(child_element);
        }
        return element;
    });
    f.register_handler("regex", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("RegExpLiteral");
        element->set_property("type", "RegExpLiteral");
        element->set_property("value", source.substr(
            ts_node_start_byte(node),
            ts_node_end_byte(node) - ts_node_start_byte(node)
        ));
        return element;
    });
    f.register_handler("object_assignment_pattern", [this](const TSNode& node, const std::string& source) {
        // Represents: { a = 1 } (inside object pattern)
        const auto element = std::make_shared<GraphElement>("Binding");
        element->set_property("type", "Binding");

        TSNode left = ts_node_named_child(node, 0);   // identifier/pattern
        TSNode right = ts_node_named_child(node, 1);  // initializer expression

        if (!ts_node_is_null(left)) {
            if (auto target = process_node(left, source)) element->add_child(target);
        }
        if (!ts_node_is_null(right)) {
            if (auto init = process_node(right, source)) element->add_child(init);
        }
        return element;
    });
    f.register_handler("pair_pattern", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("PropertyKeyed");
        element->set_property("type", "PropertyKeyed");
        element->set_property("kind", "init");

        TSNode key_node = ts_node_child_by_field_name(node, "key", 3);
        TSNode value_node = ts_node_child_by_field_name(node, "value", 5);

        if (auto key = process_node(key_node, source)) element->add_child(key);
        if (auto value = process_node(value_node, source)) element->add_child(value);

        element->set_property("computed", "false");
        return element;
    });
    f.register_handler("assignment_pattern", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("Binding");
        element->set_property("type", "Binding");

        // named children: [0]=left pattern, [1]=right init
        TSNode left = ts_node_named_child(node, 0);
        TSNode right = ts_node_named_child(node, 1);

        if (!ts_node_is_null(left)) {
            if (auto target = process_node(left, source)) element->add_child(target);
        }
        if (!ts_node_is_null(right)) {
            if (auto init = process_node(right, source)) element->add_child(init);
        }
        return element;
    });
    f.register_handler("true", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("BooleanLiteral");
        element->set_property("type", "BooleanLiteral");
        element->set_property("value", "true"); // "true" ou "false"
        return element;
    });
    f.register_handler("false", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("BooleanLiteral");
        element->set_property("type", "BooleanLiteral");
        element->set_property("value", "false"); // "true" ou "false"
        return element;
    });
    f.register_handler("super", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("SuperExpression");
        element->set_property("type", "SuperExpression");
        return element;
    });
    f.register_handler("array_pattern", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ArrayPattern");
        element->set_property("type", "ArrayPattern");
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            const auto expr = process_node(child, source);
            if (expr) element->add_child(expr);
        }
        return element;
    });
    f.register_handler("object_pattern", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ObjectPattern");
        element->set_property("type", "ObjectPattern");
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            const auto expr = process_node(child, source);
            if (expr) element->add_child(expr);
        }
        return element;
    });
    f.register_handler("optional_chain", [this](const TSNode& node, const std::string& source) -> std::shared_ptr<GraphElement> {
        if (ts_node_is_null(node)) return nullptr;

        // Ouptut the node code
        // On parcourt les enfants pour trouver le premier qui est "nommé"
        // (L'expression réelle derrière le chaînage)
        if (const uint32_t count = ts_node_named_child_count(node); count > 0) {
            return process_node(ts_node_named_child(node, 0), source);
        }

        // Fallback : si pas d'enfant nommé, on tente le premier enfant brut
        if (ts_node_child_count(node) > 0) {
            return process_node(ts_node_child(node, 0), source);
        }

        return nullptr;
    });
    f.register_handler("optional_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("Optional");
        element->set_property("type", "Optional");
        element->set_property("raw", get_node_text(node, source));
        return element;
    });

    f.register_handler("optional_call_expression", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("OptionalChain");
        element->set_property("type", "OptionalChain");
        element->set_property("raw", get_node_text(node, source));
        return element;
    });

    f.register_handler("arrow_function", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ArrowFunctionLiteral");
        element->set_property("type", "ArrowFunctionLiteral");

        // async ?
        const uint32_t childCount = ts_node_child_count(node);
        for (uint32_t i = 0; i < childCount; i++) {
            TSNode child = ts_node_child(node, i);
            if (std::string(ts_node_type(child)) == "async") {
                element->set_property("async", "true");
                break;
            }
        }

        // --- Parameters (tree-sitter can return identifier OR formal_parameters) ---
        TSNode params_node = ts_node_child_by_field_name(node, "parameters", 10);
        if (ts_node_is_null(params_node)) {
            // some grammars use "parameter" for the single param form
            params_node = ts_node_child_by_field_name(node, "parameter", 9);
        }

        std::shared_ptr<GraphElement> params_el;

        if (!ts_node_is_null(params_node)) {
            const std::string ptype = ts_node_type(params_node);

            if (ptype == "identifier") {
                // Normalize: wrap single identifier into a ParameterList
                params_el = std::make_shared<GraphElement>("ParameterList");
                params_el->set_property("type", "ParameterList");

                if (auto id = process_node(params_node, source)) {
                    params_el->add_child(id);
                }
            } else {
                // formal_parameters handler already returns ParameterList
                params_el = process_node(params_node, source);
            }
        } else {
            // Fallback: try first named child as params if nothing found
            // (avoid breaking if fields differ)
            for (uint32_t i = 0; i < childCount; i++) {
                TSNode child = ts_node_child(node, i);
                if (!ts_node_is_named(child)) continue;
                const std::string t = ts_node_type(child);
                if (t == "identifier" || t == "formal_parameters") {
                    if (t == "identifier") {
                        params_el = std::make_shared<GraphElement>("ParameterList");
                        params_el->set_property("type", "ParameterList");
                        if (auto id = process_node(child, source)) params_el->add_child(id);
                    } else {
                        params_el = process_node(child, source);
                    }
                    break;
                }
            }
        }

        if (params_el) element->add_child(params_el);

        // --- Body ---
        TSNode body_node = ts_node_child_by_field_name(node, "body", 4);
        if (ts_node_is_null(body_node)) {
            // Fallback: usually last named child
            for (int i = (int)childCount - 1; i >= 0; i--) {
                TSNode child = ts_node_child(node, i);
                if (ts_node_is_named(child)) { body_node = child; break; }
            }
        }

        if (!ts_node_is_null(body_node)) {
            if (auto body_el = process_node(body_node, source)) {
                element->add_child(body_el);
            }
        }

        return element;
    });

    f.register_handler("variable_declarator", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("Binding");
        element->set_property("type", "Binding");

        // Enfant 0 : Le pattern (Identifier, ObjectPattern ou ArrayPattern)
        if (const auto target = process_node(ts_node_child(node, 0), source)) {
            element->add_child(target);
        }

        // Enfant 2 (si existe) : L'initialisation (= ...)
        if (ts_node_child_count(node) > 2) {
            if (const auto init = process_node(ts_node_child(node, 2), source)) {
                element->add_child(init);
            }
        }
        return element;
    });
    f.register_handler("program", [this](const TSNode& node, const std::string& source) {
        const auto program = std::make_shared<GraphElement>("Program");
        program->set_property("type", "Program");
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            if (const auto stmt = process_node(child, source))
                program->add_child(stmt);
        }
        return program;
    });
    f.register_handler("throw_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ThrowStatement");
        element->set_property("type", "ThrowStatement");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            if (auto expr = process_node(child, source)) element->add_child(expr);
        }

        return element;
    });
    f.register_handler("statement_block", [this](const TSNode& node, const std::string& source) {
        const auto block = std::make_shared<GraphElement>("BlockStatement");
        block->set_property("type", "BlockStatement");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            const auto stmt = process_node(child, source);
            if (stmt) block->add_child(stmt);
        }

        return block;
    });
    f.register_handler("if_statement", [this](const TSNode& node, const std::string& source) {
        const auto if_element = std::make_shared<GraphElement>("IfStatement");
        if_element->set_property("type", "IfStatement");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            const auto stmt = process_node(child, source);
            if (stmt) if_element->add_child(stmt);
        }
        return if_element;
    });
    f.register_handler("else_clause", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ElseClause");
        element->set_property("type", "ElseClause");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            if (const auto child_element = process_node(child, source)) element->add_child(child_element);
        }

        return element;
    });
    f.register_handler("switch_statement", [this](const TSNode& node, const std::string& source) {
        const auto switch_element = std::make_shared<GraphElement>("SwitchStatement");
        switch_element->set_property("type", "SwitchStatement");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            const std::string ctype = ts_node_type(child);

            if (const auto child_element = process_node(child, source))
                switch_element->add_child(child_element);
        }

        return switch_element;
    });
    f.register_handler("switch_body", [this](const TSNode& node, const std::string& source) {
        const auto switch_element = std::make_shared<GraphElement>("SwitchBody");
        switch_element->set_property("type", "SwitchBody");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            const std::string ctype = ts_node_type(child);

            if (const auto child_element = process_node(child, source))
                switch_element->add_child(child_element);
        }

        return switch_element;
    });
    f.register_handler("switch_case", [this](const TSNode& node, const std::string& source) {
        const auto switch_element = std::make_shared<GraphElement>("SwitchCase");
        switch_element->set_property("type", "SwitchCase");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            if (const auto child_element = process_node(child, source))
                switch_element->add_child(child_element);
        }

        return switch_element;
    });
    f.register_handler("switch_default", [this](const TSNode& node, const std::string& source) {
        const auto switch_element = std::make_shared<GraphElement>("SwitchDefault");
        switch_element->set_property("type", "SwitchDefault");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            const std::string ctype = ts_node_type(child);

            if (const auto child_element = process_node(child, source))
                switch_element->add_child(child_element);
        }
        return switch_element;
    });
    f.register_handler("empty_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("EmptyStatement");
        element->set_property("type", "EmptyStatement");
        return element;
    });
    f.register_handler("function_declaration", [this](const TSNode& node, const std::string& source) {
        const auto decl = std::make_shared<GraphElement>("FunctionDeclaration");
        decl->set_property("type", "FunctionDeclaration");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const TSNode child = ts_node_child(node, i);
            const std::string type = ts_node_type(child);

            // On intercepte le modificateur async
            if (type == "async") {
                decl->set_property("async", "true");
                continue; // Pas besoin de l'ajouter en enfant si on le gère en propriété
            }

            if (auto processed = process_node(child, source))
                decl->add_child(processed);
        }
        return decl;
    });
    f.register_handler("expression_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ExpressionStatement");
        element->set_property("type", "ExpressionStatement");

        if (ts_node_child_count(node) > 0) {
            const auto expr_node = ts_node_child(node, 0);
            const auto expr_element = process_node(expr_node, source);
            if (expr_element) element->add_child(expr_element);
        }
        return element;
    });
    f.register_handler("block", [this](const TSNode& node, const std::string& source) {
        const auto block = std::make_shared<GraphElement>("BlockStatement");
        block->set_property("type", "BlockStatement");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            const auto stmt = process_node(child, source);
            if (stmt) block->add_child(stmt);
        }
        return block;
    });
    f.register_handler("return_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ReturnStatement");
        element->set_property("type", "ReturnStatement");

        if (const auto child_count = ts_node_child_count(node); child_count > 1) {
            const auto arg_node = ts_node_child(node, 1);
            const auto arg_element = process_node(arg_node, source);
            if (arg_element)
                element->add_child(arg_element);
        }
        return element;
    });
    f.register_handler("variable_declaration", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("VariableStatement");
        element->set_property("type", "VariableStatement");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto decl_node = ts_node_child(node, i);
            if (const auto decl_element = process_node(decl_node, source))
                element->add_child(decl_element);
        }
        return element;
    });
    f.register_handler("for_in_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ForInOfStatement");
        element->set_property("type", "ForInOfStatement");

        std::string op = "in";
        std::string kind = "";

        // 1. Parcours manuel des enfants pour trouver l'opérateur et le mot-clé
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(node, i);
            std::string type = ts_node_type(child);

            if (type == "of") op = "of";

            // Détection des mots-clés de déclaration
            if (type == "const") kind = "const";
            else if (type == "let") kind = "let";
            else if (type == "var") kind = "var";

            // Si Tree-sitter a groupé le mot-clé dans une lexical_declaration
            if (type == "lexical_declaration") {
                std::string text = get_node_text(child, source);
                if (text.starts_with("const")) kind = "const";
                else if (text.starts_with("let")) kind = "let";
            }
        }

        element->set_property("operator", op);
        if (!kind.empty()) {
            element->set_property("declaration_kind", kind);
        }

        // 2. Extraction des champs nommés standards
        const auto left = ts_node_child_by_field_name(node, "left", 4);
        const auto right = ts_node_child_by_field_name(node, "right", 5);
        const auto body = ts_node_child_by_field_name(node, "body", 4);

        if (auto l = process_node(left, source)) element->add_child(l);
        if (auto r = process_node(right, source)) element->add_child(r);
        if (auto b = process_node(body, source)) element->add_child(b);

        return element;
    });
    f.register_handler("for_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ForStatement");
        element->set_property("type", "ForStatement");

        auto make_empty_expr = []() {
            auto e = std::make_shared<GraphElement>("EmptyExpression");
            e->set_property("type", "EmptyExpression");
            return e;
        };

        TSNode initNode = ts_node_child_by_field_name(node, "initializer", 11);
        TSNode condNode = ts_node_child_by_field_name(node, "condition", 9);
        TSNode incrNode = ts_node_child_by_field_name(node, "increment", 9);
        TSNode bodyNode = ts_node_child_by_field_name(node, "body", 4);

        // init
        if (!ts_node_is_null(initNode)) {
            if (auto init = process_node(initNode, source)) element->add_child(init);
            else element->add_child(make_empty_expr());
        } else {
            element->add_child(make_empty_expr());
        }

        // condition
        if (!ts_node_is_null(condNode)) {
            if (auto cond = process_node(condNode, source)) element->add_child(cond);
            else element->add_child(make_empty_expr());
        } else {
            element->add_child(make_empty_expr());
        }

        // increment
        if (!ts_node_is_null(incrNode)) {
            if (auto inc = process_node(incrNode, source)) element->add_child(inc);
            else element->add_child(make_empty_expr());
        } else {
            element->add_child(make_empty_expr());
        }

        // body (si body absent -> fallback empty statement)
        if (!ts_node_is_null(bodyNode)) {
            if (auto body = process_node(bodyNode, source)) element->add_child(body);
            else {
                auto b = std::make_shared<GraphElement>("EmptyStatement");
                b->set_property("type", "EmptyStatement");
                element->add_child(b);
            }
        } else {
            auto b = std::make_shared<GraphElement>("EmptyStatement");
            b->set_property("type", "EmptyStatement");
            element->add_child(b);
        }

        return element;
    });

    f.register_handler("break_statement", [this](const TSNode&, const std::string&) {
        const auto element = std::make_shared<GraphElement>("BranchStatement");
        element->set_property("type", "BranchStatement");
        element->set_property("token", "break");
        return element;
    });

    f.register_handler("continue_statement", [this](const TSNode&, const std::string&) {
        const auto element = std::make_shared<GraphElement>("BranchStatement");
        element->set_property("type", "BranchStatement");
        element->set_property("token", "continue");
        return element;
    });

    f.register_handler("try_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("TryStatement");
        element->set_property("type", "TryStatement");
        
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const TSNode child = ts_node_child(node, i);
            if (auto sub = process_node(child, source)) element->add_child(sub);
        }

        return element;
    });
    f.register_handler("catch_clause", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("CatchClause");
        element->set_property("type", "CatchClause");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const TSNode child = ts_node_child(node, i);

            if (const std::string child_type = ts_node_type(child); child_type == "catch")
                continue;

            if (const auto sub = process_node(child, source))
                element->add_child(sub);
        }

        return element;
    });
    f.register_handler("finally_clause", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("FinallyClause");
        element->set_property("type", "FinallyClause");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            if (const auto body = process_node(ts_node_child(node, i), source))
                element->add_child(body);
        }
        return element;
    });
    f.register_handler("while_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("while");
        element->set_property("type", "WhileStatement");

        const auto test = ts_node_child(node, 1);
        element->add_child(process_node(test, source));

        const auto body = ts_node_child(node, 2);
        element->add_child(process_node(body, source));

        return element;
    });
    f.register_handler("do_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("do_while");
        element->set_property("type", "core.runnable.do_while");

        const uint32_t n = ts_node_child_count(node);
        if (n > 1) {
            const auto body = ts_node_child(node, 1);
            element->add_child(process_node(body, source));
        }
        if (n > 3) {
            const auto test = ts_node_child(node, 3);
            element->add_child(process_node(test, source));
        }
        return element;
    });
    f.register_handler("lexical_declaration", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("LexicalDeclaration");
        element->set_property("type", "LexicalDeclaration");

        // 1. On récupère le mot-clé (const, let, var)
        if (ts_node_child_count(node) > 0) {
            element->set_property("kind", get_node_text(ts_node_child(node, 0), source));
        }

        // 2. DETECTION : Est-ce une instruction isolée ?
        std::string parent_type = ts_node_type(ts_node_parent(node));
        // Si on n'est pas dans les parenthèses d'un for, c'est un statement
        if (parent_type != "for_in_statement" && parent_type != "for_statement") {
            element->set_property("isStatement", "true");
        }

        // 3. Ajout des déclarateurs (item, a = [0,1,2], etc.)
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            const auto child = ts_node_child(node, i);
            if (std::string(ts_node_type(child)) == "variable_declarator") {
                if (const auto expr = process_node(child, source))
                    element->add_child(expr);
            }
        }
        return element;
    });
    f.register_handler("property_identifier", [this](const TSNode& node, const std::string& source) {
        const auto value = source.substr(
            ts_node_start_byte(node),
            ts_node_end_byte(node) - ts_node_start_byte(node)
        );

        const auto element = std::make_shared<GraphElement>("PropertyIdentifier");
        element->set_property("type", "PropertyIdentifier");
        element->set_property("value", value);

        return element;
    });
    f.register_handler("class_declaration", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ClassDeclaration");
        element->set_property("type", "ClassDeclaration");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            if (const auto sub = process_node(child, source))
                element->add_child(sub);
        }
        return element;
    });
    f.register_handler("class_heritage", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("SuperClass");
        element->set_property("type", "SuperClass");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            if (const auto sub = process_node(child, source))
                element->add_child(sub);
        }
        return element;
    });
    f.register_handler("class_body", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ClassBody");
        element->set_property("type", "ClassBody");

        for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
            const auto child = ts_node_child(node, i);
            if (const auto sub = process_node(child, source))
                element->add_child(sub);
        }
        return element;
    });
    f.register_handler("class", [this](const TSNode& node, const std::string& source) {
        auto element = std::make_shared<GraphElement>("ClassLiteral");
        element->set_property("type", "ClassLiteral");

        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; ++i) {
            TSNode child = ts_node_child(node, i);
            std::string type = ts_node_type(child);

            if (type == "class") {
                continue;
            }

            if (auto sub = process_node(child, source)) {
                element->add_child(sub);
            }
        }
        return element;
    });

    f.register_handler("for_of_statement", [this](const TSNode& node, const std::string& source) {
        const auto element = std::make_shared<GraphElement>("ForOfStatement");
        element->set_property("type", "ForOfStatement");

        std::string kind;
        const uint32_t count = ts_node_child_count(node);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(node, i);
            std::string t = ts_node_type(child);

            if (t == "const") kind = "const";
            else if (t == "let") kind = "let";
            else if (t == "var") kind = "var";

            if (t == "lexical_declaration") {
                std::string text = get_node_text(child, source);
                if (text.starts_with("const")) kind = "const";
                else if (text.starts_with("let")) kind = "let";
            }
        }
        if (!kind.empty()) element->set_property("declaration_kind", kind);

        const auto leftNode  = ts_node_child_by_field_name(node, "left", 4);
        const auto rightNode = ts_node_child_by_field_name(node, "right", 5);
        const auto bodyNode  = ts_node_child_by_field_name(node, "body", 4);

        if (auto left_el = process_node(leftNode, source)) element->add_child(left_el);
        if (auto right_el = process_node(rightNode, source)) element->add_child(right_el);
        if (auto body_el = process_node(bodyNode, source)) element->add_child(body_el);

        return element;
    });

}
/** Destructor to clean up the Tree-sitter parser instance.
 */
JsParser::~JsParser() {
    ts_parser_delete(parser_);
}
/** Parse the given JavaScript source code and return the root GraphElement.
 * @param source The JavaScript source code to parse.
 * @return A shared pointer to the root GraphElement representing the parsed structure.
 */
std::shared_ptr<GraphElement> JsParser::parse(const std::string& source) const {
    TSTree* tree = ts_parser_parse_string(
        parser_, nullptr, source.c_str(), source.size()
    );

    const TSNode root = ts_tree_root_node(tree);

    auto element = process_node(root, source);
    ts_tree_delete(tree);
    return element;
}

/** Process a TSNode and convert it into a GraphElement.
 * @param node The TSNode to process.
 * @param src The original source code string.
 * @return A shared pointer to the corresponding GraphElement, or nullptr if the node is ignored or cannot be processed.
 */
std::shared_ptr<GraphElement> JsParser::process_node(const TSNode& node, const std::string& src) const {
    if (ts_node_is_null(node)) {
        return nullptr;
    }

    const std::string type = ts_node_type(node);

    if (IGNORED_TOKENS.contains(type)) return nullptr;

    auto elem = process_element(node, src);
    if (!elem) return nullptr;

    if (is_debug_mode_) {
        const auto start = ts_node_start_byte(node);
        const auto end   = ts_node_end_byte(node);
        elem->set_property("text", src.substr(start, end - start));
    }

    return elem;
}
/** Process a TSNode and create the corresponding GraphElement using the JsNodeFactory.
 * @param node The TSNode to process.
 * @param source The original source code string.
 * @return A shared pointer to the created GraphElement, or nullptr if the node type is "ERROR".
 */
std::shared_ptr<GraphElement> JsParser::process_element(const TSNode& node, const std::string& source) {
    const std::string type = ts_node_type(node);
    if (type == "ERROR") {
        const auto start = ts_node_start_byte(node);
        const auto end = ts_node_end_byte(node);
        const auto snippet = source.substr(start, end - start);
        LOG_THIS_ERROR("unexpected syntax near: {}", snippet);
        return nullptr;
    }

    return JsNodeFactory::instance().create(type, node, source);
}

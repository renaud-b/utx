#include <sstream>
#include "JsGenerator.hpp"

#include "JsNodeEmitter.hpp"

using namespace utx::domain::graph;

utx::infra::languages::javascript::JsGenerator::JsGenerator() {
    auto &e = JsNodeEmitter::instance();

    e.register_handler("Program", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &kids = element.children();
        for (size_t i = 0; i < kids.size(); ++i) {
            convert_node_to_js(*kids[i], out);
            if (i + 1 < kids.size()) out << " ";
        }
    });
    e.register_handler("Body", [this](const GraphElement &element, std::ostringstream &out) {
        for (const auto &i: element.children()) {
            convert_node_to_js(*i, out);
        }
    });
    e.register_handler("NumberLiteral", [this](const GraphElement &el, std::ostringstream &out) {
        out << el.get_property("value");
    });
    e.register_handler("BooleanLiteral", [this](const GraphElement &el, std::ostringstream &out) {
        out << el.get_property("value");
    });
    e.register_handler("NullLiteral", [this](const GraphElement &, std::ostringstream &out) {
        out << "null";
    });
    e.register_handler("StringLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        if (auto value = element.get_property("value");
            !value.empty() && value.front() != '"' && value.front() != '\'') {
            if (value.find('\'') != std::string::npos) {
                out << "\"" << value << "\"";
            } else {
                out << "'" << value << "'";
            }
        } else {
            out << value;
        }
    });
    e.register_handler("RegExpLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        out << element.get_property("value");
    });
    e.register_handler("SuperExpression", [this](const GraphElement &element, std::ostringstream &out) {
        out << "super";
    });
    e.register_handler("VariableStatement", [this](const GraphElement &el, std::ostringstream &out) {
        out << "var ";

        const auto& kids = el.children();

        for (size_t i = 0; i < kids.size(); ++i) {
            if (i > 0) out << ", ";

            convert_node_to_js(*kids[i], out);

            // 👇 si le prochain node est une expression, c’est l'initializer
            if (i + 1 < kids.size()) {
                const auto nextType = kids[i + 1]->get_property("type");
                if (nextType == "ClassLiteral" || nextType == "FunctionExpression" || nextType == "ArrowFunctionLiteral") {
                    out << " = ";
                    convert_node_to_js(*kids[i + 1], out);
                    ++i; // skip initializer
                }
            }
        }

        out << ";";
    });

    e.register_handler("ForLoopInitializerExpression", [this](const GraphElement &element, std::ostringstream &out) {
        convert_node_to_js(*element.children()[0], out);
    });
    e.register_handler("SequenceExpression", [this](const GraphElement &element, std::ostringstream &out) {
        for (size_t i = 0; i < element.children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*element.children()[i], out);
        }
    });
    e.register_handler("NewExpression", [this](const GraphElement &element, std::ostringstream &out) {
        out << "new ";
        convert_node_to_js(*element.children().at(0), out);

        // children[1] = ArgumentsList (si présent)
        if (element.children().size() > 1) {
            out << "(";
            const auto &args = element.children().at(1);
            for (size_t i = 0; i < args->children().size(); ++i) {
                if (i > 0) out << ", ";
                convert_node_to_js(*args->children()[i], out);
            }
            out << ")";
        }
    });
    e.register_handler("BranchStatement", [this](const GraphElement &element, std::ostringstream &out) {
        out << element.get_property("token") << ";";
    });
    e.register_handler("BracketExpression", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().size() < 2) throw std::runtime_error("BracketExpression missing children");

        convert_node_to_js(*element.children().at(0), out);

        out << (element.get_property("optional") == "true" ? "?.[" : "[");

        convert_node_to_js(*element.children().at(1), out);
        out << "]";
    });

    e.register_handler("BooleanLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        out << element.get_property("value");
    });

    e.register_handler("BinaryExpression", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &children = element.children();
        if (children.size() < 2) return;

        std::string op = element.get_property("operator");
        if (op.empty() || op == "nil") op = "+";

        const std::string jsOp = operator_converter(op);

        convert_node_to_js(*children[0], out);

        // Word operators MUST have spaces
        const bool isWordOp = (jsOp == "in" || jsOp == "instanceof");
        if (isWordOp) out << " " << jsOp << " ";
        else out << jsOp;

        convert_node_to_js(*children[1], out);
    });
    e.register_handler("Identifier", [this](const GraphElement &el, std::ostringstream &out) {
        out << el.get_property("value");
    });

    e.register_handler("DotExpression", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &left = *element.children().at(0);
        convert_node_to_js(left, out);

        if (element.get_property("optional") == "true") {
            out << "?.";
        } else {
            out << ".";
        }
        if (element.children().size() > 1) {
            const auto &right = *element.children().at(1);
            convert_node_to_js(right, out);
        } else if (element.has_property("value")) {
            out << element.get_property("value");
        }
    });
    e.register_handler("RestElement", [this](const GraphElement &element, std::ostringstream &out) {
        out << "...";
        convert_node_to_js(*element.children().at(0), out);
    });
    e.register_handler("TemplateLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        out << "`";
        for (const auto &child: element.children()) {
            if (child->get_property("isSubstitution") == "true") {
                out << "${";
                convert_node_to_js(*child, out);
                out << "}";
            } else {
                // On récupère la valeur brute du fragment de texte
                out << child->get_property("value");
            }
        }
        out << "`";
    });
    e.register_handler("FunctionDeclaration", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.get_property("async") == "true") out << "async ";

        // Cas spécial déjà géré chez toi
        if (element.children().size() == 1 && element.children()[0]->get_property("type") == "FunctionLiteral") {
            convert_node_to_js(*element.children()[0], out);
            return;
        }

        out << "function ";

        // On rend sans mettre d'espace entre Identifier et ParameterList
        // mais on garde des espaces logiques ailleurs.
        for (size_t i = 0; i < element.children().size(); ++i) {
            const auto &ch = element.children()[i];
            const auto t = ch->get_property("type");

            const bool needSpace =
                    (i > 0) &&
                    !(element.children()[i - 1]->get_property("type") == "Identifier" && t == "ParameterList");

            if (needSpace) out << " ";
            convert_node_to_js(*ch, out);
        }
    });

    e.register_handler("ExpressionStatement", [this](const GraphElement &el, std::ostringstream &out) {
        emit_expression(*el.children().at(0), out);
        out << ";";
    });
    e.register_handler("FunctionLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        // Support async pour les fonctions anonymes ou nommées
        if (element.get_property("async") == "true") {
            out << "async ";
        }

        const auto name = element.get_property("value");
        out << "function";
        if (!name.empty()) {
            out << " " << name;
        }
        out << "(";
        const auto &params = element.children().at(0);
        for (size_t i = 0; i < params->children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*params->children()[i], out);
        }
        out << ") ";
        convert_node_to_js(*element.children().at(1), out);
    });
    e.register_handler("CallExpression", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().empty()) throw std::runtime_error("CallExpression missing callee");

        convert_node_to_js(*element.children().at(0), out);

        if (element.get_property("optional") == "true") out << "?.";

        out << "(";
        if (element.children().size() > 1) convert_node_to_js(*element.children().at(1), out);
        out << ")";
    });

    e.register_handler("LexicalDeclaration", [this](const GraphElement &element, std::ostringstream &out) {
        out << element.get_property("kind") << " ";

        const auto &children = element.children();
        for (size_t i = 0; i < children.size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*children[i], out);
        }

        // Si c'est un statement, on ajoute le point-virgule et un espace
        if (element.get_property("isStatement") == "true") {
            out << ";";
        }
    });
    e.register_handler("ArgumentList", [this](const GraphElement &element, std::ostringstream &out) {
        for (size_t i = 0; i < element.children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*element.children()[i], out);
        }
    });
    e.register_handler("AssignExpression", [this](const GraphElement &element, std::ostringstream &out) {
        convert_node_to_js(*element.children().at(0), out);
        if (const auto str_operator = operator_converter(element.get_property("operator")); str_operator == "=") {
            out << " " << str_operator << " ";
        } else {
            out << " " << str_operator << "= ";
        }
        convert_node_to_js(*element.children().at(1), out);
    });
    e.register_handler("ArrowFunctionLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.get_property("async") == "true") {
            out << "async ";
        }
        const auto &paramsNode = *element.children().at(0);

        if (!paramsNode.children().empty()) {
            convert_node_to_js(paramsNode, out);
        } else {
            // cas "single param" (Identifier / RestElement / etc.)
            // évite de sortir "()"
            if (!paramsNode.get_property("type").empty()) {
                convert_node_to_js(paramsNode, out);
            }
        }

        out << " => ";
        convert_node_to_js(*element.children().at(1), out);
    });

    e.register_handler("ConditionalExpression", [this](const GraphElement &element, std::ostringstream &out) {
       // IMPORTANT: do NOT add parens here.
       // ParenthesizedExpression nodes represent real source parentheses.
       convert_node_to_js(*element.children().at(0), out);
       out << " ? ";
       convert_node_to_js(*element.children().at(1), out);
       out << " : ";
       convert_node_to_js(*element.children().at(2), out);
   });

    e.register_handler("AssignmentExpression", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().size() < 2) return;
        convert_node_to_js(*element.children()[0], out);
        out << " " << element.get_property("operator") << " ";
        convert_node_to_js(*element.children()[1], out);
    });
    e.register_handler("VariableDeclaration", [this](const GraphElement &element, std::ostringstream &out) {
        const auto kind = element.get_property("kind");
        out << kind << " ";
        for (size_t i = 0; i < element.children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*element.children()[i], out);
        }
        out << ";";
    });
    e.register_handler("ForStatement", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &c = element.children();
        if (c.size() < 4) return;

        std::function<bool(const GraphElement&)> effectivelyEmpty;
        effectivelyEmpty = [&](const GraphElement& n) -> bool {
            const auto t = n.get_property("type");

            // direct empties
            if (t == "EmptyExpression" || t == "EmptyStatement") return true;

            // wrapper node (no type): empty iff all children are empty
            if (t.empty()) {
                for (const auto& ch : n.children()) {
                    if (ch && !effectivelyEmpty(*ch)) return false;
                }
                return true;
            }

            // any other typed node is not empty
            return false;
        };

        auto emitHeaderPart = [&](const std::shared_ptr<GraphElement>& part) {
            if (!part) return;
            if (effectivelyEmpty(*part)) return;
            convert_node_to_js(*part, out);
        };

        out << "for (";

        // init
        if (c[0] && !effectivelyEmpty(*c[0])) {
            convert_node_to_js(*c[0], out);
            if (c[0]->get_property("type") != "ExpressionStatement") {
                // If
                out << ";";
            }
        } else {
            out << ";";
        }

        // condition

        if (c[1] && !effectivelyEmpty(*c[1])) {
            out << " ";
            convert_node_to_js(*c[1], out);
            if (c[1]->get_property("type") != "ExpressionStatement") {
                // If
                out << ";";
            }
        } else {
            out << ";";
        }


        // update
        if (c[2] && !effectivelyEmpty(*c[2])) {
            out << " ";
            convert_node_to_js(*c[2], out);
        }

        out << ") ";
        convert_node_to_js(*c[3], out);
    });



    e.register_handler("WhileStatement", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().size() < 2) return;
        out << "while ";
        if (const auto test = element.children().at(0);
            test->get_property("type") == "BinaryExpression") {
            convert_node_to_js(*test, out);
        } else {
            convert_node_to_js(*test, out);
        }
        out << " ";
        convert_node_to_js(*element.children().at(1), out);
    });
    e.register_handler("CatchStatement", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().size() < 2) return;
        out << " catch (";
        convert_node_to_js(*element.children().at(0), out);
        out << ") ";
        convert_node_to_js(*element.children().at(1), out);
    });
    e.register_handler("ThrowStatement", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().empty()) return;
        out << "throw ";
        if (!element.children().empty()) {
            convert_node_to_js(*element.children()[0], out);
        }
        out << ";";
    });
    e.register_handler("SwitchStatement", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().size() < 1) return;
        out << "switch (";
        if (!element.children().empty()) {
            convert_node_to_js(*element.children()[0], out);
        }
        out << ") {";
        if (element.children().size() > 1) {
            for (auto &cases = element.children()[1]; auto &switchCase: cases->children()) {
                convert_node_to_js(*switchCase, out);
            }
        }
        out << "}";
    });
    e.register_handler("DefaultCase", [this](const GraphElement &element, std::ostringstream &out) {
        out << "default: ";
        for (auto &consequent: element.children()) {
            convert_node_to_js(*consequent, out);
        }
    });
    e.register_handler("FunctionExpression", [this](const GraphElement &element, std::ostringstream &out) {
        // Support async
        if (element.get_property("async") == "true") {
            out << "async ";
        }

        out << "function";

        // Selon ton parser: enfants possibles dans l'ordre : [Identifier?], [ParameterList], [BlockStatement]
        size_t idx = 0;

        // name (optional)
        if (element.children().size() > idx &&
            element.children()[idx]->get_property("type") == "Identifier") {
            out << " ";
            convert_node_to_js(*element.children()[idx], out);
            idx++;
        }

        // params
        if (element.children().size() > idx &&
            element.children()[idx]->get_property("type") == "ParameterList") {
            // Ton handler ParameterList imprime déjà les parenthèses
            convert_node_to_js(*element.children()[idx], out);
            idx++;
        } else {
            // fallback : si jamais tree-sitter te donne un truc différent
            out << "()";
        }

        out << " ";

        // body
        if (element.children().size() > idx) {
            convert_node_to_js(*element.children()[idx], out);
        } else {
            out << "{}";
        }
    });

    e.register_handler("UndefinedLiteral", [this](const GraphElement &, std::ostringstream &out) {
        out << "undefined";
    });

    e.register_handler("IfStatement", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().size() < 2) return;
        out << "if ";
        const auto &test = element.children().at(0);
        convert_node_to_js(*test, out);

        out << " ";
        const auto &consequent = element.children().at(1);
        convert_node_to_js(*consequent, out);

        if (element.children().size() > 2) {
            out << " else ";
            const auto &alternate = element.children().at(2);
            convert_node_to_js(*alternate, out);
        }
    });
    e.register_handler("ElseClause", [this](const GraphElement &element, std::ostringstream &out) {
        if (!element.children().empty()) {
            convert_node_to_js(*element.children()[0], out);
        } else {
            out << "{}";
        }
    });
    e.register_handler("ForOfStatement", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &children = element.children();
        if (children.size() < 3) return;

        const std::string kind = element.get_property("declaration_kind");

        out << "for (";
        // si on a const/let/var et que l'enfant 0 est un pattern ou identifier
        if (!kind.empty()) {
            const auto t = children[0]->get_property("type");
            if (t == "Identifier" || t == "ArrayPattern" || t == "ObjectPattern") {
                out << kind << " ";
            }
        }

        // 1. Déclaration : "const item" ou "let item" (géré par l'enfant)
        convert_node_to_js(*children[0], out);

        out << " of ";

        // 2. L'itérable : "a"
        convert_node_to_js(*children[1], out);

        out << ") ";

        // 3. Le corps : "{ console.log(item); }"
        convert_node_to_js(*children[2], out);
    });
    e.register_handler("ForInOfStatement", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &children = element.children();
        if (children.size() < 3) return;

        const std::string op = element.get_property("operator");
        const std::string kind = element.get_property("declaration_kind"); // "const" ou "let"

        out << "for (";

        // Si on a un kind (const/let) et que l'enfant est juste un identifiant, on l'ajoute
        if (!kind.empty()) {
            const auto t = children[0]->get_property("type");
            if (t == "Identifier" || t == "ArrayPattern" || t == "ObjectPattern") {
                out << kind << " ";
            }
        }

        // 1. Partie gauche
        convert_node_to_js(*children[0], out);

        out << " " << (op.empty() ? "in" : op) << " ";

        // 2. Partie droite
        convert_node_to_js(*children[1], out);

        out << ") ";

        // 3. Corps
        convert_node_to_js(*children[2], out);
    });
    e.register_handler("AwaitExpression", [this](const GraphElement &element, std::ostringstream &out) {
        out << "await ";
        if (!element.children().empty()) {
            // Ici, on convertit l'enfant qui est généralement un CallExpression (le fetch)
            convert_node_to_js(*element.children()[0], out);
        }
    });
    e.register_handler("ClassLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.parent()->get_property("type") != "ClassDeclaration") {
            out << "class ";
        }
        for (auto &child: element.children()) {
            convert_node_to_js(*child, out);
        }
    });
    e.register_handler("ObjectLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().empty()) {
            out << "{}";
            return;
        }

        out << "{ ";
        for (size_t i = 0; i < element.children().size(); ++i) {
            auto &prop = element.children()[i];
            if (i > 0) out << ", ";

            const auto t = prop->get_property("type");

            if (t == "PropertyShort") {
                out << prop->get_property("value");
                continue;
            }
            if (t == "SpreadElement") {
                convert_node_to_js(*prop, out);
                continue;
            }

            if (t == "MethodDefinition") {
                // ✅ NEW
                convert_node_to_js(*prop, out); // réutilise ton handler MethodDefinition
                continue;
            }

            const auto children = prop->children();
            if (children.size() != 2) {
                throw std::runtime_error("Invalid Object Property, expected 2 children");
            }
            convert_node_to_js(*children[0], out);
            out << ": ";
            convert_node_to_js(*children[1], out);
        }
        out << " }";
    });

    e.register_handler("SpreadElement", [this](const GraphElement &element, std::ostringstream &out) {
        out << "...";
        convert_node_to_js(*element.children()[0], out);
    });
    e.register_handler("ArrayLiteral", [this](const GraphElement &element, std::ostringstream &out) {
        out << "[";
        for (size_t i = 0; i < element.children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*element.children()[i], out);
        }
        out << "]";
    });
    e.register_handler("ReturnStatement", [this](const GraphElement &el, std::ostringstream &out) {
        out << "return ";
        if (!el.children().empty()) emit_expression(*el.children()[0], out);
        out << ";";
    });
    e.register_handler("UnaryExpression", [this](const GraphElement &element, std::ostringstream &out) {
        const auto op = element.get_property("operator");
        const auto jsOp = operator_converter(op);
        const bool postfix = (element.get_property("postfix") == "true");

        const auto &operand = *element.children().at(0);

        const bool isWordOp =
                (jsOp == "typeof") ||
                (jsOp == "delete") ||
                (jsOp == "void");

        if (postfix) {
            convert_node_to_js(operand, out);
            out << jsOp; // postfix word-ops n’existent pas vraiment, mais ok
        } else {
            out << jsOp;
            if (isWordOp) out << " ";
            convert_node_to_js(operand, out);
        }
    });

    e.register_handler("PropertyShort", [this](const GraphElement &element, std::ostringstream &out) {
        // On imprime simplement la valeur stockée (ex: "ip")
        out << element.get_property("value");
    });
    e.register_handler("ClassDeclaration", [this](const GraphElement &element, std::ostringstream &out) {
        out << "class ";
        for (auto &child: element.children()) {
            convert_node_to_js(*child, out);
        }
    });
    e.register_handler("SuperClass", [this](const GraphElement &element, std::ostringstream &out) {
        out << " extends ";
        for (auto &child: element.children()) {
            convert_node_to_js(*child, out);
        }
        out << " ";
    });
    e.register_handler("ClassBody", [this](const GraphElement &element, std::ostringstream &out) {
        out << "{ ";
        for (const auto &child: element.children()) {
            convert_node_to_js(*child, out);
            out << " ";
        }
        out << "}";
    });
    e.register_handler("ThisExpression", [this](const GraphElement &element, std::ostringstream &out) {
        out << "this";
    });
    e.register_handler("ParameterList", [this](const GraphElement &element, std::ostringstream &out) {
        out << "(";
        for (size_t i = 0; i < element.children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*element.children()[i], out);
        }
        out << ")";
    });
    e.register_handler("ArrowFunctionLiteralWithBlock", [this](const GraphElement &element, std::ostringstream &out) {
        out << "(";
        const auto &params = element.children().at(1);
        for (size_t i = 0; i < params->children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*params->children()[i], out);
        }
        out << ") => {";
        const auto &body = element.children().at(0);
        convert_node_to_js(*body, out);
        out << "}";
    });
    e.register_handler("EmptyStatement", [this](const GraphElement &element, std::ostringstream &out) {
        out << ";";
    });
    e.register_handler("ObjectPattern", [this](const GraphElement &element, std::ostringstream &out) {
        out << "{";
        for (size_t i = 0; i < element.children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*element.children()[i], out);
        }
        out << "}";
    });
    e.register_handler("ArrayPattern", [this](const GraphElement &element, std::ostringstream &out) {
        out << "[";
        for (size_t i = 0; i < element.children().size(); ++i) {
            if (i > 0) out << ", ";
            convert_node_to_js(*element.children()[i], out);
        }
        out << "]";
    });
    e.register_handler("PropertyAccess", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &object = element.children().at(0);
        const auto &property = element.children().at(1);
        convert_node_to_js(*object, out);
        out << ".";
        convert_node_to_js(*property, out);
    });
    e.register_handler("VariableDeclarationWithDestructuring",
                       [this](const GraphElement &element, std::ostringstream &out) {
           out << "let {";
           const auto &destructured = element.children().at(0);
           for (size_t i = 0; i < destructured->children().size(); ++i) {
               if (i > 0) out << ", ";
               out << destructured->children()[i]->get_property("value");
           }
           out << "} = ";
           const auto &source = element.children().at(1);
           convert_node_to_js(*source, out);
           out << ";";
       });
    e.register_handler("Binding", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().empty()) return;

        const auto &left_side = *element.children().at(0);

        // Si la gauche est un pattern (Object/Array), on délègue
        // Sinon on écrit la valeur (cas d'un Identifier simple)
        if (left_side.get_property("type") == "ObjectPattern" ||
            left_side.get_property("type") == "ArrayPattern") {
            convert_node_to_js(left_side, out);
        } else {
            out << left_side.get_property("value");
        }

        // Si on a une valeur d'initialisation (enfant 1 dans le Binding)
        if (element.children().size() > 1) {
            out << " = ";
            convert_node_to_js(*element.children().at(1), out);
        }
    });
    e.register_handler("BlockStatement", [this](const GraphElement &element, std::ostringstream &out) {
        out << "{ ";
        for (const auto &stmt: element.children()) {
            convert_node_to_js(*stmt, out);
            out << " ";
        }
        out << "}";
    });
    e.register_handler("ForLoopInitializerVarDeclList", [this](const GraphElement &element, std::ostringstream &out) {
        out << "for (";
        const auto &init = *element.children().at(0);
        convert_node_to_js(init, out);
        out << "; ";
        const auto &test = *element.children().at(1);
        convert_node_to_js(test, out);
        out << "; ";
        const auto &update = *element.children().at(2);
        convert_node_to_js(update, out);
    });
    e.register_handler("FieldDefinition", [this](const GraphElement &element, std::ostringstream &out) {
        const auto &value_node = *element.children().at(0);
        const auto field_name = element.children().at(1)->get_property("value");
        out << field_name << "=";
        convert_node_to_js(value_node, out);
        out << ";";
    });
    e.register_handler("ImportDeclaration", [this](const GraphElement &element, std::ostringstream &out) {
        out << "import ";
        const auto &specifiers = *element.children().at(0);
        const auto source = element.children().at(1)->get_property("value");

        if (!specifiers.children().empty()) {
            out << "{ ";
            for (size_t i = 0; i < specifiers.children().size(); ++i) {
                if (i > 0) out << ", ";
                out << specifiers.children()[i]->get_property("value");
            }
            out << " } from \"";
        } else {
            out << "\"";
        }
        out << source << "\";";
    });
    e.register_handler("OptionalChain", [this](const GraphElement &element, std::ostringstream &out) {
        const auto raw = element.get_property("raw");
        if (!raw.empty()) {
            out << raw;
            return;
        }
        throw std::runtime_error("OptionalChain missing raw");
    });
    e.register_handler("Optional", [this](const GraphElement &element, std::ostringstream &out) {
        const auto raw = element.get_property("raw");
        if (!raw.empty()) {
            out << raw;
            return;
        }
        throw std::runtime_error("Optional missing raw");
    });
    e.register_handler("ParenthesizedExpression", [this](const GraphElement &element, std::ostringstream &out) {
        out << "(";
        if (!element.children().empty()) {
            convert_node_to_js(*element.children()[0], out);
        }
        out << ")";
    });
    e.register_handler("TryStatement", [this](const GraphElement &element, std::ostringstream &out) {
        out << "try ";
        convert_node_to_js(*element.children().at(0), out);
        if (element.children().size() > 1) {
            convert_node_to_js(*element.children().at(1), out);
        }
        if (element.children().size() > 2) {
            convert_node_to_js(*element.children().at(2), out);
        }
    });
    e.register_handler("CatchClause", [this](const GraphElement &element, std::ostringstream &out) {
        out << "catch";
        if (!element.children().empty()) {
            if (element.children().size() == 1) {
                out << " ";
                convert_node_to_js(*element.children()[0], out);
            } else if (element.children().size() >= 2) {
                out << " (";
                convert_node_to_js(*element.children()[0], out);
                out << ") ";
                convert_node_to_js(*element.children()[1], out);
            }
        }
    });
    e.register_handler("ThrowStatement", [this](const GraphElement &element, std::ostringstream &out) {
        if (!element.children().empty()) {
            out << "throw ";
            convert_node_to_js(*element.children()[0], out);
            out << ";";
        }
    });
    e.register_handler("SwitchStatement", [this](const GraphElement &element, std::ostringstream &out) {
        out << "switch";
        convert_node_to_js(*element.children()[0], out);
        out << " ";
        if (element.children().size() > 1) {
            convert_node_to_js(*element.children()[1], out);
        }
    });
    e.register_handler("SwitchBody", [this](const GraphElement &element, std::ostringstream &out) {
        out << "{ ";
        for (const auto &child: element.children()) {
            convert_node_to_js(*child, out);
            out << " ";
        }
        out << "}";
    });
    e.register_handler("SwitchCase", [this](const GraphElement &element, std::ostringstream &out) {
        out << "case ";
        convert_node_to_js(*element.children()[0], out);
        out << ": ";
        for (size_t i = 1; i < element.children().size(); i++) {
            convert_node_to_js(*element.children()[i], out);
        }
    });
    e.register_handler("EmptyExpression", [](const GraphElement&, std::ostringstream&) {
        // imprime rien
    });
    e.register_handler("SwitchDefault", [this](const GraphElement &element, std::ostringstream &out) {
        out << "default: ";
        for (const auto &child: element.children()) {
            convert_node_to_js(*child, out);
        }
    });
    e.register_handler("FinallyClause", [this](const GraphElement &element, std::ostringstream &out) {
        out << "finally ";
        convert_node_to_js(*element.children()[0], out);
    });
    e.register_handler("PropertyIdentifier", [this](const GraphElement &element, std::ostringstream &out) {
        out << element.get_property("value");
    });
    e.register_handler("MethodDefinition", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.get_property("static") == "true") out << "static ";
        if (element.get_property("async") == "true") out << "async ";

        const auto kind = element.get_property("kind");
        if (kind == "get") out << "get ";
        else if (kind == "set") out << "set ";

        if (element.get_property("generator") == "true") out << "*";

        if (element.children().size() < 3) {
            // tant qu'on n'a pas name/params/body, on ne peut pas rendre correctement
            throw std::runtime_error("MethodDefinition missing children (name, params, body)");
        }

        convert_node_to_js(*element.children().at(0), out); // name
        convert_node_to_js(*element.children().at(1), out); // params
        convert_node_to_js(*element.children().at(2), out); // body (no space)
    });

    e.register_handler("FieldDefinition", [this](const GraphElement &element, std::ostringstream &out) {
        // Props:
        // - element.static == "true" si static
        // - access optionnel
        // Children:
        // - on veut: [nameNode, valueNode?]
        //   (selon ton handler parser, tu as très probablement name puis initializer)
        if (element.get_property("static") == "true") out << "static ";

        // Find name + init robustly
        const GraphElement* nameNode = nullptr;
        const GraphElement* initNode = nullptr;

        for (const auto &ch : element.children()) {
            const auto t = ch->get_property("type");
            if (!nameNode && (t == "Identifier" || t == "PropertyIdentifier" || t == "PrivatePropertyIdentifier")) {
                nameNode = ch.get();
                continue;
            }
            if (!initNode) {
                // initializer can be any expression node
                initNode = ch.get();
            }
        }

        if (!nameNode) {
            throw std::runtime_error("FieldDefinition missing name child");
        }

        // name
        convert_node_to_js(*nameNode, out);

        // initializer
        if (initNode) {
            out << " = ";
            convert_node_to_js(*initNode, out);
        }

        out << ";";
    });

    e.register_handler("PropertyKeyed", [this](const GraphElement &element, std::ostringstream &out) {
        // Expected: [0] = key, [1] = value
        if (element.children().size() < 2) return;

        const auto &key = *element.children().at(0);
        const auto &val = *element.children().at(1);

        // key
        convert_node_to_js(key, out);

        // colon spacing heuristic to match your minified style:
        // - `compressed:e...` => no space after ':'
        // - `opts: { ... }`   => space after ':' when the value starts with an object/array pattern
        bool spaceAfterColon = false;

        const auto vtype = val.get_property("type");
        if (vtype == "ObjectPattern" || vtype == "ArrayPattern" || vtype == "ObjectLiteral" || vtype == "ArrayLiteral") {
            spaceAfterColon = true;
        } else if (vtype == "Binding" && !val.children().empty()) {
            const auto ltype = val.children().at(0)->get_property("type");
            if (ltype == "ObjectPattern" || ltype == "ArrayPattern") {
                spaceAfterColon = true;
            }
        }

        out << ":";
        if (spaceAfterColon) out << " ";

        // value
        convert_node_to_js(val, out);
    });

    // Dans le générateur
    e.register_handler("MemberExpression", [this](const GraphElement &element, std::ostringstream &out) {
        if (element.children().size() < 2) throw std::runtime_error("MemberExpression missing children");

        convert_node_to_js(*element.children().at(0), out);

        out << (element.get_property("optional") == "true" ? "?." : ".");

        convert_node_to_js(*element.children().at(1), out);
    });
}

/** Convert a graph element representing an operator into its JavaScript equivalent.
 * @param op The operator string from the graph element.
 * @returns The corresponding JavaScript operator string.
 * @throws std::runtime_error if the operator is unsupported.
 */
std::string utx::infra::languages::javascript::JsGenerator::operator_converter(const std::string &op) {
    static const std::unordered_map<std::string, std::string> map = {
        {"6", "+"}, {"7", "-"}, {"8", "*"}, {"9", "**"},
        {"10", "/"}, {"11", "%"}, {"12", "&"}, {"13", "|"},
        {"14", "^"}, {"15", "<<"}, {"16", ">>"}, {"17", ">>>"},
        {"30", "&&"}, {"31", "||"}, {"32", "??"}, {"33", "++"},
        {"34", "--"}, {"35", "=="}, {"36", "==="}, {"37", "<"},
        {"38", ">"}, {"39", "="}, {"40", "!"}, {"41", "~"},
        {"42", "!="}, {"43", "!=="}, {"44", "<="}, {"45", ">="},
        {"67", "in"}, {"87", "typeof"}, {"96", "instanceof"}
    };
    if (auto it = map.find(op); it != map.end()) return it->second;
    return op;
}

/** Recursively convert a graph element and its children into JavaScript code.
 * @param element The graph element to convert.
 * @param out An out string stream to append the generated JavaScript code.
 */
void utx::infra::languages::javascript::JsGenerator::convert_node_to_js(const GraphElement &element,
                                                                        std::ostringstream &out) const {
    const auto type = element.get_property("type");
    if (type.empty()) {
        for (const auto &child: element.children()) {
            convert_node_to_js(*child, out);
        }
        return;
    }
    JsNodeEmitter::instance().emit(type, element, out);
}

int utx::infra::languages::javascript::JsGenerator::binary_precedence(const std::string &op) {
    // op est déjà converti en string JS ("+", "&&", etc)
    // Plus grand = plus fort
    if (op == "**") return 14; // exponentiation (right-assoc)
    if (op == "*" || op == "/" || op == "%") return 13;
    if (op == "+" || op == "-") return 12;
    if (op == "<<" || op == ">>" || op == ">>>") return 11;
    if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "in" || op == "instanceof") return 10;
    if (op == "==" || op == "!=" || op == "===" || op == "!==") return 9;
    if (op == "&") return 8;
    if (op == "^") return 7;
    if (op == "|") return 6;
    if (op == "&&") return 5;
    if (op == "||") return 4;
    if (op == "??") return 3;
    return 1;
}

bool utx::infra::languages::javascript::JsGenerator::is_right_associative(const std::string &op) {
    return op == "**";
}

void utx::infra::languages::javascript::JsGenerator::emit_expression(const GraphElement &el, std::ostringstream &out,
                                                                     int parentPrec, bool isRightChild) const {
    const auto type = el.get_property("type");

    if (type == "BinaryExpression") {
        const auto jsOp = operator_converter(el.get_property("operator"));
        const int myPrec = binary_precedence(jsOp);

        // Règle parens :
        // - si on est plus faible que le parent : () nécessaires
        // - si égal : dépend associativité et si on est enfant droit
        const bool needParens =
                (myPrec < parentPrec) ||
                (myPrec == parentPrec && (is_right_associative(jsOp) ? !isRightChild : isRightChild));

        if (needParens) out << "(";

        // inside emit_expression(), when type == "BinaryExpression"
        emit_expression(*el.children().at(0), out, myPrec, false);

        // Pour matcher ton js_source (et éviter les "bad"+L / ": "+x),
        // on met des espaces autour de TOUS les opérateurs binaires.
        out << " " << jsOp << " ";

        emit_expression(*el.children().at(1), out, myPrec, true);

        if (needParens) out << ")";
        return;
    }

    // Parenthèses "réelles" du source : on les garde
    if (type == "ParenthesizedExpression") {
        out << "(";
        if (!el.children().empty()) emit_expression(*el.children()[0], out, 0, false);
        out << ")";
        return;
    }

    // fallback : pour tout le reste, ton emit actuel
    JsNodeEmitter::instance().emit(type, el, out);
}

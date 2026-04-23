#pragma once
#include <memory>
#include <filesystem>

#include "domain/Types.hpp"
#include "common/IO.hpp"
#include "common/Logger.hpp"
#include "domain/graph/Graph.hpp"
#include "domain/graph/GraphElement.hpp"
#include "infrastructure/languages/html/HtmlParser.hpp"
#include "infrastructure/languages/cpp/CppParser.hpp"
#include "infrastructure/languages/javascript/JsParser.hpp"

namespace utx::app::infrastructure::parser {
    namespace fs = std::filesystem;
    using namespace utx::domain;
    class GraphParser {
    public:


        static std::shared_ptr<graph::GraphElement> json_to_graph_element(
            const json &j,
            const std::string &name)
        {
            auto node = std::make_shared<graph::GraphElement>(name);

            if (j.is_object()) {
                std::vector<std::string> keys;
                for (auto it = j.begin(); it != j.end(); ++it)
                    keys.push_back(it.key());

                std::sort(keys.begin(), keys.end()); // 🔒 déterminisme

                for (const auto &key: keys) {
                    auto child = json_to_graph_element(j.at(key), key);
                    node->add_child(child);
                }
            } else if (j.is_array()) {
                for (size_t i = 0; i < j.size(); ++i) {
                    auto child = json_to_graph_element(j[i], std::to_string(i));
                    node->add_child(child);
                }
            } else if (j.is_string()) {
                node->set_property("value.type", "string");
                node->set_property("value", j.get<std::string>());
            } else if (j.is_boolean()) {
                node->set_property("value.type", "bool");
                node->set_property("value", j.get<bool>() ? "true" : "false");
            } else if (j.is_number_integer()) {
                node->set_property("value.type", "int");
                node->set_property("value", std::to_string(j.get<long long>()));
            } else if (j.is_number_float()) {
                node->set_property("value.type", "float");
                node->set_property("value", std::to_string(j.get<double>()));
            } else if (j.is_null()) {
                node->set_property("value.type", "null");
                node->set_property("value", "null");
            }

            return node;
        }

        static std::shared_ptr<graph::Graph> make_deploy_graph(
            const std::string &deploy_chain_uuid,
            const std::string &deploy_json) {
            auto parsed = json::parse(deploy_json);

            // Root node
            auto root = json_to_graph_element(parsed, "deploy_manifest");

            // Metadata
            root->set_property("utx.kind", "deploy_manifest");

            return std::make_shared<graph::Graph>(
                utx::common::UUID(deploy_chain_uuid),
                root
            );
        }

        /**
         * Utility: Parses a local file into a GraphElement root based on its kind.
         */
        std::shared_ptr<graph::GraphElement> parse_to_graph_element(const fs::path &path, utx::app::domain::TargetKind kind,
                                                                            const std::string &chain_id) {
            if (!fs::exists(path)) return nullptr;

            std::string content = utx::common::io::read_file(path.string());

            if (kind == utx::app::domain::TargetKind::Html) {
                try {
                    utx::infra::languages::html::HtmlParser parser;
                    const auto actions = parser.parse(content);
                    auto temp_graph = std::make_shared<graph::Graph>(utx::common::UUID(chain_id));
                    for (const auto &a: actions) {
                        utx::infra::blackhole::BlackholeRebuilder().apply_action(temp_graph, a);
                    }
                    return temp_graph->root();
                } catch (...) {
                    LOG_THIS_ERROR("{}❌ HTML parsing failed for file '{}'.{}",
                                   utx::app::domain::color::red, path.string(), utx::app::domain::color::reset);
                    return nullptr;
                }
            }
            if (kind == domain::TargetKind::Json) {
                try {
                    auto parsed = json::parse(content);
                    return json_to_graph_element(parsed, "root");
                } catch (...) {
                    LOG_THIS_ERROR("{}❌ JSON parsing failed for file '{}'.{}",
                                   utx::app::domain::color::red, path.string(), utx::app::domain::color::reset);
                    return nullptr;
                }
            }
            if (kind == utx::app::domain::TargetKind::Js) {
                try {
                    utx::infra::languages::javascript::JsParser parser(false);
                    return parser.parse(content);
                } catch (...) {
                    LOG_THIS_ERROR("{}❌ JavaScript parsing failed for file '{}'.{}",
                                   utx::app::domain::color::red, path.string(), utx::app::domain::color::reset);
                    return nullptr;
                }
            }
            if (kind == utx::app::domain::TargetKind::Css) {
                try {
                    utx::infra::languages::css::CssParser parser;
                    return parser.parse(content);
                } catch (...) {
                    LOG_THIS_ERROR("{}❌ CSS parsing failed for file '{}'.{}",
                                   utx::app::domain::color::red, path.string(), utx::app::domain::color::reset);
                    return nullptr;
                }
            }
            if (kind == domain::TargetKind::Cpp) {
                try {
                    infra::languages::cpp::CppParser::Options popt;
                    popt.emit_unnamed_tokens = true; // must be true for strict round-trip
                    popt.emit_ranges = false; // not needed for this test
                    popt.trim_whitespace_tokens = false; // keep whitespace if your parser emits it

                    const auto actions = infra::languages::cpp::CppParser::parse(content, popt);
                    auto temp_graph = std::make_shared<graph::Graph>(common::UUID(chain_id));
                    for (const auto &a: actions) {
                        infra::blackhole::BlackholeRebuilder().apply_action(temp_graph, a);
                    }
                    return temp_graph->root();
                } catch (...) {
                    return nullptr;
                    LOG_THIS_ERROR("{}❌ C++ parsing failed for file '{}'.{}",
                                   utx::app::domain::color::red, path.string(), utx::app::domain::color::reset);
                }
            }
            return nullptr;
        }
    };
}

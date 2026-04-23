#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "AbstractCommand.hpp"
#include "common/Hash.hpp"
#include "common/Logger.hpp"
#include "domain/Types.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/languages/cpp/CppGenerator.hpp"
#include "infrastructure/languages/css/CssGenerator.hpp"
#include "infrastructure/languages/html/HtmlGenerator.hpp"
#include "infrastructure/languages/javascript/JsGenerator.hpp"

namespace utx::app::use_case {
    namespace fs = std::filesystem;
    namespace app_domain = utx::app::domain;
    namespace graph = utx::domain::graph;

    class DownloadCommand final : public AbstractCommand {
    public:
        explicit DownloadCommand(infrastructure::context::AppContext ctx)
            : ctx_(std::move(ctx)) {}

        [[nodiscard]]
        int execute(const std::vector<std::string>& args) override {
            if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
                LOG_THIS_INFO(
                    "Usage: utx download <manifest_chain_id> [--api-target <host:port>] [--chain <id>] [--dry-run]");
                LOG_THIS_INFO("  <manifest_chain_id>        : Chain that stores .utx.deploy.json.");
                LOG_THIS_INFO("  --api-target <host:port>   : Override the network API target.");
                LOG_THIS_INFO("  --chain <id>               : Alias for the manifest chain id.");
                LOG_THIS_INFO("  --dry-run                  : Fetch and decode remote state without writing files.");
                return 1;
            }

            std::string manifest_chain_id;
            std::string api_target_override;
            bool dry_run = false;

            for (size_t i = 2; i < args.size(); ++i) {
                const auto& arg = args[i];
                if (arg == "--api-target" && i + 1 < args.size()) {
                    api_target_override = args[++i];
                    continue;
                }
                if (arg == "--chain" && i + 1 < args.size()) {
                    manifest_chain_id = args[++i];
                    continue;
                }
                if (arg == "--dry-run") {
                    dry_run = true;
                    continue;
                }
                if (arg.rfind("--", 0) == 0) {
                    continue;
                }
                if (manifest_chain_id.empty()) {
                    manifest_chain_id = arg;
                }
            }

            if (manifest_chain_id.empty()) {
                manifest_chain_id = ctx_.project_config.deploy_chain;
            }

            if (manifest_chain_id.empty()) {
                LOG_THIS_ERROR("{}❌ Missing manifest chain id.{}", app_domain::color::red, app_domain::color::reset);
                return 1;
            }

            if (!api_target_override.empty()) {
                ctx_.network_client.target = api_target_override;
                ctx_.project_config.api_target = api_target_override;
            } else if (!ctx_.project_config.api_target.empty()) {
                ctx_.network_client.target = ctx_.project_config.api_target;
            }

            LOG_THIS_INFO("{}⬇️ Downloading manifest from chain {} via {}...{}",
                          app_domain::color::cyan,
                          manifest_chain_id,
                          ctx_.network_client.target,
                          app_domain::color::reset);
            if (dry_run) {
                LOG_THIS_INFO("{}🧪 Dry run enabled: no local files will be written.{}",
                              app_domain::color::yellow,
                              app_domain::color::reset);
            }

            auto manifest_graph = ctx_.network_client.fetch_graph_state(manifest_chain_id);
            if (!manifest_graph) {
                LOG_THIS_ERROR("{}❌ Could not fetch manifest graph: {}{}",
                               app_domain::color::red,
                               manifest_graph.error(),
                               app_domain::color::reset);
                return 1;
            }

            const auto manifest_json = graph_element_to_json(*manifest_graph->root());
            app_domain::DeployConfig deploy_config;
            try {
                deploy_config = manifest_json.get<app_domain::DeployConfig>();
            } catch (const std::exception& e) {
                LOG_THIS_ERROR("{}❌ Failed to decode manifest: {}{}",
                               app_domain::color::red,
                               e.what(),
                               app_domain::color::reset);
                return 1;
            }

            if (!dry_run) {
                if (auto res = infrastructure::deploy::DeployConfigManager::save_deploy_config_atomic(
                        ctx_.root, deploy_config); !res) {
                    LOG_THIS_ERROR("{}❌ Could not write {}: {}{}",
                                   app_domain::color::red,
                                   infrastructure::deploy::kDeployFile,
                                   res.error(),
                                   app_domain::color::reset);
                    return 1;
                }
            }

            ctx_.deploy_config = deploy_config;
            ctx_.project_config.deploy_chain = manifest_chain_id;

            size_t downloaded = 0;
            size_t failed = 0;

            for (const auto& target : ctx_.deploy_config.targets) {
                if (target.chain.empty()) {
                    LOG_THIS_WARN("{}⚠️ Skipping {} because it has no chain id.{}",
                                  app_domain::color::yellow,
                                  target.path,
                                  app_domain::color::reset);
                    ++failed;
                    continue;
                }

                auto file_graph = ctx_.network_client.fetch_graph_state(target.chain);
                if (!file_graph || !file_graph->root()) {
                    LOG_THIS_ERROR("{}❌ Could not fetch target {} on chain {}.{}",
                                   app_domain::color::red,
                                   target.path,
                                   target.chain,
                                   app_domain::color::reset);
                    ++failed;
                    continue;
                }

                const auto content = render_target(file_graph->root(), target.kind);
                if (dry_run) {
                    LOG_THIS_INFO("{}🧪 Would restore {} ({} bytes){}",
                                  app_domain::color::yellow,
                                  target.path,
                                  content.size(),
                                  app_domain::color::reset);
                } else {
                    const auto out_path = ctx_.root / target.path;

                    std::error_code ec;
                    fs::create_directories(out_path.parent_path(), ec);
                    if (ec) {
                        LOG_THIS_ERROR("{}❌ Could not create parent directories for {}: {}{}",
                                       app_domain::color::red,
                                       target.path,
                                       ec.message(),
                                       app_domain::color::reset);
                        ++failed;
                        continue;
                    }

                    std::ofstream out(out_path, std::ios::trunc);
                    if (!out) {
                        LOG_THIS_ERROR("{}❌ Could not open {} for writing.{}",
                                       app_domain::color::red,
                                       out_path.string(),
                                       app_domain::color::reset);
                        ++failed;
                        continue;
                    }

                    out << content;
                    if (!out) {
                        LOG_THIS_ERROR("{}❌ Failed to write {}.{}",
                                       app_domain::color::red,
                                       out_path.string(),
                                       app_domain::color::reset);
                        ++failed;
                        continue;
                    }
                }

                LOG_THIS_INFO("{}✅ {} {} ({} bytes){}",
                              dry_run ? app_domain::color::yellow : app_domain::color::green,
                              dry_run ? "Would download" : "Downloaded",
                              target.path,
                              content.size(),
                              app_domain::color::reset);
                ++downloaded;
            }

            if (!dry_run) {
                ctx_.save_local_config();
            }

            LOG_THIS_INFO("{}🎯 Download complete: {} file(s) {}, {} failed.{}",
                          dry_run ? app_domain::color::yellow : app_domain::color::green,
                          downloaded,
                          dry_run ? "would be restored" : "restored",
                          failed,
                          app_domain::color::reset);

            return failed == 0 ? 0 : 1;
        }

    private:
        static bool is_numeric_name(const std::string& name) {
            if (name.empty()) return false;
            return std::ranges::all_of(name, [](unsigned char c) { return std::isdigit(c) != 0; });
        }

        static nlohmann::json scalar_from_node(const graph::GraphElement& node) {
            const auto type = node.get_property("value.type");
            const auto value = node.get_property("value");

            if (type == "string") return value;
            if (type == "bool") return value == "true";
            if (type == "int") return value.empty() ? 0 : std::stoll(value);
            if (type == "float") return value.empty() ? 0.0 : std::stod(value);
            if (type == "null") return nullptr;
            return value;
        }

        static nlohmann::json graph_element_to_json(const graph::GraphElement& node) {
            const auto value_type = node.get_property("value.type");
            if (!value_type.empty()) {
                return scalar_from_node(node);
            }

            const auto& children = node.children();
            if (!children.empty()) {
                const bool array_like = std::ranges::all_of(children, [](const auto& child) {
                    return child && is_numeric_name(child->name());
                });

                if (array_like) {
                    std::vector<std::pair<std::size_t, nlohmann::json>> ordered;
                    ordered.reserve(children.size());
                    for (const auto& child : children) {
                        ordered.emplace_back(std::stoull(child->name()), graph_element_to_json(*child));
                    }

                    std::ranges::sort(ordered, [](const auto& a, const auto& b) {
                        return a.first < b.first;
                    });

                    const bool contiguous = std::ranges::all_of(
                        ordered,
                        [expected = std::size_t{0}](const auto& entry) mutable {
                            const bool ok = entry.first == expected;
                            ++expected;
                            return ok;
                        });

                    if (contiguous) {
                        nlohmann::json arr = nlohmann::json::array();
                        for (const auto& [_, value] : ordered) {
                            arr.push_back(value);
                        }
                        return arr;
                    }
                }

                nlohmann::json obj = nlohmann::json::object();
                for (const auto& child : children) {
                    obj[child->name()] = graph_element_to_json(*child);
                }
                return obj;
            }

            nlohmann::json obj = nlohmann::json::object();
            for (const auto& [key, value] : node.properties()) {
                if (key == "id" || key == "name" || key == "parent" || key == "path" ||
                    key == "value" || key == "value.type" || key == "utx.kind") {
                    continue;
                }
                obj[key] = value;
            }
            return obj;
        }

        static std::string render_target(const std::shared_ptr<graph::GraphElement>& root,
                                         const app_domain::TargetKind kind) {
            if (!root) return {};

            switch (kind) {
                case app_domain::TargetKind::Html: {
                    utx::infra::languages::html::HtmlGenerator gen;
                    gen.visit(root);
                    return gen.get_result();
                }
                case app_domain::TargetKind::Js: {
                    utx::infra::languages::javascript::JsGenerator gen;
                    std::ostringstream oss;
                    gen.convert_node_to_js(*root, oss);
                    return oss.str();
                }
                case app_domain::TargetKind::Css: {
                    utx::infra::languages::css::CssGenerator gen;
                    std::ostringstream oss;
                    gen.convert_node_to_css(*root, oss);
                    return oss.str();
                }
                case app_domain::TargetKind::Cpp: {
                    utx::infra::languages::cpp::CppGenerator gen;
                    gen.visit(root);
                    return gen.get_result();
                }
                case app_domain::TargetKind::Json:
                    return graph_element_to_json(*root).dump(4);
                case app_domain::TargetKind::Graph:
                case app_domain::TargetKind::Identity:
                default:
                    return root->to_json().dump(4);
            }
        }

        infrastructure::context::AppContext ctx_;
    };
}

#pragma once

#include <algorithm>
#include <cctype>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <future>
#include <atomic>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AbstractCommand.hpp"
#include "common/Hash.hpp"
#include "common/Logger.hpp"
#include "common/ThreadPool.hpp"
#include "domain/Types.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/languages/cpp/CppGenerator.hpp"
#include "infrastructure/languages/css/CssGenerator.hpp"
#include "infrastructure/languages/html/HtmlGenerator.hpp"
#include "infrastructure/languages/go/GoGenerator.hpp"
#include "infrastructure/languages/javascript/JsGenerator.hpp"
#include "infrastructure/languages/markdown/MarkdownGenerator.hpp"

namespace utx::app::use_case {
    namespace fs = std::filesystem;
    namespace app_domain = utx::app::domain;
    namespace graph = utx::domain::graph;

    class DownloadCommand final : public AbstractCommand {
    public:
        static constexpr size_t kMaxParallelDownloads = 8;

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

            const auto download_started_at = std::chrono::steady_clock::now();

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

            const auto deploy_config = reconstruct_deploy_config(*manifest_graph->root());

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

            struct DownloadOutcome {
                std::string path;
                std::string chain;
                std::string error;
                std::size_t bytes{0};
            };

            std::vector<DownloadOutcome> outcomes(ctx_.deploy_config.targets.size());
            std::atomic<std::size_t> completed{0};

            if (!dry_run) {
                for (const auto& target : ctx_.deploy_config.targets) {
                    if (target.path.empty()) {
                        continue;
                    }

                    const auto out_path = ctx_.root / target.path;
                    const auto parent = out_path.parent_path();
                    if (!parent.empty()) {
                        std::error_code ec;
                        fs::create_directories(parent, ec);
                        if (ec) {
                            LOG_THIS_ERROR("{}❌ Could not create parent directories for {}: {}{}",
                                           app_domain::color::red,
                                           target.path,
                                           ec.message(),
                                           app_domain::color::reset);
                        }
                    }
                }
            }

            const std::size_t worker_count = std::max<std::size_t>(
                1,
                std::min<std::size_t>(kMaxParallelDownloads, ctx_.deploy_config.targets.size()));

            LOG_THIS_INFO("{}🧵 Downloading {} target(s) with up to {} workers...{}",
                          app_domain::color::cyan,
                          ctx_.deploy_config.targets.size(),
                          worker_count,
                          app_domain::color::reset);

            utx::common::ThreadPool pool(worker_count);
            std::vector<std::future<void>> futures;
            futures.reserve(ctx_.deploy_config.targets.size());

            std::atomic<bool> loader_running{true};
            std::thread loader_thread;
            if (!ctx_.deploy_config.targets.empty()) {
                loader_thread = std::thread([&]() {
                    const std::array<char, 4> spinner{'|', '/', '-', '\\'};
                    std::size_t tick = 0;
                    while (loader_running.load()) {
                        const auto done = completed.load();
                        std::cout << "\r" << app_domain::color::cyan
                                  << "⏳ " << spinner[tick % spinner.size()]
                                  << " Downloading " << done << "/" << ctx_.deploy_config.targets.size()
                                  << " target(s)..." << app_domain::color::reset << std::flush;
                        ++tick;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }

                    const auto done = completed.load();
                    std::cout << "\r" << std::string(80, ' ') << "\r"
                              << app_domain::color::cyan
                              << "⏳ Downloading " << done << "/" << ctx_.deploy_config.targets.size()
                              << " target(s)..." << app_domain::color::reset << std::flush;
                });
            }

            for (std::size_t index = 0; index < ctx_.deploy_config.targets.size(); ++index) {
                const auto target = ctx_.deploy_config.targets[index];
                futures.emplace_back(pool.enqueue([this, target, dry_run, index, &outcomes, &completed]() {
                    auto& outcome = outcomes[index];
                    outcome.path = target.path;
                    outcome.chain = target.chain;

                    if (target.chain.empty()) {
                        outcome.error = "missing chain id";
                        ++completed;
                        return;
                    }

                    infrastructure::chain::NetworkClient local_client{ctx_.network_client.target};
                    auto file_graph = local_client.fetch_graph_state(target.chain);
                    if (!file_graph || !file_graph->root()) {
                        outcome.error = file_graph ? "missing graph root" : file_graph.error();
                        ++completed;
                        return;
                    }

                    const auto content = render_target(file_graph->root(), target.kind);
                    outcome.bytes = content.size();

                    if (!dry_run) {
                        const auto out_path = ctx_.root / target.path;
                        std::ofstream out(out_path, std::ios::trunc);
                        if (!out) {
                            outcome.error = "could not open output file";
                            ++completed;
                            return;
                        }

                        out << content;
                        if (!out) {
                            outcome.error = "failed to write output file";
                            ++completed;
                            return;
                        }
                    }
                    ++completed;
                }));
            }

            for (auto& future : futures) {
                future.get();
            }

            loader_running = false;
            if (loader_thread.joinable()) {
                loader_thread.join();
            }

            if (!ctx_.deploy_config.targets.empty()) {
                std::cout << std::endl;
            }

            size_t downloaded = 0;
            size_t failed = 0;

            for (const auto& target : ctx_.deploy_config.targets) {
                const auto it = std::ranges::find_if(outcomes, [&](const auto& outcome) {
                    return outcome.path == target.path && outcome.chain == target.chain;
                });
                if (it == outcomes.end()) {
                    ++failed;
                    continue;
                }

                if (target.chain.empty()) {
                    LOG_THIS_WARN("{}⚠️ Skipping {} because it has no chain id.{}",
                                  app_domain::color::yellow,
                                  target.path,
                                  app_domain::color::reset);
                    ++failed;
                    continue;
                }

                if (!it->error.empty()) {
                    LOG_THIS_ERROR("{}❌ Could not restore {} on chain {}: {}{}",
                                   app_domain::color::red,
                                   target.path,
                                   target.chain,
                                   it->error,
                                   app_domain::color::reset);
                    ++failed;
                    continue;
                }

                if (dry_run) {
                    LOG_THIS_INFO("{}🧪 Would restore {} ({} bytes){}",
                                  app_domain::color::yellow,
                                  target.path,
                                  it->bytes,
                                  app_domain::color::reset);
                } else {
                    LOG_THIS_INFO("{}✅ Downloaded {} ({} bytes){}",
                                  app_domain::color::green,
                                  target.path,
                                  it->bytes,
                                  app_domain::color::reset);
                }
                ++downloaded;
            }

            if (!dry_run) {
                ctx_.save_local_config();
            }

            const auto download_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - download_started_at).count();

            LOG_THIS_INFO("{}🎯 Download complete: {} file(s) {}, {} failed in {} ms.{}",
                          dry_run ? app_domain::color::yellow : app_domain::color::green,
                          downloaded,
                          dry_run ? "would be restored" : "restored",
                          failed,
                          download_elapsed_ms,
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

        static std::shared_ptr<const graph::GraphElement> find_child_named(
            const graph::GraphElement& node,
            const std::string& name) {
            const auto& children = node.children();
            const auto it = std::ranges::find_if(children, [&](const auto& child) {
                return child && child->name() == name;
            });
            if (it == children.end()) {
                return nullptr;
            }
            return *it;
        }

        static std::optional<long long> parse_integer_node(const graph::GraphElement& node) {
            const auto value = node.get_property("value");
            if (value.empty()) {
                return std::nullopt;
            }
            try {
                return std::stoll(value);
            } catch (...) {
                return std::nullopt;
            }
        }

        static std::vector<std::shared_ptr<const graph::GraphElement>> ordered_children(
            const graph::GraphElement& node) {
            std::vector<std::shared_ptr<const graph::GraphElement>> children;
            for (const auto& child : node.children()) {
                if (child) {
                    children.push_back(child);
                }
            }
            return children;
        }

        static app_domain::DeployTarget reconstruct_target(const graph::GraphElement& node) {
            app_domain::DeployTarget target;

            if (const auto path = find_child_named(node, "path")) {
                target.path = path->get_property("value");
            }
            if (const auto chain = find_child_named(node, "chain")) {
                target.chain = chain->get_property("value");
            }
            if (const auto kind = find_child_named(node, "kind")) {
                target.kind = app_domain::parse_kind(kind->get_property("value")).value_or(app_domain::TargetKind::Graph);
            }
            if (const auto last_revision_id = find_child_named(node, "last_revision_id")) {
                target.last_revision_id = last_revision_id->get_property("value");
            }
            if (const auto last_synced_hash = find_child_named(node, "last_synced_hash")) {
                target.last_synced_hash = last_synced_hash->get_property("value");
            }
            if (const auto labels = find_child_named(node, "genesis_labels")) {
                for (const auto& label_node : ordered_children(*labels)) {
                    if (label_node) {
                        target.genesis_labels.push_back(label_node->get_property("value"));
                    }
                }
            }

            return target;
        }

        static app_domain::DeployConfig reconstruct_deploy_config(const graph::GraphElement& root) {
            app_domain::DeployConfig config;

            if (const auto version = find_child_named(root, "version")) {
                if (const auto parsed = parse_integer_node(*version)) {
                    config.version = static_cast<int>(*parsed);
                }
            }

            if (const auto targets = find_child_named(root, "targets")) {
                for (const auto& target_node : ordered_children(*targets)) {
                    if (target_node) {
                        config.targets.push_back(reconstruct_target(*target_node));
                    }
                }
            }

            return config;
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
                case app_domain::TargetKind::Markdown: {
                    utx::infra::languages::markdown::MarkdownGenerator gen;
                    gen.visit(root);
                    return gen.get_result();
                }
                case app_domain::TargetKind::Cpp: {
                    utx::infra::languages::cpp::CppGenerator gen;
                    gen.visit(root);
                    return gen.get_result();
                }
                case app_domain::TargetKind::Go: {
                    utx::infra::languages::go::GoGenerator gen;
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

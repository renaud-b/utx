#pragma once

#include <filesystem>
#include <utility>
#include "AbstractCommand.hpp"
#include "common/Logger.hpp"
#include "common/Uuid.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/ignore/IgnoreManager.hpp"

namespace utx::app::use_case {
    namespace fs = std::filesystem;

    /** Marker interface for command objects in the application domain. */
    class AddCommand final : public AbstractCommand {
    public:
        explicit AddCommand(
            infrastructure::context::AppContext  ctx
        ) :
        ctx_(std::move(ctx)) {}

        [[nodiscard]]
        int execute(const std::vector<std::string>& args) override {
            if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
                LOG_THIS_INFO(
                    "Usage: utx add <path> [--chain <id>] [--kind <kind>] [--force] [--label <label>] [--labels <label1,label2,...>]");
                LOG_THIS_INFO("  --chain <id>       : Specify the target chain ID (optional).");
                LOG_THIS_INFO("  --kind <kind>      : Specify the target kind (Graph, Html, Js, Cpp, Css).");
                LOG_THIS_INFO("  --force            : Force adding even if ignored by .utxignore.");
                LOG_THIS_INFO("  --label <label>     : Add a genesis label to the target (can be used multiple times).");
                LOG_THIS_INFO("  --labels <label1,label2,...> : Add multiple genesis labels (comma-separated).");
                return 1;
            }

            bool force = false;

            std::optional<std::string> chain_opt;
            std::optional<domain::TargetKind> kind_opt;
            std::vector<std::string> genesis_labels;


            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--force") force = true;
                if (args[i] == "--label" && i + 1 < args.size()) {
                    genesis_labels.push_back(args[++i]);
                    continue;
                }
                if (args[i] == "--labels" && i + 1 < args.size()) {
                    // comma-separated
                    std::string s = args[++i];
                    size_t pos = 0;
                    while (pos < s.size()) {
                        auto comma = s.find(',', pos);
                        auto part = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                        // trim (simple)
                        while (!part.empty() && part.front() == ' ') part.erase(0, 1);
                        while (!part.empty() && part.back() == ' ') part.pop_back();
                        if (!part.empty()) genesis_labels.push_back(part);
                        if (comma == std::string::npos) break;
                        pos = comma + 1;
                    }
                    continue;
                }
                if (args[i] == "--chain" && i + 1 < args.size()) {
                    chain_opt = args[++i];
                }
                if (args[i] == "--kind" && i + 1 < args.size()) {
                    if (auto k = domain::parse_kind(args[++i])) {
                        kind_opt = *k;
                    }
                }
            }


            fs::path target_path = fs::absolute(args[2]);
            if (!fs::exists(target_path)) {
                LOG_THIS_ERROR("❌ File not found: {}", target_path.string());
                return 1;
            }
            LOG_THIS_INFO("Adding file: {}", target_path.string());

            const auto ignores = ctx_.ignore_manager.load_ignore_set(ctx_.root);

            // Directory handling rule: --chain is only valid for single file
            if (fs::is_directory(target_path) && chain_opt.has_value()) {
                LOG_THIS_ERROR("{}❌ --chain cannot be used when adding a directory (each file needs its own chain).{}",
                               utx::app::domain::color::red, utx::app::domain::color::reset);
                return 1;
            }

            size_t added = 0;
            size_t skipped_ignored = 0;
            size_t skipped_non_regular = 0;

            // Lambda qui ajoute un fichier unique
            auto add_one_file = [&](const fs::path &file_path) {
                if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
                    skipped_non_regular++;
                    return;
                }

                const std::string target_file_rel_path = infrastructure::ignore::normalize_rel_path(fs::relative(file_path, ctx_.root).generic_string());

                if (!force && ignores.is_ignored(target_file_rel_path, false)) {
                    skipped_ignored++;
                    return;
                }

                domain::TargetKind kind = domain::TargetKind::Graph;
                if (kind_opt.has_value()) {
                    kind = *kind_opt;
                } else {
                    kind = deduce_kind_from_extension(file_path);
                }

                // Try to retrieve previous chain id in deploy config
                const auto it = std::ranges::find_if(ctx_.deploy_config.targets,
                                                     [&](const domain::DeployTarget &t) {
                                                         return t.path == target_file_rel_path;
                                                     });
                std::string chain_id;
                if (chain_opt.has_value()) {
                    LOG_THIS_INFO("{}ℹ️ Using provided chain id for {}: {}{}",
                                  domain::color::cyan, target_file_rel_path, *chain_opt,
                                  domain::color::reset);
                    chain_id = *chain_opt;
                } else if (it != ctx_.deploy_config.targets.end() && !it->chain.empty()) {
                    LOG_THIS_INFO("{}ℹ️ Reusing existing chain id for {}: {}{}",
                                  domain::color::cyan, target_file_rel_path, it->chain,
                                  domain::color::reset);
                    chain_id = it->chain;
                } else {
                    chain_id = utx::common::generate_uuid_v7().to_string();
                    LOG_THIS_INFO("{}ℹ️ Generated new chain id for {}: {}{}",
                                  domain::color::cyan, target_file_rel_path, *chain_opt,
                                  domain::color::reset);
                }

                upsert_target(ctx_.deploy_config, target_file_rel_path, chain_id, kind, "", genesis_labels);
                added++;

                LOG_THIS_INFO("  {}+{} {} -> chain:{} (kind:{}){}",
                              domain::color::green, domain::color::reset,
                              target_file_rel_path,
                              chain_id,
                              to_string(kind),
                              domain::color::reset);
            };

            if (target_path.string().find(fs::absolute(ctx_.root).string()) != 0) {
                LOG_THIS_ERROR("{}❌ Path must be inside the repository root.{}",
                               domain::color::red, domain::color::reset);
                return 1;
            }

            if (fs::is_regular_file(target_path)) {
                add_one_file(target_path);
            } else if (fs::is_directory(target_path)) {
                // Walk recursively, skipping ignored dirs early
                for (fs::recursive_directory_iterator it(target_path), end; it != end; ++it) {
                    const auto &entry = *it;

                    const auto rel = infrastructure::ignore::normalize_rel_path(fs::relative(entry.path(), ctx_.root).generic_string());
                    const bool is_dir = entry.is_directory();

                    if (!force && ignores.is_ignored(rel, is_dir)) {
                        if (is_dir) it.disable_recursion_pending();
                        skipped_ignored++;
                        continue;
                    }

                    if (entry.is_regular_file()) {
                        add_one_file(entry.path());
                    } else {
                        // sockets, symlinks, etc.
                        if (!is_dir) skipped_non_regular++;
                    }
                }
            } else {
                LOG_THIS_ERROR("{}❌ Unsupported path type: {}{}", domain::color::red, target_path.string(),
                               domain::color::reset);
                return 1;
            }

            if (added == 0) {
                LOG_THIS_WARN("{}⚠️ Nothing added. (ignored: {}, non-regular: {}){}",
                              domain::color::yellow, skipped_ignored, skipped_non_regular,
                              domain::color::reset);
                return 0;
            }

            // Sauvegarde manifeste
            std::ofstream f(ctx_.root / infrastructure::deploy::kDeployFile);
            f << json(ctx_.deploy_config).dump(4);

            LOG_THIS_INFO("\n{}✅ Add complete: {} file(s) updated. (ignored: {}, non-regular: {}){}",
                          domain::color::green, added, skipped_ignored, skipped_non_regular,
                          domain::color::reset);

            return 0;
        }
    private:


        static domain::TargetKind deduce_kind_from_extension(const fs::path &p) {
            auto ext = p.extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return std::tolower(c); });

            if (ext == ".html" || ext == ".htm") return domain::TargetKind::Html;
            if (ext == ".cpp" || ext == ".hpp") return domain::TargetKind::Cpp;
            if (ext == ".json") return utx::app::domain::TargetKind::Json;
            if (ext == ".js" || ext == ".mjs" || ext == ".cjs") return domain::TargetKind::Js;
            if (ext == ".css") return domain::TargetKind::Css;
            return domain::TargetKind::Graph;
        }

        static void upsert_target(domain::DeployConfig &deploy_config,
                                  const std::string &rel_path,
                                  const std::string &chain_id,
                                  domain::TargetKind kind,
                                  const std::string &file_hash,
                                  const std::vector<std::string> &genesis_labels) {
            auto it = std::ranges::find_if(deploy_config.targets, [&](auto &t) { return t.path == rel_path; });
            if (it != deploy_config.targets.end()) {
                it->chain = chain_id;
                it->kind = kind;
                it->last_synced_hash = file_hash;
                // only update if user provided labels in this add command
                if (!genesis_labels.empty()) {
                    it->genesis_labels = genesis_labels;
                }
            } else {
                domain::DeployTarget t;
                t.path = rel_path;
                t.chain = chain_id;
                t.kind = kind;
                t.last_revision_id = "";
                t.last_synced_hash = file_hash;
                t.genesis_labels = genesis_labels;
                deploy_config.targets.push_back(std::move(t));
            }
        }


        infrastructure::context::AppContext ctx_;
    };


}

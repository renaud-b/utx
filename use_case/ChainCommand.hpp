#pragma once

#include "AbstractCommand.hpp"
#include "../infrastructure/chain/TxManager.hpp"

namespace utx::app::use_case {
    /** cmd_chain : Manage Utopixia chains.
     * utx chain create [--labels <label1,label2,...>] [--kind <kind>] [--chain_id <id>] [--projector <projector_name>]
     * utx chain emit --chain_id <id> --content <content>
     */
    class ChainCommand final : public AbstractCommand {
    public:
        explicit ChainCommand(
            const infrastructure::context::AppContext &ctx
        ) : ctx_(ctx) {}

        int execute(const std::vector<std::string> &args) override {
            if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
                LOG_THIS_INFO(
                    "Usage:\n  utx chain create [--labels <label1,label2,...>] [--kind <kind>] [--chain_id <id>] [--projector <projector_name>]\n  utx chain emit --chain_id <id> --content <content>");
                LOG_THIS_INFO(
                    "  --labels <label1,label2,...> : Add genesis labels to the new chain (comma-separated).");
                LOG_THIS_INFO("  --kind <kind> : Specify the target kind for the chain (Graph, Html, Js, Cpp, Css, Markdown).");
                LOG_THIS_INFO(
                    "  --chain_id <id> : Optionally specify a custom chain ID (default: auto-generated UUID).");
                LOG_THIS_INFO(
                    "  --projector <projector_name> : Optionally specify a custom projector (overrides kind-based default).");
                LOG_THIS_INFO(
                    "\nExample:\n  utx chain create --labels \"blog,personal\" --kind Html --projector HtmlProjector\n  utx chain emit --chain_id <id> --content \"Hello, Utopixia!\"");
                return 1;
            }

            // utx chain create --label "testing" --projector "DecentralizedProjector@AZw_m2YKc5WEoueeMaj_QA:d5741f0e311aebf638d34765769d588d800e87887dd01111c9b1b9db080139f0"
            // utx chain emit --chain_id AZw_nvzScfSgFsW1MksqYQ --content "This is a test action emitted from the CLI."
            const std::string subcmd = args[2];
            // Emit one tx on the given chain
            if (subcmd == "emit") {
                std::string chain_id;
                std::string content;
                for (size_t i = 3; i < args.size(); ++i) {
                    if (args[i] == "--chain_id" && i + 1 < args.size()) {
                        chain_id = args[++i];
                    }
                    if (args[i] == "--content" && i + 1 < args.size()) {
                        content = args[++i];
                    }
                }
                if (chain_id.empty() || content.empty()) {
                    LOG_THIS_INFO("Usage: utx chain emit --chain_id <id> --content <content>");
                    return 1;
                }

                if (!ctx_.wallet) {
                    LOG_THIS_ERROR("{}❌ No wallet configured. Please run 'utx login <wallet_path>' first.{}", utx::app::domain::color::red, utx::app::domain::color::reset);
                    return 1;
                }
                auto action_block = infrastructure::chain::TxManager::create_transaction(*ctx_.wallet, content);
                if (!action_block) {
                    LOG_THIS_ERROR("❌ Failed to create pseudo block.");
                    return 1;
                }
                LOG_THIS_INFO("🚀 Sending action block to chain ID: {}, with content: {}",
                              chain_id, content);
                if (!ctx_.network_client.send_transaction(utx::domain::model::Address(chain_id), *action_block, *ctx_.wallet)) {
                    LOG_THIS_ERROR("❌ Failed to send action block to chain ID: {}", chain_id);
                    return 1;
                }
                LOG_THIS_INFO("✅ Emitted action on chain {}: set root.message = {}", chain_id, content);
                return 0;
            }
            if (subcmd == "create") {
                std::vector<std::string> genesis_labels;
                domain::TargetKind kind = domain::TargetKind::Graph;
                std::optional<std::string> chain_id_opt;

                std::string projector;
                for (size_t i = 3; i < args.size(); ++i) {
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
                    if (args[i] == "--kind" && i + 1 < args.size()) {
                        if (auto k = domain::parse_kind(args[++i])) {
                            kind = *k;
                        }
                    }
                    if (args[i] == "--chain_id" && i + 1 < args.size()) {
                        chain_id_opt = args[++i];
                    }
                    if (args[i] == "--projector" && i + 1 < args.size()) {
                        projector = args[++i];
                    }
                }

                const auto chain_id = chain_id_opt.value_or(common::generate_uuid_v7().to_string());
                if (!genesis_labels.empty()) {
                    LOG_THIS_INFO("   Genesis labels: {}", fmt::join(genesis_labels, ", "));
                }

                // Check if chain_id already exists in any target
                const auto chain_address = utx::domain::model::Address(chain_id);
                if (ctx_.network_client.get_last_block(chain_address)) {
                    LOG_THIS_WARN("⚠️ Chain ID {} already exists on the network.", chain_id);
                    return 1;
                }

                std::string selected_projector = "GraphProjector";
                if (kind == domain::TargetKind::Html) {
                    selected_projector = "HtmlProjector";
                } else if (kind == domain::TargetKind::Js) {
                    selected_projector = "JsProjector";
                } else if (kind == domain::TargetKind::Cpp) {
                    selected_projector = "CppProjector";
                } else if (kind == domain::TargetKind::Css) {
                    selected_projector = "CssProjector";
                } else if (kind == domain::TargetKind::Markdown) {
                    selected_projector = "MarkdownProjector";
                }

                if (!projector.empty()) {
                    selected_projector = projector;
                }

                if (!ctx_.wallet) {
                    LOG_THIS_ERROR("  {}❌ No wallet configured. Please run 'utx login <wallet_path>' first.{}", utx::app::domain::color::red, utx::app::domain::color::reset);
                    return 1;
                }
                auto genesis_transaction = infrastructure::chain::TxManager::forge_genesis_transaction(
                    *ctx_.wallet,
                    chain_address,
                    {selected_projector},
                    genesis_labels
                );
                if (!ctx_.network_client.send_transaction(chain_address, genesis_transaction, *ctx_.wallet)) {
                    LOG_THIS_ERROR("{}❌ Failed to create genesis block for chain {}.", utx::app::domain::color::red,
                                   chain_id);
                    return 1;
                }

                LOG_THIS_INFO("✅ Created new chain with ID: {}", chain_id);
                return 0;
            }


            return 0;
        }

    private:
        infrastructure::context::AppContext ctx_;
    };
}

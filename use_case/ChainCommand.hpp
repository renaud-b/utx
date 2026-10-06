#pragma once

#include "AbstractCommand.hpp"

namespace utx::app::use_case {

    class ChainCommand final : public AbstractCommand {
    public:
        explicit ChainCommand(
            const infrastructure::context::AppContext &ctx
        ) : ctx_(ctx) {}

        int execute(const std::vector<std::string> &args) override {
            if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
                LOG_THIS_INFO("Usage: utx chain <create|emit>");
                LOG_THIS_INFO(
                    "  create : unavailable in V1; a chain is created by the first supported deploy."
                );
                LOG_THIS_INFO(
                    "  emit   : send one opaque transaction through V1 prepare/submit."
                );
                return 1;
            }

            const std::string &subcmd = args[2];

            if (subcmd == "create") {
                LOG_THIS_ERROR(
                    "❌ 'utx chain create' is not available with the V1 node protocol."
                );
                LOG_THIS_INFO(
                    "   Track content with 'utx add', then use 'utx deploy'; the first deploy creates the chain."
                );
                return 1;
            }

            if (subcmd == "emit") {
                return emit(args);
            }

            LOG_THIS_ERROR("❌ Unknown chain subcommand: {}", subcmd);
            return 1;
        }

    private:
        int emit(const std::vector<std::string> &args) const {
            if (!ctx_.wallet) {
                LOG_THIS_ERROR(
                    "❌ No wallet configured. Run 'utx login <wallet_path>' first."
                );
                return 1;
            }

            std::string chain_id;
            std::string content;

            for (std::size_t i = 3; i < args.size(); ++i) {
                if (args[i] == "--chain_id" && i + 1 < args.size()) {
                    chain_id = args[++i];
                    continue;
                }

                if (args[i] == "--content" && i + 1 < args.size()) {
                    content = args[++i];
                    continue;
                }
            }

            if (chain_id.empty() || content.empty()) {
                LOG_THIS_INFO(
                    "Usage: utx chain emit --chain_id <id> --content <payload>"
                );
                return 1;
            }

            domain::DeployRequest req;
            req.chain_id = chain_id;
            req.file_path = "raw";
            req.kind = "raw";
            req.content = content;
            req.commit_message = "Raw transaction";
            req.force_snapshot = false;

            auto deploy_client = ctx_.deploy_client();
            const auto result = deploy_client.deploy(req, *ctx_.wallet);

            if (!result) {
                LOG_THIS_ERROR(
                    "❌ Transaction submission failed: {}",
                    result.error()
                );
                return 1;
            }

            LOG_THIS_INFO(
                "✅ Transaction finalized on chain {}.",
                chain_id
            );
            return 0;
        }

        const infrastructure::context::AppContext &ctx_;
    };

}

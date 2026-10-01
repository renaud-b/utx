#pragma once

#include "AbstractCommand.hpp"

namespace utx::app::use_case {

    class ChainCommand final : public AbstractCommand {
    public:
        explicit ChainCommand(
            const infrastructure::context::AppContext &
        ) {}

        int execute(const std::vector<std::string> &args) override {
            if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
                LOG_THIS_INFO("Usage: utx chain <create|emit>");
                LOG_THIS_INFO(
                    "  create : unavailable in V1; a chain is created by the first 'utx push' for a tracked target."
                );
                LOG_THIS_INFO(
                    "  emit   : unavailable in V1; raw public transaction submission was removed from the node API."
                );
                return 1;
            }

            const std::string &subcmd = args[2];

            if (subcmd == "create") {
                LOG_THIS_ERROR(
                    "❌ 'utx chain create' is not available with the V1 node protocol."
                );
                LOG_THIS_INFO(
                    "   Track content with 'utx add', then use 'utx commit' and 'utx push'; the first deploy creates the chain."
                );
                return 1;
            }

            if (subcmd == "emit") {
                LOG_THIS_ERROR(
                    "❌ 'utx chain emit' is not available with the V1 node protocol."
                );
                LOG_THIS_INFO(
                    "   Raw public transaction submission has been removed; use a supported high-level deploy command instead."
                );
                return 1;
            }

            LOG_THIS_ERROR("❌ Unknown chain subcommand: {}", subcmd);
            return 1;
        }
    };

}

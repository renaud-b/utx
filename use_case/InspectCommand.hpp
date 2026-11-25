#pragma once

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "AbstractCommand.hpp"
#include "common/Logger.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/inspection/ChainInspection.hpp"

namespace utx::app::use_case {

class InspectCommand final : public AbstractCommand {
public:
    explicit InspectCommand(infrastructure::context::AppContext ctx)
        : ctx_(std::move(ctx)) {}

    int execute(const std::vector<std::string>& args) override {
        if (args.size() < 3 || args[2] == "--help" || args[2] == "-h") {
            LOG_THIS_INFO("Usage: utx inspect <chain_id> [--api-target <host:port>] [--no-avatar]");
            return 1;
        }

        std::string chain_id;
        std::string endpoint = ctx_.project_config.api_target;
        bool show_avatar = true;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--api-target" && i + 1 < args.size()) {
                endpoint = args[++i];
            } else if (args[i] == "--no-avatar") {
                show_avatar = false;
            } else if (args[i].starts_with("--") || !chain_id.empty()) {
                LOG_THIS_ERROR("Unknown inspect argument: {}", args[i]);
                return 1;
            } else {
                chain_id = args[i];
            }
        }

        if (chain_id.empty()) {
            LOG_THIS_ERROR("Missing chain ID");
            return 1;
        }
        const auto info = infrastructure::inspection::fetch_chain_info(endpoint, chain_id);
        if (!info) {
            LOG_THIS_ERROR("Cannot inspect chain: {}", info.error());
            return 1;
        }
        infrastructure::inspection::print_chain_info(*info, std::cout, show_avatar);
        return 0;
    }

private:
    infrastructure::context::AppContext ctx_;
};

} // namespace utx::app::use_case

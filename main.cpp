// utx.cpp - Utopixia Git-like CLI (C++23)
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "common/Logger.hpp"
#include "infrastructure/context/AppContext.hpp"
#include "infrastructure/ignore/IgnoreManager.hpp"
#include "use_case/AddCommand.hpp"
#include "use_case/ApiCommand.hpp"
#include "use_case/ChainCommand.hpp"
#include "use_case/CommitCommand.hpp"
#include "use_case/GraphCommand.hpp"
#include "use_case/IdentityCommand.hpp"
#include "use_case/IgnoreCommand.hpp"
#include "use_case/InitCommand.hpp"
#include "use_case/LoginCommand.hpp"
#include "use_case/LastCommitCommand.hpp"
#include "use_case/DownloadCommand.hpp"
#include "use_case/PushCommand.hpp"
#include "use_case/StatusCommand.hpp"
#include "use_case/UncommitCommand.hpp"


/** cmd_logout : Log out of the current wallet session.
 * utx logout
 */
int cmd_logout(
    const utx::app::infrastructure::context::AppContext &ctx
) {
    auto cfg = ctx.project_config;
    cfg.wallet_path = "";
    utx::app::infrastructure::deploy::save_local_config(ctx.root, cfg);
    LOG_THIS_INFO("👋 Logged out. Local wallet reference cleared.");
    return 0;
}

int main(int argc, char **argv) {
    utx::common::logging::LoggerRegistry::instance().set_global_level(spdlog::level::info);
    std::vector<std::string> args(argv, argv + argc);
    if (args.size() < 2) {
        LOG_THIS_INFO("Utopixia CLI - Git-like tool for Utopixia projects v1.1");
        LOG_THIS_INFO("Usage: utx <command> [args]");
        return 0;
    }

    const std::string cmd = args[1];
    // Check if we have one --debug flag
    for (const auto &a: args) {
        if (a == "--debug") {
            std::cout << "🐛 Debug mode enabled." << '\n';
            utx::common::logging::LoggerRegistry::instance().set_global_level(spdlog::level::debug);
            LOG_THIS_DEBUG("🐛 Debug mode enabled.");
            break;
        }
    }

    if (cmd == "init") {
        return utx::app::use_case::InitCommand().execute(args);
    }

    if (cmd == "download") {
        utx::app::infrastructure::context::AppContext ctx;
        ctx.root = std::filesystem::current_path();

        utx::app::infrastructure::deploy::DeployConfigManager dc_manager;
        ctx.project_config = dc_manager.load_local_config(ctx.root);
        ctx.network_client = utx::app::infrastructure::chain::NetworkClient{
            ctx.project_config.api_target
        };
        ctx.tx_manager = utx::app::infrastructure::chain::TxManager{};
        ctx.graph_parser = utx::app::infrastructure::parser::GraphParser{};
        ctx.ignore_manager = utx::app::infrastructure::ignore::IgnoreManager{};
        ctx.project_label = utx::app::infrastructure::deploy::DeployConfigManager::project_label_from_root(ctx.root);

        return utx::app::use_case::DownloadCommand(std::move(ctx)).execute(args);
    }

    const auto ctx = utx::app::infrastructure::context::AppContextBuilder::build(args);
    if (!ctx) {
        LOG_THIS_ERROR("❌ Failed to build application context: {}", ctx.error());
        LOG_THIS_INFO("Have you initialized a Utopixia project in this directory with 'utx init' and configured a wallet with 'utx login <wallet_path>'?");
        return 1;
    }

    if (!ctx->wallet && cmd != "login") {
        LOG_THIS_ERROR("❌ Error: No wallet configured. Please run 'utx login <wallet_path>' first.");
        return 1;
    }

    try {
        if (cmd == "logout") return cmd_logout(ctx.value());
        if (cmd == "add") return utx::app::use_case::AddCommand(ctx.value()).execute(args);
        if (cmd == "api") return utx::app::use_case::ApiCommand(ctx.value()).execute(args);
        if (cmd == "commit") return utx::app::use_case::CommitCommand(ctx.value()).execute(args);
        if (cmd == "push") return utx::app::use_case::PushCommand(ctx.value()).execute(args);
        if (cmd == "uncommit") return utx::app::use_case::UncommitCommand(ctx.value()).execute(args);
        if (cmd == "identity") return utx::app::use_case::IdentityCommand(ctx.value()).execute(args);
        if (cmd == "login") return utx::app::use_case::LoginCommand(ctx.value()).execute(args);
        if (cmd == "last-commit") return utx::app::use_case::LastCommitCommand(ctx.value()).execute(args);
        if (cmd == "ignore") return utx::app::use_case::IgnoreCommand(ctx.value()).execute(args);
        if (cmd == "status") return utx::app::use_case::StatusCommand(ctx.value()).execute(args);
        if (cmd == "chain") return utx::app::use_case::ChainCommand(ctx.value()).execute(args);
        if (cmd == "graph") return utx::app::use_case::GraphCommand(ctx.value()).execute(args);
    } catch (const std::exception &e) {
        LOG_THIS_INFO("❌ Exception: {}", e.what());
        return 1;
    }

    LOG_THIS_INFO("❌ Unknown command: {}", cmd);
    return 1;
}

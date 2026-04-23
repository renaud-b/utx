#pragma once

#include <filesystem>
#include "AbstractCommand.hpp"
#include "common/Logger.hpp"

#include "domain/Types.hpp"
#include "infrastructure/deploy_config/DeployConfig.hpp"
#include "infrastructure/chain/NetworkClient.hpp"

namespace utx::app::use_case {

    class ApiCommand final : public AbstractCommand {
    public:
        /** Constructor
         */
        explicit ApiCommand(
            const infrastructure::context::AppContext &ctx
        )
        : ctx_(ctx) {}

        [[nodiscard]]
        int execute(const std::vector<std::string>& args) override {
            // Usage: utx api --help
            if (args.size() < 3 || args[2] == "--help") {
                LOG_THIS_INFO("Usage:\n  utx api set <api_target>");
                LOG_THIS_INFO("--api_target : Specify the Singularity API target (e.g.,127.0.0.1:8080)");
                LOG_THIS_INFO("  utx api check : Check connectivity to the current API target.");
                return 1;
            }
            // utx api set <api_target>
            if (args[2] == "set") {
                return cmd_set_target_api(args);
            }
            // utx api ping
            if (args[2] == "check") {
                if (const auto peers = ctx_.network_client.fetch_cluster_status()) {
                    // if net.target start with 127.0.0.1 or localhost, consider it local
                    if (ctx_.network_client.target.find("127.0.0.1") == 0 || ctx_.network_client.target.find("localhost") == 0) {
                        LOG_THIS_INFO("✅ [local-net] API  {}  is reachable. Cluster peers: {}", ctx_.network_client.target, peers->size());
                    } else {
                        LOG_THIS_INFO("✅ {}[test-net] API {} is reachable. Cluster peers: {}{}",
                                      utx::app::domain::color::orange, ctx_.network_client.target,
                                      peers->size(), utx::app::domain::color::reset);
                    }
                    for (const auto &p: *peers) {
                        LOG_THIS_INFO("   - {}: {}:{}", p.id.to_string(), p.ip, p.port);
                    }
                    return 0;
                }
                LOG_THIS_INFO("{}❌ API {} is not reachable.{}", utx::app::domain::color::red, ctx_.network_client.target,
                              utx::app::domain::color::reset);
                return 1;
            }
            return 0;
        }

    private:
        int cmd_set_target_api(
            const std::vector<std::string> &args) {
            // Usage: utx api set <api_target>
            if (args.size() < 4) {
                LOG_THIS_INFO("Usage: utx api set <api_target>");
                LOG_THIS_INFO("--api_target : Specify the Singularity API target (e.g., 127.0.0.1:8080)");
                return 1;
            }

            auto cfg = ctx_.project_config;
            cfg.api_target = args[3];
            infrastructure::deploy::save_local_config(ctx_.root, cfg);
            LOG_THIS_INFO("✅ API target set to {}", args[3]);
            return 0;
        }

        infrastructure::context::AppContext ctx_;
    };
}

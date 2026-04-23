#pragma once

#include "AbstractCommand.hpp"
#include "../infrastructure/ignore/IgnoreManager.hpp"

namespace utx::app::use_case {
    /** cmd_ignore : Manage .utxignore file for ignoring files from tracking.
     * utx ignore init
     * utx ignore add <pattern>
     */
    class IgnoreCommand final : public AbstractCommand {
    public:
        explicit IgnoreCommand(
            const infrastructure::context::AppContext &ctx
            ) : ctx_(ctx) {}

        int execute(const std::vector<std::string> &args) override {
            // Usage:
            // utx ignore add <pattern>
            // utx ignore init
            if (args.size() < 3) {
                LOG_THIS_INFO("Usage:\n  utx ignore init\n  utx ignore add <pattern>");
                return 1;
            }

            if (args[2] == "init") {
                const std::filesystem::path p = ctx_.root / infrastructure::ignore::kIgnoreFile;
                if (std::filesystem::exists(p)) {
                    LOG_THIS_INFO("ℹ️ {} already exists.", utx::app::infrastructure::ignore::kIgnoreFile);
                    return 0;
                }
                std::ofstream out(p);
                out << "# Utopixia ignore rules\n"
                        "# glob: *, ?, **\n"
                        "# suffix '/' => directory only\n"
                        "# prefix '!' => negate\n"
                        "\n"
                        ".utxignore\n"
                        ".env\n"
                        ".idea\n"
                        "node_modules/\n"
                        "build/\n"
                        "cmake-build-*/\n";
                std::cout << "✅ Created " << infrastructure::ignore::kIgnoreFile << '\n';
                return 0;
            }

            if (args[2] == "add") {
                if (args.size() < 4) {
                    LOG_THIS_INFO("Usage: utx ignore add <pattern>");
                    return 1;
                }
                const auto r = infrastructure::ignore::IgnoreManager::append_ignore_pattern(ctx_.root, args[3]);
                if (!r) {
                    LOG_THIS_ERROR("{}❌ {}{}", utx::app::domain::color::red, r.error(), utx::app::domain::color::reset);
                    return 1;
                }
                LOG_THIS_INFO("✅ Added to {}: {}", utx::app::infrastructure::ignore::kIgnoreFile, args[3]);
                return 0;
            }

            LOG_THIS_ERROR("❌ Unknown ignore subcommand: {}", args[2]);
            return 1;
        }

    private:
        infrastructure::context::AppContext ctx_;
    };
}

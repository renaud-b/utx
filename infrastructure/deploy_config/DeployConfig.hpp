#pragma once

#include <expected>
#include <filesystem>
#include <fstream>

#include "domain/Types.hpp"
#include "common/Logger.hpp"
#include "common/Uuid.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"

namespace utx::app::infrastructure::deploy {
    namespace fs = std::filesystem;
    static constexpr const char *kDeployFile = ".utx.deploy.json";
    static constexpr const char *kUtxDir = ".utx";
    static constexpr const char *kProjectConfigFile = ".utx/config.json";


    inline void save_local_config(const fs::path &root, const domain::ProjectConfig cfg) {
        fs::create_directories(root / kUtxDir);
        std::ofstream f(root / kProjectConfigFile);
        f << json(cfg).dump(4);
    }


    class DeployConfigManager {
    public:
        static std::expected<void, std::string>
        save_deploy_config_atomic(const fs::path &root, const domain::DeployConfig &dcfg) {
            try {
                const fs::path final_p = root / kDeployFile;
                const fs::path tmp_p = root / (std::string(kDeployFile) + ".tmp");

                // Write tmp
                {
                    std::ofstream f(tmp_p, std::ios::trunc);
                    if (!f) return std::unexpected("Failed to open temp deploy file: " + tmp_p.string());
                    f << json(dcfg).dump(4);
                    f.flush();
                    if (!f) return std::unexpected("Failed to write temp deploy file: " + tmp_p.string());
                }

                // Atomic replace on POSIX: rename over existing
                // On some platforms/filesystems, you may need to remove first.
                std::error_code ec;
                fs::rename(tmp_p, final_p, ec);
                if (ec) {
                    // fallback: remove then rename
                    std::error_code ec2;
                    fs::remove(final_p, ec2);
                    fs::rename(tmp_p, final_p, ec);
                    if (ec) {
                        return std::unexpected("Failed to replace deploy file: " + ec.message());
                    }
                }
                return {};
            } catch (const std::exception &e) {
                return std::unexpected(std::string("save_deploy_config_atomic exception: ") + e.what());
            }
        }


        static std::optional<fs::path> find_repo_root(fs::path start) {
            start = fs::absolute(start);
            while (true) {
                if (fs::exists(start / kDeployFile)) return start;
                if (start == start.root_path()) break;
                start = start.parent_path();
            }
            return std::nullopt;
        }

        std::expected<domain::DeployConfig, std::string> load_deploy_config(const fs::path &root) {
            fs::path p = root / kDeployFile;
            if (!fs::exists(p)) return std::unexpected("Deploy config file not found.");
            try {
                std::ifstream f(p);
                return json::parse(f).get<utx::app::domain::DeployConfig>();
            } catch (const std::exception &e) {
                return std::unexpected(std::string("Failed to load deploy config: ") + e.what());
            }
        }

        // Charge ou crée la config locale dans .utx/config.json
        domain::ProjectConfig load_local_config(const fs::path &root) {
            fs::path p = root / kProjectConfigFile;
            if (!fs::exists(p)) return {};
            try {
                std::ifstream f(p);
                return json::parse(f).get<domain::ProjectConfig>();
            } catch (...) { return {}; }
        }

        static std::string project_label_from_root(const fs::path &root) {
            auto name = root.filename().string();
            if (name.empty()) return "project";
            return name;
        }


        static std::expected<infra::wallet::KeyPair, std::string> load_wallet_from_config(
            const domain::ProjectConfig &pcfg)
        {
            if (pcfg.wallet_path.empty()) {
                return std::unexpected("No wallet configured. Please login first using 'utx login <wallet_path>'.");
            }
            if (!fs::exists(pcfg.wallet_path)) {
                return std::unexpected("Wallet file not found at: " + pcfg.wallet_path);
            }
            try {
                std::ifstream f(pcfg.wallet_path);
                nlohmann::json j;
                f >> j;
                return j.get<infra::wallet::KeyPair>();
            } catch (const std::exception &e) {
                return std::unexpected(std::string("Failed to load wallet: ") + e.what());
            }
        }

        static std::vector<std::string> merge_labels(const std::vector<std::string> &base_labels,
                                      const std::vector<std::string> &extra_labels) {
            std::vector<std::string> merged = base_labels;
            for (const auto &label: extra_labels) {
                if (std::find(merged.begin(), merged.end(), label) == merged.end()) {
                    merged.push_back(label);
                }
            }
            return merged;
        }



    };
}

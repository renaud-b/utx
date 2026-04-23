#pragma once

#include <string>
#include <vector>

#include "Types.hpp"

namespace utx::domain::model {
    struct ChainConfig {
        // Who can write to this chain
        std::vector<Address> owners;

        // Projector used by this chain
        std::vector<std::string> projectors; // ex: ["CoinProjector", "InboxProjector"]
        std::vector<std::string> labels;

        // Retention policy
        struct {
            uint64_t max_size_bytes = 0; // 0 = unlimited
            uint32_t max_blocks = 0;
            bool rotate = false;         // Rotate when limits are reached
        } retention;

        /** Get the default ChainConfig
         * @return ChainConfig with default values
         */
        static ChainConfig default_config() {
            return ChainConfig{
                .owners = {},
                .projectors = {},
                .retention = {
                    .max_size_bytes = 0,
                    .max_blocks = 0,
                    .rotate = false
                }
            };
        }
    };

    inline void to_json(nlohmann::json& j, const ChainConfig & config) {
        j = nlohmann::json{
                    {"owners", config.owners},
                    {"labels", config.labels},
                    {"projectors", config.projectors},
                    {"retention", {
                        {"max_size_bytes", config.retention.max_size_bytes},
                        {"max_blocks", config.retention.max_blocks},
                        {"rotate", config.retention.rotate}
                    }}
        };
    }


    inline void from_json(const nlohmann::json& j, ChainConfig & config) {
        config.owners = j.at("owners").get<std::vector<Address>>();
        if (j.contains("labels")) {
            config.labels = j.at("labels").get<std::vector<std::string>>();
        }
        config.projectors = j.at("projectors").get<std::vector<std::string>>();
        if (j.contains("retention")) {
            const auto& retention = j.at("retention");
            config.retention.max_size_bytes = retention.at("max_size_bytes").get<uint64_t>();
            config.retention.max_blocks = retention.at("max_blocks").get<uint32_t>();
            config.retention.rotate = retention.at("rotate").get<bool>();
        }
    }


}
#pragma once

#include "AtomicBlock.hpp"
#include "ChainConfig.hpp"

namespace utx::domain::model {

    /** Enumeration representing the synchronization status of a chain. */
    enum class ChainSyncStatus {
        SYNCHRONIZED, // Chain is fully synchronized
        COMMITTING,    // Block is being finalized (leader side)
        FRONTIER,     // We have only the end and the genesis of the chain
        PARTIAL,      // Currently synchronizing
        UNKNOWN       // Chain not found
    };

    inline std::string to_string(const ChainSyncStatus state) {
        switch (state) {
            case ChainSyncStatus::SYNCHRONIZED:
                return "SYNCHRONIZED";
            case ChainSyncStatus::FRONTIER:
                return "FRONTIER";
            case ChainSyncStatus::PARTIAL:
                return "PARTIAL";
            case ChainSyncStatus::COMMITTING:
                return "COMMITING";
            case ChainSyncStatus::UNKNOWN:
                return "UNKNOWN";
        }
        return "UNKNOWN"; // Default case, should not happen
    }

    inline void to_json(nlohmann::json& j, const ChainSyncStatus state) {
        j = to_string(state);
    }


    inline ChainSyncStatus from_str(const std::string& status) {
        if (status == "SYNCHRONIZED") {
            return ChainSyncStatus::SYNCHRONIZED;
        }
        if (status == "FRONTIER") {
            return ChainSyncStatus::FRONTIER;
        }
        if (status == "PARTIAL") {
            return ChainSyncStatus::PARTIAL;
        }
        if (status == "COMMITTING") {
            return ChainSyncStatus::COMMITTING;
        }
        return ChainSyncStatus::UNKNOWN;
    }

    inline void from_json(const nlohmann::json& j, ChainSyncStatus& state) {
        const std::string s = j.get<std::string>();
        state = from_str(s);
    }


    /** Structure to hold the genesis configuration and associated projectors */
    struct GenesisConfig {
        domain::model::ChainConfig config;
        std::vector<std::string> projectors;
    };

    inline void to_json(nlohmann::json& j, const GenesisConfig& gc) {
        j = nlohmann::json{
            {"config", gc.config},
            {"projectors", gc.projectors}
        };
    }

    inline void from_json(const nlohmann::json& j, GenesisConfig& gc) {
        gc.config = j.at("config").get<domain::model::ChainConfig>();
        gc.projectors = j.at("projectors").get<std::vector<std::string>>();
    }

    /** Structure to represent the frontier of a chain,
     * including the last block, current state, and genesis block.
     */
    struct ChainFrontier {
        AtomicBlock last_block;
        nlohmann::json current_state_serialized;
        AtomicBlock genesis_block;
    };

    inline void to_json(nlohmann::json& j, const ChainFrontier& cf) {
        j = nlohmann::json{
            {"last_block", cf.last_block},
            {"current_state_serialized", cf.current_state_serialized},
            {"genesis_block", cf.genesis_block}
        };
    }

    inline void from_json(const nlohmann::json& j, ChainFrontier& cf) {
        cf.last_block = j.at("last_block").get<domain::model::AtomicBlock>();
        cf.current_state_serialized = j.at("current_state_serialized");
        cf.genesis_block = j.at("genesis_block").get<domain::model::AtomicBlock>();
    }

} // namespace utx::model
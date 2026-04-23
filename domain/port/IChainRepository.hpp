#pragma once
#include <optional>
#include "../model/AtomicBlock.hpp"

namespace utx::domain::port {

    /**
     * @brief Port interface for chain storage.
     * Implementing classes can be InMemory (Test), LevelDB, RocksDB, etc.
     */
    class IChainRepository {
    public:
        virtual ~IChainRepository() = default;
        [[nodiscard]]
        virtual std::optional<model::AtomicBlock> get_block_by_hash(const model::Hash& hash) const = 0;
        [[nodiscard]]
        virtual std::vector<model::Address> get_all_known_addresses() const = 0;
        [[nodiscard]]
        virtual std::optional<model::AtomicBlock> get_last_block(const model::Address& address) const = 0;
        [[nodiscard]]
        virtual std::optional<model::AtomicBlock> get_block(const model::Address& address,  uint64_t position) const = 0;
        [[nodiscard]]
        virtual std::vector<model::AtomicBlock> get_chain_segment(const model::Address& address, uint64_t start_index, uint64_t page_size) const = 0;
        virtual void save_block(const model::AtomicBlock& block) = 0;
        [[nodiscard]]
        virtual bool chain_exists(const model::Address& address) const = 0;
        virtual void traverse_reverse(const model::Address& address,
                              std::function<bool(const model::AtomicBlock&)> callback) const = 0;
        /**
         * @brief Supprime physiquement une chaîne et toutes ses données associées.
         * @param address L'adresse de la chaîne à supprimer.
         */
        virtual void drop_chain(const model::Address& address) = 0;
    };

}
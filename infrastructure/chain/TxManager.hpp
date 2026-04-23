#pragma once
#include <expected>
#include <memory>
#include <string>
#include <httplib.h>

#include "NetworkClient.hpp"
#include "domain/Types.hpp"
#include "common/Hash.hpp"
#include "domain/model/AtomicBlock.hpp"
#include "domain/model/ChainConfig.hpp"
#include "domain/graph/Graph.hpp"
#include "domain/graph/Action.hpp"
#include "domain/graph/GraphElement.hpp"
#include "infrastructure/wallet/WalletHelper.hpp"
#include "infrastructure/crypto/OpenSSLCryptoService.hpp"
#include "infrastructure/network/Serialization.hpp"

namespace utx::app::infrastructure::chain {
    using namespace utx::domain::model;
    using namespace utx::domain::graph;
    using namespace utx::app::domain;

    class TxManager {
    public:
        /**
         * Utility: Computes the structural hash of a GraphElement (minified JSON MD5).
         */
        std::string compute_structural_hash(const std::shared_ptr<GraphElement> &el) {
            if (!el) return "";
            return utx::common::md5_hex(el->to_json().dump());
        }

        AtomicBlock forge_genesis_block(
            const utx::infra::wallet::KeyPair &wallet,
            const Address &chain_addr,
            const std::vector<std::string> &projectors,
            const std::vector<std::string> &labels = {}) {
            ChainConfig config;
            config.owners.push_back(Address(wallet.address));
            config.projectors = projectors;
            config.labels = labels;

            // On encode la config en MsgPack puis Base64 pour le transport
            std::string encoded_config = encode_config_payload(config);

            Transaction tx{
                .sender = Address(wallet.address),
                .receiver = chain_addr,
                .payload_data = encoded_config,
                .sender_public_key = wallet.public_key_hex
            };

            return AtomicBlock{
                .index = 0,
                .previous_hash = Hash::genesis(),
                .chain_address = chain_addr,
                .transaction = tx
            };
        }

        static SignedTransaction forge_genesis_transaction(
            const infra::wallet::KeyPair &wallet,
            const Address &chain_addr,
            const std::vector<std::string> &projectors,
            const std::vector<std::string> &labels = {}) {
            ChainConfig config;
            config.owners.push_back(Address(wallet.address));
            config.projectors = projectors;
            config.labels = labels;

            std::string encoded_config = encode_config_payload(config);
            return SignedTransaction{
                .sender = Address(wallet.address),
                .receiver = chain_addr,
                .amount = 0,
                .nonce = 0,
                .data = encoded_config,
                .sender_public_key = wallet.public_key_hex
            };
        }


        // --- [Correction de create_action_block pour être pur] ---
        std::optional<AtomicBlock> create_action_block(NetworkClient &client,
                                                       const Address &chain_address,
                                                       const infra::wallet::KeyPair &kp,
                                                       const Action &action) {
            const auto previous_block = client.get_last_block(chain_address);
            if (!previous_block) return std::nullopt;

            const auto my_address = Address(kp.address);

            Transaction tx{
                .sender = my_address,
                .receiver = my_address,
                .amount = 0,
                .payload_data = "urn:pi:graph:action:" + action.encode(),
                .nonce = previous_block->index + 1,
                .sender_public_key = kp.public_key_hex
            };

            return AtomicBlock{
                .index = previous_block->index + 1,
                .previous_hash = previous_block->hash,
                .chain_address = chain_address,
                .timestamp = static_cast<uint64_t>(std::time(nullptr)),
                .transaction = std::move(tx)
            };
        }

        static std::optional<SignedTransaction> create_action_transaction(
                                  const infra::wallet::KeyPair &kp,
                                  const Action &action) {

            const auto my_address = Address(kp.address);
            uint64_t random_nonce = std::random_device{}();
            SignedTransaction tx{
                .sender = my_address,
                .receiver = my_address,
                .amount = 0,
                .nonce = random_nonce,
                .data = "urn:pi:graph:action:" + action.encode(),
                .sender_public_key = kp.public_key_hex
            };
            return tx;
        }

        // --- [Correction de create_action_block pour être pur] ---
        std::optional<AtomicBlock> create_block(const NetworkClient &client,
                                                const Address &chain_address,
                                                const infra::wallet::KeyPair &kp,
                                                const std::string &data) {
            const auto previous_block = client.get_last_block(chain_address);
            if (!previous_block) return std::nullopt;

            const auto my_address = Address(kp.address);

            Transaction tx{
                .sender = my_address,
                .receiver = my_address,
                .amount = 0,
                .payload_data = data,
                .nonce = previous_block->index + 1,
                .sender_public_key = kp.public_key_hex
            };

            return AtomicBlock{
                .index = previous_block->index + 1,
                .previous_hash = previous_block->hash,
                .chain_address = chain_address,
                .timestamp = static_cast<uint64_t>(std::time(nullptr)),
                .transaction = std::move(tx)
            };
        }

        static std::optional<SignedTransaction> create_transaction(const infra::wallet::KeyPair &kp,
                                                          const std::string &data) {

            const auto my_address = Address(kp.address);
            const uint64_t random_nonce = std::rand();
            SignedTransaction tx{
                .sender = my_address,
                .receiver = my_address,
                .amount = 0,
                .nonce = random_nonce,
                .data = data,
                .sender_public_key = kp.public_key_hex
            };

            return tx;
        }

    private:
        static std::string encode_config_payload(const ChainConfig &config) {
            auto bytes = encode_msgpack(config);
            return common::base64::encode(std::string(bytes.begin(), bytes.end()));
        }
    };
}

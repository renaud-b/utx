#pragma once
#include "Types.hpp"
#include <cstdint>

#include "Attestation.hpp"
#include "../../common/Base64.hpp"

namespace utx::domain::model {
    /** @brief Enumeration of supported signature types. */
    enum class SignatureType {
        ECDSA_SECP256K1,
        DILITHIUM_MODE2 // Post-Quantum ready
    };

    /** Return a human-readable string representation of the signature type.
     * @param sig_type The signature type to convert.
     * @return A string representation of the signature type.
     */
    inline std::string sig_type_to_str(SignatureType sig_type) {
        switch (sig_type) {
            case SignatureType::DILITHIUM_MODE2:
                return "dilithium";
            default:
                return "ecdsa";
        }
    }
    /** Parse a string to determine the corresponding signature type.
     * @param str The string representation of the signature type.
     * @return The corresponding SignatureType enum value.
     */
    inline SignatureType sig_type_from_str(const std::string& str) {
        if (str == "dilithium") {
            return SignatureType::DILITHIUM_MODE2;
        }
        return SignatureType::ECDSA_SECP256K1;
    }

    /** Structure of a transaction within an atomic block. */
    struct Transaction {
        Address sender;
        Address receiver;
        uint64_t amount = 0;
        std::string payload_data;
        uint64_t nonce;
        std::string sender_public_key;
        Signature signature;

        /**
         * @brief Serializes the transaction for signing purposes.
         * Crucial for consensus integrity.
         */
        [[nodiscard]]
        std::string serialize_for_signing() const {
            return std::format("{}:{}:{}:{}:{}",
                sender, receiver, amount, payload_data, nonce);
        }
        /** @brief Retrieves the payload data as a byte vector. */
        [[nodiscard]]
        std::vector<uint8_t> payload_bytes() const {
            const auto decoded_payload = common::base64::decode(payload_data);
            return {decoded_payload.begin(), decoded_payload.end()};
        }

    };

    // Convert JSON to Transaction and vice versa
    inline void to_json(nlohmann::json& j, const Transaction& t) {
        j = nlohmann::json{
                    {"sender", t.sender.to_string()},
                    {"receiver", t.receiver.to_string()},
                    {"amount", t.amount},
                    {"data", t.payload_data},
                    {"nonce", t.nonce},
                    {"sender_pubkey", t.sender_public_key},
                    {"signature", t.signature.to_string()}
        };
    }

    inline void from_json(const nlohmann::json& j, Transaction& t) {
        t.sender = Address(j.at("sender").get<std::string>());
        t.receiver = Address(j.at("receiver").get<std::string>());
        t.amount = j.at("amount").get<uint64_t>();
        t.payload_data = j.at("data").get<std::string>();
        t.nonce = j.at("nonce").get<uint64_t>();
        t.sender_public_key = j.at("sender_pubkey").get<std::string>();
        t.signature = Signature(j.at("signature").get<std::string>());
    }

    /** Structure representing an incoming transaction request, should have signature and public key for verification */
    struct SignedTransaction {
        /** The address of the sender of the transaction. */
        Address sender;
        /** The address of the receiver of the transaction. */
        Address receiver;
        /** The amount of value being transferred in the transaction, represented as an unsigned 64-bit integer. */
        uint64_t amount = 0;
        /** A nonce value to prevent replay attacks, should be unique per sender and typically incremented with each transaction. */
        uint64_t nonce = 0;
        /** Arbitrary payload data associated with the transaction, encoded as a string for flexibility in representing various types of transactions. */
        std::string data;
        /** The public key of the sender, used for signature verification to ensure the authenticity of the transaction. */
        std::string sender_public_key;
        /** The signature of the transaction, which should be a cryptographic signature over the transaction data. */
        Signature signature;
        [[nodiscard]]
        Transaction to_transaction() const {
            return Transaction{
                .sender = sender,
                .receiver = receiver,
                .amount = amount,
                .payload_data = data,
                .nonce = nonce,
                .sender_public_key = sender_public_key,
                .signature = signature
            };
        }
        /** Serializes the signed transaction for signing purposes.
         * This should produce a consistent string representation of the transaction data that is used for signature verification.
         */
        [[nodiscard]]
        std::string serialize_for_signing() const {
            return std::format("{}:{}:{}:{}:{}",
                sender, receiver, amount, data, nonce);
        }
    };

    /** Convert JSON to SignedTransaction and vice versa */
    inline void to_json(nlohmann::json& j, const SignedTransaction& t) {
        j = nlohmann::json{
            {"sender", t.sender.to_string()},
            {"receiver", t.receiver.to_string()},
            {"amount", t.amount},
            {"nonce", t.nonce},
            {"data", t.data},
            {"sender_pubkey", t.sender_public_key},
            {"signature", t.signature.to_string()}
        };
    }
    inline void from_json(const nlohmann::json& j, SignedTransaction& t) {
        t.sender = Address(j.at("sender").get<std::string>());
        t.receiver = Address(j.at("receiver").get<std::string>());
        t.amount = j.at("amount").get<uint64_t>();
        t.nonce = j.at("nonce").get<uint64_t>();
        t.data = j.at("data").get<std::string>();
        t.sender_public_key = j.at("sender_pubkey").get<std::string>();
        t.signature = Signature(j.at("signature").get<std::string>());
    }

    /** Structure representing an atomic block in the chain. */
    struct AtomicBlock {
        /** Index of the block in the chain, starting from 0 for the genesis block. */
        uint64_t index = 0;
        /** Hash of the previous block in the chain, should be Hash::genesis() for genesis. */
        Hash previous_hash;
        /** Hash representing the state of the chain after applying this block's transaction. */
        Hash state_hash;
        /** Address of the chain */
        Address chain_address;
        /** Timestamp of block creation, represented as UNIX timestamp (seconds since epoch). */
        uint64_t timestamp = 0;
        /** The transaction contained in this block. */
        Transaction transaction;

        // --- Security ---
        /**  The signature type used for this block (defaulting to ECDSA_SECP256K1 for backward compatibility).
         *  This allows for future-proofing the protocol by supporting multiple signature schemes.
         */
        SignatureType sig_type = SignatureType::ECDSA_SECP256K1;
        /** The public key of the leader that proposed this block, used for signature verification and attestation purposes. */
        std::string leader_public_key;
        /** The address of the leader that proposed this block, used for signature verification and attestation purposes. */
        Address leader_address;
        /** The signature of the block, which should be a signature over the block's hash by the leader's private key. */
        Signature signature;
        /** The hash of the block, calculated over the block's content (excluding the hash field itself) for integrity verification. */
        Hash hash;
        /** List of attestations from other nodes confirming the validity of this block, used for consensus and finalization. */
        std::vector<Attestation> attestations;

        /** Equality operator to compare two AtomicBlocks based on their hash. */
        bool operator==(const AtomicBlock& other) const {
            return hash == other.hash;
        }

        /** Factory method to create an AtomicBlock from an IncomingTransaction, filling in the header fields */
        static AtomicBlock from_incoming_transaction(
            const SignedTransaction& tx,
            const Address& chain_address,
            const uint64_t index,
            const Hash& previous_hash,
            const Hash& state_hash,
            const uint64_t timestamp)
        {
            AtomicBlock block;
            block.index = index;
            block.previous_hash = previous_hash;
            block.state_hash = state_hash;
            block.chain_address = chain_address;
            block.timestamp = timestamp;
            block.transaction = tx.to_transaction();
            return block;
        }
    };

    /** Custom JSON serialization and deserialization for AtomicBlock, including nested structures and enum handling. */
    inline void to_json(nlohmann::json& j, const AtomicBlock& b) {
        j = nlohmann::json{
            {"index", b.index},
            {"previous_hash", b.previous_hash.to_string()},
            {"state_hash", b.state_hash.to_string()},
            {"chain_address", b.chain_address.to_string()},
            {"timestamp", b.timestamp},
            {"transaction", b.transaction},
            {"leader_address", b.leader_address.to_string()},
            {"leader_public_key", b.leader_public_key},
            {"sig_type", static_cast<int>(b.sig_type)},
            {"signature", b.signature.to_string()},
            {"hash", b.hash.to_string()},
            {"attestations", b.attestations}
        };
    }

    inline nlohmann::json to_json(const AtomicBlock& b) {
        nlohmann::json j;
        to_json(j, b);
        return j;
    }

    inline nlohmann::json header_to_json(const AtomicBlock& b, const size_t max_size = 100) {
        auto j = nlohmann::json{
            {"index", b.index},
            {"previous_hash", b.previous_hash.to_string()},
            {"state_hash", b.state_hash.to_string()},
            {"chain_address", b.chain_address.to_string()},
            {"timestamp", b.timestamp},
            {"leader_address", b.leader_address.to_string()},
            {"leader_public_key", b.leader_public_key},
            {"sig_type", static_cast<int>(b.sig_type)},
            {"signature", b.signature.to_string()},
            {"hash", b.hash.to_string()},
            {"attestations", b.attestations}
        };
        const auto tx_substr = b.transaction.serialize_for_signing().substr(0, max_size) + "...";
        j["transaction"] = tx_substr;
        return j;
    }

    inline void from_json(const nlohmann::json& j, AtomicBlock& b) {
        b.index = j.at("index").get<uint64_t>();
        b.previous_hash = Hash(j.at("previous_hash").get<std::string>());
        b.state_hash = Hash(j.at("state_hash").get<std::string>());
        b.chain_address = Address(j.at("chain_address").get<std::string>());
        b.timestamp = j.at("timestamp").get<uint64_t>();
        b.leader_address = Address(j.at("leader_address").get<std::string>());
        b.transaction = j.at("transaction").get<Transaction>();
        b.leader_public_key = j.at("leader_public_key").get<std::string>();
        b.sig_type = static_cast<SignatureType>(j.at("sig_type").get<int>());
        b.signature = Signature(j.at("signature").get<std::string>());
        b.hash = Hash(j.at("hash").get<std::string>());
        if (j.contains("attestations")) {
            b.attestations = j.at("attestations").get<std::vector<Attestation>>();
        }
    }

    /** Enumeration of possible errors that can occur during block validation and chain processing. */
    enum class ChainError {
        INVALID_SEQUENCE,       // Sequence error
        INVALID_INDEX_SEQUENCE, // When the block index does not follow the expected sequence (e.g., not incrementing by 1)
        INVALID_HASH_SEQUENCE,  // When the previous hash does not match the expected hash from the last block
        GENESIS_VIOLATION,      // Can be interpreted as sequence error
        INVALID_GENESIS_FORMAT, // Malformed genesis payload
        INVALID_SIGNATURE,      // Invalid block signature
        INVALID_HASH,           // When previous hash doesn't match the actual previous block hash
        INVALID_STATE,          // When the block's state hash doesn't match the projected state hash
        VALIDATION_FAIL,        // State projection failed, so a rule was violated
        STATE_APPLY_FAIL,       // Failed to apply state changes from the block
        PROJECTOR_APPLY_FAIL,   // Failed to apply a specific projector during state application (could include projector name in a more detailed error)
        ADDRESS_MISMATCH,       // Block's chain address doesn't match expected
        CHAIN_NOT_FOUND,        // Chain does not exist
        CONTEXT_BUILD_FAIL,     // Failed to build chain context for validation (could be due to missing data or internal error)
        REPOSITORY_ERROR,       // Generic storage/repository error
        QUORUM_NOT_REACH,       // Not enough nodes responded to reach quorum
        COMMIT_FAILED,          // Commit phase failed (when we send new block to replicas)
        STORAGE_ERROR,          // Low-level storage error
        FINALIZATION_ERROR,     // Error during finalization phase
        MISSING_CONTEXT,        // Missing chain context for non-genesis block (should not happen if sequence is correct)
    };

    /** Convert a ChainError enum value to a human-readable string message for logging and error reporting.
     * @param err The ChainError value to convert.
     * @return A string message describing the error.
     */
    inline std::string to_string(const ChainError& err) {
        switch (err) {
            case ChainError::INVALID_SEQUENCE: return "The sequence of the block is invalid (index or previous hash mismatch)";
            case ChainError::INVALID_INDEX_SEQUENCE: return "The block's index does not follow the expected sequence (should increment by 1)";
            case ChainError::INVALID_HASH_SEQUENCE: return "The block's previous hash does not match the hash of the last block in the chain";
            case ChainError::INVALID_SIGNATURE: return "The block's signature is invalid (not signed by the proposer or signature does not match content)";
            case ChainError::INVALID_HASH: return "The block's hash does not match the computed hash of its content";
            case ChainError::INVALID_STATE: return "The block's state hash does not match the projected state hash after applying the transaction";
            case ChainError::ADDRESS_MISMATCH: return "Address Mismatch: The block's chain address does not match the expected address for the block's content or sender";
            case ChainError::GENESIS_VIOLATION: return "Genesis Violation: The first block of a chain must have index 0 and previous hash of genesis";
            case ChainError::CHAIN_NOT_FOUND: return "Chain Not Found: The specified chain does not exist in the repository";
            case ChainError::REPOSITORY_ERROR: return "Repository Error: An error occurred while accessing the block repository";
            case ChainError::VALIDATION_FAIL: return "Validation Failed: The block failed business logic validation during state projection";
            case ChainError::INVALID_GENESIS_FORMAT: return "Invalid Genesis Format: The genesis block's payload is malformed or does not conform to expected structure";
            case ChainError::QUORUM_NOT_REACH: return "Quorum Not Reached: Not enough nodes responded to reach the required quorum for validation";
            case ChainError::COMMIT_FAILED: return "Commit Failed: An error occurred during the commit phase when replicating the block to other nodes";
            case ChainError::STORAGE_ERROR: return "Storage Error: A low-level error occurred while saving or retrieving data from storage";
            case ChainError::FINALIZATION_ERROR: return "Finalization Error: An error occurred during the finalization phase after block validation";
            case ChainError::MISSING_CONTEXT: return "Missing Context: The chain context is missing for a non-genesis block, which indicates a sequence error";
            case ChainError::CONTEXT_BUILD_FAIL: return "Context Build Failed: An error occurred while building the chain context for validation, possibly due to missing data or internal errors";
            default: return std::format("Unknown chain error (raw value: {})", static_cast<int>(err));
        }
    }

}

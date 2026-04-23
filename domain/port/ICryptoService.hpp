#pragma once
#include "../model/AtomicBlock.hpp"

namespace utx::domain::port {
    /**
     * @brief Port interface for cryptographic operations.
     * Decouples domain logic from OpenSSL/LibOQS.
     */
    class ICryptoService {
    public:
        virtual ~ICryptoService() = default;
        /** Verify the signature of a block and its attestations
         * @param block The AtomicBlock to verify
         * @return true if all signatures are valid, false otherwise
         */
        [[nodiscard]]
        virtual bool verify_signatures(const model::AtomicBlock& block) const = 0;
        /** Sign a hash
         * @param hash The hash to sign
         * @param private_key_hex The private key in hex format to use for signing
         * @return The generated signature as a Signature object
         */
        [[nodiscard]]
        virtual model::Signature sign_hash(const model::Hash& hash, const std::string& private_key_hex) const = 0;
        /** Calculate the hash of a block
         * @param block The AtomicBlock to hash
         * @return The calculated hash as a Hash object
         */
        [[nodiscard]]
        virtual model::Hash calculate_hash(const model::AtomicBlock& block) const = 0;
        /** Calculate the hash of a string (e.g., transaction payload)
         * @param payload The input string to hash
         * @return The calculated hash as a Hash object
         */
        [[nodiscard]]
        virtual model::Hash calculate_hash(const std::string& payload) const = 0;
        /** Verify the validity of a transaction (e.g., signature, sender address etc.)
         * @param transaction Transaction to verify
         * @return true if transaction is valid, false otherwise
         */
        [[nodiscard]]
        virtual bool verify_incoming_transaction(const model::SignedTransaction& transaction) const = 0;
        /** Verify a signature given the original string, its hash, and the signature
         * @param pub_key_hex The public key of the signer in hex format
         * @param hash The hash of the original string
         * @param signature The signature to verify
         * @return true if the signature is valid, false otherwise
         */
        [[nodiscard]]
        virtual bool verify_signature(
            const std::string & pub_key_hex,
            const std::string & hash, const std::string & signature
        ) const = 0;
        /** Derive a shared secret using ECDH given a private key and a peer's public key
         * @param private_key_hex Hex string of the private key
         * @param peer_public_key_hex Hex string of the peer's public key
         * @return Derived shared secret as a byte vector
         */
        [[nodiscard]]
        virtual std::vector<uint8_t>
        derive_shared_secret(
            const std::string& private_key_hex,
            const std::string& peer_public_key_hex
        ) const = 0;
        /** Key Derivation Function using SHA-256, takes arbitrary input and produces a 32-byte output
         * @param input Arbitrary input data as a byte vector
         * @return Derived key as a 32-byte byte vector
         */
        [[nodiscard]]
        virtual std::vector<uint8_t>
        kdf_sha256(const std::vector<uint8_t>& input) const = 0;
    };
}
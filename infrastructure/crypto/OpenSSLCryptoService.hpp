#pragma once

#include <openssl/evp.h>
#include <vector>
#include <memory>

#include "../../common/Logger.hpp"
#include "../../domain/port/ICryptoService.hpp"

namespace utx::infra::crypto {

    // Helper RAII for OpenSSL 3
    using ScopedEVP_PKEY = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using ScopedEVP_PKEY_CTX = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

    class OpenSSLCryptoService : public domain::port::ICryptoService {
    public:

        [[nodiscard]]
        domain::model::Hash calculate_hash(const std::string& payload) const  override;

        [[nodiscard]]
        domain::model::Hash calculate_hash(const domain::model::AtomicBlock& block) const override ;

        [[nodiscard]]
        domain::model::Signature sign_hash(const domain::model::Hash& hash, const std::string& private_key_hex) const override;
        /** Verify a signature given the original string, its hash, and the signature
         * @param string The original string that was signed
         * @param hash The hash of the original string
         * @param signature The signature to verify
         * @return true if the signature is valid, false otherwise
         */
        [[nodiscard]]
        bool verify_signature(const std::string & pub_key, const std::string & hash, const std::string & signature) const override;
        // Helper pour reconstruire la clé (similaire à WalletHelper)
        static ScopedEVP_PKEY create_pkey_from_priv_hex(const std::string& priv_hex);

        static std::string bytes_to_hex(const std::vector<unsigned char>& data);
        /** Verify signatures of an AtomicBlock and its attestations
         * @param block AtomicBlock to verify
         * @return true if all signatures are valid, false otherwise
         */
        [[nodiscard]]
        bool verify_signatures(const domain::model::AtomicBlock& block) const override;
        /** Verify the validity of an incoming transaction (e.g., signature, sender address etc.)
         * @param transaction Transaction to verify
         * @return true if transaction is valid, false otherwise
         */
        [[nodiscard]]
        bool verify_incoming_transaction(const domain::model::SignedTransaction& transaction) const override;
        /** Derive a shared secret using ECDH given a private key and a peer's public key
         * @param private_key_hex Hex string of the private key
         * @param peer_public_key_hex Hex string of the peer's public key
         * @return Derived shared secret as a byte vector
         */
        [[nodiscard]]
        std::vector<uint8_t>
        derive_shared_secret(
            const std::string& private_key_hex,
            const std::string& peer_public_key_hex
        ) const override;
        /** Key Derivation Function using SHA-256, takes arbitrary input and produces a 32-byte output
         * @param input Arbitrary input data as a byte vector
         * @return Derived key as a 32-byte byte vector
         */
        [[nodiscard]]
        std::vector<uint8_t>
        kdf_sha256(const std::vector<uint8_t>& input) const override;
    private:
        /** Verify ECDSA signature of an AtomicBlock
         * @param block AtomicBlock to verify
         * @return true if signature is valid, false otherwise
         */
        [[nodiscard]]
        bool verify_atomic_block_signature_with_ecdsa(const domain::model::AtomicBlock& block) const;
        /** Verify all attestations in an AtomicBlock using ECDSA
         * @param block AtomicBlock to verify
         * @return true if all attestations are valid, false otherwise
         */
        [[nodiscard]]
        bool verify_block_attestations_with_ecdsa(const domain::model::AtomicBlock& block) const;
        /** Helper to reconstruct EVP_PKEY from raw Hex Public Key
         * @param pub_hex Hex string of the public key
         * @return ScopedEVP_PKEY object representing the public key
         */
        [[nodiscard]]
        static ScopedEVP_PKEY create_pubkey_from_hex(const std::string& pub_hex);
        /** Convert a hex string to a byte vector
         * @param hex Hex string to convert
         * @return Byte vector representation of the hex string
         */
        [[nodiscard]]
        static std::vector<unsigned char> hex_to_bytes(const std::string& hex);
    };
}
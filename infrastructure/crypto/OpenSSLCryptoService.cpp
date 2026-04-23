#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/sha.h>
#include <vector>
#include <iomanip>
#include <sstream>
#include <memory>
#include <execution>
#include <algorithm>

#include "OpenSSLCryptoService.hpp"
#include "../wallet/WalletHelper.hpp"

namespace utx::infra::crypto {

    domain::model::Hash OpenSSLCryptoService::calculate_hash(const std::string& payload) const  {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(payload.c_str()), payload.size(), hash);

        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for(const unsigned char i : hash) ss << std::setw(2) << static_cast<int>(i);

        return domain::model::Hash(ss.str());
    }

    domain::model::Hash OpenSSLCryptoService::calculate_hash(const domain::model::AtomicBlock& block) const {
        const std::string serialized = std::format("{}{}{}{}{}{}{}{}{}",
            block.index,
            block.previous_hash.to_string(),
            block.state_hash.to_string(),
            block.chain_address.to_string(),
            block.timestamp,
            block.leader_public_key,
            block.leader_address.to_string(),
            block.transaction.sender_public_key,
            block.transaction.signature.to_string(),
            block.transaction.serialize_for_signing()
        );

        return calculate_hash(serialized);
    }

    domain::model::Signature OpenSSLCryptoService::sign_hash(const domain::model::Hash& hash, const std::string& private_key_hex) const {
        try {
            // 1. Reconstitution de la clé privée
            auto pkey = create_pkey_from_priv_hex(private_key_hex);

            // 2. Initialisation du contexte de signature
            EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
            if (!ctx_raw) return domain::model::Signature("");
            ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);

            if (EVP_PKEY_sign_init(ctx.get()) <= 0) return domain::model::Signature("");

            // 3. Préparation des données (Hash -> Bytes)
            std::vector<unsigned char> hash_bytes = hex_to_bytes(hash.to_string());
            size_t sig_len = 0;

            // Déterminer la taille de la signature
            if (EVP_PKEY_sign(ctx.get(), nullptr, &sig_len, hash_bytes.data(), hash_bytes.size()) <= 0)
                return domain::model::Signature("");


            std::vector<unsigned char> sig_bytes(sig_len);

            // Signer réellement
            if (EVP_PKEY_sign(ctx.get(), sig_bytes.data(), &sig_len, hash_bytes.data(), hash_bytes.size()) <= 0)
                return domain::model::Signature("");

            sig_bytes.resize(sig_len);
            return domain::model::Signature(bytes_to_hex(sig_bytes));

        } catch (...) {
            return domain::model::Signature("");
        }
    }

    bool OpenSSLCryptoService::verify_signature(const std::string & pub_key, const std::string & hash, const std::string & signature) const {
        try {
            const auto pkey = create_pubkey_from_hex(pub_key);
            if (!pkey) {
                LOG_THIS_ERROR("Failed to create public key from hex for signature verification");
                return false;
            }
            const std::vector<unsigned char> hash_bytes = hex_to_bytes(hash);
            const std::vector<unsigned char> sig_bytes = hex_to_bytes(signature);
            EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
            if (!ctx_raw) {
                LOG_THIS_ERROR("Failed to create EVP_PKEY_CTX for signature verification");
                return false;
            }
            ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);
            if (EVP_PKEY_verify_init(ctx.get()) <= 0) {
                LOG_THIS_ERROR("Failed to initialize signature verification");
                return false;
            }

            int ret = EVP_PKEY_verify(ctx.get(),
                                      sig_bytes.data(), sig_bytes.size(),
                                      hash_bytes.data(), hash_bytes.size());
            return ret == 1;
        } catch (const std::exception& e) {
            LOG_THIS_ERROR("Exception during signature verification: {}", e.what());
            return false;
        }

    }
    // Helper pour reconstruire la clé (similaire à WalletHelper)
    ScopedEVP_PKEY OpenSSLCryptoService::create_pkey_from_priv_hex(const std::string& priv_hex) {
        // 1️⃣ Convert hex → BIGNUM
        BIGNUM* priv_bn = nullptr;
        if (!BN_hex2bn(&priv_bn, priv_hex.c_str()) || !priv_bn) {
            LOG_THIS_ERROR("Failed to convert private key hex to BIGNUM: invalid hex string");
            throw std::runtime_error("Invalid private key hex");
        }

        std::unique_ptr<BIGNUM, decltype(&BN_free)> bn_guard(priv_bn, BN_free);

        // 2️⃣ Build params
        OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
        if (!bld)
            throw std::runtime_error("OSSL_PARAM_BLD_new failed");

        if (OSSL_PARAM_BLD_push_utf8_string(bld, "group", "secp256k1", 0) != 1)
            throw std::runtime_error("Failed to push group param");

        if (OSSL_PARAM_BLD_push_BN(bld, "priv", priv_bn) != 1)
            throw std::runtime_error("Failed to push private key param");

        OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
        OSSL_PARAM_BLD_free(bld);

        if (!params)
            throw std::runtime_error("OSSL_PARAM_BLD_to_param failed");

        std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)>
            params_guard(params, OSSL_PARAM_free);

        // 3️⃣ Create EC key from params
        EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        if (!ctx_raw)
            throw std::runtime_error("EVP_PKEY_CTX_new_from_name failed");

        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>
            ctx(ctx_raw, EVP_PKEY_CTX_free);

        if (EVP_PKEY_fromdata_init(ctx.get()) <= 0)
            throw std::runtime_error("EVP_PKEY_fromdata_init failed");

        EVP_PKEY* pkey_raw = nullptr;
        if (EVP_PKEY_fromdata(ctx.get(), &pkey_raw, EVP_PKEY_KEYPAIR, params) <= 0)
            throw std::runtime_error("EVP_PKEY_fromdata failed");

        if (!pkey_raw)
            throw std::runtime_error("EVP_PKEY_fromdata returned null key");

        ScopedEVP_PKEY pkey(pkey_raw, EVP_PKEY_free);

        // 4️⃣ Now validate private key
        EVP_PKEY_CTX* check_ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
        if (!check_ctx_raw)
            throw std::runtime_error("Private key validation ctx failed");

        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>
            check_ctx(check_ctx_raw, EVP_PKEY_CTX_free);

        if (EVP_PKEY_private_check(check_ctx.get()) <= 0)
            throw std::runtime_error("Invalid EC private key");

        return pkey;
    }

    std::string OpenSSLCryptoService::bytes_to_hex(const std::vector<unsigned char>& data) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (auto b : data) ss << std::setw(2) << static_cast<int>(b);
        return ss.str();
    }
    /** Verify signatures of an AtomicBlock and its attestations
     * @param block AtomicBlock to verify
     * @return true if all signatures are valid, false otherwise
     */
    bool OpenSSLCryptoService::verify_signatures(const domain::model::AtomicBlock& block) const {

        if (!verify_atomic_block_signature_with_ecdsa(block)) {
            LOG_THIS_ERROR("AtomicBlock signature verification failed for block index {}", block.index);
            return false;
        }
        if (!verify_block_attestations_with_ecdsa(block)) {
            LOG_THIS_ERROR("AtomicBlock attestation verification failed for block index {}", block.index);
            return false;
        }

        const auto transaction = block.transaction;
        try {
            const auto pkey = create_pubkey_from_hex(transaction.sender_public_key);
            if (!pkey) {
                LOG_THIS_ERROR("Failed to create public key from hex for transaction signature verification");
                return false;
            }

            const auto address = infra::wallet::WalletHelper::derive_address(transaction.sender_public_key);
            if (transaction.sender.to_string() != address) {
                LOG_THIS_ERROR("Transaction sender address mismatch: expected {}, got {}",
                    address, transaction.sender.to_string());
                return false;
            }

            const std::string serialized_tx = transaction.serialize_for_signing();
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char*>(serialized_tx.c_str()), serialized_tx.size(), hash);
            std::vector hash_bytes(hash, hash + SHA256_DIGEST_LENGTH);
            std::vector<unsigned char> sig_bytes = hex_to_bytes(transaction.signature.to_string());
            EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
            if (!ctx_raw) {
                LOG_THIS_ERROR("Failed to create EVP_PKEY_CTX for transaction signature verification");
                return false;
            }
            ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);
            if (EVP_PKEY_verify_init(ctx.get()) <= 0) {
                LOG_THIS_ERROR("Failed to initialize signature verification for transaction");
                return false;
            }
            int ret = EVP_PKEY_verify(ctx.get(),
                                      sig_bytes.data(), sig_bytes.size(),
                                      hash_bytes.data(), hash_bytes.size());
            if (ret != 1) {
                LOG_THIS_ERROR("Transaction signature verification failed for sender {}: OpenSSL returned {}",
                    transaction.sender.to_string(), ret);
                return false;
            }

            return true;
        } catch (const std::exception& e) {
            LOG_THIS_ERROR("Exception during transaction verification: {}", e.what());
            return false;
        }
    }
    /** Verify the validity of an incoming transaction (e.g., signature, sender address etc.)
     * @param transaction Transaction to verify
     * @return true if transaction is valid, false otherwise
     */
    bool OpenSSLCryptoService::verify_incoming_transaction(const domain::model::SignedTransaction& transaction) const {
        try {
            const auto pkey = create_pubkey_from_hex(transaction.sender_public_key);
            if (!pkey) return false;

            const auto address = wallet::WalletHelper::derive_address(transaction.sender_public_key);
            if (transaction.sender.to_string() != address) {
                LOG_THIS_ERROR("Transaction sender address mismatch: expected {}, got {}",
                    address, transaction.sender.to_string());
                return false;
            }

            const std::string serialized_tx = transaction.serialize_for_signing();
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256(reinterpret_cast<const unsigned char*>(serialized_tx.c_str()), serialized_tx.size(), hash);
            std::vector<unsigned char> hash_bytes(hash, hash + SHA256_DIGEST_LENGTH);
            std::vector<unsigned char> sig_bytes = hex_to_bytes(transaction.signature.to_string());
            EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
            if (!ctx_raw) return false;
            ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);
            if (EVP_PKEY_verify_init(ctx.get()) <= 0) return false;
            int ret = EVP_PKEY_verify(ctx.get(),
                                      sig_bytes.data(), sig_bytes.size(),
                                      hash_bytes.data(), hash_bytes.size());
            if (ret != 1) {
                LOG_THIS_ERROR("Transaction signature verification failed for sender {}: OpenSSL returned {}",
                    transaction.sender.to_string(), ret);
                return false;
            }

            return true;
        } catch (const std::exception& e) {
            LOG_THIS_ERROR("Exception during transaction verification: {}", e.what());
            return false;
        }
    }
    /** Verify ECDSA signature of an AtomicBlock
     *  We verify the block's signature against the block's hash, using the leader's public key.
     * @param block AtomicBlock to verify
     * @return true if signature is valid, false otherwise
     */
    bool OpenSSLCryptoService::verify_atomic_block_signature_with_ecdsa(const domain::model::AtomicBlock& block) const {
        if (block.sig_type != domain::model::SignatureType::ECDSA_SECP256K1) return false;
        try {
            // 1. Reconstruct Public Key from Hex
            auto pkey = create_pubkey_from_hex(block.leader_public_key);
            if (!pkey) {
                LOG_THIS_ERROR("Failed to create public key from hex for block signature verification");
                return false;
            }

            // 2. Create Verify Context
            EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
            if (!ctx_raw) {
                LOG_THIS_ERROR("Failed to create EVP_PKEY_CTX for block signature verification");
                return false;
            }
            ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);

            if (EVP_PKEY_verify_init(ctx.get()) <= 0) {
                LOG_THIS_ERROR("Failed to initialize signature verification for block");
                return false;
            }

            // 3. Prepare Data
            // The signature signs the HASH of the block
            const auto block_hash = calculate_hash(block);
            std::string hash_hex = block_hash.to_string();
            if (block.hash.to_string() != hash_hex) {
                LOG_THIS_ERROR("Block hash mismatch for chain {} during signature verification: expected {}, got {}",
                    block.chain_address.to_string(), hash_hex, block.hash.to_string());
                return false;
            }
            std::vector<unsigned char> hash_bytes = hex_to_bytes(hash_hex);
            std::vector<unsigned char> sig_bytes = hex_to_bytes(block.signature.to_string());

            if (block.hash != block_hash) {
                LOG_THIS_ERROR("Block hash mismatch during signature verification: expected {}, got {}",
                    block_hash.to_string(), block.hash.to_string());
                return false;
            }
            // 4. Verify
            // Returns 1 for success
            int ret = EVP_PKEY_verify(ctx.get(),
                                      sig_bytes.data(), sig_bytes.size(),
                                      hash_bytes.data(), hash_bytes.size());

            return (ret == 1);

        } catch (const std::exception& e) {
            LOG_THIS_ERROR("Exception during block signature verification: {}", e.what());
            return false;
        }
    }

    /** Verify all attestations in an AtomicBlock using ECDSA
     * @param block AtomicBlock to verify
     * @return true if all attestations are valid, false otherwise
     */
    bool OpenSSLCryptoService::verify_block_attestations_with_ecdsa(const domain::model::AtomicBlock& block) const {
        // Calculate the hash once
        const std::string hash_hex = calculate_hash(block).to_string();
        const std::vector<unsigned char> hash_bytes = hex_to_bytes(hash_hex);

        // Concurrent verification of attestations
        return std::all_of(std::execution::par,
                           block.attestations.begin(),
                           block.attestations.end(),
                           [&](const auto& att) {

            // A. Reconstruct Public Key (Thread-local)
            auto pkey = create_pubkey_from_hex(att.public_key);
            EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
            // B. Create Verify Context (Thread-local)
            if (!ctx_raw) return false;


            // Use RAII for context management
            const ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);
            if (EVP_PKEY_public_check(ctx.get()) <= 0)
                return false;

            if (EVP_PKEY_verify_init(ctx.get()) <= 0) return false;

            // C. Prepare Signature Bytes
            // The signature signs the HASH of the block
            const std::vector<unsigned char> sig_bytes = hex_to_bytes(att.signature.to_string());

            // Verify
            const int ret = EVP_PKEY_verify(ctx.get(),
                                      sig_bytes.data(), sig_bytes.size(),
                                      hash_bytes.data(), hash_bytes.size());

            return (ret == 1);
        });
    }
    // Helper to reconstruct EVP_PKEY from raw Hex Public Key
    ScopedEVP_PKEY OpenSSLCryptoService::create_pubkey_from_hex(const std::string& pub_hex) {
        std::vector<unsigned char> pub_bytes = hex_to_bytes(pub_hex);

        OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
        OSSL_PARAM_BLD_push_utf8_string(bld, "group", "secp256k1", 0);
        OSSL_PARAM_BLD_push_octet_string(bld, "pub", pub_bytes.data(), pub_bytes.size());

        OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
        OSSL_PARAM_BLD_free(bld);

        EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
        EVP_PKEY_fromdata_init(ctx);
        EVP_PKEY* pkey = nullptr;
        EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params);

        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);

        if (!pkey) throw std::runtime_error("Invalid public key format");
        return ScopedEVP_PKEY(pkey, EVP_PKEY_free);
    }

    std::vector<unsigned char>
    OpenSSLCryptoService::hex_to_bytes(const std::string& hex)
    {
        if (hex.size() % 2 != 0) {
            LOG_THIS_ERROR("Invalid hex string length: {}", hex.size());
            throw std::runtime_error("Invalid hex length");
        }

        std::vector<unsigned char> bytes;
        bytes.reserve(hex.size() / 2);

        for (size_t i = 0; i < hex.size(); i += 2)
        {
            unsigned int value;
            std::stringstream ss;
            ss << std::hex << hex.substr(i, 2);
            ss >> value;

            if (ss.fail()) {
                LOG_THIS_ERROR("Invalid hex character in string: {}", hex.substr(i, 2));
                throw std::runtime_error("Invalid hex encoding");
            }

            bytes.push_back(static_cast<unsigned char>(value));
        }
        return bytes;
    }

    std::vector<uint8_t>
    OpenSSLCryptoService::derive_shared_secret(
        const std::string& private_key_hex,
        const std::string& peer_public_key_hex
        ) const
    {
        auto priv_key = create_pkey_from_priv_hex(private_key_hex);
        auto peer_pub = create_pubkey_from_hex(peer_public_key_hex);

        // 🔴 Validate EC public key
        EVP_PKEY_CTX* check_ctx_raw = EVP_PKEY_CTX_new(peer_pub.get(), nullptr);
        if (!check_ctx_raw) {
            LOG_THIS_ERROR("Failed to create EVP_PKEY_CTX for EC validation");
            throw std::runtime_error("EC validation ctx failed");
        }


        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>
            check_ctx(check_ctx_raw, EVP_PKEY_CTX_free);

        if (EVP_PKEY_public_check(check_ctx.get()) <= 0) {
            LOG_THIS_ERROR("Invalid EC public key: {}", peer_public_key_hex);
            throw std::runtime_error("Invalid EC public key");
        }

        EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new_from_pkey(nullptr, priv_key.get(), nullptr);
        if (!ctx_raw) {
            LOG_THIS_ERROR("Failed to create EVP_PKEY_CTX for ECDH derive");
            throw std::runtime_error("ECDH ctx alloc failed");
        }

        ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);
        EVP_PKEY_public_check(ctx.get());

        if (EVP_PKEY_derive_init(ctx.get()) <= 0) {
            LOG_THIS_ERROR("Failed to initialize ECDH derive context");
            throw std::runtime_error("ECDH derive init failed");
        }

        if (EVP_PKEY_derive_set_peer(ctx.get(), peer_pub.get()) <= 0) {
            LOG_THIS_ERROR("Failed to set ECDH peer key");
            throw std::runtime_error("ECDH set peer failed");
        }

        size_t secret_len = 0;
        EVP_PKEY_derive(ctx.get(), nullptr, &secret_len);

        std::vector<uint8_t> secret(secret_len);

        if (EVP_PKEY_derive(ctx.get(), secret.data(), &secret_len) <= 0) {
            LOG_THIS_ERROR("Failed to derive ECDH shared secret");
            throw std::runtime_error("ECDH derive failed");
        }

        secret.resize(secret_len);
        return secret;
    }

    std::vector<uint8_t>
    OpenSSLCryptoService::kdf_sha256(const std::vector<uint8_t>& input) const {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(input.data(), input.size(), hash);
        return std::vector<uint8_t>(hash, hash + SHA256_DIGEST_LENGTH);
    }
}
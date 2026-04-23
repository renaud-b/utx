#pragma once
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>
#include <vector>
#include <string>
#include <sstream>
#include <memory>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include "../../common/Helper.hpp"

namespace utx::infra::wallet {

    struct KeyPair {
        std::string private_key_hex;
        std::string public_key_hex;
        std::string address;
    };

    // Smart pointer pour gestion auto de la mémoire OpenSSL
    using ScopedEVP_PKEY = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using ScopedEVP_PKEY_CTX = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
    using ScopedOSSL_PARAM_BLD = std::unique_ptr<OSSL_PARAM_BLD, decltype(&OSSL_PARAM_BLD_free)>;
    using ScopedOSSL_PARAM = std::unique_ptr<OSSL_PARAM, decltype(&OSSL_PARAM_free)>;

    class WalletHelper {
    public:
        // ---------------------------------------------------------------------
        // GENERATION (OpenSSL 3.0 Style)
        // ---------------------------------------------------------------------
        static KeyPair generate_keypair() {
            // 1. Generate Key using the High-Level "Q" API (Quick)
            EVP_PKEY* pkey_raw = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "secp256k1");
            if (!pkey_raw) throw std::runtime_error("Key generation failed");
            const ScopedEVP_PKEY pkey(pkey_raw, EVP_PKEY_free);

            // 2. Extract Private Key (BigNum to Hex)
            BIGNUM* priv_bn = nullptr;
            if (!EVP_PKEY_get_bn_param(pkey.get(), OSSL_PKEY_PARAM_PRIV_KEY, &priv_bn)) {
                throw std::runtime_error("Failed to extract private key");
            }
            char* priv_hex = BN_bn2hex(priv_bn);
            std::string priv_str(priv_hex);
            OPENSSL_free(priv_hex);
            BN_free(priv_bn);

            // 3. Extract Public Key (Point Octets to Hex)
            // OpenSSL 3 exports pubkey as an Octet String for EC
            size_t pub_len = 0;
            if (!EVP_PKEY_get_octet_string_param(pkey.get(), OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0, &pub_len)) {
                throw std::runtime_error("Failed to get pubkey length");
            }
            std::vector<unsigned char> pub_bytes(pub_len);
            if (!EVP_PKEY_get_octet_string_param(pkey.get(), OSSL_PKEY_PARAM_PUB_KEY, pub_bytes.data(), pub_len, nullptr)) {
                throw std::runtime_error("Failed to extract public key");
            }

            KeyPair kp;
            kp.private_key_hex = priv_str;
            kp.public_key_hex = common::bytes_to_hex(pub_bytes);
            kp.address = derive_address(kp.public_key_hex);

            return kp;
        }

        static bool save_to_file(const KeyPair& kp, const std::string& path) {
            try {
                nlohmann::json j = {
                    {"address", kp.address},
                    {"public_key", kp.public_key_hex},
                    {"private_key", kp.private_key_hex}
                };
                std::ofstream file(path);
                file << j.dump(4);
                return true;
            } catch (...) { return false; }
        }

        static KeyPair load_from_file(const std::string& path) {
            std::ifstream file(path);
            nlohmann::json j;
            file >> j;
            if (j.contains("private_key") && j.contains("public_key") && j.contains("address")) {
                return {
                    j.at("private_key").get<std::string>(),
                    j.at("public_key").get<std::string>(),
                    j.at("address").get<std::string>()
                };
            } if (j.contains("private_key_hex") && j.contains("public_key_hex") && j.contains("address")) {
                return {
                    j.at("private_key_hex").get<std::string>(),
                    j.at("public_key_hex").get<std::string>(),
                    j.at("address").get<std::string>()
                };
            }
            throw std::runtime_error("Invalid wallet file format");
        }

        // ---------------------------------------------------------------------
        // SIGNATURE (OpenSSL 3.0 Style)
        // ---------------------------------------------------------------------
        static std::string sign_message(const std::string& private_key_hex, const std::string& message_hash_hex) {
            // 1. Rebuild Keypair from Hex Private Key
            const auto pkey = create_pkey_from_priv_hex(private_key_hex);

            // 2. Initialize Signing Context
            EVP_PKEY_CTX* ctx_raw = EVP_PKEY_CTX_new(pkey.get(), nullptr);
            if (!ctx_raw) throw std::runtime_error("Failed to create context");
            const ScopedEVP_PKEY_CTX ctx(ctx_raw, EVP_PKEY_CTX_free);

            if (EVP_PKEY_sign_init(ctx.get()) <= 0) throw std::runtime_error("Sign init failed");

            // 3. Sign the Hash
            const std::vector<unsigned char> hash_bytes = common::hex_to_bytes(message_hash_hex);

            size_t sig_len = 0;
            // First call to get length
            if (EVP_PKEY_sign(ctx.get(), nullptr, &sig_len, hash_bytes.data(), hash_bytes.size()) <= 0) {
                throw std::runtime_error("Sign sizing failed");
            }

            std::vector<unsigned char> signature(sig_len);
            // Second call to sign
            if (EVP_PKEY_sign(ctx.get(), signature.data(), &sig_len, hash_bytes.data(), hash_bytes.size()) <= 0) {
                throw std::runtime_error("Signing failed");
            }

            // Resize to actual length (DER format can vary in size)
            signature.resize(sig_len);
            return common::bytes_to_hex(signature);
        }
        static std::string derive_address(const std::string& pub_hex) {
            // 1. Convertir la string Hex en Bytes bruts
            // "04AF..." -> [0x04, 0xAF...]
            std::vector<unsigned char> pub_bytes = common::hex_to_bytes(pub_hex);

            // 2. Hacher les bytes bruts (Standard Ethereum/Bitcoin)
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256(pub_bytes.data(), pub_bytes.size(), hash);

            // 3. Convertir le hash résultant en Hex
            std::string hash_hex = common::bytes_to_hex(std::vector<unsigned char>(hash, hash + SHA256_DIGEST_LENGTH));

            // 4. Garder les 40 derniers caractères (20 derniers bytes)
            // SHA256 fait 64 chars hex. On prend les 40 derniers.
            if (hash_hex.length() > 40) {
                return "0x" + hash_hex.substr(hash_hex.length() - 40);
            }
            return "0x" + hash_hex;
        }
        // ---------------------------------------------------------------------
        // HELPERS
        // ---------------------------------------------------------------------
    private:
        static ScopedEVP_PKEY create_pkey_from_priv_hex(const std::string& priv_hex) {
            BIGNUM* priv_bn = BN_new();
            BN_hex2bn(&priv_bn, priv_hex.c_str());

            OSSL_PARAM_BLD* bld = OSSL_PARAM_BLD_new();
            OSSL_PARAM_BLD_push_utf8_string(bld, "group", "secp256k1", 0);
            OSSL_PARAM_BLD_push_BN(bld, "priv", priv_bn);

            OSSL_PARAM* params = OSSL_PARAM_BLD_to_param(bld);
            OSSL_PARAM_BLD_free(bld);
            BN_free(priv_bn);

            EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
            EVP_PKEY_fromdata_init(ctx);
            EVP_PKEY* pkey = nullptr;
            EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_KEYPAIR, params);

            EVP_PKEY_CTX_free(ctx);
            OSSL_PARAM_free(params);

            if(!pkey) throw std::runtime_error("Failed to reconstruct private key");
            return {pkey, EVP_PKEY_free};
        }

    };

    inline void to_json(nlohmann::json& j, const KeyPair& kp) {
        // output as JWK
        j = nlohmann::json{
            {"private_key_hex", kp.private_key_hex},
            {"public_key_hex", kp.public_key_hex},
            {"address", kp.address}
        };
    }

    inline void from_json(const nlohmann::json& j, KeyPair& kp) {
        j.at("private_key_hex").get_to(kp.private_key_hex);
        j.at("public_key_hex").get_to(kp.public_key_hex);
        j.at("address").get_to(kp.address);
    }
}

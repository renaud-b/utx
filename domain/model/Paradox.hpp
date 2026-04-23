#pragma once

#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include "Types.hpp"
#include "../../common/Hash.hpp"
#include "../../common/Base64.hpp"
#include "../../infrastructure/wallet/WalletHelper.hpp"
#include "../port/ICryptoService.hpp"

namespace utx::domain::model {

    struct EncryptedSecretShare {
        uint32_t index = 0;                       // fragment index (1..n)
        std::vector<uint8_t> encrypted_data;      // encrypted fragment using session_pubkey

        [[nodiscard]]
        nlohmann::json to_json() const {
            const auto j = nlohmann::json{
                {"index", index},
                {"data", encrypted_data}
            };
            return j;
        }

        [[nodiscard]]
        std::string serialize_for_signing() const {
            std::vector<uint8_t> buffer;

            // index in fixed 4 bytes big endian
            buffer.push_back((index >> 24) & 0xFF);
            buffer.push_back((index >> 16) & 0xFF);
            buffer.push_back((index >>  8) & 0xFF);
            buffer.push_back(index & 0xFF);

            // encrypted data length in fixed 4 bytes big endian
            uint32_t len = static_cast<uint32_t>(encrypted_data.size());
            buffer.push_back((len >> 24) & 0xFF);
            buffer.push_back((len >> 16) & 0xFF);
            buffer.push_back((len >>  8) & 0xFF);
            buffer.push_back(len & 0xFF);

            // encrypted data
            buffer.insert(buffer.end(), encrypted_data.begin(), encrypted_data.end());

            return common::sha256_hex(buffer);
        }
    };
    inline void from_json(const nlohmann::json& j, EncryptedSecretShare& s) {
        s.index = j.at("index").get<uint32_t>();
        s.encrypted_data = j.at("data").get<std::vector<uint8_t>>();
    }

    struct SecretShare {
        uint32_t index = 0;                 // fragment index (1..n)
        std::vector<uint8_t> data;          // fragment bytes

        [[nodiscard]]
        std::string serialize() const {
            const auto j = nlohmann::json{
                    {"index", index},
                    {"data", data}
            };
            return common::base64::encode(j.dump());
        }
    };

    inline void to_json(nlohmann::json& j, const SecretShare& s) {
        j = nlohmann::json{
                {"index", s.index},
                {"data", s.data}
        };
    }
    inline void from_json(const nlohmann::json& j, SecretShare& s) {
        s.index = j["index"];
        s.data = j["data"].get<std::vector<uint8_t>>();
    }

    struct FragmentEnvelop {
        std::string key_id;      // UUID
        uint32_t fragment_index = 0; // i in Shamir
        uint32_t threshold = 0;      // t
        uint32_t total_shares = 0;   // n

        Address owner;
        std::vector<Address> acl;
        std::vector<Address> nodes_holding_copy; // for tracking which nodes have a copy of this fragment

        uint64_t created_at = 0;
        bool revoked = false;
        bool committed = false;

        [[nodiscard]]
        nlohmann::json to_json() const {
            return nlohmann::json{
                            {"key_id", key_id},
                            {"fragment_index", fragment_index},
                            {"threshold", threshold},
                            {"total_shares", total_shares},
                            {"owner", owner.to_string()},
                            {"acl", acl},
                            {"nodes_holding_copy", nodes_holding_copy},
                            {"created_at", created_at},
                            {"revoked", revoked},
                            {"committed", committed}
            };
        }
        [[nodiscard]]
        static FragmentEnvelop from_json(const nlohmann::json& j) {
            FragmentEnvelop env;
            env.key_id = j["key_id"];
            env.fragment_index = j["fragment_index"];
            env.threshold = j["threshold"];
            env.total_shares = j["total_shares"];
            env.owner = Address(j["owner"].get<std::string>());
            env.acl = j["acl"].get<std::vector<Address>>();
            env.created_at = j["created_at"];
            env.revoked = j["revoked"];
            env.committed = j["committed"];

            const auto& nodes_copy_json = j["nodes_holding_copy"];
            if (nodes_copy_json.is_array()) {
                env.nodes_holding_copy = nodes_copy_json.get<std::vector<Address>>();
            }
            return env;
        }
    };

    struct ParadoxFragment {
        std::string key_id;      // UUID
        uint32_t fragment_index = 0; // i in Shamir
        SecretShare secret;      // fragment
        uint32_t threshold = 0;      // t
        uint32_t total_shares = 0;   // n

        Address owner;
        std::vector<Address> acl;
        std::vector<Address> nodes_holding_copy; // for tracking which nodes have a copy of this fragment
        uint64_t created_at  = 0;
        bool revoked = false;
        bool committed = false;

        [[nodiscard]]
        nlohmann::json to_json() const {
            return nlohmann::json{
                        {"key_id", key_id},
                        {"fragment_index", fragment_index},
                        {"secret", secret.serialize()},
                        {"threshold", threshold},
                        {"total_shares", total_shares},
                        {"owner", owner.to_string()},
                        {"acl", acl},
                        {"nodes_holding_copy", nodes_holding_copy},
                        {"created_at", created_at},
                        {"revoked", revoked},
                        {"committed", committed}
            };
        }

        [[nodiscard]]
        static ParadoxFragment from_json(const nlohmann::json& j) {
            ParadoxFragment f;
            f.key_id = j["key_id"];
            f.fragment_index = j["fragment_index"];
            const auto secret_share_json = common::base64::decode(j["secret"]);
            auto secret_share = nlohmann::json::parse(secret_share_json).get<SecretShare>();
            f.secret = secret_share;
            f.threshold = j["threshold"];
            f.total_shares = j["total_shares"];
            f.owner = Address(j["owner"].get<std::string>());
            f.acl = j["acl"].get<std::vector<Address>>();
            f.created_at = j["created_at"];
            f.revoked = j["revoked"];
            f.committed = j["committed"];

            const auto& nodes_copy_json = j["nodes_holding_copy"];
            if (nodes_copy_json.is_array()) {
                f.nodes_holding_copy = nodes_copy_json.get<std::vector<Address>>();
            }
            return f;
        }

        [[nodiscard]]
        FragmentEnvelop get_envelop() const {
            return FragmentEnvelop{
                    .key_id = key_id,
                    .fragment_index = fragment_index,
                    .threshold = threshold,
                    .total_shares = total_shares,
                    .owner = owner,
                    .acl = acl,
                    .nodes_holding_copy = nodes_holding_copy,
                    .created_at = created_at,
                    .revoked = revoked,
                    .committed = committed
            };
        }
    };

    struct ParadoxShareDelivery {
        std::string key_id;
        uint32_t fragment_index = 0;
        uint32_t threshold = 0;

        EncryptedSecretShare share;

        Address provider_address;   // node emitting this fragment
        uint64_t request_nonce = 0;

        [[nodiscard]]
        nlohmann::json to_json() const {
            return {
                {"key_id", key_id},
                {"fragment_index", fragment_index},
                {"threshold", threshold},
                {"share", share.to_json()},
                {"provider_address", provider_address.to_string()},
                {"request_nonce", request_nonce}
            };
        }

        [[nodiscard]]
        std::string serialize_for_signing() const {
            std::vector<uint8_t> buffer;

            auto append_u32_be = [&](uint32_t v){
                buffer.push_back((v >> 24) & 0xFF);
                buffer.push_back((v >> 16) & 0xFF);
                buffer.push_back((v >>  8) & 0xFF);
                buffer.push_back(v & 0xFF);
            };
            auto append_u64_be = [&](uint64_t v){
                for (int i = 7; i >= 0; --i) buffer.push_back((v >> (i*8)) & 0xFF);
            };
            auto append_str_lp = [&](const std::string& s){
                append_u32_be(static_cast<uint32_t>(s.size()));
                buffer.insert(buffer.end(), s.begin(), s.end());
            };
            auto append_hex_str_lp = [&](const std::string& hex){
                // si tu veux rester 100% déterministe, garde la string hex telle quelle
                append_str_lp(hex);
            };

            append_str_lp(key_id);
            append_u32_be(fragment_index);
            append_u32_be(threshold);

            // share hash (déjà deterministe)
            append_hex_str_lp(share.serialize_for_signing());

            append_str_lp(provider_address.to_string());
            append_u64_be(request_nonce);

            return common::sha256_hex(buffer);
        }
    };

    inline void from_json(const nlohmann::json& j, ParadoxShareDelivery& d) {
        d.key_id = j.at("key_id").get<std::string>();
        d.fragment_index = j.at("fragment_index").get<uint32_t>();
        d.threshold = j.at("threshold").get<uint32_t>();
        d.share = j.at("share").get<EncryptedSecretShare>();
        d.provider_address = Address(j.at("provider_address").get<std::string>());
        d.request_nonce = j.at("request_nonce").get<uint64_t>();
    }

    struct SignedParadoxShareDelivery {
        ParadoxShareDelivery delivery;
        std::string provider_public_key;
        Signature signature;
        SignatureType sig_type = SignatureType::ECDSA_SECP256K1;

        static
        SignedParadoxShareDelivery from_share_delivery(
            const std::shared_ptr<port::ICryptoService>& crypto,
            const infra::wallet::KeyPair& kp,
            const ParadoxShareDelivery& delivery)
        {
            const auto serialized_share_delivery = delivery.serialize_for_signing();
            const std::string domain = "UTX_PARADOX_SHARE_DELIVERY_V1";
            const auto share_delivery_hash = crypto->calculate_hash(domain+"|"+serialized_share_delivery);
            const auto share_delivery_signature = crypto->sign_hash(share_delivery_hash, kp.private_key_hex);
            return SignedParadoxShareDelivery{
                .delivery = delivery,
                .provider_public_key = kp.public_key_hex,
                .signature = share_delivery_signature
            };
        }

        [[nodiscard]]
        nlohmann::json to_json() const {
            return {
                {"delivery", delivery.to_json()},
                {"provider_public_key", provider_public_key},
                {"signature", signature.to_string()},
                {"sig_type" ,sig_type_to_str(sig_type)}
            };
        }
    };
    inline void from_json(const nlohmann::json& j, SignedParadoxShareDelivery& s) {
        s.delivery = j.at("delivery").get<ParadoxShareDelivery>();
        s.provider_public_key = j.at("provider_public_key").get<std::string>();
        s.signature = Signature(j.at("signature").get<std::string>());

        if (j.contains("sig_type"))
            s.sig_type = sig_type_from_str(j.at("sig_type").get<std::string>());
    }
}
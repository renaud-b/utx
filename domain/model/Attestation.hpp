#pragma once
#include <cstring>
#include <string>
#include <vector>
#include "Types.hpp"

namespace utx::domain::model {
    /** Structure representing an attestation (proof) of a public key ownership.
     * Contains the public key and its corresponding signature.
     */
    struct Attestation {
        std::string public_key; // Hex encoded or raw bytes
        Signature signature;

        /** * Serializes the proof into a byte vector: [PK_LENGTH][PK][SIG_LENGTH][SIG]
         * or a simple concatenation if lengths are fixed.
         * @return Byte vector containing the serialized proof
         */
        [[nodiscard]]
        std::vector<uint8_t> serialize() const {
            std::vector<uint8_t> data;
            // Assuming public_key is hex, convert to raw bytes or just use strings
            // For simplicity, let's store them as lengths + data
            auto add_string = [&](const std::string& s) {
                uint32_t len = static_cast<uint32_t>(s.size());
                auto* p = reinterpret_cast<uint8_t*>(&len);
                data.insert(data.end(), p, p + sizeof(uint32_t));
                data.insert(data.end(), s.begin(), s.end());
            };

            add_string(public_key);
            add_string(signature.to_string());
            return data;
        }

        /** Reconstructs an Attestation from a byte vector
         * Expects the same format as serialize().
         * @param data Byte vector containing the serialized proof
         * @return Deserialized Attestation object
         */
        static Attestation deserialize(const std::vector<uint8_t>& data) {
            Attestation proof;
            size_t offset = 0;

            auto read_string = [&](std::string& out) {
                uint32_t len;
                std::memcpy(&len, &data[offset], sizeof(uint32_t));
                offset += sizeof(uint32_t);
                out.assign(reinterpret_cast<const char*>(&data[offset]), len);
                offset += len;
            };

            read_string(proof.public_key);
            std::string sig_str;
            read_string(sig_str);
            proof.signature = Signature(sig_str);
            
            return proof;
        }
    };



    inline void to_json(nlohmann::json& j, const Attestation& a) {
        j = nlohmann::json{
                    {"pubkey", a.public_key},
                    {"sig", a.signature.to_string()}
        };
    }

    inline void from_json(const nlohmann::json& j, Attestation& a) {
        a.public_key = j.at("pubkey").get<std::string>();
        a.signature = Signature(j.at("sig").get<std::string>());
    }

}
#pragma once
#include <nlohmann/json.hpp>
#include "domain/model/AtomicBlock.hpp"
#include "domain/model/NodeInfo.hpp"
#include "domain/model/ChainConfig.hpp"

namespace utx::domain::model {



    /** Helper to convert any serializable object to MessagePack bytes */
    template<typename T>
    inline std::vector<uint8_t> encode_msgpack(const T& obj) {
        nlohmann::json j = obj;
        return nlohmann::json::to_msgpack(j);
    }

    /** Helper to reconstruct an object from MessagePack bytes */
    template<typename T>
    inline T decode_msgpack(const std::vector<uint8_t>& data) {
        nlohmann::json j = nlohmann::json::from_msgpack(data);
        return j.get<T>();
    }

}

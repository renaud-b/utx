#include "Action.hpp"
#include <stdexcept>
#include <nlohmann/json.hpp>

#include "../../common/Logger.hpp"
#include "../../common/Uuid.hpp"
#include "../../common/Base64.hpp"

using namespace utx::domain::graph;
using json = nlohmann::json;

constexpr size_t MAX_GROUP_ACTIONS = 50;


// Convert first character to ActionType
static ActionType action_type_from_char(char c) {
    switch (c) {
        case 'S': return ActionType::SET;
        case 'G': return ActionType::GROUP;
        case 'M': return ActionType::MOVE;
        case 'D': return ActionType::DELETE;
        case 'T': return ActionType::COMMIT_TAG;
        default: return ActionType::UNKNOWN;
    }
}

std::string Action::to_string() const {
    std::ostringstream oss;
    oss << "Action{type=" << action_type_to_char(type)
        << ", element_id=" << element_id
        << ", payload=" << (payload ? payload.value() : "nullopt") << "}";
    return oss.str();
}

std::string Action::encode() const {
    const auto action_type = action_type_to_char(type);
    if (!payload) {
        return std::format("{}{}", action_type, element_id);
    }
    const auto encoded_payload = common::base64::encode(payload.value());
    return std::format("{}{}{}", action_type, element_id, encoded_payload);
}

Action Action::decode_action(const std::string& raw) {
    if (raw.empty()) throw std::invalid_argument("Empty action string");

    const char type_char = raw[0];

    const ActionType type = action_type_from_char(type_char);
    LOG_THIS_DEBUG("Decoding action of type {}, raw action string: {}", type_char, raw);
    if (type == ActionType::UNKNOWN) throw std::invalid_argument(std::format("Unknown action type char: {}", type_char));

    Action action;
    action.type = type;

    if (type == ActionType::DELETE) {
        if (raw.size() > 1) {
            action.element_id = raw.substr(1);
        }
        return action;
    }
    if (type == ActionType::GROUP) {
        const std::string encoded_payload = raw.substr(1);
        const std::string json_payload = common::base64::decode(encoded_payload);
        const auto j = nlohmann::json::parse(json_payload);
        if (!j.is_array()) {
            throw std::runtime_error("Invalid GROUP payload");
        }
        action.payload = j.dump();
        return action;
    }
    if (type == ActionType::COMMIT_TAG) {
        if (raw.size() > 1) {
            const std::string encoded_payload = raw.substr(1);
            const std::string json_payload = common::base64::decode(encoded_payload);
            action.payload = json_payload;
        }
        return action;
    }

    if (raw.size() > 33) {
        action.element_id = raw.substr(1, 32);
        const std::string encoded_payload = raw.substr(33);

        const std::string json_payload = common::base64::decode(encoded_payload);
        action.payload = json_payload;
    }
    return action;
}


Action Action::decode_blockchain_action(const std::string& raw) {
    const std::string prefix = "urn:pi:graph:action:";
    if (raw.rfind(prefix, 0) != 0) {
        throw std::invalid_argument("Invalid URN prefix");
    }

    auto without_prefix = raw.substr(prefix.size());
    LOG_THIS_DEBUG("Decoding blockchain action, without prefix: {}", without_prefix);
    Action action = decode_action(without_prefix);

    // Additional validation for GROUP actions
    if (action.type == ActionType::GROUP && action.payload.has_value()) {
        auto j = nlohmann::json::parse(action.payload.value());
        if (j.size() > MAX_GROUP_ACTIONS) {
            throw std::runtime_error("Too many actions in GROUP");
        }

        for (const auto& sub : j) {
            if (!sub.is_string()) throw std::runtime_error("Invalid action in GROUP");
            if (sub.get<std::string>().rfind("G", 0) == 0) {
                throw std::invalid_argument("Nested GROUP actions are not allowed");
            }
        }
    }

    return action;
}

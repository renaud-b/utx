#pragma once
#include <string>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>

namespace utx::domain::graph {
    /** Enumeration of possible action types in a graph. */
    enum class ActionType {
        SET,
        GROUP,
        MOVE,
        DELETE,
        COMMIT_TAG,
        UNKNOWN
    };

    inline char action_type_to_char(const ActionType type) {
        switch (type) {
            case ActionType::SET: return 'S';
            case ActionType::GROUP: return 'G';
            case ActionType::MOVE: return 'M';
            case ActionType::DELETE: return 'D';
            case ActionType::COMMIT_TAG: return 'T';
            default: throw std::invalid_argument("Invalid action type");
        }
    }
    /** Payload structure for SET actions. */
    struct CommitPayload {
        std::string message;
        std::string author;
        uint8_t number_of_blocks;
        std::string revision_id; // UUID du commit
        nlohmann::json extra_metadata;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(CommitPayload, message, author, number_of_blocks, revision_id, extra_metadata)
    };

    struct SetPayload {
        std::string property;
        std::string value;
        bool is_property_deleted = false;
    };
    [[nodiscard]]
    inline std::string encode_set_payload(const SetPayload& payload) {
        nlohmann::json json_payload;
        json_payload["property"] = payload.property;
        json_payload["value"] = payload.value;
        json_payload["is_property_deleted"] = payload.is_property_deleted;
        return json_payload.dump();
    }
    inline SetPayload decode_set_payload(const std::string& payload) {
        nlohmann::json json_payload = nlohmann::json::parse(payload);
        SetPayload set_payload;
        set_payload.property = json_payload["property"];
        set_payload.value = json_payload["value"];
        set_payload.is_property_deleted = json_payload["is_property_deleted"];
        return set_payload;
    }
    /** Payload structure for MOVE actions. */
    struct MovePayload {
        std::string destination_id;
        std::string direction;
    };
    inline std::string encode_move_payload(const MovePayload& payload) {
        nlohmann::json json_payload;
        json_payload["destination_id"] = payload.destination_id;
        json_payload["direction"] = payload.direction;
        return json_payload.dump();
    }
    /** Payload structure for GROUP actions. */
    struct GroupPayload {
        std::vector<std::string> actions;
    };
    inline std::string encode_group_payload(const GroupPayload& payload) {
        nlohmann::json json_payload;
        json_payload["actions"] = payload.actions;
        return json_payload.dump();
    }
    /** Represents an action to be performed on a graph element.
     * Supports encoding to and decoding from string representations.
     */
    struct Action {
        ActionType type;
        std::string element_id;
        std::optional<std::string> payload;
        /** Encode the action to a string representation.
         * @returns The encoded action string.
         */
        [[nodiscard]]
        std::string encode() const;
        /** Decode a blockchain action from a raw string.
         * @param raw The raw action string.
         * @returns The decoded Action object.
         * @throws std::invalid_argument if the action string is invalid.
         */
        [[nodiscard]]
        static Action decode_blockchain_action(const std::string& raw);
        /** Decode a general action from a raw string.
         * @param raw The raw action string.
         * @returns The decoded Action object.
         * @throws std::invalid_argument if the action string is invalid.
         */
        [[nodiscard]]
        static Action decode_action(const std::string& raw);
        /** Get a string representation of the action for debugging purposes.
         * @returns A string describing the action.
         */
        [[nodiscard]]
        std::string to_string() const;
     };
    /** Build a COMMIT_TAG action.
     * @param payload The commit payload.
     * @returns The constructed Action object.
     */
    inline Action build_commit_action(const CommitPayload& payload) {
        Action action;
        action.type = ActionType::COMMIT_TAG;
        action.payload = nlohmann::json(payload).dump();
        return action;
    }
    /** Build a SET action for a property.
        * @param element_id The ID of the graph element.
        * @param property The property to set.
        * @param value The value to set for the property.
        * @param is_property_deleted Flag indicating if the property is to be deleted.
        * @returns The constructed Action object.
     */
    inline Action build_set_action(const std::string& element_id,
                             const std::string& property,
                             const std::string& value,
                             bool is_property_deleted = false) {
        Action action;
        action.type = ActionType::SET;
        action.element_id = element_id;

        SetPayload payload{property, value, is_property_deleted};
        action.payload = encode_set_payload(payload);

        return action;
    }
    /** Build a DELETE action for a property.
     * @param element_id The ID of the graph element.
     * @param property The property to delete.
     * @returns The constructed Action object.
     */
    inline Action build_delete_property_action(const std::string& element_id,
                             const std::string& property) {
        Action action;
        action.type = ActionType::SET;
        action.element_id = element_id;

        SetPayload payload{property, "", true};
        action.payload = encode_set_payload(payload);

        return action;
    }
    /** Build a GROUP action from a list of actions.
     * @param actions The list of actions to group.
     * @returns The constructed Action object.
     */
    inline Action build_group_action(const std::vector<Action>& actions) {
        Action action;
        action.type = ActionType::GROUP;

        nlohmann::json j;
        for (const auto& act : actions) {
            j.push_back(act.encode());
        }
        action.payload = j.dump();

        return action;
    }
    /** Build a MOVE action for a graph element.
     * @param element_id The ID of the graph element to move.
     * @param destination_id The ID of the destination element.
     * @param direction The direction of the move ("in", "left", "right").
     * @returns The constructed Action object.
     */
    inline Action build_move_action(const std::string &element_id,
                             const std::string &destination_id,
                             const std::string &direction) {
        Action action;
        action.type = ActionType::MOVE;
        action.element_id = element_id;

        MovePayload payload{destination_id, direction};

        action.payload = encode_move_payload(payload);

        return action;
    }
    /** Batch a list of actions into GROUP actions of specified size.
     * @param actions The list of actions to batch.
     * @param batch_size The maximum size of each GROUP action.
     * @returns A vector of grouped Action objects.
     */
    inline std::vector<Action> batch_into_group_actions(const std::vector<Action>& actions, size_t batch_size) {
        std::vector<Action> grouped_actions;
        for (size_t i = 0; i < actions.size(); i += batch_size) {
            std::vector<Action> batch;
            for (size_t j = i; j < i + batch_size && j < actions.size(); ++j) {
                batch.push_back(actions[j]);
            }
            grouped_actions.push_back(build_group_action(batch));
        }
        return grouped_actions;
    }

} // namespace utx::domain::graph

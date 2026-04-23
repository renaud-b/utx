#pragma once
#include "GraphElement.hpp"
#include <memory>
#include <string>

#include "../../common/Uuid.hpp"
#include "../model/Types.hpp"


namespace utx::domain::graph {
    /** Represents a single entry in the graph's revision history. */
    struct GraphRevisionEntry {
        std::string revision_id;
        std::string message;
        std::string author;
        uint64_t timestamp;
    };

    /** Represents a graph structure with elements and provides methods to manage and access them.
     * Each graph has a unique identifier (UUID) and a root element.
     */
    class Graph {
    public:
        /** Construct a Graph with the given UUID.
         * @param id The UUID of the graph.
         */
        explicit Graph(const common::UUID &id);

        explicit Graph(const common::UUID &id, const std::shared_ptr<GraphElement>& root)
            : id_(id), root_(root) {
            root_->set_owner(this);
            register_element(root_);
        }
        /** Get the UUID of the graph.
         * @returns A const reference to the graph's UUID.
         */
        [[nodiscard]]
        const common::UUID& id() const noexcept;
        /** Get the root element of the graph.
         * @returns A shared pointer to the root GraphElement.
         */
        [[nodiscard]]
        std::shared_ptr<GraphElement> root() const noexcept;
        /** Find an element in the graph by its ID.
         * @param element_id The ID of the element to find.
         * @returns A shared pointer to the found GraphElement, or nullptr if not found.
         */
        [[nodiscard]]
        std::shared_ptr<GraphElement> find_element(const std::string& element_id) const;
        /** Convert the graph to a JSON string representation.
         * @returns A JSON string representing the graph.
         */
        [[nodiscard]]
        std::string to_json_string() const {
            return to_json().dump();
        }
        /** Convert the graph to a JSON object representation.
         * @returns A nlohmann::json object representing the graph.
         */
        [[nodiscard]]
        nlohmann::json to_json() const {
            nlohmann::json j;
            j["graph_id"] = id_.to_string();
            j["root"] = root_->to_json();
            return j;
        }
        /** Register a new element in the graph.
         * @param element A shared pointer to the GraphElement to register.
         */
        void register_element(const std::shared_ptr<GraphElement>& element);
        /** Unregister an element from the graph by its ID.
         * @param id The ID of the element to unregister.
         */
        void unregister_element(const std::string& id);
        /** Parse and populate the graph from a JSON representation.
         * @param value A nlohmann::json reference representing the graph.
         */
        void parse_from(nlohmann::json::const_reference value);
        /** Add an entry to the graph's history.
         * @param revision The revision identifier.
         * @param message A message describing the change.
         * @param author The author of the change.
         * @param timestamp The timestamp of the change.
         */
        void add_to_history(const std::string & revision, const std::string & message, const std::string & author, uint64_t timestamp);

        /** Set the graph's UUID based on a given address.
         * @param address The address to derive the graph's UUID from.
         */
        void set_id(const model::Address & address);

        /** Load a Graph from a JSON string representation.
         * @param json_str The JSON string representing the graph.
         * @returns A Graph object constructed from the JSON data.
         */
        static Graph from_json(const std::string& json_str);
        /** Load a Graph from a raw nlohmann::json object.
         * @param j The nlohmann::json object representing the graph.
         * @returns A Graph object constructed from the JSON data.
         */
        [[nodiscard]]
        static Graph from_raw_json(const nlohmann::json &j);

    private:

        void adopt_tree(const std::shared_ptr<GraphElement>& node,
                       const std::shared_ptr<GraphElement>& parent);

        common::UUID id_;

        std::shared_ptr<GraphElement> root_;
        std::vector<GraphRevisionEntry> revisions_;

        std::unordered_map<std::string, std::weak_ptr<GraphElement>> index_;

    };

} // namespace utx::domain::graph

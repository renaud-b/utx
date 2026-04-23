#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <set>
#include <nlohmann/json.hpp>


namespace utx::domain::graph {
    using json = nlohmann::json;

    /** Forward declaration of Graph class to avoid circular dependency. */
    class Graph;
    /** Represents an element in a graph structure.
     * Each element has a unique ID, name, properties, and can have child elements.
     * Provides methods to manage properties, children, and traverse the graph.
     */
    class GraphElement: public std::enable_shared_from_this<GraphElement> {
    public:
        /** Construct a GraphElement with the given name.
         * @param name The name of the graph element.
         */
        explicit GraphElement(std::string name);
        /** Get the unique ID of the graph element.
         * @returns A const reference to the element's ID string.
         */
        [[nodiscard]]
        const std::string& id() const noexcept;
        /** Get the name of the graph element.
         * @returns A const reference to the element's name string.
         */
        [[nodiscard]]
        const std::string& name() const noexcept;
        /** Get the path of the graph element in the hierarchy.
         * @returns The path string representing the element's location in the graph.
         */
        [[nodiscard]]
        std::string path() const;
        /** Return the number of elements in the graph (including children).
         * @returns The size of the graph element.
         */
        [[nodiscard]]
        size_t size() const;
        /** Check if the graph element has a specific property by key.
         * @param key The property key to check.
         * @returns true if the property exists, false otherwise.
         */
        [[nodiscard]]
        bool has_property(const std::string& key) const;
        /** Get the properties of the graph element.
         * @returns A const reference to the unordered_map of properties (key-value pairs).
         */
        [[nodiscard]]
        const std::unordered_map<std::string, std::string>& properties() const noexcept;
        /** Get the value of a specific property by name.
         * @param name The property key to look up.
         * @returns The property value if found, or an empty string if not found.
         */
        [[nodiscard]]
        std::string get_property(const std::string &name) const;
        /** Set a property key-value pair for the graph element.
         * @param key The property key.
         * @param value The property value.
         */
        void set_property(const std::string& key, const std::string& value);
        /** Delete a property by key from the graph element.
         * @param key The property key to delete.
         */
        void delete_property(const std::string& key);
        /** Add a child element to this graph element.
         * @param child A shared pointer to the child GraphElement to add.
         */
        void add_child(std::shared_ptr<GraphElement> child);
        /** Get the child elements of this graph element.
         * @returns A const reference to the vector of shared pointers to child GraphElements.
         */
        [[nodiscard]] const std::vector<std::shared_ptr<GraphElement>>& children() const noexcept;
        /** Find a child element by its ID within this graph element's hierarchy.
         * @param element_id The ID of the child element to find.
         * @returns A shared pointer to the found GraphElement, or nullptr if not found.
         */
        [[nodiscard]]
        std::shared_ptr<GraphElement> find_child_by_id(const std::string& element_id) const;
        /** Find the next child element by its name.
         * @param element_name The name of the child element to find.
         * @returns A shared pointer to the found GraphElement, or nullptr if not found.
         */
        [[nodiscard]]
        std::shared_ptr<GraphElement> next(const std::string& element_name) const;
        /** Check if there is a next child element with the given name.
         * @param element_name The name of the child element to check for.
         * @returns true if a child with the given name exists, false otherwise.
         */
        [[nodiscard]]
        bool has_next(const std::string& element_name) const;
        /** Get the parent element of this graph element.
         * @returns A shared pointer to the parent GraphElement, or nullptr if this is the root.
         */
        [[nodiscard]]
        std::shared_ptr<GraphElement> parent() const;
        /** Convert the graph element and its children to a JSON string representation.
         * @returns A JSON object representing the graph element.
         */
        [[nodiscard]]
        nlohmann::json to_json() const;
        /** Remove a child element by its ID from this graph element.
         * @param element_id The ID of the child element to remove.
         */
        void remove_child(const std::string& element_id);
        /** Move a child element before another child element.
         * @param moving_id The ID of the child element to move.
         * @param target_id The ID of the target child element to move before.
         * @returns true if the move was successful, false otherwise.
         */
        bool move_child_before(const std::string& moving_id, const std::string& target_id);
        /** Move a child element after another child element.
         * @param moving_id The ID of the child element to move.
         * @param target_id The ID of the target child element to move after.
         * @returns true if the move was successful, false otherwise.
         */
        bool move_child_after(const std::string& moving_id, const std::string& target_id);
        /** Move a child element into a new parent element.
         * @param moving_id The ID of the child element to move.
         * @param new_parent A shared pointer to the new parent GraphElement.
         * @returns true if the move was successful, false otherwise.
         */
        bool move_child_in(const std::string& moving_id, const std::shared_ptr<GraphElement> &new_parent);
        /** Traverse the graph starting from this element, applying a visitor function to each element.
         * @param visitor A function that takes a shared pointer to a GraphElement and returns void.
         */
        void traverse(const std::function<void(std::shared_ptr<GraphElement>)> &visitor);
        /** Reserve space for a number of child elements to optimize memory allocation.
         * @param n The number of child elements to reserve space for.
         */
        void reserve_children(size_t n);
        /** Set the owner graph of this element.
         * @param owner A pointer to the owning Graph.
         */
        void set_owner(Graph* owner);
        /** Create a GraphElement from a JSON object.
         * @param json_body The JSON object representing the graph element.
         * @returns A shared pointer to the created GraphElement.
         */
        [[nodiscard]]
        static std::shared_ptr<GraphElement> from_json_object(const nlohmann::json &json_body);
        /** Set the parent of this graph element.
         * @param parent A shared pointer to the new parent GraphElement.
         */
        void set_parent(const std::shared_ptr<GraphElement> &parent);
        /** Insert a child element at a specific index in the children vector.
         * @param child A shared pointer to the child GraphElement to insert.
         * @param index The index at which to insert the child element.
         * @returns true if the insertion was successful, false otherwise.
         */
        bool insert_child_at(const std::shared_ptr<GraphElement> &child, size_t index);
        /**  Insert a child element before another child element identified by target_id.
         * @param child A shared pointer to the child GraphElement to insert.
         * @param target_id The ID of the target child element to insert before.
         * @returns true if the insertion was successful, false otherwise.
         */
        bool insert_child_before(const std::shared_ptr<GraphElement> &child, const std::string &target_id);
        /** Insert a child element after another child element identified by target_id.
         * @param child A shared pointer to the child GraphElement to insert.
         * @param target_id The ID of the target child element to insert after.
         * @returns true if the insertion was successful, false otherwise.
         */
        bool insert_child_after(const std::shared_ptr<GraphElement> &child, const std::string &target_id);
    private:
        Graph* owner_ = nullptr;
        std::string id_;
        std::string name_;
        std::unordered_map<std::string, std::string> props_;
        std::vector<std::shared_ptr<GraphElement>> children_;
        std::weak_ptr<GraphElement> parent_;
        mutable std::string cached_path_;
        mutable bool path_dirty_ = true;

        /** Invalidate the cached path, forcing a recomputation on next access.
         */
        void invalidate_path_cache() const;

        /** Recompute and reassign the ID based on the current path.
         */
        void reattribute_id();

        /** Join path segments into a single path string, using '/' as a separator.
         * @param segments A vector of path segments to join.
         * @returns The joined path string.
         */
        static std::string join_segments(const std::vector<std::string>& segments);

        /** Generate a unique ID based on the given path using MD5 hashing.
         * @param path The path string to generate the ID from.
         * @returns The generated ID string.
         */
        static std::string generate_id(const std::string& path);
        static std::atomic<int64_t> active_elements;

    };
    /** Compute the next available name for a child element based on existing siblings.
     * If the base name is already taken, appends an index to create a unique name.
     * @param siblings A vector of shared pointers to sibling GraphElements.
     * @param base_name The desired base name for the new child element.
     * @returns A unique name for the new child element that does not conflict with siblings.
     */
    inline std::string compute_next_available_name(
        const std::vector<std::shared_ptr<GraphElement>>& siblings,
        const std::string& base_name
    ) {
        std::set<std::string> existing_names;
        for (const auto& sibling : siblings) {
            existing_names.insert(sibling->name());
        }

        if (!existing_names.contains(base_name)) {
            return base_name;
        }

        int index = 1;
        std::string new_name;
        do {
            new_name = base_name + "_" + std::to_string(index++);
        } while (existing_names.contains(new_name));

        return new_name;
    }

} // namespace utx::domain::graph

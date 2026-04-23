#include "GraphElement.hpp"
#include "Graph.hpp"
#include "../../common/Hash.hpp"

#include <algorithm>
#include <sstream>

using namespace utx::domain::graph;

/**
 * INVARIANT #1
 * The ID of a GraphElement is always equal to md5(path()).
 * IDs are deterministic and reflect the structural position of the element.
 *
 * Any structural modification that changes the path must call:
 *   - invalidate_path_cache()
 *   - reattribute_id()
 *
 * INVARIANT #2
 * Sibling elements must have unique names.
 * This guarantees path uniqueness and therefore ID determinism.
 *
 * INVARIANT #3
 * If owner_ is not null, every element in the subtree must be
 * registered in the owner graph registry.
 *
 * INVARIANT #4
 * cached_path_ is valid only when path_dirty_ == false.
 * Any structural change must invalidate the cache.
 */

/** Construct a GraphElement with the given name.
 * The ID of the element is generated based on its name to ensure uniqueness within the graph.
 *
 * @param name The name of the graph element.
 */
GraphElement::GraphElement(std::string name)
    : name_(std::move(name)) {
    id_ = generate_id("//" + name_);
}
/** Add a child element to this graph element.
 * The child element's name is adjusted to ensure uniqueness among siblings, and its parent and owner references are set.
 *
 * @param child A shared pointer to the child GraphElement to add.
 */
bool GraphElement::insert_child_at(const std::shared_ptr<GraphElement>& child, size_t index) {
    if (!child) return false;

    if (index > children_.size())
        index = children_.size();

    child->name_ = compute_next_available_name(children_, child->name_);
    child->parent_ = shared_from_this();
    child->set_owner(owner_);

    children_.insert(children_.begin() + index, child);

    invalidate_path_cache();
    reattribute_id();

    if (owner_) {
        child->traverse([this](const std::shared_ptr<GraphElement>& elem){
            owner_->register_element(elem);
        });
    }

    return true;
}
/** Insert a child element before another child element identified by target_id.
 * @param child A shared pointer to the child GraphElement to insert.
 * @param target_id The ID of the target child element to insert before.
 * @returns true if the insertion was successful, false otherwise.
 */
bool GraphElement::insert_child_before(const std::shared_ptr<GraphElement> &child,
                                       const std::string& target_id) {
    if (!child) return false;
    const auto it = std::ranges::find_if(children_,
                                   [&](const auto& c) { return c->id() == target_id; });
    if (it == children_.end())
        return false;

    const size_t index = std::distance(children_.begin(), it);
    return insert_child_at(child, index);
}
/** Insert a child element after another child element identified by target_id.
 * @param child A shared pointer to the child GraphElement to insert.
 * @param target_id The ID of the target child element to insert after.
 * @returns true if the insertion was successful, false otherwise.
 */
bool GraphElement::insert_child_after(const std::shared_ptr<GraphElement> &child,
                                      const std::string& target_id) {
    if (!child) return false;
    const auto it = std::ranges::find_if(children_,
                                         [&](const auto& c) { return c->id() == target_id; });

    if (it == children_.end())
        return false;

    const size_t index = std::distance(children_.begin(), it) + 1;
    return insert_child_at(child, index);
}
/** Move a child element before another child element identified by target_id.
 * @param moving_id The ID of the child element to move.
 * @param target_id The ID of the target child element to move before.
 * @returns true if the move was successful, false otherwise.
 */
bool GraphElement::move_child_before(const std::string& moving_id, const std::string& target_id) {
    std::shared_ptr<GraphElement> moving = nullptr;
    const auto it_moving = std::ranges::find_if(children_,
                                          [&](auto& c){ return c->id() == moving_id; });
    if (it_moving == children_.end()) return false;

    // We verify if target exists before erasing the moving element to avoid leaving the graph in an inconsistent state if the target is not found
    const auto it_target = std::ranges::find_if(children_,
                                          [&](auto& c){ return c->id() == target_id; });
    if (it_target == children_.end()) return false;

    // We can safely move the element now
    moving = *it_moving;
    children_.erase(it_moving);

    if (it_target == children_.end()) return false;
    children_.insert(it_target, moving);

    // We moved an element, so we need to invalidate the path cache and reattribute IDs for the affected elements
    invalidate_path_cache();
    reattribute_id();
    return true;
}
/** Move a child element after another child element identified by target_id.
 * @param moving_id The ID of the child element to move.
 * @param target_id The ID of the target child element to move after.
 * @returns true if the move was successful, false otherwise.
 */
bool GraphElement::move_child_after(const std::string& moving_id, const std::string& target_id) {
    std::shared_ptr<GraphElement> moving = nullptr;
    const auto it_moving = std::ranges::find_if(children_,
                                          [&](auto& c){ return c->id() == moving_id; });
    if (it_moving == children_.end()) return false;

    const auto it_target = std::ranges::find_if(children_,
                                          [&](auto& c){ return c->id() == target_id; });
    if (it_target == children_.end()) return false;

    moving = *it_moving;
    children_.erase(it_moving);

    if (it_target == children_.end()) return false;
    children_.insert(std::next(it_target), moving);

    // We moved an element, so we need to invalidate the path cache and reattribute IDs for the affected elements
    invalidate_path_cache();
    reattribute_id();
    return true;
}
/** Move a child element into another parent element.
 * @param moving_id The ID of the child element to move.
 * @param new_parent A shared pointer to the new parent GraphElement to move the child into.
 * @returns true if the move was successful, false otherwise.
 */
bool GraphElement::move_child_in(const std::string& moving_id, const std::shared_ptr<GraphElement> &new_parent) {
    const auto it_moving = std::ranges::find_if(children_,
                                          [&](auto& c){ return c->id() == moving_id; });
    if (it_moving == children_.end()) return false;

    const auto moving = *it_moving;

    children_.erase(it_moving);
    new_parent->add_child(moving);
    new_parent->invalidate_path_cache();
    new_parent->reattribute_id();
    return true;
}
/** Get the ID of the graph element.
 * @returns The unique ID of the graph element.
 */
const std::string& GraphElement::id() const noexcept {
    return id_;
}
/** Get the name of the graph element.
 * @returns The name of the graph element.
 */
const std::string& GraphElement::name() const noexcept {
    return name_;
}
/** Get the total number of descendant elements in the graph hierarchy under this element.
 * This function recursively counts all child elements and their descendants.
 * @returns The total count of descendant elements.
 */
size_t GraphElement::size() const {
    size_t count = 0;
    for (const auto& child : children_) {
        count += 1 + child->size();
    }
    return count;
}
/** Get the path of the graph element within the graph hierarchy.
 * The path is constructed by concatenating the names of the element and its ancestors,
 * separated by slashes. The path is cached for performance and invalidated when necessary.
 * @returns The path string representing the location of the graph element in the hierarchy.
 */
std::string GraphElement::path() const {
    if (!path_dirty_) return cached_path_;

    std::vector<std::string> segments;
    auto current = shared_from_this();
    while (current) {
        segments.push_back(current->name_);
        const auto parent_ptr = current->parent_.lock();
        if (!parent_ptr) break;
        current = parent_ptr;
    }
    std::ranges::reverse(segments);

    std::ostringstream oss;
    for (const auto& seg : segments) {
        oss << "/" << seg;
    }
    cached_path_ = join_segments(segments);
    path_dirty_ = false;

    return cached_path_;
}

std::string GraphElement::join_segments(const std::vector<std::string>& segments) {
    std::ostringstream oss;
    oss << "//";

    for (size_t i = 0; i < segments.size(); ++i) {
        oss << segments[i];
        if (i + 1 < segments.size())
            oss << "/";
    }

    return oss.str();
}
/** Check if the graph element has a property with the specified key.
 * @param key The property key to check for.
 * @returns true if the property exists, false otherwise.
 */
bool GraphElement::has_property(const std::string &key) const {
    if (const auto it = props_.find(key); it != props_.end()) {
        return true;
    }
    return false;
}
/** Get the properties of the graph element.
 * @returns A const reference to the unordered_map of properties (key-value pairs).
 */
const std::unordered_map<std::string, std::string>& GraphElement::properties() const noexcept {
    return props_;
}
/** Get the value of a specific property by name.
 * @param name The property key to look up.
 * @returns The property value if found, or an empty string if not found.
 */
std::string GraphElement::get_property(const std::string &name) const {
    if (const auto it = props_.find(name); it != props_.end()) {
        return it->second;
    }
    return {};
}
/** Set a property key-value pair for the graph element.
 * @param key The property key.
 * @param value The property value.
 */
void GraphElement::set_property(const std::string& key, const std::string& value) {
    props_[key] = value;
}
/** Delete a property by key from the graph element.
 * @param key The property key to delete.
 */
void GraphElement::delete_property(const std::string& key) {
    props_.erase(key);
}
/** Set the parent of this graph element.
 * @param parent A shared pointer to the new parent GraphElement.
 */
void GraphElement::set_parent(const std::shared_ptr<GraphElement>& parent) {
    parent_ = parent;
    invalidate_path_cache();
    reattribute_id();
}
/** Add a child element to this graph element.
 * @param child A shared pointer to the child GraphElement to add.
 */
void GraphElement::add_child(std::shared_ptr<GraphElement> child) {
    if (!child) return;

    // Ensure unique sibling name
    child->name_ = compute_next_available_name(children_, child->name_);

    child->parent_ = shared_from_this();
    child->set_owner(owner_);          // propagate owner recursively
    child->invalidate_path_cache();    // invalidate subtree
    child->reattribute_id();           // recompute subtree IDs

    children_.push_back(child);

    if (owner_) {
        child->traverse([this](const std::shared_ptr<GraphElement>& elem){
            owner_->register_element(elem);
        });
    }
}
/** Get the child elements of this graph element.
 * @returns A const reference to the vector of shared pointers to child GraphElements.
 */
const std::vector<std::shared_ptr<GraphElement>>& GraphElement::children() const noexcept {
    return children_;
}
/** Find a child element by its ID within this graph element's hierarchy.
 * @param element_id The ID of the child element to find.
 * @returns A shared pointer to the found GraphElement, or nullptr if not found.
 */
std::shared_ptr<GraphElement> GraphElement::find_child_by_id(const std::string& element_id) const {
    if (id() == element_id) return std::const_pointer_cast<GraphElement>(shared_from_this());
    for (const auto& child : children_) {
        if (auto nested = child->find_child_by_id(element_id)) return nested;
    }
    return nullptr;
}
/** Find the next child element by its name.
 * @param element_name The name of the child element to find.
 * @returns A shared pointer to the found GraphElement, or nullptr if not found.
 */
std::shared_ptr<GraphElement> GraphElement::next(const std::string &element_name) const {
    for (const auto& child : children_) {
        if (child->name() == element_name) return child;
    }
    return nullptr;
}
/** Check if there is a next child element with the given name.
 * @param element_name The name of the child element to check for.
 * @returns true if a child with the given name exists, false otherwise.
 */
bool GraphElement::has_next(const std::string &element_name) const {
    for (const auto& child : children_) {
        if (child->name() == element_name) return true;
    }
    return false;
}
/** Get the parent element of this graph element.
 * @returns A shared pointer to the parent GraphElement, or nullptr if this is the root.
 */
std::shared_ptr<GraphElement> GraphElement::parent() const {
    return parent_.lock();
}
/** Convert the graph element and its children to a JSON string representation.
 * @returns A JSON object representing the graph element.
 */
nlohmann::json GraphElement::to_json() const {
    try {
        nlohmann::json j;
        for (const auto& [key, value] : props_) {
            j[key] = value;
        }
        j["id"] = id_;
        j["name"] = name_;
        j["parent"] = parent_.lock() ? parent_.lock()->id() : "";
        j["path"] = path();
        j["children"] = nlohmann::json::array();
        for (const auto& child : children_) {
            j["children"].push_back(child->to_json());
        }
        return j;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Failed to serialize GraphElement: ") + e.what());
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to serialize GraphElement: ") + e.what());
    }
}
/** Remove a child element by its ID from this graph element.
 * @param element_id The ID of the child element to remove.
 */
void GraphElement::remove_child(const std::string& element_id) {
    std::shared_ptr<GraphElement> removed;
    Graph* owner = nullptr;
    {
        auto it = std::ranges::find_if(
            children_,
            [&](const std::shared_ptr<GraphElement>& c) { return c->id() == element_id; }
        );

        if (it == children_.end()) {
            return; // nothing to remove
        }

        removed = *it;     // keep subtree alive after erase
        owner = owner_;    // snapshot
        children_.erase(it);
    }

    // Outside of GraphElement lock: avoid lock inversions / re-entrancy issues
    if (!owner || !removed) return;

    removed->traverse([owner](const std::shared_ptr<GraphElement>& elem) {
        owner->unregister_element(elem->id());
    });
}
/** Generate a unique ID for a graph element based on its path.
 * This is a strong invariant that ensures that the ID of an element is always derived from its path in the graph hierarchy,
 * making it deterministic and reflecting the structure of the graph.
 * @param path The path of the graph element.
 * @returns The generated unique ID as a string.
 */
std::string GraphElement::generate_id(const std::string& path) {
    return common::md5_hex(path);
}
/** Traverse the graph element and its children, applying a visitor function to each element.
 * @param visitor A function that takes a shared pointer to a GraphElement and returns void.
 *                This function will be called for the current element and recursively for all children.
 */
void GraphElement::traverse(const std::function<void(std::shared_ptr<GraphElement>)>& visitor) {
    visitor(shared_from_this());
    for (const auto& child : children_) {
        child->traverse(visitor);
    }
}
/** Recompute the ID of this graph element and all its children based on their paths.
 * This method should be called whenever the structure of the graph changes in a way that affects paths (e.g., moving elements).
 * TODO : this is a costly operation, we should consider optimizing it by only reattributing affected subtrees or by using a different ID generation strategy that doesn't require path-based IDs.
 * actually we are in O(n) where n is the number of affected elements,
 * which is optimal since we need to update all of them.
 * The path-based ID generation is a design choice that ensures IDs are deterministic and reflect the structure of the graph,
 * but it does mean that any change in the hierarchy requires reattributing IDs for the affected subtree.
 * We could optimize this by only reattributing the subtree that was moved, rather than the entire graph,
 * but that would add complexity to the implementation.
 */
void GraphElement::reattribute_id() {
    id_ = generate_id(path());
    for (const auto& child : children_) {
        child->reattribute_id();
    }
}
/** Reserve space for a number of child elements to optimize memory allocation.
 * @param n The number of child elements to reserve space for.
 */
void GraphElement::reserve_children(const size_t n) {
    children_.reserve(n);
}
/** Invalidate the cached path for this graph element and all its children.
 * This should be called whenever the structure of the graph changes in a way that affects paths (e.g., moving elements).
 */
void GraphElement::invalidate_path_cache() const {
    path_dirty_ = true;
    for (const auto& child : children_) {
        child->invalidate_path_cache();
    }
}
/** Set the owner graph of this element and all its children.
 * @param owner A pointer to the owning Graph.
 */
void GraphElement::set_owner(Graph* owner) {
    owner_ = owner;
    for (const auto& child : children_) {
        child->set_owner(owner);
    }
}
/** Create a GraphElement from a JSON object.
 * @param json_body The JSON object representing the graph element.
 * @returns A shared pointer to the created GraphElement.
 */
std::shared_ptr<GraphElement> GraphElement::from_json_object(const nlohmann::json &json_body) {
    try {
        // each field of the json_body is a property except "children", "name", "id", "parent", "path"
        auto output = std::make_shared<GraphElement>(json_body.value("name", "unnamed"));
        const std::vector<std::string> ignored = {"children", "name", "id", "parent", "path"};
        for (const auto& entry: json_body.items()) {
            if (std::ranges::find(ignored, entry.key()) != ignored.end()) {
                continue;
            }
            output->set_property(entry.key(), entry.value().get<std::string>());
        }
        if (json_body.contains("children") && json_body["children"].is_array()) {
            for (const auto& child_json : json_body["children"]) {
                if (child_json.is_object()) {
                    const auto child = from_json_object(child_json);
                    output->add_child(child);
                }
            }
        }
        return output;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("Failed to deserialize GraphElement: ") + e.what());
    }
}

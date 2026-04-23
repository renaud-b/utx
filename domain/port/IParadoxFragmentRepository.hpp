#pragma once
#include <vector>
#include <string>

#include "../model/Paradox.hpp"

namespace utx::domain::port {


    class IParadoxFragmentRepository {
    public:
        virtual ~IParadoxFragmentRepository() = default;

        /** Save a fragment */
        virtual void save_fragment(const model::ParadoxFragment &fragment) = 0;

        /** Load all fragments for a given key_id (usually 0 or 1 per node) */
        [[nodiscard]]
        virtual std::vector<model::ParadoxFragment>
        load_fragments_by_key(const std::string &key_id) const = 0;

        /** Delete all fragments for a key */
        virtual void delete_fragments(const std::string &key_id) = 0;

        /** Update ACL metadata */
        virtual void update_acl(
            const std::string &key_id,
            const std::vector<model::Address> &new_acl) = 0;

        /** Mark key as revoked */
        virtual void revoke_key(const std::string &key_id) = 0;

        virtual void save_pending_fragment(const model::ParadoxFragment& fragment) = 0;
        virtual void commit_fragment(const std::string& key_id,
                                     uint32_t fragment_index) = 0;
    };
}

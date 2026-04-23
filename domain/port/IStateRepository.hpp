#pragma once
#include <nlohmann/json.hpp>
#include <optional>
#include "../model/Types.hpp"

namespace utx::domain::port {

    class IStateRepository {
    public:
        virtual ~IStateRepository() = default;

        /** Sauvegarde l'état JSON d'une chaîne */
        virtual void save_state(const model::Address& addr, const nlohmann::json& state) = 0;

        /** Récupère l'état JSON d'une chaîne. Retourne un JSON vide ou nullopt si inconnu */
        virtual std::optional<nlohmann::json> load_state(const model::Address& addr) const = 0;

        /** Supprime l'état (utile pour une resynchronisation complète) */
        virtual void delete_state(const model::Address& addr) = 0;
    };
}
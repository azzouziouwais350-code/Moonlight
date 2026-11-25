#pragma once

#include <cassert>
#include <optional>
#include <utility>

#include "core/error.hpp"

namespace moonlight::core {

/// Resultat d'une operation qui produit une valeur OU echoue avec un Status.
///
/// Complement de Status pour les fonctions qui retournent quelque chose.
/// Pas d'exceptions : l'appelant doit tester isOk() avant d'utiliser value().
/// Appeler value() sur un resultat en echec est une erreur de programmation
/// (verifiee par assert en Debug).
template <typename T>
class [[nodiscard]] Result {
public:
    /// Succes (conversion implicite voulue : `return value;`).
    Result(T value) : value_(std::move(value)) {}  // NOLINT(google-explicit-constructor)

    /// Echec (conversion implicite voulue : `return Status::error(...);`).
    /// Un Status "ok" n'a pas de valeur a transporter : il est converti en
    /// erreur InvalidState plutot que de produire un Result sans valeur ni erreur.
    Result(Status error) : status_(std::move(error)) {  // NOLINT(google-explicit-constructor)
        if (status_.isOk()) {
            status_ = Status::error(ErrorCode::InvalidState,
                                    "Result constructed from an ok Status (no value)");
        }
    }

    bool isOk() const noexcept { return value_.has_value(); }
    explicit operator bool() const noexcept { return isOk(); }

    /// Statut de l'operation : "ok" si une valeur est presente.
    const Status& status() const noexcept { return status_; }

    T& value() & {
        assert(isOk());
        return *value_;
    }
    const T& value() const& {
        assert(isOk());
        return *value_;
    }
    T&& value() && {
        assert(isOk());
        return std::move(*value_);
    }

    /// Valeur en cas de succes, `fallback` sinon.
    T valueOr(T fallback) const& { return value_.has_value() ? *value_ : std::move(fallback); }

private:
    std::optional<T> value_;
    Status status_;  // "ok" par defaut ; n'est une erreur que si value_ est vide
};

}  // namespace moonlight::core

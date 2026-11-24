#pragma once

#include <string>
#include <utility>

namespace moonlight::core {

/// Categories d'erreurs du moteur. A etendre uniquement quand un besoin reel apparait.
enum class ErrorCode {
    None,             ///< Pas d'erreur.
    InvalidState,     ///< Operation appelee dans un etat du cycle de vie incorrect.
    InvalidArgument,  ///< Argument ou configuration invalide.
};

constexpr const char* toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None:            return "None";
        case ErrorCode::InvalidState:    return "InvalidState";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
    }
    return "Unknown";
}

/// Resultat d'une operation qui peut echouer sans produire de valeur.
/// Le moteur n'utilise pas d'exceptions pour ses erreurs previsibles : chaque
/// echec est retourne explicitement (et [[nodiscard]] empeche de l'ignorer).
class [[nodiscard]] Status {
public:
    /// Construit un statut "succes".
    Status() = default;

    static Status ok() { return Status(); }
    static Status error(ErrorCode code, std::string message) {
        return Status(code, std::move(message));
    }

    bool isOk() const noexcept { return code_ == ErrorCode::None; }
    explicit operator bool() const noexcept { return isOk(); }

    ErrorCode code() const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }

private:
    Status(ErrorCode code, std::string message) : code_(code), message_(std::move(message)) {}

    ErrorCode code_ = ErrorCode::None;
    std::string message_;
};

}  // namespace moonlight::core

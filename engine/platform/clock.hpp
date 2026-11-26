#pragma once

// Abstraction de l'horloge. Permet de remplacer le temps reel par un temps
// controle dans les tests, et de brancher plus tard une source propre a une plateforme.

#include <chrono>

namespace moonlight::platform {

class IClock {
public:
    virtual ~IClock() = default;

    IClock(const IClock&) = delete;
    IClock& operator=(const IClock&) = delete;

    /// Temps monotone : ne recule jamais, origine arbitraire. A utiliser pour
    /// mesurer des durees, jamais pour afficher une date.
    virtual std::chrono::nanoseconds monotonicNow() const = 0;

    /// Heure murale (peut etre ajustee par le systeme ou l'utilisateur).
    /// A utiliser pour les horodatages affiches ou ecrits dans des fichiers.
    virtual std::chrono::system_clock::time_point systemNow() const = 0;

protected:
    IClock() = default;
};

}  // namespace moonlight::platform

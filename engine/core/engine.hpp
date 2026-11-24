#pragma once

#include <cstdint>
#include <string>

#include "core/error.hpp"

namespace moonlight::core {

struct EngineConfig {
    /// Nom affiche dans les logs.
    std::string applicationName = "MOONLIGHT";

    /// Plafond du pas de temps accepte par update(), en secondes. Un pas plus
    /// grand (fenetre suspendue, point d'arret...) est ramene a cette valeur
    /// pour eviter qu'une simulation ne "saute" dans le temps.
    double maxFrameDeltaSeconds = 0.25;
};

/// Squelette du moteur : gere uniquement le cycle de vie et le temps simule.
///
/// Cycle de vie :  Created --initialize()--> Initialized --shutdown()--> ShutDown
///                 ShutDown --initialize()--> Initialized  (reinitialisation permise)
///
/// En 0.1.0 le moteur ne contient AUCUN sous-systeme de rendu, d'audio, d'entree
/// ni de jeu : il fournit la fondation sur laquelle ils seront branches.
///
/// Non thread-safe : les appels doivent venir d'un seul thread (ou etre serialises).
class Engine {
public:
    enum class State { Created, Initialized, ShutDown };

    explicit Engine(EngineConfig config = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /// Prepare le moteur. Echoue (InvalidState) s'il est deja initialise, ou
    /// (InvalidArgument) si la configuration est invalide.
    Status initialize();

    /// Avance la simulation de deltaSeconds. Echoue (InvalidState) si le moteur
    /// n'est pas initialise, ou (InvalidArgument) si le pas est negatif ou non fini.
    Status update(double deltaSeconds);

    /// Arrete le moteur. Sans effet s'il n'est pas initialise (idempotent).
    void shutdown();

    State state() const noexcept { return state_; }
    std::uint64_t frameCount() const noexcept { return frameCount_; }
    double elapsedSeconds() const noexcept { return elapsedSeconds_; }
    const EngineConfig& config() const noexcept { return config_; }

private:
    EngineConfig config_;
    State state_ = State::Created;
    std::uint64_t frameCount_ = 0;
    double elapsedSeconds_ = 0.0;
};

const char* toString(Engine::State state) noexcept;

}  // namespace moonlight::core

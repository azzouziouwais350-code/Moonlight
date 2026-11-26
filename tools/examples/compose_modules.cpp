// Exemple : composition des modules de la 0.2.0 AU NIVEAU DE L'APPLICATION.
//
//   configuration (fichier INI) -> logs -> montage des donnees de l'utilisateur -> moteur
//
// Ce programme sert de reference d'utilisation et de test d'integration des
// couches (core, platform, assets) ensemble. Il n'implemente aucun jeu.
// NOTE : Engine n'utilise pas encore le ResourceManager ; relier les deux est
// prevu pour la 0.3.0. Ici, l'application les cree cote a cote.
//
// Usage : moonlight_example_compose [chemin/vers/moonlight.ini]
// Sans fichier de configuration, les valeurs par defaut sont utilisees.

#include <cstdio>
#include <string>

#include "assets/resource_manager.hpp"
#include "core/config.hpp"
#include "core/engine.hpp"
#include "core/log.hpp"
#include "platform/config_io.hpp"
#include "platform/host_clock.hpp"
#include "platform/host_filesystem.hpp"

using namespace moonlight;

int main(int argc, char** argv) {
    const std::string configPath = (argc > 1) ? argv[1] : "moonlight.ini";

    platform::HostFileSystem fileSystem;
    platform::HostClock clock;
    const auto startedAt = clock.monotonicNow();

    // 1. Configuration : un fichier absent donne les valeurs par defaut ; un
    //    fichier present mais invalide est une erreur (jamais ignore en silence).
    core::Result<core::Config> config = platform::loadConfigOrDefault(fileSystem, configPath);
    if (!config.isOk()) {
        std::fprintf(stderr, "configuration error: %s\n", config.status().message().c_str());
        return 1;
    }

    // 2. Journalisation selon la configuration (niveau, format, destination).
    if (const core::Status logStatus = core::applyLogConfig(config.value().log); !logStatus.isOk()) {
        std::fprintf(stderr, "logging error: %s\n", logStatus.message().c_str());
        return 1;
    }

    // 3. Ressources : on monte le dossier de donnees fourni par l'utilisateur,
    //    s'il est configure. Rien du jeu n'est inclus dans ce depot.
    assets::ResourceManager resources(fileSystem);
    const std::string& gameDataDir = config.value().paths.gameDataDir;
    if (gameDataDir.empty()) {
        MOONLIGHT_LOG_WARN("app", "paths.game_data_dir is not configured: no game data is mounted");
    } else if (const core::Status mounted = resources.mountDirectory("game", gameDataDir);
               !mounted.isOk()) {
        MOONLIGHT_LOG_ERROR("app", "cannot mount game data: %s", mounted.message().c_str());
        return 1;
    }

    // 4. Moteur : cycle de vie sur quelques images simulees.
    core::Engine engine(core::makeEngineConfig(config.value()));
    if (const core::Status initialized = engine.initialize(); !initialized.isOk()) {
        MOONLIGHT_LOG_ERROR("app", "engine initialization failed: %s",
                            initialized.message().c_str());
        return 1;
    }
    for (int frame = 0; frame < 3; ++frame) {
        if (const core::Status updated = engine.update(1.0 / 60.0); !updated.isOk()) {
            MOONLIGHT_LOG_ERROR("app", "engine update failed: %s", updated.message().c_str());
            engine.shutdown();
            return 1;
        }
    }
    engine.shutdown();

    const auto elapsedMicroseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(clock.monotonicNow() - startedAt);
    MOONLIGHT_LOG_FIELDS(core::LogLevel::Info, "app", "finished",
                         {"frames", std::to_string(engine.frameCount())},
                         {"mounts", std::to_string(resources.mounts().size())},
                         {"elapsed_us", std::to_string(elapsedMicroseconds.count())});
    return 0;
}

#pragma once

// Configuration de MOONLIGHT : structure typee, valeurs par defaut, validation,
// lecture/ecriture du format texte de type INI.
//
// Ce module est PUR (aucun acces disque) : parseConfig() et serializeConfig()
// travaillent sur du texte. Le chargement et la sauvegarde de fichiers sont dans
// platform/config_io.hpp, qui s'appuie sur IFileSystem.
//
// Format (exemple, toutes les cles sont optionnelles) :
//
//     # commentaire (aussi ';')
//     [log]
//     level = info                 ; trace | debug | info | warning | error
//     format = text                ; text | json
//     file =                       ; vide = stderr
//
//     [paths]
//     game_data_dir = /chemin/vers/vos/donnees   ; fournies par l'utilisateur
//     mods_dir = mods
//     saves_dir = saves
//
//     [engine]
//     max_frame_delta_seconds = 0.25
//
// Regles : un seul "cle = valeur" par ligne ; sections et cles insensibles a la
// casse ; pas de commentaire en fin de ligne (une ligne de commentaire commence
// par '#' ou ';') ; valeur entre guillemets pour conserver des espaces de
// debut/fin (echappements \" et \\ uniquement). Strict : toute cle ou section
// inconnue, tout doublon et toute valeur invalide est une ERREUR, jamais ignoree
// en silence.

#include <cstddef>
#include <string>
#include <string_view>

#include "core/engine.hpp"
#include "core/log.hpp"
#include "core/log_sinks.hpp"
#include "core/result.hpp"

namespace moonlight::core {

/// Taille maximale acceptee pour un texte de configuration.
constexpr std::size_t kMaxConfigBytes = 1024 * 1024;
/// Longueur maximale d'un chemin dans la configuration.
constexpr std::size_t kMaxConfigPathLength = 4096;
/// Borne superieure de engine.max_frame_delta_seconds.
constexpr double kMaxFrameDeltaUpperBound = 10.0;

struct LogConfig {
    LogLevel minLevel = LogLevel::Info;
    LogFormat format = LogFormat::Text;
    std::string filePath;  ///< Vide : ecrit sur stderr.

    bool operator==(const LogConfig&) const = default;
};

struct PathsConfig {
    /// Dossier des donnees du jeu FOURNIES PAR L'UTILISATEUR. Vide = non configure.
    /// MOONLIGHT ne contient et ne distribue aucune donnee du jeu.
    std::string gameDataDir;
    std::string modsDir = "mods";
    std::string savesDir = "saves";

    bool operator==(const PathsConfig&) const = default;
};

struct EngineSettings {
    double maxFrameDeltaSeconds = 0.25;

    bool operator==(const EngineSettings&) const = default;
};

struct Config {
    LogConfig log;
    PathsConfig paths;
    EngineSettings engine;

    bool operator==(const Config&) const = default;
};

/// Verifie qu'une configuration est utilisable (plages, chemins, enumerations).
/// InvalidArgument si ce n'est pas le cas ; le message nomme la cle fautive.
Status validateConfig(const Config& config);

/// Analyse un texte de configuration. Part des valeurs par defaut ; seules les
/// cles presentes les modifient. En cas d'erreur, AUCUNE configuration partielle
/// n'est retournee : ParseError avec le numero de ligne (valeur ou syntaxe
/// invalide, cle inconnue, doublon), TooLarge si le texte depasse kMaxConfigBytes.
Result<Config> parseConfig(std::string_view text);

/// Produit le texte d'une configuration (toutes les cles, avec commentaires
/// d'aide). Precondition : validateConfig(config) reussit. parseConfig() relit
/// exactement la valeur ecrite.
std::string serializeConfig(const Config& config);

/// Parametres du moteur deduits de la configuration.
EngineConfig makeEngineConfig(const Config& config);

/// Applique la configuration de journalisation (niveau, format, destination) au
/// Logger. Tout ou rien : si la destination ne peut pas etre ouverte (ex. dossier
/// du fichier de log inexistant), le Logger reste inchange et l'erreur est retournee.
Status applyLogConfig(const LogConfig& config, Logger& logger = Logger::instance());

}  // namespace moonlight::core

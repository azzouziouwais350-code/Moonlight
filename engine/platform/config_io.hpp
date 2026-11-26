#pragma once

// Chargement et sauvegarde de la configuration via IFileSystem.

#include <string_view>

#include "core/config.hpp"
#include "core/result.hpp"
#include "platform/filesystem.hpp"

namespace moonlight::platform {

/// Charge et valide un fichier de configuration.
/// Erreurs : NotFound (fichier absent), AccessDenied/IoError (lecture),
/// TooLarge, ParseError (contenu invalide, avec numero de ligne). Le message
/// d'erreur est prefixe par le chemin du fichier.
core::Result<core::Config> loadConfig(const IFileSystem& fileSystem, std::string_view path);

/// Comme loadConfig, mais un fichier ABSENT donne la configuration par defaut
/// (premier lancement). Toute autre erreur - dont un fichier PRESENT mais
/// invalide - est retournee : une configuration cassee n'est jamais remplacee
/// silencieusement par les valeurs par defaut.
core::Result<core::Config> loadConfigOrDefault(const IFileSystem& fileSystem,
                                               std::string_view path);

/// Valide puis ecrit la configuration. Ecriture sure : le contenu est d'abord
/// ecrit dans "<path>.tmp" puis renomme ; en cas d'echec, l'ancien fichier est
/// conserve intact et le fichier temporaire est supprime. Cree les dossiers
/// parents manquants. InvalidArgument si la configuration est invalide.
core::Status saveConfig(IFileSystem& fileSystem, std::string_view path,
                        const core::Config& config);

}  // namespace moonlight::platform

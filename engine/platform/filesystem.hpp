#pragma once

// Abstraction du systeme de fichiers.
//
// Cette interface n'utilise QUE des types standards portables (chaines UTF-8,
// std::byte, std::span) : aucune API ni aucun type specifique a un systeme n'y
// apparait. Chaque plateforme fournit sa propre implementation :
//   - HostFileSystem : systeme de fichiers de l'hote (teste sous Linux) ;
//   - plus tard, par exemple : acces aux assets de l'APK Android, au
//     "sandbox" iOS, ou un systeme de fichiers en memoire pour les tests.
//
// Convention des chemins : UTF-8, separateur '/', sans caractere NUL. Une
// implementation peut accepter des chemins relatifs ou absolus ; ce qui est
// relatif est interprete par rapport a son propre contexte (pour l'hote : le
// dossier de travail du processus).

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"

namespace moonlight::platform {

using Bytes = std::vector<std::byte>;

enum class FileType {
    NotFound,   ///< L'element n'existe pas (ce n'est pas une erreur pour fileType()).
    File,       ///< Fichier ordinaire.
    Directory,  ///< Dossier.
    Other,      ///< Autre chose (peripherique, socket...).
};

/// Interface d'acces aux fichiers. Les implementations ne lancent pas d'exceptions.
///
/// Codes d'erreur communs (core::ErrorCode) :
///   InvalidArgument : chemin vide, contenant NUL, ou type d'element inadapte ;
///   NotFound        : element (ou dossier parent) inexistant ;
///   AccessDenied    : permissions insuffisantes ;
///   TooLarge        : fichier plus grand que la limite demandee ;
///   IoError         : toute autre erreur d'entree/sortie.
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    IFileSystem(const IFileSystem&) = delete;
    IFileSystem& operator=(const IFileSystem&) = delete;

    /// Type de l'element, liens symboliques suivis. Un element absent donne
    /// la VALEUR FileType::NotFound ; une erreur est reservee aux vrais problemes
    /// (permissions, chemin invalide...).
    virtual core::Result<FileType> fileType(std::string_view path) const = 0;

    /// Lit un fichier ordinaire en entier. Echoue avec TooLarge, SANS lire le
    /// contenu, si sa taille depasse `maxBytes`. Un fichier dont la taille
    /// change pendant la lecture donne IoError.
    virtual core::Result<Bytes> readFile(std::string_view path, std::uint64_t maxBytes) const = 0;

    /// Cree ou remplace un fichier. Le dossier parent doit exister (NotFound sinon).
    /// L'ecriture n'est PAS atomique : pour cela, ecrire un fichier temporaire
    /// puis appeler renameEntry().
    virtual core::Status writeFile(std::string_view path, std::span<const std::byte> data) = 0;

    /// Cree le dossier et ses parents manquants. Reussit s'il existe deja ;
    /// InvalidArgument si un fichier occupe cet emplacement.
    virtual core::Status createDirectories(std::string_view path) = 0;

    /// Deplace/renomme un fichier ; remplace la destination si c'est un fichier.
    /// Atomique quand le systeme sous-jacent le permet (c'est le cas pour
    /// l'hote POSIX dans un meme volume).
    virtual core::Status renameEntry(std::string_view from, std::string_view to) = 0;

    /// Supprime un fichier ordinaire (pas un dossier : InvalidArgument).
    virtual core::Status removeFile(std::string_view path) = 0;

    /// Chemin absolu, sans "." ni "..", liens symboliques resolus. L'element
    /// doit exister (NotFound sinon). Sert a verifier qu'un chemin ne sort pas
    /// d'une zone autorisee.
    virtual core::Result<std::string> canonicalPath(std::string_view path) const = 0;

protected:
    IFileSystem() = default;
};

// --- Aides construites sur l'interface -----------------------------------

/// Convertit du texte en octets.
Bytes toBytes(std::string_view text);

/// Lit un fichier comme texte (octets recopies tels quels, sans conversion).
core::Result<std::string> readTextFile(const IFileSystem& fileSystem, std::string_view path,
                                       std::uint64_t maxBytes);

/// Ecrit du texte dans un fichier (cree ou remplace ; non atomique).
core::Status writeTextFile(IFileSystem& fileSystem, std::string_view path, std::string_view text);

}  // namespace moonlight::platform

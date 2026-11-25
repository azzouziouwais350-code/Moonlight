// Systeme de fichiers en memoire pour les tests : implemente platform::IFileSystem
// sans toucher au disque, et permet de simuler des cas difficiles a produire
// sur un vrai disque (fichier illisible meme en root, liens symboliques, panne d'ecriture).
//
// Les chemins sont normalises en chemins absolus "/a/b" ; un chemin relatif est
// interprete depuis la racine "/". Les ".." sont resolus physiquement (comme sur
// un vrai systeme : apres un lien symbolique, ".." remonte depuis la cible).
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "platform/filesystem.hpp"

namespace moonlight::testing {

class MemoryFileSystem final : public moonlight::platform::IFileSystem {
public:
    MemoryFileSystem();

    // --- IFileSystem ---
    moonlight::core::Result<moonlight::platform::FileType> fileType(
        std::string_view path) const override;
    moonlight::core::Result<moonlight::platform::Bytes> readFile(
        std::string_view path, std::uint64_t maxBytes) const override;
    moonlight::core::Status writeFile(std::string_view path,
                                      std::span<const std::byte> data) override;
    moonlight::core::Status createDirectories(std::string_view path) override;
    moonlight::core::Status renameEntry(std::string_view from, std::string_view to) override;
    moonlight::core::Status removeFile(std::string_view path) override;
    moonlight::core::Result<std::string> canonicalPath(std::string_view path) const override;

    // --- Preparation des scenarios de test ---

    /// Cree un fichier (et ses dossiers parents).
    void addFile(std::string_view path, std::string_view content);
    void addDirectory(std::string_view path);
    /// Cree un lien symbolique `linkPath` vers `absoluteTarget` (chemin absolu).
    void addSymlink(std::string_view linkPath, std::string_view absoluteTarget);
    /// Les lectures de ce fichier echoueront avec AccessDenied.
    void setUnreadable(std::string_view path);
    /// Toutes les ecritures echoueront avec IoError tant que `fail` est vrai.
    void setFailWrites(bool fail) { failWrites_ = fail; }

    /// Contenu d'un fichier (liens suivis), ou nullopt s'il n'existe pas.
    std::optional<std::string> contentOf(std::string_view path) const;
    bool hasFile(std::string_view path) const { return contentOf(path).has_value(); }

private:
    // Resout liens et "..". Retourne un chemin absolu normalise, ou une erreur
    // (chemin invalide, boucle de liens).
    moonlight::core::Result<std::string> resolve(std::string_view path) const;

    std::map<std::string, moonlight::platform::Bytes> files_;
    std::set<std::string> directories_;
    std::map<std::string, std::string> links_;
    std::set<std::string> unreadable_;
    bool failWrites_ = false;
};

}  // namespace moonlight::testing

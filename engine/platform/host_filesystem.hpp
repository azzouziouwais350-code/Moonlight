#pragma once

#include "platform/filesystem.hpp"

namespace moonlight::platform {

/// Implementation de IFileSystem sur le systeme de fichiers de la machine hote,
/// construite sur la bibliotheque standard (<filesystem> et <cstdio>), sans exceptions.
///
/// Statut : testee UNIQUEMENT sous Linux x86-64. Les chemins sont passes tels
/// quels (UTF-8 avec '/') : ce qui est specifique a Windows (chemins larges,
/// lettres de lecteur) n'est pas gere. Aucune restriction de dossier racine :
/// c'est le role du ResourceManager de borner ce qui est accessible.
class HostFileSystem final : public IFileSystem {
public:
    HostFileSystem() = default;

    core::Result<FileType> fileType(std::string_view path) const override;
    core::Result<Bytes> readFile(std::string_view path, std::uint64_t maxBytes) const override;
    core::Status writeFile(std::string_view path, std::span<const std::byte> data) override;
    core::Status createDirectories(std::string_view path) override;
    core::Status renameEntry(std::string_view from, std::string_view to) override;
    core::Status removeFile(std::string_view path) override;
    core::Result<std::string> canonicalPath(std::string_view path) const override;
};

}  // namespace moonlight::platform

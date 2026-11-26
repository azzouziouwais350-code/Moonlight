#pragma once

// Gestionnaire de ressources : point d'acces unique et SUR aux fichiers de donnees.
//
// Principe : le code du moteur ne manipule jamais de chemins de l'hote. Il
// demande des ressources par "chemin virtuel" ("textures/hd/menu.png"), que le
// ResourceManager resout dans des dossiers "montes" explicitement par
// l'application (par exemple : le dossier de mods, puis le dossier de donnees
// fournies par l'utilisateur). Un chemin virtuel ne peut jamais sortir d'un dossier
// monte, ni par "..", ni par chemin absolu, ni par lien symbolique.
//
// Ce module ne contient AUCUNE donnee du jeu original et n'interprete aucun
// format du jeu : il localise et lit des octets. Le chargement des fichiers du
// joueur passera par ces memes montages (voir Config::paths.gameDataDir).
//
// Pas de cache en 0.2.0. Non thread-safe pour les montages ; resolve/exists/readAll
// sont const et peuvent s'executer en parallele tant qu'on ne monte/demonte rien.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "platform/filesystem.hpp"

namespace moonlight::assets {

struct ResourceManagerOptions {
    /// Taille maximale d'une ressource lue par readAll() (TooLarge au-dela).
    std::uint64_t maxResourceBytes = 256ull * 1024 * 1024;
};

/// Un dossier monte.
struct MountInfo {
    std::string name;      ///< Nom du montage (unique).
    std::string rootPath;  ///< Chemin canonique du dossier (liens resolus).
    int priority = 0;      ///< Plus grand = consulte en premier.
};

/// Resultat de resolve().
struct ResolvedResource {
    std::string virtualPath;  ///< Chemin demande.
    std::string mountName;    ///< Montage qui contient la ressource.
    std::string hostPath;     ///< Chemin canonique du fichier, garanti sous la racine du montage.
};

class ResourceManager {
public:
    /// `fileSystem` doit survivre au ResourceManager (il n'est pas copie).
    explicit ResourceManager(const platform::IFileSystem& fileSystem,
                             ResourceManagerOptions options = {});

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    /// Verifie la syntaxe d'un chemin virtuel (sans acceder au disque).
    ///
    /// Accepte : composantes separees par '/', par exemple "dir/file.ext" ; les
    /// octets UTF-8 non ASCII sont acceptes (non valides en UTF-8 : non verifie).
    /// Refuse (InvalidArgument) : chemin vide ou > 1024 octets ; chemin absolu
    /// (commence par '/') ; '/' final ou "//" ; composantes "." ou ".." ; '\\' ;
    /// ':' ; caracteres de controle (dont NUL).
    static core::Status validateVirtualPath(std::string_view virtualPath);

    /// Monte un dossier existant sous le nom `name` ([A-Za-z0-9_-], 1 a 64 caracteres).
    /// Un montage de priorite plus elevee masque les fichiers de meme chemin des
    /// autres ; a priorite egale, le premier monte est consulte en premier.
    /// Erreurs : InvalidArgument (nom invalide ou deja pris, ou ce n'est pas un
    /// dossier), NotFound (dossier absent), AccessDenied/IoError.
    core::Status mountDirectory(std::string_view name, std::string_view directory,
                                int priority = 0);

    /// Retire un montage (NotFound s'il n'existe pas).
    core::Status unmount(std::string_view name);

    /// Montages dans l'ordre de consultation.
    const std::vector<MountInfo>& mounts() const noexcept { return mounts_; }

    /// Trouve la ressource. Erreurs :
    ///   InvalidArgument : chemin virtuel invalide, ou l'element trouve n'est pas un fichier ;
    ///   NotFound        : aucun montage ne la contient (ou aucun montage) ;
    ///   AccessDenied    : elle resout hors de la racine de son montage (lien symbolique) ;
    ///   autres          : toute erreur du systeme de fichiers est RETOURNEE, jamais
    ///                     contournee en passant au montage suivant ("echec ferme").
    core::Result<ResolvedResource> resolve(std::string_view virtualPath) const;

    /// Vrai si la ressource existe. Faux si elle est simplement absente ; une
    /// erreur (chemin invalide, acces refuse...) est retournee comme erreur.
    core::Result<bool> exists(std::string_view virtualPath) const;

    /// Lit une ressource en entier (TooLarge si > options.maxResourceBytes,
    /// AccessDenied si illisible, IoError sinon, ou les erreurs de resolve()).
    core::Result<platform::Bytes> readAll(std::string_view virtualPath) const;

private:
    const platform::IFileSystem& fileSystem_;
    ResourceManagerOptions options_;
    std::vector<MountInfo> mounts_;  // trie par priorite decroissante, stable
};

}  // namespace moonlight::assets

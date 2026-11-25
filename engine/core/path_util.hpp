#pragma once

// Manipulation TEXTUELLE de chemins, independante de tout systeme.
//
// Convention des chemins de MOONLIGHT : chaines UTF-8, separateur '/'. Aucune
// de ces fonctions ne touche au disque, ne resout "." / ".." ni les liens
// symboliques : pour cela, voir platform::IFileSystem::canonicalPath().

#include <string>
#include <string_view>

namespace moonlight::core {

/// Concatene deux chemins avec exactement un '/' entre eux.
/// joinPath("a", "b") == "a/b" ; joinPath("a/", "b") == "a/b" ; joinPath("", "b") == "b".
inline std::string joinPath(std::string_view base, std::string_view relative) {
    if (base.empty()) {
        return std::string(relative);
    }
    if (relative.empty()) {
        return std::string(base);
    }
    std::string result(base);
    if (result.back() != '/') {
        result.push_back('/');
    }
    result.append(relative);
    return result;
}

/// Dossier parent (jusqu'au dernier '/', exclu). "a/b" -> "a" ; "b" -> "" ; "/b" -> "/" ; "/" -> "/".
inline std::string_view parentPath(std::string_view path) {
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    if (slash == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash);
}

/// Vrai si `path` est STRICTEMENT a l'interieur de `root`, en comparant des
/// composantes entieres : "/data/x" est dans "/data", mais "/database" ne l'est pas.
/// Les deux chemins doivent deja etre normalises (sans "." ni ".." ni '/' final, sauf "/").
inline bool isStrictlyInside(std::string_view root, std::string_view path) {
    if (root.empty() || path.size() <= root.size()) {
        return false;
    }
    if (root.back() == '/') {  // racine du systeme de fichiers : "/"
        return path.substr(0, root.size()) == root;
    }
    return path.substr(0, root.size()) == root && path[root.size()] == '/';
}

}  // namespace moonlight::core

// Dossier temporaire unique, supprime a la destruction (tests uniquement).
//
// Utilise <filesystem> directement : c'est du code de test hote, pas du code
// du moteur (le moteur passe par platform::IFileSystem).
#pragma once

#include <chrono>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>

namespace moonlight::testing {

class TempDir {
public:
    TempDir() {
        std::random_device device;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        std::error_code ec;
        const std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        path_ = base / ("moonlight-test-" + std::to_string(ticks) + "-" +
                        std::to_string(device()));
        std::filesystem::create_directories(path_, ec);
        created_ = !ec;
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);  // best effort
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    bool isValid() const { return created_; }

    /// Chemin du dossier (separateurs '/').
    std::string path() const { return path_.generic_string(); }

    /// Chemin d'un element du dossier.
    std::string file(const std::string& relative) const {
        return (path_ / relative).generic_string();
    }

private:
    std::filesystem::path path_;
    bool created_ = false;
};

}  // namespace moonlight::testing

#include "assets/resource_manager.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "core/log.hpp"
#include "core/path_util.hpp"

namespace moonlight::assets {

using core::ErrorCode;
using core::Result;
using core::Status;
using platform::FileType;

namespace {

constexpr const char* kLogCategory = "assets";
constexpr std::size_t kMaxVirtualPathLength = 1024;
constexpr std::size_t kMaxMountNameLength = 64;

Status invalidPath(std::string_view virtualPath, const std::string& reason) {
    // Le chemin est tronque dans le message : il peut etre arbitrairement long.
    const std::string_view shown = virtualPath.substr(0, 80);
    std::string text = "invalid resource path '" + std::string(shown);
    text += (virtualPath.size() > shown.size() ? "...': " : "': ");
    text += reason;
    return Status::error(ErrorCode::InvalidArgument, std::move(text));
}

bool isControl(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}

bool isValidMountName(std::string_view name) {
    if (name.empty() || name.size() > kMaxMountNameLength) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
}

}  // namespace

ResourceManager::ResourceManager(const platform::IFileSystem& fileSystem,
                                 ResourceManagerOptions options)
    : fileSystem_(fileSystem), options_(options) {}

Status ResourceManager::validateVirtualPath(std::string_view virtualPath) {
    if (virtualPath.empty()) {
        return invalidPath(virtualPath, "path is empty");
    }
    if (virtualPath.size() > kMaxVirtualPathLength) {
        return invalidPath(virtualPath, "path is longer than 1024 bytes");
    }
    if (virtualPath.front() == '/') {
        return invalidPath(virtualPath, "absolute paths are not allowed");
    }
    for (const char c : virtualPath) {
        if (isControl(c)) {
            return invalidPath(virtualPath, "control characters are not allowed");
        }
        if (c == '\\') {
            return invalidPath(virtualPath, "backslash is not allowed (use '/')");
        }
        if (c == ':') {
            return invalidPath(virtualPath, "':' is not allowed");
        }
    }

    // Verification composante par composante.
    std::size_t start = 0;
    while (start <= virtualPath.size()) {
        const std::size_t slash = virtualPath.find('/', start);
        const std::size_t end = (slash == std::string_view::npos) ? virtualPath.size() : slash;
        const std::string_view component = virtualPath.substr(start, end - start);
        if (component.empty()) {
            return invalidPath(virtualPath, "empty path component ('//' or trailing '/')");
        }
        if (component == "." || component == "..") {
            return invalidPath(virtualPath, "'.' and '..' components are not allowed");
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return Status::ok();
}

Status ResourceManager::mountDirectory(std::string_view name, std::string_view directory,
                                       int priority) {
    if (!isValidMountName(name)) {
        return Status::error(ErrorCode::InvalidArgument,
                             "invalid mount name '" + std::string(name) +
                                 "' (use 1-64 characters among A-Z a-z 0-9 _ -)");
    }
    const bool taken = std::any_of(mounts_.begin(), mounts_.end(),
                                   [&](const MountInfo& m) { return m.name == name; });
    if (taken) {
        return Status::error(ErrorCode::InvalidArgument,
                             "a mount named '" + std::string(name) + "' already exists");
    }

    const Result<FileType> type = fileSystem_.fileType(directory);
    if (!type.isOk()) {
        return type.status();
    }
    if (type.value() == FileType::NotFound) {
        return Status::error(ErrorCode::NotFound,
                             "mount directory not found: '" + std::string(directory) + "'");
    }
    if (type.value() != FileType::Directory) {
        return Status::error(ErrorCode::InvalidArgument,
                             "mount path is not a directory: '" + std::string(directory) + "'");
    }
    Result<std::string> root = fileSystem_.canonicalPath(directory);
    if (!root.isOk()) {
        return root.status();
    }

    MountInfo mount{std::string(name), std::move(root).value(), priority};
    // Insertion stable : apres tous les montages de priorite superieure ou egale.
    const auto position =
        std::find_if(mounts_.begin(), mounts_.end(),
                     [&](const MountInfo& existing) { return existing.priority < priority; });
    MOONLIGHT_LOG_FIELDS(core::LogLevel::Info, kLogCategory, "directory mounted",
                         {"mount", name}, {"root", mount.rootPath},
                         {"priority", std::to_string(priority)});
    mounts_.insert(position, std::move(mount));
    return Status::ok();
}

Status ResourceManager::unmount(std::string_view name) {
    const auto found = std::find_if(mounts_.begin(), mounts_.end(),
                                    [&](const MountInfo& m) { return m.name == name; });
    if (found == mounts_.end()) {
        return Status::error(ErrorCode::NotFound,
                             "no mount named '" + std::string(name) + "'");
    }
    MOONLIGHT_LOG_FIELDS(core::LogLevel::Info, kLogCategory, "directory unmounted",
                         {"mount", name});
    mounts_.erase(found);
    return Status::ok();
}

Result<ResolvedResource> ResourceManager::resolve(std::string_view virtualPath) const {
    if (Status valid = validateVirtualPath(virtualPath); !valid.isOk()) {
        return valid;
    }
    if (mounts_.empty()) {
        return Status::error(ErrorCode::NotFound,
                             "resource '" + std::string(virtualPath) +
                                 "' not found: no directory is mounted");
    }

    for (const MountInfo& mount : mounts_) {
        const std::string candidate = core::joinPath(mount.rootPath, virtualPath);

        const Result<FileType> type = fileSystem_.fileType(candidate);
        if (!type.isOk()) {
            return type.status();  // echec ferme : on ne passe pas au montage suivant
        }
        if (type.value() == FileType::NotFound) {
            continue;
        }

        // Le chemin final (liens resolus) doit rester sous la racine du montage.
        Result<std::string> canonical = fileSystem_.canonicalPath(candidate);
        if (!canonical.isOk()) {
            return canonical.status();
        }
        if (!core::isStrictlyInside(mount.rootPath, canonical.value())) {
            MOONLIGHT_LOG_FIELDS(core::LogLevel::Warning, kLogCategory,
                                 "resource resolves outside its mount (symbolic link?)",
                                 {"resource", virtualPath}, {"mount", mount.name},
                                 {"resolved", canonical.value()});
            return Status::error(ErrorCode::AccessDenied,
                                 "resource '" + std::string(virtualPath) +
                                     "' resolves outside of mount '" + mount.name + "'");
        }
        if (type.value() != FileType::File) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "resource '" + std::string(virtualPath) +
                                     "' is not a regular file");
        }
        return ResolvedResource{std::string(virtualPath), mount.name,
                                std::move(canonical).value()};
    }

    return Status::error(ErrorCode::NotFound,
                         "resource '" + std::string(virtualPath) + "' not found in " +
                             std::to_string(mounts_.size()) + " mount(s)");
}

Result<bool> ResourceManager::exists(std::string_view virtualPath) const {
    const Result<ResolvedResource> resolved = resolve(virtualPath);
    if (resolved.isOk()) {
        return true;
    }
    if (resolved.status().code() == ErrorCode::NotFound) {
        return false;
    }
    return resolved.status();
}

Result<platform::Bytes> ResourceManager::readAll(std::string_view virtualPath) const {
    const Result<ResolvedResource> resolved = resolve(virtualPath);
    if (!resolved.isOk()) {
        return resolved.status();
    }
    Result<platform::Bytes> bytes =
        fileSystem_.readFile(resolved.value().hostPath, options_.maxResourceBytes);
    if (!bytes.isOk()) {
        return Status::error(bytes.status().code(),
                             "resource '" + std::string(virtualPath) + "': " +
                                 bytes.status().message());
    }
    MOONLIGHT_LOG_FIELDS(core::LogLevel::Debug, kLogCategory, "resource read",
                         {"resource", virtualPath}, {"mount", resolved.value().mountName},
                         {"bytes", std::to_string(bytes.value().size())});
    return bytes;
}

}  // namespace moonlight::assets

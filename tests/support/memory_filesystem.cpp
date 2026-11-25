#include "memory_filesystem.hpp"

#include <deque>
#include <vector>

#include "core/path_util.hpp"

namespace moonlight::testing {

using moonlight::core::ErrorCode;
using moonlight::core::Result;
using moonlight::core::Status;
using moonlight::platform::Bytes;
using moonlight::platform::FileType;

namespace {

constexpr int kMaxLinkHops = 40;

Status checkPath(std::string_view path) {
    if (path.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "path is empty");
    }
    if (path.find('\0') != std::string_view::npos) {
        return Status::error(ErrorCode::InvalidArgument, "path contains a NUL byte");
    }
    return Status::ok();
}

std::deque<std::string> splitComponents(std::string_view path) {
    std::deque<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = (slash == std::string_view::npos) ? path.size() : slash;
        if (end > start) {
            parts.emplace_back(path.substr(start, end - start));
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return parts;
}

std::string withoutLastComponent(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos || slash == 0 ? std::string() : path.substr(0, slash);
}

}  // namespace

MemoryFileSystem::MemoryFileSystem() { directories_.insert("/"); }

Result<std::string> MemoryFileSystem::resolve(std::string_view path) const {
    if (Status check = checkPath(path); !check.isOk()) {
        return check;
    }
    std::deque<std::string> queue = splitComponents(path);
    std::string resolved;  // "" represente la racine
    int hops = 0;
    while (!queue.empty()) {
        const std::string component = queue.front();
        queue.pop_front();
        if (component == ".") {
            continue;
        }
        if (component == "..") {
            resolved = withoutLastComponent(resolved);
            continue;
        }
        const std::string candidate = resolved + "/" + component;
        const auto link = links_.find(candidate);
        if (link != links_.end()) {
            if (++hops > kMaxLinkHops) {
                return Status::error(ErrorCode::IoError, "too many levels of symbolic links");
            }
            const std::deque<std::string> target = splitComponents(link->second);
            queue.insert(queue.begin(), target.begin(), target.end());
            resolved.clear();  // la cible est absolue
            continue;
        }
        resolved = candidate;
    }
    return resolved.empty() ? std::string("/") : resolved;
}

Result<FileType> MemoryFileSystem::fileType(std::string_view path) const {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return resolved.status();
    }
    if (files_.count(resolved.value()) != 0) {
        return FileType::File;
    }
    if (directories_.count(resolved.value()) != 0) {
        return FileType::Directory;
    }
    return FileType::NotFound;
}

Result<Bytes> MemoryFileSystem::readFile(std::string_view path, std::uint64_t maxBytes) const {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return resolved.status();
    }
    const auto file = files_.find(resolved.value());
    if (file == files_.end()) {
        if (directories_.count(resolved.value()) != 0) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "not a regular file: '" + std::string(path) + "'");
        }
        return Status::error(ErrorCode::NotFound, "file not found: '" + std::string(path) + "'");
    }
    if (unreadable_.count(resolved.value()) != 0) {
        return Status::error(ErrorCode::AccessDenied,
                             "permission denied: '" + std::string(path) + "'");
    }
    if (file->second.size() > maxBytes) {
        return Status::error(ErrorCode::TooLarge,
                             "file '" + std::string(path) + "' exceeds the size limit");
    }
    return file->second;
}

Status MemoryFileSystem::writeFile(std::string_view path, std::span<const std::byte> data) {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return resolved.status();
    }
    if (failWrites_) {
        return Status::error(ErrorCode::IoError, "simulated write failure");
    }
    if (directories_.count(resolved.value()) != 0) {
        return Status::error(ErrorCode::InvalidArgument,
                             "is a directory: '" + std::string(path) + "'");
    }
    const std::string parent = withoutLastComponent(resolved.value());
    if (directories_.count(parent.empty() ? "/" : parent) == 0) {
        return Status::error(ErrorCode::NotFound,
                             "parent directory not found for '" + std::string(path) + "'");
    }
    files_[resolved.value()] = Bytes(data.begin(), data.end());
    return Status::ok();
}

Status MemoryFileSystem::createDirectories(std::string_view path) {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return resolved.status();
    }
    if (files_.count(resolved.value()) != 0) {
        return Status::error(ErrorCode::InvalidArgument,
                             "a non-directory already exists at '" + std::string(path) + "'");
    }
    // Cree chaque composante manquante (aucune ne doit etre un fichier).
    std::string current;
    for (const std::string& component : splitComponents(resolved.value())) {
        current += "/" + component;
        if (files_.count(current) != 0) {
            return Status::error(ErrorCode::InvalidArgument,
                                 "a file is in the way at '" + current + "'");
        }
        directories_.insert(current);
    }
    return Status::ok();
}

Status MemoryFileSystem::renameEntry(std::string_view from, std::string_view to) {
    const Result<std::string> source = resolve(from);
    if (!source.isOk()) {
        return source.status();
    }
    const Result<std::string> destination = resolve(to);
    if (!destination.isOk()) {
        return destination.status();
    }
    if (failWrites_) {
        return Status::error(ErrorCode::IoError, "simulated write failure");
    }
    const auto file = files_.find(source.value());
    if (file == files_.end()) {
        return Status::error(ErrorCode::NotFound, "source not found: '" + std::string(from) + "'");
    }
    if (directories_.count(destination.value()) != 0) {
        return Status::error(ErrorCode::InvalidArgument,
                             "destination is a directory: '" + std::string(to) + "'");
    }
    const std::string parent = withoutLastComponent(destination.value());
    if (directories_.count(parent.empty() ? "/" : parent) == 0) {
        return Status::error(ErrorCode::NotFound,
                             "destination directory not found for '" + std::string(to) + "'");
    }
    Bytes content = std::move(file->second);
    files_.erase(file);
    files_[destination.value()] = std::move(content);
    return Status::ok();
}

Status MemoryFileSystem::removeFile(std::string_view path) {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return resolved.status();
    }
    if (directories_.count(resolved.value()) != 0) {
        return Status::error(ErrorCode::InvalidArgument,
                             "is a directory, not a file: '" + std::string(path) + "'");
    }
    if (files_.erase(resolved.value()) == 0) {
        return Status::error(ErrorCode::NotFound, "file not found: '" + std::string(path) + "'");
    }
    return Status::ok();
}

Result<std::string> MemoryFileSystem::canonicalPath(std::string_view path) const {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return resolved.status();
    }
    if (files_.count(resolved.value()) == 0 && directories_.count(resolved.value()) == 0) {
        return Status::error(ErrorCode::NotFound, "no such path: '" + std::string(path) + "'");
    }
    return resolved.value();
}

void MemoryFileSystem::addDirectory(std::string_view path) {
    const Status status = createDirectories(path);
    (void)status;  // un scenario de test mal construit se verra aux verifications
}

void MemoryFileSystem::addFile(std::string_view path, std::string_view content) {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return;
    }
    addDirectory(moonlight::core::parentPath(resolved.value()));
    files_[resolved.value()] = moonlight::platform::toBytes(content);
}

void MemoryFileSystem::addSymlink(std::string_view linkPath, std::string_view absoluteTarget) {
    // Le lien lui-meme n'est pas suivi : on normalise seulement son dossier parent.
    std::deque<std::string> parts = splitComponents(linkPath);
    if (parts.empty()) {
        return;
    }
    const std::string name = parts.back();
    parts.pop_back();
    std::string parentPath = "/";
    for (const std::string& part : parts) {
        parentPath = moonlight::core::joinPath(parentPath, part);
    }
    const Result<std::string> parent = resolve(parentPath);
    if (!parent.isOk()) {
        return;
    }
    addDirectory(parent.value());
    links_[moonlight::core::joinPath(parent.value(), name)] = std::string(absoluteTarget);
}

void MemoryFileSystem::setUnreadable(std::string_view path) {
    const Result<std::string> resolved = resolve(path);
    if (resolved.isOk()) {
        unreadable_.insert(resolved.value());
    }
}

std::optional<std::string> MemoryFileSystem::contentOf(std::string_view path) const {
    const Result<std::string> resolved = resolve(path);
    if (!resolved.isOk()) {
        return std::nullopt;
    }
    const auto file = files_.find(resolved.value());
    if (file == files_.end()) {
        return std::nullopt;
    }
    std::string text(file->second.size(), '\0');
    for (std::size_t i = 0; i < text.size(); ++i) {
        text[i] = static_cast<char>(std::to_integer<unsigned char>(file->second[i]));
    }
    return text;
}

}  // namespace moonlight::testing

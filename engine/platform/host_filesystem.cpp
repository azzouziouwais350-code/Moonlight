#include "platform/host_filesystem.hpp"

#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>

namespace moonlight::platform {

namespace fs = std::filesystem;
using core::ErrorCode;
using core::Result;
using core::Status;

namespace {

/// RAII sur FILE* (ferme a la destruction).
struct FileCloser {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};
using UniqueFile = std::unique_ptr<std::FILE, FileCloser>;

Status checkPath(std::string_view path) {
    if (path.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "path is empty");
    }
    if (path.find('\0') != std::string_view::npos) {
        return Status::error(ErrorCode::InvalidArgument, "path contains a NUL byte");
    }
    return Status::ok();
}

ErrorCode classify(const std::error_code& ec) {
    if (ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory) {
        return ErrorCode::NotFound;
    }
    if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted ||
        ec == std::errc::read_only_file_system) {
        return ErrorCode::AccessDenied;
    }
    if (ec == std::errc::is_a_directory || ec == std::errc::invalid_argument) {
        return ErrorCode::InvalidArgument;
    }
    return ErrorCode::IoError;
}

Status errorFrom(const std::error_code& ec, std::string_view action, std::string_view path) {
    return Status::error(classify(ec), std::string(action) + " '" + std::string(path) +
                                           "': " + ec.message());
}

Status errorFromErrno(int errnoValue, std::string_view action, std::string_view path) {
    return errorFrom(std::error_code(errnoValue, std::generic_category()), action, path);
}

fs::path toPath(std::string_view path) { return fs::path(std::string(path)); }

}  // namespace

Result<FileType> HostFileSystem::fileType(std::string_view path) const {
    if (Status check = checkPath(path); !check.isOk()) {
        return check;
    }
    std::error_code ec;
    const fs::file_status status = fs::status(toPath(path), ec);
    if (ec) {
        if (classify(ec) == ErrorCode::NotFound) {
            return FileType::NotFound;
        }
        return errorFrom(ec, "cannot stat", path);
    }
    switch (status.type()) {
        case fs::file_type::not_found: return FileType::NotFound;
        case fs::file_type::regular:   return FileType::File;
        case fs::file_type::directory: return FileType::Directory;
        default:                       return FileType::Other;
    }
}

Result<Bytes> HostFileSystem::readFile(std::string_view path, std::uint64_t maxBytes) const {
    const Result<FileType> type = fileType(path);
    if (!type.isOk()) {
        return type.status();
    }
    if (type.value() == FileType::NotFound) {
        return Status::error(ErrorCode::NotFound, "file not found: '" + std::string(path) + "'");
    }
    if (type.value() != FileType::File) {
        return Status::error(ErrorCode::InvalidArgument,
                             "not a regular file: '" + std::string(path) + "'");
    }

    std::error_code ec;
    const std::uintmax_t size = fs::file_size(toPath(path), ec);
    if (ec) {
        return errorFrom(ec, "cannot get size of", path);
    }
    if (size > maxBytes || size > std::numeric_limits<std::size_t>::max()) {
        return Status::error(ErrorCode::TooLarge,
                             "file '" + std::string(path) + "' is " + std::to_string(size) +
                                 " bytes, limit is " + std::to_string(maxBytes));
    }

    const std::string pathString(path);
    const UniqueFile file(std::fopen(pathString.c_str(), "rb"));
    if (!file) {
        return errorFromErrno(errno, "cannot open", path);
    }

    Bytes data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        const std::size_t read = std::fread(data.data(), 1, data.size(), file.get());
        if (read != data.size()) {
            return Status::error(ErrorCode::IoError,
                                 "short read or file changed while reading '" + pathString + "'");
        }
    }
    // Le fichier ne doit pas avoir grandi depuis la mesure de sa taille.
    if (std::fgetc(file.get()) != EOF) {
        return Status::error(ErrorCode::IoError,
                             "file changed while reading '" + pathString + "'");
    }
    if (std::ferror(file.get()) != 0) {
        return Status::error(ErrorCode::IoError, "read error on '" + pathString + "'");
    }
    return data;
}

Status HostFileSystem::writeFile(std::string_view path, std::span<const std::byte> data) {
    if (Status check = checkPath(path); !check.isOk()) {
        return check;
    }
    const std::string pathString(path);
    std::FILE* raw = std::fopen(pathString.c_str(), "wb");
    if (raw == nullptr) {
        return errorFromErrno(errno, "cannot open for writing", path);
    }
    UniqueFile file(raw);

    if (!data.empty() && std::fwrite(data.data(), 1, data.size(), file.get()) != data.size()) {
        const int savedErrno = errno;
        return errorFromErrno(savedErrno, "write failed on", path);
    }
    // fclose peut reveler une erreur d'ecriture differee (disque plein...).
    if (std::fclose(file.release()) != 0) {
        const int savedErrno = errno;
        return errorFromErrno(savedErrno, "cannot finish writing", path);
    }
    return Status::ok();
}

Status HostFileSystem::createDirectories(std::string_view path) {
    const Result<FileType> type = fileType(path);
    if (!type.isOk()) {
        return type.status();
    }
    if (type.value() == FileType::Directory) {
        return Status::ok();
    }
    if (type.value() != FileType::NotFound) {
        return Status::error(ErrorCode::InvalidArgument,
                             "a non-directory already exists at '" + std::string(path) + "'");
    }
    std::error_code ec;
    fs::create_directories(toPath(path), ec);
    if (ec) {
        return errorFrom(ec, "cannot create directories", path);
    }
    return Status::ok();
}

Status HostFileSystem::renameEntry(std::string_view from, std::string_view to) {
    if (Status check = checkPath(from); !check.isOk()) {
        return check;
    }
    if (Status check = checkPath(to); !check.isOk()) {
        return check;
    }
    std::error_code ec;
    fs::rename(toPath(from), toPath(to), ec);
    if (ec) {
        return errorFrom(ec, "cannot rename '" + std::string(from) + "' to", to);
    }
    return Status::ok();
}

Status HostFileSystem::removeFile(std::string_view path) {
    const Result<FileType> type = fileType(path);
    if (!type.isOk()) {
        return type.status();
    }
    if (type.value() == FileType::NotFound) {
        return Status::error(ErrorCode::NotFound, "file not found: '" + std::string(path) + "'");
    }
    if (type.value() == FileType::Directory) {
        return Status::error(ErrorCode::InvalidArgument,
                             "is a directory, not a file: '" + std::string(path) + "'");
    }
    std::error_code ec;
    fs::remove(toPath(path), ec);
    if (ec) {
        return errorFrom(ec, "cannot remove", path);
    }
    return Status::ok();
}

Result<std::string> HostFileSystem::canonicalPath(std::string_view path) const {
    if (Status check = checkPath(path); !check.isOk()) {
        return check;
    }
    std::error_code ec;
    const fs::path canonical = fs::canonical(toPath(path), ec);
    if (ec) {
        return errorFrom(ec, "cannot resolve", path);
    }
    return canonical.generic_string();
}

}  // namespace moonlight::platform

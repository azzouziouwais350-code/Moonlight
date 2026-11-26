#include "platform/config_io.hpp"

#include <string>

#include "core/log.hpp"
#include "core/path_util.hpp"

namespace moonlight::platform {

using core::Config;
using core::ErrorCode;
using core::Result;
using core::Status;

namespace {

constexpr const char* kLogCategory = "config";

Status withPath(const Status& status, std::string_view path) {
    return Status::error(status.code(), std::string(path) + ": " + status.message());
}

}  // namespace

Result<Config> loadConfig(const IFileSystem& fileSystem, std::string_view path) {
    const Result<std::string> text = readTextFile(fileSystem, path, core::kMaxConfigBytes);
    if (!text.isOk()) {
        return withPath(text.status(), path);
    }
    Result<Config> config = core::parseConfig(text.value());
    if (!config.isOk()) {
        return withPath(config.status(), path);
    }
    MOONLIGHT_LOG_FIELDS(core::LogLevel::Debug, kLogCategory, "configuration loaded",
                         {"path", path});
    return config;
}

Result<Config> loadConfigOrDefault(const IFileSystem& fileSystem, std::string_view path) {
    Result<Config> config = loadConfig(fileSystem, path);
    if (config.isOk() || config.status().code() != ErrorCode::NotFound) {
        return config;
    }
    MOONLIGHT_LOG_FIELDS(core::LogLevel::Info, kLogCategory,
                         "configuration file not found, using defaults", {"path", path});
    return Config{};
}

Status saveConfig(IFileSystem& fileSystem, std::string_view path, const Config& config) {
    if (Status valid = core::validateConfig(config); !valid.isOk()) {
        return withPath(valid, path);
    }
    if (path.empty()) {
        return Status::error(ErrorCode::InvalidArgument, "configuration path is empty");
    }

    const std::string_view parent = core::parentPath(path);
    if (!parent.empty()) {
        if (Status s = fileSystem.createDirectories(parent); !s.isOk()) {
            return s;
        }
    }

    const std::string temporaryPath = std::string(path) + ".tmp";
    if (Status written = writeTextFile(fileSystem, temporaryPath, core::serializeConfig(config));
        !written.isOk()) {
        const Status ignored = fileSystem.removeFile(temporaryPath);  // nettoyage au mieux
        (void)ignored;
        return written;
    }
    if (Status renamed = fileSystem.renameEntry(temporaryPath, path); !renamed.isOk()) {
        const Status ignored = fileSystem.removeFile(temporaryPath);
        (void)ignored;
        return renamed;
    }
    MOONLIGHT_LOG_FIELDS(core::LogLevel::Debug, kLogCategory, "configuration saved",
                         {"path", path});
    return Status::ok();
}

}  // namespace moonlight::platform

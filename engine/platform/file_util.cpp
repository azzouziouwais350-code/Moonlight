#include <cstddef>
#include <string>

#include "platform/filesystem.hpp"

namespace moonlight::platform {

Bytes toBytes(std::string_view text) {
    Bytes bytes(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(text[i]));
    }
    return bytes;
}

core::Result<std::string> readTextFile(const IFileSystem& fileSystem, std::string_view path,
                                       std::uint64_t maxBytes) {
    core::Result<Bytes> bytes = fileSystem.readFile(path, maxBytes);
    if (!bytes.isOk()) {
        return bytes.status();
    }
    std::string text(bytes.value().size(), '\0');
    for (std::size_t i = 0; i < text.size(); ++i) {
        text[i] = static_cast<char>(std::to_integer<unsigned char>(bytes.value()[i]));
    }
    return text;
}

core::Status writeTextFile(IFileSystem& fileSystem, std::string_view path, std::string_view text) {
    const Bytes bytes = toBytes(text);
    return fileSystem.writeFile(path, bytes);
}

}  // namespace moonlight::platform

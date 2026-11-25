#pragma once

// Petits utilitaires texte ASCII independants de la locale (std::tolower,
// isspace... dependent de la locale courante : on les evite volontairement).

#include <cstddef>
#include <string_view>

namespace moonlight::core::text {

constexpr char asciiLower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool isBlank(char c) noexcept { return c == ' ' || c == '\t'; }

/// Caractere de controle ASCII (0x00-0x1F ou 0x7F).
constexpr bool isControl(char c) noexcept {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}

constexpr bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i])) {
            return false;
        }
    }
    return true;
}

/// Retire les espaces et tabulations en debut et fin.
constexpr std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && isBlank(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && isBlank(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace moonlight::core::text

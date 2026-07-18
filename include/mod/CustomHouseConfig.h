#ifndef CUSTOMHOUSECONFIG_H
#define CUSTOMHOUSECONFIG_H

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>

namespace CustomHouseConfig {

/**
 * Parse a complete base-10 integer without throwing.
 * The destination is left unchanged when parsing fails.
 */
inline bool parseInteger(const std::string& text, int& destination) noexcept {
    if(text.empty()) {
        return false;
    }

    int parsedValue = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsedValue, 10);
    if(result.ec != std::errc{} || result.ptr != end) {
        return false;
    }

    destination = parsedValue;
    return true;
}

/**
 * Parse a complete finite floating-point value without throwing.
 * The destination is left unchanged when parsing fails.
 */
inline bool parseDouble(const std::string& text, double& destination) noexcept {
    if(text.empty()) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    const double parsedValue = std::strtod(text.c_str(), &end);
    if(errno == ERANGE || end != text.c_str() + text.size() || !std::isfinite(parsedValue)) {
        return false;
    }

    destination = parsedValue;
    return true;
}

/**
 * Restrict optional presentation assets to portable paths inside the active
 * mod search path. Empty paths are valid and request the normal fallback.
 */
inline bool isSafeAssetPath(const std::string& path) {
    if(path.empty()) {
        return true;
    }
    if(path.front() == '/' || path.front() == '\\' || path.find('\\') != std::string::npos
       || path.find(':') != std::string::npos) {
        return false;
    }

    std::size_t segmentStart = 0;
    while(segmentStart <= path.size()) {
        const std::size_t separator = path.find('/', segmentStart);
        const std::size_t segmentEnd = separator == std::string::npos ? path.size() : separator;
        const std::string segment = path.substr(segmentStart, segmentEnd - segmentStart);
        if(segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if(separator == std::string::npos) {
            break;
        }
        segmentStart = separator + 1;
    }
    return true;
}

inline bool isValidVoicePlaybackRate(double value) noexcept {
    return std::isfinite(value) && value >= 0.25 && value <= 4.0;
}

inline bool isValidVoiceGain(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 4.0;
}

} // namespace CustomHouseConfig

#endif // CUSTOMHOUSECONFIG_H

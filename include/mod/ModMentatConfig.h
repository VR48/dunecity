#ifndef MODMENTATCONFIG_H
#define MODMENTATCONFIG_H

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

struct ModMentatInfo {
    bool enabled = false;
    int identityHouse = -1;
    std::string backgroundAsset;
    std::string eyesAsset;
    std::string mouthAsset;
    int eyesFrames = 5;
    double eyesFrameRate = 0.5;
    bool doubleEyes = true;
    int eyesTransparentColor = -1;
    int eyesX = -1;
    int eyesY = -1;
    int mouthFrames = 5;
    double mouthFrameRate = 5.0;
    bool doubleMouth = true;
    int mouthTransparentColor = -1;
    int mouthX = -1;
    int mouthY = -1;
    bool useBaseExtras = false;
};

namespace ModMentatConfig {

inline std::string lowercaseAscii(std::string value) {
    for(char& character : value) {
        if(character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

inline bool parseBoolean(const std::string& text, bool& destination) {
    const std::string normalized = lowercaseAscii(text);
    if(normalized == "true" || normalized == "yes" || normalized == "1") {
        destination = true;
        return true;
    }
    if(normalized == "false" || normalized == "no" || normalized == "0") {
        destination = false;
        return true;
    }
    return false;
}

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

inline bool isValid(const ModMentatInfo& info) {
    const auto validCoordinate = [](int value) { return value == -1 || (value >= 0 && value <= 4096); };
    const auto validColorKey = [](int value) { return value >= -1 && value <= 255; };

    return info.identityHouse >= -1 && info.identityHouse < 8
        && isSafeAssetPath(info.backgroundAsset)
        && isSafeAssetPath(info.eyesAsset)
        && isSafeAssetPath(info.mouthAsset)
        && info.eyesFrames >= 1 && info.eyesFrames <= 64
        && info.mouthFrames >= 1 && info.mouthFrames <= 64
        && info.eyesFrameRate > 0.0 && info.eyesFrameRate <= 100.0
        && info.mouthFrameRate > 0.0 && info.mouthFrameRate <= 100.0
        && validColorKey(info.eyesTransparentColor)
        && validColorKey(info.mouthTransparentColor)
        && validCoordinate(info.eyesX)
        && validCoordinate(info.eyesY)
        && validCoordinate(info.mouthX)
        && validCoordinate(info.mouthY);
}

} // namespace ModMentatConfig

#endif // MODMENTATCONFIG_H
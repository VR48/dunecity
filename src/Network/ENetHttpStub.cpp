/*
 *  Browser stub for the curl-backed ENetHttp.cpp, which cannot build under
 *  Emscripten (libcurl has no port). Provides exactly the ENetHttp symbols
 *  referenced by MetaServerClient/VersionChecker (loadFromHttp) and
 *  Dune2RAssetManager (downloadHttpFile).
 */

#include <Network/ENetHttp.h>

#include <stdexcept>

namespace {

[[noreturn]] void throwWebHttpUnsupported() {
    throw std::runtime_error("HTTP downloads are not available in the browser build");
}

} // namespace

std::string loadFromHttp(const std::string& url, const std::map<std::string, std::string>& parameters) {
    (void) url;
    (void) parameters;
    throwWebHttpUnsupported();
}

void downloadHttpFile(const std::string& url, const std::string& filename,
                      const std::function<bool(uint64_t, uint64_t)>& progress) {
    (void) url;
    (void) filename;
    (void) progress;
    throwWebHttpUnsupported();
}

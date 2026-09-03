/*
 *  Browser stub for HTTP helpers used by desktop builds (MetaServer,
 *  VersionChecker, Dune2R asset delivery). The Emscripten target excludes
 *  the curl-backed ENetHttp.cpp implementation.
 */

#include <Network/ENetHttp.h>

#include <stdexcept>

namespace {

[[noreturn]] void throwWebHttpUnsupported() {
    throw std::runtime_error("HTTP downloads are not available in the browser build");
}

} // namespace

std::string getDomainFromURL(const std::string& url) {
    (void) url;
    return {};
}

std::string getFilePathFromURL(const std::string& url) {
    (void) url;
    return {};
}

int getPortFromURL(const std::string& url) {
    (void) url;
    return PORT_HTTP;
}

std::string percentEncode(const std::string& s) {
    return s;
}

std::string loadFromHttp(const std::string& url, const std::map<std::string, std::string>& parameters) {
    (void) url;
    (void) parameters;
    throwWebHttpUnsupported();
}

std::string loadFromHttp(const std::string& domain, const std::string& filepath, unsigned short port) {
    (void) domain;
    (void) filepath;
    (void) port;
    throwWebHttpUnsupported();
}

void downloadHttpFile(const std::string& url, const std::string& filename,
                      const std::function<bool(uint64_t, uint64_t)>& progress) {
    (void) url;
    (void) filename;
    (void) progress;
    throwWebHttpUnsupported();
}

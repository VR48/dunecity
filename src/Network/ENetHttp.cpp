/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Network/ENetHttp.h>

#include <Network/ENetHelper.h>

#include <misc/exceptions.h>
#include <misc/FileSystem.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <curl/curl.h>
#endif
#include <enet/enet.h>

#ifdef __ANDROID__
#include <SDL_system.h>
#include <unistd.h>
#endif

#ifndef __EMSCRIPTEN__
namespace {

const char* getBundledCertificateBundle() {
    static const std::string certificateBundle = [] {
        const std::filesystem::path dataRoot = getDuneLegacyDataDir();
        const std::filesystem::path candidates[] = {
            dataRoot / "data" / "cacert.pem",
            dataRoot / "cacert.pem",
            dataRoot / ".." / "share" / "DuneCity" / "cacert.pem"
        };

        for(const auto& candidate : candidates) {
            std::error_code error;
            if(std::filesystem::is_regular_file(candidate, error)) {
                return candidate.lexically_normal().string();
            }
        }
        return std::string{};
    }();
    return certificateBundle.empty() ? nullptr : certificateBundle.c_str();
}

#ifdef __ANDROID__
const char* getAndroidCertificateBundle() {
    const char* storagePath = SDL_AndroidGetExternalStoragePath();
    if(storagePath == nullptr || storagePath[0] == '\0') {
        storagePath = SDL_AndroidGetInternalStoragePath();
    }
    if(storagePath == nullptr || storagePath[0] == '\0') {
        return nullptr;
    }

    static std::string certificateBundle;
    certificateBundle = std::string(storagePath) + "/data/cacert.pem";
    return access(certificateBundle.c_str(), R_OK) == 0
               ? certificateBundle.c_str()
               : nullptr;
}

const char* getAndroidCertificatePath() {
    constexpr std::array<const char*, 2> certificatePaths = {
        "/apex/com.android.conscrypt/cacerts",
        "/system/etc/security/cacerts"
    };

    for(const char* path : certificatePaths) {
        if(access(path, R_OK) == 0) {
            return path;
        }
    }

    return nullptr;
}
#endif

void configureCurlCertificates(CURL* curl) {
#if defined(_WIN32) && defined(CURLSSLOPT_NATIVE_CA)
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif

    if(const char* certificateBundle = getBundledCertificateBundle()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, certificateBundle);
    }

#ifdef __ANDROID__
    if(const char* certificateBundle = getAndroidCertificateBundle()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, certificateBundle);
    } else if(const char* certificatePath = getAndroidCertificatePath()) {
        curl_easy_setopt(curl, CURLOPT_CAPATH, certificatePath);
    }
#endif
}

struct FileDownloadContext {
    std::filesystem::path filename;
    std::FILE* file = nullptr;
    curl_off_t resumeOffset = 0;
    bool responseModeKnown = false;
    bool cancelled = false;
    std::function<bool(uint64_t, uint64_t)> progress;

    ~FileDownloadContext() {
        if(file != nullptr) {
            std::fclose(file);
        }
    }
};

size_t curlDownloadHeaderCallback(char* buffer, size_t size, size_t count, void* userdata) {
    const size_t bytes = size * count;
    auto& context = *static_cast<FileDownloadContext*>(userdata);
    const std::string header(buffer, bytes);
    if(header.rfind("HTTP/", 0) == 0) {
        const size_t statusStart = header.find(' ');
        const int status = statusStart == std::string::npos
                               ? 0
                               : std::atoi(header.c_str() + statusStart + 1);
        if(status == 200 || status == 206) {
            if(context.file != nullptr) {
                std::fclose(context.file);
                context.file = nullptr;
            }
            if(status == 200) {
                context.resumeOffset = 0;
            }
            const char* mode = status == 206 && context.resumeOffset > 0 ? "ab" : "wb";
            context.file = std::fopen(context.filename.string().c_str(), mode);
            context.responseModeKnown = context.file != nullptr;
        }
    }
    return bytes;
}

size_t curlDownloadWriteCallback(void* contents, size_t size, size_t count, void* userdata) {
    auto& context = *static_cast<FileDownloadContext*>(userdata);
    if(!context.responseModeKnown || context.file == nullptr) {
        return 0;
    }
    return std::fwrite(contents, size, count, context.file) * size;
}

int curlDownloadProgressCallback(void* userdata, curl_off_t downloadTotal,
                                 curl_off_t downloaded, curl_off_t, curl_off_t) {
    auto& context = *static_cast<FileDownloadContext*>(userdata);
    if(!context.progress) {
        return 0;
    }
    const uint64_t current = static_cast<uint64_t>(std::max<curl_off_t>(0, downloaded))
                             + static_cast<uint64_t>(std::max<curl_off_t>(0, context.resumeOffset));
    const uint64_t total = static_cast<uint64_t>(std::max<curl_off_t>(0, downloadTotal))
                           + static_cast<uint64_t>(std::max<curl_off_t>(0, context.resumeOffset));
    context.cancelled = !context.progress(current, total);
    return context.cancelled ? 1 : 0;
}

} // namespace
#endif

std::string getDomainFromURL(const std::string& url) {
    size_t domainStart = 0;

    if(url.substr(0,7) == "http://") {
        domainStart += 7;
    } else if(url.substr(0,8) == "https://") {
        domainStart += 8;
    }

    size_t domainEnd = url.find_first_of(":/", domainStart);

    return url.substr(domainStart, domainEnd-domainStart);
}

std::string getFilePathFromURL(const std::string& url) {
    size_t domainStart = 0;

    if(url.substr(0,7) == "http://") {
        domainStart += 7;
    } else if(url.substr(0,8) == "https://") {
        domainStart += 8;
    }

    size_t domainEnd = url.find_first_of('/', domainStart);

    return url.substr(domainEnd, std::string::npos);
}

int getPortFromURL(const std::string& url) {
    size_t domainStart = 0;

    if(url.substr(0,7) == "http://") {
        domainStart += 7;
    } else if(url.substr(0,8) == "https://") {
        domainStart += 8;
    }

    size_t domainEnd = url.find_first_of(":/", domainStart);

    if(domainEnd == std::string::npos) {
        return 0;
    }

    if(url.at(domainEnd) == ':') {
        int port = strtol(&url[domainEnd+1], nullptr, 10);

        if(port <= 0) {
            return -1;
        }

        return port;
    } else {
        return 0;
    }
}

std::string percentEncode(const std::string & s) {
    const std::string unreservedCharacters = "-.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_~"; // see RFC 3986

    std::string result;
    for(char c : s) {
        if(unreservedCharacters.find_first_of(c) == std::string::npos) {
            // percent encode
            result += fmt::sprintf("%%%.2X", (unsigned char) c);
        } else {
            // copy unmodifed
            result += c;
        }
    }

    return result;
}


#ifndef __EMSCRIPTEN__
// Callback function for libcurl to write data
static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}
#endif

std::string loadFromHttp(const std::string& url, const std::map<std::string, std::string>& parameters) {
    // Build URL with parameters
    std::string fullUrl = url;
    
    for(const auto& param : parameters) {
        if(fullUrl.find_first_of('?') == std::string::npos) {
            // first parameter
            fullUrl += "?";
        } else {
            fullUrl += "&";
        }
        fullUrl += percentEncode(param.first) + "=" + percentEncode(param.second);
    }
    
#ifdef __EMSCRIPTEN__
    void* responseBuffer = nullptr;
    int responseSize = 0;
    int requestError = 0;
    emscripten_wget_data(fullUrl.c_str(), &responseBuffer, &responseSize, &requestError);
    if(requestError != 0 || responseBuffer == nullptr) {
        std::free(responseBuffer);
        THROW(std::runtime_error, "Browser HTTP request failed");
    }

    const std::string responseData(static_cast<const char*>(responseBuffer),
                                   static_cast<size_t>(responseSize));
    std::free(responseBuffer);
    return responseData;
#else
    // Initialize curl
    CURL* curl = curl_easy_init();
    if(!curl) {
        THROW(std::runtime_error, "Failed to initialize libcurl");
    }
    
    std::string responseData;
    std::array<char, CURL_ERROR_SIZE> errorBuffer{};
    
    // Set curl options
    curl_easy_setopt(curl, CURLOPT_URL, fullUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer.data());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); // Follow redirects (HTTP -> HTTPS)
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L); // Max 5 redirects
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // 30 second timeout
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DuneLegacy/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L); // Verify SSL certificates
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L); // Verify hostname

    configureCurlCertificates(curl);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    if(res != CURLE_OK) {
        std::string error = errorBuffer[0] != '\0'
                                ? errorBuffer.data()
                                : curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        THROW(std::runtime_error, "HTTP request failed: " + error);
    }
    
    // Check HTTP response code
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    curl_easy_cleanup(curl);
    
    if(httpCode != 200) {
        THROW(std::runtime_error, "Server Error: Received HTTP status code " + std::to_string(httpCode));
    }
    
    return responseData;
#endif
}

std::string loadFromHttp(const std::string& domain, const std::string& filepath, unsigned short port) {
    // Build URL from components
    std::string url;
    
    if(port == 443) {
        url = "https://";
    } else {
        url = "http://";
    }
    
    url += domain;
    
    if((port != 80 && port != 443) || port == 0) {
        url += ":" + std::to_string(port);
    }
    
    url += filepath;
    
    // Use the URL-based version
    return loadFromHttp(url, std::map<std::string, std::string>());
}

void downloadHttpFile(const std::string& url, const std::string& filename,
                      const std::function<bool(uint64_t, uint64_t)>& progress) {
    const std::filesystem::path output(filename);
    std::error_code error;
    if(!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), error);
        if(error) {
            THROW(std::runtime_error, "Could not create download directory: " + error.message());
        }
    }

#ifdef __EMSCRIPTEN__
    if(progress && !progress(0, 0)) {
        THROW(std::runtime_error, "Download cancelled");
    }
    if(emscripten_wget(url.c_str(), filename.c_str()) != 0) {
        THROW(std::runtime_error, "Browser HTTP download failed");
    }
    uint64_t size = 0;
    if(std::filesystem::is_regular_file(output, error)) {
        size = std::filesystem::file_size(output, error);
    }
    if(progress && !progress(size, size)) {
        THROW(std::runtime_error, "Download cancelled");
    }
#else
    FileDownloadContext context;
    context.filename = output;
    context.progress = progress;
    if(std::filesystem::is_regular_file(output, error)) {
        context.resumeOffset = static_cast<curl_off_t>(std::filesystem::file_size(output, error));
        if(error) {
            context.resumeOffset = 0;
        }
    }

    CURL* curl = curl_easy_init();
    if(curl == nullptr) {
        THROW(std::runtime_error, "Failed to initialize libcurl");
    }
    std::array<char, CURL_ERROR_SIZE> errorBuffer{};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer.data());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DuneCity-Dune2R/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    // Do not feed an HTTPS proxy's "200 Connection established" response
    // into the resume-mode header callback; only origin response headers
    // decide whether the output file is appended or restarted.
    curl_easy_setopt(curl, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlDownloadHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &context);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlDownloadWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlDownloadProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    if(context.resumeOffset > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, context.resumeOffset);
    }
    configureCurlCertificates(curl);

    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if(context.file != nullptr) {
        std::fclose(context.file);
        context.file = nullptr;
    }
    if(result != CURLE_OK) {
        if(context.cancelled) {
            THROW(std::runtime_error, "Download cancelled");
        }
        const std::string message = errorBuffer[0] != '\0'
                                        ? errorBuffer.data()
                                        : curl_easy_strerror(result);
        THROW(std::runtime_error, "HTTP download failed: " + message);
    }
    if(status != 200 && status != 206) {
        THROW(std::runtime_error, "Server Error: Received HTTP status code " + std::to_string(status));
    }
#endif
}



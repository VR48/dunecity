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

#include <algorithm>
#include <array>
#include <stdio.h>
#include <curl/curl.h>
#include <enet/enet.h>

#ifdef __ANDROID__
#include <SDL_system.h>
#include <unistd.h>
#endif

namespace {

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

} // namespace

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


// Callback function for libcurl to write data
static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

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

#ifdef __ANDROID__
    // Android's trust store uses legacy hash names that OpenSSL 3 may not
    // resolve through CURLOPT_CAPATH. Prefer the packaged Mozilla CA bundle
    // and retain the platform directory as a fallback.
    if(const char* certificateBundle = getAndroidCertificateBundle()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, certificateBundle);
    } else if(const char* certificatePath = getAndroidCertificatePath()) {
        curl_easy_setopt(curl, CURLOPT_CAPATH, certificatePath);
    }
#endif
    
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



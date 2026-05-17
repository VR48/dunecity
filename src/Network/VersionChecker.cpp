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

#include <Network/VersionChecker.h>
#include <Network/ENetHttp.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <config.h>

#include <sstream>
#include <map>
#include <vector>

VersionChecker::VersionChecker(const std::string& metaServerURL)
 : metaServerURL(metaServerURL) {
    mutex = SDL_CreateMutex();
    if(mutex == nullptr) {
        THROW(std::runtime_error, "VersionChecker: Unable to create mutex");
    }
}

VersionChecker::~VersionChecker() {
    if(checkThread != nullptr) {
        SDL_WaitThread(checkThread, nullptr);
    }
    if(mutex != nullptr) {
        SDL_DestroyMutex(mutex);
    }
}

void VersionChecker::checkForUpdates() {
    if(bChecking) {
        return; // Already checking
    }

    SDL_LockMutex(mutex);
    bChecking = true;
    bCheckComplete = false;
    SDL_UnlockMutex(mutex);

    checkThread = SDL_CreateThread(checkThreadMain, "VersionCheck", (void*)this);
    if(checkThread == nullptr) {
        SDL_LockMutex(mutex);
        bChecking = false;
        SDL_UnlockMutex(mutex);
        SDL_Log("VersionChecker: Failed to create check thread");
    }
}

void VersionChecker::update() {
    if(!bChecking) {
        return;
    }

    SDL_LockMutex(mutex);
    bool complete = bCheckComplete;
    VersionInfo info = versionInfo;
    if(complete) {
        bChecking = false;
        bCheckComplete = false;
    }
    SDL_UnlockMutex(mutex);

    if(complete && onVersionCheckComplete) {
        onVersionCheckComplete(info);
    }
}

int VersionChecker::compareVersions(const std::string& v1, const std::string& v2) {
    std::vector<int> parts1, parts2;

    // Parse v1
    std::stringstream ss1(v1);
    std::string segment;
    while(std::getline(ss1, segment, '.')) {
        try {
            parts1.push_back(std::stoi(segment));
        } catch(...) {
            parts1.push_back(0);
        }
    }

    // Parse v2
    std::stringstream ss2(v2);
    while(std::getline(ss2, segment, '.')) {
        try {
            parts2.push_back(std::stoi(segment));
        } catch(...) {
            parts2.push_back(0);
        }
    }

    // Compare component by component
    size_t maxLen = std::max(parts1.size(), parts2.size());
    for(size_t i = 0; i < maxLen; ++i) {
        int p1 = (i < parts1.size()) ? parts1[i] : 0;
        int p2 = (i < parts2.size()) ? parts2[i] : 0;

        if(p1 > p2) return 1;
        if(p1 < p2) return -1;
    }

    return 0; // Equal
}

int VersionChecker::checkThreadMain(void* data) {
    VersionChecker* pChecker = static_cast<VersionChecker*>(data);

    VersionInfo info;
    info.updateAvailable = false;
    info.latestVersion = VERSION;
    info.downloadURL = "https://github.com/svan058/dunecity/releases/latest";

    // Dune City's update channel is GitHub Releases on the project repo.
    // We query api.github.com/repos/.../releases/latest and parse the
    // tag_name field (expected form: "v1.0.0"). Stripping the leading
    // 'v' gives a SemVer string we can compare against the build's
    // VERSION macro. No auth needed for public repos.
    //
    // The old upstream-Dune-Legacy metaServer call was removed: it
    // would forever respond with "Latest=0.99.5" which is upstream's
    // last release, not a Dune City update.
    const std::string api =
        "https://api.github.com/repos/svan058/dunecity/releases/latest";

    try {
        std::string response = loadFromHttp(api);

        // GitHub returns JSON; we only need tag_name. A full JSON parser
        // would be overkill — find the field by substring. Format:
        //   "tag_name":"v1.0.0"
        const std::string tagKey = "\"tag_name\":\"";
        const size_t tagPos = response.find(tagKey);
        if (tagPos != std::string::npos) {
            const size_t start = tagPos + tagKey.size();
            const size_t end   = response.find('"', start);
            if (end != std::string::npos) {
                std::string tag = response.substr(start, end - start);
                // Strip leading 'v' if present (v1.0.0 → 1.0.0).
                if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
                    tag.erase(0, 1);
                }
                info.latestVersion   = tag;
                info.updateAvailable = (compareVersions(tag, VERSION) > 0);

                SDL_Log("VersionChecker: Current=%s, Latest=%s, UpdateAvailable=%s",
                        VERSION, info.latestVersion.c_str(),
                        info.updateAvailable ? "yes" : "no");
            }
        } else {
            // No tag_name in the response — most likely there are no
            // releases yet (404 path turned into an empty body, or the
            // repo really has nothing tagged). Treat as "no update".
            SDL_Log("VersionChecker: no releases found on GitHub yet");
        }

    } catch(std::exception& e) {
        SDL_Log("VersionChecker: Error checking for updates: %s", e.what());
    }

    // Store result
    SDL_LockMutex(pChecker->mutex);
    pChecker->versionInfo = info;
    pChecker->bCheckComplete = true;
    SDL_UnlockMutex(pChecker->mutex);

    return 0;
}


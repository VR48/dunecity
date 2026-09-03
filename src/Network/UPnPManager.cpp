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

#include <Network/UPnPManager.h>
#include <SDL2/SDL_log.h>

#ifdef __EMSCRIPTEN__

struct UPnPManager::UPnPData {};

UPnPManager::UPnPManager() : data(new UPnPData()) {}
UPnPManager::~UPnPManager() { delete data; }

bool UPnPManager::discover(int) {
    status = Status::Unavailable;
    lastError = "UPnP is unavailable in a web browser";
    return false;
}

bool UPnPManager::addPortMapping(uint16_t, uint16_t, const std::string&,
                                 const std::string&, int) {
    status = Status::Unavailable;
    lastError = "UPnP is unavailable in a web browser";
    return false;
}

bool UPnPManager::removePortMapping(uint16_t, const std::string&) { return false; }
std::string UPnPManager::getExternalIPAddress() { return {}; }
std::string UPnPManager::getStatusString() const { return "Unavailable in browser"; }

#else

#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>

#include <cstring>

struct UPnPManager::UPnPData {
    UPNPUrls urls;
    IGDdatas igdData;
    char lanAddress[64] = {0};
    char externalAddress[64] = {0};
    bool initialized = false;
};

UPnPManager::UPnPManager() {
    data = new UPnPData();
}

UPnPManager::~UPnPManager() {
    // Remove any active port mapping
    if (status == Status::PortMapped && mappedPort != 0) {
        removePortMapping(mappedPort, mappedProtocol);
    }
    
    // Clean up UPnP resources
    if (data && data->initialized) {
        FreeUPNPUrls(&data->urls);
    }
    
    delete data;
}

bool UPnPManager::discover(int timeoutMs) {
    status = Status::Discovering;
    SDL_Log("UPnP: Discovering devices...");
    
    int error = 0;
    
    // Discover UPnP devices
    UPNPDev* deviceList = upnpDiscover(timeoutMs, nullptr, nullptr, 0, 0, 2, &error);
    
    if (deviceList == nullptr) {
        lastError = "No UPnP devices found";
        status = Status::Unavailable;
        SDL_Log("UPnP: %s (error %d)", lastError.c_str(), error);
        return false;
    }
    
    // Find a valid IGD (Internet Gateway Device)
    char wanAddress[64] = {0};
    int result = UPNP_GetValidIGD(deviceList, &data->urls, &data->igdData, 
                                   data->lanAddress, sizeof(data->lanAddress),
                                   wanAddress, sizeof(wanAddress));
    
    freeUPNPDevlist(deviceList);
    
    // Only result == 1 means a valid, connected IGD
    // 2 = valid IGD but not connected, 3 = not an IGD
    if (result != 1) {
        lastError = result == 0 ? "No UPnP device found" : 
                    result == 2 ? "IGD found but not connected" :
                    "Device found but not an IGD";
        status = Status::Unavailable;
        SDL_Log("UPnP: %s (result=%d)", lastError.c_str(), result);
        return false;
    }
    
    data->initialized = true;
    
    // Get external IP address
    int ipResult = UPNP_GetExternalIPAddress(data->urls.controlURL, 
                                              data->igdData.first.servicetype,
                                              data->externalAddress);
    
    if (ipResult != UPNPCOMMAND_SUCCESS) {
        SDL_Log("UPnP: Could not get external IP address");
    }
    
    status = Status::Available;
    
    SDL_Log("UPnP: Found IGD at %s", data->urls.controlURL);
    SDL_Log("UPnP: Local IP: %s, External IP: %s", data->lanAddress, data->externalAddress);
    
    return true;
}

bool UPnPManager::addPortMapping(uint16_t internalPort, uint16_t externalPort,
                                  const std::string& protocol,
                                  const std::string& description,
                                  int leaseDuration) {
    if (!data->initialized) {
        lastError = "UPnP not initialized - call discover() first";
        SDL_Log("UPnP: %s", lastError.c_str());
        return false;
    }
    
    if (externalPort == 0) {
        externalPort = internalPort;
    }
    
    char internalPortStr[16];
    char externalPortStr[16];
    char leaseStr[16];
    
    snprintf(internalPortStr, sizeof(internalPortStr), "%u", internalPort);
    snprintf(externalPortStr, sizeof(externalPortStr), "%u", externalPort);
    snprintf(leaseStr, sizeof(leaseStr), "%d", leaseDuration);
    
    SDL_Log("UPnP: Adding port mapping %s:%s -> %s (%s)", 
            data->externalAddress, externalPortStr, internalPortStr, protocol.c_str());
    
    int result = UPNP_AddPortMapping(data->urls.controlURL,
                                      data->igdData.first.servicetype,
                                      externalPortStr,
                                      internalPortStr,
                                      data->lanAddress,
                                      description.c_str(),
                                      protocol.c_str(),
                                      nullptr,  // Remote host (nullptr = any)
                                      leaseStr);
    
    if (result != UPNPCOMMAND_SUCCESS) {
        lastError = std::string("Failed to add port mapping: ") + strupnperror(result);
        status = Status::Error;
        SDL_Log("UPnP: %s", lastError.c_str());
        return false;
    }
    
    mappedPort = externalPort;
    mappedProtocol = protocol;
    status = Status::PortMapped;
    
    SDL_Log("UPnP: Port mapping added successfully!");
    return true;
}

bool UPnPManager::removePortMapping(uint16_t externalPort, const std::string& protocol) {
    if (!data->initialized) {
        return false;
    }
    
    char externalPortStr[16];
    snprintf(externalPortStr, sizeof(externalPortStr), "%u", externalPort);
    
    SDL_Log("UPnP: Removing port mapping %s (%s)", externalPortStr, protocol.c_str());
    
    int result = UPNP_DeletePortMapping(data->urls.controlURL,
                                         data->igdData.first.servicetype,
                                         externalPortStr,
                                         protocol.c_str(),
                                         nullptr);  // Remote host
    
    if (result != UPNPCOMMAND_SUCCESS) {
        lastError = std::string("Failed to remove port mapping: ") + strupnperror(result);
        SDL_Log("UPnP: %s", lastError.c_str());
        return false;
    }
    
    if (mappedPort == externalPort) {
        mappedPort = 0;
        mappedProtocol.clear();
        status = Status::Available;
    }
    
    SDL_Log("UPnP: Port mapping removed successfully!");
    return true;
}

std::string UPnPManager::getExternalIPAddress() {
    if (data->initialized && data->externalAddress[0] != '\0') {
        return std::string(data->externalAddress);
    }
    return "";
}

std::string UPnPManager::getStatusString() const {
    switch (status) {
        case Status::NotInitialized: return "Not Initialized";
        case Status::Discovering: return "Discovering...";
        case Status::Available: return "Available";
        case Status::Unavailable: return "Unavailable";
        case Status::PortMapped: return "Port Mapped";
        case Status::Error: return "Error";
    }
    return "Unknown";
}

#endif


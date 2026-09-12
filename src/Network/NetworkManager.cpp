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

#include <Network/NetworkManager.h>

#include <config.h>

#include <Network/ENetHelper.h>
#include <Network/StunClient.h>

#include <GameInitSettings.h>
#include <Network/GameInitSettingsPolicy.h>

#include <misc/exceptions.h>
#include <misc/FileSystem.h>
#include <misc/fnkdat.h>

#include <mod/ModManager.h>
#include <mod/ModTransferValidation.h>

#include <globals.h>
#include <players/QuantBotConfig.h>

#include <stdio.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>

NetworkManager::NetworkManager(int port, const std::string& metaserver) {

    if(enet_initialize() != 0) {
        THROW(std::runtime_error, "NetworkManager: An error occurred while initializing ENet.");
    }

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    host = enet_host_create(&address, 32, 2, 0, 0);
    if(host == nullptr) {
        enet_deinitialize();
        THROW(std::runtime_error, "NetworkManager: An error occurred while trying to create a server host.");
    }

    if(enet_host_compress_with_range_coder(host) < 0) {
        enet_deinitialize();
        THROW(std::runtime_error, "NetworkManager: Cannot activate range coder.");
    }

    // Bound what ENet itself will allocate for an inbound peer before we ever see a packet.
    // maximumPacketSize is checked against the announced fragment total *before* the
    // reassembly buffer is allocated (src/enet/protocol.c), and maximumWaitingData bounds the
    // data one peer may have queued. Both defaults are 32 MiB, far above anything this
    // protocol sends: the largest legitimate packet is a map inside SENDGAMEINFO and the
    // largest legitimate burst is a 10 MiB mod transfer in 64 KiB chunks.
    host->maximumPacketSize = MAX_ENET_PACKET_SIZE;
    host->maximumWaitingData = MAX_ENET_WAITING_DATA;

    try {
        pLANGameFinderAndAnnouncer = std::make_unique<LANGameFinderAndAnnouncer>();
        pMetaServerClient = std::make_unique<MetaServerClient>(metaserver);
        pUPnPManager = std::make_unique<UPnPManager>();
        // UPnP discovery is deferred to startServer() to avoid startup delay
        // when just joining games or playing offline
    } catch (...) {
        enet_deinitialize();
        throw;
    }
}


NetworkManager::~NetworkManager() {
    // Remove UPnP port mapping if active
    // Note: We attempt removal even if previous attempts failed, and log but don't block on failure
    if (upnpMappedPort != 0 && pUPnPManager) {
        if (pUPnPManager->removePortMapping(upnpMappedPort, "UDP")) {
            SDL_Log("NetworkManager: UPnP port mapping removed on shutdown");
        } else {
            SDL_Log("NetworkManager: Warning - failed to remove UPnP port mapping on shutdown");
        }
        upnpPortMapped = false;
        upnpMappedPort = 0;
    }
    
    pUPnPManager.reset();
    pMetaServerClient.reset();
    pLANGameFinderAndAnnouncer.reset();
    enet_host_destroy(host);
    enet_deinitialize();
}

void NetworkManager::startServer(bool bLANServer, const std::string& serverName, const std::string& playerName, GameInitSettings* pGameInitSettings, int numPlayers, int maxPlayers) {
    // Reset game-in-progress flag for new game. The same NetworkManager instance is reused
    // when a player returns from a match and hosts another one, so the previous match's phase
    // and seed must not leak into this session.
    bGameInProgress = false;
    simulationSeed = 0;


    if(bLANServer == true) {
        if(pLANGameFinderAndAnnouncer != nullptr) {
            pLANGameFinderAndAnnouncer->startAnnounce(serverName, host->address.port, pGameInitSettings->getFilename(), numPlayers, maxPlayers);
        }
    } else {
        // Internet game - try UPnP port mapping
        if (pUPnPManager && !upnpPortMapped) {
            // Discover UPnP devices if not already done (deferred from constructor)
            // Only attempt discovery once - don't retry if it failed before
            if (!pUPnPManager->wasDiscoveryAttempted()) {
                SDL_Log("NetworkManager: Discovering UPnP devices...");
                if (pUPnPManager->discover(2000)) {
                    SDL_Log("NetworkManager: UPnP available - automatic port forwarding enabled");
                } else {
                    SDL_Log("NetworkManager: UPnP not available - manual port forwarding may be required");
                }
            }
            
            // Try to add port mapping if UPnP is available
            // Use 1 hour lease (3600s) instead of permanent - will be renewed if game runs longer
            if (pUPnPManager->isAvailable()) {
                if (pUPnPManager->addPortMapping(host->address.port, host->address.port, "UDP", "Dune City", UPNP_LEASE_DURATION)) {
                    upnpPortMapped = true;
                    upnpMappedPort = host->address.port;
                    upnpLeaseStartTime = SDL_GetTicks();
                    SDL_Log("NetworkManager: UPnP port %d mapped successfully (%d min lease, auto-renews)", 
                            host->address.port, UPNP_LEASE_DURATION / 60);
                } else {
                    SDL_Log("NetworkManager: UPnP port mapping failed - manual port forwarding may be required");
                }
            }
        }
        
        if(pMetaServerClient != nullptr) {
            // Get active mod info
            ModInfo activeModInfo = ModManager::instance().getModInfo(ModManager::instance().getActiveModName());
            
            // NAT traversal: Perform STUN query to discover external IP:port
            // SAFETY: STUN only runs here because peerList is empty (pre-connection)
            uint16_t stunPort = 0;
            if (host != nullptr && host->socket != ENET_SOCKET_NULL && peerList.empty()) {
                SDL_Log("NetworkManager: Performing STUN query for NAT traversal...");
                StunClient::StunResult stunResult = StunClient::performStunQuery(host->socket);
                if (stunResult.success) {
                    stunPort = stunResult.externalPort;
                    SDL_Log("NetworkManager: STUN discovered external address %s:%d", 
                            stunResult.externalIP.c_str(), stunResult.externalPort);
                } else {
                    SDL_Log("NetworkManager: STUN query failed: %s (will announce without STUN port)", 
                            stunResult.errorMessage.c_str());
                }
            }
            
            pMetaServerClient->startAnnounce(serverName, host->address.port, pGameInitSettings->getFilename(), numPlayers, maxPlayers,
                                             activeModInfo.name, activeModInfo.version, stunPort);
        }
    }

    bIsServer = true;
    this->bLANServer = bLANServer;
    this->numPlayers = numPlayers;
    this->maxPlayers = maxPlayers;
    pendingCoopMission.reset();
    this->playerName = playerName;
    this->pGameInitSettings = pGameInitSettings;
}

void NetworkManager::updateServer(int numPlayers) {
    if(bLANServer == true) {
        if(pLANGameFinderAndAnnouncer != nullptr) {
            pLANGameFinderAndAnnouncer->updateAnnounce(numPlayers);
        }
    } else {
        if(pMetaServerClient != nullptr) {
            pMetaServerClient->updateAnnounce(numPlayers);
        }
    }

    this->numPlayers = numPlayers;
}

void NetworkManager::stopAnnouncing() {
    // Stop announcing the game in the lobby/server list
    // This is called when the game starts, but the server should remain active
    if(bLANServer == true) {
        if(pLANGameFinderAndAnnouncer != nullptr) {
            pLANGameFinderAndAnnouncer->stopAnnounce();
        }
    } else {
        if(pMetaServerClient != nullptr) {
            pMetaServerClient->stopAnnounce();
        }
    }
    // NOTE: bIsServer remains TRUE so the host can continue managing the game
    
    // Mark game as in progress - this disables lobby-only features like NAT hole punch polling
    // (which uses blocking HTTP calls that would cause major stutter during gameplay)
    bGameInProgress = true;
    SDL_Log("NetworkManager: Game in progress - lobby features disabled");
}

void NetworkManager::stopServer() {
    stopAnnouncing();
    
    // Remove UPnP port mapping if active
    if (upnpPortMapped && pUPnPManager && upnpMappedPort != 0) {
        if (pUPnPManager->removePortMapping(upnpMappedPort, "UDP")) {
            upnpPortMapped = false;
            upnpMappedPort = 0;
            upnpLeaseStartTime = 0;
            SDL_Log("NetworkManager: UPnP port mapping removed");
        } else {
            // Keep upnpPortMapped true so destructor can retry
            SDL_Log("NetworkManager: Warning - failed to remove UPnP port mapping, will retry on exit");
        }
    }
    
    // Fully stop the server (called when leaving a game or menu)
    bIsServer = false;
    bLANServer = false;
    // NOTE: Do NOT reset bGameInProgress here - it should remain true while game is active
    // It will be reset when NetworkManager is destroyed or when a new server is started
    pGameInitSettings = nullptr;
}

void NetworkManager::sendHolePunchPackets(const std::string& targetIP, uint16_t targetPort, int count, int intervalMs) {
    if (host == nullptr || host->socket == ENET_SOCKET_NULL) {
        SDL_Log("NetworkManager::sendHolePunchPackets - No socket available");
        return;
    }
    
    // Resolve target address
    ENetAddress targetAddress;
    if (enet_address_set_host(&targetAddress, targetIP.c_str()) < 0) {
        SDL_Log("NetworkManager::sendHolePunchPackets - Failed to resolve %s", targetIP.c_str());
        return;
    }
    targetAddress.port = targetPort;
    
    // Send punch packets - "DLHP" (Dune Legacy Hole Punch) signature
    const uint8_t punchData[] = {'D', 'L', 'H', 'P'};
    
    SDL_Log("NetworkManager: Sending %d hole punch packets to %s:%d", count, targetIP.c_str(), targetPort);
    
    for (int i = 0; i < count; i++) {
        ENetBuffer sendBuffer;
        sendBuffer.data = const_cast<uint8_t*>(punchData);
        sendBuffer.dataLength = sizeof(punchData);
        
        int sent = enet_socket_send(host->socket, &targetAddress, &sendBuffer, 1);
        if (sent < 0) {
            SDL_Log("NetworkManager::sendHolePunchPackets - Send failed on packet %d", i + 1);
        }
        
        if (i < count - 1 && intervalMs > 0) {
            SDL_Delay(intervalMs);
        }
    }
    
    SDL_Log("NetworkManager: Hole punch packets sent");
}

uint16_t NetworkManager::performStunQuery() {
    if (host == nullptr || host->socket == ENET_SOCKET_NULL) {
        SDL_Log("NetworkManager::performStunQuery - No socket available");
        return 0;
    }
    
    if (!peerList.empty()) {
        SDL_Log("NetworkManager::performStunQuery - Cannot run with active peers");
        return 0;
    }
    
    StunClient::StunResult result = StunClient::performStunQuery(host->socket);
    if (result.success) {
        SDL_Log("NetworkManager::performStunQuery - External: %s:%d", 
                result.externalIP.c_str(), result.externalPort);
        return result.externalPort;
    } else {
        SDL_Log("NetworkManager::performStunQuery - Failed: %s", result.errorMessage.c_str());
        return 0;
    }
}

bool NetworkManager::performStunQueryFull(std::string& outIP, uint16_t& outPort) {
    if (host == nullptr || host->socket == ENET_SOCKET_NULL) {
        SDL_Log("NetworkManager::performStunQueryFull - No socket available");
        return false;
    }
    
    if (!peerList.empty()) {
        SDL_Log("NetworkManager::performStunQueryFull - Cannot run with active peers");
        return false;
    }
    
    StunClient::StunResult result = StunClient::performStunQuery(host->socket);
    if (result.success) {
        outIP = result.externalIP;
        outPort = result.externalPort;
        SDL_Log("NetworkManager::performStunQueryFull - External: %s:%d", 
                outIP.c_str(), outPort);
        return true;
    } else {
        SDL_Log("NetworkManager::performStunQueryFull - Failed: %s", result.errorMessage.c_str());
        return false;
    }
}

void NetworkManager::connect(const std::string& hostname, int port, const std::string& playerName) {
    ENetAddress address;

    if(enet_address_set_host(&address, hostname.c_str()) < 0) {
        THROW(std::runtime_error, "NetworkManager: Resolving hostname '" + hostname + "' failed!");
    }
    address.port = port;

    connect(address, playerName);
}

void NetworkManager::connect(ENetAddress address, const std::string& playerName) {
    debugNetwork("Connecting to %s:%d\n", Address2String(address).c_str(), address.port);

    // A new client session starts in the lobby again. The same NetworkManager instance is
    // reused when a player returns from a match and joins another game, so the phase and the
    // simulation seed of the previous match must not leak into this one.
    bGameInProgress = false;
    simulationSeed = 0;
    modTransferState = ModTransferState();

    connectPeer = enet_host_connect(host, &address, 2, 0);
    if(connectPeer == nullptr) {
        THROW(std::runtime_error, "NetworkManager: No available peers for initiating a connection.");
    }

    pendingCoopMission.reset();
    this->playerName = playerName;

    connectPeer->data = createPeerData(connectPeer, PeerData::PeerState::WaitingForConnect);
    awaitingConnectionList.push_back(connectPeer);
}

void NetworkManager::disconnect() {
    for(ENetPeer* pAwaitingConnectionPeer : awaitingConnectionList) {
        enet_peer_disconnect_later(pAwaitingConnectionPeer, NETWORKDISCONNECT_QUIT);
    }
    for(ENetPeer* pCurrentPeer : peerList) {
        enet_peer_disconnect_later(pCurrentPeer, NETWORKDISCONNECT_QUIT);
    }
}

void NetworkManager::update()
{
    if(pLANGameFinderAndAnnouncer != nullptr) {
        pLANGameFinderAndAnnouncer->update();
    }

    if(pMetaServerClient != nullptr) {
        pMetaServerClient->update();
    }
    
    // Renew UPnP lease before it expires (5 minutes before expiry)
    if (upnpPortMapped && pUPnPManager && upnpLeaseStartTime != 0) {
        Uint32 elapsed = (SDL_GetTicks() - upnpLeaseStartTime) / 1000;  // seconds
        if (elapsed >= (UPNP_LEASE_DURATION - UPNP_RENEWAL_MARGIN)) {
            SDL_Log("NetworkManager: Renewing UPnP port mapping lease...");
            if (pUPnPManager->addPortMapping(upnpMappedPort, upnpMappedPort, "UDP", "Dune City", UPNP_LEASE_DURATION)) {
                upnpLeaseStartTime = SDL_GetTicks();
                SDL_Log("NetworkManager: UPnP lease renewed successfully");
            } else {
                SDL_Log("NetworkManager: Warning - UPnP lease renewal failed");
            }
        }
    }
    
    // NAT Hole Punch: Non-blocking state machine for host-side punching
    // Only when hosting an internet game (not LAN) and NOT in an active game
    // CRITICAL: This uses blocking HTTP calls - MUST NOT run during gameplay!
    if (bIsServer && !bLANServer && !bGameInProgress && pMetaServerClient != nullptr) {
        Uint32 now = SDL_GetTicks();
        
        // Step 1: Poll for new punch requests (every 1 second)
        if (now - lastPunchPollTime >= PUNCH_POLL_INTERVAL_MS) {
            lastPunchPollTime = now;
            
            std::vector<std::tuple<std::string, std::string, uint16_t>> punchRequests;
            if (pMetaServerClient->pollPunchRequests(punchRequests) && !punchRequests.empty()) {
                for (const auto& request : punchRequests) {
                    std::string clientId = std::get<0>(request);
                    std::string clientIP = std::get<1>(request);
                    uint16_t clientPort = std::get<2>(request);
                    
                    SDL_Log("NAT Hole Punch: Received punch request from %s:%d (id: %s)",
                            clientIP.c_str(), clientPort, clientId.c_str());
                    
                    // Signal ready to punch (non-blocking - just HTTP GET)
                    if (pMetaServerClient->signalPunchReady(clientId)) {
                        // Schedule punch for PUNCH_DELAY_MS from now (no blocking!)
                        PendingPunch pending;
                        pending.clientId = clientId;
                        pending.clientIP = clientIP;
                        pending.clientPort = clientPort;
                        pending.punchAtTime = now + PUNCH_DELAY_MS;
                        pending.packetsRemaining = PUNCH_PACKET_COUNT;
                        pending.lastPacketTime = 0;
                        pendingPunches.push_back(pending);
                        
                        SDL_Log("NAT Hole Punch: Scheduled punch to %s:%d in %dms",
                                clientIP.c_str(), clientPort, PUNCH_DELAY_MS);
                    }
                }
            }
        }
        
        // Step 2: Process pending punches (send 1 packet per interval, no blocking)
        for (auto it = pendingPunches.begin(); it != pendingPunches.end(); ) {
            PendingPunch& pending = *it;
            
            // Check if it's time to start/continue punching
            if (now >= pending.punchAtTime && pending.packetsRemaining > 0) {
                // Check if enough time passed since last packet
                if (now - pending.lastPacketTime >= PUNCH_PACKET_INTERVAL_MS) {
                    // Send one punch packet
                    if (host != nullptr && host->socket != ENET_SOCKET_NULL) {
                        ENetAddress targetAddress;
                        if (enet_address_set_host(&targetAddress, pending.clientIP.c_str()) == 0) {
                            targetAddress.port = pending.clientPort;
                            
                            const uint8_t punchData[] = {'D', 'L', 'H', 'P'};
                            ENetBuffer sendBuffer;
                            sendBuffer.data = const_cast<uint8_t*>(punchData);
                            sendBuffer.dataLength = sizeof(punchData);
                            
                            enet_socket_send(host->socket, &targetAddress, &sendBuffer, 1);
                        }
                    }
                    
                    pending.packetsRemaining--;
                    pending.lastPacketTime = now;
                    
                    if (pending.packetsRemaining == 0) {
                        SDL_Log("NAT Hole Punch: Completed punch to %s:%d",
                                pending.clientIP.c_str(), pending.clientPort);
                    }
                }
            }
            
            // Remove completed punches
            if (pending.packetsRemaining <= 0) {
                it = pendingPunches.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // NAT keep-alive: Send periodic reliable ping to prevent NAT mapping timeout
    // Many routers drop UDP NAT mappings after 30-60 seconds of "inactivity"
    // (unreliable packets don't count as activity since they have no ACKs)
    if (!peerList.empty() || connectPeer != nullptr) {
        Uint32 now = SDL_GetTicks();
        if (now - lastKeepAliveTime >= KEEPALIVE_INTERVAL_MS) {
            lastKeepAliveTime = now;
            
            ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
            packetStream.writeUint32(NETWORKPACKET_KEEPALIVE);
            packetStream.writeUint32(now);  // Timestamp for debugging
            
            if (bIsServer) {
                sendPacketToAllConnectedPeers(packetStream);
            } else if (connectPeer != nullptr) {
                sendPacketToHost(packetStream);
            }
        }
    }

    if(bIsServer) {
        // Check for timeout of one client
        if(awaitingConnectionList.empty() == false) {
            ENetPeer* pCurrentPeer = awaitingConnectionList.front();
            PeerData* peerData = static_cast<PeerData*>(pCurrentPeer->data);

            if(peerData->peerState == PeerData::PeerState::ReadyForOtherPeersToConnect) {
                if(numPlayers >= maxPlayers) {
                    enet_peer_disconnect_later(pCurrentPeer, NETWORKDISCONNECT_GAME_FULL);
                } else {
                    // only one peer should be in state 'PeerState::WaitingForOtherPeersToConnect'
                    peerData->peerState = PeerData::PeerState::WaitingForOtherPeersToConnect;
                    peerData->timeout = SDL_GetTicks() + AWAITING_CONNECTION_TIMEOUT;
                    peerData->notYetConnectedPeers = peerList;

                    if(peerData->notYetConnectedPeers.empty()) {
                        // first client on this server
                        // => change immediately to connected

                        // get change event list first
                        ChangeEventList changeEventList = pGetChangeEventListForNewPlayerCallback(peerData->name);

                        debugNetwork("Moving '%s' from awaiting connection list to peer list\n", peerData->name.c_str());
                        peerList.push_back(pCurrentPeer);
                        peerData->peerState = PeerData::PeerState::Connected;
                        peerData->timeout = 0;
                        awaitingConnectionList.remove(pCurrentPeer);

                        // send peer game settings
                        ENetPacketOStream packetOStream2(ENET_PACKET_FLAG_RELIABLE);
                        packetOStream2.writeUint32(NETWORKPACKET_SENDGAMEINFO);
                        pGameInitSettings->save(packetOStream2);

                        changeEventList.save(packetOStream2);

                        sendPacketToPeer(pCurrentPeer, packetOStream2);
                        
                        // Send mod info to newly connected peer for mod sync
                        if(ModManager::instance().isInitialized()) {
                            std::string modName = ModManager::instance().getActiveModName();
                            std::string modChecksum = ModManager::instance().getEffectiveChecksums().combined;
                            SDL_Log("NetworkManager: Sending mod info to new peer - mod='%s', checksum=%s", 
                                    modName.c_str(), modChecksum.c_str());
                            sendModInfoToPeer(pCurrentPeer, modName, modChecksum);
                        }
                    } else {
                        // instruct all connected peers to connect

                        ENetPacketOStream packetOStream(ENET_PACKET_FLAG_RELIABLE);
                        packetOStream.writeUint32(NETWORKPACKET_CONNECT);
                        packetOStream.writeUint32(SDL_SwapBE32(pCurrentPeer->address.host));
                        packetOStream.writeUint16(pCurrentPeer->address.port);
                        packetOStream.writeString(peerData->name);

                        sendPacketToAllConnectedPeers(packetOStream);
                    }
                }
            }

            if(peerData->timeout > 0 && SDL_GetTicks() > peerData->timeout) {
                // timeout
                switch(peerData->peerState) {
                    case PeerData::PeerState::WaitingForName: {
                        // nothing to do
                    } break;

                    case PeerData::PeerState::WaitingForOtherPeersToConnect: {
                        // the client awaiting connection has timed out => send everyone a disconnect message
                        ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
                        packetStream.writeUint32(NETWORKPACKET_DISCONNECT);
                        packetStream.writeUint32(SDL_SwapBE32(pCurrentPeer->address.host));
                        packetStream.writeUint16(pCurrentPeer->address.port);

                        sendPacketToAllConnectedPeers(packetStream);

                        enet_peer_disconnect(pCurrentPeer, NETWORKDISCONNECT_TIMEOUT);

                        awaitingConnectionList.pop_front();
                    } break;

                    case PeerData::PeerState::Connected:
                    default: {
                        // should never happen
                    } break;
                }
            }
        }
    }

    ENetEvent event;
    while(enet_host_service(host, &event, 0) > 0) {

        ENetPeer* peer = event.peer;

        switch(event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                if(bIsServer) {
                    // Server
                    debugNetwork("NetworkManager: %s:%u connected.\n", Address2String(peer->address).c_str(), peer->address.port);

                    // Admission, before any state is allocated for this connection.
                    if(bGameInProgress) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "NetworkManager: refusing connection from %s:%u - game already in progress",
                                    Address2String(peer->address).c_str(), peer->address.port);
                        enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                        break;
                    }

                    const std::size_t knownPeers = peerList.size() + awaitingConnectionList.size();
                    if(knownPeers >= MAX_MESH_PEERS
                       || (maxPlayers > 0 && knownPeers >= static_cast<std::size_t>(maxPlayers))) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "NetworkManager: refusing connection from %s:%u - lobby is full (%zu peers)",
                                    Address2String(peer->address).c_str(), peer->address.port, knownPeers);
                        enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                        break;
                    }

                    PeerData* newPeerData = createPeerData(peer, PeerData::PeerState::WaitingForName);
                    newPeerData->timeout = SDL_GetTicks() + AWAITING_CONNECTION_TIMEOUT;
                    peer->data = newPeerData;

                    debugNetwork("Adding '%s' to awaiting connection list\n", newPeerData->name.c_str());
                    awaitingConnectionList.push_back(peer);

                    // Send name
                    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
                    packetStream.writeUint32(NETWORKPACKET_SENDNAME);
                    packetStream.writeString(playerName);

                    sendPacketToPeer(peer, packetStream);
                } else if(connectPeer != nullptr) {
                    // Client
                    PeerData* peerData = static_cast<PeerData*>(peer->data);

                    if(bGameInProgress && peer != connectPeer && peerData == nullptr) {
                        // No new mesh members once the match is running.
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "NetworkManager: refusing mesh connection from %s:%u during a match",
                                    Address2String(peer->address).c_str(), peer->address.port);
                        enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                        break;
                    }

                    if(peer == connectPeer) {
                        ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
                        packetStream.writeUint32(NETWORKPACKET_SENDNAME);
                        packetStream.writeString(playerName);

                        sendPacketToHost(packetStream);

                        peerData->peerState = PeerData::PeerState::WaitingForOtherPeersToConnect;
                        peerData->timeout = 0;
                    } else {
                        debugNetwork("NetworkManager: %s:%u connected.\n", Address2String(peer->address).c_str(), peer->address.port);

                        PeerData* pConnectPeerData = static_cast<PeerData*>(connectPeer->data);

                        if(pConnectPeerData->peerState == PeerData::PeerState::WaitingForOtherPeersToConnect) {
                            if(peerData == nullptr) {
                                if(peerList.size() + awaitingConnectionList.size() >= MAX_MESH_PEERS) {
                                    enet_peer_disconnect(peer, NETWORKDISCONNECT_GAME_FULL);
                                    break;
                                }

                                peerData = createPeerData(peer, PeerData::PeerState::Connected);
                                peer->data = peerData;

                                debugNetwork("Adding '%s' to awaiting connection list\n", peerData->name.c_str());
                                awaitingConnectionList.push_back(peer);
                            }
                        } else if(peerData == nullptr) {
                            // We are fully connected and did not open this connection: nobody
                            // instructed us to expect it, so it is not part of the mesh.
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                        "NetworkManager: refusing unsolicited connection from %s:%u",
                                        Address2String(peer->address).c_str(), peer->address.port);
                            enet_peer_disconnect(peer, NETWORKDISCONNECT_TIMEOUT);
                        } else {
                            ENetPacketOStream packetStream1(ENET_PACKET_FLAG_RELIABLE);
                            packetStream1.writeUint32(NETWORKPACKET_PEER_CONNECTED);
                            packetStream1.writeUint32(SDL_SwapBE32(peer->address.host));
                            packetStream1.writeUint16(peer->address.port);

                            sendPacketToHost(packetStream1);

                            ENetPacketOStream packetStream2(ENET_PACKET_FLAG_RELIABLE);
                            packetStream2.writeUint32(NETWORKPACKET_SENDNAME);
                            packetStream2.writeString(playerName);

                            sendPacketToPeer(peer, packetStream2);
                        }
                    }
                } else {
                    enet_peer_disconnect(peer, NETWORKDISCONNECT_TIMEOUT);
                }
            } break;

            case ENET_EVENT_TYPE_RECEIVE: {
                //debugNetwork("NetworkManager: A packet of length %u was received from %s:%u on channel %u on this server.\n",
                //                (unsigned int) event.packet->dataLength, Address2String(peer->address).c_str(), peer->address.port, event.channelID);

                const std::size_t receivedBytes = (event.packet != nullptr) ? event.packet->dataLength : 0;

                // The stream takes ownership of the packet, so build it first: the packet is
                // released even when the byte budget refuses to parse it.
                ENetPacketIStream packetStream(event.packet);

                if(acceptIncomingBytes(peer, receivedBytes)) {
                    handlePacket(peer, packetStream);
                }
            } break;

            case ENET_EVENT_TYPE_DISCONNECT: {
                PeerData* peerData = static_cast<PeerData*>(peer->data);

                int disconnectCause = event.data;

                debugNetwork("NetworkManager: %s:%u (%s) disconnected (%d).\n", Address2String(peer->address).c_str(), peer->address.port, (peerData != nullptr) ? peerData->name.c_str() : "unknown", disconnectCause);

                if(peerData != nullptr) {
                    if(std::find(awaitingConnectionList.begin(), awaitingConnectionList.end(), peer) != awaitingConnectionList.end()) {
                        // Only the host announces that a peer is gone. Every client is
                        // connected to every other client, so each of them sees its own ENet
                        // disconnect event; a client-sent DISCONNECT is both redundant and
                        // refused by the receiving peers' host-only rule for this packet.
                        if(bIsServer && peerData->peerState == PeerData::PeerState::WaitingForOtherPeersToConnect) {
                            ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
                            packetStream.writeUint32(NETWORKPACKET_DISCONNECT);
                            packetStream.writeUint32(SDL_SwapBE32(peer->address.host));
                            packetStream.writeUint16(peer->address.port);

                            sendPacketToAllConnectedPeers(packetStream);
                        }

                        debugNetwork("Removing '%s' from awaiting connection list\n", peerData->name.c_str());
                        awaitingConnectionList.remove(peer);
                    }


                    if(std::find(peerList.begin(), peerList.end(), peer) != peerList.end()) {
                        debugNetwork("Removing '%s' from peer list\n", peerData->name.c_str());
                        peerList.remove(peer);

                        if(bIsServer) {
                            ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
                            packetStream.writeUint32(NETWORKPACKET_DISCONNECT);
                            packetStream.writeUint32(SDL_SwapBE32(peer->address.host));
                            packetStream.writeUint16(peer->address.port);

                            sendPacketToAllConnectedPeers(packetStream);
                        }

                        if(pOnPeerDisconnected) {
                            pOnPeerDisconnected(peerData->name, (peer == connectPeer), disconnectCause);
                        }
                    } else {
                        if(peer == connectPeer) {
                            // host disconnected while establishing connection
                            if(pOnPeerDisconnected) {
                                pOnPeerDisconnected(peerData->name, true, disconnectCause);
                            }
                        }
                    }
                }

                // delete peer data
                delete peerData;
                peer->data = nullptr;

                if(peer == connectPeer) {
                    connectPeer = nullptr;
                }

            } break;

            default: {

            } break;
        }
    }
}

NetworkManager::PeerData* NetworkManager::createPeerData(ENetPeer* peer, PeerData::PeerState peerState) {
    PeerData* peerData = new PeerData(peer, peerState);
    peerData->clientId = nextClientId++;
    if(nextClientId == 0) {
        nextClientId = 1;   // never hand out 0, it doubles as "no client"
    }
    return peerData;
}

void NetworkManager::beginPeerDisconnect(ENetPeer* peer, const char* reason) {
    if(peer == nullptr) {
        return;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);

    if(peerData == nullptr) {
        // Nothing to mark; throttle the log so a connection without peer state cannot spin it.
        const Uint32 now = SDL_GetTicks();
        if(lastUnidentifiedLogTime == 0 || (now - lastUnidentifiedLogTime) >= REJECT_LOG_INTERVAL_MS) {
            lastUnidentifiedLogTime = now;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: dropping traffic from unidentified peer %s:%u (%s)",
                        Address2String(peer->address).c_str(), peer->address.port, reason);
        }
        enet_peer_disconnect_later(peer, NETWORKDISCONNECT_TIMEOUT);
        return;
    }

    if(!peerData->refusals.beginDisconnect()) {
        // The drop was already requested and logged once; say nothing further.
        return;
    }

    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "NetworkManager: disconnecting '%s' (%s:%u): %s",
                 peerData->name.c_str(), Address2String(peer->address).c_str(),
                 peer->address.port, reason);
    enet_peer_disconnect_later(peer, NETWORKDISCONNECT_TIMEOUT);
}

void NetworkManager::noteRejectedPacket(ENetPeer* peer, const char* reason) {
    if(peer == nullptr) {
        return;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);

    if(peerData == nullptr) {
        beginPeerDisconnect(peer, reason);
        return;
    }

    if(peerData->refusals.isDisconnecting()) {
        return;
    }

    const Uint32 now = SDL_GetTicks();

    // Refusals that are far apart are not an attack: a few packets can legitimately race a
    // phase transition or a peer leaving.
    const bool bTooMany = peerData->refusals.noteRefusal(now, MAX_REJECTED_PACKETS_PER_PEER,
                                                         REJECT_DECAY_MS);

    if(peerData->refusals.refusals <= 3
       || (now - peerData->lastRejectLogTime) >= REJECT_LOG_INTERVAL_MS) {
        peerData->lastRejectLogTime = now;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: rejected packet from '%s' (%s:%u): %s (%u refused so far)",
                    peerData->name.c_str(), Address2String(peer->address).c_str(),
                    peer->address.port, reason, peerData->refusals.refusals);
    }

    if(bTooMany) {
        // One shot: this marks the peer, so nothing from it is parsed, counted or logged again.
        beginPeerDisconnect(peer, "too many refused packets");
    }
}

bool NetworkManager::acceptIncomingBytes(ENetPeer* peer, std::size_t byteCount) {
    if(peer == nullptr) {
        return false;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);
    if(peerData == nullptr) {
        // No admitted state: the packet is refused by admitPacket() anyway, and that path
        // drops the connection.
        return true;
    }

    if(peerData->refusals.isDisconnecting()) {
        return false;
    }

    // Only a mod transfer this client asked for may use the large budget, and only on the
    // connection to the host. Everything else lives far below the ordinary budget.
    const bool expectingModTransfer = (!bIsServer) && (connectPeer != nullptr) && (peer == connectPeer)
        && (modTransferState.requested || modTransferState.inProgress);
    const Uint64 budget = expectingModTransfer ? MAX_MOD_TRANSFER_BYTES_PER_SECOND
                                               : MAX_PEER_BYTES_PER_SECOND;

    if(!peerData->byteWindow.accept(SDL_GetTicks(), static_cast<Uint64>(byteCount),
                                    budget, BYTE_WINDOW_MS)) {
        beginPeerDisconnect(peer, "incoming byte budget exceeded");
        return false;
    }

    return true;
}

bool NetworkManager::admitPacket(ENetPeer* peer, Uint32 packetType) {
    if(peer == nullptr) {
        return false;
    }

    PeerData* peerData = static_cast<PeerData*>(peer->data);

    // Already dropped: stop parsing anything else this peer has queued.
    if(peerData != nullptr && peerData->refusals.isDisconnecting()) {
        return false;
    }

    // Cheap flood guard: even well-formed packets are refused above a rate no legitimate
    // peer reaches (a full mod transfer is ~160 packets, in-game traffic a few dozen/s).
    if(peerData != nullptr) {
        if(!peerData->packetWindow.accept(SDL_GetTicks(), 1, MAX_PACKETS_PER_PEER_PER_SECOND,
                                          BYTE_WINDOW_MS)) {
            beginPeerDisconnect(peer, "packet rate limit exceeded");
            return false;
        }
    }

    NetworkPacketPolicy::PacketContext context;
    context.packetType = packetType;
    context.localRole = bIsServer ? NetworkPacketPolicy::LocalRole::Host
                                  : NetworkPacketPolicy::LocalRole::Client;
    context.phase = bGameInProgress ? NetworkPacketPolicy::SessionPhase::InGame
                                    : NetworkPacketPolicy::SessionPhase::Lobby;
    context.isHostConnection = (!bIsServer) && (connectPeer != nullptr) && (peer == connectPeer);

    if(peerData == nullptr) {
        context.admission = NetworkPacketPolicy::PeerAdmission::Unidentified;
    } else if(std::find(peerList.begin(), peerList.end(), peer) != peerList.end()) {
        context.admission = NetworkPacketPolicy::PeerAdmission::Established;
    } else {
        context.admission = NetworkPacketPolicy::PeerAdmission::Handshaking;
    }

    const NetworkPacketPolicy::PacketVerdict verdict = NetworkPacketPolicy::classifyPacket(context);
    if(verdict == NetworkPacketPolicy::PacketVerdict::Accept) {
        return true;
    }

    if(NetworkPacketPolicy::isExpectedOrderingRefusal(verdict)) {
        // Peers change phase at slightly different times - clients start their countdown half
        // a round trip before the host, and campaign co-op moves between missions - so packets
        // that are valid but stale are dropped quietly rather than held against the sender.
        debugNetwork("NetworkManager: dropping out-of-phase packet %u from %s:%u\n",
                     packetType, Address2String(peer->address).c_str(), peer->address.port);
        return false;
    }

    noteRejectedPacket(peer, NetworkPacketPolicy::describeVerdict(verdict));
    return false;
}

void NetworkManager::abortModTransfer(const char* reason) {
    const bool wasInProgress = modTransferState.inProgress;

    modTransferState.inProgress = false;
    modTransferState.modData.clear();
    modTransferState.modName.clear();
    modTransferState.totalSize = 0;
    modTransferState.receivedSize = 0;

    if(wasInProgress && pOnModDownloadComplete) {
        pOnModDownloadComplete(false, reason);
    }
}

void NetworkManager::handlePacket(ENetPeer* peer, ENetPacketIStream& packetStream)
{
    try {
        Uint32 packetType = packetStream.readUint32();

        // Central admission: role, handshake state and session phase are checked before any
        // payload of this packet is interpreted.
        if(!admitPacket(peer, packetType)) {
            return;
        }

        switch(packetType) {
            case NETWORKPACKET_CONNECT: {
                // Only reachable on a client, on the connection to the designated host
                // (enforced by admitPacket); the mesh address it names still has to be sane.
                const Uint32 rawHost = packetStream.readUint32();
                const Uint16 rawPort = packetStream.readUint16();
                const std::string peerName = packetStream.readString();

                // rawHost is the dotted-quad as an integer (the sender wrote
                // SDL_SwapBE32(address.host)), so the first octet is its most significant byte.
                if(!NetworkPacketPolicy::isPlausibleMeshTarget(rawHost, rawPort)
                   || !NetworkPacketPolicy::isAcceptablePlayerName(peerName)) {
                    noteRejectedPacket(peer, "implausible mesh connect target");
                    break;
                }

                if(awaitingConnectionList.size() + peerList.size() >= MAX_MESH_PEERS) {
                    noteRejectedPacket(peer, "mesh peer limit reached");
                    break;
                }

                ENetAddress address;
                address.host = SDL_SwapBE32(rawHost);
                address.port = rawPort;

                debugNetwork("Connecting to %s:%d\n", Address2String(address).c_str(), address.port);

                ENetPeer *newPeer = enet_host_connect(host, &address, 2, 0);
                if(newPeer == nullptr) {
                    debugNetwork("NetworkManager: No available peers for initiating a connection.");
                } else {
                    PeerData* peerData = createPeerData(newPeer, PeerData::PeerState::WaitingForOtherPeersToConnect);
                    peerData->name = peerName;
                    peerData->bNameAssigned = true;

                    newPeer->data = peerData;
                    debugNetwork("Adding '%s' to awaiting connection list\n", peerData->name.c_str());
                    awaitingConnectionList.push_back(newPeer);
                }
            } break;

            case NETWORKPACKET_DISCONNECT: {
                ENetAddress address;

                address.host = SDL_SwapBE32(packetStream.readUint32());
                address.port = packetStream.readUint16();

                for(ENetPeer* pCurrentPeer : peerList) {
                    if((pCurrentPeer->address.host == address.host) && (pCurrentPeer->address.port == address.port)) {
                        enet_peer_disconnect_later(pCurrentPeer, NETWORKDISCONNECT_QUIT);
                        break;
                    }
                }

                for(ENetPeer* pAwaitingConnectionPeer : awaitingConnectionList) {
                    if((pAwaitingConnectionPeer->address.host == address.host) && (pAwaitingConnectionPeer->address.port == address.port)) {
                        enet_peer_disconnect_later(pAwaitingConnectionPeer, NETWORKDISCONNECT_QUIT);
                        break;
                    }
                }

            } break;

            case NETWORKPACKET_PEER_CONNECTED: {

                ENetAddress address;

                address.host = SDL_SwapBE32(packetStream.readUint32());
                address.port = packetStream.readUint16();

                if(isServer()) {

                    if(awaitingConnectionList.empty() == false) {
                        ENetPeer* pCurrentPeer = awaitingConnectionList.front();
                        PeerData* peerData = static_cast<PeerData*>(pCurrentPeer->data);
                        if(!peerData) {
                            break;
                        }

                        if((pCurrentPeer->address.host == address.host) && (pCurrentPeer->address.port == address.port)) {

                            peerData->notYetConnectedPeers.remove(peer);

                            if(peerData->notYetConnectedPeers.empty()) {
                                // send connected to all peers (excluding the new one)
                                ENetPacketOStream packetOStream(ENET_PACKET_FLAG_RELIABLE);
                                packetOStream.writeUint32(NETWORKPACKET_PEER_CONNECTED);
                                packetOStream.writeUint32(SDL_SwapBE32(pCurrentPeer->address.host));
                                packetOStream.writeUint16(pCurrentPeer->address.port);

                                sendPacketToAllConnectedPeers(packetOStream);

                                // get change event list first
                                ChangeEventList changeEventList = pGetChangeEventListForNewPlayerCallback(peerData->name);

                                // move peer to peer list
                                debugNetwork("Moving '%s' from awaiting connection list to peer list\n", peerData->name.c_str());
                                peerList.push_back(pCurrentPeer);
                                peerData->peerState = PeerData::PeerState::Connected;
                                peerData->timeout = 0;
                                awaitingConnectionList.remove(pCurrentPeer);

                                // send peer game settings
                                ENetPacketOStream packetOStream2(ENET_PACKET_FLAG_RELIABLE);
                                packetOStream2.writeUint32(NETWORKPACKET_SENDGAMEINFO);
                                pGameInitSettings->save(packetOStream2);

                                changeEventList.save(packetOStream2);

                                sendPacketToPeer(pCurrentPeer, packetOStream2);
                                
                                // Send mod info to newly connected peer for mod sync
                                if(ModManager::instance().isInitialized()) {
                                    std::string modName = ModManager::instance().getActiveModName();
                                    std::string modChecksum = ModManager::instance().getEffectiveChecksums().combined;
                                    SDL_Log("NetworkManager: Sending mod info to new peer - mod='%s', checksum=%s", 
                                            modName.c_str(), modChecksum.c_str());
                                    sendModInfoToPeer(pCurrentPeer, modName, modChecksum);
                                }
                            }
                        }
                    }
                } else {
                    for(auto iter = awaitingConnectionList.begin(); iter != awaitingConnectionList.end(); ++iter) {
                        ENetPeer* pCurrentPeer = *iter;

                        if((pCurrentPeer->address.host == address.host) && (pCurrentPeer->address.port == address.port)) {
                            PeerData* peerData = static_cast<PeerData*>(pCurrentPeer->data);
                            if(!peerData) {
                                continue;
                            }
                            debugNetwork("Moving '%s' from awaiting connection list to peer list\n", peerData->name.c_str());
                            peerList.push_back(pCurrentPeer);
                            peerData->peerState = PeerData::PeerState::Connected;
                            peerData->timeout = 0;
                            awaitingConnectionList.erase(iter);
                            break;
                        }
                    }
                }

            } break;

            case NETWORKPACKET_SENDGAMEINFO: {
                if(!connectPeer) {
                    break;
                }

                PeerData* peerData = static_cast<PeerData*>(connectPeer->data);
                if(!peerData) {
                    break;
                }

                // Decode into temporaries and validate the whole snapshot *before* any session
                // state changes: a malformed or oversized packet must leave this client exactly
                // as it was, not half-committed with its peer list already replaced.
                GameInitSettings gameInitSettings(packetStream);
                ChangeEventList changeEventList(packetStream);

                std::string rejectionReason;
                if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(gameInitSettings,
                                                                                 rejectionReason)) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "NetworkManager: refusing game info from the host: %s",
                                rejectionReason.c_str());
                    noteRejectedPacket(peer, "unacceptable game info");
                    break;
                }

                const bool bHasMapPayload =
                    gameInitSettings.getGameType() == GameType::CustomMultiplayer
                    && !gameInitSettings.getFiledata().empty()
                    && !gameInitSettings.getFilename().empty();

                // A map we cannot store safely is a packet we do not accept at all - playing it
                // from memory while refusing to write it would hide the problem from the player.
                std::string mapFilename;
                if(bHasMapPayload
                   && !NetworkPacketPolicy::sanitizeReceivedMapFilename(
                          gameInitSettings.getFilename(), mapFilename)) {
                    noteRejectedPacket(peer, "unsafe received map filename");
                    break;
                }

                // Commit membership only now that the packet is known to be usable.
                peerList = awaitingConnectionList;
                peerData->peerState = PeerData::PeerState::Connected;
                peerData->timeout = 0;
                awaitingConnectionList.clear();

                // Save the received map to the user's maps/multiplayer directory
                if(bHasMapPayload) {
                    try {
                        {
                            char tmp[FILENAME_MAX];
                            if(fnkdat("maps/multiplayer/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) >= 0) {
                                const std::filesystem::path mapDirectory =
                                    std::filesystem::path(std::string(tmp));
                                const std::filesystem::path fullPathObject = mapDirectory / mapFilename;

                                // Belt and braces: whatever the name did, the file has to land
                                // directly inside the multiplayer maps directory.
                                std::error_code pathError;
                                const std::filesystem::path resolvedParent =
                                    std::filesystem::weakly_canonical(fullPathObject.parent_path(), pathError);
                                const std::filesystem::path resolvedDirectory =
                                    std::filesystem::weakly_canonical(mapDirectory, pathError);

                                if(pathError || resolvedParent != resolvedDirectory) {
                                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                                "NetworkManager: refusing to write a received map outside '%s'",
                                                mapDirectory.string().c_str());
                                } else {
                                    const std::string fullPath = fullPathObject.string();

                                    // Only save if the file doesn't exist yet (avoid overwriting user-modified maps)
                                    if(!existsFile(fullPath)) {
                                        if(writeCompleteFile(fullPath, gameInitSettings.getFiledata())) {
                                            SDL_Log("NetworkManager: Successfully saved received map to '%s'", fullPath.c_str());
                                        } else {
                                            SDL_Log("NetworkManager: Failed to save received map to '%s'", fullPath.c_str());
                                        }
                                    } else {
                                        SDL_Log("NetworkManager: Map '%s' already exists locally, skipping save", fullPath.c_str());
                                    }
                                }
                            } else {
                                SDL_Log("NetworkManager: Failed to get maps/multiplayer directory path");
                            }
                        }
                    } catch(std::exception& e) {
                        SDL_Log("NetworkManager: Error saving received map: %s", e.what());
                    }
                }

                if(pOnReceiveGameInfo) {
                    pOnReceiveGameInfo(gameInitSettings, changeEventList);
                }
            } break;

            case NETWORKPACKET_SENDNAME: {
                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(!peerData) {
                    break;
                }

                std::string newName = packetStream.readString();

                if(!NetworkPacketPolicy::isAcceptablePlayerName(newName)) {
                    noteRejectedPacket(peer, "unacceptable player name");
                    break;
                }

                // Identity is bound exactly once per connection. CommandManager resolves a
                // command list to a player by this name, so a later rename would let a peer
                // take over another player's commands.
                if(peerData->bNameAssigned) {
                    noteRejectedPacket(peer, "peer tried to change its established name");
                    break;
                }

                bool bFoundName = false;

                //check if name already exists
                if(bIsServer) {
                    if(playerName == newName) {
                        enet_peer_disconnect_later(peer, NETWORKDISCONNECT_PLAYER_EXISTS);
                        bFoundName = true;
                    }

                    if(bFoundName == false) {
                        for(ENetPeer* pCurrentPeer : peerList) {
                            PeerData* pCurrentPeerData = static_cast<PeerData*>(pCurrentPeer->data);
                            if(!pCurrentPeerData) {
                                continue;
                            }
                            if(pCurrentPeerData->name == newName) {
                                enet_peer_disconnect_later(peer, NETWORKDISCONNECT_PLAYER_EXISTS);
                                bFoundName = true;
                                break;
                            }
                        }
                    }

                    if(bFoundName == false) {
                        for(ENetPeer* pAwaitingConnectionPeer : awaitingConnectionList) {
                            PeerData* pAwaitingConnectionPeerData = static_cast<PeerData*>(pAwaitingConnectionPeer->data);
                            if(pAwaitingConnectionPeerData && (pAwaitingConnectionPeerData->name == newName)) {
                                enet_peer_disconnect_later(peer, NETWORKDISCONNECT_PLAYER_EXISTS);
                                bFoundName = true;
                                break;
                            }
                        }
                    }
                }

                if(bFoundName == false) {
                    peerData->name = newName;
                    peerData->bNameAssigned = true;

                    if(peerData->peerState == PeerData::PeerState::WaitingForName) {
                        peerData->peerState = PeerData::PeerState::ReadyForOtherPeersToConnect;
                    }
                }
            } break;

            case NETWORKPACKET_CHATMESSAGE: {
                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(!peerData) {
                    break;
                }

                std::string message = packetStream.readString();
                if(message.size() > MAX_CHAT_MESSAGE_LENGTH) {
                    noteRejectedPacket(peer, "chat message exceeds the length limit");
                    break;
                }
                if(pOnReceiveChatMessage) {
                    pOnReceiveChatMessage(peerData->name, message);
                }
            } break;

            case NETWORKPACKET_CHANGEEVENTLIST: {
                ChangeEventList changeEventList(packetStream);

                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(peerData == nullptr) {
                    break;
                }

                if(bIsServer) {
                    // A client only ever seats itself: every lobby slot claim it sends carries
                    // its own name (CustomGamePlayers::onClickPlayerDropDownBox). Anything else
                    // is a peer trying to move another player around.
                    bool bForeignSlotClaim = false;
                    for(const ChangeEventList::ChangeEvent& changeEvent : changeEventList.changeEventList) {
                        if(changeEvent.eventType == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer
                           && changeEvent.newStringValue != peerData->name) {
                            bForeignSlotClaim = true;
                            break;
                        }
                    }

                    if(bForeignSlotClaim) {
                        noteRejectedPacket(peer, "lobby slot claim for another player");
                        break;
                    }
                }

                if(pOnReceiveChangeEventList) {
                    pOnReceiveChangeEventList(peerData->name, changeEventList);
                }
            } break;

            case NETWORKPACKET_CONFIG_HASH: {
                Uint32 peerProtocolVersion = packetStream.readUint32();
                std::string gameVersion = packetStream.readString();
                std::string quantBotHash = packetStream.readString();
                std::string objectDataHash = packetStream.readString();
                
                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(peerData) {
                    peerData->gameVersion = gameVersion;
                    peerData->quantBotConfigHash = quantBotHash;
                    peerData->objectDataHash = objectDataHash;
                    
                    SDL_Log("========== CONFIG HASH RECEIVED ==========");
                    SDL_Log("From: %s", peerData->name.c_str());
                    SDL_Log("Protocol version: %d", peerProtocolVersion);
                    SDL_Log("Game version: %s", gameVersion.c_str());
                    SDL_Log("QuantBot Config.ini hash: %s", quantBotHash.c_str());
                    SDL_Log("ObjectData.ini hash: %s", objectDataHash.c_str());
                    SDL_Log("==========================================");
                    
                    // Get our own version and hashes (local)
                    std::string localVersion = VERSIONSTRING;
                    std::string localQuantBotHash = getQuantBotConfig().getConfigHash();
                    std::string localObjectDataHash = getObjectDataHash();

                    const bool protocolRejected = rejectIncompatibleNetworkProtocol(
                        peerProtocolVersion,
                        [peer](int cause) {
                            enet_peer_disconnect_later(peer, static_cast<enet_uint32>(cause));
                        });
                    if(protocolRejected) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "NetworkManager: rejecting incompatible protocol from %s (peer=%u, local=%u)",
                                     peerData->name.c_str(), peerProtocolVersion, NETWORK_PROTOCOL_VERSION);
                        break;
                    }
                    
                    // Mod transfer cannot replace executable simulation code.
                    if (rejectIncompatibleGameVersion(peerData->gameVersion, localVersion,
                        [peer](int cause) { enet_peer_disconnect_later(peer, static_cast<enet_uint32>(cause)); })) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rejecting game version mismatch: peer=%s local=%s",
                                     peerData->gameVersion.c_str(), localVersion.c_str());
                        break;
                    }

                    if(bIsServer) {
                        // Server: verify client matches server config
                        SDL_Log("========== SERVER CONFIG VERIFICATION ==========");
                        SDL_Log("Server protocol version: %d", NETWORK_PROTOCOL_VERSION);
                        SDL_Log("Server game version: %s", localVersion.c_str());
                        SDL_Log("Server QuantBot Config.ini hash: %s", localQuantBotHash.c_str());
                        SDL_Log("Server ObjectData.ini hash: %s", localObjectDataHash.c_str());
                        SDL_Log("Checking peer: %s", peerData->name.c_str());
                        SDL_Log("  Peer protocol: %d (Match: %s)", peerProtocolVersion,
                                (peerProtocolVersion == NETWORK_PROTOCOL_VERSION) ? "YES" : "NO");
                        SDL_Log("  Peer version: %s (Match: %s)", peerData->gameVersion.c_str(),
                                (peerData->gameVersion == localVersion) ? "YES" : "NO");
                        SDL_Log("  Peer QuantBot: %s (Match: %s)", peerData->quantBotConfigHash.c_str(), 
                                (peerData->quantBotConfigHash == localQuantBotHash) ? "YES" : "NO");
                        SDL_Log("  Peer ObjectData: %s (Match: %s)", peerData->objectDataHash.c_str(),
                                (peerData->objectDataHash == localObjectDataHash) ? "YES" : "NO");
                        
                        // Check if this peer has mismatched configs
                        bool mismatchFound = false;
                        std::string mismatchMessage;
                        
                        if(peerProtocolVersion != NETWORK_PROTOCOL_VERSION) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- %s has incompatible network protocol version\n  Client: %d\n  Server: %d",
                                                           peerData->name.c_str(), peerProtocolVersion, NETWORK_PROTOCOL_VERSION);
                            SDL_Log("*** MISMATCH: Network protocol version differs!");
                        }
                        if(peerData->gameVersion != localVersion) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- %s has different game version\n  Client: %s\n  Server: %s",
                                                           peerData->name.c_str(), peerData->gameVersion.c_str(), localVersion.c_str());
                            SDL_Log("*** MISMATCH: Game version differs!");
                        }
                        if(peerData->quantBotConfigHash != localQuantBotHash) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- %s has different QuantBot Config.ini\n  Client: %s\n  Server: %s",
                                                           peerData->name.c_str(), peerData->quantBotConfigHash.c_str(), localQuantBotHash.c_str());
                            SDL_Log("*** MISMATCH: QuantBot Config.ini differs!");
                        }
                        if(peerData->objectDataHash != localObjectDataHash) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- %s has different ObjectData.ini\n  Client: %s\n  Server: %s",
                                                           peerData->name.c_str(), peerData->objectDataHash.c_str(), localObjectDataHash.c_str());
                            SDL_Log("*** MISMATCH: ObjectData.ini differs!");
                        }
                        
                        SDL_Log("================================================");
                        
                        if(mismatchFound) {
                            // Don't abort - mod sync system will handle this
                            // Host already sent MOD_INFO, client will download and sync
                            SDL_Log("Config mismatch for %s - mod sync will resolve this", peerData->name.c_str());
                        } else {
                            SDL_Log("Config verification passed for %s", peerData->name.c_str());
                        }
                    } else {
                        // Client: verify server matches client config AND send our hash back
                        SDL_Log("========== CLIENT CONFIG VERIFICATION ==========");
                        SDL_Log("Client protocol version: %d", NETWORK_PROTOCOL_VERSION);
                        SDL_Log("Client game version: %s", localVersion.c_str());
                        SDL_Log("Client QuantBot Config.ini hash: %s", localQuantBotHash.c_str());
                        SDL_Log("Client ObjectData.ini hash: %s", localObjectDataHash.c_str());
                        SDL_Log("Checking server: %s", peerData->name.c_str());
                        SDL_Log("  Server protocol: %d (Match: %s)", peerProtocolVersion,
                                (peerProtocolVersion == NETWORK_PROTOCOL_VERSION) ? "YES" : "NO");
                        SDL_Log("  Server version: %s (Match: %s)", peerData->gameVersion.c_str(),
                                (peerData->gameVersion == localVersion) ? "YES" : "NO");
                        SDL_Log("  Server QuantBot: %s (Match: %s)", peerData->quantBotConfigHash.c_str(), 
                                (peerData->quantBotConfigHash == localQuantBotHash) ? "YES" : "NO");
                        SDL_Log("  Server ObjectData: %s (Match: %s)", peerData->objectDataHash.c_str(),
                                (peerData->objectDataHash == localObjectDataHash) ? "YES" : "NO");
                        
                        // Check if server has mismatched configs
                        bool mismatchFound = false;
                        std::string mismatchMessage;
                        
                        if(peerProtocolVersion != NETWORK_PROTOCOL_VERSION) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- Network protocol version differs\n  Your version: %d\n  Server version: %d",
                                                           NETWORK_PROTOCOL_VERSION, peerProtocolVersion);
                            SDL_Log("*** MISMATCH: Network protocol version differs!");
                        }
                        if(peerData->gameVersion != localVersion) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- Game version differs\n  Your version: %s\n  Server version: %s",
                                                           localVersion.c_str(), peerData->gameVersion.c_str());
                            SDL_Log("*** MISMATCH: Game version differs!");
                        }
                        if(peerData->quantBotConfigHash != localQuantBotHash) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- QuantBot Config.ini differs\n  Your hash: %s\n  Server hash: %s",
                                                           localQuantBotHash.c_str(), peerData->quantBotConfigHash.c_str());
                            SDL_Log("*** MISMATCH: QuantBot Config.ini differs!");
                        }
                        if(peerData->objectDataHash != localObjectDataHash) {
                            mismatchFound = true;
                            mismatchMessage += fmt::sprintf("\n- ObjectData.ini differs\n  Your hash: %s\n  Server hash: %s",
                                                           localObjectDataHash.c_str(), peerData->objectDataHash.c_str());
                            SDL_Log("*** MISMATCH: ObjectData.ini differs!");
                        }
                        
                        SDL_Log("================================================");
                        
                        // ALWAYS send our config back to server for server-side validation
                        // (even if client-side validation failed, server needs to validate too)
                        SDL_Log("Sending client config to server for verification");
                        ENetPacketOStream responsePacket(ENET_PACKET_FLAG_RELIABLE);
                        responsePacket.writeUint32(NETWORKPACKET_CONFIG_HASH);
                        responsePacket.writeUint32(NETWORK_PROTOCOL_VERSION);
                        responsePacket.writeString(localVersion);
                        responsePacket.writeString(localQuantBotHash);
                        responsePacket.writeString(localObjectDataHash);
                        sendPacketToHost(responsePacket);
                        
                        if(mismatchFound) {
                            // Don't block connection - mod sync system will handle this
                            // The client will receive MOD_INFO next and download the correct mod
                            SDL_Log("Config mismatch detected - waiting for mod sync to resolve");
                        } else {
                            SDL_Log("Config verification passed - configs match server");
                        }
                    }
                }
            } break;

            case NETWORKPACKET_COOP_MISSION: {
                // Co-op has only one remote peer; only the host can choose a mission.
                if(!bIsServer && peerList.size() == 1 && peerList.front() == peer) {
                    auto next = std::make_unique<GameInitSettings>(packetStream);

                    std::string coopRejectionReason;
                    if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(
                           *next, coopRejectionReason)) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "NetworkManager: refusing co-op mission from the host: %s",
                                    coopRejectionReason.c_str());
                        noteRejectedPacket(peer, "unacceptable co-op mission");
                        break;
                    }

                    // Either the next campaign mission, or the empty settings that end the
                    // campaign; nothing else may replace the pending mission.
                    if(next->getGameType() == GameType::CampaignCoop || next->getGameType() == GameType::Invalid)
                        pendingCoopMission = std::move(next);
                }
            } break;

            case NETWORKPACKET_STARTGAME: {
                // Only a client, only on the host connection, only in the lobby
                // (enforced by admitPacket).
                Uint32 timeLeft = packetStream.readUint32();

                if(timeLeft > MAX_START_GAME_COUNTDOWN_MS) {
                    noteRejectedPacket(peer, "start-game countdown out of range");
                    break;
                }

                if(pOnStartGame) {
                    pOnStartGame(timeLeft);
                }
            } break;

            case NETWORKPACKET_COMMANDLIST: {
                if(packetStream.readUint32() != simulationSeed) break;
                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(!peerData) {
                    break;
                }

                CommandList commandList(packetStream);

                if(pOnReceiveCommandList) {
                    pOnReceiveCommandList(peerData->name, commandList);
                }
            } break;

            case NETWORKPACKET_SELECTIONLIST: {
                if(packetStream.readUint32() != simulationSeed) break;
                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(!peerData) {
                    break;
                }

                int groupListIndex = packetStream.readSint32();
                std::set<Uint32> selectedList = packetStream.readUint32Set();

                if(selectedList.size() > NetworkPacketPolicy::kMaxSelectionSize) {
                    noteRejectedPacket(peer, "selection list exceeds the size limit");
                    break;
                }

                // -1 means "current selection"; anything else indexes HumanPlayer::selectedLists.
                if(groupListIndex < -1 || groupListIndex >= NUMSELECTEDLISTS) {
                    noteRejectedPacket(peer, "selection group index out of range");
                    break;
                }

                if(pOnReceiveSelectionList) {
                    pOnReceiveSelectionList(peerData->name, selectedList, groupListIndex);
                }
            } break;

            case NETWORKPACKET_CLIENTSTATS: {
                if(packetStream.readUint32() != simulationSeed) break;
                // Host only, established client, match running (enforced by admitPacket).
                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(!peerData) {
                    break;
                }

                Uint32 gameCycle = packetStream.readUint32();
                float avgFps = packetStream.readFloat();
                float simMsAvg = packetStream.readFloat();  // POST-VSYNC: Read simulation timing
                Uint32 queueDepth = packetStream.readUint32();
                Uint32 currentBudget = packetStream.readUint32();

                if(!NetworkPacketPolicy::isUsableStatValue(avgFps)
                   || !NetworkPacketPolicy::isUsableStatValue(simMsAvg)) {
                    noteRejectedPacket(peer, "non-finite client stats");
                    break;
                }

                // Identity is the connection, not an address hash: behind NAT (or later behind
                // a relay) host^port collides across players and is trivially spoofable.
                const Uint32 clientId = peerData->clientId;

                if(pOnReceiveClientStats) {
                    pOnReceiveClientStats(clientId, gameCycle, avgFps, simMsAvg, queueDepth, currentBudget);
                }
            } break;

            case NETWORKPACKET_SETPATHBUDGET: {
                if(packetStream.readUint32() != simulationSeed) break;
                // Client only, host connection only, match running (enforced by admitPacket).
                Uint32 newBudget = packetStream.readUint32();
                Uint32 applyCycle = packetStream.readUint32();

                if(newBudget > MAX_PATH_BUDGET_ORDER) {
                    noteRejectedPacket(peer, "path budget order out of range");
                    break;
                }

                if(pOnReceiveSetPathBudget) {
                    pOnReceiveSetPathBudget(newBudget, applyCycle);
                }
            } break;

            case NETWORKPACKET_MOD_INFO: {
                // Client only, from the host connection, lobby only (enforced by admitPacket).
                std::string modName = packetStream.readString();
                std::string modChecksum = packetStream.readString();

                if(!ModTransferValidation::isValidModName(modName)
                   || modChecksum.size() > MAX_MOD_CHECKSUM_LENGTH) {
                    noteRejectedPacket(peer, "invalid mod info");
                    break;
                }

                SDL_Log("NetworkManager: Received mod info from host - mod: '%s', checksum: %s",
                        modName.c_str(), modChecksum.c_str());

                if(pOnReceiveModInfo) {
                    pOnReceiveModInfo(modName, modChecksum);
                }
            } break;

            case NETWORKPACKET_MOD_REQUEST: {
                // Host only, from an established client, lobby only (enforced by admitPacket).
                std::string requestedModName = packetStream.readString();

                if(!ModTransferValidation::isValidModName(requestedModName)) {
                    noteRejectedPacket(peer, "invalid mod name in mod request");
                    break;
                }

                SDL_Log("NetworkManager: Client requested mod download for '%s'", requestedModName.c_str());

                // Package and send the mod files to the requesting peer
                sendModFilesToPeer(peer, requestedModName);
            } break;

            case NETWORKPACKET_MOD_CHUNK: {
                // Client only, from the host connection, lobby only (enforced by admitPacket).
                std::string modName = packetStream.readString();
                Uint32 totalSize = packetStream.readUint32();
                Uint32 chunkOffset = packetStream.readUint32();
                std::string chunkData = packetStream.readString();

                // Content is only accepted for a transfer this client actually asked for.
                if(!modTransferState.requested || modName != modTransferState.requestedModName) {
                    noteRejectedPacket(peer, "mod chunk for a transfer that was not requested");
                    abortModTransfer("Unexpected mod transfer");
                    break;
                }

                // Security: Validate totalSize against maximum allowed
                if(totalSize > MAX_MOD_TRANSFER_SIZE) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: Mod transfer size %u exceeds limit %d - aborting",
                        totalSize, MAX_MOD_TRANSFER_SIZE);
                    abortModTransfer("Mod exceeds size limit");
                    break;
                }

                if(chunkData.size() > static_cast<std::size_t>(MOD_CHUNK_SIZE)) {
                    noteRejectedPacket(peer, "mod chunk exceeds the chunk size limit");
                    abortModTransfer("Invalid chunk size");
                    break;
                }

                // Initialize transfer state if this is the first chunk
                if(!modTransferState.inProgress || modTransferState.modName != modName) {
                    modTransferState.modName = modName;
                    modTransferState.modData.clear();
                    modTransferState.modData.reserve(totalSize);
                    modTransferState.totalSize = totalSize;
                    modTransferState.receivedSize = 0;
                    modTransferState.inProgress = true;
                } else if(totalSize != modTransferState.totalSize) {
                    noteRejectedPacket(peer, "mod transfer size changed mid-transfer");
                    abortModTransfer("Invalid chunk size");
                    break;
                }

                // Security: Validate chunk offset matches expected position (enforce in-order)
                if(chunkOffset != modTransferState.receivedSize) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: Chunk offset mismatch - expected %zu, got %u. Aborting transfer.",
                        modTransferState.receivedSize, chunkOffset);
                    abortModTransfer("Out-of-order mod chunk");
                    break;
                }

                // Security: Check that adding this chunk won't exceed totalSize
                // (subtraction form: receivedSize is never greater than totalSize)
                if(chunkData.size() > totalSize - modTransferState.receivedSize) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "NetworkManager: Chunk would exceed total size - aborting");
                    abortModTransfer("Invalid chunk size");
                    break;
                }

                // Append chunk data
                modTransferState.modData.append(chunkData);
                modTransferState.receivedSize += chunkData.size();

                SDL_Log("NetworkManager: Received mod chunk %zu/%zu bytes", 
                        modTransferState.receivedSize, modTransferState.totalSize);

                if(pOnModDownloadProgress) {
                    pOnModDownloadProgress(modTransferState.receivedSize, modTransferState.totalSize);
                }
            } break;

            case NETWORKPACKET_MOD_COMPLETE: {
                // Client only, from the host connection, lobby only (enforced by admitPacket).
                bool success = packetStream.readBool();
                std::string message = packetStream.readString();
                if(message.size() > MAX_MOD_MESSAGE_LENGTH) {
                    message.resize(MAX_MOD_MESSAGE_LENGTH);
                }

                SDL_Log("NetworkManager: Mod transfer complete - success: %s, message: %s",
                        success ? "yes" : "no", message.c_str());

                if(!modTransferState.requested) {
                    noteRejectedPacket(peer, "mod completion for a transfer that was not requested");
                    abortModTransfer("Unexpected mod transfer");
                    break;
                }

                // A "successful" transfer that did not deliver every announced byte must not be
                // handed on as if it were a complete payload.
                const bool payloadComplete = modTransferState.inProgress
                    && modTransferState.totalSize > 0
                    && modTransferState.receivedSize == modTransferState.totalSize;

                if(success && !payloadComplete) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "NetworkManager: mod transfer reported success with %zu of %zu bytes - rejecting",
                                modTransferState.receivedSize, modTransferState.totalSize);
                    abortModTransfer("Incomplete mod transfer");
                    break;
                }

                modTransferState.inProgress = false;

                if(pOnModDownloadComplete) {
                    if(success) {
                        // Pass the received mod data for saving
                        pOnModDownloadComplete(true, modTransferState.modData);
                    } else {
                        pOnModDownloadComplete(false, message);
                    }
                }

                // Clear transfer state
                modTransferState.modData.clear();
                modTransferState.modName.clear();
                modTransferState.totalSize = 0;
                modTransferState.receivedSize = 0;
                modTransferState.requested = false;
                modTransferState.requestedModName.clear();
            } break;

            case NETWORKPACKET_MOD_ACK: {
                // Host only, from an established client, lobby only (enforced by admitPacket).
                bool success = packetStream.readBool();
                std::string modChecksum = packetStream.readString();

                if(modChecksum.size() > MAX_MOD_CHECKSUM_LENGTH) {
                    noteRejectedPacket(peer, "oversized mod checksum in mod ack");
                    break;
                }

                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(peerData == nullptr) {
                    break;
                }
                const std::string ackPlayerName = peerData->name;

                SDL_Log("NetworkManager: Received mod ACK from '%s' - success: %s, checksum: %s",
                        ackPlayerName.c_str(), success ? "yes" : "no", modChecksum.c_str());

                if(pOnReceiveModAck) {
                    pOnReceiveModAck(ackPlayerName, success, modChecksum);
                }
            } break;
            
            case NETWORKPACKET_KEEPALIVE: {
                // NAT keep-alive ping - just receiving it is enough to keep the NAT mapping alive
                // The reliable packet triggers ACKs which count as bidirectional traffic
                // No action needed, packet is silently consumed
            } break;

            default: {
                // Unreachable: admitPacket() already refuses unknown packet types.
                noteRejectedPacket(peer, "unknown packet type");
            };
        }

    } catch (InputStream::eof&) {
        noteRejectedPacket(peer, "packet truncated");
        return;
    } catch (std::exception& e) {
        noteRejectedPacket(peer, e.what());
    }
}


void NetworkManager::sendPacketToHost(ENetPacketOStream& packetStream, int channel) {
    if(connectPeer == nullptr) {
        // This can happen if host disconnected but game hasn't processed the quit yet
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: sendPacketToHost() failed - no host connection");
        return;
    }

    ENetPacket* enetPacket = packetStream.getPacket();

    if(enet_peer_send(connectPeer, channel, enetPacket) < 0) {
        SDL_Log("NetworkManager: Cannot send packet!");
    }
}

void NetworkManager::sendPacketToPeer(ENetPeer* peer, ENetPacketOStream& packetStream, int channel) {
    ENetPacket* enetPacket = packetStream.getPacket();

    if(enet_peer_send(peer, channel, enetPacket) < 0) {
        SDL_Log("NetworkManager: Cannot send packet!");
    }

    if(enetPacket->referenceCount == 0) {
        enet_packet_destroy(enetPacket);
    }
}


void NetworkManager::sendPacketToAllConnectedPeers(ENetPacketOStream& packetStream, int channel) {
    ENetPacket* enetPacket = packetStream.getPacket();

    for(ENetPeer* pCurrentPeer : peerList) {
        if(enet_peer_send(pCurrentPeer, channel, enetPacket) < 0) {
            SDL_Log("NetworkManager: Cannot send packet!");
            continue;
        }
    }

    if(enetPacket->referenceCount == 0) {
        enet_packet_destroy(enetPacket);
    }
}


void NetworkManager::sendChatMessage(const std::string& message)
{
    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_CHATMESSAGE);
    packetStream.writeString(message);

    sendPacketToAllConnectedPeers(packetStream);
}

void NetworkManager::sendChangeEventList(const ChangeEventList& changeEventList)
{
    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_CHANGEEVENTLIST);
    changeEventList.save(packetStream);

    if(bIsServer) {
        sendPacketToAllConnectedPeers(packetStream);
    } else {
        sendPacketToHost(packetStream);
    }
}

void NetworkManager::sendConfigHash(const std::string& quantBotHash, const std::string& objectDataHash, const std::string& gameVersion) {
    SDL_Log("========== SENDING CONFIG HASHES ==========");
    SDL_Log("Role: %s", bIsServer ? "SERVER" : "CLIENT");
    SDL_Log("Protocol Version: %d", NETWORK_PROTOCOL_VERSION);
    SDL_Log("Version: %s", gameVersion.c_str());
    SDL_Log("QuantBot: %s", quantBotHash.c_str());
    SDL_Log("ObjectData: %s", objectDataHash.c_str());
    
    if(bIsServer) {
        // Server sends to all clients
        SDL_Log("Sending to %d client(s)", (int)peerList.size());
        for(ENetPeer* pCurrentPeer : peerList) {
            ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
            packetStream.writeUint32(NETWORKPACKET_CONFIG_HASH);
            packetStream.writeUint32(NETWORK_PROTOCOL_VERSION);
            packetStream.writeString(gameVersion);
            packetStream.writeString(quantBotHash);
            packetStream.writeString(objectDataHash);
            sendPacketToPeer(pCurrentPeer, packetStream);
        }
    } else {
        // Client sends to server
        SDL_Log("Sending to server");
        ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_CONFIG_HASH);
        packetStream.writeUint32(NETWORK_PROTOCOL_VERSION);
        packetStream.writeString(gameVersion);
        packetStream.writeString(quantBotHash);
        packetStream.writeString(objectDataHash);
        sendPacketToHost(packetStream);
    }
    
    SDL_Log("Config sent successfully");
    SDL_Log("==========================================");
}

void NetworkManager::sendCoopMission(const GameInitSettings& settings) {
    if(!bIsServer) return;
    ENetPacketOStream packet(ENET_PACKET_FLAG_RELIABLE);
    packet.writeUint32(NETWORKPACKET_COOP_MISSION);
    settings.save(packet);
    sendPacketToAllConnectedPeers(packet);
}

std::unique_ptr<GameInitSettings> NetworkManager::takeCoopMission() {
    return std::move(pendingCoopMission);
}

void NetworkManager::sendStartGame(unsigned int timeLeft) {
    for(ENetPeer* pCurrentPeer : peerList) {
        ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_STARTGAME);

        // Clients start half a round trip earlier, but a large RTT must not wrap the
        // subtraction into a countdown of billions of milliseconds.
        const unsigned int halfRoundTrip = pCurrentPeer->roundTripTime / 2;
        const unsigned int peerTimeLeft = (halfRoundTrip >= timeLeft) ? 0u : (timeLeft - halfRoundTrip);
        packetStream.writeUint32(peerTimeLeft);

        sendPacketToPeer(pCurrentPeer, packetStream);
    }
}

void NetworkManager::sendCommandList(const CommandList& commandList) {
    ENetPacketOStream packetStream(ENET_PACKET_FLAG_UNSEQUENCED);
    packetStream.writeUint32(NETWORKPACKET_COMMANDLIST);
    packetStream.writeUint32(simulationSeed);
    commandList.save(packetStream);

    sendPacketToAllConnectedPeers(packetStream, 1);
}

void NetworkManager::sendSelectedList(const std::set<Uint32>& selectedList, int groupListIndex) {
    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_SELECTIONLIST);
    packetStream.writeUint32(simulationSeed);
    packetStream.writeSint32(groupListIndex);
    packetStream.writeUint32Set(selectedList);

    sendPacketToAllConnectedPeers(packetStream, 0);
}

int NetworkManager::getMaxPeerRoundTripTime() {
    int maxPeerRTT = 0;

    for(ENetPeer* pCurrentPeer : peerList) {
        maxPeerRTT = std::max(maxPeerRTT, (int) (pCurrentPeer->roundTripTime));
    }

    return maxPeerRTT;
}

void NetworkManager::debugNetwork(const char* fmt, ...) {
    if(settings.network.debugNetwork) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

void NetworkManager::sendClientStats(float avgFps, float simMsAvg, Uint32 queueDepth, Uint32 currentBudget, Uint32 gameCycle) {
    // Client → Host: Send performance stats (including simulation timing for post-vsync throttling)
    if(bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host trying to send client stats (should only be called by clients)");
        return;
    }

    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_CLIENTSTATS);
    packetStream.writeUint32(simulationSeed);
    packetStream.writeUint32(gameCycle);
    packetStream.writeFloat(avgFps);
    packetStream.writeFloat(simMsAvg);  // POST-VSYNC: Add simulation timing
    packetStream.writeUint32(queueDepth);
    packetStream.writeUint32(currentBudget);

    sendPacketToHost(packetStream);
}

void NetworkManager::broadcastPathBudget(size_t newBudget, Uint32 applyCycle) {
    // Host → All Clients: Broadcast budget change
    if(!bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client trying to broadcast path budget (only host can broadcast)");
        return;
    }

    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_SETPATHBUDGET);
    packetStream.writeUint32(simulationSeed);
    packetStream.writeUint32(static_cast<Uint32>(newBudget));
    packetStream.writeUint32(applyCycle);

    sendPacketToAllConnectedPeers(packetStream);
}

void NetworkManager::sendModInfoToPeer(ENetPeer* peer, const std::string& modName, const std::string& modChecksum) {
    // Host → Single Client: Send active mod info
    if(!bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client trying to send mod info (only host can send)");
        return;
    }

    SDL_Log("NetworkManager: Sending mod info to peer - mod: '%s', checksum: %s", 
            modName.c_str(), modChecksum.c_str());

    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_INFO);
    packetStream.writeString(modName);
    packetStream.writeString(modChecksum);

    sendPacketToPeer(peer, packetStream);
}

void NetworkManager::sendModInfo(const std::string& modName, const std::string& modChecksum) {
    // Host → All Clients: Send active mod info for verification
    if(!bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client trying to send mod info (only host can send)");
        return;
    }

    SDL_Log("NetworkManager: Broadcasting mod info to all clients - mod: '%s', checksum: %s", 
            modName.c_str(), modChecksum.c_str());

    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_INFO);
    packetStream.writeString(modName);
    packetStream.writeString(modChecksum);

    sendPacketToAllConnectedPeers(packetStream);
}

void NetworkManager::requestModDownload(const std::string& modName) {
    // Client → Host: Request mod files because of checksum mismatch
    if(bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host trying to request mod download (only clients can request)");
        return;
    }

    if(!ModTransferValidation::isValidModName(modName)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: refusing to request mod with an unusable name");
        return;
    }

    SDL_Log("NetworkManager: Requesting mod '%s' from host", modName.c_str());

    // Remember what we asked for: mod chunks that do not belong to this request are refused.
    modTransferState.requested = true;
    modTransferState.requestedModName = modName;
    modTransferState.inProgress = false;
    modTransferState.modData.clear();
    modTransferState.modName.clear();
    modTransferState.totalSize = 0;
    modTransferState.receivedSize = 0;

    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_REQUEST);
    packetStream.writeString(modName);

    sendPacketToHost(packetStream);
}

void NetworkManager::sendModFilesToPeer(ENetPeer* peer, const std::string& modName) {
    // Host: Package and send mod files to requesting client

    if(!ModManager::instance().isValidModName(modName)
       || modName != ModManager::instance().getActiveModName()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "NetworkManager: Rejected request for non-active or invalid mod '%s'",
                    modName.c_str());
        ENetPacketOStream completePacket(ENET_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("Invalid mod request");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    // Get mod path from ModManager
    std::string modPath = ModManager::instance().getModPath(modName);
    std::string modIniPath = modPath + "/mod.ini";
    
    SDL_Log("NetworkManager::sendModFilesToPeer - modName: '%s'", modName.c_str());
    SDL_Log("NetworkManager::sendModFilesToPeer - modPath: '%s'", modPath.c_str());
    SDL_Log("NetworkManager::sendModFilesToPeer - modIniPath: '%s'", modIniPath.c_str());
    SDL_Log("NetworkManager::sendModFilesToPeer - modPath.empty(): %d", modPath.empty() ? 1 : 0);
    SDL_Log("NetworkManager::sendModFilesToPeer - existsFile(modIniPath): %d", existsFile(modIniPath) ? 1 : 0);
    
    // Check if mod exists by looking for mod.ini (modPath is a directory, not a file)
    if(modPath.empty() || !existsFile(modIniPath)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Mod '%s' not found at path: %s (mod.ini missing)", 
                    modName.c_str(), modPath.c_str());
        
        // Send failure notification
        ENetPacketOStream completePacket(ENET_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("Mod not found on server");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    SDL_Log("NetworkManager: Packaging mod '%s' from path: %s", modName.c_str(), modPath.c_str());

    // Package mod files into a simple format:
    // [num_files:uint32][file1_name:string][file1_data:string][file2_name:string][file2_data:string]...
    std::string packagedData;
    
    // Write number of files placeholder (we'll update this)
    uint32_t numFiles = 0;
    
    std::vector<std::pair<std::string, std::string>> fileData;  // name -> content pairs

    try {
        const std::filesystem::path root = std::filesystem::weakly_canonical(modPath);
        std::vector<std::filesystem::path> paths;
        for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if(entry.is_symlink()) {
                throw std::runtime_error("mod contains a symbolic link");
            }
            if(entry.is_regular_file()) {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end(), [&](const auto& lhs, const auto& rhs) {
            return std::filesystem::relative(lhs, root).generic_string()
                < std::filesystem::relative(rhs, root).generic_string();
        });

        std::size_t contentBytes = 0;
        std::set<std::string> portablePathKeys;
        for(const auto& filePath : paths) {
            const std::string filename = std::filesystem::relative(filePath, root).generic_string();
            if(filename == ".dunecity-managed") {
                continue;
            }
            std::filesystem::path normalizedPath;
            if(!ModTransferValidation::normalizeRelativeFilePath(filename, normalizedPath)
               || normalizedPath.generic_string() != filename) {
                throw std::runtime_error("mod contains a non-portable path");
            }
            if(!portablePathKeys.insert(
                   ModTransferValidation::portablePathKey(normalizedPath)).second) {
                throw std::runtime_error("mod contains duplicate or case-colliding paths");
            }
            if(fileData.size() >= 4096) {
                throw std::runtime_error("mod exceeds transfer file-count limit");
            }
            const auto fileSize = std::filesystem::file_size(filePath);
            if(fileSize > static_cast<std::uintmax_t>(MAX_MOD_TRANSFER_SIZE)
               || contentBytes + static_cast<std::size_t>(fileSize) > MAX_MOD_TRANSFER_SIZE) {
                throw std::runtime_error("mod exceeds transfer size limit");
            }
            std::ifstream file(filePath, std::ios::binary);
            if(!file) {
                throw std::runtime_error("could not read " + filename);
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            contentBytes += content.size();
            fileData.push_back({filename, std::move(content)});
        }
    } catch(const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Could not package mod '%s': %s",
                    modName.c_str(), e.what());
        ENetPacketOStream completePacket(ENET_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("Could not package mod files");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    if(fileData.size() > UINT32_MAX) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Mod '%s' has too many files", modName.c_str());
        return;
    }
    numFiles = static_cast<uint32_t>(fileData.size());

    if(numFiles == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: No files found in mod '%s'", modName.c_str());
        
        ENetPacketOStream completePacket(ENET_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("No mod files found");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    // Build the package
    // Format: numFiles (4 bytes) + [nameLen (4 bytes) + name + dataLen (4 bytes) + data] * numFiles
    packagedData.reserve(1024 * 1024);  // Reserve 1MB initially
    
    // Write number of files
    packagedData.append(reinterpret_cast<const char*>(&numFiles), sizeof(numFiles));
    
    for(const auto& [name, content] : fileData) {
        uint32_t nameLen = static_cast<uint32_t>(name.size());
        uint32_t dataLen = static_cast<uint32_t>(content.size());
        
        packagedData.append(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        packagedData.append(name);
        packagedData.append(reinterpret_cast<const char*>(&dataLen), sizeof(dataLen));
        packagedData.append(content);
    }

    // Check size limit
    if(packagedData.size() > MAX_MOD_TRANSFER_SIZE) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Mod '%s' exceeds size limit (%zu > %d)", 
                    modName.c_str(), packagedData.size(), MAX_MOD_TRANSFER_SIZE);
        
        ENetPacketOStream completePacket(ENET_PACKET_FLAG_RELIABLE);
        completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
        completePacket.writeBool(false);
        completePacket.writeString("Mod exceeds size limit");
        sendPacketToPeer(peer, completePacket);
        return;
    }

    SDL_Log("NetworkManager: Sending mod '%s' (%zu bytes total, %u files) in chunks", 
            modName.c_str(), packagedData.size(), numFiles);

    // Send in chunks
    size_t totalSize = packagedData.size();
    size_t offset = 0;
    
    while(offset < totalSize) {
        size_t chunkSize = std::min(static_cast<size_t>(MOD_CHUNK_SIZE), totalSize - offset);
        std::string chunk = packagedData.substr(offset, chunkSize);
        
        ENetPacketOStream chunkPacket(ENET_PACKET_FLAG_RELIABLE);
        chunkPacket.writeUint32(NETWORKPACKET_MOD_CHUNK);
        chunkPacket.writeString(modName);
        chunkPacket.writeUint32(static_cast<Uint32>(totalSize));
        chunkPacket.writeUint32(static_cast<Uint32>(offset));
        chunkPacket.writeString(chunk);
        
        sendPacketToPeer(peer, chunkPacket);
        
        offset += chunkSize;
    }

    // Send completion notification
    ENetPacketOStream completePacket(ENET_PACKET_FLAG_RELIABLE);
    completePacket.writeUint32(NETWORKPACKET_MOD_COMPLETE);
    completePacket.writeBool(true);
    completePacket.writeString("Transfer complete");
    sendPacketToPeer(peer, completePacket);
    
    SDL_Log("NetworkManager: Mod transfer complete for '%s'", modName.c_str());
}

void NetworkManager::sendModAck(bool success, const std::string& modChecksum) {
    // Client → Host: Acknowledge mod sync complete
    if(bIsServer) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host trying to send mod ACK (only clients can send)");
        return;
    }

    SDL_Log("NetworkManager: Sending mod ACK to host - success: %s, checksum: %s", 
            success ? "yes" : "no", modChecksum.c_str());

    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_MOD_ACK);
    packetStream.writeBool(success);
    packetStream.writeString(modChecksum);

    sendPacketToHost(packetStream);
}

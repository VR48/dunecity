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
    // Reset game-in-progress flag for new game
    bGameInProgress = false;
    
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

    connectPeer = enet_host_connect(host, &address, 2, 0);
    if(connectPeer == nullptr) {
        THROW(std::runtime_error, "NetworkManager: No available peers for initiating a connection.");
    }

    this->playerName = playerName;

    connectPeer->data = new PeerData(connectPeer, PeerData::PeerState::WaitingForConnect);
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

                    PeerData* newPeerData = new PeerData(peer, PeerData::PeerState::WaitingForName);
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
                                peerData = new PeerData(peer, PeerData::PeerState::Connected);
                                peer->data = peerData;

                                debugNetwork("Adding '%s' to awaiting connection list\n", peerData->name.c_str());
                                awaitingConnectionList.push_back(peer);
                            }
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

                ENetPacketIStream packetStream(event.packet);

                handlePacket(peer, packetStream);
            } break;

            case ENET_EVENT_TYPE_DISCONNECT: {
                PeerData* peerData = static_cast<PeerData*>(peer->data);

                int disconnectCause = event.data;

                debugNetwork("NetworkManager: %s:%u (%s) disconnected (%d).\n", Address2String(peer->address).c_str(), peer->address.port, (peerData != nullptr) ? peerData->name.c_str() : "unknown", disconnectCause);

                if(peerData != nullptr) {
                    if(std::find(awaitingConnectionList.begin(), awaitingConnectionList.end(), peer) != awaitingConnectionList.end()) {
                        if(peerData->peerState == PeerData::PeerState::WaitingForOtherPeersToConnect) {
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

                        ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
                        packetStream.writeUint32(NETWORKPACKET_DISCONNECT);
                        packetStream.writeUint32(SDL_SwapBE32(peer->address.host));
                        packetStream.writeUint16(peer->address.port);

                        sendPacketToAllConnectedPeers(packetStream);

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

void NetworkManager::handlePacket(ENetPeer* peer, ENetPacketIStream& packetStream)
{
    try {
        Uint32 packetType = packetStream.readUint32();

        switch(packetType) {
            case NETWORKPACKET_CONNECT: {

                if(bIsServer == false) {
                    ENetAddress address;

                    address.host = SDL_SwapBE32(packetStream.readUint32());
                    address.port = packetStream.readUint16();

                    debugNetwork("Connecting to %s:%d\n", Address2String(address).c_str(), address.port);

                    ENetPeer *newPeer = enet_host_connect(host, &address, 2, 0);
                    if(newPeer == nullptr) {
                        debugNetwork("NetworkManager: No available peers for initiating a connection.");
                    } else {
                        PeerData* peerData = new PeerData(newPeer, PeerData::PeerState::WaitingForOtherPeersToConnect);
                        peerData->name = packetStream.readString();

                        newPeer->data = peerData;
                        debugNetwork("Adding '%s' to awaiting connection list\n", peerData->name.c_str());
                        awaitingConnectionList.push_back(newPeer);
                    }
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

                peerList = awaitingConnectionList;
                peerData->peerState = PeerData::PeerState::Connected;
                peerData->timeout = 0;
                awaitingConnectionList.clear();

                GameInitSettings gameInitSettings(packetStream);
                ChangeEventList changeEventList(packetStream);

                // Save the received map to the user's maps/multiplayer directory
                if(gameInitSettings.getGameType() == GameType::CustomMultiplayer && 
                   !gameInitSettings.getFiledata().empty() &&
                   !gameInitSettings.getFilename().empty()) {
                    
                    try {
                        char tmp[FILENAME_MAX];
                        if(fnkdat("maps/multiplayer/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) >= 0) {
                            std::string mapDirectory(tmp);
                            std::string mapFilename = gameInitSettings.getFilename();
                            
                            // Ensure the filename has .ini extension
                            if(mapFilename.length() < 4 || mapFilename.substr(mapFilename.length() - 4) != ".ini") {
                                mapFilename += ".ini";
                            }
                            
                            std::string fullPath = mapDirectory + mapFilename;
                            
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
                        } else {
                            SDL_Log("NetworkManager: Failed to get maps/multiplayer directory path");
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
                if(pOnReceiveChatMessage) {
                    pOnReceiveChatMessage(peerData->name, message);
                }
            } break;

            case NETWORKPACKET_CHANGEEVENTLIST: {
                ChangeEventList changeEventList(packetStream);

                if(pOnReceiveChangeEventList) {
                    pOnReceiveChangeEventList(changeEventList);
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

            case NETWORKPACKET_STARTGAME: {
                Uint32 timeLeft = packetStream.readUint32();

                if(pOnStartGame) {
                    pOnStartGame(timeLeft);
                }
            } break;

            case NETWORKPACKET_COMMANDLIST: {
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
                PeerData* peerData = static_cast<PeerData*>(peer->data);
                if(!peerData) {
                    break;
                }

                int groupListIndex = packetStream.readSint32();
                std::set<Uint32> selectedList = packetStream.readUint32Set();

                if(pOnReceiveSelectionList) {
                    pOnReceiveSelectionList(peerData->name, selectedList, groupListIndex);
                }
            } break;

            case NETWORKPACKET_CLIENTSTATS: {
                // Host receives client performance stats (including simulation timing)
                if(!bIsServer) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client received CLIENTSTATS packet (should only be sent to host)");
                    break;
                }

                Uint32 gameCycle = packetStream.readUint32();
                float avgFps = packetStream.readFloat();
                float simMsAvg = packetStream.readFloat();  // POST-VSYNC: Read simulation timing
                Uint32 queueDepth = packetStream.readUint32();
                Uint32 currentBudget = packetStream.readUint32();

                // Extract client ID from peer data
                // For now, we'll use the peer's address hash as a unique ID
                Uint32 clientId = peer->address.host ^ peer->address.port;

                if(pOnReceiveClientStats) {
                    pOnReceiveClientStats(clientId, gameCycle, avgFps, simMsAvg, queueDepth, currentBudget);
                }
            } break;

            case NETWORKPACKET_SETPATHBUDGET: {
                // Client receives budget change order from host
                if(bIsServer) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host received SETPATHBUDGET packet (should only be sent to clients)");
                    break;
                }

                Uint32 newBudget = packetStream.readUint32();
                Uint32 applyCycle = packetStream.readUint32();

                if(pOnReceiveSetPathBudget) {
                    pOnReceiveSetPathBudget(newBudget, applyCycle);
                }
            } break;

            case NETWORKPACKET_MOD_INFO: {
                // Client receives mod info from host
                if(bIsServer) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host received MOD_INFO packet (should only be sent to clients)");
                    break;
                }

                std::string modName = packetStream.readString();
                std::string modChecksum = packetStream.readString();

                SDL_Log("NetworkManager: Received mod info from host - mod: '%s', checksum: %s", 
                        modName.c_str(), modChecksum.c_str());

                if(pOnReceiveModInfo) {
                    pOnReceiveModInfo(modName, modChecksum);
                }
            } break;

            case NETWORKPACKET_MOD_REQUEST: {
                // Host receives mod download request from client
                if(!bIsServer) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client received MOD_REQUEST packet (should only be sent to host)");
                    break;
                }

                std::string requestedModName = packetStream.readString();
                SDL_Log("NetworkManager: Client requested mod download for '%s'", requestedModName.c_str());

                // Use the peer that sent this request directly (the 'peer' parameter from handlePacket)
                if(peer == nullptr) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: No peer context for mod request");
                    break;
                }

                // Package and send the mod files to the requesting peer
                sendModFilesToPeer(peer, requestedModName);
            } break;

            case NETWORKPACKET_MOD_CHUNK: {
                // Client receives mod file chunk from host
                if(bIsServer) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host received MOD_CHUNK packet (should only be sent to clients)");
                    break;
                }

                std::string modName = packetStream.readString();
                Uint32 totalSize = packetStream.readUint32();
                Uint32 chunkOffset = packetStream.readUint32();
                std::string chunkData = packetStream.readString();

                // Security: Validate totalSize against maximum allowed
                if(totalSize > MAX_MOD_TRANSFER_SIZE) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, 
                        "NetworkManager: Mod transfer size %u exceeds limit %d - aborting", 
                        totalSize, MAX_MOD_TRANSFER_SIZE);
                    modTransferState.inProgress = false;
                    if(pOnModDownloadComplete) {
                        pOnModDownloadComplete(false, "Mod exceeds size limit");
                    }
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
                }

                // Security: Validate chunk offset matches expected position (enforce in-order)
                if(chunkOffset != modTransferState.receivedSize) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, 
                        "NetworkManager: Chunk offset mismatch - expected %zu, got %u. Aborting transfer.", 
                        modTransferState.receivedSize, chunkOffset);
                    modTransferState.inProgress = false;
                    if(pOnModDownloadComplete) {
                        pOnModDownloadComplete(false, "Out-of-order mod chunk");
                    }
                    modTransferState.modData.clear();
                    modTransferState.modName.clear();
                    modTransferState.totalSize = 0;
                    modTransferState.receivedSize = 0;
                    break;
                }

                // Security: Check that adding this chunk won't exceed totalSize
                if(modTransferState.receivedSize + chunkData.size() > totalSize) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, 
                        "NetworkManager: Chunk would exceed total size - aborting");
                    modTransferState.inProgress = false;
                    if(pOnModDownloadComplete) {
                        pOnModDownloadComplete(false, "Invalid chunk size");
                    }
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
                // Client receives mod transfer complete notification
                if(bIsServer) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Host received MOD_COMPLETE packet (should only be sent to clients)");
                    break;
                }

                bool success = packetStream.readBool();
                std::string message = packetStream.readString();

                SDL_Log("NetworkManager: Mod transfer complete - success: %s, message: %s", 
                        success ? "yes" : "no", message.c_str());

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
            } break;

            case NETWORKPACKET_MOD_ACK: {
                // Host receives mod sync acknowledgment from client
                if(!bIsServer) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "NetworkManager: Client received MOD_ACK packet (should only be sent to host)");
                    break;
                }

                bool success = packetStream.readBool();
                std::string modChecksum = packetStream.readString();

                // Find player name for this peer
                std::string playerName = "Unknown";
                if (peer != nullptr) {
                    if (auto* peerData = static_cast<PeerData*>(peer->data)) {
                        playerName = peerData->name;
                    } else {
                        char nameBuf[64];
                        snprintf(nameBuf, sizeof(nameBuf), "%u:%u", peer->address.host, peer->address.port);
                        playerName = nameBuf;
                    }
                }

                SDL_Log("NetworkManager: Received mod ACK from '%s' - success: %s, checksum: %s",
                        playerName.c_str(), success ? "yes" : "no", modChecksum.c_str());

                if(pOnReceiveModAck) {
                    pOnReceiveModAck(playerName, success, modChecksum);
                }
            } break;
            
            case NETWORKPACKET_KEEPALIVE: {
                // NAT keep-alive ping - just receiving it is enough to keep the NAT mapping alive
                // The reliable packet triggers ACKs which count as bidirectional traffic
                // No action needed, packet is silently consumed
            } break;

            default: {
                SDL_Log("NetworkManager: Unknown packet type %d", packetType);
            };
        }

    } catch (InputStream::eof&) {
        SDL_Log("NetworkManager: Received packet is too small");
        return;
    } catch (std::exception& e) {
        SDL_Log("NetworkManager: %s", e.what());
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

void NetworkManager::sendStartGame(unsigned int timeLeft) {
    for(ENetPeer* pCurrentPeer : peerList) {
        ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
        packetStream.writeUint32(NETWORKPACKET_STARTGAME);

        packetStream.writeUint32(timeLeft - pCurrentPeer->roundTripTime/2);

        sendPacketToPeer(pCurrentPeer, packetStream);
    }
}

void NetworkManager::sendCommandList(const CommandList& commandList) {
    ENetPacketOStream packetStream(ENET_PACKET_FLAG_UNSEQUENCED);
    packetStream.writeUint32(NETWORKPACKET_COMMANDLIST);
    commandList.save(packetStream);

    sendPacketToAllConnectedPeers(packetStream, 1);
}

void NetworkManager::sendSelectedList(const std::set<Uint32>& selectedList, int groupListIndex) {
    ENetPacketOStream packetStream(ENET_PACKET_FLAG_RELIABLE);
    packetStream.writeUint32(NETWORKPACKET_SELECTIONLIST);
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

    SDL_Log("NetworkManager: Requesting mod '%s' from host", modName.c_str());

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

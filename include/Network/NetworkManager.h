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

#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <Network/ENetPacketIStream.h>
#include <Network/ENetPacketOStream.h>
#include <Network/ChangeEventList.h>
#include <Network/CommandList.h>

#include <Network/LANGameFinderAndAnnouncer.h>
#include <Network/MetaServerClient.h>
#include <Network/UPnPManager.h>

#include <misc/string_util.h>
#include <misc/SDL2pp.h>

#include <enet/enet.h>
#include <string>
#include <list>
#include <vector>
#include <functional>
#include <stdarg.h>

#define NETWORKDISCONNECT_QUIT              1
#define NETWORKDISCONNECT_TIMEOUT           2
#define NETWORKDISCONNECT_PLAYER_EXISTS     3
#define NETWORKDISCONNECT_GAME_FULL         4
#define NETWORKDISCONNECT_PROTOCOL_MISMATCH 5

#define NETWORKPACKET_UNKNOWN               0
#define NETWORKPACKET_CONNECT               1
#define NETWORKPACKET_DISCONNECT            2
#define NETWORKPACKET_PEER_CONNECTED        3
#define NETWORKPACKET_SENDGAMEINFO          4
#define NETWORKPACKET_SENDNAME              5
#define NETWORKPACKET_CHATMESSAGE           6
#define NETWORKPACKET_CHANGEEVENTLIST       7
#define NETWORKPACKET_STARTGAME             8
#define NETWORKPACKET_COMMANDLIST           9
#define NETWORKPACKET_SELECTIONLIST         10
#define NETWORKPACKET_CONFIG_HASH           11
#define NETWORKPACKET_SETPATHBUDGET         12  // Phase 1.4: Budget negotiation
#define NETWORKPACKET_CLIENTSTATS           13  // Multiplayer: Client performance stats
#define NETWORKPACKET_MOD_INFO              14  // Host -> Client: mod name + checksums
#define NETWORKPACKET_MOD_REQUEST           15  // Client -> Host: request mod files
#define NETWORKPACKET_MOD_CHUNK             16  // Host -> Client: mod file chunk
#define NETWORKPACKET_MOD_COMPLETE          17  // Host -> Client: transfer complete
#define NETWORKPACKET_MOD_ACK               18  // Client -> Host: acknowledge mod sync complete
#define NETWORKPACKET_KEEPALIVE             19  // Periodic ping to keep NAT mappings alive

// Network protocol version - increment when packet formats change
// Version 2: Added simMsAvg to NETWORKPACKET_CLIENTSTATS (5 fields instead of 4)
// Version 3: Added mod transfer packets (MOD_INFO, MOD_REQUEST, MOD_CHUNK, MOD_COMPLETE)
// Version 4: Fixed nine-house deterministic state and versioned visibility storage
#define NETWORK_PROTOCOL_VERSION            4

/**
 * Reject an incompatible config-hash handshake and dispatch its disconnect cause.
 * Returns true when the peer must be rejected.
 */
template<typename DisconnectFunction>
inline bool rejectIncompatibleNetworkProtocol(Uint32 peerProtocolVersion, DisconnectFunction&& disconnect) {
    if(peerProtocolVersion == NETWORK_PROTOCOL_VERSION) return false;
    disconnect(NETWORKDISCONNECT_PROTOCOL_MISMATCH);
    return true;
}

template<typename DisconnectFunction>
inline bool rejectIncompatibleGameVersion(const std::string& peer, const std::string& local, DisconnectFunction&& disconnect) {
    if (peer == local) return false;
    disconnect(NETWORKDISCONNECT_PROTOCOL_MISMATCH);
    return true;
}

// Mod transfer limits
#define MAX_MOD_TRANSFER_SIZE   (10 * 1024 * 1024)  // 10MB max mod size
#define MOD_CHUNK_SIZE          (64 * 1024)          // 64KB per chunk

#define AWAITING_CONNECTION_TIMEOUT     5000

class GameInitSettings;

class NetworkManager {
public:
    NetworkManager(int port, const std::string& metaserver);
    NetworkManager(const NetworkManager& o) = delete;
    ~NetworkManager();

    bool isServer() const { return bIsServer; };
    bool isLANServer() const { return bLANServer; };

    void startServer(bool bLANServer, const std::string& serverName, const std::string& playerName, GameInitSettings* pGameInitSettings, int numPlayers, int maxPlayers);
    void updateServer(int numPlayers);
    void stopAnnouncing();  // Stops lobby announcement but keeps bIsServer = true
    void stopServer();

    void connect(const std::string& hostname, int port, const std::string& playerName);
    void connect(ENetAddress address, const std::string& playerName);

    void disconnect();

    void update();

    void sendChatMessage(const std::string& message);

    void sendChangeEventList(const ChangeEventList& changeEventList);

    void sendConfigHash(const std::string& quantBotHash, const std::string& objectDataHash, const std::string& gameVersion);

    void sendStartGame(unsigned int timeLeft);

    void sendCommandList(const CommandList& commandList);

    void sendSelectedList(const std::set<Uint32>& selectedList, int groupListIndex = -1);

    std::list<std::string> getConnectedPeers() const {
        std::list<std::string> peerNameList;

        for(const ENetPeer* pPeer : peerList) {
            PeerData* peerData = static_cast<PeerData*>(pPeer->data);
            if(peerData != nullptr) {
                peerNameList.push_back(peerData->name);
            }
        }

        return peerNameList;
    }

    int getMaxPeerRoundTripTime();

    LANGameFinderAndAnnouncer* getLANGameFinderAndAnnouncer() {
        return pLANGameFinderAndAnnouncer.get();
    };

    MetaServerClient* getMetaServerClient() {
        return pMetaServerClient.get();
    };

    /**
        Sets the function that should be called when a chat message is received
        \param  pOnReceiveChatMessage   function to call on new chat message
    */
    inline void setOnReceiveChatMessage(std::function<void (const std::string&, const std::string&)> pOnReceiveChatMessage) {
        this->pOnReceiveChatMessage = pOnReceiveChatMessage;
    }

    /**
        Sets the function that should be called when game infos are received after connecting to the server.
        \param  pOnReceiveGameInfo  function to call on receive
    */
    inline void setOnReceiveGameInfo(std::function<void (const GameInitSettings&, const ChangeEventList&)> pOnReceiveGameInfo) {
        this->pOnReceiveGameInfo = pOnReceiveGameInfo;
    }


    /**
        Sets the function that should be called when a change event is received.
        \param  pOnReceiveChangeEventList   function to call on receive
    */
    inline void setOnReceiveChangeEventList(std::function<void (const ChangeEventList&)> pOnReceiveChangeEventList) {
        this->pOnReceiveChangeEventList = pOnReceiveChangeEventList;
    }


    /**
        Sets the function that should be called when a peer disconnects.
        \param  pOnPeerDisconnected function to call on disconnect
    */
    inline void setOnPeerDisconnected(std::function<void (const std::string&, bool, int)> pOnPeerDisconnected) {
        this->pOnPeerDisconnected = pOnPeerDisconnected;
    }

    /**
        Sets the function that can be used to retreive all house/player changes to get the current state
        \param  pGetGameInitSettingsCallback    function to call
    */
    inline void setGetChangeEventListForNewPlayerCallback(std::function<ChangeEventList (const std::string&)> pGetChangeEventListForNewPlayerCallback) {
        this->pGetChangeEventListForNewPlayerCallback = pGetChangeEventListForNewPlayerCallback;
    }

    /**
        Sets the function that should be called when the game is about to start and the time (in ms) left is received
        \param  pOnStartGame    function to call on receive
    */
    inline void setOnStartGame(std::function<void (unsigned int)> pOnStartGame) {
        this->pOnStartGame = pOnStartGame;
    }

    /**
        Sets the function that should be called when a command list is received.
        \param  pOnReceiveCommandList   function to call on receive
    */
    inline void setOnReceiveCommandList(std::function<void (const std::string&, const CommandList&)> pOnReceiveCommandList) {
        this->pOnReceiveCommandList = pOnReceiveCommandList;
    }

    /**
        Sets the function that should be called when a selection list is received.
        \param  pOnReceiveSelectionList function to call on receive
    */
    inline void setOnReceiveSelectionList(std::function<void (const std::string&, const std::set<Uint32>&, int)> pOnReceiveSelectionList) {
        this->pOnReceiveSelectionList = pOnReceiveSelectionList;
    }

    /**
        Sets the function that should be called when a config mismatch is detected.
        \param  pOnConfigMismatch   function to call with error message
    */
    inline void setOnConfigMismatch(std::function<void (const std::string&)> pOnConfigMismatch) {
        this->pOnConfigMismatch = pOnConfigMismatch;
    }

    /**
        Sets the function that should be called when client performance stats are received (host only).
        \param  pOnReceiveClientStats   function to call on receive
    */
    inline void setOnReceiveClientStats(std::function<void (Uint32, Uint32, float, float, Uint32, Uint32)> pOnReceiveClientStats) {
        this->pOnReceiveClientStats = pOnReceiveClientStats;
    }

    /**
        Sets the function that should be called when a path budget change order is received (client only).
        \param  pOnReceiveSetPathBudget function to call on receive
    */
    inline void setOnReceiveSetPathBudget(std::function<void (size_t, Uint32)> pOnReceiveSetPathBudget) {
        this->pOnReceiveSetPathBudget = pOnReceiveSetPathBudget;
    }

    /**
        Sends client performance stats to host (client → host).
        \param  avgFps          Average FPS (legacy metric)
        \param  simMsAvg        Average simulation time per tick in ms (primary metric)
        \param  queueDepth      Pathfinding queue depth
        \param  currentBudget   Current path budget (for validation)
        \param  gameCycle       Current game cycle
    */
    void sendClientStats(float avgFps, float simMsAvg, Uint32 queueDepth, Uint32 currentBudget, Uint32 gameCycle);

    /**
        Broadcasts a path budget change to all clients (host → all clients).
        \param  newBudget       New path budget
        \param  applyCycle      Game cycle to apply the change
    */
    void broadcastPathBudget(size_t newBudget, Uint32 applyCycle);

    // === Mod Transfer Methods ===

    /**
        Send mod info to a specific peer (host only).
        Called when a new client connects to inform them of the active mod.
        \param  peer            The peer to send to
        \param  modName         Name of the active mod
        \param  modChecksum     Combined mod checksum
    */
    void sendModInfoToPeer(ENetPeer* peer, const std::string& modName, const std::string& modChecksum);

    /**
        Send mod info to all connected clients (host only).
        \param  modName         Name of the active mod
        \param  modChecksum     Combined mod checksum
    */
    void sendModInfo(const std::string& modName, const std::string& modChecksum);

    /**
        Request mod files from host (client only).
        \param  modName         Name of the mod to download
    */
    void requestModDownload(const std::string& modName);

    /**
        Send mod sync acknowledgment to host (client only).
        \param  success         Whether mod sync was successful
        \param  modChecksum     The client's mod checksum after sync
    */
    void sendModAck(bool success, const std::string& modChecksum);

    /**
        Sets the function that should be called when mod info is received.
        \param  pOnReceiveModInfo   function(modName, modChecksum) to call
    */
    inline void setOnReceiveModInfo(std::function<void (const std::string&, const std::string&)> pOnReceiveModInfo) {
        this->pOnReceiveModInfo = pOnReceiveModInfo;
    }

    /**
        Sets the function that should be called when mod download progress updates.
        \param  pOnModDownloadProgress  function(bytesReceived, totalBytes) to call
    */
    inline void setOnModDownloadProgress(std::function<void (size_t, size_t)> pOnModDownloadProgress) {
        this->pOnModDownloadProgress = pOnModDownloadProgress;
    }

    /**
        Sets the function that should be called when mod download completes.
        \param  pOnModDownloadComplete  function(success, errorMsg) to call
    */
    inline void setOnModDownloadComplete(std::function<void (bool, const std::string&)> pOnModDownloadComplete) {
        this->pOnModDownloadComplete = pOnModDownloadComplete;
    }

    /**
        Sets the function that should be called when host receives mod ACK from client.
        \param  pOnReceiveModAck  function(playerName, success, modChecksum) to call
    */
    inline void setOnReceiveModAck(std::function<void (const std::string&, bool, const std::string&)> pOnReceiveModAck) {
        this->pOnReceiveModAck = pOnReceiveModAck;
    }

private:
    static void debugNetwork(PRINTF_FORMAT_STRING const char* fmt, ...) PRINTF_VARARG_FUNC(1);

    void sendPacketToHost(ENetPacketOStream& packetStream, int channel = 0);

    void sendPacketToPeer(ENetPeer* peer, ENetPacketOStream& packetStream, int channel = 0);
    
    /**
        Package and send mod files to a peer in chunks.
        \param  peer        The peer to send to
        \param  modName     Name of the mod to send
    */
    void sendModFilesToPeer(ENetPeer* peer, const std::string& modName);

    void sendPacketToAllConnectedPeers(ENetPacketOStream& packetStream, int channel = 0);

    void handlePacket(ENetPeer* peer, ENetPacketIStream& packetStream);

    class PeerData {
    public:
        enum class PeerState {
            WaitingForConnect,
            WaitingForName,
            ReadyForOtherPeersToConnect,
            WaitingForOtherPeersToConnect,
            Connected
        };


        PeerData(ENetPeer* pPeer, PeerState peerState)
         : pPeer(pPeer), peerState(peerState), timeout(0)  {
        }


        ENetPeer*               pPeer;

        PeerState               peerState;
        Uint32                  timeout;

        std::string             name;
        std::string             gameVersion;
        std::string             quantBotConfigHash;
        std::string             objectDataHash;
        std::list<ENetPeer*>    notYetConnectedPeers;
    };

    ENetHost* host = nullptr;
    bool bIsServer = false;
    bool bLANServer = false;
    bool bGameInProgress = false;  // Set true when game starts - disables lobby-only features
    GameInitSettings* pGameInitSettings = nullptr;
    int numPlayers = 0;
    int maxPlayers = 0;

    std::string playerName;

    ENetPeer*   connectPeer = nullptr;

    std::list<ENetPeer*> peerList;

    std::list<ENetPeer*> awaitingConnectionList;

    std::function<void (const std::string&, const std::string&)>            pOnReceiveChatMessage;
    std::function<void (const GameInitSettings&, const ChangeEventList&)>   pOnReceiveGameInfo;
    std::function<void (const ChangeEventList&)>                            pOnReceiveChangeEventList;
    std::function<void (const std::string&, bool, int)>                     pOnPeerDisconnected;
    std::function<ChangeEventList (const std::string&)>                     pGetChangeEventListForNewPlayerCallback;
    std::function<void (unsigned int)>                                      pOnStartGame;
    std::function<void (const std::string&, const CommandList&)>            pOnReceiveCommandList;
    std::function<void (const std::string&, const std::set<Uint32>&, int)>  pOnReceiveSelectionList;
    std::function<void (const std::string&)>                                 pOnConfigMismatch;
    std::function<void (Uint32, Uint32, float, float, Uint32, Uint32)>     pOnReceiveClientStats;      // Host: (clientId, gameCycle, avgFps, simMsAvg, queueDepth, currentBudget)
    std::function<void (size_t, Uint32)>                                     pOnReceiveSetPathBudget;    // Client: (newBudget, applyCycle)
    std::function<void (const std::string&, const std::string&)>            pOnReceiveModInfo;          // Client: (modName, modChecksum)
    std::function<void (size_t, size_t)>                                     pOnModDownloadProgress;     // Client: (bytesReceived, totalBytes)
    std::function<void (bool, const std::string&)>                          pOnModDownloadComplete;     // Client: (success, errorMsg)
    std::function<void (const std::string&, bool, const std::string&)>      pOnReceiveModAck;           // Host: (playerName, success, modChecksum)

    // Mod transfer state (for chunked transfer)
    struct ModTransferState {
        std::string modName;
        std::string modData;
        size_t totalSize = 0;
        size_t receivedSize = 0;
        bool inProgress = false;
    };
    ModTransferState modTransferState;

    std::unique_ptr<LANGameFinderAndAnnouncer>  pLANGameFinderAndAnnouncer = nullptr;
    std::unique_ptr<MetaServerClient>           pMetaServerClient = nullptr;
    std::unique_ptr<UPnPManager>                pUPnPManager = nullptr;
    bool                                        upnpPortMapped = false;
    uint16_t                                    upnpMappedPort = 0;
    Uint32                                      upnpLeaseStartTime = 0;
    static constexpr int                        UPNP_LEASE_DURATION = 3600;      // 1 hour lease
    static constexpr int                        UPNP_RENEWAL_MARGIN = 300;       // Renew 5 min before expiry
    
    // NAT keep-alive: send reliable ping every 10 seconds to prevent NAT timeout
    Uint32                                      lastKeepAliveTime = 0;
    static constexpr int                        KEEPALIVE_INTERVAL_MS = 10000;   // 10 seconds
    
    // NAT Hole Punch: Non-blocking state machine for host-side punching
    struct PendingPunch {
        std::string clientId;
        std::string clientIP;
        uint16_t clientPort = 0;
        Uint32 punchAtTime = 0;       // SDL_GetTicks() when to start punching
        int packetsRemaining = 0;     // Packets left to send
        Uint32 lastPacketTime = 0;    // For pacing packets
    };
    std::vector<PendingPunch>                   pendingPunches;
    Uint32                                      lastPunchPollTime = 0;
    static constexpr int                        PUNCH_POLL_INTERVAL_MS = 1000;   // Poll every 1s
    static constexpr int                        PUNCH_DELAY_MS = 2000;           // Delay before punching
    static constexpr int                        PUNCH_PACKET_COUNT = 10;         // Packets per punch
    static constexpr int                        PUNCH_PACKET_INTERVAL_MS = 50;   // Interval between packets

public:
    /**
     * Get UPnP status information
     */
    bool isUPnPAvailable() const { return pUPnPManager && pUPnPManager->isAvailable(); }
    bool isUPnPPortMapped() const { return upnpPortMapped; }
    std::string getUPnPStatus() const { return pUPnPManager ? pUPnPManager->getStatusString() : "Not initialized"; }
    std::string getExternalIPAddress() const { return pUPnPManager ? pUPnPManager->getExternalIPAddress() : ""; }
    
    /**
     * NAT Hole Punch: Send UDP punch packets to an address to create NAT mappings.
     * @param targetIP   Target IP address
     * @param targetPort Target port
     * @param count      Number of packets to send (default: 5)
     * @param intervalMs Interval between packets in ms (default: 50)
     */
    void sendHolePunchPackets(const std::string& targetIP, uint16_t targetPort, int count = 5, int intervalMs = 50);
    
    /**
     * Perform STUN query to discover external IP:port.
     * SAFETY: Only call when no ENet peers exist (peerList empty).
     * @return External port if successful, 0 on failure
     */
    uint16_t performStunQuery();
    
    /**
     * Perform STUN query and return both external IP and port.
     * SAFETY: Only call when no ENet peers exist (peerList empty).
     * @param outIP  Will be set to the external IP if successful
     * @param outPort Will be set to the external port if successful
     * @return true if successful, false on failure
     */
    bool performStunQueryFull(std::string& outIP, uint16_t& outPort);
    
    /**
     * Get the ENet host (for STUN queries)
     */
    ENetHost* getHost() const { return host; }
};

#endif // NETWORKMANAGER_H

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
#include <Network/NetworkPacketTypes.h>
#include <Network/NetworkPacketPolicy.h>
#include <Network/GamePayloadRouter.h>
#include <Network/RoomRelayClient.h>

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

// rejectIncompatibleNetworkProtocol() and rejectIncompatibleGameVersion() moved to
// Network/NetworkPacketPolicy.h so both transports can use them; they are still reachable
// through this header.

#define AWAITING_CONNECTION_TIMEOUT     5000

class GameInitSettings;

class NetworkManager {
public:
    /// Which transport carries this session.
    enum class Transport {
        EnetMesh,   ///< legacy UDP mesh with LAN discovery, the metaserver and UPnP
        RoomRelay   ///< crossplay through the room relay over one outbound WebSocket
    };

    /// Legacy ENet mesh session.
    NetworkManager(int port, const std::string& metaserver);

    /**
        Room relay session.

        Deliberately creates no ENet host and starts no LAN discovery, metaserver thread or UPnP
        mapping. None of those work in a browser, and a relay session has no use for any of them
        anywhere: the only socket it opens is one outbound WebSocket to the relay.
    */
    explicit NetworkManager(Transport transport);

    NetworkManager(const NetworkManager& o) = delete;
    ~NetworkManager();

    bool isServer() const { return bIsServer; };
    bool isLANServer() const { return bLANServer; };

    void setPublicRelayRoom(bool value) { publicRelayRoom = value; }
    bool isPublicRelayRoom() const { return publicRelayRoom; }
    bool isRelaySession() const { return transport == Transport::RoomRelay; }

    /**
        Relay v1 carries bundled, matching content only: mod transfer packets are refused by the
        relay, so the lobby must not wait for mod acknowledgements on that transport.
    */
    bool supportsModTransfer() const { return transport == Transport::EnetMesh; }

    /**
        Starts the relay session with a grant that HTTPS admission already produced.
        \param  config  session parameters
        \param  error   set to a player-facing reason on failure
        \return true if the session is connecting
    */
    bool startRelaySession(const RoomRelayClient::Config& config, std::string& error);

    /// The relay session, or nullptr on the ENet transport.
    RoomRelayClient* getRelayClient() { return pRelayClient.get(); }
    const RoomRelayClient* getRelayClient() const { return pRelayClient.get(); }

    /// The room code a player can pass to a friend, or empty when there is no relay session.
    std::string getRoomCode() const {
        return pRelayClient ? pRelayClient->roomCode() : std::string();
    }

    /// The outcome of comparing every relay peer's content with ours.
    enum class ContentCheck {
        Match,          ///< everybody reported, and everybody agrees
        AwaitingPeer,   ///< somebody has not reported yet; try again shortly
        Mismatch        ///< somebody reported content this transport cannot reconcile
    };

    /**
        Checks every relay peer's reported content against ours, for the host to call before it
        starts a match.

        The per-message check happens when a peer's hashes arrive, but the lobby lets the host
        pick a different mod afterwards, so the comparison has to be made again against what is
        actually about to be played. A peer that has not reported yet is reported separately from
        one that disagrees: waiting is recoverable, disagreeing is not.

        \param  quantBotHash    our current QuantBot config hash
        \param  objectDataHash  our current object data hash
        \param  gameVersion     our build version
        \param  reason          set to a player-facing explanation unless everybody matches
    */
    ContentCheck checkRelayContent(const std::string& quantBotHash,
                                   const std::string& objectDataHash,
                                   const std::string& gameVersion,
                                   std::string& reason) const;

    /**
        Sends a bounded diagnostic to the room. Carried in the relay envelope, never as a game
        packet, so the ENet wire format and NETWORK_PROTOCOL_VERSION are untouched.
        \return false if there is no relay session or the payload was refused
    */
    bool sendRelayDiagnostic(RoomRelay::DiagnosticKind kind, const std::uint8_t* payload,
                             std::size_t length);

    /**
        Sets the function called when a diagnostic arrives from another peer.
        \param  callback    function(senderName, kind, payload, length)
    */
    void setOnReceiveRelayDiagnostic(
        std::function<void (const std::string&, std::uint8_t, const std::uint8_t*, std::size_t)> callback) {
        pOnReceiveRelayDiagnostic = std::move(callback);
    }

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
    void sendCoopMission(const GameInitSettings& settings);
    std::unique_ptr<GameInitSettings> takeCoopMission();

    /**
        Called on every peer (host and clients) when the match starts. Besides the simulation
        seed this freezes the session phase: lobby-only packets, including renames and mod
        transfers, stop being accepted from this point on.
        \param  seed    the shared simulation seed
    */
    void beginSimulation(Uint32 seed);
    void sendCommandList(const CommandList& commandList);

    void sendSelectedList(const std::set<Uint32>& selectedList, int groupListIndex = -1);

    std::list<std::string> getConnectedPeers() const {
        std::list<std::string> peerNameList;

        if(pRelayClient) {
            for(const RoomRelayClient::Peer& peer : pRelayClient->peers()) {
                peerNameList.push_back(peer.name);
            }
            return peerNameList;
        }

        for(const ENetPeer* pPeer : peerList) {
            PeerData* peerData = static_cast<PeerData*>(pPeer->data);
            if(peerData != nullptr) {
                peerNameList.push_back(peerData->name);
            }
        }

        return peerNameList;
    }

    /// Largest ENet peer RTT; for relay sessions this is only the local heartbeat RTT.
    /// Relay heartbeat timing does not measure or bound the complete peer delivery path.
    int getMaxPeerRoundTripTime();

    /// Local relay heartbeat RTT in milliseconds; zero before the first sample or without a relay.
    Uint32 getRelayServerRoundTripTimeMs() const;
    bool isRelayHttpPollingSession() const;

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
        Sets the function that should be called when a change event is received. The first
        argument is the bound peer name of the connection it arrived on, so the lobby can
        authorize the request against the seat that connection holds.
        \param  pOnReceiveChangeEventList   function(senderName, changeEventList) to call
    */
    inline void setOnReceiveChangeEventList(std::function<void (const std::string&, const ChangeEventList&)> pOnReceiveChangeEventList) {
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
    bool publicRelayRoom = false;
    static void debugNetwork(PRINTF_FORMAT_STRING const char* fmt, ...) PRINTF_VARARG_FUNC(1);

    /// Bundles the game's callbacks for the shared payload handling.
    NetworkSessionCallbacks sessionCallbacks() const;

    /// Drives the relay session and drains its event queue. Called from update().
    void updateRelaySession();

    /**
        Applies one game payload received over the relay.

        Takes the sender's id rather than a reference to its record: handling a payload runs the
        game's own callbacks, and anything that reaches the session again can add or remove a
        peer, which moves the vector a reference would point into.
    */
    void handleRelayGamePayload(std::uint32_t peerId, const std::uint8_t* payload,
                                std::size_t length);

    /**
        Hands one relay message to the transport.
        \param  packetStream    the packet to send; its ENetPacket is consumed
        \param  channel         0 or 1
        \param  recipient       0 for every other peer in the room, or a relay peer id
    */
    void sendPacketOverRelay(ENetPacketOStream& packetStream, int channel,
                             std::uint32_t recipient);

    /// The relay peer id of the designated host, or 0 if this process is the host.
    std::uint32_t relayHostPeerId() const;

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

    /**
        Hands a mesh packet to the payload handling that both transports share.
        \return true if this packet id belongs to the shared set
    */
    bool routeSharedPayload(ENetPeer* peer, Uint32 packetType, ENetPacketIStream& packetStream);

    class PeerData;

    /**
        Applies the central admission policy to one inbound packet.
        \param  peer        the connection the packet arrived on
        \param  packetType  the packet id that was just read
        \return true if the packet may be interpreted
    */
    bool admitPacket(ENetPeer* peer, Uint32 packetType);

    /**
        Records a rejected or malformed packet for this peer, throttles the log line and
        disconnects peers that keep sending garbage.
        \param  peer    the offending connection
        \param  reason  short description for the log
    */
    void noteRejectedPacket(ENetPeer* peer, const char* reason);

    /**
        Requests a disconnect once and marks the connection, so further packets from it are
        dropped without being parsed, counted or logged again.
        \param  peer    the connection to drop
        \param  reason  short description for the single log line
    */
    void beginPeerDisconnect(ENetPeer* peer, const char* reason);

    /**
        Accounts the raw size of an inbound packet against this peer's byte budget.
        \param  peer        the connection the packet arrived on
        \param  byteCount   size of the packet as delivered by ENet
        \return true if the packet may be parsed
    */
    bool acceptIncomingBytes(ENetPeer* peer, std::size_t byteCount);

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
        bool                    bNameAssigned = false;  ///< identity is bound once and never re-bound
        Uint32                  clientId = 0;           ///< stable per-connection id (not an address hash)
        std::string             gameVersion;
        std::string             quantBotConfigHash;
        std::string             objectDataHash;
        std::list<ENetPeer*>    notYetConnectedPeers;

        // Abuse accounting: a legitimate peer never trips these.
        NetworkPacketPolicy::RefusalCounter refusals;
        NetworkPacketPolicy::RateWindow     packetWindow;
        NetworkPacketPolicy::RateWindow     byteWindow;
        Uint32                  lastRejectLogTime = 0;
    };

    /**
        Allocates peer state and gives the connection a stable local id.
        \param  peer        the connection the state belongs to
        \param  peerState   the initial handshake state
        \return the new peer state (ownership stays with peer->data)
    */
    PeerData* createPeerData(ENetPeer* peer, PeerData::PeerState peerState);

    // The path budget, start-game countdown and chat length bounds moved to
    // src/Network/GamePayloadRouter.cpp with the payload handling they belong to. Two copies of
    // one limit is how limits drift apart.

    /// Mod checksums are 16 hex characters; this leaves room without allowing junk.
    static constexpr std::size_t MAX_MOD_CHECKSUM_LENGTH = 128;
    /// Longest status message accepted with a mod transfer result.
    static constexpr std::size_t MAX_MOD_MESSAGE_LENGTH = 256;
    /// The ENet host is created with 32 peer slots; the mesh can never legitimately exceed it.
    static constexpr std::size_t MAX_MESH_PEERS = 32;
    /// Traffic budgets and abuse thresholds live in NetworkPacketPolicy so they stay
    /// transport independent and the tests use the same values the production path does.
    static constexpr Uint32     MAX_REJECTED_PACKETS_PER_PEER = NetworkPacketPolicy::kMaxRefusalsPerBurst;
    static constexpr Uint32     REJECT_DECAY_MS = NetworkPacketPolicy::kRefusalDecayMs;
    static constexpr Uint32     MAX_PACKETS_PER_PEER_PER_SECOND = NetworkPacketPolicy::kMaxPacketsPerWindow;
    static constexpr Uint32     BYTE_WINDOW_MS = NetworkPacketPolicy::kTrafficWindowMs;
    static constexpr Uint64     MAX_PEER_BYTES_PER_SECOND = NetworkPacketPolicy::kMaxPeerBytesPerWindow;
    static constexpr Uint64     MAX_MOD_TRANSFER_BYTES_PER_SECOND = NetworkPacketPolicy::kMaxModTransferBytesPerWindow;
    /// Rejection log lines per peer are throttled to one per this many milliseconds.
    static constexpr Uint32     REJECT_LOG_INTERVAL_MS = 5000;
    /// Largest single packet ENet will reassemble. Applied before the reassembly buffer is
    /// allocated, so an announced fragment total beyond it costs nothing. The largest
    /// legitimate packet is a multiplayer map inside SENDGAMEINFO; a mod chunk is 64 KiB.
    static constexpr std::size_t MAX_ENET_PACKET_SIZE = 4u * 1024 * 1024;
    /// Aggregate incoming data ENet may hold for one peer while commands are reassembled.
    static constexpr std::size_t MAX_ENET_WAITING_DATA = 16u * 1024 * 1024;

    std::unique_ptr<GameInitSettings> pendingCoopMission;
    Transport transport = Transport::EnetMesh;
    std::unique_ptr<RoomRelayClient> pRelayClient;
    Uint32 nextClientId = 1;        ///< source of the stable per-connection client ids
    Uint32 lastUnidentifiedLogTime = 0;  ///< throttles logging for connections without peer state
    Uint32 simulationSeed = 0;
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
    std::function<void (const std::string&, const ChangeEventList&)>        pOnReceiveChangeEventList;
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
    std::function<void (const std::string&, std::uint8_t, const std::uint8_t*, std::size_t)> pOnReceiveRelayDiagnostic;
    /// Bridges the shared payload handling back into pendingCoopMission; set by both constructors.
    std::function<void (const GameInitSettings&)>                          pOnReceiveCoopMissionBridge;

    /// Installs the callbacks the shared payload handling needs from the session itself.
    void installSessionBridges();

    // Mod transfer state (for chunked transfer)
    struct ModTransferState {
        std::string modName;
        std::string modData;
        size_t totalSize = 0;
        size_t receivedSize = 0;
        bool inProgress = false;
        bool requested = false;         ///< true once this client asked the host for a mod
        std::string requestedModName;   ///< the mod this client asked for; chunks must match it
    };
    ModTransferState modTransferState;

    /// Clears a mod download, optionally reporting the failure to the lobby.
    void abortModTransfer(const char* reason);

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

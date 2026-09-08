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

#ifndef METASERVERCLIENT_H
#define METASERVERCLIENT_H

#include <Network/MetaServerCommands.h>
#include <Network/GameServerInfo.h>
#include <misc/SDL2pp.h>

#include <memory>
#include <functional>

#include <enet/enet.h>
#include <string>
#include <list>
#include <vector>
#include <tuple>

#define SERVERLIST_UPDATE_INTERVAL  (8*1000)
#define GAMESERVER_UPDATE_INTERVAL  (10*1000)


class MetaServerClient {
public:

    explicit MetaServerClient(const std::string& metaServerURL);
    ~MetaServerClient();

    /**
        Sets the function that shall be called if there is an update to the server list
        \param  pOnGameServerInfoList   Function to call on an update to the server list
    */
    inline void setOnGameServerInfoList(std::function<void (std::list<GameServerInfo>&)> pOnGameServerInfoList) {
        this->pOnGameServerInfoList = pOnGameServerInfoList;
        lastServerInfoListUpdate = 0;
    }

    /**
        Sets the function that shall be called if the metaserver reports an error
        \param  pOnMetaServerError  Function to call on metaserver error
    */
    inline void setOnMetaServerError(std::function<void (int, const std::string&)> pOnMetaServerError) {
        this->pOnMetaServerError = pOnMetaServerError;
    }

    void startAnnounce(const std::string& serverName, int serverPort, const std::string& mapName, Uint8 numPlayers, Uint8 maxPlayers,
                       const std::string& modName = "vanilla", const std::string& modVersion = "",
                       uint16_t stunPort = 0);
    
    /**
        Get the session ID assigned by the metaserver (for hole punch coordination)
        \return session ID or empty string if not available
    */
    const std::string& getSessionId() const { return sessionId; }

    void updateAnnounce(Uint8 numPlayers);

    void stopAnnounce();
    
    /**
        Announce that a game is starting (sends Discord notification via metaserver)
        \param  mapName     The name of the map
        \param  modName     The name of the mod
        \param  players     Player details in format "House1:Player1,House2:Player2,..."
    */
    void announceGameStart(const std::string& mapName, const std::string& modName, const std::string& players);

    /** Queue a bounded match summary for the SQLite analytics endpoint. */
    void reportGameStats(const std::string& phase, const std::string& matchID, const std::string& stats);
    
    // NAT Traversal / Hole Punch methods (synchronous - for use in connection flow)
    
    /**
        Request hole punch coordination from metaserver (client side)
        \param  sessionId   The session ID of the game to join
        \param  stunPort    The client's STUN-discovered external port
        \return Client ID if successful, empty string on failure
    */
    std::string requestHolePunch(const std::string& sessionId, uint16_t stunPort);
    
    /**
        Poll for punch readiness (client side)
        \param  sessionId   The session ID of the game
        \param  clientId    The client ID from requestHolePunch
        \param  hostIP      Output: host's external IP
        \param  hostPort    Output: host's external port
        \param  waitSeconds Output: seconds to wait before punching
        \return true if ready, false if still waiting or error
    */
    bool pollPunchStatus(const std::string& sessionId, const std::string& clientId,
                         std::string& hostIP, uint16_t& hostPort, int& waitSeconds);
    
    /**
        Poll for pending punch requests (host side)
        \param  requests    Output: list of (clientId, clientIP, clientPort) tuples
        \return true on success, false on error
    */
    bool pollPunchRequests(std::vector<std::tuple<std::string, std::string, uint16_t>>& requests);
    
    /**
        Signal ready to punch a client (host side)
        \param  clientId    The client ID to punch
        \return true on success, false on error
    */
    bool signalPunchReady(const std::string& clientId);

    void update();


    const std::string metaServerURL;    ///< The url of the meta server which is used for receiving the list of game servers and publish own games to

private:

    /**
        The main function of the thread that performes the complete communication with the metaserver.
        \param  data    this void pointer should point to an instance of this MetaServerClient class
        \return returns 0
    */
    static int connectionThreadMain(void* data);


    /**
        Enqueues a new command in the command queue for processing by the metaserver connection thread.
        \param  metaServerCommand   a shared pointer to a command
    */
    void enqueueMetaServerCommand(std::unique_ptr<MetaServerCommand> metaServerCommand);

    /**
        Dequeues a command from the command queue. This method shall only be called from the metaserver connection thread.
        \return a shared pointer to the first command in the queue
    */
    std::unique_ptr<MetaServerCommand> dequeueMetaServerCommand();


    /**
        Sets a metaserver error message. This method shall only be called from the metaserver connection thread.
        \param  errorCause      the id of the command sent to the metaserver
        \param  errorMessage    the error message to post
    */
    void setErrorMessage(int errorCause, const std::string& errorMessage);

    /**
        Sets a new game server info list. This method shall only be called from the metaserver connection thread.
        \param  newGameServerInfoList   the new game server list
    */
    void setNewGameServerInfoList(const std::list<GameServerInfo>& newGameServerInfoList);


    // Shared data (used by main thread and connection thread):

    std::list<std::unique_ptr<MetaServerCommand> > metaServerCommandList;       ///< The command queue for the metaserver connection thread (shared between main thread and metaserver connection thread, \see sharedDataMutex)
    SDL_sem*    availableMetaServerCommandsSemaphore;                           ///< This semaphore counts how many commands are available in the metaServerCommandList

    int metaserverErrorCause = 0;                                               ///< Set to 0 in case of no error, else the id of the command sent to the metaserver
    std::string metaserverError = "";                                           ///< Set to some string in case a metaserver error occurs (only one error can be pending at once)
    bool bUpdatedGameServerInfoList = false;                                    ///< Was the gameServerInfoList updated? Set to true by the metaserver connection thread and reset to false in the main thread (\see sharedDataMutex)
    std::list<GameServerInfo> gameServerInfoList;                               ///< A list of all available game servers. Writen by the metaserver connection thread and read by the main thread (\see sharedDataMutex)

    SDL_mutex* sharedDataMutex;                                                 ///< This mutex must be locked before any shared data structures between the main thread and the metaserver connection thread is read or modified)

    SDL_Thread* connectionThread;                                               ///< The metaserver connection thread that processes all the metaserver communication


    // Non-Shared data (used only by main thread):

    std::string serverName = "";                                                ///< The name of the game server
    int serverPort = 0;                                                         ///< The port of the game server
    std::string secret = "";                                                    ///< The secret used for the metaserver updates
    std::string mapName = "";                                                   ///< The name of the map for which a game is currently set up
    Uint8 numPlayers = 0;                                                       ///< The current number of players in the currently set up game
    Uint8 maxPlayers = 0;                                                       ///< The maximum number of players in the currently set up game
    std::string modName = "vanilla";                                            ///< The active mod name
    std::string modVersion = "";                                                ///< The active mod version
    uint16_t stunPort = 0;                                                      ///< STUN-discovered external port (for NAT traversal)
    std::string sessionId = "";                                                 ///< Session ID from metaserver (for hole punch coordination)

    Uint32 lastAnnounceUpdate = 0;                                              ///< The last time the game was announced
    Uint32 lastServerInfoListUpdate = 0;                                        ///< The last time the server list was updated by a request to the metaserver

    std::function<void (std::list<GameServerInfo>&)> pOnGameServerInfoList;     ///< Callback for updates to the game server list
    std::function<void (int, std::string)> pOnMetaServerError;                  ///< Callback for metaserver errors
};

#endif // METASERVERCLIENT_H

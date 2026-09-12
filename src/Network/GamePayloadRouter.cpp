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

#include <Network/GamePayloadRouter.h>

#include <Network/NetworkPacketPolicy.h>
#include <Network/NetworkPacketTypes.h>

#include <GameInitSettings.h>
#include <Network/GameInitSettingsPolicy.h>

#include <Definitions.h>
#include <config.h>
#include <globals.h>
#include <misc/FileSystem.h>
#include <misc/fnkdat.h>
#include <players/QuantBotConfig.h>

#include <cstdio>
#include <filesystem>

namespace {

/// Coarse bound on a path budget order; Game::handleSetPathBudget applies the exact range.
constexpr Uint32 kMaxPathBudgetOrder = 1000000;
/// Longest start-game countdown accepted from the host (the lobby uses 3 s).
constexpr Uint32 kMaxStartGameCountdownMs = 30000;
/// Longest chat message accepted from a peer.
constexpr std::size_t kMaxChatMessageLength = 512;

/**
    Writes a received multiplayer map into the user's maps directory.

    The filename comes from another player, so it is reduced to a single portable component with
    the .ini extension the map loader expects, the payload is size capped, and the resolved
    parent directory is checked to be exactly maps/multiplayer before anything is written. An
    existing file is never overwritten.
*/
void storeReceivedMap(const GameInitSettings& gameInitSettings, GamePayloadPeer& peer) {
    if(gameInitSettings.getGameType() != GameType::CustomMultiplayer
       || gameInitSettings.getFiledata().empty()
       || gameInitSettings.getFilename().empty()) {
        return;
    }

    try {
        std::string mapFilename;
        if(!NetworkPacketPolicy::sanitizeReceivedMapFilename(gameInitSettings.getFilename(),
                                                             mapFilename)) {
            // Traversal, absolute paths, control characters, reserved names: the map is still
            // played from memory, it is just not stored.
            peer.refuse("unsafe received map filename");
            return;
        }
        if(gameInitSettings.getFiledata().size() > NetworkPacketPolicy::kMaxReceivedMapSize) {
            peer.refuse("received map exceeds the size limit");
            return;
        }

        char tmp[FILENAME_MAX];
        if(fnkdat("maps/multiplayer/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
            SDL_Log("GamePayloadRouter: Failed to get maps/multiplayer directory path");
            return;
        }

        const std::filesystem::path mapDirectory = std::filesystem::path(std::string(tmp));
        const std::filesystem::path fullPathObject = mapDirectory / mapFilename;

        // Belt and braces: whatever the name did, the file has to land directly inside the
        // multiplayer maps directory.
        std::error_code pathError;
        const std::filesystem::path resolvedParent =
            std::filesystem::weakly_canonical(fullPathObject.parent_path(), pathError);
        const std::filesystem::path resolvedDirectory =
            std::filesystem::weakly_canonical(mapDirectory, pathError);

        if(pathError || resolvedParent != resolvedDirectory) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "GamePayloadRouter: refusing to write a received map outside '%s'",
                        mapDirectory.string().c_str());
            return;
        }

        const std::string fullPath = fullPathObject.string();
        if(existsFile(fullPath)) {
            SDL_Log("GamePayloadRouter: Map '%s' already exists locally, skipping save",
                    fullPath.c_str());
            return;
        }
        if(writeCompleteFile(fullPath, gameInitSettings.getFiledata())) {
            SDL_Log("GamePayloadRouter: Saved received map to '%s'", fullPath.c_str());
        } else {
            SDL_Log("GamePayloadRouter: Failed to save received map to '%s'", fullPath.c_str());
        }
    } catch(std::exception& e) {
        SDL_Log("GamePayloadRouter: Error saving received map: %s", e.what());
    }
}

void handleConfigHash(InputStream& stream, GamePayloadPeer& peer,
                      const GamePayloadContext& context) {
    const Uint32 peerProtocolVersion = stream.readUint32();
    const std::string gameVersion    = stream.readString();
    const std::string quantBotHash   = stream.readString();
    const std::string objectDataHash = stream.readString();

    peer.gameVersion()        = gameVersion;
    peer.quantBotConfigHash() = quantBotHash;
    peer.objectDataHash()     = objectDataHash;

    const std::string localVersion        = VERSIONSTRING;
    const std::string localQuantBotHash   = getQuantBotConfig().getConfigHash();
    const std::string localObjectDataHash = getObjectDataHash();

    SDL_Log("Config from '%s': protocol %u, version %s", peer.name().c_str(),
            static_cast<unsigned>(peerProtocolVersion), gameVersion.c_str());

    const auto disconnect = [&peer](int cause) { peer.disconnectWithCause(cause); };

    if(rejectIncompatibleNetworkProtocol(peerProtocolVersion, disconnect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Rejecting incompatible protocol from '%s' (peer=%u, local=%u)",
                     peer.name().c_str(), static_cast<unsigned>(peerProtocolVersion),
                     NETWORK_PROTOCOL_VERSION);
        return;
    }

    // Content transfer cannot replace executable simulation code, so a different build is a
    // hard refusal rather than something a mod download could repair.
    if(rejectIncompatibleGameVersion(gameVersion, localVersion, disconnect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Rejecting game version mismatch: peer=%s local=%s",
                     gameVersion.c_str(), localVersion.c_str());
        return;
    }

    const bool quantBotMatches   = (quantBotHash == localQuantBotHash);
    const bool objectDataMatches = (objectDataHash == localObjectDataHash);
    if(!quantBotMatches || !objectDataMatches) {
        SDL_Log("Config mismatch with '%s' (QuantBot %s, ObjectData %s)", peer.name().c_str(),
                quantBotMatches ? "match" : "differ", objectDataMatches ? "match" : "differ");
    } else {
        SDL_Log("Config verification passed for '%s'", peer.name().c_str());
    }

    if(!context.isHost) {
        // Always answer, even on a mismatch: the host validates independently.
        peer.replyConfigHash();
    }
}

} // namespace

bool GamePayloadRouter::handles(Uint32 packetType) {
    switch(packetType) {
        case NETWORKPACKET_SENDGAMEINFO:
        case NETWORKPACKET_SENDNAME:
        case NETWORKPACKET_CHATMESSAGE:
        case NETWORKPACKET_CHANGEEVENTLIST:
        case NETWORKPACKET_CONFIG_HASH:
        case NETWORKPACKET_COOP_MISSION:
        case NETWORKPACKET_STARTGAME:
        case NETWORKPACKET_COMMANDLIST:
        case NETWORKPACKET_SELECTIONLIST:
        case NETWORKPACKET_CLIENTSTATS:
        case NETWORKPACKET_SETPATHBUDGET:
        case NETWORKPACKET_KEEPALIVE:
            return true;
        default:
            return false;
    }
}

bool GamePayloadRouter::handle(Uint32 packetType, InputStream& stream, GamePayloadPeer& peer,
                               const GamePayloadContext& context,
                               const NetworkSessionCallbacks& callbacks) {
    switch(packetType) {
        case NETWORKPACKET_SENDGAMEINFO: {
            GameInitSettings gameInitSettings(stream);
            ChangeEventList changeEventList(stream);

            std::string rejectionReason;
            if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(gameInitSettings, rejectionReason)) {
                peer.refuse(rejectionReason.c_str());
                return true;
            }
            std::string mapFilename;
            if(gameInitSettings.getGameType() == GameType::CustomMultiplayer
               && !gameInitSettings.getFiledata().empty()
               && !NetworkPacketPolicy::sanitizeReceivedMapFilename(gameInitSettings.getFilename(), mapFilename)) {
                peer.refuse("unsafe received map filename");
                return true;
            }
            if(!context.allowMapWrite && gameInitSettings.getFiledata().size()
                      > NetworkPacketPolicy::kMaxReceivedMapSize) {
                // On the relay the map text is only ever used from memory, but an absurd size
                // is still a reason to refuse rather than to keep it around.
                peer.refuse("received map exceeds the size limit");
                return true;
            }

            if(callbacks.onGameInfoAccepted) callbacks.onGameInfoAccepted();
            if(context.allowMapWrite) storeReceivedMap(gameInitSettings, peer);
            if(callbacks.onReceiveGameInfo && *callbacks.onReceiveGameInfo) {
                (*callbacks.onReceiveGameInfo)(gameInitSettings, changeEventList);
            }
        } return true;

        case NETWORKPACKET_SENDNAME: {
            const std::string newName = stream.readString();

            if(!NetworkPacketPolicy::isAcceptablePlayerName(newName)) {
                peer.refuse("unacceptable player name");
                return true;
            }
            // Identity is bound exactly once per connection. CommandManager resolves a command
            // list to a player by this name, so a later rename would let a peer take over
            // another player's commands.
            if(peer.nameAssigned()) {
                peer.refuse("peer tried to change its established name");
                return true;
            }
            peer.bindName(newName);
        } return true;

        case NETWORKPACKET_CHATMESSAGE: {
            const std::string message = stream.readString();
            if(message.size() > kMaxChatMessageLength) {
                peer.refuse("chat message exceeds the length limit");
                return true;
            }
            if(callbacks.onReceiveChatMessage && *callbacks.onReceiveChatMessage) {
                (*callbacks.onReceiveChatMessage)(peer.name(), message);
            }
        } return true;

        case NETWORKPACKET_CHANGEEVENTLIST: {
            ChangeEventList changeEventList(stream);

            if(context.isHost) {
                // A client only ever seats itself: every lobby slot claim it sends carries its
                // own name. Anything else is a peer trying to move another player around.
                bool foreignSlotClaim = false;
                for(const ChangeEventList::ChangeEvent& changeEvent
                        : changeEventList.changeEventList) {
                    if(changeEvent.eventType
                           == ChangeEventList::ChangeEvent::EventType::SetHumanPlayer
                       && changeEvent.newStringValue != peer.name()) {
                        foreignSlotClaim = true;
                        break;
                    }
                }
                if(foreignSlotClaim) {
                    peer.refuse("lobby slot claim for another player");
                    return true;
                }
            }

            if(callbacks.onReceiveChangeEventList && *callbacks.onReceiveChangeEventList) {
                (*callbacks.onReceiveChangeEventList)(peer.name(), changeEventList);
            }
        } return true;

        case NETWORKPACKET_CONFIG_HASH: {
            handleConfigHash(stream, peer, context);
        } return true;

        case NETWORKPACKET_COOP_MISSION: {
            // Co-op has exactly one remote partner and only the host chooses a mission. The
            // caller has already established that the sender is the host connection; what is
            // checked here is that it is also the only peer, so a third participant cannot
            // replace the session.
            if(context.isHost || !context.coopPartnerIsSolePeer) {
                peer.refuse("campaign continuation from an unexpected peer");
                return true;
            }

            GameInitSettings next(stream);
            std::string rejectionReason;
            if(!GameInitSettingsPolicy::isAcceptableReceivedGameInitSettings(next, rejectionReason, true)) {
                peer.refuse(rejectionReason.c_str());
                return true;
            }
            // Empty settings are how the host says "we are leaving the campaign".
            if(next.getGameType() != GameType::CampaignCoop
               && next.getGameType() != GameType::Invalid) {
                peer.refuse("campaign continuation for the wrong game type");
                return true;
            }
            if(callbacks.onReceiveCoopMission && *callbacks.onReceiveCoopMission) {
                (*callbacks.onReceiveCoopMission)(next);
            }
        } return true;

        case NETWORKPACKET_STARTGAME: {
            const Uint32 timeLeft = stream.readUint32();
            if(timeLeft > kMaxStartGameCountdownMs) {
                peer.refuse("start-game countdown out of range");
                return true;
            }
            if(callbacks.onStartGame && *callbacks.onStartGame) {
                (*callbacks.onStartGame)(timeLeft);
            }
        } return true;

        case NETWORKPACKET_COMMANDLIST: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            CommandList commandList(stream);
            if(callbacks.onReceiveCommandList && *callbacks.onReceiveCommandList) {
                (*callbacks.onReceiveCommandList)(peer.name(), commandList);
            }
        } return true;

        case NETWORKPACKET_SELECTIONLIST: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            const int groupListIndex = stream.readSint32();
            const std::set<Uint32> selectedList = stream.readUint32Set();

            if(selectedList.size() > NetworkPacketPolicy::kMaxSelectionSize) {
                peer.refuse("selection list exceeds the size limit");
                return true;
            }
            // -1 means "current selection"; anything else indexes HumanPlayer::selectedLists.
            if(groupListIndex < -1 || groupListIndex >= NUMSELECTEDLISTS) {
                peer.refuse("selection group index out of range");
                return true;
            }
            if(callbacks.onReceiveSelectionList && *callbacks.onReceiveSelectionList) {
                (*callbacks.onReceiveSelectionList)(peer.name(), selectedList, groupListIndex);
            }
        } return true;

        case NETWORKPACKET_CLIENTSTATS: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            const Uint32 gameCycle     = stream.readUint32();
            const float  avgFps        = stream.readFloat();
            const float  simMsAvg      = stream.readFloat();
            const Uint32 queueDepth    = stream.readUint32();
            const Uint32 currentBudget = stream.readUint32();

            if(!NetworkPacketPolicy::isUsableStatValue(avgFps)
               || !NetworkPacketPolicy::isUsableStatValue(simMsAvg)) {
                peer.refuse("unusable client stats");
                return true;
            }

            // Identity is the connection, not an address hash: behind NAT or behind a relay a
            // host^port pair collides across players and is trivially spoofable.
            if(callbacks.onReceiveClientStats && *callbacks.onReceiveClientStats) {
                (*callbacks.onReceiveClientStats)(peer.clientId(), gameCycle, avgFps, simMsAvg,
                                                  queueDepth, currentBudget);
            }
        } return true;

        case NETWORKPACKET_SETPATHBUDGET: {
            if(stream.readUint32() != context.simulationSeed) {
                return true;
            }
            const Uint32 newBudget  = stream.readUint32();
            const Uint32 applyCycle = stream.readUint32();

            if(newBudget > kMaxPathBudgetOrder) {
                peer.refuse("path budget order out of range");
                return true;
            }
            if(callbacks.onReceiveSetPathBudget && *callbacks.onReceiveSetPathBudget) {
                (*callbacks.onReceiveSetPathBudget)(newBudget, applyCycle);
            }
        } return true;

        case NETWORKPACKET_KEEPALIVE: {
            // Receiving it is the whole point; nothing to do.
        } return true;

        default:
            return false;
    }
}

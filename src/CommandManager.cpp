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

#include <CommandManager.h>

#include <CommandValidation.h>
#include <Network/NetworkManager.h>
#include <players/HumanPlayer.h>

#include <globals.h>

#include <Game.h>

#include <algorithm>
#include <limits>


CommandManager::CommandManager() {
    pStream = nullptr;
    bReadOnly = false;
    networkCycleBuffer = 0;
}

CommandManager::~CommandManager() = default;

void CommandManager::addCommand(const Command& cmd) {
    Uint32 CycleNumber = currentGame->getGameCycleCount();

    if(pNetworkManager != nullptr) {
        CycleNumber += networkCycleBuffer;
    }
    addCommand(cmd, CycleNumber);
}

void CommandManager::save(OutputStream& stream) const {
    for(unsigned int i=0;i<timeslot.size();i++) {
        for(const Command& command : timeslot[i]) {
            stream.writeUint32(i);
            command.save(stream);
        }
    }
}

void CommandManager::load(InputStream& stream) {
    // A replay or savegame is a local file, but it is still untrusted input: it may have been
    // downloaded, and auto.rpl is routinely truncated by a crash. The record format has no
    // header or terminator, so this keeps the historical "read until end of file" behaviour
    // while bounding what a file can make this allocate and refusing records that would throw
    // later, inside the simulation loop. A malformed tail stops the load with a warning
    // instead of being silently accepted as a clean end.
    std::size_t loadedCommands = 0;
    bool bCleanEnd = false;

    try {
        while(true) {
            Uint32 cycle = 0;
            try {
                cycle = stream.readUint32();
            } catch (InputStream::exception&) {
                // End of file exactly at a record boundary: this is the normal termination.
                bCleanEnd = true;
                break;
            }

            if(cycle > CommandValidation::kMaxReplayCycle) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "CommandManager: replay refers to cycle %u, beyond the supported range",
                            cycle);
                break;
            }

            if(loadedCommands >= CommandValidation::kMaxReplayCommands) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "CommandManager: replay contains more than %zu commands, stopping",
                            static_cast<std::size_t>(CommandValidation::kMaxReplayCommands));
                break;
            }

            Command command(stream);
            if(!CommandValidation::isWellFormedCommand(static_cast<Uint32>(command.getCommandID()),
                                                       command.getParameter().size())) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "CommandManager: replay contains a malformed command %u at cycle %u",
                            static_cast<unsigned int>(command.getCommandID()), cycle);
                break;
            }

            addCommand(command, cycle);
            loadedCommands++;
        }
    } catch (InputStream::exception&) {
        // The stream ended in the middle of a record: the file is truncated or corrupt.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "CommandManager: replay ended inside a command record after %zu commands",
                    loadedCommands);
    }

    if(!bCleanEnd) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "CommandManager: replay was not read to a clean end; %zu commands loaded",
                    loadedCommands);
    }
}

void CommandManager::update() {
    if(pNetworkManager != nullptr) {
        CommandList commandList;
        for(Uint32 i = std::max((int) currentGame->getGameCycleCount() - MILLI2CYCLES(2500), 0); i < currentGame->getGameCycleCount() + networkCycleBuffer; i++) {
            std::vector<Command> commands;

            if(i < timeslot.size()) {
                for(Command& command : timeslot[i]) {
                    if(command.getPlayerID() == pLocalPlayer->getPlayerID()) {
                        commands.push_back(command);
                    }
                }
            }

            commandList.commandList.emplace_back(i, commands);
        }

        pNetworkManager->sendCommandList(commandList);
    }
}

void CommandManager::addCommandList(const std::string& playername, const CommandList& commandList) {
    HumanPlayer* pPlayer = dynamic_cast<HumanPlayer*>(currentGame->getPlayerByName(playername));
    if(pPlayer == nullptr) {
        return;
    }

    const Uint32 currentCycle = currentGame->getGameCycleCount();
    const Uint32 firstExpectedCycle = pPlayer->nextExpectedCommandsCycle;

    // Pass 1 - validate everything this batch would add, before anything is queued. A content
    // fault (a command for another player, a malformed command, a cycle outside the window,
    // too many commands) means the sender is not the game, so the whole batch is dropped and
    // the watermark does not move.
    std::size_t newCommandCount = 0;
    const char* rejectionReason = nullptr;

    for(const CommandList::CommandListEntry& commandListEntry : commandList.commandList) {
        if(commandListEntry.cycle < firstExpectedCycle) {
            // Already processed: this is one of the retransmissions in the rolling history.
            continue;
        }

        // addCommand() resizes its timeslot vector to the cycle number, so a cycle far in the
        // future is an unbounded allocation.
        if(!CommandValidation::isAcceptableCommandCycle(commandListEntry.cycle, currentCycle,
                                                        networkCycleBuffer)) {
            rejectionReason = "cycle outside the acceptable window";
            break;
        }

        if(!CommandValidation::isAcceptableCommandCountPerEntry(
               static_cast<Uint32>(commandListEntry.commands.size()))) {
            rejectionReason = "too many commands in one cycle";
            break;
        }

        newCommandCount += commandListEntry.commands.size();
        if(!CommandValidation::isAcceptableCommandTotal(newCommandCount)) {
            rejectionReason = "too many commands in one packet";
            break;
        }

        for(const Command& command : commandListEntry.commands) {
            // A peer may only ever issue commands for its own player. Players that share a
            // house each have their own player id, so this still allows co-op control.
            if(command.getPlayerID() != pPlayer->getPlayerID()) {
                rejectionReason = "command issued for another player";
                break;
            }

            // An unknown command id or a wrong parameter count makes executeCommand() throw
            // out of the simulation loop, which takes down every peer that accepted it.
            if(!CommandValidation::isWellFormedCommand(static_cast<Uint32>(command.getCommandID()),
                                                       command.getParameter().size())) {
                rejectionReason = "malformed command";
                break;
            }
        }

        if(rejectionReason != nullptr) {
            break;
        }
    }

    if(rejectionReason != nullptr) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "CommandManager: dropping the whole command batch from '%s': %s",
                    playername.c_str(), rejectionReason);
        return;
    }

    // Pass 2 - apply the contiguous run that starts at the cycle we are waiting for, and stop
    // at the first gap, so the watermark never moves past a cycle we did not receive. An
    // unsorted, gapped or duplicated list therefore cannot advance it, while an ordinary packet
    // loss (the command channel is unsequenced) still recovers from the overlapping history in
    // the next packet instead of losing a whole batch.
    Uint32 expectedCycle = firstExpectedCycle;

    for(const CommandList::CommandListEntry& commandListEntry : commandList.commandList) {
        if(commandListEntry.cycle < expectedCycle) {
            continue;
        }
        if(commandListEntry.cycle != expectedCycle) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CommandManager: '%s' skipped cycle %u (offered %u); ignoring the rest",
                        playername.c_str(), expectedCycle, commandListEntry.cycle);
            break;
        }

        for(const Command& command : commandListEntry.commands) {
            addCommand(command, commandListEntry.cycle);
        }

        expectedCycle = CommandValidation::nextCycleAfter(commandListEntry.cycle);
    }

    pPlayer->nextExpectedCommandsCycle = expectedCycle;
}

void CommandManager::addCommand(const Command& cmd, Uint32 CycleNumber) {
    if(bReadOnly == false) {

        if(CycleNumber == std::numeric_limits<Uint32>::max()) {
            // CycleNumber+1 would wrap to 0 and leave timeslot[CycleNumber] out of bounds.
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CommandManager: refusing a command scheduled for the maximum cycle");
            return;
        }

        if(CycleNumber >= timeslot.size()) {
            timeslot.resize(static_cast<std::size_t>(CycleNumber) + 1);
        }

        timeslot[CycleNumber].push_back(cmd);
        std::stable_sort(   timeslot[CycleNumber].begin(),
                            timeslot[CycleNumber].end(),
                            [](const Command& cmd1, const Command& cmd2) {
                                return (cmd1.getPlayerID() < cmd2.getPlayerID());
                            });

        if(pStream != nullptr) {
            pStream->writeUint32(CycleNumber);
            cmd.save(*pStream);
        }
    }
}

void CommandManager::executeCommands(Uint32 CycleNumber) const {
    if(CycleNumber >= timeslot.size()) {
        return;
    }

    for(const Command& command : timeslot[CycleNumber]) {
        command.executeCommand();
    }
}


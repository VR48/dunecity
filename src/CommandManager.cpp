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
    try {
        while(1) {
            Uint32 cycle = stream.readUint32();
            addCommand(Command(stream), cycle);
        }
    } catch (InputStream::exception&) {
        ;
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

    for(const CommandList::CommandListEntry& commandListEntry : commandList.commandList) {
        if(pPlayer->nextExpectedCommandsCycle > commandListEntry.cycle) {
            // Already processed; this is one of the retransmissions in the rolling history.
            continue;
        }

        // addCommand() resizes its timeslot vector to the cycle number, so a cycle far in the
        // future is an unbounded allocation. Past cycles stay acceptable: they are how the
        // rolling 2.5 s history and its retransmissions work.
        if(!CommandValidation::isAcceptableCommandCycle(commandListEntry.cycle, currentCycle,
                                                        networkCycleBuffer)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "CommandManager: dropping commands from '%s' for cycle %u (current cycle %u)",
                        playername.c_str(), commandListEntry.cycle, currentCycle);
            continue;
        }

        for(const Command& command : commandListEntry.commands) {
            // A peer may only ever issue commands for its own player. Players that share a
            // house each have their own player id, so this still allows co-op control.
            if(command.getPlayerID() != pPlayer->getPlayerID()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "CommandManager: dropping command from '%s' issued for player %u",
                            playername.c_str(), static_cast<unsigned int>(command.getPlayerID()));
                continue;
            }

            // An unknown command id or a wrong parameter count makes executeCommand() throw
            // out of the simulation loop, which takes down every peer that accepted it.
            if(!CommandValidation::isWellFormedCommand(static_cast<Uint32>(command.getCommandID()),
                                                       command.getParameter().size())) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "CommandManager: dropping malformed command %u from '%s'",
                            static_cast<unsigned int>(command.getCommandID()), playername.c_str());
                continue;
            }

            addCommand(command, commandListEntry.cycle);
        }

        pPlayer->nextExpectedCommandsCycle = std::max(pPlayer->nextExpectedCommandsCycle,
                                                      CommandValidation::nextCycleAfter(commandListEntry.cycle));
    }
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


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

#ifndef COMMANDLIST_H
#define COMMANDLIST_H

#include <misc/InputStream.h>
#include <misc/OutputStream.h>
#include <misc/SDL2pp.h>

#include <Command.h>
#include <CommandValidation.h>

#include <vector>

class CommandList {
public:
    class CommandListEntry {
    public:
        CommandListEntry(Uint32 cycle, const std::vector<Command>& commands)
         : cycle(cycle), commands(commands) {

        }

        explicit CommandListEntry(InputStream& stream) {
            cycle = stream.readUint32();
            Uint32 numCommands = stream.readUint32();
            if(!CommandValidation::isAcceptableCommandCountPerEntry(numCommands)) {
                throw InputStream::error("CommandList: too many commands in one cycle entry!");
            }
            // A command is at least playerID + commandID + parameter count = 9 bytes.
            stream.requireReadableElements(numCommands, 9);
            commands.reserve(numCommands);
            for(Uint32 i = 0; i < numCommands; i++) {
                commands.push_back(Command(stream));
            }
        }

        void save(OutputStream& stream) const {
            stream.writeUint32(cycle);

            stream.writeUint32((Uint32) commands.size());
            for(const Command& command : commands) {
                command.save(stream);
            }
        }

        Uint32      cycle;
        std::vector<Command> commands;
    };

    CommandList() = default;

    explicit CommandList(InputStream& stream) {
        Uint32 numCommandListEntries = stream.readUint32();
        if(!CommandValidation::isAcceptableCommandListEntryCount(numCommandListEntries)) {
            throw InputStream::error("CommandList: too many command list entries!");
        }
        // One entry is at least cycle + command count = 8 bytes.
        stream.requireReadableElements(numCommandListEntries, 8);
        commandList.reserve(numCommandListEntries);
        for(Uint32 i = 0; i < numCommandListEntries; i++) {
            commandList.emplace_back(stream);
        }
    }

    ~CommandList() = default;

    void save(OutputStream& stream) const {
        stream.writeUint32((Uint32) commandList.size());
        for(const CommandListEntry& commandListEntry : commandList) {
            commandListEntry.save(stream);
        }
    }

    std::vector<CommandListEntry> commandList;
};

#endif //COMMANDLIST_H

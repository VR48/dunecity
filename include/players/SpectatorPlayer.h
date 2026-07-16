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

#ifndef SPECTATORPLAYER_H
#define SPECTATORPLAYER_H

#include <players/Player.h>

/**
    DuneCity 1.0.370: SpectatorPlayer.

    SpectatorPlayer is a non-interactive Player subclass that exists
    so a slot in CustomGamePlayers can be flagged as 'Spectator'.
    It takes no commands, does not produce any AI ticks, and never
    spawns a tactical map. Used for observation mode in multiplayer
    (a real spectator session) and for AI-vs-AI solo games (the local
    player picks up a Spectator slot to observe without taking any
    side). All virtual event handlers are inherited as no-ops; only
    update() and the constructor body are implemented.

    The PlayerFactory entry registering 'Spectator' is added in
    v1.0.370. CustomGamePlayers UI toggle wires the entry into a
    player slot.
*/
class SpectatorPlayer : public Player
{
public:
    SpectatorPlayer(House* associatedHouse, const std::string& playername);
    SpectatorPlayer(InputStream& stream, House* associatedHouse);
    ~SpectatorPlayer() override;

    void save(OutputStream& stream) const override;

    void update() override;
};

#endif // SPECTATORPLAYER_H

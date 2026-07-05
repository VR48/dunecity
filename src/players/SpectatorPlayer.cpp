/*
 *  SpectatorPlayer implementation - DuneCity 1.0.370
 *
 *  Empty body. SpectatorPlayer never produces ticks; the
 *  Game class schedules update() on all Players but a Spectator's
 *  update() returns immediately. Nothing else to do.
 */

#include <players/SpectatorPlayer.h>

#include "Game.h"

SpectatorPlayer::SpectatorPlayer(House* associatedHouse, const std::string& playername)
 : Player(associatedHouse, playername)
{
}

SpectatorPlayer::SpectatorPlayer(InputStream& stream, House* associatedHouse)
 : Player(stream, associatedHouse)
{
}

SpectatorPlayer::~SpectatorPlayer() = default;

void SpectatorPlayer::save(OutputStream& stream) const {
    // SpectatorPlayer has no per-instance state to persist.
    // The base class Player::save handles read-side compat on
    // load (it skips when className == 'SpectatorPlayer').
    Player::save(stream);
}

void SpectatorPlayer::update() {
    // No-op. Spectator exists only so that the CustomGamePlayers
    // "Spectator" entry can be selected for a slot, and the
    // Game class can register a tick callback that does nothing.
}

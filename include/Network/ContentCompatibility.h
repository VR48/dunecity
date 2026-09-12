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

#ifndef CONTENTCOMPATIBILITY_H
#define CONTENTCOMPATIBILITY_H

/**
    Whether two installs are playing the same game.

    Lockstep needs every peer to compute the same thing from the same commands, which means the
    same build and the same content files. On the mesh transport a difference is the expected
    prelude to a mod transfer. On the relay there is no transfer, so a difference is final and
    has to stop the match visibly rather than appear in a log.

    The rule lives here, on its own, because three places need it and they must not drift: the
    shared payload handler when a peer's hashes arrive, the host when it is about to start, and
    the tests. It has no dependencies beyond std::string so any of them can use it.
*/

#include <string>

namespace ContentCompatibility {

/// What one participant says it is running. Every field is the peer's own claim.
struct Fingerprint {
    std::string gameVersion;
    std::string quantBotHash;
    std::string objectDataHash;

    /// True when every field has a value. An absent field is never treated as a match.
    bool complete() const {
        return !gameVersion.empty() && !quantBotHash.empty() && !objectDataHash.empty();
    }
};

enum class Verdict {
    Match,          ///< both sides reported, and they agree
    AwaitingPeer,   ///< the peer has not reported yet; this is recoverable by waiting
    Mismatch        ///< they disagree, or we cannot describe ourselves
};

/**
    Compares one peer against us.

    Silence is not agreement, in either direction. A peer that has not reported yet has not
    shown that it matches; an install that could not hash its own content has not shown anything
    at all and must not go online claiming otherwise.

    \param  local       what we are running
    \param  peer        what the peer said it is running
    \param  peerName    used in the explanation; may be empty
    \param  reason      set to a player-facing explanation unless the verdict is Match
*/
inline Verdict compare(const Fingerprint& local, const Fingerprint& peer,
                       const std::string& peerName, std::string& reason) {
    reason.clear();

    if(!local.complete()) {
        reason = "This copy of the game could not check its own content files, "
                 "so it cannot play online.";
        return Verdict::Mismatch;
    }

    const std::string who = peerName.empty() ? std::string("The other player")
                                             : ("'" + peerName + "'");

    if(!peer.complete()) {
        reason = "Waiting for " + who + " to confirm its game content.";
        return Verdict::AwaitingPeer;
    }

    std::string differences;
    if(peer.gameVersion != local.gameVersion) {
        differences += "\n- a different version of the game";
    }
    if(peer.quantBotHash != local.quantBotHash) {
        differences += "\n- QuantBot Config.ini";
    }
    if(peer.objectDataHash != local.objectDataHash) {
        differences += "\n- ObjectData.ini";
    }

    if(!differences.empty()) {
        reason = who + " has different game content:" + differences
               + "\n\nEveryone needs the same content to play online.";
        return Verdict::Mismatch;
    }

    return Verdict::Match;
}

} // namespace ContentCompatibility

#endif // CONTENTCOMPATIBILITY_H

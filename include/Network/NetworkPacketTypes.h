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

#ifndef NETWORKPACKETTYPES_H
#define NETWORKPACKETTYPES_H

/**
    Wire constants of the ENet protocol. Kept separate from NetworkManager.h so the packet
    admission policy can be included by code (and tests) that must not pull in the manager.
    These values are part of the legacy wire format - do not renumber them.
*/

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

#define NETWORKPACKET_COOP_MISSION          20

// Network protocol version - increment when packet formats change
// Version 2: Added simMsAvg to NETWORKPACKET_CLIENTSTATS (5 fields instead of 4)
// Version 3: Added mod transfer packets (MOD_INFO, MOD_REQUEST, MOD_CHUNK, MOD_COMPLETE)
// Version 4: Fixed nine-house deterministic state and versioned visibility storage
#define NETWORK_PROTOCOL_VERSION            5

// Mod transfer limits
#define MAX_MOD_TRANSFER_SIZE   (10 * 1024 * 1024)  // 10MB max mod size
#define MOD_CHUNK_SIZE          (64 * 1024)          // 64KB per chunk

#endif // NETWORKPACKETTYPES_H

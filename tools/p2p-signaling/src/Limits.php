<?php
declare(strict_types=1);

/**
 * Wire and resource bounds for the DuneCity P2P signaling service.
 *
 * The numbers that a client can observe are not free choices: they are mirrored by
 * include/Network/RoomRelayProtocol.h, include/Network/RoomAdmissionClient.h and
 * include/Network/P2PSignalingProtocol.h. Those headers are authoritative for anything a client
 * parses; changing a value here without changing the header is a bug. The numbers a client
 * cannot observe (queue depths, TTLs, table sizes) are this service's own resource ceilings and
 * exist so that one room, one address or one hostile caller cannot cost the whole service.
 */
final class Limits
{
    /** RoomRelay::kProtocolVersion. */
    public const PROTOCOL_VERSION = 1;

    // --- Legacy admission response envelope (RoomAdmission::* in RoomAdmissionClient.h) ------
    public const ADMISSION_MAX_RESPONSE_BYTES = 8192;
    public const ADMISSION_MAX_RESPONSE_LINES = 16;
    public const ADMISSION_MAX_LINE_BYTES     = 512;
    public const ADMISSION_MAX_VALUE_BYTES    = 480;

    // --- Signaling response envelope --------------------------------------------------------
    //
    // These are the bounds agreed with Codex in CODEX-FEEDBACK.md §17, and they replace the
    // smaller ones still in include/Network/P2PSignalingProtocol.h. The old value bound of 2000
    // characters could not carry a hex-encoded SDP at all: a real browser offer is about 2 KiB,
    // which is 4000 characters of hex before the record's own `<from>|<seq>|<kind>|` prefix. A
    // service that emits a line its client cannot parse is, at the client, a service that is
    // down - so the envelope is sized for the largest description the stacks actually produce.
    public const SIGNAL_MAX_RESPONSE_BYTES = 524288;
    public const SIGNAL_MAX_RESPONSE_LINES = 256;
    public const SIGNAL_MAX_LINE_BYTES     = 131200;
    public const SIGNAL_MAX_VALUE_BYTES    = 131168;
    /** The largest SDP this service will accept and deliver, as raw bytes before hex. */
    public const SIGNAL_MAX_PAYLOAD_BYTES  = 65536;
    public const SIGNAL_MAX_PER_POLL       = 32;
    public const SIGNAL_MAX_ICE_SERVERS    = 4;
    public const SIGNAL_SESSION_CHARS      = 64;

    // --- Field shapes (RoomRelay::Limits) ---------------------------------------------------
    public const MAX_GRANT_CHARS        = 64;
    public const MAX_RUNTIME_CHARS      = 16;
    public const MAX_APP_VERSION_CHARS  = 32;
    public const MAX_CONTENT_HASH_CHARS = 64;
    public const MAX_NAME_CHARS         = 64;
    public const MAX_MESSAGE_CHARS      = 200;
    public const MAX_ROOM_CODE_CHARS    = 16;
    public const MAX_PEERS_PER_ROOM     = 8;

    // --- Room / grant lifecycle -------------------------------------------------------------
    public const MAX_ROOMS              = 64;
    public const MAX_OUTSTANDING_GRANTS = 512;
    public const GRANT_TTL_MS           = 30000;
    public const EMPTY_ROOM_TTL_MS      = 60000;
    public const ROOM_LIFETIME_MS       = 6 * 60 * 60 * 1000;
    /** How long a lobby peer may be silent before the room forgets it and announces `gone=`. */
    public const LOBBY_PEER_IDLE_MS     = 60000;
    /**
     * How long the directory keeps a room nobody has touched. This is a backstop, not the main
     * cleanup: an empty room goes after EMPTY_ROOM_TTL_MS, and a room whose last lobby peer fell
     * silent is noticed by the next poll that sees the membership change.
     */
    public const LOBBY_ROOM_IDLE_MS     = 15 * 60000;
    /**
     * Once a match is running the peers no longer need this service, and most of them stop
     * polling it. A silent peer in a running match is therefore not evidence that it left, so it
     * is never announced as gone; only the whole room is reaped, much later.
     */
    public const MATCH_ROOM_IDLE_MS     = 30 * 60000;

    // --- Signaling queues -------------------------------------------------------------------
    /** Records held for the whole room at once. */
    public const ROOM_MAX_SIGNALS        = 256;
    public const ROOM_MAX_SIGNAL_BYTES   = 1024 * 1024;
    /** Per ordered pair (from -> to), per room lifetime. */
    public const PAIR_MAX_CANDIDATES     = 128;
    public const PAIR_MAX_DESCRIPTIONS   = 2;
    /** Two maximum descriptions plus a full trickle of maximum candidates, and no more. */
    public const PAIR_MAX_BYTES          = 512 * 1024;
    public const SIGNAL_TTL_MS           = 120000;

    // --- SDP / candidate shapes -------------------------------------------------------------
    public const SDP_MAX_BYTES        = 65536;
    public const SDP_MAX_LINES        = 2048;
    public const SDP_MAX_LINE_BYTES   = 4096;
    /** One candidate line, and separately the media id that prefixes it on the wire. */
    public const CANDIDATE_MAX_BYTES  = 2048;
    public const CANDIDATE_MID_CHARS  = 64;

    // --- HTTP ingress -----------------------------------------------------------------------
    public const HTTP_MAX_BODY_BYTES        = 4096;
    /** 65536 bytes of SDP as hex, plus headroom for `to=`, `kind=` and the field names. */
    public const HTTP_MAX_SIGNAL_BODY_BYTES = 133120;
    public const HTTP_MAX_FORM_FIELDS       = 24;
    public const HTTP_MAX_FIELD_BYTES       = 256;
    public const HTTP_MAX_SIGNAL_FIELD_BYTES = 131072 + 16;
    public const MAX_ORIGIN_CHARS           = 256;
    public const PREFLIGHT_MAX_AGE_SECONDS  = 600;

    // --- Rate limits (per minute unless stated) ---------------------------------------------
    public const RATE_GLOBAL_INGRESS   = 8192;
    public const RATE_ADDRESS_INGRESS  = 360;
    public const RATE_GLOBAL_ADMISSION = 120;
    public const RATE_ADDRESS_ADMISSION = 10;
    /**
     * Opening a session is the direct path's equivalent of the relay's socket connect, which had
     * its own larger allowance: a grant has already been rate limited at issuance, and this is
     * the second line that bounds redeeming one at all.
     */
    public const RATE_GLOBAL_SESSION   = 512;
    public const RATE_ADDRESS_SESSION  = 30;
    public const RATE_GLOBAL_POLL      = 4096;
    public const RATE_ADDRESS_POLL     = 240;
    /** Signalling is chatty while a mesh forms and silent afterwards. */
    public const RATE_GLOBAL_SIGNAL    = 8192;
    public const RATE_ADDRESS_SIGNAL   = 600;
    public const RATE_TABLE_ENTRIES    = 2048;
    public const RATE_TABLE_TTL_MS     = 10 * 60000;

    // --- Lobby chat -------------------------------------------------------------------------
    public const LOBBY_SESSION_TTL_MS      = 90000;
    public const LOBBY_SESSION_LIFETIME_MS = 30 * 60000;
    public const LOBBY_MAX_SESSIONS        = 128;
    public const LOBBY_MAX_SESSIONS_PER_ADDRESS = 4;
    public const LOBBY_MAX_CHANNELS        = 32;
    public const LOBBY_MAX_HISTORY         = 100;
    public const LOBBY_HISTORY_TTL_MS      = 15 * 60000;
    public const LOBBY_MAX_NAME_BYTES      = 64;
    public const LOBBY_MAX_TEXT_BYTES      = 120;
    public const LOBBY_SAYS_PER_10S        = 4;
    /** The legacy parser accepts at most 12 chat lines and at most 12 directory entries. */
    public const PAGE_SIZE                 = 12;

    // --- State files ------------------------------------------------------------------------
    public const STATE_MAX_FILE_BYTES  = 4 * 1024 * 1024;
    public const ANALYTICS_MAX_BYTES   = 1024 * 1024;
    public const LOG_MAX_BYTES         = 512 * 1024;

    public const ROOM_CODE_ALPHABET = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
    public const ROOM_CODE_LENGTH   = 12;
}

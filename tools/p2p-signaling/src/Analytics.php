<?php
declare(strict_types=1);

/**
 * Truthful, session-only lifecycle events.
 *
 * What this service can honestly say about a match is small, and it is deliberately written down
 * as exactly that. It sees a room being created, players being admitted, a host declaring the
 * match started, players leaving and the room ending. It never sees a game packet - there is no
 * endpoint that would take one - so nothing recorded here may be described as a move, an outcome,
 * a score or a duration of play. `runtime` is a client's claim about itself and is labelled as a
 * claim, not as an observation.
 *
 * The relay's schema (tools/room-relay/src/analytics.js, schema_version 2) is extended rather
 * than replaced: same kinds, same id shapes, same fixed reason codes, plus `transport:
 * "direct-p2p"` and the additive `runtime_claimed` / `peers_admitted` fields, at schema_version 3.
 *
 * NOT IMPLEMENTED HERE, ON PURPOSE: delivery to the metaserver analytics API. That integration is
 * an outbound HTTPS request, and this service is specified to make no outbound request at all.
 * Events are appended to a bounded local JSONL file in the private state directory, and a
 * separate, out-of-band shipper (Codex) can read them. See README.md "Analytics".
 */
final class Analytics
{
    private const SCHEMA_VERSION = 3;
    private const KINDS = ['created', 'joined', 'started', 'left', 'closed'];
    private const REASONS = ['normal', 'host_left', 'shutdown', 'lifetime', 'empty', 'timeout',
                             'protocol_error', 'rate_limited', 'unspecified'];

    public function __construct(private readonly Store $store, private readonly bool $enabled)
    {
    }

    /**
     * @param array<string,scalar|null> $fields only primitives; no room, peer or request object
     *        ever reaches this file, so nothing can be forwarded by accident.
     */
    public function record(string $kind, array $fields): void
    {
        if (!$this->enabled || !in_array($kind, self::KINDS, true)) {
            return;
        }
        $event = [
            'schema_version' => self::SCHEMA_VERSION,
            'event_id'       => Store::randomHex(16),
            'kind'           => $kind,
            // The one server-observed transport this service can serve. A client cannot set it.
            'transport'      => 'direct-p2p',
            'occurred_at'    => (int)floor($this->store->now() / 1000),
            'room_log_id'    => self::token($fields['room_log_id'] ?? null),
            'participant_id' => self::token($fields['participant_id'] ?? null),
            'game_version'   => self::version($fields['game_version'] ?? null),
            'peers_admitted' => self::count($fields['peers_admitted'] ?? null),
            'runtime_claimed' => in_array($fields['runtime_claimed'] ?? null, ['browser', 'native'], true)
                ? $fields['runtime_claimed'] : null,
            'reason'         => in_array($fields['reason'] ?? null, self::REASONS, true)
                ? $fields['reason'] : null,
        ];
        $line = json_encode($event, JSON_UNESCAPED_SLASHES);
        if ($line === false || strlen($line) > 1024) {
            return;     // a malformed event is a bug here, not a reason to interrupt a game
        }
        $this->store->append('analytics.jsonl', $line, Limits::ANALYTICS_MAX_BYTES);
    }

    private static function token(mixed $value): ?string
    {
        return is_string($value) && preg_match('/^[A-Za-z0-9_-]{8,64}$/D', $value) ? $value : null;
    }

    private static function version(mixed $value): ?string
    {
        return is_string($value) && preg_match('/^[A-Za-z0-9._-]{1,32}$/D', $value) ? $value : null;
    }

    private static function count(mixed $value): ?int
    {
        return is_int($value) && $value >= 0 && $value <= Limits::MAX_PEERS_PER_ROOM ? $value : null;
    }
}

/**
 * The operational log. Endpoint, refusal code and a salted address tag, and nothing else.
 *
 * Never written here: SDP, ICE candidates, grants, session tokens, control tokens, invitation
 * codes, display names or chat text. The address is salted with a per-installation secret so the
 * log can still answer "was this the same caller" without being a list of who played.
 */
final class ServiceLog
{
    /**
     * @param callable():string $salt resolved lazily. The salt lives in the room directory, and
     *        taking that lock on every request - including the ones that never log anything -
     *        would make the busiest file in the service the one nobody needed.
     */
    public function __construct(private readonly Store $store, private readonly bool $enabled,
                                private $salt)
    {
    }

    public function denied(string $endpoint, string $code, string $address): void
    {
        if (!$this->enabled) {
            return;
        }
        try {
            $salt = ($this->salt)();
        } catch (Throwable) {
            // If the state directory cannot be read we are already failing the request; not
            // being able to log that is not a reason to fail it twice.
            return;
        }
        $line = json_encode([
            'at'       => (int)floor($this->store->now() / 1000),
            'endpoint' => preg_match('#^/[A-Za-z0-9/_-]{0,64}$#', $endpoint) ? $endpoint : 'unknown',
            'code'     => preg_match('/^[a-z_]{1,32}$/D', $code) ? $code : 'unknown',
            'address'  => substr(hash('sha256', $salt . '|' . $address), 0, 16),
        ], JSON_UNESCAPED_SLASHES);
        if ($line !== false) {
            $this->store->append('log.jsonl', $line, Limits::LOG_MAX_BYTES);
        }
    }
}

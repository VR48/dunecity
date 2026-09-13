<?php
declare(strict_types=1);

/**
 * Ephemeral, bounded lobby chat, with the same semantics as tools/room-relay/src/lobby.js.
 *
 * Names here are confirmed display names, not accounts: entering takes a name, checks it is not
 * already in use on that channel, and hands back a session token that binds every later message
 * to that name. A caller cannot say something under somebody else's name, and cannot claim a name
 * that is in use. A channel is one (game protocol, content hash) pair, so players only ever see
 * chat from builds they could actually play with.
 *
 * Neither the token nor the message contents go into analytics or the log.
 */
final class Lobby
{
    public function __construct(private readonly Store $store)
    {
    }

    /**
     * Decodes a hex-encoded UTF-8 string and refuses anything that could impersonate or reorder.
     * PHP's mbstring is not available on the production host, so UTF-8 is validated here.
     */
    public static function decodeText(?string $hex, int $maxBytes): string
    {
        if (!is_string($hex) || strlen($hex) > $maxBytes * 2 || !Http::isHex($hex)) {
            throw new ServiceError(400, 'bad_request', 'Text is missing or too long.');
        }
        $text = (string)hex2bin($hex);
        // A valid UTF-8 sequence, and nothing from the control, format, line- or paragraph-
        // separator categories: those are what is used to fake a name or reverse a sentence.
        if (!preg_match('//u', $text) || preg_match('/\p{Cc}|\p{Cf}|\p{Zl}|\p{Zp}/u', $text)) {
            throw new ServiceError(400, 'bad_request', 'Text contains unsupported characters.');
        }
        $trimmed = trim($text);
        if ($trimmed === '') {
            throw new ServiceError(400, 'bad_request', 'Text is missing or too long.');
        }
        return $trimmed;
    }

    /** @return list<array{0:string,1:string}> the response lines after `status` and `protocol` */
    public function handle(string $action, array $form, int $gameProtocol, string $contentHash,
                           string $address): array
    {
        $now = $this->store->now();
        $channel = $gameProtocol . ':' . $contentHash;

        return $this->store->withLock('lobby.json', function (array $state) use (
            $action, $form, $channel, $address, $now
        ): array {
            $state = self::sweep($state, $now);

            if ($action === 'enter') {
                $name = self::decodeText($form['name'] ?? null, Limits::LOBBY_MAX_NAME_BYTES);
                $key = self::foldName($name);
                foreach ($state['sessions'] as $session) {
                    if ($session['channel'] === $channel && $session['key'] === $key) {
                        throw new ServiceError(409, 'name_taken', 'That name is in use. Choose another name.');
                    }
                }
                if (count($state['sessions']) >= Limits::LOBBY_MAX_SESSIONS) {
                    throw new ServiceError(503, 'capacity', 'Lobby chat is full. Try again shortly.');
                }
                $fromAddress = 0;
                foreach ($state['sessions'] as $session) {
                    if ($session['address'] === $address) {
                        $fromAddress++;
                    }
                }
                if ($fromAddress >= Limits::LOBBY_MAX_SESSIONS_PER_ADDRESS) {
                    throw new ServiceError(429, 'rate_limited',
                        'Too many chat sessions from this connection. Try again shortly.');
                }
                if (!isset($state['channels'][$channel])) {
                    if (count($state['channels']) >= Limits::LOBBY_MAX_CHANNELS) {
                        throw new ServiceError(503, 'capacity', 'Lobby chat is full. Try again shortly.');
                    }
                    $state['channels'][$channel] = ['history' => [], 'sequence' => 0];
                }
                $token = Store::randomHex(32);
                $state['sessions'][$token] = [
                    'name' => $name, 'key' => $key, 'channel' => $channel, 'address' => $address,
                    'deadline' => $now + Limits::LOBBY_SESSION_LIFETIME_MS,
                    'expiresAt' => $now + Limits::LOBBY_SESSION_TTL_MS,
                    'sends' => [], 
                ];
                return [$state, [['session', $token],
                                 ['cursor', (string)$state['channels'][$channel]['sequence']]]];
            }

            $token = $form['session'] ?? '';
            $session = (is_string($token) && preg_match('/^[0-9a-f]{64}$/D', $token))
                ? ($state['sessions'][$token] ?? null) : null;
            if (!is_array($session) || $session['channel'] !== $channel) {
                throw new ServiceError(403, 'session_expired', 'Confirm your name again to use lobby chat.');
            }
            $session['expiresAt'] = $now + Limits::LOBBY_SESSION_TTL_MS;
            $chan = $state['channels'][$channel] ?? ['history' => [], 'sequence' => 0];

            if ($action === 'say') {
                $text = self::decodeText($form['text'] ?? null, Limits::LOBBY_MAX_TEXT_BYTES);
                $session['sends'] = array_values(array_filter($session['sends'],
                    static fn($at) => $now - (int)$at < 10000));
                if (count($session['sends']) >= Limits::LOBBY_SAYS_PER_10S) {
                    throw new ServiceError(429, 'rate_limited',
                        'Please wait a moment before sending more messages.');
                }
                $session['sends'][] = $now;
                $chan['sequence']++;
                $chan['history'][] = ['id' => $chan['sequence'], 'time' => $now,
                                      'name' => $session['name'], 'text' => $text];
                if (count($chan['history']) > Limits::LOBBY_MAX_HISTORY) {
                    array_shift($chan['history']);
                }
                $state['channels'][$channel] = $chan;
                $state['sessions'][$token] = $session;
                return [$state, [['cursor', (string)$chan['sequence']]]];
            }

            $cursorRaw = $form['cursor'] ?? '';
            if ($action !== 'poll' || !is_string($cursorRaw)
                || !preg_match('/^[0-9]{1,15}$/D', $cursorRaw)
                || (int)$cursorRaw > (int)$chan['sequence']) {
                throw new ServiceError(400, 'bad_request', 'The chat request is not valid.');
            }
            $cursor = (int)$cursorRaw;
            $messages = [];
            foreach ($chan['history'] as $message) {
                if ((int)$message['id'] > $cursor && count($messages) < Limits::PAGE_SIZE) {
                    $messages[] = $message;
                }
            }
            $newCursor = $messages === [] ? (int)$chan['sequence'] : (int)end($messages)['id'];
            $oldest = $chan['history'] === [] ? (int)$chan['sequence'] + 1 : (int)$chan['history'][0]['id'];
            $lines = [['cursor', (string)$newCursor], ['gap', $oldest > $cursor + 1 ? '1' : '0']];
            foreach ($messages as $message) {
                $lines[] = ['chat', $message['id'] . '|' . bin2hex((string)$message['name'])
                    . '|' . bin2hex((string)$message['text'])];
            }
            $state['sessions'][$token] = $session;
            $state['channels'][$channel] = $chan;
            return [$state, $lines];
        });
    }

    /** Case- and compatibility-folded, so two names that render alike cannot both be claimed. */
    private static function foldName(string $name): string
    {
        $folded = class_exists('Normalizer')
            ? (string)Normalizer::normalize($name, Normalizer::FORM_KC) : $name;
        return strtolower($folded);
    }

    private static function sweep(array $state, int $now): array
    {
        $sessions = is_array($state['sessions'] ?? null) ? $state['sessions'] : [];
        $channels = is_array($state['channels'] ?? null) ? $state['channels'] : [];
        foreach ($sessions as $token => $session) {
            if (!is_array($session) || $now >= (int)$session['expiresAt'] || $now >= (int)$session['deadline']) {
                unset($sessions[$token]);
            }
        }
        foreach ($channels as $key => $channel) {
            $history = array_values(array_filter($channel['history'] ?? [],
                static fn(array $m) => $now - (int)$m['time'] < Limits::LOBBY_HISTORY_TTL_MS));
            $channels[$key]['history'] = $history;
            if ($history === []) {
                $inUse = false;
                foreach ($sessions as $session) {
                    if ($session['channel'] === $key) {
                        $inUse = true;
                        break;
                    }
                }
                if (!$inUse) {
                    unset($channels[$key]);
                }
            }
        }
        return ['sessions' => $sessions, 'channels' => $channels];
    }
}

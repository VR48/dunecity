<?php
declare(strict_types=1);

/**
 * What an offer, an answer and a candidate are allowed to be.
 *
 * The service does not need to understand WebRTC to carry a negotiation, but it does need to
 * know that what it is carrying is a data-only negotiation between two admitted players and
 * nothing else. The checks here are the ones that have a security meaning:
 *
 *   * exactly one media section, and it is `m=application ... webrtc-datachannel`. An `m=audio`
 *     or `m=video` section would ask a peer's stack to open a microphone or camera;
 *   * exactly one `a=fingerprint:sha-256`, which is the value the pair is then bound to. Two
 *     fingerprints, or none, cannot be bound to an admitted player;
 *   * no relay candidate and no TURN url anywhere, in a description or on its own. Gameplay
 *     through a third party is not something this deployment does, so it is refused here as
 *     well as at both clients;
 *   * strictly bounded size, line count and line length, and printable ASCII with CRLF. An SDP
 *     is a text format; a control character in one is somebody probing a parser.
 *
 * Everything else about the description is left alone. This is an introduction service, not a
 * WebRTC implementation, and rewriting a peer's SDP is exactly the thing it must not do.
 */
final class Sdp
{
    /** @return array{0:string,1:string} algorithm and uppercase colon-separated value */
    public static function validateDescription(string $sdp, string $kind, int $maxBytes): array
    {
        if ($sdp === '' || strlen($sdp) > min($maxBytes, Limits::SDP_MAX_BYTES)) {
            throw new ServiceError(413, 'signal_too_large', 'That connection offer is too large.');
        }
        if (preg_match('/[^\x09\x0a\x0d\x20-\x7e]/', $sdp)) {
            throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
        }
        self::refuseRelay($sdp);

        $lines = preg_split('/\r\n|\n/', rtrim($sdp, "\r\n"));
        if ($lines === false || count($lines) < 4 || count($lines) > Limits::SDP_MAX_LINES) {
            throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
        }

        $mediaSections = 0;
        $fingerprints = [];
        $fingerprintLines = 0;
        $setups = [];
        $sawVersion = false;
        foreach ($lines as $index => $line) {
            if (strlen($line) > Limits::SDP_MAX_LINE_BYTES) {
                throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
            }
            if (!preg_match('/^[a-z]=/', $line)) {
                throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
            }
            if ($index === 0) {
                if ($line !== 'v=0') {
                    throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
                }
                $sawVersion = true;
            }
            if (str_starts_with($line, 'm=')) {
                $mediaSections++;
                // Data only. A description that asks for audio or video is refused outright.
                if (!preg_match('/^m=application [0-9]{1,5} [A-Za-z0-9\/]{1,32} (webrtc-datachannel|[0-9]{1,5})$/D', $line)) {
                    throw new ServiceError(403, 'media_refused',
                        'This game only opens a data connection.');
                }
            }
            if (str_starts_with($line, 'a=fingerprint:')) {
                ++$fingerprintLines;
                $rest = substr($line, 14);
                $space = strpos($rest, ' ');
                if ($space === false || $space === 0) {
                    throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
                }
                $algorithm = strtolower(substr($rest, 0, $space));
                $value = strtoupper(trim(substr($rest, $space + 1)));
                // sha-256 only: it is what every stack in this deployment produces, and a
                // weaker digest is not something to accept on a peer's say-so.
                if ($algorithm !== 'sha-256'
                    || !preg_match('/^(?:[0-9A-F]{2}:){31}[0-9A-F]{2}$/D', $value)) {
                    throw new ServiceError(403, 'fingerprint_refused',
                        'That connection offer does not carry a usable certificate fingerprint.');
                }
                $fingerprints[$algorithm . ' ' . $value] = true;
            }
            if (str_starts_with($line, 'a=setup:')) {
                $setups[substr($line, 8)] = true;
            }
            if (str_starts_with($line, 'a=candidate:')) {
                self::validateCandidateLine(substr($line, 2));
            } elseif (str_contains($line, 'candidate:')) {
                // A line that carries a candidate without being an `a=candidate:` line is not
                // something any stack would act on, but it is also not something a stack would
                // produce. Refusing it keeps the embedded grammar exactly as strict as it was
                // before candidates grew a media-id envelope of their own.
                throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
            }
        }

        if (!$sawVersion || $mediaSections !== 1) {
            throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
        }
        if ($fingerprintLines !== 1 || count($fingerprints) !== 1) {
            // None cannot be bound to a player; two would leave which certificate is actually
            // presented up to whichever one the remote stack happens to pick.
            throw new ServiceError(403, 'fingerprint_refused',
                'That connection offer does not carry exactly one certificate fingerprint.');
        }
        foreach (array_keys($setups) as $setup) {
            if (!in_array($setup, ['actpass', 'active', 'passive', 'holdconn'], true)) {
                throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
            }
        }
        if ($kind === 'offer' && $setups !== [] && !isset($setups['actpass'])
            && !isset($setups['active']) && !isset($setups['passive'])) {
            throw new ServiceError(400, 'bad_signal', 'That connection offer is not valid.');
        }

        $only = array_key_first($fingerprints);
        [$algorithm, $value] = explode(' ', (string)$only, 2);
        return [$algorithm, $value];
    }

    /**
     * One trickled candidate, in the shape both stacks put on the wire: `<sdpMid>|<candidate>`.
     *
     * The media id is carried alongside the line because both ends need it to place the candidate
     * on the right description - libdatachannel takes it as a separate argument and the browser
     * needs it to build an RTCIceCandidate. It is bounded separately from, and far below, an SDP:
     * see P2PSignal::isAcceptableCandidatePayload, which is the client-side half of this check.
     *
     * Only host, server-reflexive and peer-reflexive candidates exist in a direct deployment. A
     * relay candidate means a TURN server, which this service neither offers nor lets a peer
     * introduce.
     */
    public static function validateCandidate(string $payload): void
    {
        if ($payload === ''
            || strlen($payload) > Limits::CANDIDATE_MAX_BYTES + Limits::CANDIDATE_MID_CHARS + 1) {
            throw new ServiceError(413, 'signal_too_large', 'That connection detail is too large.');
        }
        $split = strpos($payload, '|');
        if ($split === false || $split > Limits::CANDIDATE_MID_CHARS) {
            throw new ServiceError(400, 'bad_signal', 'That connection detail is not valid.');
        }
        $mid  = substr($payload, 0, $split);
        $line = substr($payload, $split + 1);
        // An empty media id is what a browser produces for a bundled data-only description.
        if ($mid !== '' && preg_match('/^[A-Za-z0-9_-]{1,64}$/D', $mid) !== 1) {
            throw new ServiceError(400, 'bad_signal', 'That connection detail is not valid.');
        }
        if ($line === '' || strlen($line) > Limits::CANDIDATE_MAX_BYTES
            || preg_match('/[^\x20-\x7e]/', $line)) {
            throw new ServiceError(400, 'bad_signal', 'That connection detail is not valid.');
        }
        self::refuseRelay($line);
        self::validateCandidateLine(str_starts_with($line, 'a=') ? substr($line, 2) : $line);
    }

    /** One `candidate:...` line, in the grammar of RFC 8839 restricted to what is allowed here. */
    private static function validateCandidateLine(string $line): void
    {
        // The trailing name/value pairs are what a real stack actually appends: Chromium sends
        // `generation`, `ufrag`, `network-id` and `network-cost`, a reflexive candidate adds
        // `raddr`/`rport`, and a TCP one adds `tcptype`. An ICE ufrag may contain '+' and '/'
        // and RFC 8839 allows it up to 256 characters, so the value bound is 256 rather than
        // something tighter: the line's own 2048-byte bound should be what binds, not an
        // accidental ceiling hidden inside the grammar.
        if (!preg_match(
            '/^candidate:[A-Za-z0-9+\/]{1,32} [1-2] (?:UDP|TCP|udp|tcp) [0-9]{1,10} '
            . '(?:[0-9.]{7,15}|[0-9A-Fa-f:.]{2,45}|[A-Za-z0-9\-]{1,63}(?:\.[A-Za-z0-9\-]{1,63}){0,4}) '
            . '[0-9]{1,5} typ (host|srflx|prflx)'
            . '(?: [A-Za-z0-9_\-]{1,24} [A-Za-z0-9.:+\/\-]{1,256}){0,12}$/D',
            $line)) {
            throw new ServiceError(403, 'candidate_refused',
                'That connection detail is not one this game accepts.');
        }
    }

    /**
     * A relay candidate or a TURN url, wherever it appears - on its own, inside a description, or
     * spelled with different capitalisation.
     */
    private static function refuseRelay(string $text): void
    {
        $lowered = strtolower($text);
        if (str_contains($lowered, 'typ relay') || str_contains($lowered, 'turn:')
            || str_contains($lowered, 'turns:') || str_contains($lowered, 'relay-addr')) {
            throw new ServiceError(403, 'relay_refused',
                'This game does not route play through a relay server.');
        }
    }
}

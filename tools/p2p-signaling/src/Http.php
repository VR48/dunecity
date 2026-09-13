<?php
declare(strict_types=1);

/** A refusal with a status, a fixed code and one player-facing sentence. */
final class ServiceError extends RuntimeException
{
    public function __construct(public readonly int $status, public readonly string $errorCode,
                               string $message)
    {
        parent::__construct($message);
    }
}

/**
 * Reading a request and writing an answer, both bounded.
 *
 * Everything a caller can influence is length-checked before it is looked at, and everything
 * this service writes is checked against the bounds of the parser that will read it: a response
 * that a client cannot parse is indistinguishable, at the client, from the service being down.
 */
final class Http
{
    /** @var array<string,string>|null */
    private ?array $form = null;

    public function __construct(private readonly Config $config)
    {
    }

    public function method(): string
    {
        $method = $_SERVER['REQUEST_METHOD'] ?? '';
        return is_string($method) ? strtoupper($method) : '';
    }

    /** The request path with the deployment's own prefix removed, query string discarded. */
    public function path(): string
    {
        $uri = $_SERVER['REQUEST_URI'] ?? '';
        if (!is_string($uri) || $uri === '' || strlen($uri) > 512) {
            return '';
        }
        $question = strpos($uri, '?');
        if ($question !== false) {
            $uri = substr($uri, 0, $question);
        }
        $hash = strpos($uri, '#');
        if ($hash !== false) {
            $uri = substr($uri, 0, $hash);
        }
        $base = $this->config->get('base_path');
        if (!is_string($base)) {
            $script = $_SERVER['SCRIPT_NAME'] ?? '';
            $base = is_string($script) ? rtrim(str_replace('\\', '/', dirname($script)), '/') : '';
            if ($base === '.' || $base === '/') {
                $base = '';
            }
        }
        if ($base !== '' && str_starts_with($uri, $base)) {
            $uri = substr($uri, strlen($base));
        }
        if ($uri === '' || $uri[0] !== '/') {
            $uri = '/' . $uri;
        }
        // Nothing downstream ever turns a path into a filesystem path, but a path that is not
        // literally one of the known routes is a 404 and a traversal attempt should read as one.
        if (str_contains($uri, '..') || str_contains($uri, '//') || str_contains($uri, "\0")) {
            return '';
        }
        return $uri;
    }

    /** REMOTE_ADDR only: a forwarded-for header is a caller-supplied string, not an address. */
    public function address(): string
    {
        $address = $_SERVER['REMOTE_ADDR'] ?? '';
        if (!is_string($address) || $address === '' || strlen($address) > 64) {
            return 'unknown';
        }
        return $address;
    }

    public function origin(): ?string
    {
        $origin = $_SERVER['HTTP_ORIGIN'] ?? null;
        return is_string($origin) ? $origin : null;
    }

    /**
     * The session credential. It is a request header and never a query parameter, so it does not
     * reach an access log, a proxy log or a browser history entry.
     */
    public function sessionHeader(): string
    {
        $value = $_SERVER['HTTP_X_DUNE_SESSION'] ?? '';
        if (!is_string($value) || !preg_match('/^[0-9a-f]{64}$/D', $value)) {
            return '';
        }
        return $value;
    }

    /**
     * CORS for one request: an exactly allowlisted origin is echoed, anything else gets no
     * header at all. Never `*`, never `null`, and never `Allow-Credentials` - the grant travels
     * in a response body and must not be obtainable on the strength of an ambient cookie.
     */
    public function corsHeaders(): array
    {
        $out = ['Vary' => 'Origin'];
        $origin = $this->origin();
        if (is_string($origin) && strlen($origin) <= Limits::MAX_ORIGIN_CHARS
            && in_array($origin, $this->config->allowedOrigins(), true)) {
            $out['Access-Control-Allow-Origin'] = $origin;
        }
        return $out;
    }

    /**
     * A request with no Origin is accepted: native clients do not send one. That is not evidence
     * that the caller is a native client - the grant is the authentication - it only means Origin
     * is defence in depth against a hostile page driving somebody's browser.
     */
    public function checkOrigin(): void
    {
        $origin = $this->origin();
        if ($origin === null) {
            return;
        }
        if (strlen($origin) > Limits::MAX_ORIGIN_CHARS
            || !in_array($origin, $this->config->allowedOrigins(), true)) {
            throw new ServiceError(403, 'forbidden_origin', 'That origin is not allowed.');
        }
    }

    /** Production is HTTPS. Plaintext is only ever tolerated on an explicit loopback dev setup. */
    public function requireSecureTransport(): void
    {
        $https = $_SERVER['HTTPS'] ?? '';
        if (is_string($https) && $https !== '' && strtolower($https) !== 'off') {
            return;
        }
        if ($this->config->get('allow_plaintext_loopback') === true && $this->isLoopback()) {
            return;
        }
        throw new ServiceError(403, 'insecure_transport', 'This service requires a secure connection.');
    }

    private function isLoopback(): bool
    {
        $address = $this->address();
        if ($address === '::1' || $address === '[::1]') {
            return true;
        }
        return str_starts_with($address, '127.');
    }

    /**
     * Strict urlencoded parse.
     *
     * A repeated field is a hard failure rather than a last-one-wins: two different values for
     * `visibility` or `to` must never be resolvable by whichever end of the string a parser
     * happens to read from.
     *
     * @return array<string,string>
     */
    public function form(int $maxBodyBytes, int $maxFieldBytes): array
    {
        if ($this->form !== null) {
            return $this->form;
        }
        $type = $_SERVER['CONTENT_TYPE'] ?? '';
        if (is_string($type) && $type !== '') {
            $base = strtolower(trim(explode(';', $type)[0]));
            if ($base !== 'application/x-www-form-urlencoded') {
                throw new ServiceError(415, 'bad_request', 'The request format is not supported.');
            }
        }
        $declared = $_SERVER['CONTENT_LENGTH'] ?? null;
        if (is_string($declared) && preg_match('/^[0-9]{1,12}$/D', $declared)
            && (int)$declared > $maxBodyBytes) {
            throw new ServiceError(413, 'bad_request', 'Request body is too large.');
        }
        $handle = fopen('php://input', 'rb');
        if ($handle === false) {
            throw new ServiceError(400, 'bad_request', 'Request failed.');
        }
        // One byte past the bound, so a body that is exactly too large is refused rather than
        // silently truncated into a half-form that could still be acted on.
        $body = (string)stream_get_contents($handle, $maxBodyBytes + 1);
        fclose($handle);
        if (strlen($body) > $maxBodyBytes) {
            throw new ServiceError(413, 'bad_request', 'Request body is too large.');
        }

        $out = [];
        if ($body !== '') {
            $pairs = explode('&', $body);
            if (count($pairs) > Limits::HTTP_MAX_FORM_FIELDS) {
                throw new ServiceError(400, 'bad_request', 'Too many form fields.');
            }
            foreach ($pairs as $pair) {
                if ($pair === '') {
                    continue;
                }
                if (strlen($pair) > $maxFieldBytes) {
                    throw new ServiceError(400, 'bad_request', 'A form field is too long.');
                }
                $eq = strpos($pair, '=');
                if ($eq === false || $eq === 0) {
                    throw new ServiceError(400, 'bad_request', 'A form field is not valid.');
                }
                $key = rawurldecode(str_replace('+', ' ', substr($pair, 0, $eq)));
                $value = rawurldecode(str_replace('+', ' ', substr($pair, $eq + 1)));
                if (strlen($key) > 32 || !preg_match('/^[A-Za-z][A-Za-z0-9_]{0,31}$/D', $key)) {
                    throw new ServiceError(400, 'bad_request', 'A form field is not valid.');
                }
                if (array_key_exists($key, $out)) {
                    throw new ServiceError(400, 'bad_request', 'A form field was repeated.');
                }
                $out[$key] = $value;
            }
        }
        $this->form = $out;
        return $out;
    }

    // --------------------------------------------------------------------------------------
    // Responses
    // --------------------------------------------------------------------------------------

    /**
     * Renders `key=value` lines and refuses to emit anything the client's parser would reject.
     *
     * The two envelopes differ: the legacy admission parser accepts 16 lines of 512 bytes in
     * 8192 bytes, the signaling parser accepts 256 lines of 2048 bytes in 65536. Exceeding
     * either one produces an answer that fails to parse, which is why this throws rather than
     * truncating.
     */
    public static function render(array $lines, bool $signaling): string
    {
        $maxLines = $signaling ? Limits::SIGNAL_MAX_RESPONSE_LINES : Limits::ADMISSION_MAX_RESPONSE_LINES;
        $maxLine  = $signaling ? Limits::SIGNAL_MAX_LINE_BYTES : Limits::ADMISSION_MAX_LINE_BYTES;
        $maxValue = $signaling ? Limits::SIGNAL_MAX_VALUE_BYTES : Limits::ADMISSION_MAX_VALUE_BYTES;
        $maxBytes = $signaling ? Limits::SIGNAL_MAX_RESPONSE_BYTES : Limits::ADMISSION_MAX_RESPONSE_BYTES;

        if (count($lines) > $maxLines) {
            throw new LogicException('response exceeds its own documented line bound');
        }
        $text = '';
        foreach ($lines as [$key, $value]) {
            $key = (string)$key;
            $value = (string)$value;
            if (!preg_match('/^[A-Za-z]{1,24}$/D', $key)) {
                throw new LogicException('response key is not a bare word');
            }
            if (strlen($value) > $maxValue) {
                throw new LogicException('response value exceeds its own documented bound');
            }
            if (preg_match('/[^\x20-\x7e]/', $value)) {
                throw new LogicException('response value is not printable ASCII');
            }
            $line = $key . '=' . $value;
            if (strlen($line) > $maxLine) {
                throw new LogicException('response line exceeds its own documented bound');
            }
            $text .= $line . "\n";
        }
        if (strlen($text) > $maxBytes) {
            throw new LogicException('response exceeds its own documented byte bound');
        }
        return $text;
    }

    public function send(int $status, array $lines, bool $signaling): void
    {
        $body = self::render($lines, $signaling);
        http_response_code($status);
        foreach ($this->corsHeaders() as $name => $value) {
            header($name . ': ' . $value);
        }
        header('Content-Type: text/plain; charset=utf-8');
        header('Content-Length: ' . strlen($body));
        // Grants, invitation codes and session tokens travel in these bodies.
        header('Cache-Control: no-store');
        header('Pragma: no-cache');
        header('X-Content-Type-Options: nosniff');
        header('Referrer-Policy: no-referrer');
        echo $body;
    }

    public function sendError(ServiceError $error, bool $signaling): void
    {
        $this->send($error->status, [
            ['status', 'error'],
            ['code', $error->errorCode],
            ['message', self::sanitize($error->getMessage())],
        ], $signaling);
    }

    public function sendPreflight(): void
    {
        $cors = $this->corsHeaders();
        if (!isset($cors['Access-Control-Allow-Origin'])) {
            throw new ServiceError(403, 'forbidden_origin', 'That origin is not allowed.');
        }
        $requested = $_SERVER['HTTP_ACCESS_CONTROL_REQUEST_METHOD'] ?? null;
        if (is_string($requested) && strtoupper($requested) !== 'POST') {
            throw new ServiceError(403, 'forbidden_origin', 'Only POST is allowed here.');
        }
        http_response_code(204);
        foreach ($cors as $name => $value) {
            header($name . ': ' . $value);
        }
        header('Access-Control-Allow-Methods: POST');
        header('Access-Control-Allow-Headers: content-type, x-dune-session');
        header('Access-Control-Max-Age: ' . Limits::PREFLIGHT_MAX_AGE_SECONDS);
        header('Cache-Control: no-store');
        header('Content-Length: 0');
    }

    /**
     * Lowercase hex of an even length.
     *
     * Deliberately not a regular expression: `/^(?:[0-9a-f]{2})+$/` against 131072 characters of
     * hex-encoded SDP exceeds PCRE's recursion limit, and preg_match then returns false - which
     * reads exactly like "not hex" and would refuse every large description. strspn has no such
     * limit and no such failure mode.
     */
    public static function isHex(string $text): bool
    {
        return $text !== '' && strlen($text) % 2 === 0
            && strspn($text, '0123456789abcdef') === strlen($text);
    }

    /** A service message ends up in the player's UI, so it never carries control characters. */
    public static function sanitize(string $text): string
    {
        $out = '';
        $length = strlen($text);
        for ($i = 0; $i < $length && strlen($out) < Limits::MAX_MESSAGE_CHARS; $i++) {
            $c = ord($text[$i]);
            $out .= ($c >= 32 && $c <= 126) ? $text[$i] : ' ';
        }
        return $out;
    }
}

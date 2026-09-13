<?php
declare(strict_types=1);

/**
 * The operator's configuration, and the refusal to guess at one.
 *
 * The state directory is the only place this service writes, and it holds invitation codes,
 * grants and session tokens. There is deliberately no default for it: falling back to the web
 * root would publish the tokens, and falling back to /tmp would put them in a world-readable
 * directory on a shared host. A missing or unsafe configuration is a 503 with a generic message,
 * which is a service an operator will notice and fix, rather than a service that quietly leaks.
 */
final class Config
{
    /** @var array<string,mixed> */
    private array $values;

    private function __construct(array $values)
    {
        $this->values = $values;
    }

    /**
     * @throws ConfigError with an operator-facing reason. The reason is logged, never returned.
     */
    public static function load(): self
    {
        $path = getenv('DUNECITY_P2P_CONFIG');
        if (!is_string($path) || $path === '') {
            $path = dirname(__DIR__) . '/config/config.php';
        }
        if (!is_string($path) || $path === '' || $path[0] !== '/') {
            throw new ConfigError('DUNECITY_P2P_CONFIG must be an absolute path');
        }
        if (is_link($path)) {
            throw new ConfigError('the configuration file must not be a symlink');
        }
        if (!is_file($path) || !is_readable($path)) {
            throw new ConfigError('the configuration file is missing: ' . $path);
        }
        /** @psalm-suppress UnresolvableInclude */
        $raw = require $path;
        if (!is_array($raw)) {
            throw new ConfigError('the configuration file must return an array');
        }
        return new self(self::validate($raw));
    }

    private static function validate(array $raw): array
    {
        $stateDir = $raw['state_dir'] ?? null;
        if (!is_string($stateDir) || $stateDir === '' || $stateDir[0] !== '/') {
            throw new ConfigError("'state_dir' must be an absolute path outside the web root");
        }
        $stateDir = rtrim($stateDir, '/');
        if ($stateDir === '' || str_contains($stateDir, "\0") || basename($stateDir) === '') {
            throw new ConfigError("'state_dir' is not a usable directory path");
        }
        // Existence, creation, ownership and mode are Store's bootstrap: it is the thing that can
        // create the directory as the PHP worker, and it is where the post-open handle checks
        // live. What is decided here is only what the operator wrote.
        if (is_link($stateDir)) {
            throw new ConfigError("'state_dir' must not be a symlink");
        }
        $parent = dirname($stateDir);
        $parentReal = realpath($parent);
        if ($parentReal === false || $parentReal !== $parent) {
            // A symlinked ancestor would make every later check be about a different directory.
            // On macOS this is what /var/folders (really /private/var/folders) trips over.
            throw new ConfigError("'state_dir' must be given with a canonical parent path; "
                . $parent . ' resolves to ' . var_export($parentReal, true));
        }
        // A document root inside the state directory - or the other way round - would publish
        // grants and session tokens as static files.
        $docRoot = $_SERVER['DOCUMENT_ROOT'] ?? '';
        if (is_string($docRoot) && $docRoot !== '') {
            $docReal = realpath($docRoot);
            if ($docReal !== false
                && (str_starts_with($stateDir . '/', rtrim($docReal, '/') . '/')
                    || str_starts_with($docReal . '/', $stateDir . '/'))) {
                throw new ConfigError("'state_dir' must not be inside the document root");
            }
        }

        $origins = $raw['allowed_origins'] ?? [];
        if (!is_array($origins)) {
            throw new ConfigError("'allowed_origins' must be a list");
        }
        $clean = [];
        foreach ($origins as $origin) {
            if (!is_string($origin) || $origin === 'null' || !self::isCanonicalOrigin($origin)) {
                throw new ConfigError('allowed_origins entry is not a canonical http(s) origin: '
                    . (is_string($origin) ? $origin : gettype($origin)));
            }
            $clean[] = $origin;
        }

        $ice = $raw['ice_servers'] ?? ['stun:stun.l.google.com:19302'];
        if (!is_array($ice) || count($ice) > Limits::SIGNAL_MAX_ICE_SERVERS) {
            throw new ConfigError("'ice_servers' must be a list of at most "
                . Limits::SIGNAL_MAX_ICE_SERVERS . ' STUN urls');
        }
        foreach ($ice as $url) {
            // A TURN url would mean gameplay travelling through a third party. There is no
            // configuration of this service in which that is allowed.
            if (!is_string($url) || !preg_match('/^stuns?:[A-Za-z0-9.\-:\[\]?=]{3,122}$/D', $url)
                || str_contains($url, '@')) {
                throw new ConfigError("'ice_servers' accepts stun: and stuns: urls only");
            }
        }

        $base = $raw['public_base_url'] ?? '';
        if (!is_string($base) || $base === '' || !preg_match('#^https?://[^\s/?\#]{1,120}(/[A-Za-z0-9._~\-/]{0,80})?$#', $base)
            || str_ends_with($base, '/')) {
            throw new ConfigError("'public_base_url' must be an absolute url with no trailing slash");
        }
        $loopback = (bool)($raw['allow_plaintext_loopback'] ?? false);
        if (str_starts_with($base, 'http://') && !$loopback) {
            throw new ConfigError("'public_base_url' must be https unless 'allow_plaintext_loopback' is on");
        }

        $app = $raw['app'] ?? 'dunecity';
        if (!is_string($app) || !preg_match('/^[A-Za-z0-9_-]{1,32}$/D', $app)) {
            throw new ConfigError("'app' must be a short token");
        }

        $basePath = $raw['base_path'] ?? null;
        if ($basePath !== null && (!is_string($basePath)
            || ($basePath !== '' && !preg_match('#^/[A-Za-z0-9._~\-/]{0,80}$#', $basePath)))) {
            throw new ConfigError("'base_path' must be empty or an absolute path prefix");
        }

        $maxSignal = (int)($raw['max_signal_payload_bytes'] ?? 0);
        if ($maxSignal === 0) {
            $maxSignal = Limits::SIGNAL_MAX_PAYLOAD_BYTES;
        }
        // One record must always fit one response line: `sig=<from>|<seq>|<kind>|<hex>`.
        if ($maxSignal * 2 + 40 > Limits::SIGNAL_MAX_VALUE_BYTES) {
            throw new ConfigError("'max_signal_payload_bytes' would not fit one response line");
        }
        if ($maxSignal < 256 || $maxSignal > Limits::SIGNAL_MAX_PAYLOAD_BYTES) {
            throw new ConfigError("'max_signal_payload_bytes' is out of range");
        }

        return [
            'state_dir'                => $stateDir,
            'allowed_origins'          => $clean,
            'ice_servers'              => array_values($ice),
            'public_base_url'          => $base,
            'allow_plaintext_loopback' => $loopback,
            'app'                      => $app,
            'base_path'                => $basePath,
            'required_game_protocol'   => (int)($raw['required_game_protocol'] ?? 0),
            'max_signal_payload_bytes' => $maxSignal,
            'analytics_enabled'        => (bool)($raw['analytics_enabled'] ?? true),
            'log_enabled'              => (bool)($raw['log_enabled'] ?? true),
        ];
    }

    /**
     * Exactly what a browser puts in an Origin header: scheme://host[:port], no credentials, no
     * path, no query, no fragment, no trailing slash, no default port. Anything else in the
     * allowlist could never match a real header, so accepting it would silently allow nothing -
     * or, worse, look like it allowed something.
     */
    public static function isCanonicalOrigin(string $value): bool
    {
        if ($value === '' || strlen($value) > Limits::MAX_ORIGIN_CHARS) {
            return false;
        }
        if (!preg_match('#^(https?)://([A-Za-z0-9.\-]{1,120}|\[[0-9A-Fa-f:.]{2,45}\])(?::([0-9]{1,5}))?$#',
                        $value, $m)) {
            return false;
        }
        if (isset($m[3]) && $m[3] !== '') {
            $port = (int)$m[3];
            if ($port < 1 || $port > 65535) {
                return false;
            }
            if (($m[1] === 'http' && $port === 80) || ($m[1] === 'https' && $port === 443)) {
                return false;   // a default port never appears in a real Origin header
            }
        }
        return true;
    }

    public function get(string $key): mixed
    {
        return $this->values[$key] ?? null;
    }

    public function stateDir(): string
    {
        return (string)$this->values['state_dir'];
    }

    /** @return list<string> */
    public function allowedOrigins(): array
    {
        return $this->values['allowed_origins'];
    }
}

final class ConfigError extends RuntimeException
{
}

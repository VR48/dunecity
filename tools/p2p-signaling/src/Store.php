<?php
declare(strict_types=1);

/**
 * Bounded JSON state files in a private directory outside the web root.
 *
 * There is no PDO SQLite on the production host and no daemon may be added, so the state is a
 * small number of files. What that costs is care, and the care is specific:
 *
 * **A stable lock file, never the data file.** Every state file `X.json` has a companion `X.lock`
 * that is created once and never removed. Readers and writers all take `flock(LOCK_EX)` on that
 * lock, and the data file is replaced by `rename()` underneath it. Locking the data file itself
 * would be wrong: the moment a writer replaces it, the lock two workers are holding refers to two
 * different inodes and they both believe they won.
 *
 * **Writes are atomic or they do not happen.** A mutation is written to a fresh temp file in the
 * same directory, written in full (a short `fwrite` is a failure, not a success), flushed, and
 * then `rename()`d over the data file. `rename()` within one directory is atomic, so a reader
 * that is not holding the lock still sees one whole version, and a crash or a full disk leaves
 * the *previous* state intact. Truncating the live file in place - which an earlier draft of this
 * class did - can erase the grant and rate state that authorisation depends on.
 *
 * **Corrupt state fails closed.** A data file that is non-empty but unreadable, oversized or not
 * valid JSON is a 503, not an empty array. Treating it as fresh would silently reset the room
 * directory, the outstanding grants and the rate counters, which is precisely the state an
 * attacker would want reset. Only a file that does not exist yet, or is exactly zero bytes,
 * initialises.
 *
 * **What protects the paths.** This class does *not* have `O_NOFOLLOW`: PHP's `fopen()` has no
 * such flag. The actual model is two things together:
 *
 *   1. the state directory is 0700 and owned by the user PHP runs as, inside a parent that is not
 *      world-writable and whose path contains no symlink (checked once, at bootstrap), so no
 *      other account can create a name in it at all; and
 *   2. every handle is verified *after* it is open - `fstat` says regular file, owned by us, not
 *      group- or world-accessible, and its (dev, ino) equals what `lstat` reports for the name we
 *      asked for. A name that was swapped for a symlink between the check and the open therefore
 *      fails the comparison rather than being written through.
 *
 * New files are created with `fopen(..., 'xb')`, which fails rather than following an existing
 * symlink, and are chmod'ed 0600 immediately; the window before that chmod is inside a directory
 * no other account can read.
 *
 * **Lock discipline.** A request holds at most one of these locks at a time. Where an operation
 * needs both the directory and a room it takes them one after the other, releasing the first
 * before taking the second, so there is no lock order to get wrong. The cost is that a peer count
 * in the directory can be one operation stale, which is a listing briefly off by one and never an
 * authorisation decision.
 */
final class Store
{
    /** Cached once per process: the uid this PHP worker actually runs as. */
    private static ?int $uid = null;

    /** The fixed set of journal files this class will append to. */
    private const JOURNALS = ['analytics.jsonl', 'log.jsonl'];

    private bool $ready = false;

    public function __construct(private readonly string $dir)
    {
    }

    public function now(): int
    {
        return (int)round(microtime(true) * 1000);
    }

    /** A room id is 8 lowercase hex characters this service generated. Nothing else is a path. */
    public static function isRoomId(string $id): bool
    {
        return preg_match('/^[0-9a-f]{8}$/D', $id) === 1;
    }

    private static function unavailable(): ServiceError
    {
        // One message for every storage refusal. An operator gets the detail from the error log;
        // a caller gets nothing it could use to map the filesystem.
        return new ServiceError(503, 'unavailable', 'The game service is not available right now.');
    }

    // ------------------------------------------------------------------------------------
    // Bootstrap and identity
    // ------------------------------------------------------------------------------------

    /**
     * Verifies - and on first run creates - the private state directory.
     *
     * The bootstrap is deliberately one an ordinary deploy account can satisfy without an
     * administrator: the deploy user pre-creates the *parent* (group www-data, mode 2770) and
     * this code creates the state directory inside it, 0700, owned by the PHP worker. Nobody has
     * to chown anything to www-data.
     */
    private function ready(): void
    {
        if ($this->ready) {
            return;
        }
        $parent = dirname($this->dir);
        // The whole path to the parent must contain no symlink, because every check below is
        // about this directory and a symlinked ancestor would make them all be about another one.
        if (is_link($parent) || !is_dir($parent) || realpath($parent) !== $parent) {
            error_log('dunecity-p2p: state_dir parent is missing, a symlink, or not canonical: ' . $parent);
            throw self::unavailable();
        }
        $parentStat = @lstat($parent);
        if ($parentStat === false || ($parentStat['mode'] & 0o002) !== 0) {
            error_log('dunecity-p2p: state_dir parent is world-writable: ' . $parent);
            throw self::unavailable();
        }

        if (!is_dir($this->dir)) {
            if (is_link($this->dir) || file_exists($this->dir)) {
                error_log('dunecity-p2p: state_dir exists but is not a directory: ' . $this->dir);
                throw self::unavailable();
            }
            // mkdir's mode is masked by the umask, so the chmod is the one that decides.
            @mkdir($this->dir, 0o700);
            @chmod($this->dir, 0o700);
        }
        self::assertPrivateDirectory($this->dir);
        if (realpath($this->dir) !== $this->dir) {
            error_log('dunecity-p2p: state_dir is not its own real path: ' . $this->dir);
            throw self::unavailable();
        }
        $this->ready = true;
    }

    /** A directory that only this worker can read: not a symlink, 0700, and owned by us. */
    private static function assertPrivateDirectory(string $path): void
    {
        if (is_link($path) || !is_dir($path)) {
            error_log('dunecity-p2p: not a private directory: ' . $path);
            throw self::unavailable();
        }
        $stat = @lstat($path);
        if ($stat === false || ($stat['mode'] & 0o170000) !== 0o040000
            || ($stat['mode'] & 0o077) !== 0
            || (int)$stat['uid'] !== self::uid($path)) {
            error_log('dunecity-p2p: state directory has unsafe ownership or mode: ' . $path);
            throw self::unavailable();
        }
    }

    /**
     * The uid this worker runs as.
     *
     * `posix_geteuid()` answers this directly, but the ext/posix extension is not guaranteed on a
     * shared host and the brief requires the standard API alone. The fallback creates a file
     * exclusively - so it is certainly ours - and reads the owner back off the open handle.
     *
     * @param string $within a directory we are already allowed to write in
     */
    private static function uid(string $within): int
    {
        if (self::$uid !== null) {
            return self::$uid;
        }
        if (function_exists('posix_geteuid')) {
            $uid = @posix_geteuid();
            if (is_int($uid)) {
                return self::$uid = $uid;
            }
        }
        $probe = $within . '/.owner-' . bin2hex(random_bytes(8));
        $handle = @fopen($probe, 'xb');
        if ($handle === false) {
            error_log('dunecity-p2p: cannot write in the state directory: ' . $within);
            throw self::unavailable();
        }
        $stat = fstat($handle);
        fclose($handle);
        @unlink($probe);
        if ($stat === false) {
            throw self::unavailable();
        }
        return self::$uid = (int)$stat['uid'];
    }

    /**
     * What an open handle has to be before anything is read from or written to it.
     *
     * This is the post-open half of the symlink defence described in the class comment: it does
     * not stop a name being swapped, it makes a swapped name fail. A handle that is not a regular
     * file, not ours, readable by anyone else, or no longer the object the name points at, is
     * refused rather than used.
     *
     * @param resource $handle
     */
    private static function verifyOpened($handle, string $path, int $uid): void
    {
        $opened = fstat($handle);
        $named  = @lstat($path);
        if ($opened === false || $named === false
            || ($opened['mode'] & 0o170000) !== 0o100000
            || (int)$opened['uid'] !== $uid
            || ($opened['mode'] & 0o077) !== 0
            || $opened['ino'] !== $named['ino']
            || $opened['dev'] !== $named['dev']) {
            error_log('dunecity-p2p: refusing an unsafe state handle: ' . $path);
            throw self::unavailable();
        }
    }

    /** A short write is a failure. Acknowledging one would commit a truncated state file. */
    private static function writeAll($handle, string $data): bool
    {
        $total = strlen($data);
        $written = 0;
        while ($written < $total) {
            $chunk = @fwrite($handle, substr($data, $written));
            if ($chunk === false || $chunk <= 0) {
                return false;
            }
            $written += $chunk;
        }
        return $written === $total;
    }

    // ------------------------------------------------------------------------------------
    // Paths
    // ------------------------------------------------------------------------------------

    /**
     * The data file and the stable lock file for one state name.
     *
     * Room locks are taken from a fixed pool of 256, keyed by the first byte of the room id,
     * rather than one lock file per room. That keeps the number of lock files bounded and - more
     * importantly - means a lock file is never deleted: unlinking a lock somebody else is already
     * waiting on would hand two workers a lock on two different inodes.
     *
     * @return array{0:string,1:string}
     */
    private function pathsFor(string $name): array
    {
        if ($name === 'index.json' || $name === 'lobby.json' || $name === 'rate.json') {
            $stem = substr($name, 0, -5);
            return [$this->dir . '/' . $name, $this->dir . '/' . $stem . '.lock'];
        }
        if (preg_match('#^rooms/([0-9a-f]{8})\.json$#', $name, $match) === 1) {
            $this->ensureSubdir('rooms');
            return [$this->dir . '/rooms/' . $match[1] . '.json',
                    $this->dir . '/rooms/lock-' . substr($match[1], 0, 2) . '.lock'];
        }
        throw new LogicException('state file name is not one this service generates');
    }

    /**
     * Creates the rooms subdirectory if it is missing, and verifies it if it is not.
     *
     * The link check comes first on purpose: `is_dir()` follows symlinks, so asking "is it a
     * directory" before "is it a link" accepts a symlinked directory as a real one.
     */
    private function ensureSubdir(string $name): void
    {
        $path = $this->dir . '/' . $name;
        if (is_link($path)) {
            error_log('dunecity-p2p: state subdirectory is a symlink: ' . $path);
            throw self::unavailable();
        }
        if (!is_dir($path)) {
            if (file_exists($path)) {
                error_log('dunecity-p2p: state subdirectory is not a directory: ' . $path);
                throw self::unavailable();
            }
            @mkdir($path, 0o700);
            @chmod($path, 0o700);
        }
        self::assertPrivateDirectory($path);
    }

    // ------------------------------------------------------------------------------------
    // Read-modify-write
    // ------------------------------------------------------------------------------------

    /**
     * Runs `$mutator` with the decoded contents of a state file while holding its stable lock,
     * and atomically replaces the file with whatever it returns. Returning null for the new state
     * leaves the file untouched.
     *
     * @param callable(array):array{0:?array,1:mixed} $mutator [new state or null, result]
     * @return mixed the mutator's result
     */
    public function withLock(string $name, callable $mutator): mixed
    {
        $this->ready();
        [$dataPath, $lockPath] = $this->pathsFor($name);
        $uid = self::uid($this->dir);
        $lock = $this->acquire($lockPath, $uid);
        try {
            $state = $this->readState($dataPath, $uid);
            [$next, $result] = $mutator($state);
            if ($next !== null) {
                $this->commit($dataPath, $next, $uid);
            }
            return $result;
        } finally {
            flock($lock, LOCK_UN);
            fclose($lock);
        }
    }

    /**
     * Opens and exclusively locks the stable lock file, creating it on first use.
     *
     * @return resource
     */
    private function acquire(string $lockPath, int $uid)
    {
        if (!file_exists($lockPath)) {
            $created = @fopen($lockPath, 'xb');
            if ($created !== false) {
                @chmod($lockPath, 0o600);
                fclose($created);
            }
        }
        $handle = @fopen($lockPath, 'r+b');
        if ($handle === false) {
            error_log('dunecity-p2p: cannot open the lock file: ' . $lockPath);
            throw self::unavailable();
        }
        self::verifyOpened($handle, $lockPath, $uid);
        if (!flock($handle, LOCK_EX)) {
            fclose($handle);
            throw self::unavailable();
        }
        // The name could have been replaced while this call was blocked on the lock; if it was,
        // the lock being held is on an orphaned inode and protects nothing.
        try {
            self::verifyOpened($handle, $lockPath, $uid);
        } catch (ServiceError $error) {
            flock($handle, LOCK_UN);
            fclose($handle);
            throw $error;
        }
        return $handle;
    }

    /**
     * Reads and decodes one state file, or fails closed.
     *
     * A missing file and an exactly-zero-length one are the only two things treated as "no state
     * yet". Anything else that cannot be decoded is a 503: silently starting from an empty
     * directory would drop every outstanding grant's accounting, every room and every rate
     * counter, which is worse than being unavailable for as long as it takes an operator to look.
     */
    private function readState(string $path, int $uid): array
    {
        if (!file_exists($path)) {
            return [];
        }
        $handle = @fopen($path, 'rb');
        if ($handle === false) {
            error_log('dunecity-p2p: cannot read state: ' . $path);
            throw self::unavailable();
        }
        try {
            self::verifyOpened($handle, $path, $uid);
            $stat = fstat($handle);
            if ($stat === false) {
                throw self::unavailable();
            }
            $size = (int)$stat['size'];
            if ($size === 0) {
                return [];
            }
            if ($size > Limits::STATE_MAX_FILE_BYTES) {
                error_log('dunecity-p2p: state file is larger than its bound: ' . $path);
                throw self::unavailable();
            }
            $raw = (string)stream_get_contents($handle);
            if (strlen($raw) !== $size) {
                error_log('dunecity-p2p: short read of state: ' . $path);
                throw self::unavailable();
            }
            $decoded = json_decode($raw, true, 32, JSON_INVALID_UTF8_SUBSTITUTE);
            if (!is_array($decoded)) {
                error_log('dunecity-p2p: state file is not valid JSON: ' . $path);
                throw self::unavailable();
            }
            return $decoded;
        } finally {
            fclose($handle);
        }
    }

    /**
     * Writes a fresh temp file in the same directory and renames it over the data file.
     *
     * Every failure before the rename leaves the previous state exactly as it was, which is the
     * whole point: a full disk, a killed worker or a refused write must not be able to cost the
     * service its grants.
     */
    private function commit(string $path, array $state, int $uid): void
    {
        $encoded = json_encode($state, JSON_UNESCAPED_SLASHES | JSON_INVALID_UTF8_SUBSTITUTE);
        if ($encoded === false || strlen($encoded) > Limits::STATE_MAX_FILE_BYTES) {
            // The callers' own record caps are what normally keep state well under this bound;
            // reaching it means one of those is wrong, and growing past it is not the answer.
            error_log('dunecity-p2p: refusing to write state past its bound: ' . $path);
            throw new ServiceError(503, 'capacity', 'The game service is busy. Try again shortly.');
        }
        $temp = $path . '.' . bin2hex(random_bytes(8)) . '.tmp';
        // Exclusive create: this cannot follow an existing symlink, and the name is unguessable.
        $handle = @fopen($temp, 'xb');
        if ($handle === false) {
            error_log('dunecity-p2p: cannot create a temp state file beside: ' . $path);
            throw self::unavailable();
        }
        $written = false;
        try {
            @chmod($temp, 0o600);
            self::verifyOpened($handle, $temp, $uid);
            $written = self::writeAll($handle, $encoded) && @fflush($handle);
            if ($written && function_exists('fsync')) {
                @fsync($handle);
            }
        } finally {
            fclose($handle);
            if (!$written) {
                @unlink($temp);
            }
        }
        if (!$written) {
            error_log('dunecity-p2p: incomplete state write, previous state kept: ' . $path);
            throw self::unavailable();
        }
        if (!@rename($temp, $path)) {
            @unlink($temp);
            error_log('dunecity-p2p: could not replace state, previous state kept: ' . $path);
            throw self::unavailable();
        }
    }

    /**
     * Deletes a room's state file under that room's stable lock.
     *
     * Taking the lock is what makes this safe: a worker that is part-way through a read-modify-
     * write on the same room holds it, so the file cannot be unlinked out from under a mutation
     * that is about to rename a new version into place.
     */
    public function deleteRoomFile(string $roomId, ?callable $shouldDelete = null): bool
    {
        if (!self::isRoomId($roomId)) {
            return false;
        }
        $this->ready();
        [$dataPath, $lockPath] = $this->pathsFor('rooms/' . $roomId . '.json');
        $lock = $this->acquire($lockPath, self::uid($this->dir));
        try {
            if ($shouldDelete !== null
                && !$shouldDelete($this->readState($dataPath, self::uid($this->dir)))) {
                return false;
            }
            if (!is_link($dataPath) && is_file($dataPath)) {
                @unlink($dataPath);
            }
            // Clean up any temp file a crashed worker left beside it. The lock file itself is
            // never removed - somebody may already be waiting on it.
            foreach ((array)@glob($dataPath . '.*.tmp') as $stale) {
                if (is_string($stale) && !is_link($stale) && is_file($stale)) {
                    @unlink($stale);
                }
            }
            return true;
        } finally {
            flock($lock, LOCK_UN);
            fclose($lock);
        }
    }

    /**
     * Appends one bounded line to a journal, rotating it once when it is full.
     *
     * Rotation and append happen under one stable lock shared by both journals, so a rotation can
     * never land between another worker's size check and its write. Telemetry must not be able to
     * interrupt a game, so a journal this worker does not own is skipped rather than fatal - it
     * is not authorisation state, and refusing to serve players because a log file looks wrong
     * would be the wrong trade.
     */
    public function append(string $name, string $line, int $maxBytes): void
    {
        if (!in_array($name, self::JOURNALS, true)
            || strlen($line) > 4096 || str_contains($line, "\n")) {
            return;
        }
        try {
            $this->ready();
            $uid = self::uid($this->dir);
            $lock = $this->acquire($this->dir . '/journal.lock', $uid);
        } catch (ServiceError) {
            return;
        }
        $path = $this->dir . '/' . $name;
        try {
            if (is_link($path)) {
                return;
            }
            $size = is_file($path) ? (int)@filesize($path) : 0;
            if ($size > 0 && $size + strlen($line) + 1 > $maxBytes) {
                @rename($path, $path . '.1');
                @chmod($path . '.1', 0o600);
                $size = 0;
            }
            if (!file_exists($path)) {
                $created = @fopen($path, 'xb');
                if ($created === false) {
                    return;
                }
                @chmod($path, 0o600);
                fclose($created);
            }
            $handle = @fopen($path, 'ab');
            if ($handle === false) {
                return;
            }
            try {
                self::verifyOpened($handle, $path, $uid);
                self::writeAll($handle, $line . "\n");
                @fflush($handle);
            } catch (ServiceError) {
                // Not ours, or no longer the file we asked for. Do not write into it.
            } finally {
                fclose($handle);
            }
        } finally {
            flock($lock, LOCK_UN);
            fclose($lock);
        }
    }

    /** CSPRNG hex. Every token, room code and identifier in this service comes from here. */
    public static function randomHex(int $bytes): string
    {
        return bin2hex(random_bytes($bytes));
    }

    /** Unbiased draw from the 32-symbol Crockford alphabet (32 divides 256, so no rejection). */
    public static function generateRoomCode(): string
    {
        $bytes = random_bytes(Limits::ROOM_CODE_LENGTH);
        $out = '';
        for ($i = 0; $i < Limits::ROOM_CODE_LENGTH; $i++) {
            $out .= Limits::ROOM_CODE_ALPHABET[ord($bytes[$i]) % 32];
        }
        return substr($out, 0, 4) . '-' . substr($out, 4, 4) . '-' . substr($out, 8, 4);
    }

    public static function normalizeRoomCode(string $raw): ?string
    {
        if (strlen($raw) > Limits::MAX_ROOM_CODE_CHARS) {
            return null;
        }
        $compact = strtoupper(str_replace('-', '', $raw));
        if (strlen($compact) !== Limits::ROOM_CODE_LENGTH) {
            return null;
        }
        for ($i = 0; $i < Limits::ROOM_CODE_LENGTH; $i++) {
            if (!str_contains(Limits::ROOM_CODE_ALPHABET, $compact[$i])) {
                return null;
            }
        }
        return substr($compact, 0, 4) . '-' . substr($compact, 4, 4) . '-' . substr($compact, 8, 4);
    }
}

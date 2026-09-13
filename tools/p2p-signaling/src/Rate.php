<?php
declare(strict_types=1);

/**
 * Fixed-window counters for one address and for the whole service, in one small locked file.
 *
 * The address table is bounded in entries as well as in time: an attacker with a large address
 * pool must not be able to make the rate limiter itself the expensive part. When the table is
 * full the oldest windows are dropped first, and if that is not enough the request is refused -
 * a limiter that fails open is not a limiter.
 */
final class Rate
{
    public function __construct(private readonly Store $store)
    {
    }

    /**
     * @param string $bucket which allowance this request draws on ('ingress', 'admission', ...)
     * @throws ServiceError 429 when either the global or the per-address allowance is spent
     */
    public function charge(string $bucket, string $address, int $globalLimit, int $addressLimit): void
    {
        $now = $this->store->now();
        $window = intdiv($now, 60000);
        $allowed = $this->store->withLock('rate.json', function (array $state) use (
            $bucket, $address, $globalLimit, $addressLimit, $window, $now
        ): array {
            $globals = is_array($state['g'] ?? null) ? $state['g'] : [];
            if (($globals[$bucket]['w'] ?? -1) !== $window) {
                $globals[$bucket] = ['w' => $window, 'n' => 0];
            }
            if ($globals[$bucket]['n'] >= $globalLimit) {
                return [['g' => $globals, 'a' => $state['a'] ?? []], false];
            }

            $addresses = is_array($state['a'] ?? null) ? $state['a'] : [];
            // Expire before capacity is judged, so a table full of stale windows still admits.
            foreach ($addresses as $key => $entry) {
                if (!is_array($entry) || ($entry['t'] ?? 0) < $now - Limits::RATE_TABLE_TTL_MS) {
                    unset($addresses[$key]);
                }
            }
            $key = $bucket . '|' . $address;
            if (!isset($addresses[$key]) && count($addresses) >= Limits::RATE_TABLE_ENTRIES) {
                // Full of live windows. Refusing is the safe direction: it is a denial of
                // service against everybody, which is what is already happening.
                return [['g' => $globals, 'a' => $addresses], false];
            }
            if (($addresses[$key]['w'] ?? -1) !== $window) {
                $addresses[$key] = ['w' => $window, 'n' => 0, 't' => $now];
            }
            $addresses[$key]['t'] = $now;
            if ($addresses[$key]['n'] >= $addressLimit) {
                return [['g' => $globals, 'a' => $addresses], false];
            }
            $addresses[$key]['n']++;
            $globals[$bucket]['n']++;
            return [['g' => $globals, 'a' => $addresses], true];
        });

        if ($allowed !== true) {
            throw new ServiceError(429, 'rate_limited', 'Too many requests. Try again in a minute.');
        }
    }
}

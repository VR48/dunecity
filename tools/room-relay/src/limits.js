'use strict';

// Rate limiting with bounded memory.
//
// A rate limiter that remembers every address it has ever seen is itself a denial-of-service
// vector, so every table here has a hard entry cap and a time-to-live. When the cap is reached
// the oldest entry is evicted; the worst case is that an attacker forces its own counters to be
// forgotten, which still leaves the global limiter in place.

/** Fixed-window counter for one connection. Buckets are one second wide. */
class WindowCounter {
  constructor(limit, windowMs) {
    this.limit = limit;
    this.windowMs = windowMs;
    this.windowStart = 0;
    this.count = 0;
  }

  /**
   * @param {number} now monotonic-ish milliseconds
   * @param {number} amount how much to add (1 for messages, byte count for bandwidth)
   * @returns {boolean} true if the budget is still respected
   */
  allow(now, amount) {
    if (now - this.windowStart >= this.windowMs) {
      this.windowStart = now;
      this.count = 0;
    }
    this.count += amount;
    return this.count <= this.limit;
  }
}

/** Bounded LRU of sliding-window counters keyed by a string (an address, normally). */
class BoundedRateTable {
  constructor({ limit, windowMs, maxEntries, ttlMs }) {
    this.limit = limit;
    this.windowMs = windowMs;
    this.maxEntries = maxEntries;
    this.ttlMs = ttlMs;
    this.entries = new Map();
  }

  sweep(now) {
    for (const [key, entry] of this.entries) {
      if (now - entry.lastSeen > this.ttlMs) {
        this.entries.delete(key);
      } else {
        // Map preserves insertion order, and entries are re-inserted on touch, so the first
        // entry that is still fresh means every later one is too.
        break;
      }
    }
  }

  allow(key, now) {
    this.sweep(now);

    let entry = this.entries.get(key);
    if (entry === undefined) {
      while (this.entries.size >= this.maxEntries) {
        const oldest = this.entries.keys().next();
        if (oldest.done) break;
        this.entries.delete(oldest.value);
      }
      entry = { windowStart: now, count: 0, lastSeen: now };
    } else {
      this.entries.delete(key);
    }

    if (now - entry.windowStart >= this.windowMs) {
      entry.windowStart = now;
      entry.count = 0;
    }
    entry.count += 1;
    entry.lastSeen = now;
    this.entries.set(key, entry);

    return entry.count <= this.limit;
  }

  get size() {
    return this.entries.size;
  }
}

module.exports = { WindowCounter, BoundedRateTable };

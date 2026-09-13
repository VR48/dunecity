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

#ifndef BOUNDEDHTTPCLIENT_H
#define BOUNDEDHTTPCLIENT_H

/**
    One bounded HTTPS request at a time, and never a blocking one.

    Every request this game makes to the online service - the admission request that produces a
    grant, and the session, poll and signal requests that introduce players to each other - is a
    small POST whose answer has a known maximum size. They share one implementation so that they
    share one set of guarantees: no redirect is followed, no ambient credentials or proxy are
    used, the response is bounded before it is allocated and again while it streams, and a
    request that is cancelled or outlives its caller cannot write into freed state.

    Hardening one of those boundaries and not the other is how the weaker one becomes the way in;
    the admission request is the *first* thing a direct match does, so it gets the same treatment
    as the rest.

    The interface is abstract so tests can script a service without a socket, and so the two real
    backends (libcurl natively, Fetch in the browser) stay out of their callers.
*/

#include <cstddef>
#include <memory>
#include <string>

class BoundedHttpClient {
public:
    struct Request {
        std::string url;
        std::string body;           ///< application/x-www-form-urlencoded
        std::string sessionToken;   ///< sent as X-Dune-Session; never placed in the url
        /**
            Largest answer this caller will read, in bytes.

            Endpoint specific on purpose: an admission answer is 8 KiB and a signalling poll can
            carry a full session description. Whatever is asked for, the implementation clamps it
            to kMaxResponseBytes - a caller cannot raise the ceiling, only lower it.
        */
        std::size_t maxResponseBytes = 0;
        /// Deadline in seconds. Zero means the implementation's default.
        long        timeoutSeconds   = 0;
    };

    /// Why a request produced no answer. The caller turns this into the player's sentence.
    enum class Failure {
        None,
        Network,        ///< refused, dropped, blocked, or a redirect that was not followed
        Timeout,
        Certificate,    ///< TLS verification failed
        NameResolution,
        TooLarge        ///< the answer exceeded the bound this caller asked for
    };

    struct Result {
        long        httpStatus = 0;
        std::string body;
        /// Non-empty when the request never produced a response at all.
        std::string transportError;
        Failure     failure = Failure::None;
    };

    /// The ceiling every caller is clamped to, whatever it asks for.
    static constexpr std::size_t kMaxResponseBytes = 524288;

    virtual ~BoundedHttpClient() = default;

    /// Starts a request. Any request in flight is abandoned.
    virtual void begin(const Request& request) = 0;

    /// Drives the request. Called from the game loop; must never block.
    virtual void update() = 0;

    /// Takes the result of the finished request, if there is one.
    virtual bool poll(Result& out) = 0;

    virtual bool busy() const = 0;

    virtual void cancel() = 0;
};

/// The platform's bounded HTTP client: libcurl natively, Fetch in the browser.
std::unique_ptr<BoundedHttpClient> createBoundedHttpClient();

#endif // BOUNDEDHTTPCLIENT_H

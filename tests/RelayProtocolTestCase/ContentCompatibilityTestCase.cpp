/*
 *  ContentCompatibilityTestCase.cpp - who is allowed to be in the same match
 *
 *  Lockstep needs every peer to compute the same thing from the same commands, so it needs the
 *  same build and the same content files. On the relay there is no way to transfer content, so a
 *  difference is final: it has to stop the match rather than end up in a log nobody reads.
 *
 *  The rule itself lives in one header because three places apply it - the shared payload handler
 *  when a peer's hashes arrive, the host just before it starts, and these tests - and an answer
 *  that depends on which of them looked first would be worse than no check at all.
 */

#include <catch2/catch_all.hpp>

#include <Network/ContentCompatibility.h>

#include <string>

namespace {

ContentCompatibility::Fingerprint knownGood() {
    ContentCompatibility::Fingerprint fingerprint;
    fingerprint.gameVersion    = "1.0.655";
    fingerprint.quantBotHash   = "0123456789abcdef";
    fingerprint.objectDataHash = "fedcba9876543210";
    return fingerprint;
}

} // namespace

TEST_CASE("identical installs match", "[relay][content]") {
    std::string reason;
    REQUIRE(ContentCompatibility::compare(knownGood(), knownGood(), "guest", reason)
            == ContentCompatibility::Verdict::Match);
    REQUIRE(reason.empty());
}

TEST_CASE("a peer that has not reported yet is not a peer that agrees",
          "[relay][content][security]") {
    std::string reason;

    // Distinguished from disagreement on purpose: the host may simply have pressed start before
    // the other side's hashes arrived, and that is recoverable by waiting.
    ContentCompatibility::Fingerprint silent;
    REQUIRE(ContentCompatibility::compare(knownGood(), silent, "guest", reason)
            == ContentCompatibility::Verdict::AwaitingPeer);
    REQUIRE(reason.find("guest") != std::string::npos);

    ContentCompatibility::Fingerprint partial = knownGood();
    partial.objectDataHash.clear();
    REQUIRE(ContentCompatibility::compare(knownGood(), partial, "guest", reason)
            == ContentCompatibility::Verdict::AwaitingPeer);
}

TEST_CASE("an install that cannot describe itself does not get to play",
          "[relay][content][security]") {
    std::string reason;

    // Two installs that both failed to hash themselves would otherwise compare equal, and
    // neither would have verified anything at all.
    ContentCompatibility::Fingerprint blank;
    REQUIRE(ContentCompatibility::compare(blank, blank, "guest", reason)
            == ContentCompatibility::Verdict::Mismatch);
    REQUIRE_FALSE(reason.empty());

    ContentCompatibility::Fingerprint halfKnown = knownGood();
    halfKnown.quantBotHash.clear();
    REQUIRE(ContentCompatibility::compare(halfKnown, knownGood(), "guest", reason)
            == ContentCompatibility::Verdict::Mismatch);
}

TEST_CASE("a different build is a mismatch", "[relay][content]") {
    std::string reason;
    ContentCompatibility::Fingerprint peer = knownGood();
    peer.gameVersion = "1.0.654";
    REQUIRE(ContentCompatibility::compare(knownGood(), peer, "guest", reason)
            == ContentCompatibility::Verdict::Mismatch);
    REQUIRE(reason.find("version") != std::string::npos);
}

TEST_CASE("the explanation names the file that differs", "[relay][content]") {
    std::string reason;

    ContentCompatibility::Fingerprint peer = knownGood();
    peer.quantBotHash = "aaaaaaaaaaaaaaaa";
    REQUIRE(ContentCompatibility::compare(knownGood(), peer, "guest", reason)
            == ContentCompatibility::Verdict::Mismatch);
    REQUIRE(reason.find("QuantBot Config.ini") != std::string::npos);
    REQUIRE(reason.find("ObjectData.ini") == std::string::npos);

    peer = knownGood();
    peer.objectDataHash = "aaaaaaaaaaaaaaaa";
    REQUIRE(ContentCompatibility::compare(knownGood(), peer, "guest", reason)
            == ContentCompatibility::Verdict::Mismatch);
    REQUIRE(reason.find("ObjectData.ini") != std::string::npos);
    REQUIRE(reason.find("QuantBot Config.ini") == std::string::npos);
}

TEST_CASE("an unnamed peer still produces a usable sentence", "[relay][content]") {
    std::string reason;
    ContentCompatibility::Fingerprint peer = knownGood();
    peer.objectDataHash = "aaaaaaaaaaaaaaaa";
    REQUIRE(ContentCompatibility::compare(knownGood(), peer, "", reason)
            == ContentCompatibility::Verdict::Mismatch);
    REQUIRE(reason.find("The other player") != std::string::npos);
}

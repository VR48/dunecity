/*
    What the signaling service is allowed to say, and what it is not.

    The service introduces players to each other and is not trusted with anything else, so these
    tests are mostly about refusals: a TURN url, a relayed candidate, a description with the wrong
    fingerprint or an extra media section, a peer record with an unusable name, a signal stream
    that replays. Each of those is a way a compromised or confused service could turn a direct
    match into something else, and each is refused here rather than at the RTC stack.
*/

#include <catch2/catch_test_macros.hpp>

#include <Network/P2PSignalingProtocol.h>

#include <string>

namespace {

/// A minimal data-only description of the shape a browser and libdatachannel both produce.
std::string dataChannelSdp(const std::string& fingerprint = "sha-256 "
                           "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:"
                           "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99") {
    return "v=0\r\n"
           "o=- 1 2 IN IP4 127.0.0.1\r\n"
           "s=-\r\n"
           "t=0 0\r\n"
           "a=group:BUNDLE 0\r\n"
           "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
           "c=IN IP4 0.0.0.0\r\n"
           "a=mid:0\r\n"
           "a=setup:actpass\r\n"
           "a=fingerprint:" + fingerprint + "\r\n"
           "a=sctp-port:5000\r\n";
}

} // namespace

TEST_CASE("A session answer is understood, and an incomplete one is not", "[p2psignal]") {
    const std::string body =
        "status=ok\n"
        "protocol=1\n"
        "peer=7\n"
        "session=" + std::string(64, 'a') + "\n"
        "role=host\n"
        "maxPeers=4\n"
        "phase=lobby\n"
        "room=H4PQ-7T2M-9XKB\n"
        "ice=stun:stun.l.google.com:19302\n"
        "somethingNew=ignored\n";

    P2PSignal::SessionResponse response;
    std::string error;
    REQUIRE(P2PSignal::parseSessionResponse(body, response, error));
    REQUIRE(response.ok);
    REQUIRE(response.peerId == 7u);
    REQUIRE(response.role == RoomRelay::Role::Host);
    REQUIRE(response.maxPeers == 4);
    REQUIRE(response.iceServers.size() == 1);

    SECTION("a refusal is a valid answer and keeps its message") {
        P2PSignal::SessionResponse refusal;
        REQUIRE(P2PSignal::parseSessionResponse(
            "status=error\ncode=room_full\nmessage=That game is full.\n", refusal, error));
        REQUIRE_FALSE(refusal.ok);
        REQUIRE(refusal.errorCode == "room_full");
        REQUIRE(refusal.errorMessage == "That game is full.");
    }

    SECTION("missing fields are not guessed at") {
        P2PSignal::SessionResponse partial;
        REQUIRE_FALSE(P2PSignal::parseSessionResponse("status=ok\npeer=7\n", partial, error));
        REQUIRE_FALSE(error.empty());
    }

    SECTION("a TURN url is refused, so gameplay cannot be relayed") {
        P2PSignal::SessionResponse turn;
        const std::string withTurn = "status=ok\nprotocol=1\npeer=7\nsession="
                                   + std::string(64, 'a')
                                   + "\nrole=host\nmaxPeers=2\nice=turn:relay.example.net\n";
        REQUIRE_FALSE(P2PSignal::parseSessionResponse(withTurn, turn, error));
    }

    SECTION("a session token of the wrong shape is refused") {
        P2PSignal::SessionResponse bad;
        REQUIRE_FALSE(P2PSignal::parseSessionResponse(
            "status=ok\nprotocol=1\npeer=7\nsession=NOTHEX\nrole=host\nmaxPeers=2\n", bad, error));
    }
}

TEST_CASE("A poll answer carries members, attestations and signals", "[p2psignal]") {
    const std::string fingerprint =
        "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:"
        "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99";
    const std::string body =
        "status=ok\n"
        "phase=lobby\n"
        "peer=2|client|53746566616e|6e6174697665\n"
        "fp=2|1|sha-256|" + fingerprint + "\n"
        "sig=2|1|offer|763d300d0a\n"
        "cursor=1\n";

    P2PSignal::PollResponse response;
    std::string error;
    REQUIRE(P2PSignal::parsePollResponse(body, 1, response, error));
    REQUIRE(response.members.size() == 1);
    REQUIRE(response.members[0].id == 2u);
    REQUIRE(response.members[0].name == "Stefan");
    REQUIRE(response.members[0].runtime == "native");
    REQUIRE(response.fingerprints.size() == 1);
    REQUIRE(response.fingerprints[0].value == fingerprint);
    REQUIRE(response.signals.size() == 1);
    REQUIRE(response.signals[0].kind == P2PSignal::SignalKind::Offer);
    REQUIRE(response.signals[0].payload == "v=0\r\n");
    REQUIRE(response.cursor == 1u);
}

TEST_CASE("Poll answers that could not be true are refused", "[p2psignal][adversarial]") {
    P2PSignal::PollResponse response;
    std::string error;
    const std::string fingerprint =
        "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:"
        "AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99";

    const std::string cases[] = {
        "status=ok\ncursor=1\npeer=2|client|53746566616e\n",          // too few fields
        "status=ok\ncursor=1\npeer=0|client|53746566616e|6e6174697665\n",  // peer id zero
        "status=ok\ncursor=1\npeer=2|admin|53746566616e|6e6174697665\n",   // unknown role
        "status=ok\ncursor=1\npeer=2|client|00|6e6174697665\n",       // control character in name
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\nfp=2|1|sha-1|" + fingerprint
            + "\n",                                                     // weak digest
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\nfp=2|1|sha-256|AA:BB\n",
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\nfp=2|3|sha-256|" + fingerprint
            + "\n",                                                     // bound to another pair
        "status=ok\ncursor=1\nfp=2|1|sha-256|" + fingerprint + "\n",  // not a member
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\npeer=2|client|42|6e6174697665\n"
            "cursor=1\n",                                               // duplicate peer id
        "status=ok\ncursor=1\npeer=2|host|41|6e6174697665\npeer=3|host|42|6e6174697665\n",
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\npeer=3|client|41|6e6174697665\n",
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\ngone=2\n",
        "status=ok\ncursor=1\npeer=1|client|41|6e6174697665\n"
            "sig=1|1|offer|763d30\n",                                   // a signal from ourselves
        "status=ok\ncursor=2\npeer=2|client|41|6e6174697665\n"
            "sig=2|2|offer|763d30\nsig=2|1|offer|763d30\n",            // replayed order
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\nsig=2|5|offer|763d30\n",
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\nsig=2|1|shout|763d30\n",
        "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\nsig=2|1|offer|zz\n",
        "status=ok\ncursor=1\nsig=2|1|offer|763d30\n",                // from outside the room
        "status=ok\n",                                                 // no cursor
    };
    for(const std::string& body : cases) {
        INFO(body);
        REQUIRE_FALSE(P2PSignal::parsePollResponse(body, 1, response, error));
    }
}

TEST_CASE("Only STUN urls are acceptable", "[p2psignal]") {
    REQUIRE(P2PSignal::isAcceptableIceUrl("stun:stun.l.google.com:19302"));
    REQUIRE(P2PSignal::isAcceptableIceUrl("stuns:stun.example.net:5349"));
    REQUIRE_FALSE(P2PSignal::isAcceptableIceUrl("turn:relay.example.net:3478"));
    REQUIRE_FALSE(P2PSignal::isAcceptableIceUrl("turns:relay.example.net:5349"));
    REQUIRE_FALSE(P2PSignal::isAcceptableIceUrl("stun:user:secret@stun.example.net"));
    // The browser transport's own url rule is a strict character set; anything it would refuse
    // has to be refused here too, or a match fails only in the browser and only at connect time.
    REQUIRE_FALSE(P2PSignal::isAcceptableIceUrl("stun:stun.example.net?transport=tcp"));
    REQUIRE_FALSE(P2PSignal::isAcceptableIceUrl("https://example.net"));
    REQUIRE_FALSE(P2PSignal::isAcceptableIceUrl("stun:" + std::string(200, 'a')));
}

TEST_CASE("Relayed candidates are recognised wherever they appear", "[p2psignal]") {
    REQUIRE(P2PSignal::mentionsRelayCandidate(
        "candidate:1 1 udp 41885439 1.2.3.4 50000 typ relay raddr 0.0.0.0 rport 0"));
    REQUIRE(P2PSignal::mentionsRelayCandidate("a=candidate:2 1 UDP 100 1.2.3.4 1 TYP RELAY"));
    REQUIRE(P2PSignal::mentionsRelayCandidate("turn:relay.example.net"));
    REQUIRE_FALSE(P2PSignal::mentionsRelayCandidate(
        "candidate:1 1 udp 2122260223 192.168.1.2 50000 typ host"));
    REQUIRE_FALSE(P2PSignal::mentionsRelayCandidate(
        "candidate:2 1 udp 1686052607 203.0.113.7 50000 typ srflx raddr 192.168.1.2 rport 50000"));
}

TEST_CASE("A description must be one data channel with one sha-256 fingerprint",
          "[p2psignal][adversarial]") {
    std::string algorithm;
    std::string value;
    REQUIRE(P2PSignal::extractSdpFingerprint(dataChannelSdp(), algorithm, value));
    REQUIRE(algorithm == "sha-256");
    REQUIRE(value.size() == 95);

    SECTION("lowercase hex in the description is normalised for comparison") {
        std::string lowered = dataChannelSdp();
        for(char& c : lowered) {
            if(c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        }
        std::string loweredAlgorithm;
        std::string loweredValue;
        // "a=fingerprint" survives the lowering; the value is compared uppercase either way.
        REQUIRE(P2PSignal::extractSdpFingerprint(lowered, loweredAlgorithm, loweredValue));
        REQUIRE(loweredValue == value);
    }

    SECTION("no fingerprint at all") {
        std::string sdp = dataChannelSdp();
        const std::size_t at = sdp.find("a=fingerprint:");
        sdp.erase(at, sdp.find("\r\n", at) - at + 2);
        REQUIRE_FALSE(P2PSignal::extractSdpFingerprint(sdp, algorithm, value));
    }

    SECTION("two fingerprints") {
        std::string sdp = dataChannelSdp();
        sdp += "a=fingerprint:sha-256 "
               "11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:"
               "11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00\r\n";
        REQUIRE_FALSE(P2PSignal::extractSdpFingerprint(sdp, algorithm, value));
    }

    SECTION("a weaker digest") {
        REQUIRE_FALSE(P2PSignal::extractSdpFingerprint(
            dataChannelSdp("sha-1 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD"),
            algorithm, value));
    }

    SECTION("an extra media section") {
        std::string sdp = dataChannelSdp();
        sdp += "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n";
        REQUIRE_FALSE(P2PSignal::extractSdpFingerprint(sdp, algorithm, value));
    }

    SECTION("no media section") {
        std::string sdp = dataChannelSdp();
        const std::size_t at = sdp.find("m=application");
        sdp.erase(at, sdp.find("\r\n", at) - at + 2);
        REQUIRE_FALSE(P2PSignal::extractSdpFingerprint(sdp, algorithm, value));
    }

    SECTION("an oversized description") {
        REQUIRE_FALSE(P2PSignal::extractSdpFingerprint(std::string(70000, 'a'), algorithm, value));
    }
}

TEST_CASE("Response bodies are bounded before they are believed", "[p2psignal][adversarial]") {
    P2PSignal::PollResponse response;
    std::string error;
    REQUIRE_FALSE(P2PSignal::parsePollResponse(
        std::string(P2PSignal::Limits::kMaxResponseBytes + 1, 'a'), 1, response, error));
    REQUIRE_FALSE(P2PSignal::parsePollResponse("", 1, response, error));

    std::string manyLines = "status=ok\ncursor=1\n";
    for(std::size_t i = 0; i < P2PSignal::Limits::kMaxResponseLines + 4; ++i) {
        manyLines += "unknown=1\n";
    }
    REQUIRE_FALSE(P2PSignal::parsePollResponse(manyLines, 1, response, error));

    // A line with no separator is a hard failure, not a skipped line.
    REQUIRE_FALSE(P2PSignal::parsePollResponse("status=ok\ncursor=1\ngarbage\n", 1, response,
                                               error));
}


TEST_CASE("A real-sized description survives the whole signaling path", "[p2psignal][limits]") {
    // The bounds have to be algebraically consistent after hex expansion, or two real stacks
    // cannot exchange the descriptions they actually produce. A full-size SDP is 65536 bytes,
    // which is 131072 hex characters plus the record's own metadata.
    REQUIRE(P2PSignal::Limits::kMaxSignalHexChars == 131072);
    REQUIRE(P2PSignal::Limits::kMaxValueBytes > P2PSignal::Limits::kMaxSignalHexChars);
    REQUIRE(P2PSignal::Limits::kMaxLineBytes > P2PSignal::Limits::kMaxValueBytes);
    REQUIRE(P2PSignal::Limits::kMaxResponseBytes > P2PSignal::Limits::kMaxLineBytes);
    REQUIRE(P2PSignal::Limits::kMaxSignalRequestBytes > P2PSignal::Limits::kMaxSignalHexChars);

    const std::string sdp(P2PSignal::Limits::kMaxSdpBytes, 'x');
    const std::string body = "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\n"
                             "sig=2|1|answer|" + P2PSignal::encodeHexText(sdp) + "\n";
    P2PSignal::PollResponse response;
    std::string error;
    REQUIRE(P2PSignal::parsePollResponse(body, 1, response, error));
    REQUIRE(response.signals.size() == 1);
    REQUIRE(response.signals[0].payload.size() == P2PSignal::Limits::kMaxSdpBytes);

    SECTION("one byte more is refused") {
        const std::string oversized(P2PSignal::Limits::kMaxSdpBytes + 1, 'x');
        const std::string tooBig = "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\n"
                                   "sig=2|1|answer|" + P2PSignal::encodeHexText(oversized) + "\n";
        P2PSignal::PollResponse refused;
        REQUIRE_FALSE(P2PSignal::parsePollResponse(tooBig, 1, refused, error));
    }

    SECTION("an ordinary browser-sized description is unremarkable") {
        const std::string ordinary(2048, 'x');
        const std::string small = "status=ok\ncursor=1\npeer=2|client|41|6e6174697665\n"
                                  "sig=2|1|answer|" + P2PSignal::encodeHexText(ordinary) + "\n";
        P2PSignal::PollResponse ok;
        REQUIRE(P2PSignal::parsePollResponse(small, 1, ok, error));
        REQUIRE(ok.signals[0].payload.size() == 2048);
    }
}

TEST_CASE("Overflowed numbers are not accepted as small ones", "[p2psignal][adversarial]") {
    std::uint64_t value = 0;
    // 2^64 + 1 wraps a naive accumulator to 1, which would pass a limit of 1.
    REQUIRE_FALSE(P2PSignal::parseUnsigned("18446744073709551617", 1, value));
    REQUIRE_FALSE(P2PSignal::parseUnsigned("18446744073709551616", 65535, value));
    REQUIRE_FALSE(P2PSignal::parseUnsigned("99999999999999999999", 1, value));
    REQUIRE_FALSE(P2PSignal::parseUnsigned("007", 65535, value));
    REQUIRE_FALSE(P2PSignal::parseUnsigned("", 65535, value));
    REQUIRE(P2PSignal::parseUnsigned("0", 65535, value));
    REQUIRE(value == 0);
    REQUIRE(P2PSignal::parseUnsigned("65535", 65535, value));
    REQUIRE(value == 65535);
    REQUIRE_FALSE(P2PSignal::parseUnsigned("65536", 65535, value));
}

TEST_CASE("Candidates carry a media id and never a relayed route", "[p2psignal]") {
    REQUIRE(P2PSignal::isAcceptableCandidatePayload(
        "0|candidate:1 1 udp 2122260223 192.168.1.2 50000 typ host"));
    REQUIRE(P2PSignal::isAcceptableCandidatePayload(
        "data|candidate:2 1 udp 1686052607 203.0.113.7 50000 typ srflx raddr 10.0.0.1 rport 1"));
    // The browser refuses an empty media id, so this side must too.
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        "|candidate:1 1 udp 2122260223 192.168.1.2 50000 typ host"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        "0|not-a-candidate 1 udp 1 1.2.3.4 1 typ host"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        "0|candidate:1 1 udp 1 1.2.3.4 1 typ hostile"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        "0|candidate:1 1 udp 1 1.2.3.4 1"));                            // no type at all
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload("no-delimiter"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload("0|"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        "0|candidate:1 1 udp 41885439 1.2.3.4 50000 typ relay"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        "0|candidate:1 1 udp 1 1.2.3.4 1 typ host\r\na=evil"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        std::string(65, 'm') + "|candidate:1 1 udp 1 1.2.3.4 1 typ host"));
    REQUIRE_FALSE(P2PSignal::isAcceptableCandidatePayload(
        "0|" + std::string(P2PSignal::Limits::kMaxCandidateBytes + 1, 'c')));
}

TEST_CASE("a=sctp-init is removed and nothing else is", "[p2psignal][interop]") {
    // Not a precaution: Chromium offers this attribute, libdatachannel does not implement it but
    // echoes it back, and the data channel then fails on its first message. Removing the line is
    // what makes browser-to-native play work at all.
    const std::string chromiumOffer =
        "v=0\r\n"
        "o=- 1 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "a=mid:0\r\n"
        "a=sctp-init:AQIDBAUGBwgJCgsMDQ4PEA==\r\n"
        "a=fingerprint:sha-256 AA:BB\r\n"
        "a=sctp-port:5000\r\n";
    const std::string stripped = P2PSignal::stripUnsupportedSctpInit(chromiumOffer);
    REQUIRE(stripped.find("a=sctp-init") == std::string::npos);
    REQUIRE(stripped.find("a=fingerprint:sha-256 AA:BB\r\n") != std::string::npos);
    REQUIRE(stripped.find("a=sctp-port:5000\r\n") != std::string::npos);
    REQUIRE(stripped.find("a=mid:0\r\n") != std::string::npos);
    REQUIRE(stripped.size() == chromiumOffer.size() - std::string("a=sctp-init:AQIDBAUGBwgJCgsMDQ4PEA==\r\n").size());

    SECTION("bare LF is handled the same way") {
        const std::string lf = "v=0\na=sctp-init:AA==\na=fingerprint:sha-256 AA:BB\n";
        REQUIRE(P2PSignal::stripUnsupportedSctpInit(lf)
                == "v=0\na=fingerprint:sha-256 AA:BB\n");
    }

    SECTION("a final line without a separator still survives") {
        REQUIRE(P2PSignal::stripUnsupportedSctpInit("v=0\na=sctp-port:5000") == "v=0\na=sctp-port:5000");
        REQUIRE(P2PSignal::stripUnsupportedSctpInit("v=0\na=sctp-init:AA==") == "v=0\n");
    }

    SECTION("it is a line match, not a substring match") {
        // An attribute that merely mentions the text is left alone.
        const std::string other = "a=x-note:a=sctp-init: is unsupported\r\n";
        REQUIRE(P2PSignal::stripUnsupportedSctpInit(other) == other);
    }

    SECTION("a description without the attribute is returned unchanged") {
        const std::string plain = dataChannelSdp();
        REQUIRE(P2PSignal::stripUnsupportedSctpInit(plain) == plain);
    }
}

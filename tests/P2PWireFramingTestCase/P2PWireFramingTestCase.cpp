/*
    The bytes a direct match puts on a data channel.

    The golden strings below were produced by the vendored browser chunker
    (platform/web/p2pkit/src/framing/index.ts) and are asserted verbatim in both directions: the
    native encoder must produce exactly these, and tools/p2p-interop/framing-vectors.mjs feeds the
    same literals to the browser chunker. If the two ever disagree, one of these fails rather than
    a browser and a native client failing to play each other.
*/

#include <catch2/catch_test_macros.hpp>

#include <Network/P2PWireFraming.h>

#include <string>
#include <vector>

using P2PWire::ChunkAssembler;

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<int> values) {
    std::vector<std::uint8_t> out;
    for(const int value : values) {
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}

/// Feeds a whole payload through a fresh assembler and requires it to complete.
std::string roundTrip(const std::string& groupId, const std::string& value) {
    ChunkAssembler assembler;
    std::string assembled;
    const char* reason = nullptr;
    for(const std::string& packet : P2PWire::splitIntoChunkPackets(groupId, value)) {
        const ChunkAssembler::Outcome outcome = assembler.ingest(packet, 1000, assembled, reason);
        if(outcome == ChunkAssembler::Outcome::Complete) {
            return assembled;
        }
        REQUIRE(outcome == ChunkAssembler::Outcome::Pending);
    }
    FAIL("the payload never completed");
    return std::string();
}

} // namespace

TEST_CASE("Game envelopes match the agreed text", "[p2pwire]") {
    const std::vector<std::uint8_t> payload = bytes({0x00, 0x01, 0xff, 0x7f});
    const std::string envelope = P2PWire::encodeGameEnvelope(payload.data(), payload.size(), 0, 0);
    REQUIRE(envelope == "\"g:0:0:0001ff7f\"");

    P2PWire::Envelope decoded;
    const char* reason = nullptr;
    REQUIRE(P2PWire::decodeEnvelope(envelope, decoded, reason));
    REQUIRE(decoded.kind == P2PWire::EnvelopeKind::Game);
    REQUIRE(decoded.channel == 0);
    REQUIRE(decoded.recipient == 0u);
    REQUIRE(decoded.payload == payload);
}

TEST_CASE("Every envelope kind survives a round trip", "[p2pwire]") {
    const std::vector<std::uint8_t> diagnostic = bytes({0xde, 0xad, 0xbe, 0xef});
    const std::string diagnosticEnvelope =
        P2PWire::encodeDiagnosticEnvelope(1, diagnostic.data(), diagnostic.size());
    REQUIRE(diagnosticEnvelope == "\"d:1:deadbeef\"");

    const std::string readiness = P2PWire::encodeReadinessEnvelope("1,2,3", {2, 3});
    REQUIRE(readiness == "\"r:1,2,3:2,3\"");

    const std::string probe = P2PWire::encodeProbeEnvelope(false, 4294967295u);
    REQUIRE(probe == "\"p:4294967295\"");

    P2PWire::Envelope decoded;
    const char* reason = nullptr;

    REQUIRE(P2PWire::decodeEnvelope(diagnosticEnvelope, decoded, reason));
    REQUIRE(decoded.kind == P2PWire::EnvelopeKind::Diagnostic);
    REQUIRE(decoded.diagnosticKind == 1);
    REQUIRE(decoded.payload == diagnostic);

    REQUIRE(P2PWire::decodeEnvelope(readiness, decoded, reason));
    REQUIRE(decoded.kind == P2PWire::EnvelopeKind::Readiness);
    REQUIRE(decoded.rosterKey == "1,2,3");
    REQUIRE(decoded.connectedPeers == std::vector<std::uint32_t>{2, 3});

    REQUIRE(P2PWire::decodeEnvelope(probe, decoded, reason));
    REQUIRE(decoded.kind == P2PWire::EnvelopeKind::Ping);
    REQUIRE(decoded.token == 4294967295u);

    REQUIRE(P2PWire::decodeEnvelope(P2PWire::encodeProbeEnvelope(true, 7), decoded, reason));
    REQUIRE(decoded.kind == P2PWire::EnvelopeKind::Pong);
    REQUIRE(decoded.token == 7u);
}

TEST_CASE("Fragments are byte-identical to the browser chunker", "[p2pwire][interop]") {
    // Single fragment.
    const auto single = P2PWire::splitIntoChunkPackets("abcdef0123456789", "\"g:0:0:0001ff7f\"");
    REQUIRE(single.size() == 1);
    REQUIRE(single[0]
            == "{\"id\":\"abcdef0123456789\",\"i\":0,\"n\":1,\"part\":\"\\\"g:0:0:0001ff7f\\\"\"}");

    // A group id from the browser's alphabet, which is not hex.
    const auto readiness = P2PWire::splitIntoChunkPackets("AB_cd-01", "\"r:1,2,3:2,3\"");
    REQUIRE(readiness.size() == 1);
    REQUIRE(readiness[0] == "{\"id\":\"AB_cd-01\",\"i\":0,\"n\":1,\"part\":\"\\\"r:1,2,3:2,3\\\"\"}");

    // A payload that spans three fragments; 40008 characters at 16000 per fragment.
    const std::string value = "\"g:1:7:" + std::string(40000, 'a') + "\"";
    REQUIRE(value.size() == 40008);
    const auto many = P2PWire::splitIntoChunkPackets("0123456789abcdef", value);
    REQUIRE(many.size() == 3);
    const std::string expectedPrefix = "{\"id\":\"0123456789abcdef\",\"i\":0,\"n\":3,\"part\":\"\\\"";
    REQUIRE(expectedPrefix.size() == 47);
    REQUIRE(many[0].compare(0, expectedPrefix.size(), expectedPrefix) == 0);
    REQUIRE(many[2].find("\"i\":2,\"n\":3") != std::string::npos);
    REQUIRE(roundTrip("0123456789abcdef", value) == value);
}

TEST_CASE("A full-size game packet still fits the framing", "[p2pwire]") {
    const std::vector<std::uint8_t> payload(P2PWire::Limits::kMaxGamePayloadBytes, 0xab);
    const std::string envelope =
        P2PWire::encodeGameEnvelope(payload.data(), payload.size(), 1, 9);
    REQUIRE_FALSE(envelope.empty());
    REQUIRE(envelope.size() < P2PWire::Limits::kMaxAssembledBytes);

    const auto packets = P2PWire::splitIntoChunkPackets("0f0f0f0f0f0f0f0f", envelope);
    REQUIRE(packets.size() <= P2PWire::Limits::kMaxFragments);
    REQUIRE(roundTrip("0f0f0f0f0f0f0f0f", envelope) == envelope);

    P2PWire::Envelope decoded;
    const char* reason = nullptr;
    REQUIRE(P2PWire::decodeEnvelope(envelope, decoded, reason));
    REQUIRE(decoded.payload == payload);
    REQUIRE(decoded.recipient == 9u);

    // One byte more than the transport carries is refused rather than truncated.
    const std::vector<std::uint8_t> tooLarge(P2PWire::Limits::kMaxGamePayloadBytes + 1, 0);
    REQUIRE(P2PWire::encodeGameEnvelope(tooLarge.data(), tooLarge.size(), 0, 0).empty());
}

TEST_CASE("Malformed envelopes are refused, never half-understood", "[p2pwire][adversarial]") {
    P2PWire::Envelope decoded;
    const char* reason = nullptr;
    const char* cases[] = {
        "",                                 // nothing at all
        "g:0:0:00",                         // not a JSON string
        "\"g:0:0:00\" ",                    // trailing data
        "\"g:2:0:00\"",                     // channel out of range
        "\"g:0:0:\"",                       // empty payload
        "\"g:0:0:0\"",                      // odd-length hex
        "\"g:0:0:00FF\"",                   // uppercase hex
        "\"g:0:0:zz\"",                     // not hex
        "\"g:00:0:00\"",                    // leading zero
        "\"g:0:99999999999999:00\"",        // recipient overflow
        "\"x:0:0:00\"",                     // unknown kind
        "\"g:0:00\"",                       // too few fields
        "\"d:256:00\"",                     // diagnostic kind out of range
        "\"r:1,2:0\"",                      // peer id zero
        "\"r:1,2:1,\"",                     // trailing separator
        "\"p:\"",                           // empty token
        "\"p:-1\"",                         // negative token
        "\"g:0:0:00\\u0000\"",              // embedded NUL in the hex field
    };
    for(const char* text : cases) {
        INFO(text);
        REQUIRE_FALSE(P2PWire::decodeEnvelope(text, decoded, reason));
        REQUIRE(reason != nullptr);
    }
}

TEST_CASE("Malformed fragments fail the connection instead of being skipped",
          "[p2pwire][adversarial]") {
    ChunkAssembler assembler;
    std::string out;
    const char* reason = nullptr;

    const char* fatal[] = {
        "",                                                     // empty message
        "[]",                                                   // not an object
        "{\"id\":\"a\",\"i\":0,\"n\":0,\"part\":\"x\"}",         // n below range
        "{\"id\":\"a\",\"i\":0,\"n\":129,\"part\":\"x\"}",       // n above range
        "{\"id\":\"a\",\"i\":2,\"n\":2,\"part\":\"x\"}",         // i outside the group
        "{\"id\":\"\",\"i\":0,\"n\":1,\"part\":\"x\"}",          // empty id
        "{\"id\":\"a b\",\"i\":0,\"n\":1,\"part\":\"x\"}",       // id charset
        "{\"id\":\"a\",\"i\":0,\"n\":1}",                        // missing part
        "{\"id\":\"a\",\"i\":0,\"n\":1,\"part\":\"x\",\"part\":\"y\"}",  // repeated field
        "{\"id\":\"a\",\"i\":0,\"n\":1,\"part\":{\"a\":1}}",     // structured value
        "{\"id\":\"a\",\"i\":0,\"n\":1,\"part\":\"x\"} trailing",
    };
    for(const char* message : fatal) {
        INFO(message);
        ChunkAssembler fresh;
        REQUIRE(fresh.ingest(message, 0, out, reason) == ChunkAssembler::Outcome::Fatal);
        REQUIRE(reason != nullptr);
    }

    // An unknown scalar key is tolerated, because the browser side may grow a field.
    REQUIRE(assembler.ingest("{\"id\":\"a\",\"i\":0,\"n\":1,\"part\":\"\\\"p:1\\\"\",\"v\":2}",
                             0, out, reason)
            == ChunkAssembler::Outcome::Complete);
    REQUIRE(out == "\"p:1\"");
}

TEST_CASE("Reassembly bounds are fatal, not silent drops", "[p2pwire][adversarial]") {
    std::string out;
    const char* reason = nullptr;

    SECTION("a repeated fragment with different contents ends the connection") {
        ChunkAssembler assembler;
        REQUIRE(assembler.ingest("{\"id\":\"g1\",\"i\":0,\"n\":2,\"part\":\"aa\"}", 0, out, reason)
                == ChunkAssembler::Outcome::Pending);
        REQUIRE(assembler.ingest("{\"id\":\"g1\",\"i\":0,\"n\":2,\"part\":\"aa\"}", 0, out, reason)
                == ChunkAssembler::Outcome::Duplicate);
        REQUIRE(assembler.ingest("{\"id\":\"g1\",\"i\":0,\"n\":2,\"part\":\"bb\"}", 0, out, reason)
                == ChunkAssembler::Outcome::Fatal);
    }

    SECTION("changing a group's fragment count ends the connection") {
        ChunkAssembler assembler;
        REQUIRE(assembler.ingest("{\"id\":\"g1\",\"i\":0,\"n\":2,\"part\":\"aa\"}", 0, out, reason)
                == ChunkAssembler::Outcome::Pending);
        REQUIRE(assembler.ingest("{\"id\":\"g1\",\"i\":0,\"n\":3,\"part\":\"aa\"}", 0, out, reason)
                == ChunkAssembler::Outcome::Fatal);
    }

    SECTION("too many open groups ends the connection") {
        ChunkAssembler assembler;
        for(std::size_t i = 0; i < P2PWire::Limits::kMaxOpenGroups; ++i) {
            const std::string message = "{\"id\":\"g" + std::to_string(i)
                                      + "\",\"i\":0,\"n\":2,\"part\":\"aa\"}";
            REQUIRE(assembler.ingest(message, 0, out, reason) == ChunkAssembler::Outcome::Pending);
        }
        REQUIRE(assembler.ingest("{\"id\":\"gz\",\"i\":0,\"n\":2,\"part\":\"aa\"}", 0, out, reason)
                == ChunkAssembler::Outcome::Fatal);
    }

    SECTION("a group that never finishes ends the connection") {
        ChunkAssembler assembler;
        REQUIRE(assembler.ingest("{\"id\":\"g1\",\"i\":0,\"n\":2,\"part\":\"aa\"}", 0, out, reason)
                == ChunkAssembler::Outcome::Pending);
        REQUIRE(assembler.ingest("{\"id\":\"g1\",\"i\":1,\"n\":2,\"part\":\"bb\"}",
                                 P2PWire::Limits::kGroupLifetimeMs + 1, out, reason)
                == ChunkAssembler::Outcome::Fatal);
    }

    SECTION("an oversized fragment ends the connection") {
        ChunkAssembler assembler;
        const std::string message = "{\"id\":\"g1\",\"i\":0,\"n\":2,\"part\":\""
                                  + std::string(P2PWire::Limits::kMaxPacketSize + 1, 'a') + "\"}";
        REQUIRE(assembler.ingest(message, 0, out, reason) == ChunkAssembler::Outcome::Fatal);
    }
}

TEST_CASE("JSON strings are read strictly", "[p2pwire][adversarial]") {
    std::string out;
    std::size_t cursor = 0;

    cursor = 0;
    REQUIRE(P2PWire::decodeJsonString("\"a\\u0041\\n\"", cursor, 64, out));
    REQUIRE(out == "aA\n");

    const char* bad[] = {
        "\"unterminated",
        "\"\x01\"",              // raw control character
        "\"\\x\"",               // unknown escape
        "\"\\u00\"",             // truncated escape
        "\"\\ud800\"",           // lone high surrogate
        "\"\\udc00\"",           // lone low surrogate
        "\"\\ud800\\u0041\"",    // high surrogate not followed by a low one
    };
    for(const char* text : bad) {
        INFO(text);
        cursor = 0;
        REQUIRE_FALSE(P2PWire::decodeJsonString(text, cursor, 64, out));
    }

    // The bound is enforced before the allocation, not after it.
    cursor = 0;
    const std::string long_ = "\"" + std::string(100, 'a') + "\"";
    REQUIRE_FALSE(P2PWire::decodeJsonString(long_, cursor, 16, out));
}

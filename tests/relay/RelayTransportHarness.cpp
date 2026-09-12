/*
 *  RelayTransportHarness.cpp - drives the real crossplay transport against a real relay.
 *
 *  The wire harness (tests/wasm/RelayWireHarness.cpp) checks the parsers with crafted bytes.
 *  This one opens an actual connection: HTTPS admission, the WebSocket handshake, the relay
 *  handshake, membership, routing, the phase change and the diagnostic channel. It is the thing
 *  that can show that native and browser transports really work, and that two peers agree.
 *
 *  It is deliberately small: it uses RoomAdmissionClient and RoomRelayClient directly and does
 *  not pull in the game. No game data, no assets, no ENet.
 *
 *  Typical use, with tools/room-relay running on loopback:
 *
 *      ./relay_transport_harness --endpoint=http://127.0.0.1:8787 --dev --host --name=desktop
 *      ./relay_transport_harness --endpoint=http://127.0.0.1:8787 --dev --join=CODE --name=guest
 *
 *  The host prints "ROOM code=..." as soon as it has one. Every line is stable and greppable so
 *  two runs can be diffed.
 *
 *  Exit code 0 means the session did what it was asked to do.
 */

// This harness owns main(); SDL must not rename it.
#define SDL_MAIN_HANDLED

#include <Network/GameStateDigest.h>
#include <Network/NetworkPacketTypes.h>
#include <Network/RoomAdmissionClient.h>
#include <Network/RoomRelayClient.h>

#include <misc/SDL2pp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string endpoint;
    std::string name = "harness";
    std::string roomCode;
    bool        hosting = false;
    bool        development = false;
    bool        coop = false;
    bool        corruptState = false;
    bool        expectMismatch = false;
    int         seconds = 25;
};

void usage() {
    std::printf(
        "usage: relay_transport_harness --endpoint=URL (--host | --join=CODE) [options]\n"
        "  --name=NAME          player name to present (must differ between the two peers)\n"
        "  --dev                allow a plain http/ws loopback endpoint\n"
        "  --coop               host a two-player co-op room instead of a custom room\n"
        "  --corrupt            perturb this peer's synthetic state to force a divergence\n"
        "  --expect-mismatch    succeed only if a divergence is detected\n"
        "  --seconds=N          how long to stay in the room (default 25)\n");
}

bool parseOptions(int argc, char** argv, Options& options) {
    for(int index = 1; index < argc; index++) {
        const std::string argument(argv[index]);
        if(argument.rfind("--endpoint=", 0) == 0) {
            options.endpoint = argument.substr(11);
        } else if(argument.rfind("--name=", 0) == 0) {
            options.name = argument.substr(7);
        } else if(argument.rfind("--join=", 0) == 0) {
            options.roomCode = argument.substr(7);
            options.hosting = false;
        } else if(argument.rfind("--seconds=", 0) == 0) {
            options.seconds = std::atoi(argument.c_str() + 10);
        } else if(argument == "--host") {
            options.hosting = true;
        } else if(argument == "--dev") {
            options.development = true;
        } else if(argument == "--coop") {
            options.coop = true;
        } else if(argument == "--corrupt") {
            options.corruptState = true;
        } else if(argument == "--expect-mismatch") {
            options.expectMismatch = true;
        } else {
            std::printf("unknown option: %s\n", argument.c_str());
            return false;
        }
    }

    if(options.endpoint.empty()) {
        return false;
    }
    if(!options.hosting && options.roomCode.empty()) {
        return false;
    }
    if(options.seconds < 1 || options.seconds > 600) {
        return false;
    }
    return true;
}

/**
    A stand-in for the simulation: a tiny deterministic state machine both peers run identically.

    It exists so the harness can exercise the digest channel end to end without the game. Two
    honest peers produce identical digests by construction; --corrupt makes one of them diverge,
    which is how the divergence detection itself gets tested.
*/
class SyntheticState {
public:
    void advance() {
        cycle_ += GameStateDigest::kDigestIntervalCycles;
        seed_ = seed_ * 1103515245u + 12345u;
        objects_ = (objects_ + (seed_ >> 28)) % 97u;
        credits_ = (credits_ + 37) % 10000;
        ticks_++;
        if(corruptAfter_ > 0 && ticks_ == corruptAfter_) {
            seed_ ^= 0x5A5A5A5Au;
        }
    }

    void corruptAfterTicks(int ticks) { corruptAfter_ = ticks; }

    GameStateDigest::Digest digest() const {
        GameStateDigest::Digest result;
        result.gameCycle   = cycle_;
        result.randomSeed  = seed_;
        result.objectCount = objects_;

        GameStateDigest::Hasher objectHasher;
        for(std::uint32_t index = 0; index < objects_; index++) {
            objectHasher.mixUint32(index);
            objectHasher.mixUint32(seed_ ^ index);
            objectHasher.mixInt32(static_cast<std::int32_t>(index) - 5);
        }
        result.objectHash = objectHasher.value();

        GameStateDigest::Hasher houseHasher;
        houseHasher.mixInt32(credits_);
        houseHasher.mixUint32(objects_);
        result.houseHash = houseHasher.value();
        return result;
    }

private:
    std::uint32_t cycle_   = 0;
    std::uint32_t seed_    = 0x1234ABCD;
    std::uint32_t objects_ = 11;
    std::int32_t  credits_ = 1000;
    int           ticks_   = 0;
    int           corruptAfter_ = 0;
};

/// A game packet exactly as the game writes it: a little-endian uint32 packet id, then a body.
std::vector<std::uint8_t> gamePayload(std::uint32_t packetType, std::size_t bodyBytes,
                                      std::uint8_t filler) {
    std::vector<std::uint8_t> payload(4 + bodyBytes, filler);
    for(int shift = 0; shift < 32; shift += 8) {
        payload[shift / 8] = static_cast<std::uint8_t>((packetType >> shift) & 0xFF);
    }
    return payload;
}

const char* roleName(RoomRelay::Role role) {
    switch(role) {
        case RoomRelay::Role::Host:   return "host";
        case RoomRelay::Role::Client: return "client";
        default:                      return "unknown";
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if(!parseOptions(argc, argv, options)) {
        usage();
        return 2;
    }

    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_TIMER) != 0) {
        std::printf("RESULT failed reason=sdl-init\n");
        return 1;
    }

    const RelayWebSocketSupport support = relayWebSocketSupport();
    std::printf("HARNESS role=%s name=%s websocket=%s\n",
                options.hosting ? "host" : "client", options.name.c_str(),
                support.available ? "available" : "unavailable");
    if(!support.available) {
        std::printf("RESULT failed reason=no-websocket detail=%s\n", support.reason.c_str());
        SDL_Quit();
        return 1;
    }

    AdmissionRequest request;
    request.baseUrl = options.endpoint;
    request.allowLoopbackPlaintext = options.development;
    request.appVersion = "1.0.655";
    request.gameProtocol = static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
    // A fixed fingerprint: both peers of a harness run claim the same bundled content.
    request.contentHash = std::string(32, 'a');
#ifdef __EMSCRIPTEN__
    request.runtime = "browser";
#else
    request.runtime = "native";
#endif
    request.hosting = options.hosting;
    request.mode = options.coop ? "coop" : "custom";
    request.maxPeers = options.coop ? 2 : 4;
    request.roomCode = options.roomCode;

    RoomAdmissionClient admission;
    admission.begin(request);

    RoomRelayClient relay;
    bool relayStarted = false;
    bool joined = false;
    bool phaseAnnounced = false;

    SyntheticState state;
    if(options.corruptState) {
        state.corruptAfterTicks(2);
    }

    std::map<std::uint32_t, GameStateDigest::Digest> ourDigests;
    std::map<std::uint32_t, GameStateDigest::Digest> theirDigests;

    int sentPayloads = 0;
    int receivedPayloads = 0;
    int digestsSent = 0;
    int digestsCompared = 0;
    int digestMismatches = 0;
    int peersSeen = 0;
    int peersLeft = 0;
    std::string closeMessage;
    std::uint16_t closeCode = 0;

    const Uint32 startedAt = SDL_GetTicks();
    const Uint32 deadline = startedAt + static_cast<Uint32>(options.seconds) * 1000u;
    Uint32 lastTick = 0;

    while(SDL_GetTicks() < deadline) {
        admission.update();

        if(!relayStarted) {
            if(admission.status() == RoomAdmissionClient::Status::Failed) {
                std::printf("RESULT failed reason=admission detail=%s\n",
                            admission.errorMessage().c_str());
                SDL_Quit();
                return 1;
            }
            if(admission.status() == RoomAdmissionClient::Status::Succeeded) {
                const AdmissionResponse& granted = admission.response();
                std::printf("ROOM code=%s peers=%u\n", granted.roomCode.c_str(),
                            static_cast<unsigned>(granted.maxPeers));
                std::fflush(stdout);

                RoomRelayClient::Config config;
                config.socketUrl   = granted.socketUrl;
                config.grant       = granted.grant;
                config.displayName = options.name;
                config.appVersion  = "1.0.655";
                config.contentHash = request.contentHash;
                config.runtime     = request.runtime;
                config.gameProtocolVersion =
                    static_cast<std::uint16_t>(NETWORK_PROTOCOL_VERSION);
                config.allowLoopbackPlaintext = options.development;

                std::string error;
                if(!relay.start(config, error)) {
                    std::printf("RESULT failed reason=connect detail=%s\n", error.c_str());
                    SDL_Quit();
                    return 1;
                }
                relayStarted = true;
                admission.cancel();
            }
        }

        if(relayStarted) {
            relay.update();

            if(!joined && relay.isJoined()) {
                joined = true;
                std::printf("JOINED peer=%u role=%s room=%s\n",
                            static_cast<unsigned>(relay.localPeerId()),
                            relay.isHost() ? "host" : "client", relay.roomCode().c_str());
                std::fflush(stdout);
            }

            RoomRelayClient::Event event;
            while(relay.pollEvent(event)) {
                switch(event.type) {
                    case RoomRelayClient::Event::Type::PeerJoined: {
                        peersSeen++;
                        std::printf("PEER joined id=%u name=%s role=%s runtime=%s\n",
                                    static_cast<unsigned>(event.peerId), event.name.c_str(),
                                    roleName(event.role), event.runtime.c_str());
                    } break;

                    case RoomRelayClient::Event::Type::PeerLeft: {
                        peersLeft++;
                        std::printf("PEER left id=%u name=%s reason=%u\n",
                                    static_cast<unsigned>(event.peerId), event.name.c_str(),
                                    static_cast<unsigned>(event.reason));
                    } break;

                    case RoomRelayClient::Event::Type::GamePayload: {
                        receivedPayloads++;
                        std::printf("RECV type=%u from=%u bytes=%zu\n",
                                    static_cast<unsigned>(event.gameMessageType),
                                    static_cast<unsigned>(event.peerId), event.payload.size());
                    } break;

                    case RoomRelayClient::Event::Type::Diagnostic: {
                        GameStateDigest::Digest remote;
                        if(event.diagnosticKind
                               != static_cast<std::uint8_t>(RoomRelay::DiagnosticKind::StateDigest)
                           || !GameStateDigest::decode(event.payload.data(), event.payload.size(),
                                                       remote)) {
                            std::printf("DIGEST recv from=%u status=unusable\n",
                                        static_cast<unsigned>(event.peerId));
                            break;
                        }
                        theirDigests[remote.gameCycle] = remote;
                        const auto ours = ourDigests.find(remote.gameCycle);
                        if(ours != ourDigests.end()) {
                            digestsCompared++;
                            const bool same = !(ours->second.divergesFrom(remote));
                            if(!same) {
                                digestMismatches++;
                            }
                            std::printf("DIGEST compare cycle=%u match=%s local=%s remote=%s\n",
                                        static_cast<unsigned>(remote.gameCycle),
                                        same ? "yes" : "no",
                                        GameStateDigest::describe(ours->second).c_str(),
                                        GameStateDigest::describe(remote).c_str());
                        }
                    } break;

                    case RoomRelayClient::Event::Type::PhaseChanged: {
                        std::printf("PHASE %s by=%u\n",
                                    event.phase == RoomRelay::Phase::Match ? "match" : "lobby",
                                    static_cast<unsigned>(event.peerId));
                    } break;

                    case RoomRelayClient::Event::Type::Refused: {
                        std::printf("REFUSED code=%u detail=%s\n",
                                    static_cast<unsigned>(event.code), event.message.c_str());
                    } break;

                    case RoomRelayClient::Event::Type::Closed: {
                        closeCode = event.code;
                        closeMessage = event.message;
                        std::printf("CLOSED code=%u message=%s\n",
                                    static_cast<unsigned>(event.code), event.message.c_str());
                    } break;
                }
                std::fflush(stdout);
            }

            // Once there is somebody to talk to, run the agreed exchange one step per second.
            const Uint32 now = SDL_GetTicks();
            if(joined && !relay.peers().empty() && (now - lastTick) >= 1000) {
                lastTick = now;

                if(relay.isHost() && !phaseAnnounced) {
                    const std::vector<std::uint8_t> start =
                        gamePayload(NETWORKPACKET_STARTGAME, 4, 0x11);
                    if(relay.sendGamePayload(start.data(), start.size(), 0, 0)) {
                        sentPayloads++;
                        std::printf("SENT type=%u\n",
                                    static_cast<unsigned>(NETWORKPACKET_STARTGAME));
                    }
                    relay.setRoomPhase(RoomRelay::Phase::Match);
                    phaseAnnounced = true;
                    std::fflush(stdout);
                    continue;
                }

                const std::vector<std::uint8_t> chat =
                    gamePayload(NETWORKPACKET_CHATMESSAGE, 16, 0x22);
                if(relay.sendGamePayload(chat.data(), chat.size(), 0, 0)) {
                    sentPayloads++;
                }

                if(relay.phase() == RoomRelay::Phase::Match) {
                    const std::vector<std::uint8_t> commands =
                        gamePayload(NETWORKPACKET_COMMANDLIST, 24, 0x33);
                    if(relay.sendGamePayload(commands.data(), commands.size(), 1, 0)) {
                        sentPayloads++;
                    }

                    state.advance();
                    const GameStateDigest::Digest digest = state.digest();
                    ourDigests[digest.gameCycle] = digest;

                    std::uint8_t encoded[GameStateDigest::kEncodedSize];
                    GameStateDigest::encode(digest, encoded);
                    if(relay.sendDiagnostic(RoomRelay::DiagnosticKind::StateDigest, encoded,
                                            sizeof(encoded))) {
                        digestsSent++;
                        std::printf("DIGEST sent %s\n",
                                    GameStateDigest::describe(digest).c_str());
                    }

                    // A digest from the other side may have arrived before we reached its cycle.
                    const auto theirs = theirDigests.find(digest.gameCycle);
                    if(theirs != theirDigests.end()) {
                        digestsCompared++;
                        const bool same = !(digest.divergesFrom(theirs->second));
                        if(!same) {
                            digestMismatches++;
                        }
                        std::printf("DIGEST compare cycle=%u match=%s local=%s remote=%s\n",
                                    static_cast<unsigned>(digest.gameCycle),
                                    same ? "yes" : "no",
                                    GameStateDigest::describe(digest).c_str(),
                                    GameStateDigest::describe(theirs->second).c_str());
                    }
                }
                std::fflush(stdout);
            }

            if(relay.status() == RoomRelayClient::Status::Closed) {
                break;
            }
        }

        SDL_Delay(5);
    }

    if(relay.status() != RoomRelayClient::Status::Closed) {
        relay.stop(1);
    }

    const bool sawPeer = (peersSeen > 0);
    const bool exchanged = (sentPayloads > 0) && (receivedPayloads > 0);
    const bool digestOutcome = options.expectMismatch ? (digestMismatches > 0)
                                                      : (digestMismatches == 0);

    std::printf("RESULT %s peers=%d sent=%d received=%d digests=%d compared=%d mismatches=%d "
                "close=%u detail=%s\n",
                (sawPeer && exchanged && digestOutcome) ? "ok" : "failed",
                peersSeen, sentPayloads, receivedPayloads, digestsSent, digestsCompared,
                digestMismatches, static_cast<unsigned>(closeCode), closeMessage.c_str());
    (void)peersLeft;

    SDL_Quit();
    return (sawPeer && exchanged && digestOutcome) ? 0 : 1;
}

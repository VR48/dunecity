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

/**
    The browser half of a direct match, from the game's side of the boundary.

    The connection itself is P2PKit's hardened RTCTransport, owned by
    platform/web/src/dune-direct-bridge.ts and reached through the small queue-shaped surface the
    bridge installs on globalThis. Everything crossing this file is a string: the envelope text
    defined in include/Network/P2PWireFraming.h in one direction, and offers, answers and
    candidates in the other.

    Fragmentation is not done here. The bridge hands the value to RTCTransport, whose Chunker
    splits it with the same rules the native backend applies, which is what lets a browser and a
    native client play each other.
*/

#include <Network/DirectPeerConnection.h>

#include <Network/P2PSignalingProtocol.h>
#include <Network/P2PWireFraming.h>

#if defined(__EMSCRIPTEN__)

#include <emscripten.h>

#include <cstdlib>
#include <memory>
#include <string>

EM_JS_DEPS(dune_direct_bridge_deps, "$stringToUTF8,$lengthBytesUTF8,malloc");

// The bridge is absent only when the page did not load platform/web/p2p-direct.js. Every entry
// point checks, so a missing bridge is a clean refusal rather than an exception in the glue.
EM_JS(int, duneDirectBridgeAvailable, (), {
    return (typeof globalThis.DuneDirectBridge === "object" && globalThis.DuneDirectBridge !== null) ? 1 : 0;
});

EM_JS(int, duneDirectCreate, (const char* iceServersJson, int initiator, const char* label), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    try {
        return bridge.create(UTF8ToString(iceServersJson), initiator !== 0, UTF8ToString(label));
    } catch(error) {
        return 0;
    }
});

EM_JS(int, duneDirectSetRemoteDescription, (int handle, const char* kind, const char* sdp), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    try {
        return bridge.setRemoteDescription(handle, UTF8ToString(kind), UTF8ToString(sdp)) ? 1 : 0;
    } catch(error) {
        return 0;
    }
});

EM_JS(int, duneDirectAddCandidate, (int handle, const char* payload), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    try {
        return bridge.addRemoteCandidate(handle, UTF8ToString(payload)) ? 1 : 0;
    } catch(error) {
        return 0;
    }
});

EM_JS(int, duneDirectSendValue, (int handle, const char* value), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    try {
        return bridge.sendValue(handle, UTF8ToString(value)) ? 1 : 0;
    } catch(error) {
        return 0;
    }
});

/// Returns a malloc'd C string, or null when the queue is empty. The caller frees it.
EM_JS(char*, duneDirectPollValue, (int handle), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    var text = null;
    try { text = bridge.pollValue(handle); } catch(error) { return 0; }
    if(typeof text !== "string") return 0;
    var length = lengthBytesUTF8(text) + 1;
    var pointer = _malloc(length);
    if(!pointer) return 0;
    stringToUTF8(text, pointer, length);
    return pointer;
});

EM_JS(char*, duneDirectPollSignal, (int handle), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    var text = null;
    try { text = bridge.pollSignal(handle); } catch(error) { return 0; }
    if(typeof text !== "string") return 0;
    var length = lengthBytesUTF8(text) + 1;
    var pointer = _malloc(length);
    if(!pointer) return 0;
    stringToUTF8(text, pointer, length);
    return pointer;
});

EM_JS(char*, duneDirectLastError, (int handle), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    var text = "";
    try { text = bridge.lastError(handle); } catch(error) { return 0; }
    if(typeof text !== "string" || text.length === 0) return 0;
    var length = lengthBytesUTF8(text) + 1;
    var pointer = _malloc(length);
    if(!pointer) return 0;
    stringToUTF8(text, pointer, length);
    return pointer;
});

EM_JS(double, duneDirectBufferedAmount, (int handle), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 0;
    try { return bridge.bufferedAmount(handle); } catch(error) { return 0; }
});

EM_JS(int, duneDirectState, (int handle), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return 4;
    try { return bridge.state(handle); } catch(error) { return 4; }
});

EM_JS(void, duneDirectClose, (int handle), {
    var bridge = globalThis.DuneDirectBridge;
    if(!bridge) return;
    try { bridge.close(handle); } catch(error) { /* already gone */ }
});

namespace {

/// Takes ownership of a string the bridge allocated, and frees it.
bool takeBridgeString(char* pointer, std::string& out) {
    if(pointer == nullptr) {
        return false;
    }
    out.assign(pointer);
    std::free(pointer);
    return true;
}

class BrowserBridgeConnection final : public DirectPeerConnection {
public:
    explicit BrowserBridgeConnection(const DirectPeerConnectionOptions& options) {
        // Refused rather than filtered. Connecting with a configuration nobody chose is a
        // success that only shows up later, as a player who cannot get through a NAT; the
        // bridge refuses a configuration it does not understand for the same reason.
        std::string iceJson = "[";
        for(std::size_t i = 0; i < options.iceServers.size(); ++i) {
            if(!P2PSignal::isAcceptableIceUrl(options.iceServers[i])) {
                state_     = State::Failed;
                lastError_ = "the game service named a connection helper this game will not use";
                return;
            }
            if(iceJson.size() > 1) {
                iceJson += ',';
            }
            iceJson += P2PWire::encodeJsonString(options.iceServers[i]);
        }
        iceJson += ']';

        handle_ = duneDirectCreate(iceJson.c_str(), options.initiator ? 1 : 0,
                                   options.label.c_str());
        if(handle_ == 0) {
            state_     = State::Failed;
            lastError_ = "the browser could not create a direct connection";
        } else {
            state_ = State::Connecting;
        }
    }

    ~BrowserBridgeConnection() override { close(); }

    bool setRemoteDescription(P2PSignal::SignalKind kind, const std::string& sdp) override {
        if(handle_ == 0 || kind == P2PSignal::SignalKind::Candidate || sdp.empty()
           || sdp.size() > P2PSignal::Limits::kMaxSdpBytes) {
            return false;
        }
        const char* kindName = kind == P2PSignal::SignalKind::Answer ? "answer" : "offer";
        return duneDirectSetRemoteDescription(handle_, kindName, sdp.c_str()) != 0;
    }

    bool addRemoteCandidate(const std::string& candidate) override {
        // The same shared rule the native backend applies; the bridge and the vendored transport
        // then check it again on their side of the boundary.
        if(handle_ == 0 || !P2PSignal::isAcceptableCandidatePayload(candidate)) {
            return false;
        }
        return duneDirectAddCandidate(handle_, candidate.c_str()) != 0;
    }

    bool sendValue(const std::string& value) override {
        if(handle_ == 0 || state_ != State::Connected) {
            return false;
        }
        return duneDirectSendValue(handle_, value.c_str()) != 0;
    }

    std::size_t bufferedAmount() const override {
        if(handle_ == 0) {
            return 0;
        }
        const double buffered = duneDirectBufferedAmount(handle_);
        return buffered > 0 ? static_cast<std::size_t>(buffered) : 0;
    }

    void update() override {
        if(handle_ == 0) {
            return;
        }
        const int state = duneDirectState(handle_);
        switch(state) {
            case 0: state_ = State::New;        break;
            case 1: state_ = State::Connecting; break;
            case 2: state_ = State::Connected;  break;
            case 3: state_ = State::Failed;     break;
            default: state_ = State::Closed;    break;
        }
        std::string error;
        if(takeBridgeString(duneDirectLastError(handle_), error) && !error.empty()) {
            lastError_ = error;
        }
    }

    void close() override {
        if(handle_ != 0) {
            duneDirectClose(handle_);
            handle_ = 0;
        }
        if(state_ != State::Failed) {
            state_ = State::Closed;
        }
    }

    bool pollLocalSignal(LocalSignal& out) override {
        if(handle_ == 0) {
            return false;
        }
        std::string text;
        if(!takeBridgeString(duneDirectPollSignal(handle_), text)) {
            return false;
        }
        const std::size_t split = text.find('|');
        if(split == std::string::npos) {
            return false;
        }
        const std::string kind = text.substr(0, split);
        if(!P2PSignal::parseSignalKind(kind, out.kind)) {
            return false;
        }
        out.payload = text.substr(split + 1);
        return !out.payload.empty();
    }

    bool pollValue(std::string& out) override {
        if(handle_ == 0) {
            return false;
        }
        return takeBridgeString(duneDirectPollValue(handle_), out);
    }

    State state() const override { return state_; }

    const std::string& lastError() const override { return lastError_; }

private:
    int         handle_ = 0;
    State       state_  = State::New;
    std::string lastError_;
};

} // namespace

bool isDirectPeerConnectionAvailable() {
    return duneDirectBridgeAvailable() != 0;
}

std::unique_ptr<DirectPeerConnection> createDirectPeerConnection(
    const DirectPeerConnectionOptions& options, std::string& error) {
    if(duneDirectBridgeAvailable() == 0) {
        error = "the page did not load the direct connection bridge";
        return nullptr;
    }
    auto connection = std::make_unique<BrowserBridgeConnection>(options);
    if(connection->state() == DirectPeerConnection::State::Failed) {
        error = connection->lastError();
        return nullptr;
    }
    return connection;
}

#endif // __EMSCRIPTEN__

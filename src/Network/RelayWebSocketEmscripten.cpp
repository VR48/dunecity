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

#ifdef __EMSCRIPTEN__

/**
    Browser relay transport on the Emscripten WebSocket API (-lwebsocket.js).

    The rule this file exists to enforce: **a browser event never reaches game code directly**.
    The open/message/error/close callbacks fire from the browser's event loop, which in this
    build can interleave with the simulation at any Asyncify yield point. They therefore do
    nothing except validate sizes and append to a queue. pump(), called from the game loop,
    is the only thing that hands data onwards.

    Nothing here blocks. There is no synchronous XHR, no busy wait and no pthread.
*/

#include <Network/RelayWebSocket.h>

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

#include <SDL.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace {

class EmscriptenRelayWebSocket final : public RelayWebSocket {
public:
    explicit EmscriptenRelayWebSocket(const std::string& url) {
        if(!emscripten_websocket_is_supported()) {
            lastError_ = "This browser cannot open game connections.";
            state_ = State::Closed;
            return;
        }

        EmscriptenWebSocketCreateAttributes attributes;
        emscripten_websocket_init_create_attributes(&attributes);
        attributes.url = url.c_str();
        attributes.protocols = nullptr;
        attributes.createOnMainThread = EM_TRUE;

        socket_ = emscripten_websocket_new(&attributes);
        if(socket_ <= 0) {
            lastError_ = "This browser refused the game connection.";
            state_ = State::Closed;
            return;
        }

        emscripten_websocket_set_onopen_callback(socket_, this, onOpen);
        emscripten_websocket_set_onmessage_callback(socket_, this, onMessage);
        emscripten_websocket_set_onerror_callback(socket_, this, onError);
        emscripten_websocket_set_onclose_callback(socket_, this, onClose);
    }

    ~EmscriptenRelayWebSocket() override {
        if(socket_ > 0) {
            // Detach first: a callback that fires while this object is being destroyed would
            // write through a dangling pointer.
            emscripten_websocket_set_onopen_callback(socket_, nullptr, nullptr);
            emscripten_websocket_set_onmessage_callback(socket_, nullptr, nullptr);
            emscripten_websocket_set_onerror_callback(socket_, nullptr, nullptr);
            emscripten_websocket_set_onclose_callback(socket_, nullptr, nullptr);
            emscripten_websocket_close(socket_, 1000, "closing");
            emscripten_websocket_delete(socket_);
            socket_ = 0;
        }
    }

    void pump() override {
        if(state_ != State::Open) {
            return;
        }
        flushOutgoing();
    }

    State state() const override { return state_; }

    bool send(const std::vector<std::uint8_t>& frame) override {
        if(state_ == State::Closed) {
            return false;
        }
        if(frame.empty() || frame.size() > RoomRelay::Limits::kMaxFrameBytes) {
            fail("The game tried to send a message that was too large.");
            return false;
        }
        if(outgoingBytes_ + frame.size() > RoomRelay::Limits::kMaxOutgoingQueueBytes
           || outgoing_.size() >= RoomRelay::Limits::kMaxQueuedFrames) {
            // Deliberately fatal: silently discarding a queued game message would desynchronise
            // the match instead of reporting a problem the player can see.
            fail("This browser tab could not keep up with the game connection.");
            return false;
        }

        outgoingBytes_ += frame.size();
        outgoing_.push_back(frame);
        if(state_ == State::Open) {
            flushOutgoing();
        }
        return state_ != State::Closed;
    }

    bool receive(std::vector<std::uint8_t>& frame) override {
        if(incoming_.empty()) {
            return false;
        }
        frame = std::move(incoming_.front());
        incoming_.pop_front();
        incomingBytes_ -= (frame.size() < incomingBytes_) ? frame.size() : incomingBytes_;
        return true;
    }

    void close(std::uint16_t code, const std::string& reason) override {
        if(socket_ > 0 && state_ != State::Closed) {
            // The browser only accepts 1000 or a 3000..4999 code, and at most 123 bytes.
            const unsigned short wireCode =
                (code == 1000 || (code >= 3000 && code <= 4999)) ? code : 1000;
            const std::string trimmed = RoomRelay::sanitizeRelayMessage(reason).substr(0, 100);
            emscripten_websocket_close(socket_, wireCode, trimmed.c_str());
        }
        if(closeCode_ == 0) {
            closeCode_ = code;
        }
        state_ = State::Closed;
    }

    std::uint16_t closeCode() const override { return closeCode_; }
    const std::string& lastError() const override { return lastError_; }

    std::size_t outgoingBacklogBytes() const override {
        std::size_t buffered = 0;
        if(socket_ > 0) {
            emscripten_websocket_get_buffered_amount(socket_, &buffered);
        }
        return outgoingBytes_ + buffered;
    }

private:
    void fail(const std::string& message) {
        if(lastError_.empty()) {
            lastError_ = message;
        }
        state_ = State::Closed;
    }

    void flushOutgoing() {
        while(!outgoing_.empty()) {
            std::vector<std::uint8_t>& frame = outgoing_.front();
            const EMSCRIPTEN_RESULT result = emscripten_websocket_send_binary(
                socket_, frame.data(), static_cast<uint32_t>(frame.size()));
            if(result != EMSCRIPTEN_RESULT_SUCCESS) {
                fail("The connection to the game was lost.");
                return;
            }
            outgoingBytes_ -= (frame.size() < outgoingBytes_) ? frame.size() : outgoingBytes_;
            outgoing_.pop_front();
        }
    }

    // --- browser callbacks: queue only, never touch game state ---------------------------

    static EM_BOOL onOpen(int, const EmscriptenWebSocketOpenEvent*, void* userData) {
        auto* self = static_cast<EmscriptenRelayWebSocket*>(userData);
        if(self == nullptr) return EM_TRUE;
        if(self->state_ != State::Closed) {
            self->state_ = State::Open;
            self->flushOutgoing();
        }
        return EM_TRUE;
    }

    static EM_BOOL onMessage(int, const EmscriptenWebSocketMessageEvent* event, void* userData) {
        auto* self = static_cast<EmscriptenRelayWebSocket*>(userData);
        if(self == nullptr || event == nullptr) return EM_TRUE;
        if(self->state_ == State::Closed) return EM_TRUE;

        if(event->isText) {
            self->closeCode_ = RoomRelay::Close::ProtocolError;
            self->fail("The game service sent an unexpected message.");
            return EM_TRUE;
        }

        // Limits are applied to the announced length before anything is copied out of the
        // browser's buffer. numBytes is a uint32 and the queue bound is checked by subtraction,
        // which matters on wasm32 where size_t addition wraps.
        const std::size_t length = static_cast<std::size_t>(event->numBytes);
        if(length == 0 || length > RoomRelay::Limits::kMaxFrameBytes) {
            self->closeCode_ = RoomRelay::Close::TooLarge;
            self->fail("The game service sent a message that was too large.");
            return EM_TRUE;
        }
        if(self->incoming_.size() >= RoomRelay::Limits::kMaxQueuedFrames
           || self->incomingBytes_ > RoomRelay::Limits::kMaxIncomingQueueBytes - length) {
            self->fail("The game fell too far behind the connection.");
            return EM_TRUE;
        }

        self->incoming_.emplace_back(event->data, event->data + length);
        self->incomingBytes_ += length;
        return EM_TRUE;
    }

    static EM_BOOL onError(int, const EmscriptenWebSocketErrorEvent*, void* userData) {
        auto* self = static_cast<EmscriptenRelayWebSocket*>(userData);
        if(self == nullptr) return EM_TRUE;
        // The browser deliberately hides the reason a WebSocket failed, so there is nothing more
        // specific that can honestly be reported here.
        self->fail("The game service could not be reached.");
        return EM_TRUE;
    }

    static EM_BOOL onClose(int, const EmscriptenWebSocketCloseEvent* event, void* userData) {
        auto* self = static_cast<EmscriptenRelayWebSocket*>(userData);
        if(self == nullptr) return EM_TRUE;
        if(event != nullptr && self->closeCode_ == 0) {
            self->closeCode_ = static_cast<std::uint16_t>(event->code);
        }
        if(self->lastError_.empty()) {
            self->lastError_ = RoomRelay::describeCloseCode(self->closeCode_);
        }
        self->state_ = State::Closed;
        return EM_TRUE;
    }

    EMSCRIPTEN_WEBSOCKET_T socket_ = 0;
    State         state_     = State::Connecting;
    std::uint16_t closeCode_ = 0;
    std::string   lastError_;

    std::deque<std::vector<std::uint8_t>> outgoing_;
    std::size_t outgoingBytes_ = 0;
    std::deque<std::vector<std::uint8_t>> incoming_;
    std::size_t incomingBytes_ = 0;
};

} // namespace

RelayWebSocketSupport relayWebSocketSupport() {
    RelayWebSocketSupport support;
    support.available = emscripten_websocket_is_supported() != 0;
    if(!support.available) {
        support.reason = "This browser cannot open game connections.";
    }
    return support;
}

std::unique_ptr<RelayWebSocket> createRelayWebSocket(const std::string& url,
                                                     const std::string& origin) {
    // The browser sets Origin itself and does not let a page override it, which is exactly why
    // Origin is worth checking on the relay for browsers and worth nothing for native clients.
    (void)origin;
    if(!emscripten_websocket_is_supported()) {
        return nullptr;
    }
    return std::make_unique<EmscriptenRelayWebSocket>(url);
}

#endif // __EMSCRIPTEN__

/*
 *  MockRelayWebSocket.h - a scriptable relay socket for the session tests.
 *
 *  RoomRelayClient reaches its transport through two free functions, relayWebSocketSupport() and
 *  createRelayWebSocket(). That is a link seam: a test target can compile the production
 *  RoomRelayClient.cpp and supply its own definitions of those two symbols instead of linking
 *  RelayWebSocketCurl.cpp. No hook, flag or virtual exists in the production build for this; the
 *  substitution happens entirely in the test target's source list.
 *
 *  What the mock gives the test is exact control over what the session sees: which frames arrive
 *  and when, whether the socket is open, and what the session tried to send back.
 */

#ifndef MOCKRELAYWEBSOCKET_H
#define MOCKRELAYWEBSOCKET_H

#include <Network/RelayWebSocket.h>
#include <Network/RoomRelayProtocol.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class MockRelayWebSocket final : public RelayWebSocket {
public:
    void pump() override { pumpCount++; }

    State state() const override { return state_; }

    bool send(const std::vector<std::uint8_t>& frame) override {
        if(state_ == State::Closed || refuseSends) {
            return false;
        }
        sentFrames.push_back(frame);
        return true;
    }

    bool receive(std::vector<std::uint8_t>& frame) override {
        if(inbound.empty()) {
            return false;
        }
        frame = std::move(inbound.front());
        inbound.pop_front();
        return true;
    }

    void close(std::uint16_t code, const std::string& reason) override {
        closeCalls++;
        if(closeCode_ == 0) {
            closeCode_ = code;
        }
        lastCloseReason = reason;
        state_ = State::Closed;
    }

    std::uint16_t closeCode() const override { return closeCode_; }
    const std::string& lastError() const override { return lastError_; }
    std::size_t outgoingBacklogBytes() const override { return 0; }

    // --- test controls -------------------------------------------------------------------

    void open() { state_ = State::Open; }
    void closeFromPeer(std::uint16_t code, const std::string& error) {
        closeCode_ = code;
        lastError_ = error;
        state_ = State::Closed;
    }
    void deliver(std::vector<std::uint8_t> frame) { inbound.push_back(std::move(frame)); }

    State                                 state_ = State::Connecting;
    std::uint16_t                         closeCode_ = 0;
    std::string                           lastError_;
    std::string                           lastCloseReason;
    bool                                  refuseSends = false;
    int                                   pumpCount = 0;
    int                                   closeCalls = 0;
    std::deque<std::vector<std::uint8_t>> inbound;
    std::vector<std::vector<std::uint8_t>> sentFrames;
};

namespace MockRelayTransport {

/// The socket the next createRelayWebSocket() call hands out, and the one the test drives.
MockRelayWebSocket* current();

/// Forgets the previous socket so the next session starts from a clean one.
void reset();

/// What createRelayWebSocket() was asked for, so URL handling can be asserted.
const std::string& lastUrl();
const std::string& lastOrigin();

/// Makes the next relayWebSocketSupport() report the platform as unusable.
void setSupported(bool supported, const std::string& reason);

/// Makes the next createRelayWebSocket() return nullptr, as a refused URL would.
void setCreateFails(bool fails);

} // namespace MockRelayTransport

#endif // MOCKRELAYWEBSOCKET_H

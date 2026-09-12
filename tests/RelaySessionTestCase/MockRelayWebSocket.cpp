/*
 *  MockRelayWebSocket.cpp - the link seam described in MockRelayWebSocket.h.
 *
 *  These are the same two symbols RelayWebSocketCurl.cpp and RelayWebSocketEmscripten.cpp
 *  define. The relay session test target compiles this file instead of either of them, so the
 *  production RoomRelayClient.cpp under test is byte-identical to the one that ships.
 */

#include "MockRelayWebSocket.h"

#include <memory>

namespace {

MockRelayWebSocket* currentSocket = nullptr;
std::string lastUrlValue;
std::string lastOriginValue;
bool        supportedValue = true;
std::string supportReason;
bool        createFailsValue = false;

} // namespace

namespace MockRelayTransport {

MockRelayWebSocket* current() { return currentSocket; }

void reset() {
    currentSocket = nullptr;
    lastUrlValue.clear();
    lastOriginValue.clear();
    supportedValue = true;
    supportReason.clear();
    createFailsValue = false;
}

const std::string& lastUrl() { return lastUrlValue; }
const std::string& lastOrigin() { return lastOriginValue; }

void setSupported(bool supported, const std::string& reason) {
    supportedValue = supported;
    supportReason = reason;
}

void setCreateFails(bool fails) { createFailsValue = fails; }

} // namespace MockRelayTransport

RelayWebSocketSupport relayWebSocketSupport() {
    RelayWebSocketSupport support;
    support.available = supportedValue;
    support.reason = supportReason;
    return support;
}

std::unique_ptr<RelayWebSocket> createRelayWebSocket(const std::string& url,
                                                     const std::string& origin) {
    lastUrlValue = url;
    lastOriginValue = origin;
    if(createFailsValue) {
        currentSocket = nullptr;
        return nullptr;
    }

    auto socket = std::make_unique<MockRelayWebSocket>();
    // The session owns the socket; the test observes it through this raw pointer. It is valid
    // until that session is destroyed or stopped - stop() releases the socket - so every test
    // calls reset() before it starts rather than trusting what a previous one left behind.
    currentSocket = socket.get();
    return socket;
}

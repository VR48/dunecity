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
    Headless interoperability harness: the real native backend against a real browser.

    This is the check that unit tests cannot make. It builds the same
    DirectPeerConnectionLibdatachannel the game uses, with the same framing, and talks to a page
    running the shipped browser bridge over P2PKit's RTCTransport. If the two ever stop agreeing
    about the wire - a fragment shape, an envelope field, a candidate encoding - this fails, and
    it fails without a signaling service, a relay or an internet connection: signals are exchanged
    through a directory, and candidates are host candidates on loopback.

    Usage:
        dunecity_p2p_interop --dir <mailbox> [--initiator] [--timeout <seconds>]

    See tools/p2p-interop/README.md for the browser half.
*/

#include <Network/DirectPeerConnection.h>
#include <Network/P2PWireFraming.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

/// How many ordered messages are sent after the large one, to check ordering end to end.
constexpr int kOrderedMessages = 100;

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// Writes to a temporary name and renames, so the reader never sees half a message.
void writeMessage(const std::filesystem::path& directory, const std::string& name,
                  const std::string& text) {
    const std::filesystem::path temporary = directory / (name + ".partial");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << text;
    }
    std::filesystem::rename(temporary, directory / (name + ".msg"));
}

const char* kindName(P2PSignal::SignalKind kind) {
    return P2PSignal::signalKindName(kind);
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path mailbox;
    bool initiator = false;
    int  timeoutSeconds = 60;

    for(int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if(argument == "--dir" && i + 1 < argc) {
            mailbox = argv[++i];
        } else if(argument == "--initiator") {
            initiator = true;
        } else if(argument == "--timeout" && i + 1 < argc) {
            timeoutSeconds = std::atoi(argv[++i]);
        }
    }
    if(mailbox.empty()) {
        std::cerr << "usage: dunecity_p2p_interop --dir <mailbox> [--initiator]\n";
        return 2;
    }

    const std::filesystem::path outbox = mailbox / "to-browser";
    const std::filesystem::path inbox  = mailbox / "to-native";
    std::filesystem::create_directories(outbox);
    std::filesystem::create_directories(inbox);

    DirectPeerConnectionOptions options;
    options.initiator = initiator;
    // No STUN: host candidates on loopback are enough, and the harness must not need a network.
    std::string error;
    std::unique_ptr<DirectPeerConnection> connection = createDirectPeerConnection(options, error);
    if(!connection) {
        std::cerr << "could not create a peer connection: " << error << "\n";
        return 1;
    }

    // The payloads. The large one is a full-size game packet, which is what exercises
    // fragmentation in both directions.
    std::vector<std::uint8_t> large(P2PWire::Limits::kMaxGamePayloadBytes, 0);
    for(std::size_t i = 0; i < large.size(); ++i) {
        large[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    const std::string largeEnvelope =
        P2PWire::encodeGameEnvelope(large.data(), large.size(), 0, 0);

    std::set<std::string> consumed;
    int    outgoingSequence = 0;
    bool   sentPayloads     = false;
    bool   sawLarge         = false;
    int    orderedSeen      = 0;
    bool   ordered          = true;

    const auto started = std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now() - started < std::chrono::seconds(timeoutSeconds)) {
        connection->update();

        // Publish whatever the backend produced.
        DirectPeerConnection::LocalSignal signal;
        while(connection->pollLocalSignal(signal)) {
            const std::string name = "n" + std::to_string(outgoingSequence++);
            writeMessage(outbox, name, std::string(kindName(signal.kind)) + "\n" + signal.payload);
        }

        // Apply whatever the browser left for us.
        std::vector<std::filesystem::path> arrived;
        for(const auto& entry : std::filesystem::directory_iterator(inbox)) {
            if(entry.path().extension() == ".msg"
               && consumed.find(entry.path().filename().string()) == consumed.end()) {
                arrived.push_back(entry.path());
            }
        }
        std::sort(arrived.begin(), arrived.end());
        for(const auto& path : arrived) {
            consumed.insert(path.filename().string());
            const std::string text = readFile(path);
            const std::size_t split = text.find('\n');
            if(split == std::string::npos) {
                continue;
            }
            const std::string kind    = text.substr(0, split);
            const std::string payload = text.substr(split + 1);
            if(kind == "candidate") {
                connection->addRemoteCandidate(payload);
            } else if(kind == "offer" || kind == "answer") {
                connection->setRemoteDescription(kind == "answer" ? P2PSignal::SignalKind::Answer
                                                                  : P2PSignal::SignalKind::Offer,
                                                 payload);
            }
        }

        if(connection->state() == DirectPeerConnection::State::Connected && !sentPayloads) {
            sentPayloads = true;
            if(!connection->sendValue(largeEnvelope)) {
                std::cerr << "the large payload was refused: " << connection->lastError() << "\n";
                return 1;
            }
            for(int i = 0; i < kOrderedMessages; ++i) {
                const std::string probe = P2PWire::encodeProbeEnvelope(false,
                                                                       static_cast<std::uint32_t>(i));
                if(!connection->sendValue(probe)) {
                    std::cerr << "an ordered message was refused at " << i << "\n";
                    return 1;
                }
            }
        }

        std::string value;
        while(connection->pollValue(value)) {
            P2PWire::Envelope envelope;
            const char* reason = nullptr;
            if(!P2PWire::decodeEnvelope(value, envelope, reason)) {
                std::cerr << "the browser sent something unreadable: "
                          << (reason != nullptr ? reason : "?") << "\n";
                return 1;
            }
            if(envelope.kind == P2PWire::EnvelopeKind::Game) {
                if(envelope.payload != large) {
                    std::cerr << "the large payload came back different\n";
                    return 1;
                }
                sawLarge = true;
            } else if(envelope.kind == P2PWire::EnvelopeKind::Ping) {
                if(envelope.token != static_cast<std::uint32_t>(orderedSeen)) {
                    ordered = false;
                }
                ++orderedSeen;
            }
        }

        if(sawLarge && orderedSeen >= kOrderedMessages) {
            break;
        }
        if(connection->state() == DirectPeerConnection::State::Failed) {
            std::cerr << "the connection failed: " << connection->lastError() << "\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    connection->close();

    if(!sawLarge || orderedSeen < kOrderedMessages || !ordered) {
        std::cerr << "interop failed: large=" << sawLarge << " ordered=" << orderedSeen
                  << "/" << kOrderedMessages << " inOrder=" << ordered << "\n";
        return 1;
    }
    std::cout << "interop ok: full-size payload and " << kOrderedMessages
              << " ordered messages round tripped between native and browser\n";
    return 0;
}

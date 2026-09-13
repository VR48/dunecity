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

#include <Network/RoomAdmissionClient.h>

#include <Network/BoundedHttpClient.h>

#include <misc/SDL2pp.h>

#include <memory>
#include <string>

namespace {

/// Longest we wait for an admission answer before telling the player it did not work.
constexpr long kAdmissionTimeoutSeconds = 10;

/**
    Checks the admission base URL with the same rules as the gameplay socket: https:// always,
    plain http:// only for loopback and only when the development endpoint was chosen.
    Implemented by mapping the scheme onto the WebSocket one so there is a single rule.
*/
bool isAcceptableAdmissionBaseUrl(const std::string& baseUrl, bool allowLoopbackPlaintext,
                                  std::string& error) {
    std::string mapped;
    if(baseUrl.compare(0, 8, "https://") == 0) {
        mapped = "wss://" + baseUrl.substr(8);
    } else if(baseUrl.compare(0, 7, "http://") == 0) {
        mapped = "ws://" + baseUrl.substr(7);
    } else {
        error = "The game service address must start with https://.";
        return false;
    }
    if(!mapped.empty() && mapped.back() == '/') {
        mapped.pop_back();
    }
    return isAcceptableRelayUrl(mapped, allowLoopbackPlaintext, error);
}

std::string buildFormBody(const AdmissionRequest& request) {
    std::string body;
    body += "app=dunecity";
    body += "&appVersion=" + RoomAdmission::encodeFormValue(request.appVersion);
    body += "&gameProtocol=" + std::to_string(static_cast<unsigned>(request.gameProtocol));
    body += "&contentHash=" + RoomAdmission::encodeFormValue(request.contentHash);
    body += "&runtime=" + RoomAdmission::encodeFormValue(request.runtime);
    if(request.operation == AdmissionOperation::Visibility) {
        body += "&room=" + RoomAdmission::encodeFormValue(request.roomCode);
        body += "&control=" + RoomAdmission::encodeFormValue(request.controlToken);
        body += request.publicRoom ? "&visibility=public" : "&visibility=private";
    } else if(request.operation != AdmissionOperation::Room) {
        body += "&session=" + RoomAdmission::encodeFormValue(request.chatSession);
        body += "&name=" + RoomAdmission::hexText(request.displayName);
        body += "&text=" + RoomAdmission::hexText(request.chatText);
        body += "&cursor=" + std::to_string(request.chatCursor);
    } else if(request.listing) {
        body += "&offset=" + std::to_string(request.listOffset);
    } else if(request.hosting) {
        body += "&maxPeers=" + std::to_string(static_cast<unsigned>(request.maxPeers));
        body += "&mode=" + RoomAdmission::encodeFormValue(request.mode);
        body += request.publicRoom ? "&visibility=public" : "&visibility=private";
    } else {
        body += "&room=" + RoomAdmission::encodeFormValue(request.roomCode);
        body += request.publicOnly ? "&publicOnly=1" : "&publicOnly=0";
    }
    return body;
}

std::string buildEndpointUrl(const AdmissionRequest& request) {
    std::string base = request.baseUrl;
    while(!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    switch(request.operation) {
        case AdmissionOperation::Visibility: return base + "/v1/admission/visibility";
        case AdmissionOperation::ChatEnter: return base + "/v1/lobby/enter";
        case AdmissionOperation::ChatPoll: return base + "/v1/lobby/poll";
        case AdmissionOperation::ChatSay: return base + "/v1/lobby/say";
        default: break;
    }
    return base + (request.listing ? "/v1/admission/list"
        : request.hosting ? "/v1/admission/host" : "/v1/admission/join");
}

} // namespace

// -------------------------------------------------------------------------------------------
// Platform state
// -------------------------------------------------------------------------------------------

/**
    The request in flight.

    There used to be two of these, one per platform, and they were the weaker half of the pair:
    the browser one read the whole answer into memory before anything checked its size, and the
    native one left the ambient proxy configuration in place. Both are now the shared bounded
    client (include/Network/BoundedHttpClient.h), which is the same code the direct-play
    signalling uses - admission is the first network boundary a direct match crosses, so it gets
    the same treatment as the rest.
*/
class RoomAdmissionClient::Impl {
public:
    std::unique_ptr<BoundedHttpClient> http = createBoundedHttpClient();
    std::string url;
    std::string body;
};

// -------------------------------------------------------------------------------------------

RoomAdmissionClient::RoomAdmissionClient() = default;
RoomAdmissionClient::~RoomAdmissionClient() = default;

void RoomAdmissionClient::cancel() {
    impl_.reset();
    if(status_ == Status::InProgress) {
        status_ = Status::Idle;
    }
}

void RoomAdmissionClient::finishWithError(const std::string& message) {
    impl_.reset();
    response_ = AdmissionResponse();
    errorMessage_ = message;
    status_ = Status::Failed;
}

void RoomAdmissionClient::finishWithBody(long httpStatus, const std::string& body) {
    impl_.reset();

    AdmissionResponse parsed;
    std::string error;
    if(!RoomAdmission::parseAdmissionResponse(body, parsed, error, listing_, operation_)) {
        finishWithError(error);
        return;
    }

    if(!parsed.ok) {
        response_ = parsed;
        errorMessage_ = parsed.errorMessage;
        status_ = Status::Failed;
        return;
    }
    if(httpStatus != 200) {
        finishWithError("The game service refused the request.");
        return;
    }

    response_ = parsed;
    errorMessage_.clear();
    status_ = Status::Succeeded;
}

void RoomAdmissionClient::begin(const AdmissionRequest& request) {
    cancel();
    response_ = AdmissionResponse();
    errorMessage_.clear();
    listing_ = request.listing;
    operation_ = request.operation;

    std::string error;
    if(!isAcceptableAdmissionBaseUrl(request.baseUrl, request.allowLoopbackPlaintext, error)) {
        finishWithError(error);
        return;
    }
    if(request.runtime != "native" && request.runtime != "browser") {
        finishWithError("The game could not describe itself to the game service.");
        return;
    }
    if(request.operation == AdmissionOperation::Room && !request.hosting && !request.listing) {
        std::string normalized;
        if(!RoomRelay::normalizeRoomCode(request.roomCode, normalized)) {
            finishWithError("That game code is not valid. Codes look like ABCD-EFGH-JKMN.");
            return;
        }
    }
    if(request.operation == AdmissionOperation::Room && request.hosting && !request.listing
       && (request.maxPeers < 2 || request.maxPeers > RoomRelay::Limits::kMaxPeersPerRoom)) {
        finishWithError("That number of players is not supported online.");
        return;
    }

    AdmissionRequest normalizedRequest = request;
    if(request.operation == AdmissionOperation::Room && !request.hosting && !request.listing) {
        RoomRelay::normalizeRoomCode(request.roomCode, normalizedRequest.roomCode);
    }

    impl_ = std::make_unique<Impl>();
    status_ = Status::InProgress;

    impl_->url  = buildEndpointUrl(normalizedRequest);
    impl_->body = buildFormBody(normalizedRequest);
    if(!impl_->http) {
        finishWithError("The game service could not be reached.");
        return;
    }

    BoundedHttpClient::Request httpRequest;
    httpRequest.url  = impl_->url;
    httpRequest.body = impl_->body;
    // An admission answer is the small key=value text in docs/room-relay-protocol.md 3.2, and
    // the parser refuses anything longer. Asking for exactly that means a service cannot make
    // this allocate more by claiming to have more to say - and a refusal still arrives with a
    // readable body, which is where the reason the player sees comes from.
    httpRequest.maxResponseBytes = RoomAdmission::kMaxResponseBytes;
    httpRequest.timeoutSeconds   = kAdmissionTimeoutSeconds;
    impl_->http->begin(httpRequest);
}

void RoomAdmissionClient::update() {
    if(status_ != Status::InProgress || !impl_ || !impl_->http) {
        return;
    }

    impl_->http->update();

    BoundedHttpClient::Result result;
    if(!impl_->http->poll(result)) {
        return;
    }

    if(result.failure != BoundedHttpClient::Failure::None) {
        switch(result.failure) {
            case BoundedHttpClient::Failure::Certificate:
                finishWithError("The game service certificate could not be verified.");
                break;
            case BoundedHttpClient::Failure::Timeout:
                finishWithError("The game service did not answer in time.");
                break;
            case BoundedHttpClient::Failure::NameResolution:
                finishWithError("The game service address could not be found.");
                break;
            case BoundedHttpClient::Failure::TooLarge:
                finishWithError("The game service sent an unusable answer.");
                break;
            default:
                finishWithError("The game service could not be reached.");
                break;
        }
        return;
    }

    if(result.body.empty()) {
        finishWithError("The game service could not be reached.");
        return;
    }
    finishWithBody(result.httpStatus, result.body);
}

// Exercises the real room state machine, HTTP client, native RTC backend and P2PKit framing.
// This is a transport integration fixture, not a game simulation or NAT reachability test.
#include <Network/DirectRoomTransport.h>
#include <SDL.h>
#include <rtc/rtc.hpp>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>
std::string getDuneLegacyDataDir() { return "."; }
int main(int argc, char** argv) {
    if(argc != 3) return 2;
    const int count = std::atoi(argv[1]);
    const unsigned hold = static_cast<unsigned>(std::atoi(argv[2]));
    if(count < 2 || count > 8 || hold > 120000) return 2;
    SDL_Init(SDL_INIT_TIMER);
    if(std::getenv("DUNE_P2P_TEST_DEBUG")) rtc::InitLogger(rtc::LogLevel::Debug);
    DirectRoomTransport::Config config;
    std::getline(std::cin,config.signalingBaseUrl);
    std::getline(std::cin,config.grant);
    std::getline(std::cin,config.roomCode);
    std::getline(std::cin,config.displayName);
    config.appVersion="1.0.662";config.gameProtocolVersion=7;
    config.contentHash=std::string(64,'a');config.runtime="native";
    config.allowLoopbackPlaintext=true;
    DirectRoomTransport transport;
    std::string error;
    if(!transport.start(config,error)){std::cerr<<error<<'\n';return 1;}
    const auto started=SDL_GetTicks();
    unsigned readyAt=0,lastSend=0,sequence=0;
    std::map<unsigned,unsigned> received;
    std::vector<std::uint8_t> payload(262128,0xab);
    while(SDL_GetTicks()-started < hold+45000) {
        transport.update();
        RoomSessionTransport::Event event;
        while(transport.pollEvent(event)) {
            if(event.type==RoomSessionTransport::Event::Type::GamePayload) {
                auto& next=received[event.peerId];
                if(event.payload.size()!=payload.size() || event.payload[0]!=(next&255)
                   || event.payload[1]!=0xab || event.payload.back()!=0xab) {
                    std::cerr<<"FAIL: altered or out-of-order payload\n";return 1;
                }
                ++next;
            } else if(event.type==RoomSessionTransport::Event::Type::Closed) {
                std::cerr<<"FAIL: "<<event.message<<'\n';return 1;
            } else if(event.type==RoomSessionTransport::Event::Type::Diagnostic) {
                if(!event.message.empty())std::cerr<<"DIAGNOSTIC: "<<event.message<<'\n';
            } else if(event.type==RoomSessionTransport::Event::Type::Refused) {
                std::cerr<<"REFUSED: "<<event.message<<'\n';
            }
        }
        if(!readyAt && transport.connectedPeerCount()==static_cast<unsigned>(count-1)
           && transport.meshReady()) {
            readyAt=SDL_GetTicks();
            if(transport.isHost()&&!transport.setRoomPhase(RoomRelay::Phase::Match))return 1;
            else if(!transport.isHost())transport.assumeMatchPhase();
            std::cout<<"READY\n"<<std::flush;
        }
        if(readyAt && SDL_GetTicks()-lastSend>=1000 && sequence<hold/1000+5) {
            payload[0]=sequence&255;
            if(!transport.sendGamePayload(payload.data(),payload.size(),1,0)){
                std::cerr<<"FAIL: send refused\n";return 1;
            }
            ++sequence;lastSend=SDL_GetTicks();
        }
        if(readyAt && SDL_GetTicks()-readyAt>=hold) {
            bool enough=received.size()==static_cast<unsigned>(count-1);
            for(const auto& pair:received) enough=enough && pair.second>=hold/1000-2;
            if(enough){std::cout<<"PASS: direct full mesh, ordered maximum payloads\n"<<std::flush;SDL_Delay(1000);return 0;}
        }
        SDL_Delay(2);
    }
    std::cerr<<"FAIL: connection or traffic timed out: "<<transport.statusMessage()<<'\n';return 1;
}

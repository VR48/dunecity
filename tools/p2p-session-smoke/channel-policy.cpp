// Real SCTP negotiation against the native adapter: reliable/ordered is mandatory.
#include <Network/DirectPeerConnection.h>
#include <Network/P2PWireFraming.h>
#include <rtc/rtc.hpp>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
struct Pending {std::mutex mutex;std::deque<DirectPeerConnection::LocalSignal> signals;};
bool check(int mode) {
    std::string error;
    DirectPeerConnectionOptions options;options.initiator=false;options.iceServers={};
    auto adapter=createDirectPeerConnection(options,error);
    if(!adapter){std::cerr<<error<<'\n';return false;}
    auto pending=std::make_shared<Pending>();
    std::weak_ptr<Pending> weak=pending;
    rtc::Configuration config;config.iceServers={};
    auto raw=std::make_shared<rtc::PeerConnection>(config);
    raw->onLocalDescription([weak](rtc::Description d){
        if(auto state=weak.lock()){std::lock_guard<std::mutex> guard(state->mutex);state->signals.push_back({P2PSignal::SignalKind::Offer,std::string(d)});}
    });
    raw->onLocalCandidate([weak](rtc::Candidate c){
        if(auto state=weak.lock()){std::lock_guard<std::mutex> guard(state->mutex);state->signals.push_back({P2PSignal::SignalKind::Candidate,c.mid()+"|"+c.candidate().substr(c.candidate().compare(0,2,"a=")==0?2:0)});}
    });
    rtc::DataChannelInit init;
    if(mode==1)init.reliability.unordered=true;
    if(mode==2)init.reliability.maxRetransmits=0;
    if(mode==3)init.reliability.maxPacketLifeTime=std::chrono::milliseconds(10);
    auto channel=raw->createDataChannel(mode==4?"wrong":"p2pkit",init);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    bool sawSize=false,passed=false;
    while(std::chrono::steady_clock::now()<deadline) {
        std::deque<DirectPeerConnection::LocalSignal> inbound;
        {std::lock_guard<std::mutex> guard(pending->mutex);inbound.swap(pending->signals);}
        for(const auto& signal:inbound) {
            const bool accepted=signal.kind==P2PSignal::SignalKind::Candidate
                ?adapter->addRemoteCandidate(signal.payload)
                :adapter->setRemoteDescription(signal.kind,signal.payload);
            if(!accepted){adapter->update();std::cerr<<"setup refused kind="<<static_cast<int>(signal.kind)<<" bytes="<<signal.payload.size()<<" candidate-prefix="<<(signal.kind==P2PSignal::SignalKind::Candidate?signal.payload.substr(0,24):std::string())<<" reason="<<adapter->lastError()<<"\n";goto done;}
        }
        DirectPeerConnection::LocalSignal signal;
        while(adapter->pollLocalSignal(signal)) {
            if(signal.kind==P2PSignal::SignalKind::Candidate) {
                auto split=signal.payload.find('|');raw->addRemoteCandidate(rtc::Candidate(signal.payload.substr(split+1),signal.payload.substr(0,split)));
            } else {
                sawSize=signal.payload.find("a=max-message-size:"+std::to_string(P2PWire::Limits::kMaxChannelMessageBytes))!=std::string::npos;
                raw->setRemoteDescription(rtc::Description(signal.payload,"answer"));
            }
        }
        adapter->update();
        if(mode==0 && adapter->state()==DirectPeerConnection::State::Connected){passed=sawSize;break;}
        if(mode!=0 && adapter->state()==DirectPeerConnection::State::Failed){passed=sawSize;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
done:
    adapter->close();channel->close();raw->close();
    std::cout<<(passed?"PASS":"FAIL")<<": channel policy mode "<<mode<<", bounded SCTP size "<<sawSize<<'\n';
    return passed;
}
int main(){for(int mode=0;mode<5;++mode)if(!check(mode))return 1;return 0;}

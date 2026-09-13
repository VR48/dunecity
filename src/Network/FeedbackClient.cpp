#include <Network/FeedbackClient.h>
#include <Network/ENetHttp.h>
#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#include <cstring>
#else
#include <thread>
#endif

namespace FeedbackClient {
namespace {
constexpr const char* endpoint = "https://dunelegacy.com/metaserver/feedback.php";
constexpr const char* unavailable = "ERROR Feedback could not be sent. Your text is still here; try again.";
#ifdef __EMSCRIPTEN__
struct Request {
    std::shared_ptr<Submission> result;
    std::string body;
};
void completed(emscripten_fetch_t* fetch) {
    std::unique_ptr<Request> request(static_cast<Request*>(fetch->userData));
    request->result->response = fetch->status == 200 && fetch->numBytes <= 16384
        ? std::string(fetch->data, fetch->numBytes) : unavailable;
    request->result->done.store(true);
    emscripten_fetch_close(fetch);
}
#endif
}
std::shared_ptr<Submission> submit(const std::map<std::string, std::string>& fields) {
    auto result = std::make_shared<Submission>();
#ifdef __EMSCRIPTEN__
    auto request = std::make_unique<Request>();
    request->result = result;
    for(const auto& field : fields) {
        if(!request->body.empty()) request->body += '&';
        request->body += percentEncode(field.first) + '=' + percentEncode(field.second);
    }
    emscripten_fetch_attr_t attributes;
    emscripten_fetch_attr_init(&attributes);
    std::strcpy(attributes.requestMethod, "POST");
    attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attributes.timeoutMSecs = 20000;
    const char* headers[] = {"Content-Type", "application/x-www-form-urlencoded", nullptr};
    attributes.requestHeaders = headers;
    attributes.requestData = request->body.data();
    attributes.requestDataSize = request->body.size();
    attributes.onsuccess = completed;
    attributes.onerror = completed;
    attributes.userData = request.get();
    if(emscripten_fetch(&attributes, endpoint)) request.release();
    else { result->response = unavailable; result->done.store(true); }
#else
    // Only the shared result crosses threads. Closing the dialog never leaves a UI pointer in flight.
    std::thread([result, fields] {
        try { result->response = postToHttp(endpoint, fields, 20); }
        catch(...) { result->response = unavailable; }
        result->done.store(true);
    }).detach();
#endif
    return result;
}
}

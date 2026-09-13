#ifndef DUNECITY_FEEDBACK_CLIENT_H
#define DUNECITY_FEEDBACK_CLIENT_H
#include <atomic>
#include <memory>
#include <string>
#include <map>

// The service holds the repo-scoped GitHub credential. No token is shipped to players.
namespace FeedbackClient {
struct Submission {
    std::atomic<bool> done{false};
    std::string response;
};
std::shared_ptr<Submission> submit(const std::map<std::string, std::string>& fields);
}
#endif

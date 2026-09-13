#ifndef DUNECITY_FEEDBACK_ISSUE_H
#define DUNECITY_FEEDBACK_ISSUE_H

#include <string>
#include <map>
#include <stdexcept>

namespace FeedbackIssue {
inline bool hasText(const std::string& value) {
    return value.find_first_not_of(" \r\n\t") != std::string::npos;
}

// Only a confirmed issue in this repository can be opened by the client.
inline bool isIssueUrl(const std::string& url) {
    const std::string prefix = "https://github.com/VR48/dunecity/issues/";
    return url.compare(0, prefix.size(), prefix) == 0 && url.size() > prefix.size()
        && url[prefix.size()] != '0'
        && url.find_first_not_of("0123456789", prefix.size()) == std::string::npos;
}
inline std::map<std::string, std::string> fields(const std::string& id, const std::string& title,
        const std::string& details, const std::string& context) {
    if(!hasText(title) || !hasText(details)) throw std::invalid_argument("Enter a summary and some feedback first.");
    if(title.size() > 400 || details.size() > 8000 || context.size() > 8000)
        throw std::invalid_argument("Please shorten the feedback.");
    return {{"request_id", id}, {"title", title}, {"details", details}, {"context", context}};
}
}
#endif

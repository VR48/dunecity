#ifndef DUNECITY_FEEDBACK_ISSUE_H
#define DUNECITY_FEEDBACK_ISSUE_H

#include <string>
#include <stdexcept>

namespace FeedbackIssue {
inline bool hasText(const std::string& value) {
    return value.find_first_not_of(" \r\n\t") != std::string::npos;
}

inline std::string encode(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for(unsigned char c : value) {
        if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
           || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else {
            result += '%'; result += hex[c >> 4]; result += hex[c & 15];
        }
    }
    return result;
}

inline std::string url(const std::string& title, const std::string& details, const std::string& context) {
    if(!hasText(title) || !hasText(details)) throw std::invalid_argument("Enter a summary and some feedback first.");
    const auto result = "https://github.com/VR48/dunecity/issues/new?title=" + encode(title)
        + "&body=" + encode(details + "\n\n---\n" + context);
    if(result.size() > 7500) throw std::invalid_argument("Please shorten the feedback before opening GitHub.");
    return result;
}
}
#endif

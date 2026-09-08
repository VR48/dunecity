#include <players/AIDecisionLog.h>
#include <misc/fnkdat.h>
#include <SDL_log.h>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <limits>

namespace AITelemetry {
std::string Record::quote(const std::string& value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += c; }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
        else result += c;
    }
    return result + "\"";
}
void Record::add(const std::string& key, const std::string& encoded) {
    if (!fields.empty()) fields += ',';
    fields += quote(key) + ':' + encoded;
}
Record& Record::set(const std::string& key, int64_t value) { add(key, std::to_string(value)); return *this; }
Record& Record::set(const std::string& key, const std::string& value) { add(key, quote(value)); return *this; }
Record& Record::set(const std::string& key, const Record& value) { add(key, value.json()); return *this; }

bool DecisionLog::start(const std::string& directory, const Record& metadata, uint64_t byteLimit) {
    stop();
    sequence = bytes = 0;
    lastCycle = 0; economy.clear(); observationCounts.clear();
    limit = byteLimit;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // Exclusive directory creation also separates concurrent game processes.
    for (int suffix = 0; suffix < 100; ++suffix) {
        session = std::to_string(stamp) + "-" + std::to_string(suffix);
        const auto folder = std::filesystem::path(directory) / session;
        if (!std::filesystem::create_directory(folder, ec)) { if (ec) return false; continue; }
        filename = (folder / "events.jsonl").string();
        stream.clear();
        stream.open(filename, std::ios::out | std::ios::binary);
        if (!stream) { stream.close(); return false; }
        lastFlush = std::chrono::steady_clock::now();
        write(0, -1, -1, "session_start", metadata);
        stream.flush();
        return enabled();
    }
    return false;
}
void DecisionLog::stop() {
    if (enabled()) { write(lastCycle, -1, -1, "session_end", Record()); stream.close(); }
}
uint64_t DecisionLog::write(uint32_t cycle, int house, int player,
                          const std::string& event, const Record& details) {
    if (!enabled()) return 0;
    lastCycle = std::max(lastCycle, cycle);
    const bool routine=event=="city_growth_sample" || event=="harvest_rally_move_order";
    if (routine && (++observationCounts[event]-1)%8!=0) return 0;
    const bool terminal=event=="game_summary" || event=="session_end" || event=="simulation_exception";
    if (limit>=1024*1024 && !terminal && bytes>=limit-limit/16) return 0;
    const uint64_t id = ++sequence;
    const auto row = Record().set("schema_version", 1).set("telemetry_version", 10).set("policy_version", "coordinated-army-v38")
        .set("session", session).set("seq", id).set("cycle", cycle)
        .set("house", house).set("player", player).set("event", event).set("data", details).json() + '\n';
    if (bytes + row.size() > limit) {
        // Explicit terminal marker; a few hundred bytes beyond the configured cap.
        stream << Record().set("schema_version", 1).set("telemetry_version", 10).set("policy_version", "coordinated-army-v38").set("session", session).set("seq", id)
            .set("cycle", cycle).set("house", -1).set("player", -1)
            .set("event", "capture_limit").set("data", Record().set("byte_limit", limit)).json() << '\n';
        stream.close();
        SDL_Log("AI telemetry reached its session byte limit: %s", filename.c_str());
        return 0;
    }
    stream << row;
    bytes += row.size();
    const auto now = std::chrono::steady_clock::now();
    if (id % 128 == 0 || now - lastFlush >= std::chrono::seconds(2)) {
        stream.flush(); lastFlush = now;
    }
    if (!stream) {
        stream.close();
        SDL_Log("AI telemetry disabled after write failure: %s", filename.c_str());
        return 0;
    }
    return id;
}
void DecisionLog::account(int house, const std::string& category, int64_t rawCredits) {
    if (!enabled()) return;
    auto& total = economy[house][category];
    if (rawCredits > 0 && total > std::numeric_limits<int64_t>::max() - rawCredits)
        total = std::numeric_limits<int64_t>::max();
    else if (rawCredits < 0 && total < std::numeric_limits<int64_t>::min() - rawCredits)
        total = std::numeric_limits<int64_t>::min();
    else total += rawCredits;
}
Record DecisionLog::economyTotals(int house) const {
    Record result;
    const auto found = economy.find(house);
    if (found != economy.end()) for (const auto& entry : found->second)
        result.set(entry.first, entry.second / (int64_t{1} << 32));
    return result;
}
DecisionLog& log() { static DecisionLog instance; return instance; }
void startGame(const Record& metadata) {
    log().stop();
    const char* enabled = std::getenv("DUNECITY_AI_TELEMETRY");
    if (enabled && std::string(enabled) == "0") return;
    char root[FILENAME_MAX];
    if (fnkdat("ai-decisions/", root, sizeof(root), FNKDAT_USER | FNKDAT_CREAT) < 0
        || !log().start(root, metadata)) {
        SDL_Log("AI telemetry unavailable; game continues without structured capture");
    } else SDL_Log("AI telemetry: %s", log().path().c_str());
}
} // namespace AITelemetry

#ifndef AI_DECISION_LOG_H
#define AI_DECISION_LOG_H

#include <cstdint>
#include <fstream>
#include <string>
#include <chrono>
#include <map>

namespace AITelemetry {

// Small typed JSON builder: callers cannot insert unescaped text as JSON.
class Record {
public:
    Record& set(const std::string& key, int64_t value);
    Record& set(const std::string& key, const std::string& value);
    Record& set(const std::string& key, const Record& value);
    std::string json() const { return "{" + fields + "}"; }
    static std::string quote(const std::string& value);
private:
    void add(const std::string& key, const std::string& encoded);
    std::string fields;
};

// One local stream per game/load. Never reads or changes simulation RNG/state.
// Single game-thread writer; buffered, bounded and optional. SQLite is offline.
class DecisionLog {
public:
    bool start(const std::string& directory, const Record& metadata,
               uint64_t byteLimit = 256 * 1024 * 1024);
    void stop();
    bool enabled() const { return stream.is_open(); }
    uint64_t write(uint32_t cycle, int house, int player, const std::string& event,
                   const Record& details);
    // Accumulate Q32 credit amounts without dropping fractional payouts.
    void account(int house, const std::string& category, int64_t rawCredits);
    Record economyTotals(int house) const;
    const std::string& path() const { return filename; }
private:
    std::ofstream stream;
    std::string session, filename;
    uint64_t sequence = 0, bytes = 0, limit = 0;
    uint32_t lastCycle = 0;
    std::map<std::string, uint64_t> observationCounts;
    std::map<int, std::map<std::string, int64_t>> economy;
    std::chrono::steady_clock::time_point lastFlush;
};

DecisionLog& log();
void startGame(const Record& metadata);
} // namespace AITelemetry
#endif

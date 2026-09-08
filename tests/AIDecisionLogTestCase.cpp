#include <catch2/catch_test_macros.hpp>
#include <players/AIDecisionLog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>

TEST_CASE("AI telemetry escapes text and preserves numeric/nested fields", "[ai][telemetry]") {
    const auto row = AITelemetry::Record().set("reason", "a\"b\n\\c\t")
        .set("demand", -300).set("state", AITelemetry::Record().set("credits", 17922)).json();
    REQUIRE(row == "{\"reason\":\"a\\\"b\\u000a\\\\c\\u0009\",\"demand\":-300,\"state\":{\"credits\":17922}}");
}

TEST_CASE("AI telemetry separates sessions and ends bounded capture explicitly", "[ai][telemetry]") {
    const auto root = std::filesystem::temp_directory_path() / ("dunecity-telemetry-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(), AITelemetry::Record().set("test", 1), 1200));
    const auto first = writer.path();
    REQUIRE(writer.write(200, 6, 2, "decision", AITelemetry::Record().set("item", 20)) == 2);
    for (int i = 0; i < 20; ++i) writer.write(201 + i, 6, 2, "decision", AITelemetry::Record());
    REQUIRE_FALSE(writer.enabled());
    std::ifstream input(first);
    std::stringstream contents; contents << input.rdbuf();
    REQUIRE(contents.str().find("\"event\":\"capture_limit\"") != std::string::npos);
    REQUIRE(contents.str().find("\"cycle\":200") != std::string::npos);
    REQUIRE(writer.start(root.string(), AITelemetry::Record()));
    REQUIRE(writer.path() != first);
    writer.stop();
    REQUIRE(std::filesystem::exists(first));
    if (const char* artifact = std::getenv("DUNECITY_AI_TEST_EXPORT"))
        std::filesystem::copy_file(first, artifact, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove_all(root);
}

TEST_CASE("AI telemetry retains fractional income and records the real closing cycle", "[ai][telemetry]") {
    const auto root = std::filesystem::temp_directory_path() / ("dunecity-ledger-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(), AITelemetry::Record()));
    const auto file = writer.path();
    for (int n = 0; n < 8; ++n) writer.account(3, "city_gross", int64_t{1} << 30);
    REQUIRE(writer.economyTotals(3).json() == "{\"city_gross\":2}");
    REQUIRE(writer.economyTotals(7).json() == "{}");
    writer.write(746182, 3, 50, "state_snapshot", AITelemetry::Record());
    writer.stop();
    std::ifstream input(file);
    std::string line, last;
    while (std::getline(input, line)) last = line;
    REQUIRE(last.find("\"cycle\":746182") != std::string::npos);
    REQUIRE(last.find("\"event\":\"session_end\"") != std::string::npos);
    REQUIRE(writer.start(root.string(), AITelemetry::Record()));
    REQUIRE(writer.economyTotals(3).json() == "{}");
    writer.stop();
    std::filesystem::remove_all(root);
}

TEST_CASE("Telemetry thins repeated growth observations without dropping actual changes", "[ai][telemetry]") {
    const auto root=std::filesystem::temp_directory_path()/("dunecity-thinning-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AITelemetry::DecisionLog writer;
    REQUIRE(writer.start(root.string(),AITelemetry::Record(),2*1024*1024));
    int samples=0,changes=0;
    for (int i=0;i<16;++i) {
        samples+=writer.write(i,0,0,"city_growth_sample",AITelemetry::Record())!=0;
        changes+=writer.write(i,0,0,"city_level_changed",AITelemetry::Record())!=0;
    }
    REQUIRE(samples==2);
    REQUIRE(changes==16);
    const auto path=writer.path();
    const std::string payload(10000,'x');
    for (int i=0;i<250;++i) writer.write(20+i,0,0,"decision",AITelemetry::Record().set("payload",payload));
    REQUIRE(writer.enabled()); // capture space retained for the outcome
    REQUIRE(writer.write(300,-1,-1,"game_summary",AITelemetry::Record().set("ended",1))!=0);
    writer.stop();
    std::ifstream input(path); std::stringstream contents; contents<<input.rdbuf();
    REQUIRE(contents.str().find("\"event\":\"game_summary\"")!=std::string::npos);
    REQUIRE(contents.str().find("\"event\":\"session_end\"")!=std::string::npos);
    std::filesystem::remove_all(root);
}

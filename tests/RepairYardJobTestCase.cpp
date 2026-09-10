#include <catch2/catch_test_macros.hpp>
#include <structures/RepairYardJob.h>

TEST_CASE("Repair yard releases a disappeared occupant once and preserves other bookings", "[repair][regression]") {
    bool repairing = true;
    Uint32 bookings = 3;
    int unit = 1;
    int* storedUnit = &unit;
    int releases = 0;
    auto resolve = [&] {
        return RepairYardJob::resolve(repairing, [&] { return storedUnit; }, [&] {
            ++releases;
            RepairYardJob::finish(repairing, bookings);
        });
    };
    REQUIRE(resolve() == &unit);
    REQUIRE(bookings == 3);
    storedUnit = nullptr; // Object manager removed the unit after lethal damage.
    REQUIRE(resolve() == nullptr);
    REQUIRE_FALSE(repairing);
    REQUIRE(bookings == 2);
    REQUIRE(releases == 1);
    REQUIRE(resolve() == nullptr); // Next update / late carryall pickup / destructor.
    RepairYardJob::finish(repairing, bookings);
    REQUIRE(bookings == 2);
    REQUIRE(releases == 1);
    storedUnit = &unit;
    repairing = true; // Another booked unit can now enter and complete repair.
    REQUIRE(resolve() == &unit);
    RepairYardJob::finish(repairing, bookings);
    REQUIRE(bookings == 1);
}

TEST_CASE("Repair yard cancellation never underflows an empty booking count", "[repair][regression]") {
    bool repairing = true;
    Uint32 bookings = 0; // Stale save or booking already released by another path.
    RepairYardJob::finish(repairing, bookings);
    REQUIRE(bookings == 0);
    RepairYardJob::finish(repairing, bookings);
    REQUIRE(bookings == 0);
    REQUIRE_FALSE(repairing);
}

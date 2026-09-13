cmake_minimum_required(VERSION 3.10)

# Find Catch2 package
find_package(Catch2 3 REQUIRED)

# Test executable
add_executable(dunelegacy_tests)

# Source files - new Catch2-based tests
# Note: Catch2WithMain provides main(), no testmain.cpp needed
target_sources(dunelegacy_tests
    PRIVATE
        CoopGameSettingsTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/GameInitSettings.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/IFileStream.cpp
        CityTrafficPolicyTestCase.cpp
        CitySpritePolicyTestCase.cpp
        RendererSizeTestCase.cpp
        QuantBotBuildPolicyTestCase.cpp
        AIDecisionLogTestCase.cpp
        CityPlacementPolicyTestCase.cpp
        GroundAccessPolicyTestCase.cpp
        NuclearBlastPolicyTestCase.cpp
        RepairYardJobTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/players/AIDecisionLog.cpp
        PathBudgetTestCase/PathBudgetTestCase.cpp
        NetworkManagerTestCase/NetworkManagerTestCase.cpp
        NetworkHardeningTestCase/NetworkHardeningTestCase.cpp
        RelayProtocolTestCase/RelayProtocolTestCase.cpp
        # Direct peer-to-peer play. The framing and protocol headers are header-only so these
        # drive the real encoder and decoder; the transport test drives the real state machine
        # with a scripted signaling service and fake data channels.
        P2PWireFramingTestCase/P2PWireFramingTestCase.cpp
        P2PSignalingProtocolTestCase/P2PSignalingProtocolTestCase.cpp
        DirectRoomTransportTestCase/DirectRoomTransportTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/DirectRoomTransport.cpp
        RelayProtocolTestCase/ContentCompatibilityTestCase.cpp
        CommandEmissionTestCase/CommandEmissionTestCase.cpp
        CommandBufferPolicyTestCase/CommandBufferPolicyTestCase.cpp
        GenericNinthHouseRegressionTestCase/GenericNinthHouseRegressionTestCase.cpp
        ZoneStructureTestCase/ZoneStructureTestCase.cpp
        RoadPlacementTestCase/RoadPlacementTestCase.cpp
        CityBudgetIntegrationTestCase/CityBudgetIntegrationTestCase.cpp
        BudgetAllocationTestCase/BudgetAllocationTestCase.cpp
        CityBuildLogicTestCase/CityBuildLogicTestCase.cpp
        OverlayLegendTestCase/OverlayLegendTestCase.cpp
        CityRoadTestCase/CityRoadTestCase.cpp
        RoadNetworkGrowthTestCase/RoadNetworkGrowthTestCase.cpp
        MilestoneTestCase/MilestoneTestCase.cpp
        CityStructuresTestCase/CityStructuresTestCase.cpp
        ConstructionYardResolverTestCase/ConstructionYardResolverTestCase.cpp
        UnitMovementPolicyTestCase/UnitMovementPolicyTestCase.cpp
        PalaceUnitLimitRegressionTestCase/PalaceUnitLimitRegressionTestCase.cpp
        ZonePowerTestCase/ZonePowerTestCase.cpp
        CityEffectsTestCase/CityEffectsTestCase.cpp
        ../src/fixmath/fix32.c
        ValueModelTestCase/ValueModelTestCase.cpp
        PoliceStationTestCase/PoliceStationTestCase.cpp
        SaveCompatTestCase/SaveCompatTestCase.cpp
        NeutralHouseTestCase/NeutralHouseTestCase.cpp
        Dune2RAssetManagerTestCase/Dune2RAssetManagerTestCase.cpp
        EnhancedAtlasCacheTestCase/EnhancedAtlasCacheTestCase.cpp
        EnhancedBuildingGeometryTestCase/EnhancedBuildingGeometryTestCase.cpp
        MenuLayoutTestCase/MenuLayoutTestCase.cpp
        ${CMAKE_SOURCE_DIR}/src/mmath.cpp
        ${CMAKE_SOURCE_DIR}/src/FileClasses/EnhancedAtlasCache.cpp
        ${CMAKE_SOURCE_DIR}/src/FileClasses/lodepng.cpp
        # DuneCity core sources (needed for CitySimulation tests)
        ${CMAKE_SOURCE_DIR}/src/dunecity/CitySimulation.cpp
        CityEffectsRuntime_test_stub.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/PowerGrid.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/ZoneSimulation.cpp
        # TrafficSimulation.cpp excluded: it pulls in Tile/Map/StructureBase
        # and is only used by CityEffectsRuntime (which is stubbed in tests).
        ${CMAKE_SOURCE_DIR}/src/dunecity/CityBudget.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/CityScanner.cpp
        ${CMAKE_SOURCE_DIR}/src/dunecity/CityEvaluation.cpp
        ${CMAKE_SOURCE_DIR}/src/audio/sounds.cpp
        ${CMAKE_SOURCE_DIR}/src/mod/Dune2RAssetManager.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/ENetHttp.cpp
        ${CMAKE_SOURCE_DIR}/src/FileClasses/INIFile.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/FileSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/fnkdat.cpp
        globals_test_stub.cpp
        TextManager_stub.cpp
        # ENet library sources (needed for NetworkManager tests)
        ${CMAKE_SOURCE_DIR}/src/enet/callbacks.c
        ${CMAKE_SOURCE_DIR}/src/enet/compress.c
        ${CMAKE_SOURCE_DIR}/src/enet/host.c
        ${CMAKE_SOURCE_DIR}/src/enet/list.c
        ${CMAKE_SOURCE_DIR}/src/enet/packet.c
        ${CMAKE_SOURCE_DIR}/src/enet/peer.c
        ${CMAKE_SOURCE_DIR}/src/enet/protocol.c
        $<$<NOT:$<PLATFORM_ID:Windows>>:${CMAKE_SOURCE_DIR}/src/enet/unix.c>
        $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/enet/win32.c>
        # fmt library (needed for THROW macro in ENet headers)
        ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
)

# Include directories
target_include_directories(dunelegacy_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src/enet  # For enet headers
        ${CMAKE_SOURCE_DIR}/src      # For audio/sounds.h and other src-level headers
)

# Link libraries
target_link_libraries(dunelegacy_tests
    PRIVATE
        Catch2::Catch2WithMain
        SDL2::SDL2
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
        CURL::libcurl
)

if(APPLE)
    # fnkdat.cpp calls getMacApplicationSupportFolder(), which is implemented in
    # the Objective-C helper the game target also compiles (src/CMakeLists.txt).
    target_sources(dunelegacy_tests PRIVATE ${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(dunelegacy_tests
        PRIVATE
            "-framework Cocoa"
            "-framework Foundation"
            "-framework CoreFoundation"
    )
endif()

if(WIN32)
    target_link_libraries(dunelegacy_tests
        PRIVATE
            ws2_32
            winmm
            imm32
            version
    )
endif()

# Compiler options
target_compile_options(dunelegacy_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

# Add test
add_test(NAME dunelegacy_tests COMMAND dunelegacy_tests)
add_test(NAME lobby_authorization_tests COMMAND dunelegacy_tests "[lobby][security][authorization]")

# Standalone network wire harness.
#
# Same sources as tests/wasm/run-network-wire-harness.sh, built for the host so the wire
# boundary can be checked without the rest of the test target (and, with the script, under
# Emscripten where size_t is 32 bits). It needs no game data and no Catch2.
add_executable(network_wire_harness
    wasm/NetworkWireHarness.cpp
    ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
    ${CMAKE_SOURCE_DIR}/src/enet/callbacks.c
    ${CMAKE_SOURCE_DIR}/src/enet/compress.c
    ${CMAKE_SOURCE_DIR}/src/enet/host.c
    ${CMAKE_SOURCE_DIR}/src/enet/list.c
    ${CMAKE_SOURCE_DIR}/src/enet/packet.c
    ${CMAKE_SOURCE_DIR}/src/enet/peer.c
    ${CMAKE_SOURCE_DIR}/src/enet/protocol.c
    $<$<NOT:$<PLATFORM_ID:Windows>>:${CMAKE_SOURCE_DIR}/src/enet/unix.c>
    $<$<PLATFORM_ID:Windows>:${CMAKE_SOURCE_DIR}/src/enet/win32.c>
)

target_include_directories(network_wire_harness
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/enet
)

target_link_libraries(network_wire_harness
    PRIVATE
        SDL2::SDL2
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
)

if(WIN32)
    target_link_libraries(network_wire_harness PRIVATE ws2_32 winmm)
endif()

add_test(NAME network_wire_harness COMMAND network_wire_harness)

# Standalone relay wire harness.
#
# The crossplay equivalent of the above, and the reason it is separate: the relay protocol, the
# endpoint validation, the admission parser, the HTTPS polling batch format and the state digest
# have no SDL, ENet or game dependencies at all. tests/wasm/run-relay-wire-harness.sh builds the
# same sources under Emscripten, which is the run that matters because the browser client is
# wasm32 and an additive length check wraps there.
#
# RelayHttpTransport.cpp is compiled in for the same reason: it is the polling state machine, it
# is deliberately free of SDL, libcurl and Emscripten, and its own length checks have to hold on
# a 32-bit size_t as well.
add_executable(relay_wire_harness
    wasm/RelayWireHarness.cpp
    ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransport.cpp
)

target_include_directories(relay_wire_harness
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
)

add_test(NAME relay_wire_harness COMMAND relay_wire_harness)

# Relay session tests.
#
# A separate executable rather than another file in dunelegacy_tests, because it compiles the
# production RoomRelayClient.cpp and needs the transport to be a scripted one. RoomRelayClient
# reaches its socket through two free functions; this target supplies its own definitions of them
# instead of linking RelayWebSocketCurl.cpp, so the code under test is exactly what ships and the
# production build carries no test hook for it.
add_executable(relay_session_tests
    RelaySessionTestCase/RelaySessionTestCase.cpp
    RelaySessionTestCase/MockRelayWebSocket.cpp
    ${CMAKE_SOURCE_DIR}/src/Network/RoomRelayClient.cpp
    # misc/SDL2pp.h reaches misc/exceptions.h, whose THROW macro is built on fmt.
    ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
)

target_include_directories(relay_session_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/RelaySessionTestCase
)

target_link_libraries(relay_session_tests
    PRIVATE
        Catch2::Catch2WithMain
        SDL2::SDL2
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
)

target_compile_options(relay_session_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

if(WIN32)
    target_link_libraries(relay_session_tests PRIVATE ws2_32 winmm version)
endif()

add_test(NAME relay_session_tests COMMAND relay_session_tests)

# Relay HTTPS polling transport tests.
#
# Separate from dunelegacy_tests for the same reason relay_session_tests is: it compiles a
# production source file - the polling state machine - and supplies its collaborator itself. The
# transport reaches HTTP and the clock through RelayHttpBackend, so the test target provides a
# scripted backend with a virtual clock and asserts on the exact bytes the transport sent. No
# hook exists in the production build for any of it.
add_executable(relay_http_transport_tests
    RelayHttpTransportTestCase/RelayHttpTransportTestCase.cpp
    ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransport.cpp
)

target_include_directories(relay_http_transport_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/RelayHttpTransportTestCase
)

target_link_libraries(relay_http_transport_tests
    PRIVATE
        Catch2::Catch2WithMain
)

target_compile_options(relay_http_transport_tests
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

add_test(NAME relay_http_transport_tests COMMAND relay_http_transport_tests)

# Relay transport harness.
#
# Not a ctest: it needs a running relay, so it is driven by hand or by
# tests/relay/run-relay-transport-harness.sh. It opens a real connection through the real
# transport, which is the only way to show that the native side actually works.
if(NOT EMSCRIPTEN)
    add_executable(relay_transport_harness
        relay/RelayTransportHarness.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RoomRelayClient.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RoomAdmissionClient.cpp
        # Admission and direct-play signalling share one bounded HTTP client.
        ${CMAKE_SOURCE_DIR}/src/Network/BoundedHttpClient.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RelayWebSocketCurl.cpp
        # Both transports, because createRelayWebSocket() now dispatches on the endpoint scheme
        # and the harness has to be able to follow the relay to whichever it hands out.
        ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransport.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/RelayHttpTransportCurl.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/FileSystem.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/fnkdat.cpp
        ${CMAKE_SOURCE_DIR}/src/misc/format.cpp
    )

    target_include_directories(relay_transport_harness
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_BINARY_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(relay_transport_harness
        PRIVATE
            SDL2::SDL2
            $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
            CURL::libcurl
    )

    if(APPLE)
        target_sources(relay_transport_harness
            PRIVATE ${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(relay_transport_harness
            PRIVATE
                "-framework Cocoa"
                "-framework Foundation"
                "-framework CoreFoundation"
        )
    endif()

    if(WIN32)
        target_link_libraries(relay_transport_harness PRIVATE ws2_32 winmm version)
    endif()
endif()

# Set test properties
set_tests_properties(dunelegacy_tests lobby_authorization_tests PROPERTIES
    ENVIRONMENT "DUNECITY_DATADIR=${CMAKE_SOURCE_DIR}/data;DUNE_CITY_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
)

# Copy test data to build directory
add_custom_command(
    TARGET dunelegacy_tests
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data"
        "${CMAKE_BINARY_DIR}/tests/data"
    COMMENT "Copying test data to build directory"
)

# The interoperability harness is a separate executable on purpose: it needs the real WebRTC
# backend, which the unit tests deliberately do not link. See tools/p2p-interop/README.md.
if(DUNECITY_DIRECT_P2P AND NOT EMSCRIPTEN)
    add_executable(dunecity_p2p_interop
        ${CMAKE_SOURCE_DIR}/tools/p2p-interop/native-peer.cpp
        ${CMAKE_SOURCE_DIR}/src/Network/DirectPeerConnectionLibdatachannel.cpp)
    target_include_directories(dunecity_p2p_interop PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(dunecity_p2p_interop PRIVATE DUNECITY_HAVE_LIBDATACHANNEL)
    target_link_libraries(dunecity_p2p_interop PRIVATE ${DUNECITY_LIBDATACHANNEL_TARGET})
    target_compile_features(dunecity_p2p_interop PRIVATE cxx_std_17)
endif()

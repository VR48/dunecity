# Set DUNELEGACY_DATADIR only for Linux FHS installs (with trailing slash so
# callers that concatenate subdirs without a separator still work, e.g.
# GFXManager's imported_sprites paths). Windows and macOS leave it unset so
# getDuneLegacyDataDir() falls through to SDL_GetBasePath() at runtime,
# making the install relocatable.
if(NOT WIN32 AND NOT APPLE)
    set(DUNELEGACY_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/")
endif()

# Configure config.h (must come after DUNELEGACY_DATADIR is conditionally set)
configure_file(
    ${CMAKE_SOURCE_DIR}/include/config.h.in
    ${CMAKE_BINARY_DIR}/include/config.h
)

# Main executable
if(APPLE)
    # Create macOS application bundle
    add_executable(dunecity MACOSX_BUNDLE)
    
    # Set bundle properties
    set_target_properties(dunecity PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Dune City"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "dunecity.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.dunecity.DuneCity"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/IDE/xCode/Info.plist"
        MACOSX_BUNDLE_COPYRIGHT "© Dune City Team"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "net.dunecity.DuneCity"
    )
    
    # Add icon to bundle
    set_source_files_properties(${CMAKE_SOURCE_DIR}/dunecity.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(dunecity PRIVATE ${CMAKE_SOURCE_DIR}/dunecity.icns)
elseif(ANDROID)
    add_library(dunecity SHARED)
    set_target_properties(dunecity PROPERTIES OUTPUT_NAME main)
else()
    add_executable(dunecity)
endif()

# Enable Win32 subsystem on Windows
if(WIN32)
    if(MSVC)
        # Add Windows resource file with icon for MSVC
        target_sources(dunecity PRIVATE "${CMAKE_SOURCE_DIR}/resource.rc")
        # Set application icon properties for MSVC
        set_target_properties(dunecity PROPERTIES
            VS_DPI_AWARE "PerMonitor"
            WIN32_EXECUTABLE TRUE)
        
        target_link_options(dunecity PRIVATE
            $<$<CONFIG:Release>:/SUBSYSTEM:WINDOWS>
            $<$<CONFIG:Debug>:/SUBSYSTEM:CONSOLE>
        )
    else()
        # For MinGW, add the resource file with the icon
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/dunecity.rc"
            "${CMAKE_CURRENT_BINARY_DIR}/dunecity.rc"
            COPYONLY
        )
        target_sources(dunecity PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/dunecity.rc")
        
        # MinGW equivalent for Windows subsystem
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            target_link_options(dunecity PRIVATE -mwindows)
        else()
            target_link_options(dunecity PRIVATE -mconsole)
        endif()
    endif()
    
    # When using vcpkg, CMake will automatically handle DLL copying
    # Add a post-build message for confirmation
    add_custom_command(TARGET dunecity POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "vcpkg will automatically copy required DLLs to output directory"
        COMMENT "Checking for SDL2 DLLs..."
    )
endif()

# Precompiled headers
if(DUNECITY_ENABLE_PCH AND WIN32 AND MSVC)
    target_precompile_headers(dunecity
        PRIVATE
            ${PCH_HEADER}
    )
    target_sources(dunecity
        PRIVATE
            ${PCH_SOURCE}
    )
endif()

# All source files
target_sources(dunecity
    PRIVATE
        # Root sources
        AStarSearch.cpp
        Bullet.cpp
        Choam.cpp
        Command.cpp
        CommandManager.cpp
        CityScore.cpp
        CrashHandler.cpp
        CursorManager.cpp
        Explosion.cpp
        Game.cpp
        GameInitSettings.cpp
        GameInterface.cpp
        House.cpp
        Map.cpp
        SpatialGrid.cpp
        MapSeed.cpp
        globals.cpp
        main.cpp
        mmath.cpp
        ObjectBase.cpp
        ObjectData.cpp
        ObjectManager.cpp
        ObjectPointer.cpp
        RadarView.cpp
        ScreenBorder.cpp
        sand.cpp
        SoundPlayer.cpp
        Tile.cpp

        # Audio
        audio/sounds.cpp

        # INIMap
        INIMap/INIMapLoader.cpp
        INIMap/INIMapEditorLoader.cpp
        INIMap/INIMapPreviewCreator.cpp

        # FileClasses
        FileClasses/Animation.cpp
        FileClasses/Cpsfile.cpp
        FileClasses/Decode.cpp
        FileClasses/FileManager.cpp
        FileClasses/FontManager.cpp
        FileClasses/GFXManager.cpp
        FileClasses/Icnfile.cpp
        FileClasses/IndexedTextFile.cpp
        FileClasses/INIFile.cpp
        FileClasses/LoadSavePNG.cpp
        FileClasses/lodepng.cpp
        FileClasses/MentatTextFile.cpp
        FileClasses/Pakfile.cpp
        FileClasses/Palfile.cpp
        FileClasses/PictureFactory.cpp
        FileClasses/POFile.cpp
        FileClasses/SFXManager.cpp
        FileClasses/Shpfile.cpp
        FileClasses/TextManager.cpp
        FileClasses/TTFFont.cpp
        FileClasses/Vocfile.cpp
        FileClasses/Wsafile.cpp
        FileClasses/adl/sound_adlib.cpp
        FileClasses/adl/surroundopl.cpp
        FileClasses/adl/woodyopl.cpp
        FileClasses/music/ADLPlayer.cpp
        FileClasses/music/DirectoryPlayer.cpp
        FileClasses/music/XMIPlayer.cpp
        FileClasses/xmidi/xmidi.cpp

        # GUI
        GUI/Button.cpp
        GUI/GUIStyle.cpp
        GUI/Widget.cpp
        GUI/Window.cpp
        GUI/ScrollBar.cpp
        GUI/ListBox.cpp
        GUI/DropDownBox.cpp
        GUI/TextView.cpp
        GUI/RadioButtonManager.cpp
        GUI/dune/ChatManager.cpp
        GUI/dune/CityBudgetWindow.cpp
        GUI/dune/DuneStyle.cpp
        GUI/dune/GameOptionsWindow.cpp
        GUI/dune/LoadSaveWindow.cpp
        GUI/dune/InGameMenu.cpp
        GUI/dune/InGameSettingsMenu.cpp
        GUI/dune/NewsTicker.cpp
        GUI/dune/MessageTicker.cpp
        GUI/dune/BuilderList.cpp
        GUI/dune/DisasterNotification.cpp
        GUI/dune/WaitingForOtherPlayers.cpp

        # Menu
        Menu/MenuBase.cpp
        Menu/MainMenu.cpp
        Menu/SinglePlayerMenu.cpp
        Menu/SinglePlayerSkirmishMenu.cpp
        Menu/CustomGameMenu.cpp
        Menu/CustomGamePlayers.cpp
        Menu/CustomGameStatsMenu.cpp
        Menu/OptionsMenu.cpp
        Menu/AboutMenu.cpp
        Menu/HouseChoiceMenu.cpp
        Menu/MentatMenu.cpp
        Menu/MultiPlayerMenu.cpp
        Menu/BriefingMenu.cpp
        Menu/HouseChoiceInfoMenu.cpp
        Menu/MentatHelp.cpp
        Menu/MapChoice.cpp
        Menu/CampaignStatsMenu.cpp
        Menu/ModMenu.cpp
        Menu/ModEditorMenu.cpp
        Menu/HowToPlayMenu.cpp

        # Network
        Network/LANGameFinderAndAnnouncer.cpp
        Network/NetworkManager.cpp
        Network/ENetHttp.cpp
        Network/MetaServerClient.cpp
        Network/VersionChecker.cpp
        Network/UPnPManager.cpp
        Network/StunClient.cpp

        # Players
        players/Player.cpp
        players/HumanPlayer.cpp
        players/PlayerFactory.cpp
        players/AIPlayer.cpp
        players/CampaignAIPlayer.cpp
        players/QuantBot.cpp
        players/Mentat.cpp
        players/MentatBuildOrderMentat.cpp
        players/QuantBotConfig.cpp
        players/SmartBot.cpp

        # Mod System
        mod/ModManager.cpp

        # Structures
        structures/StructureBase.cpp
        structures/BuilderBase.cpp
        structures/TurretBase.cpp
        structures/Barracks.cpp
        structures/ConstructionYard.cpp
        structures/GunTurret.cpp
        structures/HeavyFactory.cpp
        structures/HighTechFactory.cpp
        structures/IX.cpp
        structures/LightFactory.cpp
        structures/Palace.cpp
        structures/Radar.cpp
        structures/Refinery.cpp
        structures/RepairYard.cpp
        structures/RocketTurret.cpp
        structures/Silo.cpp
        structures/StarPort.cpp
        structures/Wall.cpp
        structures/WindTrap.cpp
        structures/AdvancedWindTrap.cpp
        structures/Worfinery.cpp
        structures/TechCenter.cpp
        structures/Scoutpost.cpp
        structures/WOR.cpp
        structures/NuclearPlant.cpp
        structures/PoliceStation.cpp
        structures/Stadium.cpp
        structures/Airport.cpp
        structures/ZoneStructure.cpp

        # Units
        units/UnitBase.cpp
        units/AirUnit.cpp
        units/GroundUnit.cpp
        units/InfantryBase.cpp
        units/TrackedUnit.cpp
        units/TankBase.cpp
        units/Carryall.cpp
        units/Devastator.cpp
        units/Deviator.cpp
        units/Frigate.cpp
        units/Harvester.cpp
        units/RebelHarvester.cpp
        units/Launcher.cpp
        units/MCV.cpp
        units/Ornithopter.cpp
        units/Quad.cpp
        units/RaiderTrike.cpp
        units/Saboteur.cpp
        units/SandWorm.cpp
        units/SiegeTank.cpp
        units/Soldier.cpp
        units/SonicTank.cpp
        units/Tank.cpp
        units/Trike.cpp
        units/Trooper.cpp
        units/AmbientAirplane.cpp
        units/AmbientHelicopter.cpp
        units/RocketTrike.cpp
        units/SonicTrike.cpp
        units/FlameTank.cpp
        units/EliteLauncher.cpp
        units/EliteSiegeTank.cpp

        # CutScenes
        CutScenes/CutScene.cpp
        CutScenes/Scene.cpp
        CutScenes/Intro.cpp
        CutScenes/Meanwhile.cpp
        CutScenes/Finale.cpp
        CutScenes/VideoEvent.cpp
        CutScenes/WSAVideoEvent.cpp
        CutScenes/FadeOutVideoEvent.cpp
        CutScenes/FadeInVideoEvent.cpp
        CutScenes/HoldPictureVideoEvent.cpp
        CutScenes/CrossBlendVideoEvent.cpp
        CutScenes/TextEvent.cpp
        CutScenes/CutSceneTrigger.cpp

        # MapEditor
        MapEditor/ChoamWindow.cpp
        MapEditor/MapEditor.cpp
        MapEditor/MapEditorInterface.cpp
        MapEditor/MapEditorOperation.cpp
        MapEditor/MapGenerator.cpp
        MapEditor/MapEditorRadarView.cpp
        MapEditor/MapSettingsWindow.cpp
        MapEditor/MapMirror.cpp
        MapEditor/NewMapWindow.cpp
        MapEditor/LoadMapWindow.cpp
        MapEditor/PlayerSettingsWindow.cpp
        MapEditor/ReinforcementsWindow.cpp
        MapEditor/TeamsWindow.cpp

        # Trigger
        Trigger/TriggerManager.cpp
        Trigger/ReinforcementTrigger.cpp
        Trigger/TimeoutTrigger.cpp

        # DuneCity - city-building simulation layer
        dunecity/CitySimulation.cpp
        dunecity/CityEffectsRuntime.cpp
        dunecity/PowerGrid.cpp
        dunecity/ZoneSimulation.cpp
        dunecity/TrafficSimulation.cpp
        dunecity/CityScanner.cpp
        dunecity/CityBudget.cpp
        dunecity/CityEvaluation.cpp

        # misc
        misc/draw_util.cpp
        misc/FileSystem.cpp
        misc/fnkdat.cpp
        misc/format.cpp
        misc/IFileStream.cpp
        misc/md5.cpp
        misc/OFileStream.cpp

        misc/sound_util.cpp
        misc/string_util.cpp
        misc/Scaler.cpp
        misc/TouchInput.cpp
)

# C source files
target_sources(dunecity
    PRIVATE
        # enet
        enet/callbacks.c
        enet/compress.c
        enet/host.c
        enet/list.c
        enet/packet.c
        enet/peer.c
        enet/protocol.c
        enet/unix.c
        enet/win32.c

        # fixmath
        fixmath/fix16.c
        fixmath/fix16_exp.c
        fixmath/fix16_sqrt.c
        fixmath/fix16_str.c
        fixmath/fix16_trig.c
        fixmath/fix32.c
        fixmath/fix32_exp.c
        fixmath/fix32_sqrt.c
        fixmath/fix32_str.c
        fixmath/fix32_trig.c
)

# Platform-specific source files
if(APPLE)
    target_sources(dunecity
        PRIVATE
            ${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
    )
    # Enable Objective-C ARC (Automatic Reference Counting)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/IDE/xCode/MacFunctions.m
        PROPERTIES
            COMPILE_FLAGS "-fobjc-arc"
    )
endif()

# Source groups for better organization in IDEs
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${sources})

# Include directories
target_include_directories(dunecity PRIVATE
    ${CMAKE_BINARY_DIR}/include   # generated config.h (has correct PROJECT_VERSION)
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Platform-specific include directories
if(APPLE)
    target_include_directories(dunecity PRIVATE
        ${CMAKE_SOURCE_DIR}/IDE/xCode
    )
endif()

# Find miniupnpc for UPnP support
find_package(miniupnpc CONFIG QUIET)
if(NOT miniupnpc_FOUND)
    find_library(MINIUPNPC_LIBRARY miniupnpc PATHS ${CMAKE_PREFIX_PATH}/lib)
    find_path(MINIUPNPC_INCLUDE_DIR miniupnpc/miniupnpc.h PATHS ${CMAKE_PREFIX_PATH}/include)
    if(MINIUPNPC_LIBRARY AND MINIUPNPC_INCLUDE_DIR)
        add_library(miniupnpc::miniupnpc UNKNOWN IMPORTED)
        set_target_properties(miniupnpc::miniupnpc PROPERTIES
            IMPORTED_LOCATION "${MINIUPNPC_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${MINIUPNPC_INCLUDE_DIR}")

    endif()
endif()

# Find discord-rpc for Discord Rich Presence (optional)
find_library(DISCORD_RPC_LIBRARY discord-rpc PATHS ${CMAKE_PREFIX_PATH}/lib)
if(DISCORD_RPC_LIBRARY)
    target_sources(dunecity PRIVATE misc/DiscordManager.cpp)
    target_compile_definitions(dunecity PRIVATE HAS_DISCORD_RPC)
endif()

# Link libraries
# vcpkg provides modern CMake targets for SDL2
target_link_libraries(dunecity
    PRIVATE
        $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
        $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
        $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,SDL2_mixer::SDL2_mixer-static>
        $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,SDL2_ttf::SDL2_ttf-static>
        CURL::libcurl
        $<IF:$<TARGET_EXISTS:miniupnpc::miniupnpc>,miniupnpc::miniupnpc,${MINIUPNPC_LIBRARY}>
        $<$<BOOL:${DISCORD_RPC_LIBRARY}>:${DISCORD_RPC_LIBRARY}>
)

if(WIN32)
    target_link_libraries(dunecity
        PRIVATE
            ws2_32
            winmm
            imm32
            version
            dbghelp
    )
endif()

# macOS-specific frameworks
if(APPLE)
    target_link_libraries(dunecity
        PRIVATE
            "-framework Cocoa"
            "-framework Foundation"
            "-framework CoreFoundation"
    )
endif()

# Compiler options
target_compile_options(dunecity
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -pedantic>
)

# Data directory handling
set(DATA_DIR "${CMAKE_SOURCE_DIR}/data")
set(TORNIE_MOD_DIR "${CMAKE_SOURCE_DIR}/mods/Tornie")
set(TORNIE_MOD_REFERENCE_FILES
    "${TORNIE_MOD_DIR}/mod.ini"
    "${TORNIE_MOD_DIR}/ObjectData.ini"
    "${TORNIE_MOD_DIR}/QuantBot Config.ini"
    "${TORNIE_MOD_DIR}/GameOptions.ini"
)

function(add_copy_tornie_mod_reference DEST_DIR)
    if(EXISTS "${TORNIE_MOD_DIR}")
        add_custom_command(
            TARGET dunecity
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${DEST_DIR}/mods/Tornie"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${TORNIE_MOD_REFERENCE_FILES}
                "${DEST_DIR}/mods/Tornie/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${TORNIE_MOD_DIR}/campaign"
                "${DEST_DIR}/mods/Tornie/campaign"
            COMMENT "Copying Tornie mod reference files"
        )
    endif()
endfunction()

if(APPLE)
    # For macOS bundle, copy data files directly to Resources/ (not Resources/data/)
    # This matches the Xcode project structure where .PAK files are in Resources root
    set(BUNDLE_RESOURCES_DIR "$<TARGET_BUNDLE_CONTENT_DIR:dunecity>/Resources")
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        # Copy all .PAK files and other data to Resources root
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DATA_DIR}"
            "${BUNDLE_RESOURCES_DIR}"
        # Create config subdirectory
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${BUNDLE_RESOURCES_DIR}/config"
        # Copy config template files (.ini.default and .ini) to Resources/config/
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/config/ObjectData.ini.default"
            "${CMAKE_SOURCE_DIR}/config/QuantBot Config.ini.default"
            "${CMAKE_SOURCE_DIR}/config/Dune City.ini"
            "${CMAKE_SOURCE_DIR}/config/README.md"
            "${BUNDLE_RESOURCES_DIR}/config/"
        COMMENT "Copying data and config files into app bundle Resources"
    )

    add_copy_tornie_mod_reference("${BUNDLE_RESOURCES_DIR}")

    # Copy imported Micropolis-derived zone sprites into the bundle so they
    # are found by getDuneLegacyDataDir() + "imported_sprites/..." at runtime.
    set(IMPORTED_SPRITES_SRC "${CMAKE_SOURCE_DIR}/imported_sprites")
    if(EXISTS "${IMPORTED_SPRITES_SRC}")
        add_custom_command(
            TARGET dunecity
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${IMPORTED_SPRITES_SRC}"
                "${BUNDLE_RESOURCES_DIR}/imported_sprites"
            COMMENT "Copying imported sprites into app bundle Resources"
        )
    endif()

    # Code sign the app bundle with ad-hoc signature
    # This is required for macOS Gatekeeper to allow the app to run when downloaded
    # Without proper signing, users get "app is damaged and can't be opened" error
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:dunecity>"
        COMMENT "Code signing app bundle with ad-hoc signature"
    )
else()
    # For other platforms, copy to build/data directory
    set(BUILD_DATA_DIR "${CMAKE_BINARY_DIR}/data")
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DATA_DIR}"
            "${BUILD_DATA_DIR}"
        COMMENT "Copying data directory to build directory"
    )

    # Copy config template files next to the binary so the mod system can seed
    # ObjectData.ini etc. For local builds, DUNELEGACY_DATADIR points at a
    # non-existent install path; the fallback in FileSystem.cpp uses
    # SDL_GetBasePath() which resolves to this directory.
    add_custom_command(
        TARGET dunecity
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/config/ObjectData.ini.default"
            "${CMAKE_SOURCE_DIR}/config/QuantBot Config.ini.default"
            "${CMAKE_SOURCE_DIR}/config/Dune City.ini"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config/"
        COMMENT "Copying config template files to build output"
    )

    add_copy_tornie_mod_reference("${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

    # Copy imported zone sprites next to the binary for non-bundle builds
    set(IMPORTED_SPRITES_SRC "${CMAKE_SOURCE_DIR}/imported_sprites")
    if(EXISTS "${IMPORTED_SPRITES_SRC}")
        add_custom_command(
            TARGET dunecity
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${IMPORTED_SPRITES_SRC}"
                "${CMAKE_BINARY_DIR}/bin/imported_sprites"
            COMMENT "Copying imported sprites to build output"
        )
    endif()
endif()

# Install data directory
# For macOS bundles, data files are already inside the .app via POST_BUILD commands
if(WIN32)
    # Windows: install PAK files and data to share/DuneLegacy where game expects them
    # Use data directory which has all PAK files
    
    # Install all PAK files from data directory to root installation directory
    install(
        DIRECTORY ${DATA_DIR}/
        DESTINATION .
        FILES_MATCHING PATTERN "*.PAK"
    )
    
    # Install locale files
    install(
        DIRECTORY ${DATA_DIR}/locale
        DESTINATION .
    )
    
    # Install maps
    install(
        DIRECTORY ${DATA_DIR}/maps
        DESTINATION .
    )
    
    # Install Dune2-Versions.txt
    install(
        FILES "${DATA_DIR}/Dune2-Versions.txt"
        DESTINATION .
    )
    
    # Install config files from config directory to root installation directory
    install(
        DIRECTORY ${CMAKE_SOURCE_DIR}/config
        DESTINATION .
    )
elseif(NOT APPLE)
    # Linux: use FHS structure
    install(
        DIRECTORY ${DATA_DIR}/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}
        FILES_MATCHING PATTERN "*.*"
    )
endif()

# Install imported sprite assets (Micropolis-derived civic/zone PNGs).
# macOS bundles already include these via the POST_BUILD copy above.
# Windows and Linux packaged builds need an explicit install rule so CPack
# bundles the PNGs into the installer — without this, getDuneLegacyDataDir()
# + "imported_sprites/..." finds nothing and civic sprites fall back to
# placeholders (or crash on older builds).
set(IMPORTED_SPRITES_SRC "${CMAKE_SOURCE_DIR}/imported_sprites")
if(EXISTS "${IMPORTED_SPRITES_SRC}")
    if(WIN32)
        install(
            DIRECTORY "${IMPORTED_SPRITES_SRC}/"
            DESTINATION imported_sprites
        )
    elseif(NOT APPLE)
        install(
            DIRECTORY "${IMPORTED_SPRITES_SRC}/"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/imported_sprites
        )
    endif()
endif()

if(EXISTS "${TORNIE_MOD_DIR}")
    if(WIN32)
        set(TORNIE_INSTALL_BASE ".")
    elseif(NOT APPLE)
        set(TORNIE_INSTALL_BASE "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
    endif()

    if(WIN32 OR NOT APPLE)
        install(
            FILES ${TORNIE_MOD_REFERENCE_FILES}
            DESTINATION "${TORNIE_INSTALL_BASE}/mods/Tornie"
        )
        install(
            DIRECTORY "${TORNIE_MOD_DIR}/campaign/"
            DESTINATION "${TORNIE_INSTALL_BASE}/mods/Tornie/campaign"
        )
    endif()
endif()

# Install executable
if(WIN32)
    # Windows: install to root directory
    # Use CMake build output (not VS IDE output)
    install(TARGETS dunecity
        RUNTIME DESTINATION .
    )
    # Install vcpkg DLLs for Windows
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_BUILD_TYPE)
        install(DIRECTORY ${CMAKE_BINARY_DIR}/bin/Release/
            DESTINATION .
            FILES_MATCHING PATTERN "*.dll"
        )
    endif()
elseif(APPLE)
    # macOS: install as bundle
    install(TARGETS dunecity
        BUNDLE DESTINATION .
    )

    # Re-codesign the installed bundle. The POST_BUILD codesign on the
    # in-tree build is invalidated by `cmake --install` rewriting the
    # binary's rpath, so verification fails ("code or signature have
    # been modified") and macOS shows "dunecity.app is damaged and
    # can't be opened." Signing the bundle at install time, after all
    # rpath fixup is done, restores a valid ad-hoc signature. The
    # signature is still ad-hoc (no Apple Developer ID), so first-run
    # downloads still hit Gatekeeper, but with a valid signature the
    # warning becomes "cannot verify developer" with an Open Anyway
    # path instead of the unrecoverable "damaged" message.
    install(CODE "
        message(STATUS \"Re-codesigning installed dunecity.app...\")
        execute_process(
            COMMAND codesign --force --deep --sign - \"\${CMAKE_INSTALL_PREFIX}/dunecity.app\"
            RESULT_VARIABLE _codesign_result
        )
        if(NOT _codesign_result EQUAL 0)
            message(FATAL_ERROR \"Install-time codesign failed with exit code \${_codesign_result}\")
        endif()
        execute_process(
            COMMAND codesign --verify --deep --strict \"\${CMAKE_INSTALL_PREFIX}/dunecity.app\"
            RESULT_VARIABLE _verify_result
        )
        if(NOT _verify_result EQUAL 0)
            message(FATAL_ERROR \"Install-time codesign verification failed with exit code \${_verify_result}\")
        endif()
        message(STATUS \"Installed dunecity.app signature verified.\")
    ")
else()
    # Linux: set rpath so the installed binary finds bundled .so files relative
    # to itself rather than requiring system-installed or build-tree libraries.
    # $ORIGIN expands to the directory containing the binary at runtime, so
    # $ORIGIN/../lib/dunecity resolves to <prefix>/lib/dunecity.
    set_target_properties(dunecity PROPERTIES
        INSTALL_RPATH "$ORIGIN/../lib/dunecity"
        BUILD_WITH_INSTALL_RPATH FALSE
    )

    install(TARGETS dunecity
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    # Bundle all vcpkg-built shared libraries so the installed binary is
    # self-contained and does not depend on system SDL2/mixer/ttf/curl/etc.
    # VCPKG_INSTALLED_DIR and VCPKG_TARGET_TRIPLET are set by the vcpkg
    # toolchain file; skip silently if building without vcpkg.
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/"
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/dunecity"
            FILES_MATCHING
                PATTERN "*.so"
                PATTERN "*.so.*"
            PATTERN "pkgconfig" EXCLUDE
        )
    endif()
endif()

# DUNELEGACY_DATADIR is set at the top of this file (before configure_file)
# only for Linux. Windows and macOS use SDL_GetBasePath() at runtime.

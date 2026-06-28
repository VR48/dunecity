cmake_minimum_required(VERSION 3.15)

# Project name and version
project(DuneCity VERSION 1.0.112 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include cmake modules
include(CMakeDependentOption)
include(CTest)
include(GNUInstallDirs)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# Release build optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(APPLE)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum OS X deployment version" FORCE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -ffast-math")
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -ffast-math")
    endif()
endif()

# Check for modern C++ support
if(MSVC)
    # Use C++17 consistently
    set(CMAKE_CXX_STANDARD 17)
else()
    # Check if compiler supports C++17
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-std=c++17" COMPILER_SUPPORTS_CXX17)
    
    if(COMPILER_SUPPORTS_CXX17)
        set(CMAKE_CXX_STANDARD 17)
    else()
        set(CMAKE_CXX_STANDARD 14)
        message(STATUS "C++17 not supported, falling back to C++14")
    endif()
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(DUNECITY_BUILD_TESTS "Build test suite" OFF)
option(DUNECITY_ENABLE_PCH "Enable precompiled headers" ON)
option(DUNECITY_STATIC_RUNTIME "Use static runtime libraries" OFF)

# Find required packages
# When using vcpkg, these will be automatically found via CMAKE_TOOLCHAIN_FILE
find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_mixer CONFIG REQUIRED)
find_package(SDL2_ttf CONFIG REQUIRED)
find_package(CURL REQUIRED)

# Platform-specific settings
if(WIN32)
    # Windows-specific settings
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    
    # Check for Visual Studio
    if(MSVC)
        # Set Visual Studio specific options
        set(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION "10.0")
        set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x64")
        
        # Runtime library settings
        if(DUNECITY_STATIC_RUNTIME)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        else()
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
        
        # Precompiled header settings
        if(DUNECITY_ENABLE_PCH)
            set(PCH_SOURCE "${CMAKE_SOURCE_DIR}/IDE/VC/stdafx.cpp")
            set(PCH_HEADER "${CMAKE_SOURCE_DIR}/IDE/VC/stdafx.h")
        endif()
    endif()
else()
    # Unix-like systems
    find_package(Threads REQUIRED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()

# Global compile options
if(MSVC)
    add_compile_options(/W4)
    # Prevent Windows.h from defining min/max macros that conflict with std::min/std::max
    add_compile_definitions(NOMINMAX)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -pedantic)
    add_compile_options(-Wno-unused-parameter -Wno-unused-but-set-variable -Wno-unknown-pragmas) # FIXME These should be cleaned up.
    add_compile_options(-Wno-expansion-to-defined)  # Silences "define FMT_HAS_FEATURE(x) __has_feature(x) may not be portable" warnings.
endif()

# Windows-specific file operations (before adding subdirectories)
if(WIN32)
    # Copy all .PAK files
    file(GLOB PAK_FILES "${CMAKE_SOURCE_DIR}/data/*.PAK")
    foreach(PAK_FILE ${PAK_FILES})
        get_filename_component(PAK_FILENAME ${PAK_FILE} NAME)
        file(COPY "${PAK_FILE}" DESTINATION "${CMAKE_BINARY_DIR}/bin/")
    endforeach()
endif()

# Add subdirectories
add_subdirectory(src)
if(DUNECITY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Create a custom target for copying data files (non-macOS platforms)
# macOS bundles handle data copying in src/CMakeLists.txt
if(NOT APPLE)
    add_custom_target(copy_data ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data/"
        "${CMAKE_BINARY_DIR}/bin/"
        COMMENT "Copying data files directly to bin directory"
    )
    
    # Add dependency to ensure data files are copied after main target is built
    add_dependencies(copy_data dunecity)
endif()


# Platform-specific install configuration
if(WIN32)
    # Windows: install all files to root directory (not in a share subdirectory)
    set(DUNECITY_INSTALL_ROOT ".")
    set(DUNECITY_DATA_DIR ".")
    set(DUNECITY_CONFIG_DIR "config")
    set(DUNECITY_DOC_DIR ".")
elseif(APPLE)
    # macOS already handled via bundle
    set(DUNECITY_INSTALL_ROOT ".")
    set(DUNECITY_DATA_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
    set(DUNECITY_CONFIG_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/config")
    set(DUNECITY_DOC_DIR "${CMAKE_INSTALL_DOCDIR}")
else()
    # Unix/Linux FHS structure
    set(DUNECITY_INSTALL_ROOT "${CMAKE_INSTALL_BINDIR}")
    set(DUNECITY_DATA_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
    set(DUNECITY_CONFIG_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/config")
    set(DUNECITY_DOC_DIR "${CMAKE_INSTALL_DOCDIR}")
endif()

# Install data files
# Install configuration template files to config subdirectory
# For macOS bundles, these are already inside the .app via POST_BUILD commands
# For Windows with VS build, config files are installed from VS output directory to root
if(NOT APPLE)
    set(VS_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/Release-x64")
    if(WIN32 AND EXISTS "${VS_OUTPUT_DIR}/config")
        # Config files will be installed from VS output directory to root (see src/CMakeLists.txt)
    else()
        install(FILES 
            config/ObjectData.ini.default
            "config/QuantBot Config.ini.default"
            "config/Dune City.ini"
            config/README.md
            DESTINATION ${DUNECITY_CONFIG_DIR}
        )
    endif()
endif()

# Install desktop file and icons on Linux
if(UNIX AND NOT APPLE)
    install(FILES dunecity.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES dunecity.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps
    )
    install(FILES dunecity-128x128.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
        RENAME dunecity.png
    )
    install(FILES dunecity.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
endif()

# Install documentation
if(WIN32)
    # Windows: install docs to root
    install(FILES AUTHORS COPYING NEWS README
        DESTINATION ${DUNECITY_DOC_DIR}
    )
    # Install release notes
    install(FILES "release_notes/RELEASE_NOTES.md"
        DESTINATION ${DUNECITY_DOC_DIR}
        RENAME "Release_Notes.md"
    )
elseif(NOT APPLE)
    # Linux: use standard doc directory
    install(FILES AUTHORS COPYING NEWS README
        DESTINATION ${DUNECITY_DOC_DIR}
    )
    install(FILES "release_notes/RELEASE_NOTES.md"
        DESTINATION ${DUNECITY_DOC_DIR}
        RENAME "Release_Notes.md"
        OPTIONAL
    )
endif()

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "Dune City Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_HOMEPAGE_URL ${PROJECT_HOMEPAGE_URL})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/dunecity.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/dunecity.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "dunecity.exe")
    set(CPACK_NSIS_DISPLAY_NAME "Dune City")
    set(CPACK_NSIS_PACKAGE_NAME "Dune City")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "DuneCity")
    set(CPACK_PACKAGE_FILE_NAME "DuneCity-${PROJECT_VERSION}-Windows-x64")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_MUI_FINISHPAGE_RUN "dunecity.exe")
    
    # Add checkbox to view release notes on finish page
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKLM \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\dunecity.exe\\\" \\\"\\\" \\\"$INSTDIR\\\\dunecity.exe\\\"
        WriteRegStr HKLM \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\dunecity.exe\\\" \\\"Path\\\" \\\"$INSTDIR\\\"
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegKey HKLM \\\"Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\App Paths\\\\dunecity.exe\\\"
    ")
    
    # Custom NSIS code for release notes checkbox on finish page
    set(CPACK_NSIS_DEFINES "
        !define MUI_FINISHPAGE_SHOWREADME \\\"$INSTDIR\\\\Release_Notes.md\\\"
        !define MUI_FINISHPAGE_SHOWREADME_TEXT \\\"View Release Notes\\\"
        !define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED
    ")
    
    # Create Start Menu shortcuts
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "
        CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Dune City.lnk\\\" \\\"$INSTDIR\\\\dunecity.exe\\\"
        CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Release Notes.lnk\\\" \\\"$INSTDIR\\\\Release_Notes.md\\\"
        CreateShortCut \\\"$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Readme.lnk\\\" \\\"$INSTDIR\\\\README\\\"
    ")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA "
        Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Dune City.lnk\\\"
        Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Release Notes.lnk\\\"
        Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Readme.lnk\\\"
    ")
    
    # Add custom target for Windows installer
    add_custom_target(installer
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning CPack cache and install directory..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/_CPack_Packages
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/*.exe
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Building binary..."
        COMMAND ${CMAKE_COMMAND} --build . --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Removing install directory to force fresh install..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Installing to staging directory..."
        COMMAND ${CMAKE_COMMAND} --install . --prefix install --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Creating installer..."
        COMMAND ${CMAKE_CPACK_COMMAND} -G NSIS -C Release
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building Windows NSIS installer (with guaranteed fresh install)..."
    )
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Dune City ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")  # Compressed
    set(CPACK_PACKAGE_FILE_NAME "DuneCity-${PROJECT_VERSION}-macOS")
    set(MACOSX_BUNDLE_ICON_FILE "dunecity.icns")
    
    # Install README and release notes to DMG root (not inside .app)
    install(FILES README DESTINATION . COMPONENT Runtime)
    install(FILES "release_notes/RELEASE_NOTES.md" 
            DESTINATION . 
            RENAME "Release Notes.md"
            COMPONENT Runtime
            OPTIONAL)
    
    # Optional: Add custom DMG background and layout if files exist
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/dmg_background.png")
        set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/dmg_background.png")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/DMGSetup.scpt")
        set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/packaging/DMGSetup.scpt")
    endif()
    
    # Add custom target for DMG creation
    # NOTE: We bypass CPack entirely due to persistent caching issues where old binaries
    # would get packaged even after fresh builds. This manual approach uses hdiutil directly.
    add_custom_target(dmg
        COMMAND ${CMAKE_COMMAND} -E echo "Building binary..."
        COMMAND ${CMAKE_COMMAND} --build . --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning old DMG and staging..."
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/DuneCity-${PROJECT_VERSION}-macOS.dmg
        COMMAND ${CMAKE_COMMAND} -E remove_directory /tmp/dunecity_dmg_staging
        COMMAND ${CMAKE_COMMAND} -E echo "Staging files for DMG..."
        COMMAND ${CMAKE_COMMAND} -E make_directory /tmp/dunecity_dmg_staging
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_BINARY_DIR}/bin/dunecity.app /tmp/dunecity_dmg_staging/dunecity.app
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/README /tmp/dunecity_dmg_staging/README
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/release_notes/RELEASE_NOTES.md /tmp/dunecity_dmg_staging/Release\\ Notes.md
        COMMAND ${CMAKE_COMMAND} -E echo "Creating Applications symlink for drag-and-drop install..."
        COMMAND ln -sf /Applications /tmp/dunecity_dmg_staging/Applications
        COMMAND ${CMAKE_COMMAND} -E echo "Creating DMG with hdiutil..."
        COMMAND hdiutil create -volname "Dune City ${PROJECT_VERSION}" -srcfolder /tmp/dunecity_dmg_staging -ov -format UDZO ${CMAKE_BINARY_DIR}/DuneCity-${PROJECT_VERSION}-macOS.dmg
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning up staging..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory /tmp/dunecity_dmg_staging
        COMMAND ${CMAKE_COMMAND} -E echo "DMG created: DuneCity-${PROJECT_VERSION}-macOS.dmg"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building macOS DMG installer (manual hdiutil - bypasses CPack caching issues)..."
    )
else()
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_SECTION "games")
    # SDL2, SDL2_mixer, SDL2_ttf, curl, miniupnpc, and discord-rpc are all
    # bundled from vcpkg into lib/dunecity/ — do not declare them as system
    # package dependencies. Only the baseline C/C++ runtime is required.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17), libstdc++6 (>= 6)")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Dune City Team")
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-2.0+")
    set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")
    set(CPACK_PACKAGE_FILE_NAME "DuneCity-${PROJECT_VERSION}-Linux-x64")
    
    # Add custom target for Linux packages. CMake reserves the target
    # name "package" when CPack is enabled (newer CMake versions error
    # out at configure time), so use "linux-packages" instead. Update
    # BUILD.md and .vscode/tasks.json to match.
    add_custom_target(linux-packages
        COMMAND ${CMAKE_COMMAND} -E echo "Cleaning CPack cache and install directory..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/_CPack_Packages
        COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/*.deb ${CMAKE_BINARY_DIR}/*.rpm ${CMAKE_BINARY_DIR}/*.tar.gz
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Building binary..."
        COMMAND ${CMAKE_COMMAND} --build . --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Removing install directory to force fresh install..."
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/install
        COMMAND ${CMAKE_COMMAND} -E echo "Installing to staging directory..."
        COMMAND ${CMAKE_COMMAND} --install . --prefix install --config Release
        COMMAND ${CMAKE_COMMAND} -E echo "Creating packages..."
        COMMAND ${CMAKE_CPACK_COMMAND} -G "DEB;RPM;TGZ" -C Release
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building Linux packages (DEB, RPM, TGZ) (with guaranteed fresh install)..."
    )
endif()

include(CPack) 

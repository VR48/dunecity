/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <main.h>

#include <globals.h>

#include <config.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/GFXManager.h>
#include <FileClasses/SFXManager.h>
#include <FileClasses/FontManager.h>
#include <FileClasses/TextManager.h>
#include <FileClasses/INIFile.h>
#include <FileClasses/Palfile.h>
#include <FileClasses/music/DirectoryPlayer.h>
#include <FileClasses/music/ADLPlayer.h>
#include <FileClasses/music/XMIPlayer.h>

#include <GUI/GUIStyle.h>
#include <GUI/dune/DuneStyle.h>

#include <Menu/MainMenu.h>
#include <Menu/OptionsMenu.h>

#include <misc/DiscordManager.h>

#include <misc/fnkdat.h>
#include <misc/FileSystem.h>
#include <misc/Scaler.h>
#include <misc/string_util.h>
#include <misc/exceptions.h>
#include <misc/format.h>
#include <misc/SDL2pp.h>
#include <misc/md5.h>

#include <players/QuantBotConfig.h>
#include <mod/ModManager.h>

#include <CrashHandler.h>
#include <SoundPlayer.h>

#include <mmath.h>

#include <CutScenes/Intro.h>

#include <SDL_ttf.h>

#include <iostream>
#include <typeinfo>
#include <future>
#include <array>
#include <ctime>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <fcntl.h>


#ifdef _WIN32
    #include <windows.h>
    #include <stdio.h>
    #include <io.h>
#else
    #include <sys/types.h>
    #include <pwd.h>
    #include <unistd.h>
#endif

#ifdef __APPLE__
#include <misc/MacFunctions.h>
#endif

#if defined(__linux__)
#include <dlfcn.h>
#endif

#if !defined(__GNUG__) || (defined(_GLIBCXX_HAS_GTHREADS) && defined(_GLIBCXX_USE_C99_STDINT_TR1) && (ATOMIC_INT_LOCK_FREE > 1) && !defined(_GLIBCXX_HAS_GTHREADS))
// g++ does not provide std::async on all platforms
#define HAS_ASYNC
#endif

#if defined( __clang__ ) || defined(__GNUG__) || defined( __GLIBCXX__ ) || defined( __GLIBCPP__ )
#include <cxxabi.h>
inline std::string demangleSymbol(const char* symbolname) {
    int status = 0;
    std::size_t size = 0;
    char* result = abi::__cxa_demangle(symbolname, nullptr, &size, &status);
    if(status != 0) {
        return std::string(symbolname);
    } else {
        std::string name = std::string(result);
        std::free(result);
        return name;
    }
}
#else
inline std::string demangleSymbol(const char* symbolname) {
    return std::string(symbolname);
}
#endif

void setVideoMode(int displayIndex);
void realign_buttons();

static void printUsage() {
    fprintf(stderr, "Usage:\n\tdunecity [--showlog] [--fullscreen|--window] [--PlayerName=X] [--ServerPort=X]\n");
}

int getLogicalToPhysicalResolutionFactor(int physicalWidth, int physicalHeight) {
    if(physicalWidth >= 1280*3 && physicalHeight >= 720*3) {
        return 3;
    } else if(physicalWidth >= 640*2 && physicalHeight >= 480*2) {
        return 2;
    } else {
        return 1;
    }
}

void setVideoMode(int displayIndex)
{
    int videoFlags = 0;

    if(settings.video.fullscreen) {
        videoFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    SDL_DisplayMode targetDisplayMode = { 0, settings.video.physicalWidth, settings.video.physicalHeight, 0, nullptr};
    SDL_DisplayMode closestDisplayMode;

    if(SDL_GetClosestDisplayMode(displayIndex, &targetDisplayMode, &closestDisplayMode) == nullptr) {
        SDL_Log("Warning: Falling back to a display resolution of 640x480!");
        settings.video.physicalWidth = 640;
        settings.video.physicalHeight = 480;
        int factor = getLogicalToPhysicalResolutionFactor(settings.video.physicalWidth, settings.video.physicalHeight);
        // Prevent division by zero and ensure minimum dimensions
        if(factor <= 0) {
            factor = 1;
        }
        settings.video.width = settings.video.physicalWidth / factor;
        settings.video.height = settings.video.physicalHeight / factor;
        // Ensure minimum dimensions
        if(settings.video.width < 640) settings.video.width = 640;
        if(settings.video.height < 480) settings.video.height = 480;
    } else {
        settings.video.physicalWidth = closestDisplayMode.w;
        settings.video.physicalHeight = closestDisplayMode.h;
        int factor = getLogicalToPhysicalResolutionFactor(settings.video.physicalWidth, settings.video.physicalHeight);
        // Prevent division by zero and ensure minimum dimensions
        if(factor <= 0) {
            factor = 1;
        }
        settings.video.width = settings.video.physicalWidth / factor;
        settings.video.height = settings.video.physicalHeight / factor;
        
        // Ensure minimum dimensions
        if(settings.video.width < 640) settings.video.width = 640;
        if(settings.video.height < 480) settings.video.height = 480;
    }

    // Prefer Direct3D on Windows, let SDL choose best renderer on other platforms
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#else
    // On non-Windows platforms, let SDL choose the best renderer
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // Use nearest-neighbor scaling for pixel-perfect look
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");       // Enable render batching for performance

    window = SDL_CreateWindow("DuneCity",
                              SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex),
                              settings.video.physicalWidth, settings.video.physicalHeight,
                              videoFlags);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    // Create renderer (VSync set separately for macOS compatibility)
    Uint32 rendererFlags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
    
    renderer = SDL_CreateRenderer(window, -1, rendererFlags);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }
    
    // Set VSync after renderer creation (works better on macOS Metal)
    if(settings.video.frameLimit) {
        SDL_RenderSetVSync(renderer, 1);
        SDL_Log("VSync enabled");
    } else {
        SDL_RenderSetVSync(renderer, 0);
        SDL_Log("VSync disabled");
    }
    SDL_RenderSetLogicalSize(renderer, settings.video.width, settings.video.height);
    screenTexture = SDL_CreateTexture(renderer, SCREEN_FORMAT, SDL_TEXTUREACCESS_TARGET, settings.video.width, settings.video.height);

    // Check if texture creation failed
    if (!screenTexture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    // Enable hardware acceleration
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(screenTexture, SDL_ScaleModeNearest);

    SDL_ShowCursor(SDL_ENABLE);
}

void toogleFullscreen()
{
    // Safety checks
    if(window == nullptr || renderer == nullptr) {
        SDL_Log("Warning: Cannot toggle fullscreen - window or renderer not available");
        return;
    }

    if(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        // switch to windowed mode
        SDL_Log("Switching to windowed mode.");
        SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP));

        SDL_SetWindowSize(window, settings.video.physicalWidth, settings.video.physicalHeight);
        SDL_RenderSetLogicalSize(renderer, settings.video.width, settings.video.height);
    } else {
        // switch to fullscreen mode
        SDL_Log("Switching to fullscreen mode.");
        SDL_DisplayMode displayMode;
        int displayIndex = SDL_GetWindowDisplayIndex(window);
        if(displayIndex >= 0 && SDL_GetDesktopDisplayMode(displayIndex, &displayMode) == 0) {
            SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP));
            SDL_SetWindowSize(window, displayMode.w, displayMode.h);
            SDL_RenderSetLogicalSize(renderer, settings.video.width, settings.video.height);
        } else {
            SDL_Log("Warning: Could not get desktop display mode, using current window size");
            SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) ^ SDL_WINDOW_FULLSCREEN_DESKTOP));
        }
    }

    // we just need to flush all events; otherwise we might get them twice
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    // wait a bit to avoid immediately switching back
    SDL_Delay(100);
}

std::string getConfigFilepath()
{
    // User config file is stored in user directory (AppData on Windows, ~/.config on Linux, etc.)
    char tmp[FILENAME_MAX];
    if(fnkdat(CONFIGFILENAME, tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for config file!");
    }
    return std::string(tmp);
}

std::string getConfigTemplateFilepath()
{
    // Template config file is in config subdirectory of game directory
    return getDuneLegacyDataDir() + "/config/" + CONFIGFILENAME;
}

std::string getLogFilepath()
{
    // determine path to config file
    char tmp[FILENAME_MAX];
    if(fnkdat(LOGFILENAME, tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed!");
    }

    return std::string(tmp);
}

std::string getPerformanceLogFilepath()
{
    // determine path to performance logfile
    char tmp[FILENAME_MAX];
    if(fnkdat("DuneCity-Performance.log", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for performance log!");
    }

    return std::string(tmp);
}

std::string getObjectDataConfigFilepath()
{
    // If ModManager is initialized and a non-vanilla mod is active, use mod path
    if (ModManager::instance().isInitialized() && 
        ModManager::instance().getActiveModName() != "vanilla") {
        return ModManager::instance().getActiveObjectDataPath();
    }
    
    // Default: user config directory (preserves existing user customizations)
    char tmp[FILENAME_MAX];
    if(fnkdat("config/ObjectData.ini", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT) < 0) {
        THROW(std::runtime_error, "fnkdat() failed for ObjectData.ini!");
    }
    return std::string(tmp);
}

std::string getObjectDataTemplateFilepath()
{
    // Template ObjectData.ini.default is in config subdirectory of game directory
    return getDuneLegacyDataDir() + "/config/ObjectData.ini.default";
}

static std::string getQuantBotTemplateFilepath()
{
    return getDuneLegacyDataDir() + "/config/QuantBot Config.ini.default";
}

static bool computeFileDigest(const std::string& filepath, std::array<unsigned char, 16>& digest)
{
    if(md5_file(filepath.c_str(), digest.data()) != 0) {
        return false;
    }
    return true;
}

static bool areConfigFilesOutOfSync(bool& objectDataOutOfSync, bool& quantBotOutOfSync)
{
    objectDataOutOfSync = false;
    quantBotOutOfSync = false;

    const std::string objectTemplate = getObjectDataTemplateFilepath();
    const std::string objectUser = getObjectDataConfigFilepath();

    if(!existsFile(objectTemplate)) {
        SDL_Log("Warning: Template ObjectData.ini.default missing at %s", objectTemplate.c_str());
        objectDataOutOfSync = true;
    } else if(!existsFile(objectUser)) {
        SDL_Log("ObjectData.ini missing at %s", objectUser.c_str());
        objectDataOutOfSync = true;
    } else {
        std::array<unsigned char, 16> templateDigest{};
        std::array<unsigned char, 16> userDigest{};
        if(!computeFileDigest(objectTemplate, templateDigest) || !computeFileDigest(objectUser, userDigest)) {
            SDL_Log("Warning: Unable to compare ObjectData configuration files.");
            objectDataOutOfSync = true;
        } else if(templateDigest != userDigest) {
            objectDataOutOfSync = true;
        }
    }

    const std::string quantTemplate = getQuantBotTemplateFilepath();
    const std::string quantUser = getQuantBotConfigFilepath();

    if(!existsFile(quantTemplate)) {
        SDL_Log("Warning: Template QuantBot Config.ini.default missing at %s", quantTemplate.c_str());
        quantBotOutOfSync = true;
    } else if(!existsFile(quantUser)) {
        SDL_Log("QuantBot Config.ini missing at %s", quantUser.c_str());
        quantBotOutOfSync = true;
    } else {
        std::array<unsigned char, 16> templateDigest{};
        std::array<unsigned char, 16> userDigest{};
        if(!computeFileDigest(quantTemplate, templateDigest) || !computeFileDigest(quantUser, userDigest)) {
            SDL_Log("Warning: Unable to compare QuantBot configuration files.");
            quantBotOutOfSync = true;
        } else if(templateDigest != userDigest) {
            quantBotOutOfSync = true;
        }
    }

    return objectDataOutOfSync || quantBotOutOfSync;
}

static bool copyTemplateFile(const std::string& templateRelativePath, const std::string& destinationPath)
{
    try {
        auto rwSource = pFileManager->openFile(templateRelativePath);
        if(!rwSource) {
            SDL_Log("copyTemplateFile: failed to open template '%s'", templateRelativePath.c_str());
            return false;
        }

        auto rwDest = sdl2::RWops_ptr{ SDL_RWFromFile(destinationPath.c_str(), "wb") };
        if(!rwDest) {
            SDL_Log("copyTemplateFile: failed to open destination '%s': %s", destinationPath.c_str(), SDL_GetError());
            return false;
        }

        SDL_ClearError();
        std::array<char, 4096> buffer{};

        while(true) {
            size_t bytesRead = SDL_RWread(rwSource.get(), buffer.data(), 1, buffer.size());
            if(bytesRead == 0) {
                const char* err = SDL_GetError();
                if(err != nullptr && err[0] != '\0') {
                    SDL_Log("copyTemplateFile: read error on '%s': %s", templateRelativePath.c_str(), err);
                    return false;
                }
                break; // EOF
            }

            size_t bytesWritten = SDL_RWwrite(rwDest.get(), buffer.data(), 1, bytesRead);
            if(bytesWritten != bytesRead) {
                SDL_Log("copyTemplateFile: write error on '%s': %s", destinationPath.c_str(), SDL_GetError());
                return false;
            }
        }

        return true;
    } catch(const std::exception& ex) {
        SDL_Log("copyTemplateFile: exception while copying '%s' -> '%s': %s", templateRelativePath.c_str(), destinationPath.c_str(), ex.what());
        return false;
    }
}

std::string getDefaultPlayerName() {
    char playername[MAX_PLAYERNAMELENGHT+1] = "Player";

#ifdef _WIN32
    DWORD playernameLength = MAX_PLAYERNAMELENGHT+1;
    GetUserName(playername, &playernameLength);
#else
    struct passwd* pwent = getpwuid(getuid());

    if(pwent != nullptr) {
        strncpy(playername, pwent->pw_name, MAX_PLAYERNAMELENGHT + 1);
        playername[MAX_PLAYERNAMELENGHT] = '\0';
    }
#endif

    playername[0] = toupper(playername[0]);
    return std::string(playername);
}

bool restoreDefaultConfigs() {
    SDL_Log("========== RESTORING DEFAULT CONFIG FILES ==========");
    
    bool success = true;
    
    // Restore ObjectData.ini
    {
        try {
            std::string userPath = getObjectDataConfigFilepath();
            SDL_Log("Restoring ObjectData.ini to: %s", userPath.c_str());
            
            if (copyTemplateFile("config/ObjectData.ini.default", userPath)) {
                SDL_Log("  ✓ ObjectData.ini restored successfully");
            } else {
                SDL_Log("  ✗ Failed to restore ObjectData.ini");
                success = false;
            }
        } catch (std::exception& e) {
            SDL_Log("  ✗ Error restoring ObjectData.ini: %s", e.what());
            success = false;
        }
    }
    
    // Restore QuantBot Config.ini
    {
        try {
            std::string userPath = getQuantBotConfigFilepath();
            SDL_Log("Restoring QuantBot Config.ini to: %s", userPath.c_str());
            
            if (copyTemplateFile("config/QuantBot Config.ini.default", userPath)) {
                SDL_Log("  ✓ QuantBot Config.ini restored successfully");
            } else {
                SDL_Log("  ✗ Failed to restore QuantBot Config.ini");
                success = false;
            }
        } catch (std::exception& e) {
            SDL_Log("  ✗ Error restoring QuantBot Config.ini: %s", e.what());
            success = false;
        }
    }
    
    SDL_Log("====================================================");
    return success;
}

static bool promptToRestoreOutOfSyncConfigurations()
{
    // With the mod system, vanilla mod is automatically seeded from templates
    // by ModManager::initialize() -> vanillaNeedsReseed() -> seedVanillaFromDefaults()
    // This legacy check is no longer needed.
    return false;
}

void createDefaultConfigFile(const std::string& configfilepath, const std::string& language) {
    SDL_Log("Creating user config file '%s'", configfilepath.c_str());

    // Try to copy template file from config directory first (only if FileManager is initialized)
    if (pFileManager) {
    try {
        auto templateFile = pFileManager->openFile("config/" + std::string(CONFIGFILENAME));
        if (templateFile) {
            SDL_Log("Copying template from game installation directory...");
            INIFile templateINI(templateFile.get());
            
            // Set user-specific defaults
            templateINI.setStringValue("General", "Player Name", getDefaultPlayerName());
            templateINI.setStringValue("General", "Language", language);
            
            if (templateINI.saveChangesTo(configfilepath)) {
                SDL_Log("User config file created from template successfully");
                SDL_Log("  Template location: %s", getConfigTemplateFilepath().c_str());
                SDL_Log("  User config location: %s", configfilepath.c_str());
                return;
            }
        }
    } catch (std::exception& e) {
        SDL_Log("Warning: Could not copy template from config directory: %s", e.what());
        SDL_Log("Falling back to programmatic creation...");
    }
    } // end if (pFileManager)

    // Fallback: create programmatically in user directory
    SDL_Log("Creating default config file in user directory: %s", configfilepath.c_str());
    auto file = sdl2::RWops_ptr{ SDL_RWFromFile(configfilepath.c_str(), "w") };
    if(!file) {
        THROW(sdl_error, "Opening config file failed: %s!", SDL_GetError());
    }

    const char configfile[] =   "[General]\n"
                                "Play Intro = false          # Play the intro when starting the game?\n"
                                "Player Name = %s            # The name of the player\n"
                                "Language = %s               # en = English, fr = French, de = German\n"
                                "Scroll Speed = 50           # Amount to scroll the map when the cursor is near the screen border\n"
                                "Show Tutorial Hints = true  # Show tutorial hints during the game\n"
                                "\n"
                                "[Video]\n"
                                "# Minimum resolution is 640x480\n"
                                "Width = 640\n"
                                "Height = 480\n"
                                "Physical Width = 640\n"
                                "Physical Height = 480\n"
                                "Fullscreen = true\n"
                                "FrameLimit = true           # Enable VSync for smooth, tear-free rendering.\n"
                                "Preferred Zoom Level = 1    # 0 = no zooming, 1 = 2x, 2 = 3x\n"
                                "Scaler = ScaleHD            # Scaler to use: ScaleHD = apply manual drawn mask to upscale, Scale2x = smooth edges, ScaleNN = nearest neighbour, \n"
                                "RotateUnitGraphics = false  # Freely rotate unit graphics, e.g. carryall graphics\n"
                                "\n"
                                "[Audio]\n"
                                "# There are three different possibilities to play music\n"
                                "#  adl       - This option will use the Dune 2 music as used on e.g. SoundBlaster16 cards\n"
                                "#  xmi       - This option plays the xmi files of Dune 2. Sounds more midi-like\n"
                                "#  directory - Plays music from the \"music\"-directory inside your game directory\n"
                                "#              The \"music\"-directory should contain 5 subdirectories named attack, intro, peace, win and lose\n"
                                "#              Put any mp3, ogg or mid file there and it will be played in the particular situation\n"
                                "Music Type = adl\n"
                                "Play Music = true\n"
                                "Music Volume = 64           # Volume between 0 and 128\n"
                                "Play SFX = true\n"
                                "SFX Volume = 64             # Volume between 0 and 128\n"
                                "\n"
                                "[Network]\n"
                                "ServerPort = %d\n"
                                "MetaServer = %s\n"
                                "\n"
                                "[AI]\n"
                                "Campaign AI = CampaignAIPlayer\n"
                                "\n"
                                "[Game Options]\n"
                                "Game Speed = 16                         # The default speed of the game: 32 = very slow, 8 = very fast, 16 = default\n"
                                "Concrete Required = true                # If true building on bare rock will result in 50%% structure health penalty\n"
                                "Structures Degrade On Concrete = true   # If true structures will degrade on power shortage even if built on concrete\n"
                                "Fog of War = false                      # If true explored terrain will become foggy when no unit or structure is next to it\n"
                                "Start with Explored Map = false         # If true the complete map is unhidden at the beginning of the game\n"
                                "Instant Build = false                   # If true the building of structures and units does not take any time\n"
                                "Only One Palace = false                 # If true, only one palace can be build per house\n"
                                "Rocket-Turrets Need Power = false       # If true, rocket turrets are dysfunctional on power shortage\n"
                                "Sandworms Respawn = false               # If true, killed sandworms respawn after some time\n"
                                "Killed Sandworms Drop Spice = false     # If true, killed sandworms drop some spice\n"
                                "Manual Carryall Drops = false           # If true, player can request carryall to transport units\n"
                                "Maximum Number of Units Override = 0    # Override the maximum number of units each house is allowed to build (-1 = use map default, 0 = unlimited, >0 = specific limit)\n"
                                "Maximum Number of Harvesters Override = -1  # Override the maximum number of harvesters each house is allowed to build (-1 = use map size defaults from ObjectData.ini, >=0 = specific limit)\n";

    // replace player name, language, server port and metaserver
    std::string playername = getDefaultPlayerName();
    std::string strConfigfile = fmt::sprintf(configfile, playername, language, DEFAULT_PORT, DEFAULT_METASERVER);

    if(SDL_RWwrite(file.get(), strConfigfile.c_str(), 1, strConfigfile.length()) == 0) {
        THROW(sdl_error, "Writing config file failed: %s!", SDL_GetError());
    }
}

void logOutputFunction(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    /*
    static const char* priorityStrings[] = {
        nullptr,
        "VERBOSE ",
        "DEBUG   ",
        "INFO    ",
        "WARN    ",
        "ERROR   ",
        "CRITICAL"
    };
    fprintf(stderr, "%s:   %s\n", priorityStrings[priority], message);
    */
    fprintf(stderr, "%s\n", message);
    fflush(stderr);
}

void showMissingFilesMessageBox() {
    SDL_ShowCursor(SDL_ENABLE);

    std::string instruction = "DuneCity uses the data files from original Dune II. The following files are missing:\n";

    for(const std::string& missingFile : FileManager::getMissingFiles()) {
        instruction += " " + missingFile + "\n";
    }

    instruction += "\nPut them in one of the following directories and restart DuneCity:\n";
    for(const std::string& searchPath : FileManager::getSearchPath()) {
        instruction += " " + searchPath + "\n";
    }

    instruction += "\nYou may want to add GERMAN.PAK or FRENCH.PAK for playing in these languages.";

    if(!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DuneCity", instruction.c_str(), nullptr)) {
        fprintf(stderr, "%s\n", instruction.c_str());
    }
}

std::string getUserLanguage() {
    const char* pLang = nullptr;

#ifdef _WIN32
    char ISO639_LanguageName[10];
    if(GetLocaleInfo(GetUserDefaultLCID(), LOCALE_SISO639LANGNAME, ISO639_LanguageName, sizeof(ISO639_LanguageName)) == 0) {
        return "";
    } else {

        pLang = ISO639_LanguageName;
    }

#elif defined (__APPLE__)
    pLang = getMacLanguage();
    if(pLang == nullptr) {
        return "";
    }

#else
    // should work on most unices
    pLang = getenv("LC_ALL");
    if(pLang == nullptr) {
        // try LANG
        pLang = getenv("LANG");
        if(pLang == nullptr) {
            return "";
        }
    }
#endif

    SDL_Log("User locale is '%s'", pLang);

    if(strlen(pLang) < 2) {
        return "";
    } else {
        return strToLower(std::string(pLang, 2));
    }
}


int main(int argc, char *argv[]) {
    SDL_LogSetOutputFunction(logOutputFunction, nullptr);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_WARN);
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);

    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    // global try/catch around everything
    try {

        // init fnkdat
        if(fnkdat(nullptr, nullptr, 0, FNKDAT_INIT) < 0) {
            THROW(std::runtime_error, "Cannot initialize fnkdat!");
        }

        bool bShowDebugLog = false;
        for(int i=1; i < argc; i++) {
            //check for overiding params
            std::string parameter(argv[i]);

            if(parameter == "--showlog") {
                // special parameter which does not overwrite settings
                bShowDebugLog = true;
            } else if((parameter == "-f") || (parameter == "--fullscreen") || (parameter == "-w") || (parameter == "--window") || (parameter.compare(0, 13, "--PlayerName=") == 0) || (parameter.compare(0, 13, "--ServerPort=") == 0)) {
                // normal parameter for overwriting settings
                // handle later
            } else {
                printUsage();
                exit(EXIT_FAILURE);
            }
        }

        if(bShowDebugLog == false) {
            // get utf8-encoded log file path
            std::string logfilePath = getLogFilepath();
            char* pLogfilePath = (char*) logfilePath.c_str();

            #ifdef _WIN32

            // on win32 we need an ansi-encoded filepath
            WCHAR szwLogPath[MAX_PATH];
            char szLogPath[MAX_PATH];

            if(MultiByteToWideChar(CP_UTF8, 0, pLogfilePath, -1, szwLogPath, MAX_PATH) == 0) {
                THROW(std::runtime_error, "Conversion of logfile path from utf-8 to utf-16 failed!");
            }

            if(WideCharToMultiByte(CP_ACP, 0, szwLogPath, -1, szLogPath, MAX_PATH, nullptr, nullptr) == 0) {
                THROW(std::runtime_error, "Conversion of logfile path from utf-16 to ansi failed!");
            }

            pLogfilePath = szLogPath;

            if(freopen(pLogfilePath, "w", stdout) == NULL) {
                THROW(io_error, "Reopening logfile '%s' as stdout failed!", pLogfilePath);
            }
            setbuf(stdout, nullptr);   // No buffering

            if(freopen(pLogfilePath, "w", stderr) == NULL) {
                // use stdout in this error case as stderr is not yet ready
                THROW(io_error, "Reopening logfile '%s' as stderr failed!", pLogfilePath);
            }
            setbuf(stderr, nullptr);   // No buffering

            if(dup2(fileno(stdout), fileno(stderr)) < 0) {
                THROW(io_error, "Redirecting stderr to stdout failed!");
            }

            #else

            int d = open(pLogfilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if(d < 0) {
                THROW(io_error, "Opening logfile '%s' failed!", pLogfilePath);
            }
            // Hint: fileno(stdout) != STDOUT_FILENO on Win32
            if(dup2(d, fileno(stdout)) < 0) {
                THROW(io_error, "Redirecting stdout failed!");
            }

            // Hint: fileno(stderr) != STDERR_FILENO on Win32
            if(dup2(d, fileno(stderr)) < 0) {
                THROW(io_error, "Redirecting stderr failed!");
            }

            #endif
        }

        // Install crash handlers early, after logging is set up
        std::string crashLogPath = getLogFilepath();
        installCrashHandlers(crashLogPath.c_str());

        SDL_Log("Starting DuneCity %s on %s", VERSION, SDL_GetPlatform());

#if defined(__linux__)
        // Verify that required shared libraries are loadable before proceeding.
        // If the binary was installed without bundled .so files and the system
        // libraries are the wrong version or absent, dlopen catches this and
        // logs a human-readable error instead of crashing silently on SDL_Init.
        {
            const char* const requiredLibs[] = {
                "libSDL2-2.0.so.0",
                "libSDL2_mixer-2.0.so.0",
                "libSDL2_ttf-2.0.so.0",
            };
            bool allLibsFound = true;
            for (const char* lib : requiredLibs) {
                // RTLD_NOLOAD checks if already loaded (it is, since we're running);
                // if it returns null anyway, fall back to a full load attempt.
                void* handle = dlopen(lib, RTLD_LAZY | RTLD_NOLOAD);
                if (!handle) {
                    handle = dlopen(lib, RTLD_LAZY);
                }
                if (!handle) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Required library missing or incompatible: %s — %s", lib, dlerror());
                    allLibsFound = false;
                } else {
                    dlclose(handle);
                }
            }
            if (!allLibsFound) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "Cannot start: one or more required shared libraries could not be loaded. "
                    "See log at: %s", getLogFilepath().c_str());
                return EXIT_FAILURE;
            }
        }
#endif

        // First check for missing files
        std::vector<std::string> missingFiles = FileManager::getMissingFiles();

        if(!missingFiles.empty()) {
            // create data directory inside config directory
            char tmp[FILENAME_MAX];
            fnkdat("data/", tmp, FILENAME_MAX, FNKDAT_USER | FNKDAT_CREAT);

            showMissingFilesMessageBox();

            return EXIT_FAILURE;
        }

        bool bExitGame = false;
        bool bFirstInit = true;
        bool bFirstGamestart = false;

        debug = false;

        int currentDisplayIndex = SCREEN_DEFAULT_DISPLAYINDEX;

        do {
            // we do not use rand() but maybe some library does; thus we shall initialize it
            unsigned int seed = (unsigned int) time(nullptr);
            srand(seed);

            // check if configfile exists
            std::string configfilepath = getConfigFilepath();
            if(existsFile(configfilepath) == false) {
                std::string userLanguage = getUserLanguage();
                if(userLanguage.empty()) {
                    userLanguage = "en";
                }

                bFirstGamestart = true;
                createDefaultConfigFile(configfilepath, userLanguage);
            }

            INIFile myINIFile(configfilepath);

            settings.general.playIntro = myINIFile.getBoolValue("General","Play Intro",false);
            settings.general.playerName = myINIFile.getStringValue("General","Player Name","Player");
            settings.general.language = myINIFile.getStringValue("General","Language","en");
            settings.general.scrollSpeed = myINIFile.getIntValue("General","Scroll Speed",50);
            settings.general.showTutorialHints = myINIFile.getBoolValue("General","Show Tutorial Hints",true);
            settings.video.width = myINIFile.getIntValue("Video","Width",640);
            settings.video.height = myINIFile.getIntValue("Video","Height",480);
            settings.video.physicalWidth= myINIFile.getIntValue("Video","Physical Width",640);
            settings.video.physicalHeight = myINIFile.getIntValue("Video","Physical Height",480);
            settings.video.fullscreen = myINIFile.getBoolValue("Video","Fullscreen",false);
            settings.video.frameLimit = myINIFile.getBoolValue("Video","FrameLimit",true);
            settings.video.preferredZoomLevel = myINIFile.getIntValue("Video","Preferred Zoom Level", 0);
            settings.video.scaler = myINIFile.getStringValue("Video","Scaler","ScaleHD");
            settings.video.rotateUnitGraphics = myINIFile.getBoolValue("Video","RotateUnitGraphics",false);
            settings.video.showWatermark = myINIFile.getBoolValue("Video","Show Watermark",true);
            settings.video.cursorScale = myINIFile.getIntValue("Video","Cursor Scale",0);
            settings.audio.musicType = myINIFile.getStringValue("Audio","Music Type","adl");
            settings.audio.playMusic = myINIFile.getBoolValue("Audio","Play Music", true);
            settings.audio.musicVolume = myINIFile.getIntValue("Audio","Music Volume", 64);
            settings.audio.playSFX = myINIFile.getBoolValue("Audio","Play SFX", true);
            settings.audio.sfxVolume = myINIFile.getIntValue("Audio","SFX Volume", 64);
            settings.audio.playCreditsSFX = myINIFile.getBoolValue("Audio","Play Credits SFX", true);

            settings.network.serverPort = myINIFile.getIntValue("Network","ServerPort",DEFAULT_PORT);
            settings.network.metaServer = myINIFile.getStringValue("Network","MetaServer",DEFAULT_METASERVER);
            
            // Migrate old SourceForge metaserver URL to new dunelegacy.com URL
            if(settings.network.metaServer.find("dunelegacy.sourceforge.net") != std::string::npos) {
                SDL_Log("Migrating old SourceForge metaserver URL to dunelegacy.com...");
                size_t pos = settings.network.metaServer.find("dunelegacy.sourceforge.net");
                settings.network.metaServer.replace(pos, strlen("dunelegacy.sourceforge.net"), "dunelegacy.com");
                myINIFile.setStringValue("Network","MetaServer",settings.network.metaServer);
                myINIFile.saveChangesTo(configfilepath);
                SDL_Log("Metaserver URL updated to: %s", settings.network.metaServer.c_str());
            }
            
            // Discord settings
            settings.discord.webhookUrl = myINIFile.getStringValue("Discord","WebhookUrl","");
            
            settings.network.debugNetwork = myINIFile.getBoolValue("Network","Debug Network",false);

            settings.ai.campaignAI = myINIFile.getStringValue("AI","Campaign AI",DEFAULTAIPLAYERCLASS);

            settings.gameOptions.gameSpeed = myINIFile.getIntValue("Game Options","Game Speed",GAMESPEED_DEFAULT);
            settings.gameOptions.concreteRequired = myINIFile.getBoolValue("Game Options","Concrete Required",true);
            settings.gameOptions.structuresDegradeOnConcrete = myINIFile.getBoolValue("Game Options","Structures Degrade On Concrete",true);
            settings.gameOptions.fogOfWar = myINIFile.getBoolValue("Game Options","Fog of War",false);
            settings.gameOptions.startWithExploredMap = myINIFile.getBoolValue("Game Options","Start with Explored Map",false);
            settings.gameOptions.instantBuild = myINIFile.getBoolValue("Game Options","Instant Build",false);
            settings.gameOptions.onlyOnePalace = myINIFile.getBoolValue("Game Options","Only One Palace",false);
            settings.gameOptions.rocketTurretsNeedPower = myINIFile.getBoolValue("Game Options","Rocket-Turrets Need Power",false);
            settings.gameOptions.sandwormsRespawn = myINIFile.getBoolValue("Game Options","Sandworms Respawn",false);
            settings.gameOptions.killedSandwormsDropSpice = myINIFile.getBoolValue("Game Options","Killed Sandworms Drop Spice",false);
            settings.gameOptions.manualCarryallDrops = myINIFile.getBoolValue("Game Options","Manual Carryall Drops",false);
            settings.gameOptions.maximumNumberOfUnitsOverride = myINIFile.getIntValue("Game Options","Maximum Number of Units Override",0);
            settings.gameOptions.maximumNumberOfHarvestersOverride = myINIFile.getIntValue("Game Options","Maximum Number of Harvesters Override",-1);
            settings.gameOptions.immortalHumanPlayer = myINIFile.getBoolValue("Game Options","Immortal Human Player",false);

            pTextManager = std::make_unique<TextManager>();

            missingFiles = FileManager::getMissingFiles();
            if(!missingFiles.empty()) {
                // set back to English
                std::string setBackToEnglishWarning = fmt::sprintf("The following files are missing for language \"%s\":\n",_("LanguageFileExtension"));
                for(const std::string& filename : missingFiles) {
                    setBackToEnglishWarning += filename + "\n";
                }
                setBackToEnglishWarning += "\nLanguage is changed to English!";
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "DuneCity", setBackToEnglishWarning.c_str(), NULL);

                SDL_Log("Warning: Language is changed to English!");

                settings.general.language = "en";
                myINIFile.setStringValue("General","Language",settings.general.language);
                myINIFile.saveChangesTo(configfilepath);

                // reinit text manager
                pTextManager = std::make_unique<TextManager>();
            }

            for(int i=1; i < argc; i++) {
                //check for overiding params
                std::string parameter(argv[i]);

                if((parameter == "-f") || (parameter == "--fullscreen")) {
                    settings.video.fullscreen = true;
                } else if((parameter == "-w") || (parameter == "--window")) {
                    settings.video.fullscreen = false;
                } else if(parameter.compare(0, 13, "--PlayerName=") == 0) {
                    settings.general.playerName = parameter.substr(strlen("--PlayerName="));
                } else if(parameter.compare(0, 13, "--ServerPort=") == 0) {
                    settings.network.serverPort = atol(argv[i] + strlen("--ServerPort="));
                }
            }

            if(bFirstInit == true) {
                SDL_Log("Initializing game...");

                // Force OpenGL rendering on macOS
                SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "1");
                SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
                SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
                SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
                // VSync disabled by default - controlled via renderer flags in setVideoMode()
                SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
                SDL_SetHint(SDL_HINT_VIDEO_X11_FORCE_EGL, "0");  // Disable EGL
                SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");      // Enable render batching
                SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");   // Best line rendering quality

                if(SDL_Init(SDL_INIT_TIMER | SDL_INIT_VIDEO) < 0) {
                    THROW(sdl_error, "Couldn't initialize SDL: %s!", SDL_GetError());
                }

                SDL_version compiledVersion;
                SDL_version linkedVersion;
                SDL_VERSION(&compiledVersion);
                SDL_GetVersion(&linkedVersion);
                SDL_Log("SDL runtime v%d.%d.%d", linkedVersion.major, linkedVersion.minor, linkedVersion.patch);
                SDL_Log("SDL compile-time v%d.%d.%d", compiledVersion.major, compiledVersion.minor, compiledVersion.patch);

                if(TTF_Init() < 0) {
                    THROW(sdl_error, "Couldn't initialize SDL2_ttf: %s!", TTF_GetError());
                }

                SDL_version TTFCompiledVersion;
                SDL_TTF_VERSION(&TTFCompiledVersion);
                const SDL_version* pTTFLinkedVersion = TTF_Linked_Version();
                SDL_Log("SDL2_ttf runtime v%d.%d.%d", pTTFLinkedVersion->major, pTTFLinkedVersion->minor, pTTFLinkedVersion->patch);
                SDL_Log("SDL2_ttf compile-time v%d.%d.%d", TTFCompiledVersion.major, TTFCompiledVersion.minor, TTFCompiledVersion.patch);
            }

            if(bFirstGamestart == true && bFirstInit == true) {
                SDL_DisplayMode displayMode;
                SDL_GetDesktopDisplayMode(currentDisplayIndex, &displayMode);

                int factor = getLogicalToPhysicalResolutionFactor(displayMode.w, displayMode.h);
                settings.video.physicalWidth = displayMode.w;
                settings.video.physicalHeight = displayMode.h;
                settings.video.width = displayMode.w / factor;
                settings.video.height = displayMode.h / factor;
                settings.video.preferredZoomLevel = 1;

                myINIFile.setIntValue("Video","Width",settings.video.width);
                myINIFile.setIntValue("Video","Height",settings.video.height);
                myINIFile.setIntValue("Video","Physical Width",settings.video.physicalWidth);
                myINIFile.setIntValue("Video","Physical Height",settings.video.physicalHeight);
                myINIFile.setIntValue("Video","Preferred Zoom Level",1);

                myINIFile.saveChangesTo(getConfigFilepath());
            }

            Scaler::setDefaultScaler(Scaler::getScalerByName(settings.video.scaler));

            if(bFirstInit == true) {
                SDL_Log("Initializing audio...");
                if( Mix_OpenAudio(AUDIO_FREQUENCY, AUDIO_S16SYS, 2, 1024) < 0 ) {
                    SDL_Quit();
                    THROW(sdl_error, "Couldn't set %d Hz 16-bit audio. Reason: %s!", AUDIO_FREQUENCY, SDL_GetError());
                } else {
                    SDL_Log("%d audio channels were allocated.", Mix_AllocateChannels(28));
                }
            }

            pFileManager = std::make_unique<FileManager>();

            // Initialize the mod system (seeds vanilla mod from install defaults if needed)
            SDL_Log("Initializing mod system...");
            ModManager::instance().initialize();

            // Initialize effective game options (base settings + mod overrides)
            effectiveGameOptions = ModManager::instance().loadEffectiveGameOptions(settings.gameOptions);
            SDL_Log("Effective game options initialized (active mod: %s)", 
                    ModManager::instance().getActiveModName().c_str());

            // Create user config files if they don't exist
            // Check and copy ObjectData.ini
            {
                std::string userObjectDataPath = getObjectDataConfigFilepath();
                if (!existsFile(userObjectDataPath)) {
                    SDL_Log("ObjectData.ini not found in user directory, copying template...");
                    try {
                        if (copyTemplateFile("config/ObjectData.ini.default", userObjectDataPath)) {
                            SDL_Log("ObjectData.ini created successfully at: %s", userObjectDataPath.c_str());
                        } else {
                            SDL_Log("Warning: Failed to create ObjectData.ini");
                        }
                    } catch (std::exception& e) {
                        SDL_Log("Warning: Could not copy ObjectData.ini.default template: %s", e.what());
                    }
                }
            }

            // Check and copy QuantBot Config.ini  
            {
                std::string userQuantBotPath = getQuantBotConfigFilepath();
                if (!existsFile(userQuantBotPath)) {
                    SDL_Log("QuantBot Config.ini not found in user directory, copying template...");
                    try {
                        if (copyTemplateFile("config/QuantBot Config.ini.default", userQuantBotPath)) {
                            SDL_Log("QuantBot Config.ini created successfully at: %s", userQuantBotPath.c_str());
                        } else {
                            SDL_Log("Warning: Failed to create QuantBot Config.ini");
                        }
                    } catch (std::exception& e) {
                        SDL_Log("Warning: Could not copy QuantBot Config.ini.default template: %s", e.what());
                    }
                }
            }

            bool abortIteration = false;

            if(promptToRestoreOutOfSyncConfigurations()) {
                SDL_Log("Default configuration restored. Exiting to allow restart.");
                bExitGame = true;
                abortIteration = true;
            }

            if(!abortIteration) {
                // now we can finish loading texts
                pTextManager->loadData();

                palette = LoadPalette_RW(pFileManager->openFile("IBM.PAL").get());

                SDL_Log("Setting video mode...");
                setVideoMode(currentDisplayIndex);
                
                // Give the renderer time to fully initialize
                SDL_Delay(100);
                
                SDL_RendererInfo rendererInfo;
                SDL_GetRendererInfo(renderer, &rendererInfo);
                SDL_Log("Renderer: %s (max texture size: %dx%d)", rendererInfo.name, rendererInfo.max_texture_width, rendererInfo.max_texture_height);

                // Verify renderer is valid before proceeding
                if(renderer == nullptr) {
                    SDL_Log("Error: Renderer is null after setVideoMode!");
                    THROW(std::runtime_error, "Failed to create renderer during video mode initialization");
                }

                SDL_Log("Loading fonts...");
                pFontManager = std::make_unique<FontManager>();

                SDL_Log("Loading graphics and sounds...");

#ifdef HAS_ASYNC
                auto gfxManagerFut = std::async(std::launch::async, []() { return std::make_unique<GFXManager>(); } );
                auto sfxManagerFut = std::async(std::launch::async, []() { return std::make_unique<SFXManager>(); } );

                pGFXManager = gfxManagerFut.get();
                pSFXManager = sfxManagerFut.get();
#else
                // g++ does not provide std::launch::async on all platforms
                pGFXManager = std::make_unique<GFXManager>();
                pSFXManager = std::make_unique<SFXManager>();
#endif

                GUIStyle::setGUIStyle(std::make_unique<DuneStyle>());

                if(bFirstInit == true) {
                    SDL_Log("Starting sound player...");
                    soundPlayer = std::make_unique<SoundPlayer>();

                    if(settings.audio.musicType == "directory") {
                        SDL_Log("Starting directory music player...");
                        musicPlayer = std::make_unique<DirectoryPlayer>();
                    } else if(settings.audio.musicType == "adl") {
                        SDL_Log("Starting ADL music player...");
                        musicPlayer = std::make_unique<ADLPlayer>();
                    } else if(settings.audio.musicType == "xmi") {
                        SDL_Log("Starting XMI music player...");
                        musicPlayer = std::make_unique<XMIPlayer>();
                    } else {
                        THROW(std::runtime_error, "Invalid music type: '%'", settings.audio.musicType);
                    }

                    //musicPlayer->changeMusic(MUSIC_INTRO);
                }

                // Playing intro
                if(((bFirstGamestart == true) || (settings.general.playIntro == true)) && (bFirstInit==true)) {
                    SDL_Log("Playing intro...");
                    Intro().run();
                }

                bFirstInit = false;

                // Re-enable cursor for main menu (fixes Windows cursor visibility issue)
                SDL_ShowCursor(SDL_ENABLE);

                // Initialize Discord Rich Presence
                DiscordManager::instance().initialize();
                if (!settings.discord.webhookUrl.empty()) {
                    DiscordManager::instance().setWebhookUrl(settings.discord.webhookUrl);
                    SDL_Log("Discord webhook configured");
                }

                SDL_Log("Starting main menu...");
                { // Scope
                    int menuResult = MainMenu().showMenu();
                    if (menuResult == MENU_QUIT_DEFAULT) {
                        bExitGame = true;
                    } else if (menuResult == MENU_QUIT_REINITIALIZE) {
                        // Reinitialize video mode and continue the loop
                        SDL_Log("Reinitializing video mode...");
                        // The loop will continue and reinitialize everything
                    }
                }
            }

            SDL_Log("Deinitialize...");

            GUIStyle::destroyGUIStyle();

            // clear everything
            if(bExitGame == true) {
                musicPlayer.reset();
                soundPlayer.reset();
                Mix_HaltMusic();
                Mix_CloseAudio();
            } else {
                // save the current display index for later reuse
                if(window != nullptr) {
                    currentDisplayIndex = SDL_GetWindowDisplayIndex(window);
                    // Safety check: ensure we got a valid display index
                    if(currentDisplayIndex < 0) {
                        currentDisplayIndex = 0; // Fallback to primary display
                    }
                }
            }

            pTextManager.reset();
            pSFXManager.reset();
            pGFXManager.reset();
            pFontManager.reset();
            pFileManager.reset();

            // Safely destroy SDL objects
            if(screenTexture != nullptr) {
                SDL_DestroyTexture(screenTexture);
                screenTexture = nullptr;
            }
            if(renderer != nullptr) {
                SDL_DestroyRenderer(renderer);
                renderer = nullptr;
            }
            if(window != nullptr) {
                SDL_DestroyWindow(window);
                window = nullptr;
            }

            if(bExitGame == true) {
                DiscordManager::instance().shutdown();
                TTF_Quit();
                SDL_Quit();
            }
            SDL_Log("Deinitialization finished!");
        } while(bExitGame == false);

        // deinit fnkdat
        if(fnkdat(nullptr, nullptr, 0, FNKDAT_UNINIT) < 0) {
            THROW(std::runtime_error, "Cannot uninitialize fnkdat!");
        }
    } catch(const std::exception& e) {
        std::string message = std::string("An unhandled exception of type \'") + demangleSymbol(typeid(e).name()) + std::string("\' was thrown:\n\n") + e.what() + std::string("\n\nDuneCity will now be terminated!");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DuneCity: Unrecoverable error", message.c_str(), nullptr);

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

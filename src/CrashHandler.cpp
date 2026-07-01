#include "CrashHandler.h"
#include "config.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>

#ifdef _WIN32
    #include <windows.h>
    #include <dbghelp.h>
    #include <shellapi.h>
    #pragma comment(lib, "dbghelp.lib")
    #pragma comment(lib, "shlwapi.lib")
#else
    #include <execinfo.h>  // For backtrace (POSIX)
    #include <unistd.h>
#endif

#include <SDL.h>

// File-scope state for crash handler (must be POD types for signal safety)
static FILE* crashLogFile = nullptr;
static const char* crashLogPath = nullptr;
static void* registeredGame = nullptr;

/**
 * Get current timestamp as string
 * Signal-safe: uses static buffer, no allocations
 */
static const char* getTimeStamp() {
    static char buffer[64];
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return buffer;
}

/**
 * Write raw string to a file (no formatting)
 */
static void writeCrashLogRaw(FILE* f, const char* str) {
    if(!f) return;
    fputs(str, f);
    fflush(f);
}

/**
 * Write to crash log (signal-safe)
 */
static void writeCrashLog(const char* format, ...) {
    if(!crashLogFile) return;
    
    va_list args;
    va_start(args, format);
    vfprintf(crashLogFile, format, args);
    va_end(args);
    fflush(crashLogFile);
}

/**
 * Get signal name as string
 */
static const char* getSignalName(int signal) {
    switch(signal) {
        case SIGSEGV: return "SIGSEGV (Segmentation fault)";
        case SIGABRT: return "SIGABRT (Abort)";
        case SIGFPE:  return "SIGFPE (Floating point exception)";
        case SIGILL:  return "SIGILL (Illegal instruction)";
#ifndef _WIN32
        case SIGBUS:  return "SIGBUS (Bus error)";
        case SIGPIPE: return "SIGPIPE (Broken pipe)";
#endif
        case SIGTERM: return "SIGTERM (Termination request)";
        default:      return "Unknown signal";
    }
}

/**
 * Write game state to crash log
 * This is a weak implementation that can be overridden by Game class
 */
void writeCrashGameState() {
    if(!registeredGame) {
        writeCrashLog("Game state: Not available (game not initialized)\n");
        return;
    }
    
    // Forward declaration - Game.cpp will provide the actual implementation
    // For now, just log that game exists
    writeCrashLog("Game state: Available (ptr=%p)\n", registeredGame);
    
    // TODO: Cast to Game* and extract:
    // - Game mode (campaign, custom, multiplayer)
    // - Game cycle count
    // - Number of players
    // - Map name
    // - Unit count
    // - Structure count
    // - Whether multiplayer
}

/**
 * Signal handler - called when a crash occurs
 * 
 * IMPORTANT: This function must be signal-safe:
 * - No malloc/new
 * - No C++ exceptions
 * - Minimal library calls
 * - No complex data structures
 */
static void signalHandler(int sig) {
    // Prevent recursive crashes
    static volatile sig_atomic_t in_handler = 0;
    if(in_handler) {
        _exit(128 + sig);
    }
    in_handler = 1;
    
    writeCrashLog("\n");
    writeCrashLog("========================================\n");
    writeCrashLog("CRASH DETECTED\n");
    writeCrashLog("========================================\n");
    writeCrashLog("Signal: %d (%s)\n", sig, getSignalName(sig));
    writeCrashLog("Time: %s\n", getTimeStamp());
    writeCrashLog("Version: %s\n", VERSION);
    writeCrashLog("Platform: %s\n", SDL_GetPlatform());
    writeCrashLog("\n");
    
    // Write game state if available
    writeCrashGameState();
    writeCrashLog("\n");
    
#ifdef _WIN32
    // Windows: Get stack trace using CaptureStackBackTrace
    writeCrashLog("Stack Trace (Windows):\n");
    
    void* stack[64];
    WORD frames = CaptureStackBackTrace(0, 64, stack, NULL);
    
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);
    
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
    if(symbol) {
        symbol->MaxNameLen = 255;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        
        for(WORD i = 0; i < frames; i++) {
            DWORD64 address = (DWORD64)(stack[i]);
            
            if(SymFromAddr(process, address, 0, symbol)) {
                writeCrashLog("  [%d] 0x%016llX %s\n", i, address, symbol->Name);
            } else {
                writeCrashLog("  [%d] 0x%016llX <unknown>\n", i, address);
            }
        }
        
        free(symbol);
    }
    
    SymCleanup(process);
    
#else
    // POSIX (macOS/Linux): Get stack trace using backtrace
    writeCrashLog("Stack Trace:\n");
    
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    char** symbols = backtrace_symbols(callstack, frames);
    
    if(symbols) {
        for(int i = 0; i < frames; i++) {
            writeCrashLog("  [%d] %s\n", i, symbols[i]);
        }
        free(symbols);
    } else {
        writeCrashLog("  (Unable to get stack trace)\n");
    }
#endif
    
    writeCrashLog("\n");
    writeCrashLog("========================================\n");
    writeCrashLog("Crash report saved to:\n");
    writeCrashLog("%s\n", crashLogPath ? crashLogPath : "(unknown location)");
    writeCrashLog("\n");
    writeCrashLog("Please report this crash:\n");
    writeCrashLog("- GitHub: https://github.com/henricj/dunelegacy/issues\n");
    writeCrashLog("- Forums: https://forum.dune2k.com/\n");
    writeCrashLog("========================================\n");
    writeCrashLog("\n");
    
    if(crashLogFile) {
        fclose(crashLogFile);
        crashLogFile = nullptr;
    }
    
    // Show message to user
    char message[512];
    snprintf(message, sizeof(message),
        "Dune City has crashed unexpectedly.\n\n"
        "A crash report has been saved to:\n"
        "%s\n\n"
        "Please report this bug on GitHub or the forums.\n"
        "Include the crash report to help fix the issue.",
        crashLogPath ? crashLogPath : "Dune City.log");
    
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        "Dune City - Fatal Error",
        message,
        nullptr
    );
    
    // Restore default handler and re-raise signal
    // This allows the OS to generate a core dump if enabled
    signal(sig, SIG_DFL);
    raise(sig);
}

static std::terminate_handler prevTerminate = nullptr;

static void terminateHandler() noexcept {
    FILE* f = (crashLogFile != nullptr) ? crashLogFile : stderr;
    writeCrashLogRaw(f, "\n========================================\n");
    writeCrashLogRaw(f, "UNCAUGHT EXCEPTION (std::terminate)\n");
    writeCrashLogRaw(f, "========================================\n");
    writeCrashLogRaw(f, "An uncaught C++ exception triggered std::terminate.\n");
    writeCrashLogRaw(f, "This is likely an allocation failure (std::bad_alloc)\n");
    writeCrashLogRaw(f, "or an exception escaping a noexcept function.\n");
    writeCrashLogRaw(f, "\n");
#ifdef _WIN32
    // Write a minidump before abort
    WriteMiniDump(nullptr);
#endif
    if(crashLogFile && crashLogFile != stderr) {
        fflush(crashLogFile);
        fclose(crashLogFile);
        crashLogFile = nullptr;
    }
    std::abort();
}

#ifdef _WIN32
// Vectored Exception Handler - catches ALL Windows exceptions before CRT
static LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo) {
    if(!ExceptionInfo || !ExceptionInfo->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    DWORD code = ExceptionInfo->ExceptionRecord->ExceptionCode;
    
    // Ignore debug exceptions
    if(code == EXCEPTION_BREAKPOINT || code == 0x406D1388) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    
    // Open log if not already open
    FILE* f = crashLogFile;
    if(!f) {
        f = fopen(crashLogPath, "a");
        if(!f) f = stderr;
    }
    
    writeCrashLogRaw(f, "\n========================================\n");
    writeCrashLogRaw(f, "CRASH DETECTED (Windows Exception)\n");
    writeCrashLogRaw(f, "========================================\n");
    
    char buf[256];
    snprintf(buf, sizeof(buf), "Exception Code: 0x%08X at 0x%p\n", code, ExceptionInfo->ExceptionRecord->ExceptionAddress);
    writeCrashLogRaw(f, buf);
    writeCrashLogRaw(f, "\n");
    
    // Write minidump
    WriteMiniDump(ExceptionInfo);
    
    // Stack trace
    writeCrashLogRaw(f, "Stack Trace:\n");
    void* stack[64];
    WORD frames = CaptureStackBackTrace(0, 64, stack, NULL);
    for(WORD i = 0; i < frames; i++) {
        snprintf(buf, sizeof(buf), "  [%d] 0x%p\n", i, stack[i]);
        writeCrashLogRaw(f, buf);
    }
    writeCrashLogRaw(f, "========================================\n\n");
    
    if(f != stderr) {
        fflush(f);
        fclose(f);
    }
    
    return EXCEPTION_CONTINUE_SEARCH;  // Let the process crash normally
}

static PVOID vectoredHandlerHandle = nullptr;
#endif

/**
 * Install crash handlers for all common crash signals
 */
void installCrashHandlers(const char* logPath) {
    if(!logPath) {
        fprintf(stderr, "Warning: installCrashHandlers called with NULL logPath\n");
        return;
    }
    
    crashLogPath = logPath;
    
    // Open log file for crash reporting (append mode)
    crashLogFile = fopen(logPath, "a");
    if(!crashLogFile) {
        fprintf(stderr, "Warning: Could not open crash log file: %s\n", logPath);
        fprintf(stderr, "Crash reports will be sent to stderr instead\n");
        crashLogFile = stderr;
    }
    
    // Install handlers for common crash signals
    signal(SIGSEGV, signalHandler);  // Segmentation fault
    signal(SIGABRT, signalHandler);  // Abort
    signal(SIGFPE,  signalHandler);  // Floating point exception
    signal(SIGILL,  signalHandler);  // Illegal instruction
    
#ifndef _WIN32
    signal(SIGBUS,  signalHandler);  // Bus error (POSIX)
    signal(SIGPIPE, SIG_IGN);        // Ignore broken pipe (POSIX)
#else
    signal(SIGTERM, signalHandler);  // Termination request (Windows)
#endif
    
    prevTerminate = std::set_terminate(terminateHandler);

#ifdef _WIN32
    // Install vectored exception handler (runs BEFORE std::terminate)
    vectoredHandlerHandle = AddVectoredExceptionHandler(1, VectoredExceptionHandler);
#endif

    SDL_Log("Crash handlers installed (log: %s)", logPath);
}

/**
 * Register game instance for crash reporting
 */
void registerGameForCrashReporting(void* game) {
    registeredGame = game;
    SDL_Log("Game instance registered for crash reporting");
}

#ifdef _WIN32
// Write a minidump to logs folder
static void WriteMiniDump(EXCEPTION_POINTERS* ExceptionInfo) {
    char dumpPath[MAX_PATH];
    char appDataPath[MAX_PATH];
    
    // Get AppData/Roaming path
    if(FAILED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appDataPath))) {
        return;
    }
    
    // Create DuneCity folder
    char dumpDir[MAX_PATH];
    snprintf(dumpDir, sizeof(dumpDir), "%s\\DuneCity", appDataPath);
    CreateDirectoryA(dumpDir, NULL);
    
    // Create minidump filename with timestamp
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    
    snprintf(dumpPath, sizeof(dumpPath), "%s\\crash_%04d%02d%02d_%02d%02d%02d.dmp",
             dumpDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    HANDLE hFile = CreateFileA(dumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if(hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    
    MINIDUMP_EXCEPTION_INFORMATION mdei = {};
    mdei.ThreadId = GetCurrentThreadId();
    mdei.ExceptionPointers = ExceptionInfo;
    mdei.ClientPointers = TRUE;
    
    MINIDUMP_TYPE dumpType = MiniDumpNormal;
    
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType,
                      &mdei, NULL, NULL);
    
    CloseHandle(hFile);
    
    // Log the dump path
    FILE* f = crashLogFile;
    if(!f) f = stderr;
    
    char msg[MAX_PATH + 64];
    snprintf(msg, sizeof(msg), "Minidump saved to: %s\n", dumpPath);
    writeCrashLogRaw(f, msg);
}
#endif


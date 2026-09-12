#include <misc/WebRuntime.h>
#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

namespace {

EM_ASYNC_JS(int, copyBrowserText, (const char* text), {
    const value = UTF8ToString(text);
    try {
        if (!navigator.clipboard || !navigator.clipboard.writeText) return 0;
        // Bound the wait so a browser permission prompt cannot stall the lobby indefinitely.
        return await Promise.race([
            navigator.clipboard.writeText(value).then(() => 1, () => 0),
            new Promise(resolve => setTimeout(() => resolve(0), 2000))
        ]);
    } catch (_) {
        return 0;
    }
});

EM_JS(void, syncBrowserFileSystem, (), {
    if (typeof Module.requestPersistentSync === 'function') {
        Module.requestPersistentSync();
    }
});

EM_JS(void, markBrowserGameReady, (), {
    if (typeof Module.markGameReady === 'function') {
        Module.markGameReady();
    }
});

}
#endif

bool WebRuntime::copyText(const std::string& text) {
#ifdef __EMSCRIPTEN__
    return copyBrowserText(text.c_str()) != 0;
#else
    return SDL_SetClipboardText(text.c_str()) == 0;
#endif
}

void WebRuntime::yieldToBrowser() {
#ifdef __EMSCRIPTEN__
    emscripten_sleep(0);
#endif
}

void WebRuntime::markGameReady() {
#ifdef __EMSCRIPTEN__
    markBrowserGameReady();
#endif
}

void WebRuntime::syncPersistentFiles() {
#ifdef __EMSCRIPTEN__
    syncBrowserFileSystem();
#endif
}

int WebRuntime::defaultVideoWidth() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({ return Module.defaultVideoSize().width; });
#else
    return 1280;
#endif
}

int WebRuntime::defaultVideoHeight() {
#ifdef __EMSCRIPTEN__
    return EM_ASM_INT({ return Module.defaultVideoSize().height; });
#else
    return 720;
#endif
}

void WebRuntime::reportMatchStats(const std::string& phase, const std::string& matchID, const std::string& payload) {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        Module.reportMatchStats(UTF8ToString($0), UTF8ToString($1), UTF8ToString($2));
    }, phase.c_str(), matchID.c_str(), payload.c_str());
#endif
}

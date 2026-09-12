#include <misc/WebRuntime.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

namespace {

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

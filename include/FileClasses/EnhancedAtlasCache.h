#ifndef ENHANCEDATLAS_CACHE_H
#define ENHANCEDATLAS_CACHE_H

#include <misc/SDL2pp.h>

#include <cstdint>
#include <future>
#include <map>
#include <string>

// Native PNG decoding runs one page at a time off the render thread. SDL texture
// creation and eviction stay on the caller's render thread.
class EnhancedAtlasCache {
public:
    explicit EnhancedAtlasCache(SDL_Renderer* renderer, uint64_t byteLimit = 192ull * 1024 * 1024);
    SDL_Texture* request(const std::string& path, int width, int height);
    void clear();
    uint64_t residentBytes() const { return bytes; }

private:
    struct Entry {
        sdl2::texture_ptr texture;
        int width = 0;
        int height = 0;
        uint64_t lastUse = 0;
        bool failed = false;
    };
    struct Decoded {
        sdl2::surface_ptr surface;
        std::string error;
    };
    static Decoded decode(const std::string& path, int width, int height);
    void complete();

    SDL_Renderer* renderer;
    uint64_t limit;
    uint64_t bytes = 0;
    uint64_t clock = 0;
    unsigned generation = 0;
    unsigned pendingGeneration = 0;
    std::map<std::string, Entry> entries;
    std::string pendingPath;
    // Declared last so destruction joins the decoder before the other state dies.
    std::future<Decoded> pending;
};

#endif

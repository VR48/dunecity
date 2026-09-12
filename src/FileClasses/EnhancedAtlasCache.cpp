#include <FileClasses/EnhancedAtlasCache.h>
#include <FileClasses/lodepng.h>

#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <vector>

EnhancedAtlasCache::EnhancedAtlasCache(SDL_Renderer* renderer, uint64_t byteLimit)
    : renderer(renderer), limit(byteLimit) {}

EnhancedAtlasCache::Decoded EnhancedAtlasCache::decode(const std::string& path, int width, int height) {
    try {
        auto input = sdl2::RWops_ptr{SDL_RWFromFile(path.c_str(), "rb")};
        if(!input) {
            return {nullptr, SDL_GetError()};
        }
        std::array<Uint8, 24> header{};
        const Uint8 signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        if(SDL_RWread(input.get(), header.data(), 1, header.size()) != header.size()
           || std::memcmp(header.data(), signature, sizeof(signature)) != 0
           || std::memcmp(header.data() + 12, "IHDR", 4) != 0) {
            return {nullptr, "invalid PNG header"};
        }
        auto read32 = [&](int offset) {
            Uint32 value;
            std::memcpy(&value, header.data() + offset, sizeof(value));
            return SDL_SwapBE32(value);
        };
        const auto actualWidth = read32(16), actualHeight = read32(20);
        if(actualWidth != static_cast<Uint32>(width) || actualHeight != static_cast<Uint32>(height)) {
            return {nullptr, "dimensions " + std::to_string(actualWidth) + "x"
                + std::to_string(actualHeight) + "; manifest expects "
                + std::to_string(width) + "x" + std::to_string(height)};
        }
        SDL_RWseek(input.get(), 0, RW_SEEK_SET);
        const auto size = SDL_RWsize(input.get());
        if(size <= 0 || size > 64 * 1024 * 1024) {
            return {nullptr, "PNG file exceeds the page input limit"};
        }
        std::vector<unsigned char> encoded(static_cast<size_t>(size));
        if(SDL_RWread(input.get(), encoded.data(), 1, encoded.size()) != encoded.size()) {
            return {nullptr, "PNG read failed"};
        }
        unsigned char* raw = nullptr;
        unsigned decodedWidth = 0, decodedHeight = 0;
        const unsigned error = lodepng_decode32(&raw, &decodedWidth, &decodedHeight, encoded.data(), encoded.size());
        std::unique_ptr<unsigned char, decltype(&std::free)> pixels(raw, std::free);
        if(error != 0) {
            return {nullptr, lodepng_error_text(error)};
        }
        auto surface = sdl2::surface_ptr{SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32)};
        if(!surface) {
            return {nullptr, SDL_GetError()};
        }
        for(int y = 0; y < height; ++y) {
            std::memcpy(static_cast<Uint8*>(surface->pixels) + y * surface->pitch,
                        pixels.get() + size_t(y) * width * 4, size_t(width) * 4);
        }
        return {std::move(surface), {}};
    } catch(const std::exception& error) {
        return {nullptr, error.what()};
    }
}

void EnhancedAtlasCache::complete() {
    if(!pending.valid()) {
        return;
    }
    const auto status = pending.wait_for(std::chrono::seconds(0));
    if(status == std::future_status::timeout) {
        return;
    }
    auto decoded = pending.get();
    if(pendingGeneration != generation) {
        return;
    }
    auto found = entries.find(pendingPath);
    if(found == entries.end()) {
        return;
    }
    auto& entry = found->second;
    if(!decoded.surface) {
        entry.failed = true;
        SDL_Log("Dune2R atlas rejected: %s (%s)", pendingPath.c_str(), decoded.error.c_str());
        return;
    }

    const uint64_t required = uint64_t(entry.width) * entry.height * 4;
    while(bytes + required > limit) {
        auto oldest = entries.end();
        for(auto it = entries.begin(); it != entries.end(); ++it) {
            if(it->second.texture && (oldest == entries.end() || it->second.lastUse < oldest->second.lastUse)) {
                oldest = it;
            }
        }
        if(oldest == entries.end()) {
            break;
        }
        bytes -= uint64_t(oldest->second.width) * oldest->second.height * 4;
        oldest->second.texture.reset();
    }
    entry.texture.reset(SDL_CreateTextureFromSurface(renderer, decoded.surface.get()));
    if(!entry.texture) {
        entry.failed = true;
        SDL_Log("Dune2R atlas upload failed: %s (%s)", pendingPath.c_str(), SDL_GetError());
        return;
    }
    SDL_SetTextureBlendMode(entry.texture.get(), SDL_BLENDMODE_BLEND);
    entry.lastUse = ++clock;
    bytes += required;
}

SDL_Texture* EnhancedAtlasCache::request(const std::string& path, int width, int height) {
    complete();
    auto& entry = entries[path];
    if(entry.width == 0) {
        entry.width = width;
        entry.height = height;
        SDL_RendererInfo info{};
        SDL_GetRendererInfo(renderer, &info);
        if(width <= 0 || height <= 0 || uint64_t(width) * height * 4 > limit
           || (info.max_texture_width > 0 && width > info.max_texture_width)
           || (info.max_texture_height > 0 && height > info.max_texture_height)) {
            entry.failed = true;
            SDL_Log("Dune2R atlas exceeds texture budget or renderer limits: %s (%dx%d)", path.c_str(), width, height);
        }
    }
    if(entry.texture) {
        entry.lastUse = ++clock;
        return entry.texture.get();
    }
    if(!entry.failed && !pending.valid()) {
        pendingPath = path;
        pendingGeneration = generation;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
        // The single-threaded browser build cannot create a native worker.
        constexpr auto policy = std::launch::deferred;
#else
        constexpr auto policy = std::launch::async;
#endif
        try {
            pending = std::async(policy, [path, width, height] { return decode(path, width, height); });
        } catch(const std::exception& error) {
            entry.failed = true;
            SDL_Log("Dune2R atlas worker failed: %s (%s)", path.c_str(), error.what());
        }
    }
    return nullptr;
}

void EnhancedAtlasCache::clear() {
    // A worker owns only its filename and surface. Ignore its stale result
    // without blocking a mod switch or retaining any renderer texture.
    ++generation;
    entries.clear();
    bytes = 0;
}

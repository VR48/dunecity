#include <SDL.h>
#include <SDL_mixer.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

namespace {
constexpr int kVocSampleRate = 11025;

void writeLe16(std::ofstream& out, std::uint16_t value) {
    const unsigned char bytes[2] = {
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff)
    };
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeLe24(std::ofstream& out, std::uint32_t value) {
    const unsigned char bytes[3] = {
        static_cast<unsigned char>(value & 0xff),
        static_cast<unsigned char>((value >> 8) & 0xff),
        static_cast<unsigned char>((value >> 16) & 0xff)
    };
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

bool writeVoc(const std::string& outputPath, const Mix_Chunk* chunk) {
    if(chunk == nullptr || chunk->abuf == nullptr || chunk->alen == 0) {
        std::cerr << "No decoded audio data.\n";
        return false;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if(!out) {
        std::cerr << "Cannot open output: " << outputPath << "\n";
        return false;
    }

    constexpr std::uint16_t version = 0x010a;
    constexpr std::uint16_t checksum = static_cast<std::uint16_t>((~version + 0x1234) & 0xffff);
    constexpr unsigned char timeConstant = 0xa5;
    constexpr unsigned char packing = 0;

    out.write("Creative Voice File", 19);
    out.put(0x1a);
    writeLe16(out, 0x001a);
    writeLe16(out, version);
    writeLe16(out, checksum);

    out.put(1);
    writeLe24(out, chunk->alen + 2);
    out.put(static_cast<char>(timeConstant));
    out.put(static_cast<char>(packing));
    out.write(reinterpret_cast<const char*>(chunk->abuf), chunk->alen);
    out.put(0);

    return static_cast<bool>(out);
}
}

int main(int argc, char* argv[]) {
    if(argc != 3) {
        std::cerr << "Usage: ogg_to_voc <input.ogg|input.wav> <output.voc>\n";
        return 2;
    }

    SDL_SetMainReady();
    if(SDL_Init(SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    const int mixInitFlags = MIX_INIT_OGG;
    if((Mix_Init(mixInitFlags) & mixInitFlags) != mixInitFlags) {
        std::cerr << "Mix_Init OGG failed: " << Mix_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    if(Mix_OpenAudio(kVocSampleRate, AUDIO_U8, 1, 1024) != 0) {
        std::cerr << "Mix_OpenAudio failed: " << Mix_GetError() << "\n";
        Mix_Quit();
        SDL_Quit();
        return 1;
    }

    Mix_Chunk* chunk = Mix_LoadWAV(argv[1]);
    if(chunk == nullptr) {
        std::cerr << "Mix_LoadWAV failed: " << Mix_GetError() << "\n";
        Mix_CloseAudio();
        Mix_Quit();
        SDL_Quit();
        return 1;
    }

    const bool ok = writeVoc(argv[2], chunk);

    Mix_FreeChunk(chunk);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();

    return ok ? 0 : 1;
}

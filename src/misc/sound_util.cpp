
#include <misc/sound_util.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/Vocfile.h>

#include <misc/exceptions.h>
#include <misc/SDL2pp.h>

#include <globals.h>

#include <SDL_mixer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <exception>
#include <string>

sdl2::mix_chunk_ptr create_chunk()
{
    return sdl2::mix_chunk_ptr{ static_cast<Mix_Chunk*>(SDL_malloc(sizeof(Mix_Chunk))) };
}

namespace {
sdl2::mix_chunk_ptr cloneChunk(Mix_Chunk* sound) {
    auto returnChunk = create_chunk();
    if(returnChunk == nullptr) {
        return nullptr;
    }

    returnChunk->allocated = 1;
    returnChunk->volume = sound ? sound->volume : MIX_MAX_VOLUME;
    returnChunk->alen = sound ? sound->alen : 0;

    if(returnChunk->alen == 0) {
        returnChunk->abuf = nullptr;
        return returnChunk;
    }

    sdl2::sdl_ptr<Uint8> buffer{ static_cast<Uint8*>(SDL_malloc(returnChunk->alen)) };
    if(buffer == nullptr) {
        return nullptr;
    }

    std::memcpy(buffer.get(), sound->abuf, returnChunk->alen);
    returnChunk->abuf = buffer.release();
    return returnChunk;
}

int bytesPerSample(Uint16 format) {
    switch(format) {
        case AUDIO_S16LSB:
        case AUDIO_S16MSB:
            return 2;
        default:
            return 0;
    }
}

Sint16 readS16Sample(const Uint8* data, Uint16 format) {
    Uint16 raw = 0;
    std::memcpy(&raw, data, sizeof(raw));

    if(format == AUDIO_S16LSB) {
        raw = SDL_SwapLE16(raw);
    } else if(format == AUDIO_S16MSB) {
        raw = SDL_SwapBE16(raw);
    }

    return static_cast<Sint16>(raw);
}

void writeS16Sample(Uint8* data, Uint16 format, Sint16 value) {
    Uint16 raw = static_cast<Uint16>(value);

    if(format == AUDIO_S16LSB) {
        raw = SDL_SwapLE16(raw);
    } else if(format == AUDIO_S16MSB) {
        raw = SDL_SwapBE16(raw);
    }

    std::memcpy(data, &raw, sizeof(raw));
}
}

sdl2::mix_chunk_ptr concat2Chunks(Mix_Chunk* sound1, Mix_Chunk* sound2)
{
    auto returnChunk = create_chunk();
    if(returnChunk == nullptr) {
        return nullptr;
    }

    returnChunk->allocated = 1;
    returnChunk->volume = sound1->volume;
    returnChunk->alen = sound1->alen + sound2->alen;

    sdl2::sdl_ptr<Uint8> buffer{ static_cast<Uint8 *>(SDL_malloc(returnChunk->alen)) };
    if (buffer == nullptr) {
        return nullptr;
    }

    auto p = buffer.get();

    memcpy(p, sound1->abuf, sound1->alen);
    p += sound1->alen;
    memcpy(p, sound2->abuf, sound2->alen);

    returnChunk->abuf = buffer.release();

    return returnChunk;
}

sdl2::mix_chunk_ptr concat3Chunks(Mix_Chunk* sound1, Mix_Chunk* sound2, Mix_Chunk* sound3)
{
    auto returnChunk = create_chunk();
    if(returnChunk == nullptr) {
        return nullptr;
    }

    returnChunk->allocated = 1;
    returnChunk->volume = sound1->volume;
    returnChunk->alen = sound1->alen + sound2->alen + sound3->alen;

    sdl2::sdl_ptr<Uint8> buffer{ static_cast<Uint8 *>(SDL_malloc(returnChunk->alen)) };
    if(buffer == nullptr) {
        return nullptr;
    }

    auto p = buffer.get();

    memcpy(p, sound1->abuf, sound1->alen);
    p += sound1->alen;
    memcpy(p, sound2->abuf, sound2->alen);
    p += sound2->alen;
    memcpy(p, sound3->abuf, sound3->alen);

    returnChunk->abuf = buffer.release();

    return returnChunk;
}

sdl2::mix_chunk_ptr concat4Chunks(Mix_Chunk* sound1, Mix_Chunk* sound2, Mix_Chunk* sound3, Mix_Chunk* sound4)
{
    auto returnChunk = create_chunk();
    if (returnChunk == nullptr) {
        return nullptr;
    }

    returnChunk->allocated = 1;
    returnChunk->volume = sound1->volume;
    returnChunk->alen = sound1->alen + sound2->alen + sound3->alen + sound4->alen;

    sdl2::sdl_ptr<Uint8> buffer{ static_cast<Uint8 *>(SDL_malloc(returnChunk->alen)) };
    if (buffer == nullptr) {
        return nullptr;
    }

    auto p = buffer.get();

    memcpy(p, sound1->abuf, sound1->alen);
    p += sound1->alen;
    memcpy(p, sound2->abuf, sound2->alen);
    p += sound2->alen;
    memcpy(p, sound3->abuf, sound3->alen);
    p += sound3->alen;
    memcpy(p, sound4->abuf, sound4->alen);

    returnChunk->abuf = buffer.release();

    return returnChunk;
}

sdl2::mix_chunk_ptr createEmptyChunk()
{
    auto returnChunk = create_chunk();
    if (returnChunk == nullptr) {
        return nullptr;
    }

    returnChunk->allocated = 1;
    returnChunk->volume = 0;
    returnChunk->alen = 0;
    returnChunk->abuf = nullptr;

    return returnChunk;
}

sdl2::mix_chunk_ptr createSilenceChunk(int length)
{
    auto returnChunk = create_chunk();
    if (returnChunk == nullptr) {
        return nullptr;
    }

    returnChunk->allocated = 1;
    returnChunk->volume = MIX_MAX_VOLUME;
    returnChunk->alen = length;

    sdl2::sdl_ptr<Uint8> buffer{ static_cast<Uint8 *>(SDL_calloc(returnChunk->alen, 1)) };
    if (buffer == nullptr) {
        return nullptr;
    }

    returnChunk->abuf = buffer.release();

    return returnChunk;
}

sdl2::mix_chunk_ptr getChunkFromFile(const std::string& filename) {
    auto returnChunk = LoadVOC_RW(pFileManager->openFile(filename).get());
    if(returnChunk == nullptr) {
        THROW(io_error, "Cannot load '%s'!", filename);
    }

    return returnChunk;
}

sdl2::mix_chunk_ptr getChunkFromFile(const std::string& filename, const std::string& alternativeFilename) {
    if(pFileManager->exists(filename)) {
        try {
            return getChunkFromFile(filename);
        } catch(const std::exception& e) {
            SDL_Log("Cannot load '%s' (%s), trying '%s' instead", filename.c_str(), e.what(), alternativeFilename.c_str());
        }
    }

    if(pFileManager->exists(alternativeFilename)) {
        return getChunkFromFile(alternativeFilename);
    }

    THROW(io_error, "Cannot open '%s' or '%s'!", filename, alternativeFilename);
    return nullptr;
}

sdl2::mix_chunk_ptr getAudioChunkFromFile(const std::string& filename) {
    std::string lowerFilename = filename;
    std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if(lowerFilename.size() >= 4 && lowerFilename.substr(lowerFilename.size() - 4) == ".ogg") {
        if((Mix_Init(MIX_INIT_OGG) & MIX_INIT_OGG) == 0) {
            THROW(io_error, "Cannot initialize OGG support for '%s': %s!", filename, Mix_GetError());
        }
    }

    auto rwop = pFileManager->openFile(filename);
    auto returnChunk = sdl2::mix_chunk_ptr{ Mix_LoadWAV_RW(rwop.release(), 1) };
    if(returnChunk == nullptr) {
        THROW(io_error, "Cannot load '%s': %s!", filename, Mix_GetError());
    }

    return returnChunk;
}

sdl2::mix_chunk_ptr pitchShiftChunk(Mix_Chunk* sound, double playbackRate, double gain) {
    if(sound == nullptr || sound->abuf == nullptr || sound->alen == 0) {
        return cloneChunk(sound);
    }

    if(playbackRate <= 0.0) {
        playbackRate = 1.0;
    }

    int targetFrequency = 0;
    Uint16 targetFormat = 0;
    int channels = 0;
    if(Mix_QuerySpec(&targetFrequency, &targetFormat, &channels) == 0) {
        return cloneChunk(sound);
    }

    const int sampleBytes = bytesPerSample(targetFormat);
    if(sampleBytes != 2 || channels <= 0) {
        return cloneChunk(sound);
    }

    const int frameBytes = sampleBytes * channels;
    const Uint32 sourceFrames = sound->alen / frameBytes;
    if(sourceFrames == 0) {
        return cloneChunk(sound);
    }

    const Uint32 targetFrames = std::max<Uint32>(1, static_cast<Uint32>(std::ceil(static_cast<double>(sourceFrames) / playbackRate)));
    auto returnChunk = create_chunk();
    if(returnChunk == nullptr) {
        return nullptr;
    }

    returnChunk->allocated = 1;
    returnChunk->volume = sound->volume;
    returnChunk->alen = targetFrames * frameBytes;

    sdl2::sdl_ptr<Uint8> buffer{ static_cast<Uint8*>(SDL_malloc(returnChunk->alen)) };
    if(buffer == nullptr) {
        return nullptr;
    }

    for(Uint32 targetFrame = 0; targetFrame < targetFrames; ++targetFrame) {
        const double sourcePos = std::min<double>(static_cast<double>(sourceFrames - 1), static_cast<double>(targetFrame) * playbackRate);
        const Uint32 sourceFrame0 = static_cast<Uint32>(sourcePos);
        const Uint32 sourceFrame1 = std::min<Uint32>(sourceFrame0 + 1, sourceFrames - 1);
        const double fraction = sourcePos - static_cast<double>(sourceFrame0);

        for(int channel = 0; channel < channels; ++channel) {
            const Uint8* src0 = sound->abuf + sourceFrame0 * frameBytes + channel * sampleBytes;
            const Uint8* src1 = sound->abuf + sourceFrame1 * frameBytes + channel * sampleBytes;
            Uint8* dst = buffer.get() + targetFrame * frameBytes + channel * sampleBytes;

            const double sample0 = static_cast<double>(readS16Sample(src0, targetFormat));
            const double sample1 = static_cast<double>(readS16Sample(src1, targetFormat));
            const double mixed = (sample0 * (1.0 - fraction) + sample1 * fraction) * gain;
            const auto clamped = static_cast<Sint16>(std::clamp(mixed, -32768.0, 32767.0));
            writeS16Sample(dst, targetFormat, clamped);
        }
    }

    returnChunk->abuf = buffer.release();
    return returnChunk;
}

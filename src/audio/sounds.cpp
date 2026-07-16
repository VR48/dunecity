/*
 *  Dune City Sound System - Programmatic Tone Generation Implementation
 *
 *  Generates PCM WAV data in-memory and converts to Mix_Chunk via Mix_LoadWAV_RW.
 */

#include <audio/sounds.h>

#include <SDL_mixer.h>

#include <cmath>
#include <cstring>
#include <vector>

namespace DuneCitySounds {

bool masterMute = false;

namespace {

constexpr int SAMPLE_RATE = 22050;
constexpr int BITS_PER_SAMPLE = 16;
constexpr int NUM_CHANNELS = 1;
constexpr double PI = 3.14159265358979323846;

// Helper: write little-endian 16-bit value into a byte buffer
void writeLE16(uint8_t* out, uint16_t val) {
    out[0] = static_cast<uint8_t>(val & 0xff);
    out[1] = static_cast<uint8_t>((val >> 8) & 0xff);
}

// Helper: write little-endian 32-bit value into a byte buffer
void writeLE32(uint8_t* out, uint32_t val) {
    out[0] = static_cast<uint8_t>(val & 0xff);
    out[1] = static_cast<uint8_t>((val >> 8) & 0xff);
    out[2] = static_cast<uint8_t>((val >> 16) & 0xff);
    out[3] = static_cast<uint8_t>((val >> 24) & 0xff);
}

std::vector<int16_t> generateSineWave(int frequencyHz, int numSamples, int volume) {
    std::vector<int16_t> samples(numSamples);
    const double amplitude = (volume / 128.0) * 32767.0;
    const double twoPiF = 2.0 * PI * frequencyHz;
    const double invRate = 1.0 / SAMPLE_RATE;

    for (int i = 0; i < numSamples; ++i) {
        // Linear fade-out: envelope goes from 1.0 to 0.0 over the duration
        const double envelope = 1.0 - (static_cast<double>(i) / numSamples);
        const double sample = std::sin(twoPiF * i * invRate) * amplitude * envelope;
        samples[i] = static_cast<int16_t>(std::round(sample));
    }

    return samples;
}

std::vector<int16_t> generateSilence(int numSamples) {
    return std::vector<int16_t>(numSamples, 0);
}

sdl2::mix_chunk_ptr buildChunkFromSamples(const std::vector<int16_t>& samples) {
    if (samples.empty()) {
        return nullptr;
    }

    constexpr int waveHeaderSize = 44;
    const auto pcmByteSize = samples.size() * sizeof(int16_t);
    const auto totalSize = waveHeaderSize + pcmByteSize;

    auto* wav = static_cast<uint8_t*>(SDL_malloc(totalSize));
    if (!wav) {
        return nullptr;
    }

    // RIFF chunk
    std::memcpy(wav + 0, "RIFF", 4);
    writeLE32(wav + 4, static_cast<uint32_t>(totalSize - 8));
    std::memcpy(wav + 8, "WAVE", 4);

    // fmt  sub-chunk
    std::memcpy(wav + 12, "fmt ", 4);
    writeLE32(wav + 16, 16); // subchunk1 size (PCM)
    writeLE16(wav + 20, 1);  // audio format: 1 = PCM
    writeLE16(wav + 22, NUM_CHANNELS);
    writeLE32(wav + 24, SAMPLE_RATE);
    writeLE32(wav + 28, SAMPLE_RATE * NUM_CHANNELS * static_cast<int>(sizeof(int16_t)));
    writeLE16(wav + 32, NUM_CHANNELS * static_cast<int>(sizeof(int16_t)));
    writeLE16(wav + 34, BITS_PER_SAMPLE);

    // data sub-chunk
    std::memcpy(wav + 36, "data", 4);
    writeLE32(wav + 40, static_cast<uint32_t>(pcmByteSize));

    // PCM samples — 16-bit PCM in WAV is signed little-endian.
    auto* dataStart = wav + waveHeaderSize;
    for (size_t i = 0; i < samples.size(); ++i) {
        writeLE16(dataStart + i * sizeof(int16_t), static_cast<uint16_t>(samples[i]));
    }

    auto* rw = SDL_RWFromMem(wav, static_cast<int>(totalSize));
    if (!rw) {
        SDL_free(wav);
        return nullptr;
    }

    sdl2::mix_chunk_ptr chunk{ Mix_LoadWAV_RW(rw, 1) };
    SDL_free(wav);
    return chunk;
}

sdl2::mix_chunk_ptr createMixChunk() {
    return sdl2::mix_chunk_ptr{ static_cast<Mix_Chunk*>(SDL_malloc(sizeof(Mix_Chunk))) };
}

sdl2::mix_chunk_ptr cloneChunk(const Mix_Chunk* source) {
    if (!source) {
        return nullptr;
    }

    auto chunk = createMixChunk();
    if (!chunk) {
        return nullptr;
    }

    chunk->allocated = 1;
    chunk->volume = source->volume;
    chunk->alen = source->alen;

    if (source->alen == 0 || source->abuf == nullptr) {
        chunk->abuf = nullptr;
        return chunk;
    }

    auto* buffer = static_cast<uint8_t*>(SDL_malloc(source->alen));
    if (!buffer) {
        return nullptr;
    }

    std::memcpy(buffer, source->abuf, source->alen);
    chunk->abuf = buffer;

    return chunk;
}

sdl2::mix_chunk_ptr concatChunks(const Mix_Chunk* a, const Mix_Chunk* b) {
    if (!a) {
        return cloneChunk(b);
    }
    if (!b) {
        return cloneChunk(a);
    }

    auto chunk = createMixChunk();
    if (!chunk) {
        return nullptr;
    }

    chunk->allocated = 1;
    chunk->volume = a->volume;
    chunk->alen = a->alen + b->alen;

    auto* buffer = static_cast<uint8_t*>(SDL_malloc(chunk->alen));
    if (!buffer) {
        return nullptr;
    }

    std::memcpy(buffer, a->abuf, a->alen);
    std::memcpy(buffer + a->alen, b->abuf, b->alen);
    chunk->abuf = buffer;

    return chunk;
}

sdl2::mix_chunk_ptr generateTone(int frequencyHz, int durationMs, int volume) {
    if (frequencyHz <= 0 || durationMs <= 0 || volume <= 0) {
        return nullptr;
    }

    int numSamples = (SAMPLE_RATE * durationMs) / 1000;
    if (numSamples < 1) {
        numSamples = 1;
    }

    return buildChunkFromSamples(generateSineWave(frequencyHz, numSamples, volume));
}

sdl2::mix_chunk_ptr generateSilenceChunk(int durationMs) {
    if (durationMs <= 0) {
        return nullptr;
    }

    int numSamples = (SAMPLE_RATE * durationMs) / 1000;
    if (numSamples < 1) {
        numSamples = 1;
    }

    return buildChunkFromSamples(generateSilence(numSamples));
}

sdl2::mix_chunk_ptr generateAlertTone(int freq1, int freq2, int numBeeps, int beepMs, int gapMs, int volume) {
    if (numBeeps <= 0) {
        return nullptr;
    }

    auto tone1 = generateTone(freq1, beepMs, volume);
    auto tone2 = generateTone(freq2, beepMs, volume);
    auto gap = generateSilenceChunk(gapMs);
    if (!tone1 || !tone2 || !gap) {
        return nullptr;
    }

    sdl2::mix_chunk_ptr result;

    for (int i = 0; i < numBeeps; ++i) {
        auto pair = concatChunks(tone1.get(), tone2.get());
        if (!pair) {
            return nullptr;
        }

        auto withGap = concatChunks(pair.get(), gap.get());
        if (!withGap) {
            return nullptr;
        }

        result = concatChunks(result.get(), withGap.get());
        if (!result) {
            return nullptr;
        }
    }

    return result;
}

sdl2::mix_chunk_ptr generateArpeggio(const int* frequencies, int numNotes, int noteMs, int volume) {
    if (!frequencies || numNotes <= 0) {
        return nullptr;
    }

    sdl2::mix_chunk_ptr result;

    for (int i = 0; i < numNotes; ++i) {
        auto note = generateTone(frequencies[i], noteMs, volume);
        if (!note) {
            return nullptr;
        }

        result = concatChunks(result.get(), note.get());
        if (!result) {
            return nullptr;
        }
    }

    return result;
}

void playCachedChunk(Mix_Chunk* chunk) {
    if (chunk) {
        Mix_PlayChannel(-1, chunk, 0);
    }
}

sdl2::mix_chunk_ptr createCombatNearbyChunk() {
    auto low = generateTone(110, 250, MIX_MAX_VOLUME / 2);
    auto mid = generateTone(165, 250, MIX_MAX_VOLUME / 3);
    if (!low || !mid) {
        return nullptr;
    }

    auto combined = concatChunks(low.get(), mid.get());
    if (!combined) {
        return nullptr;
    }

    return concatChunks(combined.get(), mid.get());
}

} // anonymous namespace

sdl2::mix_chunk_ptr CreateZoneBuiltChunk() {
    return generateTone(880, 100, MIX_MAX_VOLUME / 2);
}

sdl2::mix_chunk_ptr CreateDisasterWarningChunk() {
    // Three alternating beeps: 440 Hz / 554 Hz (A4 / C#5 — tritone, unsettling)
    return generateAlertTone(440, 554, 3, 150, 50, MIX_MAX_VOLUME * 2 / 3);
}

sdl2::mix_chunk_ptr CreatePowerOutageChunk() {
    // Low 220 Hz tone with longer decay — ominous hum
    return generateTone(220, 400, MIX_MAX_VOLUME / 2);
}

sdl2::mix_chunk_ptr CreateMilestoneChunk() {
    // C5 -> E5 -> G5 -> C6 arpeggio (major chord, triumphant)
    constexpr int notes[] = { 523, 659, 784, 1047 };
    return generateArpeggio(notes, 4, 120, MIX_MAX_VOLUME * 2 / 3);
}

sdl2::mix_chunk_ptr CreateTaxCollectedChunk() {
    // Pleasant two-tone ascending: 440 Hz -> 660 Hz
    constexpr int notes[] = { 440, 660 };
    return generateArpeggio(notes, 2, 80, MIX_MAX_VOLUME / 2);
}

sdl2::mix_chunk_ptr CreateBudgetLowChunk() {
    // Urgent double beep at 600 Hz
    return generateAlertTone(600, 600, 2, 100, 50, MIX_MAX_VOLUME * 2 / 3);
}

// ===========================================================================
// Public sound functions
// ===========================================================================

void Sound_ZoneBuilt() {
    if (masterMute) return;
    static auto chunk = CreateZoneBuiltChunk();
    playCachedChunk(chunk.get());
}

void Sound_DisasterWarning() {
    if (masterMute) return;
    static auto chunk = CreateDisasterWarningChunk();
    playCachedChunk(chunk.get());
}

void Sound_PowerOutage() {
    if (masterMute) return;
    static auto chunk = CreatePowerOutageChunk();
    playCachedChunk(chunk.get());
}

void Sound_Milestone() {
    if (masterMute) return;
    static auto chunk = CreateMilestoneChunk();
    playCachedChunk(chunk.get());
}

void Sound_CombatNearby() {
    if (masterMute) return;
    static auto chunk = createCombatNearbyChunk();
    playCachedChunk(chunk.get());
}

void Sound_TaxCollected() {
    if (masterMute) return;
    static auto chunk = CreateTaxCollectedChunk();
    playCachedChunk(chunk.get());
}

void Sound_BudgetLow() {
    if (masterMute) return;
    static auto chunk = CreateBudgetLowChunk();
    playCachedChunk(chunk.get());
}

} // namespace DuneCitySounds

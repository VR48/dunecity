#ifndef DUNECITY_RESIDENTIAL_POPULATION_H
#define DUNECITY_RESIDENTIAL_POPULATION_H
#include <algorithm>
#include <cstdint>

namespace DuneCity::ResidentialPopulation {
constexpr uint8_t legacy = 255;
inline int normalize(int pop) {
    if (pop <= 8) return std::clamp(pop,0,8);
    return std::clamp(pop/8*8,16,40);
}
inline int fromDensity(int density) {
    return density <= 0 ? 0 : density == 1 ? 16 : density == 2 ? 24 : 40;
}
inline int density(int pop) {
    return pop <= 8 ? 0 : pop == 16 ? 1 : pop < 40 ? 2 : 3;
}
inline int grow(int pop, int localDensity) {
    pop = normalize(pop);
    if (pop < 8) return pop+1;
    if (pop == 8) return localDensity > 64 ? 16 : 8;
    return std::min(40,pop+8);
}
inline int decline(int pop) {
    pop = normalize(pop);
    return pop <= 8 ? std::max(0,pop-1) : pop-8;
}
inline int supply(int pop) {
    // Retain the existing apartment supply scale; free houses contribute
    // proportionally, with at least one worker from an inhabited lot.
    if (pop <= 8) return pop == 0 ? 0 : (pop*10+15)/16;
    return pop == 16 ? 10 : pop == 24 ? 25 : pop == 32 ? 37 : 50;
}
// Shared save codec: older saves have no extra byte and keep their existing
// apartment population. Resolve the sentinel only after map tiles are loaded.
template<class Stream> uint8_t read(Stream& stream, int version) {
    return version >= 9835 ? static_cast<uint8_t>(normalize(stream.readUint8())) : legacy;
}
template<class Stream> void write(Stream& stream, int population) {
    stream.writeUint8(static_cast<uint8_t>(normalize(population)));
}
}
#endif

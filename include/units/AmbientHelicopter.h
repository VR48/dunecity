#ifndef AMBIENTHELICOPTER_H
#define AMBIENTHELICOPTER_H

#include <units/AirUnit.h>

/// Ambient city helicopter — non-combat, non-selectable air unit spawned by
/// Airport structures.  Orbits near the Airport for a fixed number of game
/// cycles then self-destructs.  Reuses Ornithopter graphics as placeholder.
class AmbientHelicopter final : public AirUnit
{
public:
    explicit AmbientHelicopter(House* newOwner);
    explicit AmbientHelicopter(InputStream& stream);
    void init();
    virtual ~AmbientHelicopter();

    void save(OutputStream& stream) const override;

    void checkPos() override;
    bool canPass(int xPos, int yPos) const override;

    bool update() override;
    void deploy(const Coord& newLocation) override;

private:
    Uint32 spawnCycle;     ///< Game cycle at which this helicopter was spawned
    Uint32 lifetime;       ///< Number of cycles before self-destruct
    Coord  orbitCenter;    ///< Center point to orbit around (Airport location)
};

#endif // AMBIENTHELICOPTER_H

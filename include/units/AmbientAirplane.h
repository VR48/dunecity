#ifndef AMBIENTAIRPLANE_H
#define AMBIENTAIRPLANE_H

#include <units/AirUnit.h>

/// Ambient city airplane — non-combat, non-selectable air unit spawned by
/// Airport structures.  Flies from one map edge across toward the opposite
/// edge then self-destructs, like a Frigate delivery run but purely visual.
/// Reuses Carryall graphics as placeholder.
class AmbientAirplane final : public AirUnit
{
public:
    explicit AmbientAirplane(House* newOwner);
    explicit AmbientAirplane(InputStream& stream);
    void init();
    virtual ~AmbientAirplane();

    void save(OutputStream& stream) const override;

    void checkPos() override;
    bool canPass(int xPos, int yPos) const override;

    bool update() override;
    void deploy(const Coord& newLocation) override;

private:
    bool passedDestination;   ///< Has this airplane reached its flyover point?
};

#endif // AMBIENTAIRPLANE_H

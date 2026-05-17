#ifndef NUCLEARPLANT_H
#define NUCLEARPLANT_H

#include <structures/StructureBase.h>

/**
 * Nuclear power plant: high-output power source for DuneCity mode.
 *
 * Mirrors WindTrap mechanics — produced power scales with health and
 * is added to the owner's House::producedPower pool. Sized 3x3,
 * rendered with the Micropolis nuclear-plant sprite scaled to fill
 * the footprint (HighTechFactory art is a fallback if missing).
 */
class NuclearPlant final : public StructureBase
{
public:
    explicit NuclearPlant(House* newOwner);
    explicit NuclearPlant(InputStream& stream);
    virtual ~NuclearPlant();

    bool update() override;
    void setHealth(FixPoint newHealth) override;

protected:
    int getProducedPower() const;

private:
    void init();
};

#endif // NUCLEARPLANT_H

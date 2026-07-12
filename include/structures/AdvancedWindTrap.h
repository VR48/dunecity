#ifndef ADVANCEDWINDTRAP_H
#define ADVANCEDWINDTRAP_H

#include <structures/StructureBase.h>

/**
 * Advanced Windtrap: high-output 3x3 power source for DuneCity mode.
 *
 * Produces 300 power (3x standard WindTrap). Power output scales with health,
 * matching WindTrap mechanics. Requires Windtrap + Radar + Hightech Factory.
 * Reuses WindTrap animation sprite.
 */
class AdvancedWindTrap final : public StructureBase
{
public:
    explicit AdvancedWindTrap(House* newOwner, Uint32 newItemID = Structure_AdvancedWindTrap);
    explicit AdvancedWindTrap(InputStream& stream, Uint32 newItemID = Structure_AdvancedWindTrap);
    virtual ~AdvancedWindTrap();

    ObjectInterface* getInterfaceContainer() override;
    bool update() override;
    void setHealth(FixPoint newHealth) override;

protected:
    int getProducedPower() const;

private:
    void init(Uint32 newItemID);
};

#endif // ADVANCEDWINDTRAP_H

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
    explicit AdvancedWindTrap(House* newOwner);
    explicit AdvancedWindTrap(InputStream& stream);
    virtual ~AdvancedWindTrap();

    bool update() override;
    void setHealth(FixPoint newHealth) override;

protected:
    int getProducedPower() const;

private:
    void init();
};

#endif // ADVANCEDWINDTRAP_H

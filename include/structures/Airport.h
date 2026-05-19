#ifndef AIRPORT_H
#define AIRPORT_H

#include <structures/StructureBase.h>

/**
 * Airport — DuneCity economic building.
 *
 * 3x3 footprint, no animation. Provides commercial/transport boost to
 * the city economy. Maps to CityRole::Commercial so it generates jobs
 * and tax revenue, similar to SimCity Classic's airport zone.
 */
class Airport final : public StructureBase
{
public:
    explicit Airport(House* newOwner);
    explicit Airport(InputStream& stream);
    virtual ~Airport();

private:
    void init();
};

#endif // AIRPORT_H

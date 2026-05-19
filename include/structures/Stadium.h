#ifndef STADIUM_H
#define STADIUM_H

#include <structures/StructureBase.h>

/**
 * Stadium — DuneCity civic building.
 *
 * 3x3 footprint, no animation. Provides a large land-value boost to
 * surrounding residential/commercial areas via getParkLandValueBonus().
 * Does not count as R/C/I — it's a civic amenity like parks in SimCity.
 */
class Stadium final : public StructureBase
{
public:
    explicit Stadium(House* newOwner);
    explicit Stadium(InputStream& stream);
    virtual ~Stadium();

private:
    void init();
};

#endif // STADIUM_H

/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 */

#ifndef SCOUTPOST_H
#define SCOUTPOST_H

#include <structures/TurretBase.h>

class Scoutpost final : public TurretBase
{
public:
    explicit Scoutpost(House* newOwner);
    explicit Scoutpost(InputStream& stream);
    void init();
    ~Scoutpost() override;

    bool canAttack(const ObjectBase* object) const override;
    ObjectInterface* getInterfaceContainer() override;
    void setHealth(FixPoint newHealth) override;

    int getProducedPower() const;

protected:
    void updateStructureSpecificStuff() override;
};

#endif // SCOUTPOST_H

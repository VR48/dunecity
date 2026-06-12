/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MENTAT_BUILD_STEP_H
#define MENTAT_BUILD_STEP_H

#include <DataTypes.h>
#include <players/QuantBotBuildContext.h>
#include <functional>
#include <string>
#include <utility>

class QuantBot;

/// Mode filter flags for build steps.
struct MentatStepMode {
    bool cityOnly = false;      ///< Skip if !ctx.isCitySim
    bool vanillaOnly = false;   ///< Skip if ctx.isCitySim
    bool campaignOnly = false;  ///< Skip if !ctx.isCampaign
    bool customOnly = false;    ///< Skip if ctx.isCampaign
};

/// Convenience constructors for common mode filters.
inline MentatStepMode anyMode()      { return {}; }
inline MentatStepMode cityOnly()     { return {true, false, false, false}; }
inline MentatStepMode vanillaOnly()  { return {false, true, false, false}; }
inline MentatStepMode vanillaCustomOnly() { return {false, true, false, true}; }

using MentatCheckFn = std::function<bool(QuantBot*, const BuilderBase*, const QuantBotBuildContext&)>;
using MentatRunFn   = std::function<std::pair<Uint32, bool>(QuantBot*, const BuilderBase*, QuantBotBuildContext&)>;

/// One step in the Mentat CY build priority order.
struct MentatBuildStep {
    std::string label;       ///< Debug label: "WINDTRAP-0", "POWER-RECOVERY", etc.
    MentatStepMode mode;     ///< Mode filters

    /// Returns true if this step's condition is met.
    MentatCheckFn check;

    /// Execute this step.
    /// Returns {itemID, handled}:
    /// - {itemID, false} → build this item, continue to dedup/placement
    /// - {NONE_ID, true}  → step did its own work (repair/upgrade), stop iterating
    /// - {NONE_ID, false} → condition matched but nothing to do, continue to next step
    MentatRunFn run;
};

#endif // MENTAT_BUILD_STEP_H

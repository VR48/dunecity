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

#include <FileClasses/SFXManager.h>

#include <algorithm>

#include <globals.h>

#include <FileClasses/FileManager.h>
#include <FileClasses/Vocfile.h>

#include <FileClasses/adl/sound_adlib.h>

#include <audio/sounds.h>
#include <misc/sound_util.h>
#include <misc/exceptions.h>

// Not used:
// - EXCANNON.VOC (same as EXSMALL.VOC)
// - DROPEQ2P.VOC
// - POPPA.VOC

SFXManager::SFXManager() {
    // load voice and language specific sounds
    if(settings.general.language == "de") {
        loadNonEnglishVoice("G");
    } else if(settings.general.language == "fr") {
        loadNonEnglishVoice("F");
    } else {
        loadEnglishVoice();
    }

    loadSoundEffects();

    for(int i = 0; i < NUM_SOUNDCHUNK; i++) {
        if(soundChunk[i] == nullptr) {
            THROW(std::runtime_error, "Not all sounds could be loaded: soundChunk[%d] == nullptr!", i);
        }
    }
}

SFXManager::~SFXManager() = default;

Mix_Chunk* SFXManager::getVoice(Voice_enum id, int house) {
    if(settings.general.language == "de" || settings.general.language == "fr") {
        return getNonEnglishVoice(id,house);
    } else {
        return getEnglishVoice(id,house);
    }
}

Mix_Chunk* SFXManager::getSound(Sound_enum id) {
    if(id >= soundChunk.size())
        return nullptr;

    return soundChunk[id].get();
}

sdl2::mix_chunk_ptr SFXManager::loadMixFromADL(const std::string& adlFile, int index, int volume) const {

    auto rwop = pFileManager->openFile(adlFile);
    auto pSoundAdlibPC = std::make_unique<SoundAdlibPC>(rwop.get(), AUDIO_FREQUENCY);
    pSoundAdlibPC->setVolume(volume);
    sdl2::mix_chunk_ptr chunk{ pSoundAdlibPC->getSubsong(index) };

    return chunk;
}

void SFXManager::loadEnglishVoice() {
    lngVoice.clear();
    lngVoice.resize(NUM_VOICE*NUM_HOUSES);

    // now we can load
    for(auto house = 0; house < NUM_HOUSES; house++) {
        sdl2::mix_chunk_ptr HouseNameChunk;

        std::string HouseString;
        const int VoiceNum = house;
        switch(house) {
            case HOUSE_HARKONNEN:
                HouseString = "H";
                HouseNameChunk = getChunkFromFile(HouseString + "HARK.VOC");
                break;
            case HOUSE_ATREIDES:
                HouseString = "A";
                HouseNameChunk = getChunkFromFile(HouseString + "ATRE.VOC");
                break;
            case HOUSE_ORDOS:
                HouseString = "O";
                HouseNameChunk = getChunkFromFile(HouseString + "ORDOS.VOC");
                break;
            case HOUSE_FREMEN:
                HouseString = "A";
                HouseNameChunk = getChunkFromFile(HouseString + "FREMEN.VOC");
                break;
            case HOUSE_SARDAUKAR:
                HouseString = "H";
                HouseNameChunk = getChunkFromFile(HouseString + "SARD.VOC");
                break;
            case HOUSE_MERCENARY:
                HouseString = "O";
                HouseNameChunk = getChunkFromFile(HouseString + "MERC.VOC");
                break;
            case HOUSE_NEUTRAL:
                HouseString = "A";
                HouseNameChunk = getChunkFromFile("MNEU.VOC", "ANEU.VOC");
                break; // Neutral uses Atreides action voices but its own house name (prefer MNEU)
            default:
                break;
        }

        { // Scope
        // "... Harvester deployed", "... Unit deployed" and "... Unit launched"
            auto Harvester = getChunkFromFile(HouseString + "HARVEST.VOC");
            auto Unit = getChunkFromFile(HouseString + "UNIT.VOC");
            auto Deployed = getChunkFromFile(HouseString + "DEPLOY.VOC");
            auto Launched = getChunkFromFile(HouseString + "LAUNCH.VOC");
            lngVoice[HarvesterDeployed*NUM_HOUSES + VoiceNum] = concat3Chunks(HouseNameChunk.get(), Harvester.get(), Deployed.get());
            lngVoice[UnitDeployed*NUM_HOUSES + VoiceNum] = concat3Chunks(HouseNameChunk.get(), Unit.get(), Deployed.get());
            lngVoice[UnitLaunched*NUM_HOUSES + VoiceNum] = concat3Chunks(HouseNameChunk.get(), Unit.get(), Launched.get());
        }

        // "Contruction complete"
        lngVoice[ConstructionComplete*NUM_HOUSES+VoiceNum] = getChunkFromFile(HouseString + "CONST.VOC");

        { // Scope
          // "Vehicle repaired"
            auto Vehicle = getChunkFromFile(HouseString + "VEHICLE.VOC");
            auto Repaired = getChunkFromFile(HouseString + "REPAIR.VOC");
            lngVoice[VehicleRepaired*NUM_HOUSES + VoiceNum] = concat2Chunks(Vehicle.get(), Repaired.get());
        }

        { // Scope
          // "Frigate has arrived"
            auto FrigateChunk = getChunkFromFile(HouseString + "FRIGATE.VOC");
            auto HasArrivedChunk = getChunkFromFile(HouseString + "ARRIVE.VOC");
            lngVoice[FrigateHasArrived*NUM_HOUSES + VoiceNum] = concat2Chunks(FrigateChunk.get(), HasArrivedChunk.get());
        }

        // "Your mission is complete"
        lngVoice[YourMissionIsComplete*NUM_HOUSES+VoiceNum] = getChunkFromFile(HouseString + "WIN.VOC");

        // "You have failed your mission"
        lngVoice[YouHaveFailedYourMission*NUM_HOUSES+VoiceNum] = getChunkFromFile(HouseString + "LOSE.VOC");

        { // Scope
          // "Radar activated"/"Radar deactivated"
            auto RadarChunk = getChunkFromFile(HouseString + "RADAR.VOC");
            auto RadarActivatedChunk = getChunkFromFile(HouseString + "ON.VOC");
            auto RadarDeactivatedChunk = getChunkFromFile(HouseString + "OFF.VOC");
            lngVoice[RadarActivated*NUM_HOUSES + VoiceNum] = concat2Chunks(RadarChunk.get(), RadarActivatedChunk.get());
            lngVoice[RadarDeactivated*NUM_HOUSES + VoiceNum] = concat2Chunks(RadarChunk.get(), RadarDeactivatedChunk.get());
        }

        { // Scope
          // "Bloom located"
            auto Bloom = getChunkFromFile(HouseString + "BLOOM.VOC");
            auto Located = getChunkFromFile(HouseString + "LOCATED.VOC");
            lngVoice[BloomLocated*NUM_HOUSES + VoiceNum] = concat2Chunks(Bloom.get(), Located.get());
        }

        { // Scope
          // "Warning Wormsign"
            auto WarningChunk = getChunkFromFile(HouseString + "WARNING.VOC");
            auto WormSignChunk = getChunkFromFile(HouseString + "WORMY.VOC");
            lngVoice[WarningWormSign*NUM_HOUSES + VoiceNum] = concat2Chunks(WarningChunk.get(), WormSignChunk.get());
        }

        // "Our base is under attack"
        lngVoice[BaseIsUnderAttack*NUM_HOUSES+VoiceNum] = getChunkFromFile(HouseString + "ATTACK.VOC");

        { // Scope
          // "Saboteur approaching" and "Missile approaching"
            auto SabotChunk = getChunkFromFile(HouseString + "SABOT.VOC");
            auto MissileChunk = getChunkFromFile(HouseString + "MISSILE.VOC");
            auto ApproachingChunk = getChunkFromFile(HouseString + "APPRCH.VOC");
            lngVoice[SaboteurApproaching*NUM_HOUSES + VoiceNum] = concat2Chunks(SabotChunk.get(), ApproachingChunk.get());
            lngVoice[MissileApproaching*NUM_HOUSES + VoiceNum] = concat2Chunks(MissileChunk.get(), ApproachingChunk.get());
        }

        HouseNameChunk.reset();

        // "Yes Sir"
        lngVoice[YesSir*NUM_HOUSES+VoiceNum] = getChunkFromFile("ZREPORT1.VOC", "REPORT1.VOC");

        // "Reporting"
        lngVoice[Reporting*NUM_HOUSES+VoiceNum] = getChunkFromFile("ZREPORT2.VOC", "REPORT2.VOC");

        // "Acknowledged"
        lngVoice[Acknowledged*NUM_HOUSES+VoiceNum] = getChunkFromFile("ZREPORT3.VOC", "REPORT3.VOC");

        // "Affirmative"
        lngVoice[Affirmative*NUM_HOUSES+VoiceNum] = getChunkFromFile("ZAFFIRM.VOC", "AFFIRM.VOC");

        // "Moving out"
        lngVoice[MovingOut*NUM_HOUSES+VoiceNum] = getChunkFromFile("ZMOVEOUT.VOC", "MOVEOUT.VOC");

        // "Infantry out"
        lngVoice[InfantryOut*NUM_HOUSES+VoiceNum] = getChunkFromFile("ZOVEROUT.VOC", "OVEROUT.VOC");

        // "Somthing's under the sand"
        lngVoice[SomethingUnderTheSand*NUM_HOUSES+VoiceNum] = getChunkFromFile("SANDBUG.VOC");

        // "House Harkonnen"
        lngVoice[HouseHarkonnen*NUM_HOUSES+VoiceNum] = getChunkFromFile("MHARK.VOC");

        // "House Atreides"
        lngVoice[HouseAtreides*NUM_HOUSES+VoiceNum] = getChunkFromFile("MATRE.VOC");

        // "House Ordos"
        lngVoice[HouseOrdos*NUM_HOUSES+VoiceNum] = getChunkFromFile("MORDOS.VOC");

        // "House Neutral" — use house-specific voice if available
        if (VoiceNum == HOUSE_HARKONNEN || VoiceNum == HOUSE_SARDAUKAR) {
            lngVoice[HouseNeutral*NUM_HOUSES+VoiceNum] = getChunkFromFile("HNEU.VOC", "MNEU.VOC", "MATRE.VOC");
        } else if (VoiceNum == HOUSE_ORDOS || VoiceNum == HOUSE_MERCENARY) {
            lngVoice[HouseNeutral*NUM_HOUSES+VoiceNum] = getChunkFromFile("ONEU.VOC", "MNEU.VOC", "MATRE.VOC");
        } else {
            lngVoice[HouseNeutral*NUM_HOUSES+VoiceNum] = getChunkFromFile("ANEU.VOC", "MNEU.VOC", "MATRE.VOC");
        }
    }

    // Override Neutral action voices with M-prefix (mentat/neutral) where available,
    // falling back to the Atreides prefix already loaded above.  This ensures that
    // Neutral-owned units play neutral-sounding voice lines rather than Atreides lines
    // when built, deployed, or triggered.
    {
        const int n = HOUSE_NEUTRAL;
        auto neuName = getChunkFromFile("MNEU.VOC", "ANEU.VOC", "AATRE.VOC");

        { // HarvesterDeployed / UnitDeployed / UnitLaunched
            auto Harvester = getChunkFromFile("MHARVEST.VOC", "AHARVEST.VOC");
            auto Unit      = getChunkFromFile("MUNIT.VOC",    "AUNIT.VOC");
            auto Deployed  = getChunkFromFile("MDEPLOY.VOC",  "ADEPLOY.VOC");
            auto Launched  = getChunkFromFile("MLAUNCH.VOC",  "ALAUNCH.VOC");
            lngVoice[HarvesterDeployed*NUM_HOUSES + n] = concat3Chunks(neuName.get(), Harvester.get(), Deployed.get());
            lngVoice[UnitDeployed*NUM_HOUSES      + n] = concat3Chunks(neuName.get(), Unit.get(),      Deployed.get());
            lngVoice[UnitLaunched*NUM_HOUSES      + n] = concat3Chunks(neuName.get(), Unit.get(),      Launched.get());
        }

        // Fallback to MNEU.VOC ("Neutral") rather than Atreides ACONST.VOC when MCONST.VOC is absent
        lngVoice[ConstructionComplete*NUM_HOUSES + n] = getChunkFromFile("MCONST.VOC", "MNEU.VOC", "ANEU.VOC");

        { // VehicleRepaired
            auto Vehicle  = getChunkFromFile("MVEHICLE.VOC", "AVEHICLE.VOC");
            auto Repaired = getChunkFromFile("MREPAIR.VOC",  "AREPAIR.VOC");
            lngVoice[VehicleRepaired*NUM_HOUSES + n] = concat2Chunks(Vehicle.get(), Repaired.get());
        }

        { // FrigateHasArrived
            auto Frigate    = getChunkFromFile("MFRIGATE.VOC", "AFRIGATE.VOC");
            auto HasArrived = getChunkFromFile("MARRIVE.VOC",  "AARRIVE.VOC");
            lngVoice[FrigateHasArrived*NUM_HOUSES + n] = concat2Chunks(Frigate.get(), HasArrived.get());
        }

        lngVoice[YourMissionIsComplete*NUM_HOUSES    + n] = getChunkFromFile("MWIN.VOC",  "AWIN.VOC");
        lngVoice[YouHaveFailedYourMission*NUM_HOUSES + n] = getChunkFromFile("MLOSE.VOC", "ALOSE.VOC");

        { // RadarActivated / RadarDeactivated
            auto Radar    = getChunkFromFile("MRADAR.VOC", "ARADAR.VOC");
            auto RadarOn  = getChunkFromFile("MON.VOC",    "AON.VOC");
            auto RadarOff = getChunkFromFile("MOFF.VOC",   "AOFF.VOC");
            lngVoice[RadarActivated*NUM_HOUSES   + n] = concat2Chunks(Radar.get(), RadarOn.get());
            lngVoice[RadarDeactivated*NUM_HOUSES + n] = concat2Chunks(Radar.get(), RadarOff.get());
        }

        { // BloomLocated
            auto Bloom   = getChunkFromFile("MBLOOM.VOC",   "ABLOOM.VOC");
            auto Located = getChunkFromFile("MLOCATED.VOC", "ALOCATED.VOC");
            lngVoice[BloomLocated*NUM_HOUSES + n] = concat2Chunks(Bloom.get(), Located.get());
        }

        { // WarningWormSign
            auto Warning  = getChunkFromFile("MWARNING.VOC", "AWARNING.VOC");
            auto WormSign = getChunkFromFile("MWORMY.VOC",   "AWORMY.VOC");
            lngVoice[WarningWormSign*NUM_HOUSES + n] = concat2Chunks(Warning.get(), WormSign.get());
        }

        lngVoice[BaseIsUnderAttack*NUM_HOUSES + n] = getChunkFromFile("MATTACK.VOC", "AATTACK.VOC");

        { // SaboteurApproaching / MissileApproaching
            auto Sabot       = getChunkFromFile("MSABOT.VOC",   "ASABOT.VOC");
            auto Missile     = getChunkFromFile("MMISSILE.VOC", "AMISSILE.VOC");
            auto Approaching = getChunkFromFile("MAPPRCH.VOC",  "AAPPRCH.VOC");
            lngVoice[SaboteurApproaching*NUM_HOUSES + n] = concat2Chunks(Sabot.get(),   Approaching.get());
            lngVoice[MissileApproaching*NUM_HOUSES  + n] = concat2Chunks(Missile.get(), Approaching.get());
        }
    }

    const auto bad_voice = std::find(lngVoice.cbegin(), lngVoice.cend(), nullptr);

    if (bad_voice != lngVoice.cend()) {
        THROW(std::runtime_error, "Not all voice sounds could be loaded: lngVoice[%d] == nullptr!", static_cast<int>(bad_voice - lngVoice.cbegin()));
    }

}


Mix_Chunk* SFXManager::getEnglishVoice(Voice_enum id, int house) const {
    if(static_cast<size_t>(id) >= lngVoice.size())
        return nullptr;

    return lngVoice[id*NUM_HOUSES + house].get();
}

void SFXManager::loadNonEnglishVoice(const std::string& languagePrefix) {
    lngVoice.clear();
    lngVoice.resize(NUM_VOICE);

    // "Harvester deployed"
    lngVoice[HarvesterDeployed] = getChunkFromFile(languagePrefix + "HARVEST.VOC");

    // "Unit deployed"
    lngVoice[UnitDeployed] = getChunkFromFile(languagePrefix + "DEPLOY.VOC");

    // "Unit launched"
    lngVoice[UnitLaunched] = getChunkFromFile(languagePrefix + "VEHICLE.VOC");

    // "Contruction complete"
    lngVoice[ConstructionComplete] = getChunkFromFile(languagePrefix + "CONST.VOC");

    // "Vehicle repaired"
    lngVoice[VehicleRepaired] = getChunkFromFile(languagePrefix + "REPAIR.VOC");

    // "Frigate has arrived"
    lngVoice[FrigateHasArrived] = getChunkFromFile(languagePrefix + "FRIGATE.VOC");

    // "Your mission is complete" (No non-english voc available)
    lngVoice[YourMissionIsComplete] = createEmptyChunk();

    // "You have failed your mission" (No non-english voc available)
    lngVoice[YouHaveFailedYourMission] = createEmptyChunk();

    // "Radar activated"/"Radar deactivated"
    lngVoice[RadarActivated] = getChunkFromFile(languagePrefix + "ON.VOC");
    lngVoice[RadarDeactivated] = getChunkFromFile(languagePrefix + "OFF.VOC");

    // "Bloom located"
    lngVoice[BloomLocated] = getChunkFromFile(languagePrefix + "BLOOM.VOC");

    // "Warning Wormsign"
    if(pFileManager->exists(languagePrefix + "WORMY.VOC")) {
        auto WarningChunk = getChunkFromFile(languagePrefix + "WARNING.VOC");
        auto WormSignChunk = getChunkFromFile(languagePrefix + "WORMY.VOC");
        lngVoice[WarningWormSign] = concat2Chunks(WarningChunk.get(), WormSignChunk.get());
    } else {
        lngVoice[WarningWormSign] = getChunkFromFile(languagePrefix + "WARNING.VOC");
    }

    // "Our base is under attack"
    lngVoice[BaseIsUnderAttack] = getChunkFromFile(languagePrefix + "ATTACK.VOC");

    // "Saboteur approaching"
    lngVoice[SaboteurApproaching] = getChunkFromFile(languagePrefix + "SABOT.VOC");

    // "Missile approaching"
    lngVoice[MissileApproaching] = getChunkFromFile(languagePrefix + "MISSILE.VOC");

        // "Yes Sir"
    lngVoice[YesSir] = getChunkFromFile(languagePrefix + "REPORT1.VOC");

    // "Reporting"
    lngVoice[Reporting] = getChunkFromFile(languagePrefix + "REPORT2.VOC");

    // "Acknowledged"
    lngVoice[Acknowledged] = getChunkFromFile(languagePrefix + "REPORT3.VOC");

    // "Affirmative"
    lngVoice[Affirmative] = getChunkFromFile(languagePrefix + "AFFIRM.VOC");

    // "Moving out"
    lngVoice[MovingOut] = getChunkFromFile(languagePrefix + "MOVEOUT.VOC");

    // "Infantry out"
    lngVoice[InfantryOut] = getChunkFromFile(languagePrefix + "OVEROUT.VOC");

    // "Somthing's under the sand"
    lngVoice[SomethingUnderTheSand] = getChunkFromFile("SANDBUG.VOC");

    // "House Atreides"
    lngVoice[HouseAtreides] = getChunkFromFile(languagePrefix + "ATRE.VOC");

    // "House Ordos"
    lngVoice[HouseOrdos] = getChunkFromFile(languagePrefix + "ORDOS.VOC");

    // "House Harkonnen"
    lngVoice[HouseHarkonnen] = getChunkFromFile(languagePrefix + "HARK.VOC");

    // "House Neutral"
    lngVoice[HouseNeutral] = getChunkFromFile("MNEU.VOC", "MATRE.VOC");

    const auto bad_voice = std::find(lngVoice.cbegin(), lngVoice.cend(), nullptr);

    if (bad_voice != lngVoice.cend()) {
        THROW(std::runtime_error, "Not all voice sounds could be loaded: lngVoice[%d] == nullptr!", static_cast<int>(bad_voice - lngVoice.cbegin()));
    }

}

void SFXManager::loadSoundEffects() {
    soundChunk[Sound_PlaceStructure] = getChunkFromFile("EXDUD.VOC");
    soundChunk[Sound_ButtonClick] = getChunkFromFile("BUTTON.VOC");
    soundChunk[Sound_InvalidAction] = loadMixFromADL("DUNE1.ADL", 47);
    soundChunk[Sound_CreditsTick] = loadMixFromADL("DUNE1.ADL", 52, 4*MIX_MAX_VOLUME);
    soundChunk[Sound_Tick] = loadMixFromADL("DUNE1.ADL", 38);
    soundChunk[Sound_RadarNoise] = getChunkFromFile("STATICP.VOC");
    soundChunk[Sound_ExplosionGas] = getChunkFromFile("EXGAS.VOC");
    soundChunk[Sound_ExplosionTiny] = getChunkFromFile("EXTINY.VOC");
    soundChunk[Sound_ExplosionSmall] = getChunkFromFile("EXSMALL.VOC");
    soundChunk[Sound_ExplosionMedium] = getChunkFromFile("EXMED.VOC");
    soundChunk[Sound_ExplosionLarge] = getChunkFromFile("EXLARGE.VOC");
    soundChunk[Sound_ExplosionStructure] = getChunkFromFile("CRUMBLE.VOC");
    soundChunk[Sound_WormAttack] = getChunkFromFile("WORMET3P.VOC");
    soundChunk[Sound_Gun] = getChunkFromFile("GUN.VOC");
    soundChunk[Sound_Rocket] = getChunkFromFile("ROCKET.VOC");
    soundChunk[Sound_Bloom] = getChunkFromFile("EXSAND.VOC");
    soundChunk[Sound_Scream1] = getChunkFromFile("VSCREAM1.VOC");
    soundChunk[Sound_Scream2] = getChunkFromFile("VSCREAM2.VOC");
    soundChunk[Sound_Scream3] = getChunkFromFile("VSCREAM3.VOC");
    soundChunk[Sound_Scream4] = getChunkFromFile("VSCREAM4.VOC");
    soundChunk[Sound_Scream5] = getChunkFromFile("VSCREAM5.VOC");
    soundChunk[Sound_Trumpet] = loadMixFromADL("DUNE1.ADL", 30);
    soundChunk[Sound_Drop] = loadMixFromADL("DUNE1.ADL", 24);
    soundChunk[Sound_Squashed] = getChunkFromFile("SQUISH2.VOC");
    soundChunk[Sound_MachineGun] = getChunkFromFile("GUNMULTI.VOC");
    soundChunk[Sound_Sonic] = loadMixFromADL("DUNE1.ADL", 43);
    soundChunk[Sound_RocketSmall] = getChunkFromFile("MISLTINP.VOC");

    soundChunk[Sound_CityTaxCollected] = DuneCitySounds::CreateTaxCollectedChunk();
    soundChunk[Sound_CityBudgetLow] = DuneCitySounds::CreateBudgetLowChunk();
    soundChunk[Sound_CityZoneBuilt] = DuneCitySounds::CreateZoneBuiltChunk();
    soundChunk[Sound_CityPowerShortage] = DuneCitySounds::CreatePowerOutageChunk();
    soundChunk[Sound_CityDisasterWarning] = DuneCitySounds::CreateDisasterWarningChunk();
    soundChunk[Sound_CityMilestone] = DuneCitySounds::CreateMilestoneChunk();
}

Mix_Chunk* SFXManager::getNonEnglishVoice(Voice_enum id, int house) const {
    if(static_cast<size_t>(id) >= lngVoice.size())
        return nullptr;

    return lngVoice[id].get();
}

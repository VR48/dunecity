/*
 *  CityScore.h - City scoring and comparison system for DuneCity
 *
 *  Multiplayer city comparison feature
 */

#ifndef CITYSCORE_H
#define CITYSCORE_H

#include <cstdint>
#include <algorithm>
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <functional>

/**
 * CityScore holds all metrics for a single player's city
 * Used for competitive city comparison between players
 */
struct CityScore {
    std::string playerName;          ///< Name of the player/house
    uint8_t houseID;                 ///< House identifier (0-5)

    int64_t totalPopulation;         ///< Total citizens in city
    int64_t residentialCapacity;     ///< Max residential population
    int64_t commercialCapacity;      ///< Max commercial capacity
    int64_t industrialCapacity;      ///< Max industrial capacity

    int64_t currentCredits;          ///< Current spice/credits
    int64_t totalHarvested;          ///< Total spice harvested
    int64_t taxRevenue;              ///< Total tax collected

    int64_t powerProduced;            ///< Power units produced
    int64_t powerConsumed;           ///< Power units consumed
    int64_t powerCoverage;           ///< Percentage of power coverage (0-100)

    int64_t roadTiles;                ///< Number of road tiles
    int64_t totalBuildings;          ///< Total structures built

    int64_t crimeRate;                ///< Crime level (0-100)
    int64_t pollutionLevel;           ///< Pollution level (0-100)
    int64_t landValue;                ///< Average land value

    int32_t gameTime;                 ///< Time played in seconds
    int32_t cityAge;                  ///< City age in simulation cycles

    // Calculated scores
    int64_t populationScore;          ///< Population-based score
    int64_t economyScore;            ///< Economy-based score
    int64_t infrastructureScore;      ///< Infrastructure score
    int64_t qualityOfLifeScore;      ///< Quality of life score
    int64_t totalScore;               ///< Combined total score

    CityScore()
        : playerName("")
        , houseID(0)
        , totalPopulation(0)
        , residentialCapacity(0)
        , commercialCapacity(0)
        , industrialCapacity(0)
        , currentCredits(0)
        , totalHarvested(0)
        , taxRevenue(0)
        , powerProduced(0)
        , powerConsumed(0)
        , powerCoverage(0)
        , roadTiles(0)
        , totalBuildings(0)
        , crimeRate(0)
        , pollutionLevel(0)
        , landValue(0)
        , gameTime(0)
        , cityAge(0)
        , populationScore(0)
        , economyScore(0)
        , infrastructureScore(0)
        , qualityOfLifeScore(0)
        , totalScore(0)
    {}

    /**
     * Calculate all derived scores from raw metrics
     */
    void calculateScores() {
        // Population score: weighted by population and capacity
        populationScore = (totalPopulation * 10) +
                          (residentialCapacity * 5) +
                          (commercialCapacity * 3) +
                          (industrialCapacity * 3);

        // Economy score: weighted by harvested and tax revenue
        economyScore = (totalHarvested / 100) + (taxRevenue / 50) + (currentCredits / 10);

        // Infrastructure score: roads, buildings, power
        infrastructureScore = (roadTiles * 2) +
                              (totalBuildings * 5) +
                              (powerCoverage * 10);

        // Quality of life score: inverse of negative factors
        qualityOfLifeScore = (100 - crimeRate) * 5 +
                             (100 - pollutionLevel) * 5 +
                             (landValue * 2);

        // Cap quality of life at reasonable maximum
        if (qualityOfLifeScore < 0) qualityOfLifeScore = 0;

        // Total score
        totalScore = populationScore + economyScore + infrastructureScore + qualityOfLifeScore;
    }
};

/**
 * CityScoreManager manages scores for all players in a game
 */
class CityScoreManager {
public:
    CityScoreManager() = default;

    /**
     * Get or create score for a house
     */
    CityScore& getScore(uint8_t houseID) {
        for (auto& score : scores) {
            if (score.houseID == houseID) {
                return score;
            }
        }
        scores.emplace_back();
        scores.back().houseID = houseID;
        return scores.back();
    }

    /**
     * Get all player scores sorted by total score (highest first)
     */
    std::vector<CityScore> getRankings() const {
        std::vector<CityScore> sorted = scores;
        std::sort(sorted.begin(), sorted.end(),
            [](const CityScore& a, const CityScore& b) {
                return a.totalScore > b.totalScore;
            });
        return sorted;
    }

    /**
     * Get player's rank (1 = first place)
     */
    int getRank(uint8_t houseID) const {
        auto rankings = getRankings();
        for (size_t i = 0; i < rankings.size(); ++i) {
            if (rankings[i].houseID == houseID) {
                return static_cast<int>(i + 1);
            }
        }
        return static_cast<int>(rankings.size() + 1);
    }

    /**
     * Clear all scores
     */
    void reset() {
        scores.clear();
    }

    /**
     * Get number of players tracked
     */
    size_t size() const { return scores.size(); }

private:
    std::vector<CityScore> scores;
};

#endif // CITYSCORE_H

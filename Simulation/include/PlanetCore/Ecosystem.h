// Plants and animals living on the planet, one simulated day at a time.
// Plain C++ with no Unreal dependencies, so it can be unit tested and later shared with the NPCs.
//
// Plants: a 2-degree grid of edible plant food that grows with the seasons and gets eaten.
// Animals: groups of one species that move, graze or hunt, give birth, starve and die.
//          Each group has a tiny brain that decides how it weighs food, danger and roaming
//          when it moves. When a group splits, the new group inherits the brain with small
//          mutations, so herds that move well grow, split more and pass their wiring on.
#pragma once

#include "ElevationGrid.h"
#include "Parallel.h"
#include "Random.h"
#include "Species.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace earth
{

constexpr int kAnimalInputs = 6;
constexpr int kAnimalHidden = 5;
constexpr int kAnimalOutputs = 4;

inline const char* AnimalInputName(int I)
{
	static const char* Names[kAnimalInputs] = {"Hunger", "Food here", "Better food near", "Danger here", "Season", "Crowding"};
	return (I >= 0 && I < kAnimalInputs) ? Names[I] : "?";
}

// What each output does when the group picks where to go next.
inline const char* AnimalOutputName(int O)
{
	static const char* Names[kAnimalOutputs] = {"Pull of food", "Caution", "Urge to roam", "Urge to stay"};
	return (O >= 0 && O < kAnimalOutputs) ? Names[O] : "?";
}

enum class AnimalMove : int
{
	Stayed,
	ToFood,
	Fled,
	Roamed
};

inline const char* AnimalMoveName(int M)
{
	static const char* Names[4] = {"Staying put", "Moving to better food", "Moving away from hunters", "Roaming"};
	return (M >= 0 && M < 4) ? Names[M] : "?";
}

// Inputs -> tanh hidden layer -> outputs that are never negative.
struct AnimalBrain
{
	std::array<float, kAnimalInputs * kAnimalHidden> W1{};
	std::array<float, kAnimalHidden> B1{};
	std::array<float, kAnimalHidden * kAnimalOutputs> W2{};
	std::array<float, kAnimalOutputs> B2{};

	void Think(const float* In, float* Hidden, float* Out) const
	{
		for (int h = 0; h < kAnimalHidden; ++h)
		{
			float Sum = B1[h];
			for (int i = 0; i < kAnimalInputs; ++i)
			{
				Sum += In[i] * W1[i * kAnimalHidden + h];
			}
			Hidden[h] = std::tanh(Sum);
		}
		for (int o = 0; o < kAnimalOutputs; ++o)
		{
			float Sum = B2[o];
			for (int h = 0; h < kAnimalHidden; ++h)
			{
				Sum += Hidden[h] * W2[h * kAnimalOutputs + o];
			}
			Out[o] = std::max(0.0f, Sum);
		}
	}
};

// The wiring every species starts with: hunger strengthens the pull of food, danger makes
// the group careful, crowding makes it roam.
inline AnimalBrain InstinctAnimalBrain()
{
	AnimalBrain B;
	auto Link1 = [&](int In, int Hid, float W) { B.W1[In * kAnimalHidden + Hid] = W; };
	auto Link2 = [&](int Hid, int Out, float W) { B.W2[Hid * kAnimalOutputs + Out] = W; };
	Link1(0, 0, 2.0f); B.B1[0] = -1.0f; // hunger
	Link1(3, 1, 3.0f);                  // danger
	Link1(2, 2, 3.0f);                  // better food nearby
	Link1(5, 3, 2.0f); B.B1[3] = -1.0f; // crowding
	Link1(4, 4, 2.0f); B.B1[4] = -1.0f; // season
	Link2(0, 0, 0.3f);
	Link2(1, 1, 0.5f);
	Link2(3, 2, 0.3f);
	B.B2 = {1.0f, 0.5f, 0.3f, 0.0f};
	return B;
}

struct AnimalGroup
{
	int SpeciesIndex = 0;
	uint32_t Id = 0;
	int Generation = 0;   // How many splits separate this group from the first groups.
	double Lat = 0.0;
	double Lon = 0.0;
	double Count = 0.0;
	double WellFed = 1.0; // 0 starving .. 1 full, smoothed over recent days.
	double Heading = 0.0; // Radians; groups keep roughly the same direction when nothing pulls them elsewhere.
	double Taken = 0.0;   // Animals of this group killed by hunters during the current step.
	double Hunted = 0.0;  // Animals of this group killed by hunters over about the last year.
	AnimalBrain Brain;
	// The last decision, kept so it can be shown.
	std::array<float, kAnimalInputs> LastInputs{};
	std::array<float, kAnimalHidden> LastHidden{};
	std::array<float, kAnimalOutputs> LastOutputs{};
	int LastMove = static_cast<int>(AnimalMove::Stayed);
};

// What a patch of plants is doing, for viewing tools.
struct PlantPatch
{
	double Plants = 0.0;      // kg per km^2 now.
	double Capacity = 0.0;    // kg per km^2 at the best time of year.
	double Peak = 1.0;        // The planet's best possible, kg per km^2.
	double Season = 0.0;      // 0 deep winter .. 1 peak growing season.
	double EatenPerDay = 0.0; // kg per km^2 eaten by animals per day, recently.
	double Lat = 0.0;         // Centre of the patch.
	double Lon = 0.0;
	double SizeDegrees = 2.0;
};

struct EcoEvent
{
	double Year = 0.0;
	std::string Text;
};

struct EcosystemSettings
{
	double SeaLevelMeters = -30.0;
	// Ice ages: sea level and the ice sheets follow Earth's own history, counted from this many
	// years ago when the run starts.
	bool IceAges = true;
	double StartYearsAgo = 315000.0;
	int MaxGroups = 6000;
	double CellDegrees = 2.0;
	// Best plant food per km^2 (kg) in a lush, warm cell.
	double PeakPlantsPerKm2 = 2000.0;
	// Fraction of the remaining room a cell's plants regrow per day in the growing season.
	double PlantGrowthPerDay = 0.03;
	// CPU threads used to move the animals; 0 uses all cores, 1 runs everything on the caller's thread.
	int Threads = 0;
};

class Ecosystem
{
public:
	static constexpr double kDaysPerYear = 365.0;

	Ecosystem(const ElevationGrid* InElevation, std::vector<Species> InSpecies, uint64_t Seed,
		const EcosystemSettings& InSettings = EcosystemSettings())
		: Elevation(InElevation), SpeciesList(std::move(InSpecies)), Random(Seed), BaseSeed(Seed), Settings(InSettings)
	{
		if (Settings.Threads != 1)
		{
			Pool = std::make_unique<WorkerPool>(Settings.Threads > 1 ? Settings.Threads - 1 : 0);
		}
		Cols = static_cast<int>(std::lround(360.0 / Settings.CellDegrees));
		Rows = static_cast<int>(std::lround(180.0 / Settings.CellDegrees));
		SeaChange = Settings.IceAges ? SeaLevelThen(Settings.StartYearsAgo) : 0.0;
		ClimateStep = 0;
		BuildAltitude();
		BuildPlantCells();
		SpawnAnimals();
		SpeciesAlive.assign(SpeciesList.size(), true);
		for (size_t s = 0; s < SpeciesList.size(); ++s)
		{
			if (CountOf(static_cast<int>(s)) <= 0.0)
			{
				SpeciesAlive[s] = false;
			}
		}
	}

	// ---- Queries ----------------------------------------------------------------------------

	double Day() const { return DayNumber; }
	double Years() const { return DayNumber / kDaysPerYear; }
	int DayOfYear() const { return static_cast<int>(std::fmod(DayNumber, kDaysPerYear)); }
	const std::vector<Species>& GetSpecies() const { return SpeciesList; }
	const std::vector<AnimalGroup>& Groups() const { return AnimalGroups; }
	const std::vector<EcoEvent>& Events() const { return EventLog; }

	const AnimalGroup* FindGroup(uint32_t Id) const
	{
		for (const AnimalGroup& G : AnimalGroups)
		{
			if (G.Id == Id)
			{
				return &G;
			}
		}
		return nullptr;
	}

	PlantPatch PlantsAt(double Lat, double Lon) const
	{
		const int Index = CellIndex(Lat, Lon);
		const PlantCell& C = Cells[Index];
		PlantPatch P;
		P.Plants = C.Plants;
		P.Capacity = C.Capacity;
		P.Peak = Settings.PeakPlantsPerKm2;
		P.Season = GrowingSeason(C.Lat);
		P.EatenPerDay = C.EatenPerDay;
		P.Lat = C.Lat;
		P.Lon = -180.0 + (Index % Cols + 0.5) * Settings.CellDegrees;
		P.SizeDegrees = Settings.CellDegrees;
		return P;
	}

	// ---- Climate ----------------------------------------------------------------------------

	double YearsAgo() const { return Settings.StartYearsAgo - Years(); }
	// Sea level compared with today, in metres (about -125 at the depth of an ice age).
	double SeaLevelChange() const { return SeaChange; }
	// How deep in an ice age the world is: 0 as warm as today .. 1 the coldest glacial maximum.
	double IceCover() const { return std::clamp(-SeaChange / 125.0, 0.0, 1.0); }
	// Goes up each time the coastline and plant cover are redrawn.
	int ClimateVersion() const { return ClimateCount; }

	// Global sea level compared with today, a simplified reading of the reconstructed record:
	// warm interglacials near 0 m, glacial maxima down to about -125 m.
	static double SeaLevelThen(double YearsAgoNow)
	{
		static const double Table[][2] = {
			{0, 0}, {6000, 0}, {8000, -15}, {11000, -55}, {15000, -100}, {20000, -125}, {25000, -115},
			{35000, -80}, {45000, -65}, {55000, -70}, {65000, -85}, {75000, -50}, {85000, -25},
			{100000, -30}, {105000, -50}, {115000, -30}, {125000, 5}, {130000, -10}, {140000, -120},
			{160000, -90}, {190000, -60}, {200000, -50}, {215000, -15}, {230000, -60}, {243000, -10},
			{260000, -110}, {285000, -90}, {300000, -60}, {315000, -30}, {330000, 0}};
		const int N = static_cast<int>(sizeof(Table) / sizeof(Table[0]));
		if (YearsAgoNow <= 0.0) return 0.0;
		for (int i = 1; i < N; ++i)
		{
			if (YearsAgoNow <= Table[i][0])
			{
				const double T = (YearsAgoNow - Table[i - 1][0]) / (Table[i][0] - Table[i - 1][0]);
				return Table[i - 1][1] + T * (Table[i][1] - Table[i - 1][1]);
			}
		}
		return Table[N - 1][1];
	}

	bool IsLand(double Lat, double Lon) const { return AltitudeAt(Lat, Lon) > 0.0; }

	// Height above the current sea level in metres, from a fast quarter-degree lookup.
	double AltitudeAt(double Lat, double Lon) const
	{
		if (Altitude.empty())
		{
			return -1.0;
		}
		double L = std::fmod(Lon + 180.0, 360.0);
		if (L < 0.0) L += 360.0;
		const int C = std::clamp(static_cast<int>(L * kAltitudeCellsPerDegree), 0, kAltitudeCols - 1);
		const int R = std::clamp(static_cast<int>((90.0 - Lat) * kAltitudeCellsPerDegree), 0, kAltitudeRows - 1);
		return Altitude[static_cast<size_t>(R) * kAltitudeCols + C];
	}

	// Plant food in the cell as a fraction of the planet's best possible (0..1).
	double PlantCover(double Lat, double Lon) const
	{
		return Cells[CellIndex(Lat, Lon)].Plants / std::max(1.0, Settings.PeakPlantsPerKm2);
	}

	// Plant food in the cell at the best time of year, as a fraction of the planet's best possible (0..1).
	double BestCover(double Lat, double Lon) const
	{
		return Cells[CellIndex(Lat, Lon)].Capacity / std::max(1.0, Settings.PeakPlantsPerKm2);
	}

	// Average plant cover over all land cells, relative to each cell's own maximum.
	double AverageLandCover() const
	{
		double Sum = 0.0;
		double Count = 0.0;
		for (const PlantCell& C : Cells)
		{
			if (C.Capacity > 0.0)
			{
				Sum += C.Plants / C.Capacity;
				Count += 1.0;
			}
		}
		return Count > 0.0 ? Sum / Count : 0.0;
	}

	double CountOf(int SpeciesIndex) const
	{
		double Total = 0.0;
		for (const AnimalGroup& G : AnimalGroups)
		{
			if (G.SpeciesIndex == SpeciesIndex)
			{
				Total += G.Count;
			}
		}
		return Total;
	}

	double TotalAnimals() const
	{
		double Total = 0.0;
		for (const AnimalGroup& G : AnimalGroups)
		{
			Total += G.Count;
		}
		return Total;
	}

	int SpeciesAliveCount() const
	{
		return static_cast<int>(std::count(SpeciesAlive.begin(), SpeciesAlive.end(), true));
	}

	// 0 = deep winter, 1 = peak growing season, for a latitude on the current day.
	double GrowingSeason(double Lat) const
	{
		const double Phase = 2.0 * 3.14159265358979323846 * (DayOfYear() - 172) / kDaysPerYear;
		const double Summer = (Lat >= 0.0 ? 1.0 : -1.0) * std::cos(Phase); // 1 midsummer, -1 midwinter
		const double Strength = std::clamp(std::abs(Lat) / (55.0 - 10.0 * IceCover()), 0.0, 1.0);
		return std::clamp(1.0 - Strength * (1.0 - Summer) * 0.6, 0.0, 1.0);
	}

	// How wet a place is (0 dry .. 1 wet), from how much it can grow at its best. Stands in for
	// rivers, lakes and springs until real water data is added.
	double WaterAt(double Lat, double Lon) const
	{
		return std::clamp(Cells[CellIndex(Lat, Lon)].Capacity / std::max(1.0, Settings.PeakPlantsPerKm2) * 1.5, 0.0, 1.0);
	}

	// ---- Sharing the land with people -------------------------------------------------------
	// These read the animals' positions as of the last RefreshIndex().

	// Rebuilds the per-cell lists after StepDay() has moved, split and removed groups.
	// Cheap to call often: it only rebuilds when groups have moved since the last time.
	void RefreshIndex()
	{
		if (IndexStale)
		{
			BuildGroupIndex();
		}
	}

	// Meat-eating animals in the cell around a point and the eight cells next to it.
	double PredatorsNear(double Lat, double Lon) const
	{
		double Total = 0.0;
		ForNeighbourhood(CellIndex(Lat, Lon), [&](int C)
		{
			for (int Idx : GroupsInCell[C])
			{
				if (SpeciesList[AnimalGroups[Idx].SpeciesIndex].Eats == Diet::Meat)
				{
					Total += AnimalGroups[Idx].Count;
				}
			}
		});
		return Total;
	}

	// Plant-eating animals no heavier than MaxMassKg around a point (same area as PredatorsNear).
	double GameNear(double Lat, double Lon, double MaxMassKg) const
	{
		double Total = 0.0;
		ForNeighbourhood(CellIndex(Lat, Lon), [&](int C)
		{
			for (int Idx : GroupsInCell[C])
			{
				const Species& S = SpeciesList[AnimalGroups[Idx].SpeciesIndex];
				if (S.Eats == Diet::Plants && S.BodyMassKg <= MaxMassKg)
				{
					Total += AnimalGroups[Idx].Count;
				}
			}
		});
		return Total;
	}

	// Kills one plant-eating animal no heavier than MaxMassKg near a point, picked at random
	// with bigger herds more likely. Returns the species index, or -1 if there was nothing to catch.
	int KillOneAnimal(double Lat, double Lon, double MaxMassKg, Rng& Dice)
	{
		std::vector<int> Candidates;
		double Total = 0.0;
		ForNeighbourhood(CellIndex(Lat, Lon), [&](int C)
		{
			for (int Idx : GroupsInCell[C])
			{
				const AnimalGroup& G = AnimalGroups[Idx];
				const Species& S = SpeciesList[G.SpeciesIndex];
				if (S.Eats == Diet::Plants && S.BodyMassKg <= MaxMassKg && CanTakeOne(G))
				{
					Candidates.push_back(Idx);
					Total += G.Count;
				}
			}
		});
		double Pick = Dice.Range(0.0, Total);
		for (int Idx : Candidates)
		{
			Pick -= AnimalGroups[Idx].Count;
			if (Pick <= 0.0)
			{
				AnimalGroups[Idx].Count -= 1.0;
				AnimalGroups[Idx].Hunted += 1.0;
				return AnimalGroups[Idx].SpeciesIndex;
			}
		}
		return -1;
	}

	// Kills up to N plant-eating animals no heavier than MaxMassKg near a point, each picked like
	// KillOneAnimal(), stopping once StopAtKg of animal has been killed. Returns how many of each
	// species were killed.
	std::vector<int> KillAnimals(double Lat, double Lon, double MaxMassKg, int N, double StopAtKg, Rng& Dice)
	{
		std::vector<int> Killed(SpeciesList.size(), 0);
		std::vector<int> Candidates;
		double Total = 0.0;
		ForNeighbourhood(CellIndex(Lat, Lon), [&](int C)
		{
			for (int Idx : GroupsInCell[C])
			{
				const AnimalGroup& G = AnimalGroups[Idx];
				const Species& S = SpeciesList[G.SpeciesIndex];
				if (S.Eats == Diet::Plants && S.BodyMassKg <= MaxMassKg && CanTakeOne(G))
				{
					Candidates.push_back(Idx);
					Total += G.Count;
				}
			}
		});
		double KilledKg = 0.0;
		for (int k = 0; k < N && Total > 0.0 && KilledKg < StopAtKg; ++k)
		{
			double Pick = Dice.Range(0.0, Total);
			for (int Idx : Candidates)
			{
				AnimalGroup& G = AnimalGroups[Idx];
				if (!CanTakeOne(G))
				{
					continue;
				}
				Pick -= G.Count;
				if (Pick <= 0.0)
				{
					G.Count -= 1.0;
					G.Hunted += 1.0;
					Total -= CanTakeOne(G) ? 1.0 : G.Count + 1.0;
					Killed[G.SpeciesIndex] += 1;
					KilledKg += SpeciesList[G.SpeciesIndex].BodyMassKg;
					break;
				}
			}
		}
		return Killed;
	}

	// People can't take more of a herd a year than half the young it has in a year, counting what
	// predators took too: the rest stay out of reach. Slow breeders like hippos can take little.
	bool CanTakeOne(const AnimalGroup& G) const
	{
		const double Share = std::min(0.2, 0.5 * SpeciesList[G.SpeciesIndex].BirthsPerYear);
		return G.Count >= 2.0 && G.Hunted + 1.0 <= (G.Count + G.Hunted) * Share;
	}

	// ---- Simulation -------------------------------------------------------------------------

	void StepDay() { Step(1); }

	// Advances Days days in one go. Steps longer than a day are coarser (groups move and eat
	// once for the whole span) but much faster, for fast forward. Up to 30 days at a time.
	void Step(int Days)
	{
		Days = std::clamp(Days, 1, 30);
		UpdateClimate();
		GrowPlants(Days);
		BuildGroupIndex();
		// Every group decides where to go from the same picture of the world, in parallel,
		// then they eat one after another.
		const std::function<void(size_t)> MoveOne = [&](size_t i)
		{
			AnimalGroup& G = AnimalGroups[i];
			if (G.Count > 0.0)
			{
				Rng Dice(BaseSeed ^ (static_cast<uint64_t>(DayNumber) * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(G.Id) * 0xBF58476D1CE4E5B9ull));
				Move(G, SpeciesList[G.SpeciesIndex], Days, Dice);
			}
		};
		if (Pool)
		{
			Pool->For(AnimalGroups.size(), MoveOne);
		}
		else
		{
			for (size_t i = 0; i < AnimalGroups.size(); ++i)
			{
				MoveOne(i);
			}
		}
		const double Remembered = std::exp(-Days / kDaysPerYear);
		for (AnimalGroup& G : AnimalGroups)
		{
			G.Taken = 0.0;
			G.Hunted *= Remembered;
		}
		const size_t Existing = AnimalGroups.size();
		for (size_t i = 0; i < Existing; ++i)
		{
			if (AnimalGroups[i].Count > 0.0)
			{
				LiveOneDay(i, Days);
			}
		}
		RemoveDeadAndSplit();
		IndexStale = true;
		DayNumber += Days;
	}

	void StepDays(int N)
	{
		for (int i = 0; i < N; ++i)
		{
			StepDay();
		}
	}

private:
	struct PlantCell
	{
		double Capacity = 0.0; // kg per km^2 at the best time of year.
		double Plants = 0.0;   // kg per km^2 right now.
		double Lat = 0.0;
		double EatenPerDay = 0.0; // kg per km^2, during the last step.
	};

	static constexpr int kAltitudeCellsPerDegree = 4;
	static constexpr int kAltitudeCols = 360 * kAltitudeCellsPerDegree;
	static constexpr int kAltitudeRows = 180 * kAltitudeCellsPerDegree;

	const ElevationGrid* Elevation;
	std::vector<float> Ground; // Today's height above sea level, quarter-degree.
	std::vector<float> Altitude; // Height above the sea level of the time.
	double SeaChange = 0.0;
	int ClimateStep = 0;
	int ClimateCount = 0;
	std::vector<int> TouchedCells;
	bool IndexStale = true;
	std::vector<Species> SpeciesList;
	Rng Random;
	uint64_t BaseSeed = 0;
	EcosystemSettings Settings;
	std::unique_ptr<WorkerPool> Pool;
	uint32_t NextGroupId = 1;

	int Cols = 180;
	int Rows = 90;
	std::vector<PlantCell> Cells;
	std::vector<AnimalGroup> AnimalGroups;
	std::vector<std::vector<int>> GroupsInCell;
	// Per-cell totals rebuilt each day: prey kg available to each hunter species, and
	// hunters threatening each prey species. Indexed [cell * species count + species].
	std::vector<double> PreyKgFor;
	std::vector<double> HuntersOf;
	std::vector<int> Hunters; // Indices of meat-eating species.
	std::vector<bool> SpeciesAlive;
	std::vector<EcoEvent> EventLog;
	double DayNumber = 0.0;

	int CellIndex(double Lat, double Lon) const
	{
		double L = std::fmod(Lon + 180.0, 360.0);
		if (L < 0.0) L += 360.0;
		const int C = std::clamp(static_cast<int>(L / Settings.CellDegrees), 0, Cols - 1);
		const int R = std::clamp(static_cast<int>((90.0 - Lat) / Settings.CellDegrees), 0, Rows - 1);
		return R * Cols + C;
	}

	void BuildAltitude()
	{
		if (!Elevation || !Elevation->IsValid())
		{
			return;
		}
		Ground.resize(static_cast<size_t>(kAltitudeRows) * kAltitudeCols);
		for (int R = 0; R < kAltitudeRows; ++R)
		{
			const double Lat = 90.0 - (R + 0.5) / kAltitudeCellsPerDegree;
			for (int C = 0; C < kAltitudeCols; ++C)
			{
				const double Lon = -180.0 + (C + 0.5) / kAltitudeCellsPerDegree;
				Ground[static_cast<size_t>(R) * kAltitudeCols + C] = static_cast<float>(Elevation->Sample(Lat, Lon));
			}
		}
		ApplySeaLevel();
	}

	void ApplySeaLevel()
	{
		Altitude.resize(Ground.size());
		const float Sea = static_cast<float>(Settings.SeaLevelMeters + SeaChange);
		for (size_t i = 0; i < Ground.size(); ++i) Altitude[i] = Ground[i] - Sea;
	}

	// Every century, moves the coastline and the ice to where they were at that time, and regrows
	// plant capacity to match. Land the sea gives back starts half grown.
	void UpdateClimate()
	{
		const int Step = static_cast<int>(Years() / 100.0);
		if (!Settings.IceAges || Step == ClimateStep || Ground.empty())
		{
			return;
		}
		ClimateStep = Step;
		const double Sea = SeaLevelThen(YearsAgo());
		if (std::abs(Sea - SeaChange) < 1.0)
		{
			return;
		}
		SeaChange = Sea;
		ApplySeaLevel();
		std::vector<double> Old(Cells.size());
		for (size_t i = 0; i < Cells.size(); ++i) Old[i] = Cells[i].Capacity;
		BuildPlantCells();
		for (size_t i = 0; i < Cells.size(); ++i)
		{
			Cells[i].Plants = Old[i] > 0.0 ? std::min(Cells[i].Plants, Cells[i].Capacity) : Cells[i].Capacity * 0.5;
		}
		++ClimateCount;
	}

	// How much plant food a place can grow, from latitude and altitude. A placeholder
	// climate until a real one drives it: rich tropics, a dry subtropical belt, poor tundra.
	double PlantCapacityAt(double Lat, double Lon) const
	{
		if (!IsLand(Lat, Lon))
		{
			return 0.0;
		}
		const double Height = AltitudeAt(Lat, Lon);
		const double AbsLat = std::abs(Lat);
		if (AbsLat > 75.0 || Height > 4500.0)
		{
			return 0.0;
		}
		// Ice sheets: over North America down to about 42 degrees north and northern Europe to
		// about 54 at the coldest, while Siberia and Beringia stay open, cold steppe.
		const double Ice = IceCover();
		if ((Lon > -140.0 && Lon < -50.0 && Lat > 75.0 - 33.0 * Ice) || (Lon > -10.0 && Lon < 60.0 && Lat > 75.0 - 21.0 * Ice))
		{
			return 0.0;
		}
		const double Desert = std::clamp(1.0 - std::abs(AbsLat - 24.0) / 8.0, 0.0, 1.0) * 0.85;
		const double Cold = std::clamp((AbsLat - (50.0 - 12.0 * Ice)) / 25.0, 0.0, 1.0) * 0.8;
		const double High = std::clamp((Height - 2500.0) / 2000.0, 0.0, 1.0);
		return Settings.PeakPlantsPerKm2 * (1.0 - Desert) * (1.0 - Cold) * (1.0 - High);
	}

	void BuildPlantCells()
	{
		Cells.assign(static_cast<size_t>(Rows) * Cols, PlantCell());
		for (int R = 0; R < Rows; ++R)
		{
			for (int C = 0; C < Cols; ++C)
			{
				PlantCell& Cell = Cells[static_cast<size_t>(R) * Cols + C];
				// Average a 3x3 sample so coastal cells get part of their capacity.
				double Sum = 0.0;
				for (int a = 0; a < 3; ++a)
				{
					for (int b = 0; b < 3; ++b)
					{
						const double Lat = 90.0 - (R + (a + 0.5) / 3.0) * Settings.CellDegrees;
						const double Lon = -180.0 + (C + (b + 0.5) / 3.0) * Settings.CellDegrees;
						Sum += PlantCapacityAt(Lat, Lon);
					}
				}
				Cell.Lat = 90.0 - (R + 0.5) * Settings.CellDegrees;
				Cell.Capacity = Sum / 9.0;
				Cell.Plants = Cell.Capacity * 0.7;
			}
		}
	}

	bool Habitable(const Species& S, double Lat, double Lon) const
	{
		const double Height = AltitudeAt(Lat, Lon);
		return Height > 0.0 && Height <= S.MaxElevationM && S.InRange(Lat, Lon);
	}

	void SpawnAnimals()
	{
		const AnimalBrain Instinct = InstinctAnimalBrain();
		// Plant eaters first, so hunters can start next to their prey.
		for (int Pass = 0; Pass < 2; ++Pass)
		{
			for (size_t s = 0; s < SpeciesList.size(); ++s)
			{
				const Species& S = SpeciesList[s];
				if (S.Range.empty() || (Pass == 0) != (S.Eats == Diet::Plants))
				{
					continue;
				}
				std::vector<int> PreyGroups;
				if (S.Eats == Diet::Meat)
				{
					for (size_t i = 0; i < AnimalGroups.size(); ++i)
					{
						if (IsPrey(S, AnimalGroups[i]) && S.InRange(AnimalGroups[i].Lat, AnimalGroups[i].Lon))
						{
							PreyGroups.push_back(static_cast<int>(i));
						}
					}
				}
				for (int g = 0; g < S.StartGroups; ++g)
				{
					for (int Try = 0; Try < 200; ++Try)
					{
						double Lat = 0.0;
						double Lon = 0.0;
						if (!PreyGroups.empty() && Try < 100)
						{
							const AnimalGroup& Near = AnimalGroups[PreyGroups[static_cast<size_t>(Random.Next() % PreyGroups.size())]];
							Lat = Near.Lat + Random.Range(-1.0, 1.0);
							Lon = Near.Lon + Random.Range(-1.0, 1.0);
						}
						else
						{
							const RangeBox& B = S.Range[static_cast<size_t>(Random.Next() % S.Range.size())];
							Lat = Random.Range(B.LatMin, B.LatMax);
							Lon = Random.Range(B.LonMin, B.LonMax);
						}
						// Prefer places with food, but settle anywhere habitable if none turns up.
						const bool FoodOk = S.Eats == Diet::Meat || Try > 100 || Cells[CellIndex(Lat, Lon)].Capacity > 50.0;
						if (Habitable(S, Lat, Lon) && FoodOk)
						{
							AnimalGroup G;
							G.SpeciesIndex = static_cast<int>(s);
							G.Lat = Lat;
							G.Lon = Lon;
							G.Count = S.GroupSize * Random.Range(0.6, 1.4);
							G.Heading = Random.Range(0.0, 6.283185307179586);
							G.Id = NextGroupId++;
							G.Brain = Instinct;
							AnimalGroups.push_back(G);
							break;
						}
					}
				}
			}
		}
	}

	void GrowPlants(int Days)
	{
		for (PlantCell& C : Cells)
		{
			if (C.Capacity <= 0.0)
			{
				continue;
			}
			C.EatenPerDay = 0.0;
			const double Season = GrowingSeason(C.Lat);
			const double Target = C.Capacity * (0.15 + 0.85 * Season);
			if (C.Plants < Target)
			{
				const double Rate = Settings.PlantGrowthPerDay * (0.2 + 0.8 * Season);
				C.Plants += (Target - C.Plants) * (Days == 1 ? Rate : 1.0 - std::pow(1.0 - Rate, Days));
			}
			else
			{
				// Out of season, plants die back towards the winter level.
				C.Plants -= (C.Plants - Target) * (Days == 1 ? 0.01 : 1.0 - std::pow(0.99, Days));
			}
		}
	}

	void BuildGroupIndex()
	{
		const size_t NS = SpeciesList.size();
		if (Hunters.empty())
		{
			for (size_t s = 0; s < NS; ++s)
			{
				if (SpeciesList[s].Eats == Diet::Meat)
				{
					Hunters.push_back(static_cast<int>(s));
				}
			}
		}
		if (GroupsInCell.size() != Cells.size())
		{
			GroupsInCell.assign(Cells.size(), std::vector<int>());
			PreyKgFor.assign(Cells.size() * NS, 0.0);
			HuntersOf.assign(Cells.size() * NS, 0.0);
		}
		// Clear only the cells used yesterday; most of the planet is empty.
		for (int Cell : TouchedCells)
		{
			GroupsInCell[Cell].clear();
			std::fill_n(PreyKgFor.begin() + static_cast<std::ptrdiff_t>(Cell * NS), NS, 0.0);
			std::fill_n(HuntersOf.begin() + static_cast<std::ptrdiff_t>(Cell * NS), NS, 0.0);
		}
		TouchedCells.clear();
		IndexStale = false;
		for (size_t i = 0; i < AnimalGroups.size(); ++i)
		{
			const AnimalGroup& G = AnimalGroups[i];
			const size_t Cell = static_cast<size_t>(CellIndex(G.Lat, G.Lon));
			if (GroupsInCell[Cell].empty())
			{
				TouchedCells.push_back(static_cast<int>(Cell));
			}
			GroupsInCell[Cell].push_back(static_cast<int>(i));
			const Species& S = SpeciesList[G.SpeciesIndex];
			if (S.Eats == Diet::Plants)
			{
				for (int h : Hunters)
				{
					if (IsPrey(SpeciesList[h], G))
					{
						PreyKgFor[Cell * NS + h] += G.Count * S.BodyMassKg;
					}
				}
			}
			else
			{
				for (size_t q = 0; q < NS; ++q)
				{
					const Species& Q = SpeciesList[q];
					if (Q.Eats == Diet::Plants && Q.BodyMassKg >= S.PreyMassMinKg && Q.BodyMassKg <= S.PreyMassMaxKg)
					{
						HuntersOf[Cell * NS + q] += G.Count;
					}
				}
			}
		}
	}

	static double CellAreaKm2(double Lat, double CellDegrees)
	{
		const double Side = 111.0 * CellDegrees;
		return Side * Side * std::max(0.05, std::cos(Lat * 3.14159265358979323846 / 180.0));
	}

	bool IsPrey(const Species& Hunter, const AnimalGroup& Target) const
	{
		const Species& T = SpeciesList[Target.SpeciesIndex];
		return T.Eats == Diet::Plants && T.BodyMassKg >= Hunter.PreyMassMinKg && T.BodyMassKg <= Hunter.PreyMassMaxKg && Target.Count >= 1.0;
	}

	// Prey animals available to a hunter species in a cell, in kg (as of the start of the day).
	double PreyInCell(int HunterIndex, int Cell) const
	{
		return PreyKgFor[static_cast<size_t>(Cell) * SpeciesList.size() + HunterIndex];
	}

	double HuntersInCell(int PreyIndex, int Cell) const
	{
		return HuntersOf[static_cast<size_t>(Cell) * SpeciesList.size() + PreyIndex];
	}

	void LiveOneDay(size_t Index, int Days)
	{
		AnimalGroup& G = AnimalGroups[Index];
		const Species& S = SpeciesList[G.SpeciesIndex];

		// Eat.
		const int Cell = CellIndex(G.Lat, G.Lon);
		const double Need = G.Count * S.FoodPerDayKg() * Days;
		double Eaten = 0.0;
		if (S.Eats == Diet::Plants)
		{
			// Graze the home cell first, then spread out over the cells around it.
			auto Graze = [&](int CellToEat)
			{
				PlantCell& C = Cells[CellToEat];
				const double Area = CellAreaKm2(C.Lat, Settings.CellDegrees);
				const double Take = std::min(Need - Eaten, C.Plants * Area * 0.5);
				if (Take > 0.0)
				{
					Eaten += Take;
					C.Plants = std::max(0.0, C.Plants - Take / Area);
					C.EatenPerDay += Take / Area / Days;
				}
			};
			Graze(Cell);
			if (Eaten < Need)
			{
				ForNeighbourhood(Cell, [&](int C) { if (C != Cell && Eaten < Need) Graze(C); });
			}
		}
		else
		{
			Eaten = Hunt(G, S, Cell, Need, Days);
		}
		const double Fed = Need > 0.0 ? std::clamp(Eaten / Need, 0.0, 1.0) : 1.0;
		const double Keep = Days == 1 ? 0.9 : std::pow(0.9, Days);
		G.WellFed = G.WellFed * Keep + Fed * (1.0 - Keep);

		// Births and deaths, as daily shares of yearly rates.
		// Animals cope with lean spells; only real hunger kills, and only the well fed breed.
		double Births = G.WellFed > 0.5 ? S.BirthsPerYear * (G.WellFed - 0.5) / 0.5 : 0.0;
		if (S.Eats == Diet::Plants && Births > 0.0)
		{
			// Fewer young where the land is grazed below what the season should hold, and none
			// once it is down to 60%, so herds level off with plenty of plants left standing
			// (for other species and for people) instead of eating the land bare.
			const PlantCell& Home = Cells[Cell];
			const double Expected = Home.Capacity * (0.15 + 0.85 * GrowingSeason(Home.Lat));
			Births *= Expected > 0.0 ? std::clamp((Home.Plants / Expected - 0.6) / 0.3, 0.0, 1.0) : 0.0;
		}
		if (S.Eats == Diet::Meat && Births > 0.0)
		{
			// Hunters are territorial: they only raise young where there is far more prey
			// around than all the local hunters eat in a year, which keeps them from booming
			// and then starving, and lets rival hunters share a region.
			const double Ratio = LocalPreyKg(G.SpeciesIndex, Cell) / std::max(1.0, LocalHunterAppetiteKg(Cell) * 365.0);
			Births *= std::clamp(Ratio / 3.0 - 1.0, 0.0, 1.0);
		}
		const double Deaths = 1.0 / S.LifespanYears + std::max(0.0, 0.5 - G.WellFed) * 4.0;
		G.Count += G.Count * (Days == 1 ? (Births - Deaths) / kDaysPerYear : std::expm1((Births - Deaths) * Days / kDaysPerYear));
		if (G.Count < 1.0)
		{
			G.Count = 0.0;
		}
	}

	// Prey (kg) and same-species hunters in a cell and its eight neighbours.
	double LocalPreyKg(int HunterIndex, int Cell) const
	{
		double Kg = 0.0;
		ForNeighbourhood(Cell, [&](int C) { Kg += PreyInCell(HunterIndex, C); });
		return Kg;
	}

	// Meat (kg per day) eaten by all hunters in a cell and its eight neighbours.
	double LocalHunterAppetiteKg(int Cell) const
	{
		double Kg = 0.0;
		ForNeighbourhood(Cell, [&](int C)
		{
			for (int Idx : GroupsInCell[C])
			{
				const Species& HS = SpeciesList[AnimalGroups[Idx].SpeciesIndex];
				if (HS.Eats == Diet::Meat)
				{
					Kg += AnimalGroups[Idx].Count * HS.FoodPerDayKg();
				}
			}
		});
		return Kg;
	}

	template <typename Fn>
	void ForNeighbourhood(int Cell, Fn&& Visit) const
	{
		const int R = Cell / Cols;
		const int C = Cell % Cols;
		for (int dr = -1; dr <= 1; ++dr)
		{
			if (R + dr < 0 || R + dr >= Rows)
			{
				continue;
			}
			for (int dc = -1; dc <= 1; ++dc)
			{
				Visit((R + dr) * Cols + ((C + dc) % Cols + Cols) % Cols);
			}
		}
	}

	// Hunts in the group's cell and the two rings of cells around it (a group stands for
	// hunters spread over a region).
	double Hunt(AnimalGroup& G, const Species& S, int Cell, double NeedKg, int Days)
	{
		double Got = 0.0;
		const int R = Cell / Cols;
		const int C = Cell % Cols;
		for (int dr = -2; dr <= 2 && Got < NeedKg; ++dr)
		{
			if (R + dr < 0 || R + dr >= Rows)
			{
				continue;
			}
			for (int dc = -2; dc <= 2 && Got < NeedKg; ++dc)
			{
				const int Nc = ((C + dc) % Cols + Cols) % Cols;
				for (int Idx : GroupsInCell[(R + dr) * Cols + Nc])
				{
					if (Got >= NeedKg)
					{
						break;
					}
					AnimalGroup& Prey = AnimalGroups[Idx];
					if (!IsPrey(S, Prey))
					{
						continue;
					}
					const double PreyMass = SpeciesList[Prey.SpeciesIndex].BodyMassKg;
					// Hunts often fail, and much more so when prey is scarce and spread out (a
					// "type III" response), which stops hunters wiping out their own food supply.
					const double Density = Prey.Count / SpeciesList[Prey.SpeciesIndex].GroupSize;
					const double MaxKills = G.Count * 0.15 * Days * Density * Density / (1.0 + Density * Density);
					const double Wanted = (NeedKg - Got) / (PreyMass * 0.6);
					// All hunters together take at most half the young a herd has in a year (and never
					// more than a fifth of it), so hunters starve before their prey is wiped out.
					const double Allowed = (Prey.Count + Prey.Taken) * std::min(0.2, 0.5 * SpeciesList[Prey.SpeciesIndex].BirthsPerYear) * Days / kDaysPerYear - Prey.Taken;
					const double Kills = std::min({Wanted, MaxKills, Allowed, Prey.Count - 1.0});
					if (Kills <= 0.0)
					{
						continue;
					}
					Prey.Count -= Kills;
					Prey.Taken += Kills;
					Prey.Hunted += Kills;
					Got += Kills * PreyMass * 0.6;
				}
			}
		}
		return Got;
	}

	// Look at nearby spots and move to the best one, weighing food, danger and the urge to
	// keep going the same way as the group's brain says. When nothing nearby is better, keep
	// wandering roughly the same way, so groups explore instead of jittering in place.
	void Move(AnimalGroup& G, const Species& S, int Days, Rng& Dice) const
	{
		const double Pi = 3.14159265358979323846;
		const double StepKm = S.SpeedKmPerDay * Dice.Range(0.3, 1.0) * std::min(Days, 6);
		const double CosLat = std::max(0.1, std::cos(G.Lat * Pi / 180.0));

		struct Option
		{
			double Lat, Lon, Angle, Food, Danger;
		};
		std::array<Option, 9> Options;
		int Count = 0;
		double DangerHere = 0.0;
		const double FoodHere = Food(G, S, G.Lat, G.Lon, DangerHere);
		Options[Count++] = {G.Lat, G.Lon, G.Heading, FoodHere, DangerHere};
		double BestFood = FoodHere;
		for (int d = 0; d < 8; ++d)
		{
			const double Angle = (d + Dice.Uniform()) * Pi / 4.0;
			const double Lat = G.Lat + std::sin(Angle) * StepKm / 111.0;
			double Lon = G.Lon + std::cos(Angle) * StepKm / (111.0 * CosLat);
			if (Lat > 89.0 || Lat < -89.0)
			{
				continue;
			}
			if (Lon > 180.0) Lon -= 360.0;
			if (Lon < -180.0) Lon += 360.0;
			if (!Habitable(S, Lat, Lon))
			{
				continue;
			}
			double Danger = 0.0;
			const double F = Food(G, S, Lat, Lon, Danger);
			BestFood = std::max(BestFood, F);
			Options[Count++] = {Lat, Lon, Angle, F, Danger};
		}

		float* In = G.LastInputs.data();
		In[0] = static_cast<float>(1.0 - G.WellFed);
		In[1] = static_cast<float>(std::tanh(FoodHere));
		In[2] = static_cast<float>(std::tanh(BestFood - FoodHere));
		In[3] = static_cast<float>(DangerHere);
		In[4] = static_cast<float>(GrowingSeason(G.Lat));
		In[5] = static_cast<float>(std::clamp(G.Count / (S.GroupSize * 4.0), 0.0, 1.0));
		G.Brain.Think(In, G.LastHidden.data(), G.LastOutputs.data());
		const double FoodPull = G.LastOutputs[0];
		const double Caution = G.LastOutputs[1];
		const double Roam = 0.1 * G.LastOutputs[2];
		const double Settle = 0.1 * G.LastOutputs[3];

		int Best = 0;
		double BestScore = FoodPull * FoodHere - Caution * DangerHere + Settle + Dice.Range(0.0, 0.02);
		for (int k = 1; k < Count; ++k)
		{
			const Option& O = Options[k];
			const double Sc = FoodPull * O.Food - Caution * O.Danger + Roam * std::cos(O.Angle - G.Heading) + Dice.Range(0.0, 0.02);
			if (Sc > BestScore)
			{
				BestScore = Sc;
				Best = k;
			}
		}
		if (Best == 0)
		{
			G.Heading += Dice.Range(-1.5, 1.5);
			G.LastMove = static_cast<int>(AnimalMove::Stayed);
			return;
		}
		const Option& O = Options[Best];
		if (DangerHere > 0.1 && O.Danger < DangerHere)
		{
			G.LastMove = static_cast<int>(AnimalMove::Fled);
		}
		else if (O.Food > FoodHere)
		{
			G.LastMove = static_cast<int>(AnimalMove::ToFood);
		}
		else
		{
			G.LastMove = static_cast<int>(AnimalMove::Roamed);
		}
		G.Lat = O.Lat;
		G.Lon = O.Lon;
		G.Heading = O.Angle;
	}

	// How good the food is at a place (plants: share of the best possible; hunters: a log
	// scale of prey around), and how dangerous it is.
	double Food(const AnimalGroup& G, const Species& S, double Lat, double Lon, double& Danger) const
	{
		const int Cell = CellIndex(Lat, Lon);
		if (S.Eats == Diet::Plants)
		{
			Danger = std::min(1.0, HuntersInCell(G.SpeciesIndex, Cell) / std::max(1.0, G.Count));
			return Cells[Cell].Plants / Settings.PeakPlantsPerKm2;
		}
		Danger = 0.0;
		// Hunters follow prey over a wide area (each group stands for animals spread across
		// a region), weighting nearer cells more.
		const double Need = std::max(1.0, G.Count * S.BodyMassKg * 20.0);
		double Prey = 0.0;
		const int R = Cell / Cols;
		const int C = Cell % Cols;
		const int Radius = 3;
		for (int dr = -Radius; dr <= Radius; ++dr)
		{
			if (R + dr < 0 || R + dr >= Rows)
			{
				continue;
			}
			for (int dc = -Radius; dc <= Radius; ++dc)
			{
				const int Nc = ((C + dc) % Cols + Cols) % Cols;
				const double Dist = std::sqrt(static_cast<double>(dr * dr + dc * dc));
				Prey += PreyInCell(G.SpeciesIndex, (R + dr) * Cols + Nc) / (1.0 + Dist * Dist);
			}
		}
		return std::log1p(Prey / Need);
	}

	void RemoveDeadAndSplit()
	{
		// Compacts the list in place; new groups from splits go on the end.
		const size_t Before = AnimalGroups.size();
		const double Crowding = std::max(1.0, 2.0 * static_cast<double>(Before) / Settings.MaxGroups);
		std::vector<AnimalGroup> Splits;
		std::vector<double> Alive(SpeciesList.size(), 0.0);
		size_t Kept = 0;
		for (size_t i = 0; i < Before; ++i)
		{
			AnimalGroup& G = AnimalGroups[i];
			if (G.Count < 1.0)
			{
				continue;
			}
			const Species& S = SpeciesList[G.SpeciesIndex];
			Alive[G.SpeciesIndex] += G.Count;
			// Groups kept so far, the ones still to look at, plus one extra if this one splits,
			// must fit under the cap. As the planet fills up, groups grow larger before
			// splitting, so the cap is approached gently.
			const size_t Remaining = Before - i;
			if (G.Count > S.GroupSize * 2.0 * Crowding * Crowding && Kept + Splits.size() + Remaining + 1 <= static_cast<size_t>(Settings.MaxGroups))
			{
				G.Count *= 0.5;
				G.Hunted *= 0.5;
				AnimalGroup B = G;
				B.Id = NextGroupId++;
				B.Generation = G.Generation + 1;
				B.Brain = MutateBrain(G.Brain);
				B.Lat = std::clamp(G.Lat + Random.Range(-0.3, 0.3), -89.0, 89.0);
				B.Lon = G.Lon + Random.Range(-0.3, 0.3);
				if (!Habitable(S, B.Lat, B.Lon))
				{
					B.Lat = G.Lat;
					B.Lon = G.Lon;
				}
				Splits.push_back(B);
			}
			if (Kept != i)
			{
				AnimalGroups[Kept] = G;
			}
			++Kept;
		}
		AnimalGroups.resize(Kept);
		AnimalGroups.insert(AnimalGroups.end(), Splits.begin(), Splits.end());

		for (size_t s = 0; s < SpeciesList.size(); ++s)
		{
			if (SpeciesAlive[s] && Alive[s] <= 0.0)
			{
				SpeciesAlive[s] = false;
				EventLog.push_back({Years(), SpeciesList[s].Name + " died out"});
			}
		}
	}

	// A copy of a brain with a few weights nudged at random.
	AnimalBrain MutateBrain(const AnimalBrain& Parent)
	{
		AnimalBrain B = Parent;
		auto Nudge = [&](float* W, size_t N)
		{
			for (size_t i = 0; i < N; ++i)
			{
				if (Random.Chance(0.1))
				{
					W[i] += static_cast<float>(Random.Gaussian() * 0.1);
				}
			}
		};
		Nudge(B.W1.data(), B.W1.size());
		Nudge(B.B1.data(), B.B1.size());
		Nudge(B.W2.data(), B.W2.size());
		Nudge(B.B2.data(), B.B2.size());
		return B;
	}
};

} // namespace earth

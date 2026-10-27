// Plants and animals living on the planet, one simulated day at a time.
// Plain C++ with no Unreal dependencies, so it can be unit tested and later shared with the NPCs.
//
// Plants: a 2-degree grid of edible plant food that grows with the seasons and gets eaten.
// Animals: groups of one species that move, graze or hunt, give birth, starve and die.
#pragma once

#include "ElevationGrid.h"
#include "Random.h"
#include "Species.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace earth
{

struct AnimalGroup
{
	int SpeciesIndex = 0;
	double Lat = 0.0;
	double Lon = 0.0;
	double Count = 0.0;
	double WellFed = 1.0; // 0 starving .. 1 full, smoothed over recent days.
	double Heading = 0.0; // Radians; groups keep roughly the same direction when nothing pulls them elsewhere.
};

struct EcoEvent
{
	double Year = 0.0;
	std::string Text;
};

struct EcosystemSettings
{
	double SeaLevelMeters = -30.0;
	int MaxGroups = 6000;
	double CellDegrees = 2.0;
	// Best plant food per km^2 (kg) in a lush, warm cell.
	double PeakPlantsPerKm2 = 2000.0;
	// Fraction of the remaining room a cell's plants regrow per day in the growing season.
	double PlantGrowthPerDay = 0.03;
};

class Ecosystem
{
public:
	static constexpr double kDaysPerYear = 365.0;

	Ecosystem(const ElevationGrid* InElevation, std::vector<Species> InSpecies, uint64_t Seed,
		const EcosystemSettings& InSettings = EcosystemSettings())
		: Elevation(InElevation), SpeciesList(std::move(InSpecies)), Random(Seed), Settings(InSettings)
	{
		Cols = static_cast<int>(std::lround(360.0 / Settings.CellDegrees));
		Rows = static_cast<int>(std::lround(180.0 / Settings.CellDegrees));
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
		const double Strength = std::clamp(std::abs(Lat) / 55.0, 0.0, 1.0);
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
	void RefreshIndex() { BuildGroupIndex(); }

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
				if (S.Eats == Diet::Plants && S.BodyMassKg <= MaxMassKg && G.Count >= 2.0)
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
				return AnimalGroups[Idx].SpeciesIndex;
			}
		}
		return -1;
	}

	// ---- Simulation -------------------------------------------------------------------------

	void StepDay()
	{
		GrowPlants();
		BuildGroupIndex();
		const size_t Existing = AnimalGroups.size();
		for (size_t i = 0; i < Existing; ++i)
		{
			if (AnimalGroups[i].Count > 0.0)
			{
				LiveOneDay(i);
			}
		}
		RemoveDeadAndSplit();
		DayNumber += 1.0;
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
	};

	static constexpr int kAltitudeCellsPerDegree = 4;
	static constexpr int kAltitudeCols = 360 * kAltitudeCellsPerDegree;
	static constexpr int kAltitudeRows = 180 * kAltitudeCellsPerDegree;

	const ElevationGrid* Elevation;
	std::vector<float> Altitude;
	std::vector<int> TouchedCells;
	std::vector<Species> SpeciesList;
	Rng Random;
	EcosystemSettings Settings;

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
		Altitude.resize(static_cast<size_t>(kAltitudeRows) * kAltitudeCols);
		for (int R = 0; R < kAltitudeRows; ++R)
		{
			const double Lat = 90.0 - (R + 0.5) / kAltitudeCellsPerDegree;
			for (int C = 0; C < kAltitudeCols; ++C)
			{
				const double Lon = -180.0 + (C + 0.5) / kAltitudeCellsPerDegree;
				Altitude[static_cast<size_t>(R) * kAltitudeCols + C] = static_cast<float>(Elevation->Sample(Lat, Lon) - Settings.SeaLevelMeters);
			}
		}
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
		const double Desert = std::clamp(1.0 - std::abs(AbsLat - 24.0) / 8.0, 0.0, 1.0) * 0.85;
		const double Cold = std::clamp((AbsLat - 50.0) / 25.0, 0.0, 1.0) * 0.8;
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
							AnimalGroups.push_back(G);
							break;
						}
					}
				}
			}
		}
	}

	void GrowPlants()
	{
		for (PlantCell& C : Cells)
		{
			if (C.Capacity <= 0.0)
			{
				continue;
			}
			const double Season = GrowingSeason(C.Lat);
			const double Target = C.Capacity * (0.15 + 0.85 * Season);
			if (C.Plants < Target)
			{
				C.Plants += (Target - C.Plants) * Settings.PlantGrowthPerDay * (0.2 + 0.8 * Season);
			}
			else
			{
				// Out of season, plants die back towards the winter level.
				C.Plants -= (C.Plants - Target) * 0.01;
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

	void LiveOneDay(size_t Index)
	{
		AnimalGroup& G = AnimalGroups[Index];
		const Species& S = SpeciesList[G.SpeciesIndex];

		Move(G, S);

		// Eat.
		const int Cell = CellIndex(G.Lat, G.Lon);
		const double Need = G.Count * S.FoodPerDayKg();
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
			Eaten = Hunt(G, S, Cell, Need);
		}
		const double Fed = Need > 0.0 ? std::clamp(Eaten / Need, 0.0, 1.0) : 1.0;
		G.WellFed = G.WellFed * 0.9 + Fed * 0.1;

		// Births and deaths, as daily shares of yearly rates.
		// Animals cope with lean spells; only real hunger kills, and only the well fed breed.
		double Births = G.WellFed > 0.5 ? S.BirthsPerYear * (G.WellFed - 0.5) / 0.5 : 0.0;
		if (S.Eats == Diet::Plants && Births > 0.0)
		{
			// Fewer young where the land is already grazed down, so herds level off instead of overshooting.
			const PlantCell& Home = Cells[Cell];
			Births *= Home.Capacity > 0.0 ? std::clamp(1.5 * Home.Plants / Home.Capacity, 0.0, 1.0) : 0.0;
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
		G.Count += G.Count * (Births - Deaths) / kDaysPerYear;
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

	// Hunts in the group's cell and the cells around it.
	double Hunt(AnimalGroup& G, const Species& S, int Cell, double NeedKg)
	{
		double Got = 0.0;
		const int R = Cell / Cols;
		const int C = Cell % Cols;
		for (int dr = -1; dr <= 1 && Got < NeedKg; ++dr)
		{
			if (R + dr < 0 || R + dr >= Rows)
			{
				continue;
			}
			for (int dc = -1; dc <= 1 && Got < NeedKg; ++dc)
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
					const double MaxKills = G.Count * 0.15 * Density * Density / (1.0 + Density * Density);
					const double Wanted = (NeedKg - Got) / (PreyMass * 0.6);
					const double Kills = std::min({Wanted, MaxKills, Prey.Count - 1.0});
					if (Kills <= 0.0)
					{
						continue;
					}
					Prey.Count -= Kills;
					Got += Kills * PreyMass * 0.6;
				}
			}
		}
		return Got;
	}

	// Look at nearby spots and move to the best one. When nothing nearby is better,
	// keep wandering roughly the same way, so groups explore instead of jittering in place.
	void Move(AnimalGroup& G, const Species& S)
	{
		const double Pi = 3.14159265358979323846;
		const double StepKm = S.SpeedKmPerDay * Random.Range(0.3, 1.0);
		const double CosLat = std::max(0.1, std::cos(G.Lat * Pi / 180.0));
		double BestLat = G.Lat;
		double BestLon = G.Lon;
		double BestAngle = G.Heading;
		double BestScore = Score(G, S, G.Lat, G.Lon) + Random.Range(0.0, 0.02);
		bool Moved = false;
		for (int d = 0; d < 8; ++d)
		{
			const double Angle = (d + Random.Uniform()) * Pi / 4.0;
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
			const double Wander = 0.03 * std::cos(Angle - G.Heading);
			const double Sc = Score(G, S, Lat, Lon) + Wander + Random.Range(0.0, 0.02);
			if (Sc > BestScore)
			{
				BestScore = Sc;
				BestLat = Lat;
				BestLon = Lon;
				BestAngle = Angle;
				Moved = true;
			}
		}
		G.Lat = BestLat;
		G.Lon = BestLon;
		G.Heading = Moved ? BestAngle : G.Heading + Random.Range(-1.5, 1.5);
	}

	double Score(const AnimalGroup& G, const Species& S, double Lat, double Lon) const
	{
		const int Cell = CellIndex(Lat, Lon);
		if (S.Eats == Diet::Plants)
		{
			const double Food = Cells[Cell].Plants / Settings.PeakPlantsPerKm2;
			const double Danger = std::min(1.0, HuntersInCell(G.SpeciesIndex, Cell) / std::max(1.0, G.Count));
			return Food - 0.5 * Danger;
		}
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
		std::vector<AnimalGroup> Next;
		Next.reserve(AnimalGroups.size() + 16);
		for (size_t i = 0; i < AnimalGroups.size(); ++i)
		{
			const AnimalGroup& G = AnimalGroups[i];
			if (G.Count < 1.0)
			{
				continue;
			}
			const Species& S = SpeciesList[G.SpeciesIndex];
			// Groups still to copy, plus one extra if this one splits, must fit under the cap.
			const size_t Remaining = AnimalGroups.size() - i;
			// As the planet fills up, groups grow larger before splitting, so the cap is approached gently.
			const double Crowding = std::max(1.0, 2.0 * static_cast<double>(AnimalGroups.size()) / Settings.MaxGroups);
			if (G.Count > S.GroupSize * 2.0 * Crowding * Crowding && Next.size() + Remaining + 1 <= static_cast<size_t>(Settings.MaxGroups))
			{
				AnimalGroup A = G;
				AnimalGroup B = G;
				A.Count = G.Count * 0.5;
				B.Count = G.Count * 0.5;
				B.Lat = std::clamp(G.Lat + Random.Range(-0.3, 0.3), -89.0, 89.0);
				B.Lon = G.Lon + Random.Range(-0.3, 0.3);
				if (!Habitable(S, B.Lat, B.Lon))
				{
					B.Lat = G.Lat;
					B.Lon = G.Lon;
				}
				Next.push_back(A);
				Next.push_back(B);
			}
			else
			{
				Next.push_back(G);
			}
		}
		AnimalGroups.swap(Next);

		for (size_t s = 0; s < SpeciesList.size(); ++s)
		{
			if (SpeciesAlive[s] && CountOf(static_cast<int>(s)) <= 0.0)
			{
				SpeciesAlive[s] = false;
				EventLog.push_back({Years(), SpeciesList[s].Name + " died out"});
			}
		}
	}
};

} // namespace earth

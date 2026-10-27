// The large animals alive around 315,000 years ago, with rough ranges and life histories.
// Numbers are order-of-magnitude estimates from the fossil record and living relatives,
// meant to give believable behaviour, not exact palaeontology.
#pragma once

#include <string>
#include <vector>

namespace earth
{

enum class Diet
{
	Plants,
	Meat
};

// A latitude/longitude box where the species can live.
struct RangeBox
{
	double LatMin;
	double LatMax;
	double LonMin;
	double LonMax;

	bool Contains(double Lat, double Lon) const { return Lat >= LatMin && Lat <= LatMax && Lon >= LonMin && Lon <= LonMax; }
};

struct Species
{
	std::string Name;
	Diet Eats = Diet::Plants;
	std::vector<RangeBox> Range;
	double BodyMassKg = 100.0;
	double GroupSize = 50.0;      // Typical animals per simulated group at the start.
	int StartGroups = 20;
	double SpeedKmPerDay = 15.0;
	double BirthsPerYear = 0.2;   // Young per animal per year when well fed.
	double LifespanYears = 20.0;
	double MaxElevationM = 3000.0;
	double PreyMassMinKg = 0.0;   // Predators only.
	double PreyMassMaxKg = 0.0;
	bool ExtinctToday = false;
	float Color[3] = {0.5f, 0.5f, 0.5f};

	bool InRange(double Lat, double Lon) const
	{
		for (const RangeBox& B : Range)
		{
			if (B.Contains(Lat, Lon))
			{
				return true;
			}
		}
		return false;
	}

	// Plant food (kg) or meat (kg) one animal needs per day.
	double FoodPerDayKg() const { return BodyMassKg * (Eats == Diet::Plants ? 0.02 : 0.04); }
};

inline std::vector<Species> DefaultSpecies()
{
	const RangeBox Africa{-35, 15, -18, 51};
	const RangeBox EastSouthAfrica{-34, 10, 10, 42};
	const RangeBox NorthernEurasia{40, 72, -10, 180};
	const RangeBox Europe{35, 56, -10, 60};
	const RangeBox Eurasia{30, 72, -10, 180};
	const RangeBox NorthAmerica{15, 65, -165, -60};
	const RangeBox SouthAmerica{-42, 5, -80, -35};
	const RangeBox Americas{-42, 50, -125, -35};
	const RangeBox Australia{-39, -11, 113, 154};

	auto Make = [](const char* Name, Diet D, std::vector<RangeBox> R, double Mass, double Group, int Groups,
		double Speed, double Births, double Life, bool Extinct, float Cr, float Cg, float Cb)
	{
		Species S;
		S.Name = Name;
		S.Eats = D;
		S.Range = std::move(R);
		S.BodyMassKg = Mass;
		S.GroupSize = Group;
		S.StartGroups = Groups;
		S.SpeedKmPerDay = Speed;
		S.BirthsPerYear = Births;
		S.LifespanYears = Life;
		S.ExtinctToday = Extinct;
		S.Color[0] = Cr;
		S.Color[1] = Cg;
		S.Color[2] = Cb;
		return S;
	};
	auto Hunter = [](Species S, double PreyMin, double PreyMax)
	{
		S.PreyMassMinKg = PreyMin;
		S.PreyMassMaxKg = PreyMax;
		return S;
	};

	std::vector<Species> All;
	// Africa, where the NPCs will start.
	All.push_back(Make("African elephant", Diet::Plants, {Africa}, 4000, 60, 30, 15, 0.07, 60, false, 0.55f, 0.55f, 0.58f));
	All.push_back(Make("Plains zebra", Diet::Plants, {EastSouthAfrica}, 300, 400, 30, 25, 0.20, 20, false, 0.95f, 0.95f, 0.95f));
	All.push_back(Make("Wildebeest", Diet::Plants, {{-30, 5, 15, 40}}, 200, 800, 30, 30, 0.30, 18, false, 0.40f, 0.33f, 0.25f));
	All.push_back(Make("Hippopotamus", Diet::Plants, {{-30, 15, -17, 40}}, 1500, 40, 20, 8, 0.10, 40, false, 0.50f, 0.40f, 0.45f));
	All.push_back(Hunter(Make("Lion", Diet::Meat, {Africa, {15, 37, -10, 50}}, 190, 8, 12, 20, 0.30, 15, false, 0.85f, 0.65f, 0.25f), 100, 1500));
	All.push_back(Hunter(Make("Spotted hyena", Diet::Meat, {{-34, 20, -17, 50}}, 60, 15, 12, 25, 0.35, 15, false, 0.60f, 0.50f, 0.30f), 50, 400));
	// Eurasia.
	All.push_back(Make("Steppe mammoth", Diet::Plants, {NorthernEurasia}, 7000, 40, 30, 12, 0.06, 60, true, 0.45f, 0.30f, 0.20f));
	All.push_back(Make("Straight-tusked elephant", Diet::Plants, {Europe}, 9000, 30, 15, 12, 0.05, 60, true, 0.65f, 0.55f, 0.45f));
	All.push_back(Make("Woolly rhinoceros", Diet::Plants, {{40, 70, 20, 150}}, 2000, 20, 20, 10, 0.08, 40, true, 0.55f, 0.45f, 0.35f));
	All.push_back(Make("Steppe bison", Diet::Plants, {{35, 70, -10, 180}}, 700, 200, 30, 20, 0.20, 20, true, 0.35f, 0.25f, 0.15f));
	All.push_back(Make("Wild horse", Diet::Plants, {{30, 70, -10, 140}, NorthAmerica}, 400, 150, 35, 30, 0.20, 25, false, 0.70f, 0.55f, 0.40f));
	All.push_back(Make("Giant deer", Diet::Plants, {{40, 60, -10, 90}}, 600, 60, 15, 20, 0.20, 20, true, 0.75f, 0.50f, 0.25f));
	All.push_back(Make("Gigantopithecus", Diet::Plants, {{18, 28, 100, 112}}, 250, 20, 6, 5, 0.10, 40, true, 0.30f, 0.20f, 0.10f));
	All.push_back(Hunter(Make("Cave lion", Diet::Meat, {NorthernEurasia}, 250, 6, 12, 20, 0.30, 15, true, 0.90f, 0.75f, 0.45f), 150, 1200));
	All.push_back(Hunter(Make("Gray wolf", Diet::Meat, {Eurasia}, 40, 12, 15, 35, 0.50, 10, false, 0.55f, 0.55f, 0.60f), 30, 700));
	All.push_back(Hunter(Make("Homotherium", Diet::Meat, {Eurasia, NorthAmerica}, 200, 6, 10, 20, 0.30, 15, true, 0.80f, 0.40f, 0.20f), 300, 3000));
	// The Americas.
	All.push_back(Make("Columbian mammoth", Diet::Plants, {{15, 50, -125, -75}}, 9000, 30, 25, 12, 0.05, 60, true, 0.50f, 0.35f, 0.25f));
	All.push_back(Make("Giant ground sloth", Diet::Plants, {SouthAmerica}, 4000, 15, 20, 5, 0.10, 40, true, 0.45f, 0.40f, 0.20f));
	All.push_back(Make("Glyptodon", Diet::Plants, {{-42, -5, -75, -38}}, 2000, 20, 20, 5, 0.10, 40, true, 0.35f, 0.40f, 0.25f));
	All.push_back(Hunter(Make("Smilodon", Diet::Meat, {Americas}, 280, 6, 12, 20, 0.30, 15, true, 0.95f, 0.55f, 0.15f), 200, 3000));
	// Australia and islands.
	All.push_back(Make("Diprotodon", Diet::Plants, {Australia}, 2700, 20, 20, 8, 0.10, 40, true, 0.40f, 0.35f, 0.30f));
	All.push_back(Make("Giant kangaroo", Diet::Plants, {Australia}, 230, 80, 25, 30, 0.30, 15, true, 0.80f, 0.45f, 0.30f));
	All.push_back(Hunter(Make("Marsupial lion", Diet::Meat, {Australia}, 110, 6, 10, 20, 0.30, 12, true, 0.65f, 0.35f, 0.15f), 50, 500));
	All.push_back(Make("Moa", Diet::Plants, {{-47.5, -34, 166, 179}}, 200, 40, 10, 10, 0.15, 30, true, 0.40f, 0.50f, 0.30f));
	All.push_back(Make("Elephant bird", Diet::Plants, {{-26, -12, 43, 51}}, 700, 20, 8, 8, 0.10, 40, true, 0.85f, 0.80f, 0.65f));
	return All;
}

} // namespace earth

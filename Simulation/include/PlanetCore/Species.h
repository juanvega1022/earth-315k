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
	// Most kinds are held back only by how much they find to eat. Small ones that live off a
	// sliver of the plants (birds on seeds and insects) also stop breeding at this many per km^2.
	double MaxPerKm2 = 0.0;
	// Kept animals: none at the start; a band that knows TameSkill takes them in from a herd of
	// TamedFrom near its camp, and they walk with the band from then on.
	std::string TamedFrom;
	int TameSkill = -1;
	bool Companion = false; // Kept for hunting and guarding, not eaten (dogs).
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
	// Horses also lived across both Americas (Equus, Hippidion) until the end of the Ice Age.
	All.push_back(Make("Wild horse", Diet::Plants, {{30, 70, -10, 140}, NorthAmerica, SouthAmerica}, 400, 150, 35, 30, 0.20, 25, false, 0.70f, 0.55f, 0.40f));
	All.push_back(Make("Giant deer", Diet::Plants, {{40, 60, -10, 90}}, 600, 60, 15, 20, 0.20, 20, true, 0.75f, 0.50f, 0.25f));
	All.push_back(Make("Gigantopithecus", Diet::Plants, {{18, 28, 100, 112}}, 250, 20, 6, 5, 0.10, 40, true, 0.30f, 0.20f, 0.10f));
	All.push_back(Hunter(Make("Cave lion", Diet::Meat, {NorthernEurasia}, 250, 6, 12, 20, 0.30, 15, true, 0.90f, 0.75f, 0.45f), 150, 1200));
	All.push_back(Hunter(Make("Gray wolf", Diet::Meat, {Eurasia}, 40, 12, 15, 35, 0.50, 10, false, 0.55f, 0.55f, 0.60f), 30, 700));
	All.push_back(Hunter(Make("Homotherium", Diet::Meat, {Eurasia, NorthAmerica}, 200, 6, 10, 20, 0.30, 15, true, 0.80f, 0.40f, 0.20f), 300, 3000));
	// The Americas.
	All.push_back(Make("Columbian mammoth", Diet::Plants, {{15, 50, -125, -75}}, 9000, 30, 25, 12, 0.05, 60, true, 0.50f, 0.35f, 0.25f));
	All.push_back(Make("Giant ground sloth", Diet::Plants, {SouthAmerica}, 4000, 15, 20, 5, 0.10, 40, true, 0.45f, 0.40f, 0.20f));
	All.push_back(Make("Glyptodon", Diet::Plants, {{-42, -5, -75, -38}}, 2000, 20, 20, 5, 0.10, 40, true, 0.35f, 0.40f, 0.25f));
	All.push_back(Hunter(Make("Smilodon", Diet::Meat, {Americas}, 280, 6, 12, 20, 0.30, 15, true, 0.95f, 0.55f, 0.15f), 200, 4000)); // Up to young ground sloths.
	// Australia and islands.
	All.push_back(Make("Diprotodon", Diet::Plants, {Australia}, 2700, 20, 20, 8, 0.10, 40, true, 0.40f, 0.35f, 0.30f));
	All.push_back(Make("Giant kangaroo", Diet::Plants, {Australia}, 230, 80, 25, 30, 0.30, 15, true, 0.80f, 0.45f, 0.30f));
	All.push_back(Hunter(Make("Marsupial lion", Diet::Meat, {Australia}, 110, 6, 10, 20, 0.30, 12, true, 0.65f, 0.35f, 0.15f), 50, 500));
	All.push_back(Make("Moa", Diet::Plants, {{-47.5, -34, 166, 179}}, 200, 40, 10, 10, 0.15, 30, true, 0.40f, 0.50f, 0.30f));
	All.push_back(Make("Elephant bird", Diet::Plants, {{-26, -12, 43, 51}}, 700, 20, 8, 8, 0.10, 40, true, 0.85f, 0.80f, 0.65f));

	// Added in the living-world update. Saves from before it get these when they are loaded, so
	// new kinds always go at the end: a saved herd's species is its place in this list.
	const RangeBox NorthAfrica{20, 37, -10, 35};
	All.push_back(Make("Red deer", Diet::Plants, {{30, 62, -10, 135}, {30, 37, -9, 11}}, 200, 30, 25, 20, 0.35, 18, false, 0.70f, 0.40f, 0.25f));
	All.push_back(Make("Reindeer", Diet::Plants, {{50, 75, -10, 180}, {45, 72, -170, -55}}, 110, 200, 25, 35, 0.40, 15, false, 0.75f, 0.70f, 0.60f));
	All.push_back(Make("Aurochs", Diet::Plants, {{20, 60, -10, 120}, NorthAfrica}, 800, 30, 25, 15, 0.20, 20, true, 0.25f, 0.18f, 0.12f));
	All.push_back(Make("Wild boar", Diet::Plants, {{10, 58, -10, 140}}, 90, 15, 25, 10, 1.00, 10, false, 0.35f, 0.30f, 0.28f));
	All.push_back(Make("Wild goat", Diet::Plants, {{25, 43, 22, 75}}, 50, 20, 15, 10, 0.60, 12, false, 0.65f, 0.55f, 0.40f));
	All.push_back(Make("Mouflon", Diet::Plants, {{28, 42, 25, 75}}, 45, 25, 15, 15, 0.60, 12, false, 0.60f, 0.40f, 0.30f));
	All.push_back(Make("Saiga", Diet::Plants, {{40, 55, 25, 110}}, 35, 300, 15, 40, 1.00, 8, false, 0.85f, 0.75f, 0.55f));
	All.push_back(Make("Musk ox", Diet::Plants, {{55, 75, -10, 180}, {55, 80, -170, -60}}, 300, 20, 15, 8, 0.30, 20, false, 0.30f, 0.22f, 0.18f));
	All.push_back(Make("African buffalo", Diet::Plants, {{-34, 15, -17, 45}}, 600, 300, 25, 15, 0.30, 20, false, 0.20f, 0.20f, 0.22f));
	All.push_back(Make("Eland", Diet::Plants, {EastSouthAfrica}, 500, 25, 15, 20, 0.30, 20, false, 0.80f, 0.65f, 0.45f));
	All.push_back(Make("Gazelle", Diet::Plants, {{-10, 40, -17, 75}}, 25, 50, 25, 30, 0.80, 12, false, 0.90f, 0.70f, 0.45f));
	All.push_back(Make("Warthog", Diet::Plants, {{-34, 15, -17, 45}}, 75, 6, 20, 10, 1.20, 15, false, 0.50f, 0.40f, 0.35f));
	All.push_back(Make("African wild ass", Diet::Plants, {{10, 25, 30, 48}}, 250, 20, 10, 25, 0.30, 25, false, 0.70f, 0.68f, 0.65f));
	All.push_back(Make("Wild water buffalo", Diet::Plants, {{5, 30, 70, 110}}, 900, 30, 15, 10, 0.25, 25, false, 0.30f, 0.30f, 0.35f));
	Species Junglefowl = Make("Red junglefowl", Diet::Plants, {{8, 28, 75, 120}}, 1, 30, 20, 3, 4.00, 5, false, 0.85f, 0.25f, 0.15f);
	Junglefowl.MaxPerKm2 = 10.0;
	All.push_back(Junglefowl);
	Species Turkey = Make("Wild turkey", Diet::Plants, {{15, 45, -110, -70}}, 8, 30, 20, 8, 3.00, 5, false, 0.45f, 0.35f, 0.30f);
	Turkey.MaxPerKm2 = 2.0;
	All.push_back(Turkey);
	All.push_back(Make("Guanaco", Diet::Plants, {{-55, -5, -80, -60}}, 120, 40, 20, 25, 0.40, 20, false, 0.80f, 0.55f, 0.35f));
	All.push_back(Make("Red kangaroo", Diet::Plants, {Australia}, 60, 20, 25, 30, 0.80, 15, false, 0.75f, 0.35f, 0.25f));
	All.push_back(Make("American mastodon", Diet::Plants, {NorthAmerica}, 7000, 15, 15, 10, 0.06, 60, true, 0.40f, 0.25f, 0.15f));
	All.push_back(Make("Ancient bison", Diet::Plants, {{25, 65, -165, -60}}, 700, 200, 20, 20, 0.20, 20, true, 0.38f, 0.28f, 0.18f));
	All.push_back(Make("Cave bear", Diet::Plants, {{38, 58, -10, 60}}, 400, 2, 15, 8, 0.25, 25, true, 0.40f, 0.28f, 0.20f));
	All.push_back(Hunter(Make("Leopard", Diet::Meat, {Africa, {5, 45, 30, 140}}, 60, 2, 12, 20, 0.50, 12, false, 0.90f, 0.80f, 0.40f), 10, 150));
	All.push_back(Hunter(Make("Dire wolf", Diet::Meat, {{-20, 55, -125, -60}}, 65, 10, 12, 30, 0.50, 10, true, 0.45f, 0.42f, 0.40f), 50, 900));
	All.push_back(Hunter(Make("Cave hyena", Diet::Meat, {{35, 60, -10, 140}}, 80, 12, 10, 25, 0.35, 15, true, 0.55f, 0.45f, 0.30f), 50, 600));

	// Kept animals, each tamed from a wild kind above where that kind lives.
	auto Kept = [&](const char* Name, const char* From, int Skill, double Mass, double Group, double Births, double Life, float Cr, float Cg, float Cb)
	{
		Species S = Make(Name, Diet::Plants, {{-56, 75, -180, 180}}, Mass, Group, 0, 15, Births, Life, false, Cr, Cg, Cb);
		S.TamedFrom = From;
		S.TameSkill = Skill;
		S.MaxElevationM = 4500;
		return S;
	};
	constexpr int Dogs = 24;
	constexpr int Herding = 27;
	Species Dog = Kept("Dog", "Gray wolf", Dogs, 25, 6, 1.0, 12, 0.80f, 0.65f, 0.45f);
	Dog.Companion = true;
	All.push_back(Dog);
	All.push_back(Kept("Goat", "Wild goat", Herding, 40, 30, 0.9, 12, 0.90f, 0.85f, 0.75f));
	All.push_back(Kept("Sheep", "Mouflon", Herding, 40, 30, 0.9, 12, 0.95f, 0.95f, 0.90f));
	All.push_back(Kept("Cattle", "Aurochs", Herding, 500, 25, 0.35, 20, 0.55f, 0.35f, 0.25f));
	All.push_back(Kept("Pig", "Wild boar", Herding, 80, 15, 1.5, 10, 0.95f, 0.70f, 0.70f));
	All.push_back(Kept("Domestic horse", "Wild horse", Herding, 400, 20, 0.3, 25, 0.60f, 0.40f, 0.25f));
	All.push_back(Kept("Donkey", "African wild ass", Herding, 200, 10, 0.3, 30, 0.55f, 0.52f, 0.50f));
	All.push_back(Kept("Water buffalo", "Wild water buffalo", Herding, 600, 15, 0.3, 25, 0.25f, 0.25f, 0.28f));
	All.push_back(Kept("Chicken", "Red junglefowl", Herding, 1.5, 30, 5.0, 6, 0.95f, 0.85f, 0.60f));
	All.push_back(Kept("Turkey", "Wild turkey", Herding, 8, 20, 3.0, 6, 0.30f, 0.25f, 0.25f));
	All.push_back(Kept("Llama", "Guanaco", Herding, 130, 20, 0.4, 20, 0.95f, 0.90f, 0.85f));
	All.push_back(Kept("Domestic reindeer", "Reindeer", Herding, 100, 40, 0.4, 15, 0.80f, 0.78f, 0.70f));
	return All;
}

} // namespace earth

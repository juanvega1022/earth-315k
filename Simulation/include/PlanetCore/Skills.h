// Skills people can discover, teach each other and lose: what each needs first, how hard it is
// to hit on, and what it changes in the world.
#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace earth
{

// What knowing a skill changes for a camp. Multipliers combine by multiplying, warmth by adding.
struct SkillEffects
{
	float Forage = 1.0f;         // Plant food gathered.
	float Hunt = 1.0f;           // Hunting success.
	float PredatorDeaths = 1.0f; // People killed by predators.
	float ChildDeaths = 1.0f;    // Children dying of illness.
	float Warmth = 0.0f;         // 0..1, how much of the cold's extra food cost is avoided.
	float Travel = 1.0f;         // How far scouts and departing bands can go, over water too.
	float Land = 1.0f;           // How many people the land around a camp can feed.
	float Carry = 1.0f;          // Food that can be carried and kept.
	float Teaching = 1.0f;       // How fast skills pass from person to person.

	void Add(const SkillEffects& O)
	{
		Forage *= O.Forage;
		Hunt *= O.Hunt;
		PredatorDeaths *= O.PredatorDeaths;
		ChildDeaths *= O.ChildDeaths;
		Warmth = Warmth + O.Warmth > 0.9f ? 0.9f : Warmth + O.Warmth;
		Travel *= O.Travel;
		Land *= O.Land;
		Carry *= O.Carry;
		Teaching *= O.Teaching;
	}
};

struct Skill
{
	const char* Name;
	std::vector<int> Needs;
	SkillEffects Effects;
	int Tier = 1; // 1 + the highest tier among what it needs.
	// When people really first had it (years ago, from the earliest evidence; 315,000 or more for
	// what Homo sapiens' ancestors already knew), and how long, on average, people who knew what
	// it builds on took to hit on it. Skills come at about that pace to a world of about 3,000
	// people (the run's usual number), faster to more people and slower to fewer.
	double YearsAgo = 315000.0;
	double YearsToFind = 300.0;
};

using SkillSet = uint64_t; // One bit per skill.

inline const std::vector<Skill>& AllSkills()
{
	static const std::vector<Skill> List = []
	{
		auto E = [](std::initializer_list<std::pair<int, float>> Pairs)
		{
			SkillEffects S;
			float* Fields[] = {&S.Forage, &S.Hunt, &S.PredatorDeaths, &S.ChildDeaths, &S.Warmth, &S.Travel, &S.Land, &S.Carry, &S.Teaching};
			for (const auto& P : Pairs) *Fields[P.first] = P.second;
			return S;
		};
		enum { Forage, Hunt, Predators, Children, Warmth, Travel, Land, Carry, Teaching };
		std::vector<Skill> L = {
			{"sharp stone flakes", {}, E({{Forage, 1.1f}, {Hunt, 1.1f}}), 1, 315000},                 // 0
			{"digging sticks", {}, E({{Forage, 1.15f}}), 1, 315000},                                   // 1
			{"keeping fire", {}, E({{Warmth, 0.3f}, {Predators, 0.7f}}), 1, 315000},                   // 2
			{"warning calls", {}, E({{Predators, 0.7f}}), 1, 315000},                                  // 3
			{"healing plants", {}, E({{Children, 0.85f}}), 1, 315000},                                 // 4
			{"carrying bags", {}, E({{Carry, 1.5f}}), 1, 315000},                                      // 5
			{"hand axes", {0}, E({{Hunt, 1.1f}, {Forage, 1.05f}}), 1, 315000},                         // 6
			{"making fire", {2}, E({{Warmth, 0.1f}, {Children, 0.95f}}), 1, 315000},                   // 7
			{"cooking", {2}, E({{Forage, 1.25f}, {Children, 0.9f}}), 1, 315000},                       // 8
			{"hide clothing", {0}, E({{Warmth, 0.3f}}), 1, 170000},                                    // 9
			{"shelters", {1}, E({{Warmth, 0.2f}, {Predators, 0.9f}}), 1, 315000},                      // 10
			{"grinding seeds", {0}, E({{Forage, 1.1f}}), 1, 105000},                                   // 11
			{"wooden spears", {6}, E({{Hunt, 1.3f}}), 1, 315000},                                      // 12
			{"spoken language", {3}, E({{Teaching, 2.0f}}), 1, 300000},                                // 13
			{"snares and traps", {5}, E({{Hunt, 1.2f}}), 1, 70000},                                   // 14
			{"drying and smoking food", {2, 5}, E({{Carry, 1.5f}}), 1, 40000},                        // 15
			{"rafts", {6}, E({{Travel, 2.0f}}), 1, 70000},                                            // 16
			{"fishing", {12}, E({{Forage, 1.15f}}), 1, 100000},                                        // 17
			{"sewn clothing", {9, 0}, E({{Warmth, 0.25f}}), 1, 45000},                                // 18
			{"spear throwers", {12}, E({{Hunt, 1.2f}}), 1, 70000},                                    // 19
			{"art and ornaments", {13}, E({{Teaching, 1.2f}}), 1, 100000},                             // 20
			{"counting", {13}, E({{Teaching, 1.1f}, {Carry, 1.1f}}), 1, 44000},                       // 21
			{"boats", {16}, E({{Travel, 2.0f}}), 1, 50000},                                           // 22
			{"bows and arrows", {19}, E({{Hunt, 1.3f}}), 1, 64000},                                   // 23
			{"dogs", {14, 13}, E({{Hunt, 1.15f}, {Predators, 0.8f}}), 1, 25000},                      // 24
			{"planting", {11, 1}, E({{Land, 2.0f}, {Forage, 1.2f}}), 1, 11500},                       // 25
			{"farming", {25}, E({{Land, 3.0f}, {Forage, 1.3f}}), 1, 10500},                           // 26
			{"herding", {24, 25}, E({{Land, 1.5f}, {Hunt, 1.2f}}), 1, 10000},                         // 27
			{"pottery", {8, 15}, E({{Carry, 1.5f}, {Children, 0.95f}}), 1, 20000},                    // 28
			{"villages", {26}, E({{Warmth, 0.1f}, {Land, 1.3f}}), 1, 9500},                          // 29
			{"weaving", {18, 26}, E({{Warmth, 0.1f}, {Carry, 1.2f}}), 1, 9000},                      // 30
			{"copper working", {28, 7}, E({{Forage, 1.05f}, {Hunt, 1.05f}}), 1, 7500},               // 31
			{"the wheel", {29}, E({{Carry, 2.0f}, {Travel, 1.3f}}), 1, 5500},                        // 32
			{"writing", {29, 21}, E({{Teaching, 2.0f}}), 1, 5200},                                   // 33
			{"bronze", {31}, E({{Forage, 1.1f}, {Hunt, 1.1f}}), 1, 5300},                            // 34
			{"sailing ships", {22, 30}, E({{Travel, 2.0f}}), 1, 5000},                               // 35
			{"irrigation", {26, 21}, E({{Land, 1.5f}}), 1, 7500},                                    // 36
			{"medicine", {33, 4}, E({{Children, 0.7f}}), 1, 4000},                                   // 37
			{"iron", {34}, E({{Forage, 1.1f}, {Hunt, 1.1f}, {Land, 1.2f}}), 1, 3200},                // 38
			{"mathematics", {33}, E({{Teaching, 1.2f}, {Land, 1.1f}}), 1, 4000},                     // 39
			// After the Iron Age: cities, then the medieval, industrial and modern eras.
			{"cities", {29, 36}, E({{Land, 1.2f}, {Teaching, 1.1f}}), 1, 3500},                    // 40
			{"laws", {33, 40}, E({{Predators, 0.9f}, {Children, 0.95f}}), 1, 3400},                // 41
			{"money", {40, 21}, E({{Carry, 1.2f}}), 1, 2600},                                      // 42
			{"roads", {40, 32}, E({{Travel, 1.3f}}), 1, 2300},                                     // 43
			{"astronomy", {39, 33}, E({{Travel, 1.1f}, {Teaching, 1.1f}}), 1, 2500},               // 44
			{"paper", {33, 30}, E({{Teaching, 1.2f}}), 1, 2100},                                   // 45
			{"water mills", {32, 26}, E({{Forage, 1.2f}}), 1, 2000},                               // 46
			{"compass", {38, 35, 44}, E({{Travel, 1.3f}}), 1, 1000},                               // 47
			{"gunpowder", {44, 38}, E({{Hunt, 1.2f}, {Predators, 0.8f}}), 1, 1100},                // 48
			{"universities", {45, 44}, E({{Teaching, 1.3f}}), 1, 930},                             // 49
			{"printing", {45, 38}, E({{Teaching, 1.5f}}), 1, 575},                                 // 50
			{"steam engines", {38, 39, 50}, E({{Carry, 1.3f}, {Forage, 1.1f}}), 1, 290},           // 51
			{"factories", {51, 30}, E({{Forage, 1.1f}, {Carry, 1.2f}}), 1, 250},                   // 52
			{"railways", {51, 43}, E({{Travel, 1.5f}, {Carry, 1.3f}}), 1, 200},                    // 53
			{"vaccines", {37, 49}, E({{Children, 0.6f}}), 1, 225},                                 // 54
			{"electricity", {39, 49, 38}, E({{Teaching, 1.1f}}), 1, 180},                          // 55
			{"telegraph", {55}, E({{Teaching, 1.2f}}), 1, 175},                                    // 56
			{"engines", {51, 52}, E({{Travel, 1.3f}, {Forage, 1.1f}}), 1, 140},                    // 57
			{"fertilizers", {52, 26}, E({{Land, 1.4f}}), 1, 110},                                  // 58
			{"flight", {57}, E({{Travel, 1.5f}}), 1, 120},                                         // 59
			{"antibiotics", {54}, E({{Children, 0.6f}}), 1, 95},                                   // 60
			{"radio", {56}, E({{Teaching, 1.2f}}), 1, 120},                                        // 61
			{"computers", {55, 39, 61}, E({{Teaching, 1.5f}}), 1, 80},                             // 62
			{"the internet", {62}, E({{Teaching, 1.5f}}), 1, 50},                                  // 63
		};
		for (Skill& S : L)
		{
			double Since = 315000.0; // When what it builds on was first known.
			for (int N : S.Needs)
			{
				S.Tier = S.Tier > L[N].Tier + 1 ? S.Tier : L[N].Tier + 1;
				Since = Since < L[N].YearsAgo ? Since : L[N].YearsAgo;
			}
			// Planting waits for a warm, steady climate (see People::LearnSkills), so it is timed
			// from the end of the last ice age, not from when seeds were first ground.
			if (S.Name[0] == 'p' && S.Name[1] == 'l') Since = 13000.0;
			// Recent steps came within decades of each other, so the shortest wait is shorter for them.
			const double Shortest = S.YearsAgo < S.YearsToFind ? S.YearsAgo : S.YearsToFind;
			S.YearsToFind = Since - S.YearsAgo > Shortest ? Since - S.YearsAgo : Shortest;
		}
		return L;
	}();
	return List;
}

inline bool Knows(SkillSet Set, int Skill) { return (Set >> Skill) & 1u; }

inline bool CanDiscover(SkillSet Set, int Index)
{
	if (Knows(Set, Index)) return false;
	for (int N : AllSkills()[Index].Needs)
	{
		if (!Knows(Set, N)) return false;
	}
	return true;
}

inline SkillEffects EffectsOf(SkillSet Set)
{
	SkillEffects E;
	const std::vector<Skill>& L = AllSkills();
	for (size_t i = 0; i < L.size(); ++i)
	{
		if (Knows(Set, static_cast<int>(i))) E.Add(L[i].Effects);
	}
	return E;
}

} // namespace earth

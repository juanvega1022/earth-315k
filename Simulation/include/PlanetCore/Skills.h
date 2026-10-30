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
	int Tier = 1; // 1 + the highest tier among what it needs; deeper skills are rarer finds.
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
			{"sharp stone flakes", {}, E({{Forage, 1.1f}, {Hunt, 1.1f}})},                 // 0
			{"digging sticks", {}, E({{Forage, 1.15f}})},                                   // 1
			{"keeping fire", {}, E({{Warmth, 0.3f}, {Predators, 0.7f}})},                   // 2
			{"warning calls", {}, E({{Predators, 0.7f}})},                                  // 3
			{"healing plants", {}, E({{Children, 0.85f}})},                                 // 4
			{"carrying bags", {}, E({{Carry, 1.5f}})},                                      // 5
			{"hand axes", {0}, E({{Hunt, 1.1f}, {Forage, 1.05f}})},                         // 6
			{"making fire", {2}, E({{Warmth, 0.1f}, {Children, 0.95f}})},                   // 7
			{"cooking", {2}, E({{Forage, 1.25f}, {Children, 0.9f}})},                       // 8
			{"hide clothing", {0}, E({{Warmth, 0.3f}})},                                    // 9
			{"shelters", {1}, E({{Warmth, 0.2f}, {Predators, 0.9f}})},                      // 10
			{"grinding seeds", {0}, E({{Forage, 1.1f}})},                                   // 11
			{"wooden spears", {6}, E({{Hunt, 1.3f}})},                                      // 12
			{"spoken language", {3}, E({{Teaching, 2.0f}})},                                // 13
			{"snares and traps", {5}, E({{Hunt, 1.2f}})},                                   // 14
			{"drying and smoking food", {2, 5}, E({{Carry, 1.5f}})},                        // 15
			{"rafts", {6}, E({{Travel, 2.0f}})},                                            // 16
			{"fishing", {12}, E({{Forage, 1.15f}})},                                        // 17
			{"sewn clothing", {9, 0}, E({{Warmth, 0.25f}})},                                // 18
			{"spear throwers", {12}, E({{Hunt, 1.2f}})},                                    // 19
			{"art and ornaments", {13}, E({{Teaching, 1.2f}})},                             // 20
			{"counting", {13}, E({{Teaching, 1.1f}, {Carry, 1.1f}})},                       // 21
			{"boats", {16}, E({{Travel, 2.0f}})},                                           // 22
			{"bows and arrows", {19}, E({{Hunt, 1.3f}})},                                   // 23
			{"dogs", {14, 13}, E({{Hunt, 1.15f}, {Predators, 0.8f}})},                      // 24
			{"planting", {11, 1}, E({{Land, 2.0f}, {Forage, 1.2f}})},                       // 25
			{"farming", {25}, E({{Land, 3.0f}, {Forage, 1.3f}})},                           // 26
			{"herding", {24, 25}, E({{Land, 1.5f}, {Hunt, 1.2f}})},                         // 27
			{"pottery", {7, 25}, E({{Carry, 1.5f}, {Children, 0.95f}})},                    // 28
			{"villages", {26}, E({{Warmth, 0.1f}, {Land, 1.3f}})},                          // 29
			{"weaving", {18, 26}, E({{Warmth, 0.1f}, {Carry, 1.2f}})},                      // 30
			{"copper working", {28, 7}, E({{Forage, 1.05f}, {Hunt, 1.05f}})},               // 31
			{"the wheel", {29}, E({{Carry, 2.0f}, {Travel, 1.3f}})},                        // 32
			{"writing", {29, 21}, E({{Teaching, 2.0f}})},                                   // 33
			{"bronze", {31}, E({{Forage, 1.1f}, {Hunt, 1.1f}})},                            // 34
			{"sailing ships", {22, 30}, E({{Travel, 2.0f}})},                               // 35
			{"irrigation", {26, 21}, E({{Land, 1.5f}})},                                    // 36
			{"medicine", {33, 4}, E({{Children, 0.7f}})},                                   // 37
			{"iron", {34}, E({{Forage, 1.1f}, {Hunt, 1.1f}, {Land, 1.2f}})},                // 38
			{"mathematics", {33}, E({{Teaching, 1.2f}, {Land, 1.1f}})},                     // 39
		};
		for (Skill& S : L)
		{
			for (int N : S.Needs) S.Tier = S.Tier > L[N].Tier + 1 ? S.Tier : L[N].Tier + 1;
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

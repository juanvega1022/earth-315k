#pragma once

// States above the band: chiefdoms, kingdoms, empires and nations. Bands that farm or live in
// villages join one another, pulled by shared speech, gods and trade and pushed by strong
// neighbours; their rulers take tribute, make laws, go to war and die, and the states grow, split
// and fall. People::Govern runs them once a year.

#include "Skills.h"

#include <cstdint>
#include <string>
#include <vector>

namespace earth
{
enum PolityTier : uint8_t
{
	Chiefdom,
	Kingdom,
	Empire,
	Nation
};

struct Polity
{
	uint32_t Id = 0;
	uint16_t Word = 0; // Its name, in its founders' speech.
	uint8_t Tier = Chiefdom;
	uint8_t Rule = 0;   // Who rules next: 0 the ruler's eldest child, 1 whoever the capital trusts most.
	int Capital = 0;    // Camp id.
	uint16_t City = 0;  // Name of its capital city, once it has one; 0 none.
	uint32_t Ruler = 0;
	uint16_t RulerName = 0;
	int Rulers = 1;     // How many have ruled it.
	uint32_t Founder = 0;
	uint32_t Faith = 0; // Its state religion (a belief id), 0 none.
	double Founded = 0.0;
	float Treasury = 0.0f; // Kg of food paid in as tribute over the years.
	uint32_t Enemy = 0;    // The state it is at war with, 0 at peace.
	int WarYears = 0;
	int Conquests = 0;
	std::vector<uint32_t> Allies;
	std::vector<std::string> Laws; // Given by its thinking rulers, newest last.
	std::vector<uint32_t> Partners; // States it trades with.
	double Wealth = 0.0;            // What its trade has earned, in kg of food's worth.

	// Worked out each year from its bands, not saved.
	int Bands = 0;
	int Members = 0;
	SkillSet Known = 0;
	SkillSet Heard = 0;      // What it and its trading partners know: ideas travel with goods.
	float Inequality = 1.0f; // Food put by per head in its richest band over its poorest.
	int CityPeople = 0;      // How many of its people live in its cities.
	int CapitalPeople = 0;   // How many live in its capital city.
	int Towns = 0;           // How many cities it has.
	double Lat = 0.0, Lon = 0.0; // Its capital.

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Word), Ar.Io(Tier), Ar.Io(Rule), Ar.Io(Capital), Ar.Io(City), Ar.Io(Ruler), Ar.Io(RulerName), Ar.Io(Rulers);
		Ar.Io(Founder), Ar.Io(Faith), Ar.Io(Founded), Ar.Io(Treasury), Ar.Io(Enemy), Ar.Io(WarYears), Ar.Io(Conquests), Ar.Io(Allies), Ar.Io(Laws);
		if (Ar.Version >= 16) Ar.Io(Partners), Ar.Io(Wealth);
	}
};

inline const char* TierName(int Tier)
{
	static const char* Names[] = {"chiefdom", "kingdom", "empire", "nation"};
	return Names[Tier & 3];
}

// How far from its capital a state can hold bands, in km.
inline double ReachKm(int Tier, bool Roads)
{
	static const double Km[] = {120.0, 500.0, 2500.0, 1500.0};
	return Km[Tier & 3] * (Roads ? 1.5 : 1.0);
}
} // namespace earth

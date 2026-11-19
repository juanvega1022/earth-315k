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
	uint16_t Schools = 0, Libraries = 0, Universities = 0;
	SkillSet Written = 0;           // What its libraries hold: kept even if everyone who knew it dies.
	bool Calendar = false;          // Keeps a written calendar of the year.
	uint16_t Festival = 0;          // Name of its great yearly festival, 0 none.
	uint16_t Monuments = 0;         // Temples, tombs and monuments raised from its treasury.
	uint8_t Gov = 0;                // How it is ruled: see GovName.
	int Coups = 0, CivilWars = 0;
	int Elected = 0;                // Year of its last election, for republics and democracies.
	uint8_t Ideology = 0;           // The political idea most of its people hold: see IdeologyName.
	int Revolutions = 0;
	double Coins = 0.0;        // Money in circulation, in kg of food it was first worth.
	double Debt = 0.0;         // What it owes its lenders, in kg of food.
	float LastPrice = 1.0f;    // Last year's price of food.
	bool Slavery = false, Abolished = false;
	uint16_t Discoveries = 0;  // First-ever finds made by its people.
	uint16_t Script = 0;       // The name of the script it writes in, 0 none.
	float Secular = 0.0f;      // How far its people have drifted from faith, 0 .. 1.
	uint16_t Style = 0;        // Its art's style (a word), 0 none yet.
	uint8_t Art = 0;           // What the style is mostly: see ArtName.
	float Fame = 0.0f;         // How widely its art is admired, from the states that took it up.
	bool Treaty = false;       // Signed the climate treaty: burns less fuel.
	uint16_t Parks = 0;        // Wild land it protects from hunters and fields.
	uint8_t Marriage = 0;      // Its custom: see MarriageName.
	uint8_t Rights = 0;        // How far women's rights have come: see RightsName.
	uint8_t Sport = 0;         // Its favourite sport: see SportName.
	uint16_t Medals = 0;       // Wins at the world games.

	// Worked out each year from its bands, not saved.
	int Bands = 0;
	int Members = 0;
	SkillSet Known = 0;
	SkillSet Heard = 0;      // What it and its trading partners know: ideas travel with goods.
	float Inequality = 1.0f; // Food put by per head in its richest band over its poorest.
	int CityPeople = 0;      // How many of its people live in its cities.
	int CapitalPeople = 0;   // How many live in its capital city.
	int Towns = 0;           // How many cities it has.
	float Literacy = 0.0f;   // Share of its people who can read.
	double RealPeople = 0.0;     // How many people it stands for on the real Earth's scale.
	float LifeExpectancy = 0.0f; // Mean age at death last year.
	float Welfare = 0.0f;        // How well its people live, 0 .. 1: long lives, enough food, reading, freedom, peace and fairness.
	float Price = 1.0f;      // What food costs against plenty: above 1 it is scarce, or the money is worth less.
	float Fertility = 1.0f;  // How many children its people have, against the old way: fewer once most read and live in towns.
	float CrimeRate = 0.0f;  // Crimes a year per 1,000 of its people.
	uint8_t Justice = 0;     // Bits: 1 courts, 2 police, 4 prisons.
	double Lat = 0.0, Lon = 0.0; // Its capital.

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Word), Ar.Io(Tier), Ar.Io(Rule), Ar.Io(Capital), Ar.Io(City), Ar.Io(Ruler), Ar.Io(RulerName), Ar.Io(Rulers);
		Ar.Io(Founder), Ar.Io(Faith), Ar.Io(Founded), Ar.Io(Treasury), Ar.Io(Enemy), Ar.Io(WarYears), Ar.Io(Conquests), Ar.Io(Allies), Ar.Io(Laws);
		if (Ar.Version >= 16) Ar.Io(Partners), Ar.Io(Wealth);
		if (Ar.Version >= 21) Ar.Io(Schools), Ar.Io(Libraries), Ar.Io(Universities), Ar.Io(Written);
		if (Ar.Version >= 24) Ar.Io(Calendar), Ar.Io(Festival), Ar.Io(Monuments);
		if (Ar.Version >= 25) Ar.Io(Gov), Ar.Io(Coups), Ar.Io(CivilWars), Ar.Io(Elected);
		else if (Ar.IsReading()) Gov = Tier >= Kingdom ? 1 : 0;
		if (Ar.Version >= 26) Ar.Io(Ideology), Ar.Io(Revolutions);
		if (Ar.Version >= 27) Ar.Io(Coins), Ar.Io(Debt), Ar.Io(LastPrice), Ar.Io(Slavery), Ar.Io(Abolished);
		if (Ar.Version >= 29) Ar.Io(Discoveries);
		if (Ar.Version >= 31) Ar.Io(Script);
		if (Ar.Version >= 33) Ar.Io(Secular);
		if (Ar.Version >= 34) Ar.Io(Style), Ar.Io(Art), Ar.Io(Fame);
		if (Ar.Version >= 35) Ar.Io(Treaty);
		if (Ar.Version >= 36) Ar.Io(Parks);
		if (Ar.Version >= 38) Ar.Io(Marriage), Ar.Io(Rights);
		if (Ar.Version >= 39) Ar.Io(Sport), Ar.Io(Medals);
	}
};

// One year's fighting in a war, remembered after the states are gone.
struct Battle
{
	double Year = 0.0;
	double Lat = 0.0, Lon = 0.0;
	uint16_t Place = 0; // Named in the losers' speech.
	std::string Winner, Loser;
	int WinnerDead = 0, LoserDead = 0, Refugees = 0;
	bool Siege = false; // Fought for a capital city.

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Year), Ar.Io(Lat), Ar.Io(Lon), Ar.Io(Place), Ar.Io(Winner), Ar.Io(Loser), Ar.Io(WinnerDead), Ar.Io(LoserDead), Ar.Io(Refugees), Ar.Io(Siege);
	}
};

// Something a thinking mind wrote down for others to read: a prayer, a poem, a story or a record.
struct Writing
{
	double Year = 0.0;
	std::string Author, Place, Kind, Text;

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Year), Ar.Io(Author), Ar.Io(Place), Ar.Io(Kind), Ar.Io(Text);
	}
};

enum Government : uint8_t
{
	Chieftain,
	Monarchy,
	Aristocracy, // Nobles rule with, or over, the monarch.
	Republic,    // Leading citizens choose who rules, for a time.
	Democracy,   // Everyone chooses.
	Dictatorship,
	Theocracy    // Priests rule in the name of the state faith.
};

inline const char* GovName(int Gov)
{
	static const char* Names[] = {"rule by a chieftain", "monarchy", "aristocracy", "republic", "democracy", "dictatorship", "theocracy"};
	return Names[Gov < 7 ? Gov : 0];
}

enum IdeaKind : uint8_t
{
	NoIdeology,
	Liberalism,      // Rights and votes for all.
	Nationalism,     // One people, one state.
	Socialism,       // Shared wealth, workers first.
	Militarism,      // Strength and conquest.
	Environmentalism // Save the living world.
};

inline const char* IdeologyName(int Idea)
{
	static const char* Names[] = {"none", "liberalism", "nationalism", "socialism", "militarism", "environmentalism"};
	return Names[Idea < 6 ? Idea : 0];
}

// Share of what each band puts by that the state takes each year.
// What an art style is mostly made of, by what its people can make and where they live.
inline const char* ArtName(int Art)
{
	static const char* Names[] = {"songs and dances", "carvings and statues", "painting", "music", "poetry and plays", "novels", "photographs and films", "recorded music"};
	return Names[Art < 8 ? Art : 0];
}

inline const char* MarriageName(int M)
{
	static const char* Names[] = {"no fixed custom", "one husband, one wife", "rich men take several wives", "a woman may take several husbands"};
	return Names[M < 4 ? M : 0];
}

inline const char* RightsName(int R)
{
	static const char* Names[] = {"none of their own", "women may own land and goods", "girls go to school as boys do", "women vote"};
	return Names[R < 4 ? R : 0];
}

inline const char* SportName(int S)
{
	static const char* Names[] = {"none", "wrestling and foot races", "ball games", "horse races", "chariot races", "team games with written rules", "motor racing"};
	return Names[S < 7 ? S : 0];
}

inline float TaxRate(int Gov)
{
	static const float Rates[] = {0.05f, 0.08f, 0.06f, 0.07f, 0.10f, 0.12f, 0.09f};
	return Rates[Gov < 7 ? Gov : 0];
}

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

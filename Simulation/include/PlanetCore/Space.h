#pragma once

// Life beyond Earth: a coarse layer of colonies on other worlds, founded once people know how to
// get there, which grow or die out as the ageing Sun makes each place livable or not. Earth itself
// stays fully simulated in Ecosystem and People; colonies are only head counts with a skill set.
// shortcut: colonies have no bands, brains or beliefs of their own; upgrade if life beyond Earth matters.
// Times are in years from today.

#include "Archive.h"
#include "Random.h"
#include "Skills.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace earth
{
namespace space
{
// The Sun leaves its long middle age in about 5 billion years and swells into a red giant, which
// for a few hundred million years warms the outer moons; by about 7.7 billion it has shed its outer
// layers and is a white dwarf, too faint to warm anything.
inline constexpr double kEarthDies = 1.0e9;
inline constexpr double kRedGiant = 5.0e9;
inline constexpr double kWhiteDwarf = 7.7e9;
// Red dwarfs burn for trillions of years; the run stops looking past this.
inline constexpr double kLastStars = 1.0e12;

struct Place
{
	const char* Name;
	double Room;      // People it could hold when fully livable.
	double TripYears; // How long the journey from a neighbour takes.
	double LostShips; // Share of ships that never arrive.
	bool Star;        // Another star system; there can be many of these.
};

inline const std::vector<Place>& Places()
{
	static const std::vector<Place> List = {{"the Moon", 2.0e5, 0.01, 0.02, false}, {"Mars", 1.0e7, 0.5, 0.05, false},
		{"habitats in orbit", 2.0e6, 0.01, 0.01, false}, {"the outer moons (Europa and Titan)", 2.0e6, 5.0, 0.1, false},
		{"another star", 1.0e7, 300.0, 0.3, true}};
	return List;
}

inline constexpr int kMoon = 0, kMars = 1, kHabitats = 2, kOuterMoons = 3, kStar = 4;

// How livable a place is at T (0 not at all .. 1 as good as the young Earth), with the skill to live there.
// shortcut: hand-set steps, not a climate model of each world.
inline double Livable(int P, double T)
{
	switch (P)
	{
	case kMoon: return T < kRedGiant ? 0.3 : 0.0;                                  // Under domes, while the Sun is steady.
	case kMars: return T < kEarthDies ? 0.35 : T < kRedGiant ? 0.6 : 0.0;          // Warmer and wetter once Earth is too hot.
	case kHabitats: return T < kRedGiant ? 0.5 : T < kWhiteDwarf ? 0.2 : 0.0;      // Can move outward as the Sun swells.
	case kOuterMoons: return T < kRedGiant ? 0.15 : T < kRedGiant + 5.0e8 ? 0.8 : 0.0; // Thawed by the red giant, briefly.
	case kStar: return T < kLastStars ? 0.6 : 0.0;
	default: return 0.0;
	}
}

// Real red dwarfs nearest the Sun, then nameless ones further out.
inline std::string StarName(int Index)
{
	static const char* Names[] = {"Proxima Centauri", "Barnard's Star", "Wolf 359", "Lalande 21185", "Ross 128", "Luyten's Star",
		"Teegarden's Star", "Kapteyn's Star", "Gliese 1061", "Wolf 1061", "Gliese 876", "Ross 154"};
	return Index < 12 ? Names[Index] : "a red dwarf " + std::to_string(4 + Index * 3) + " light years out";
}

struct Colony
{
	int Where = 0;
	std::string Name;
	double People = 0.0;
	SkillSet Skills;
	double Founded = 0.0; // When the ship left.
	double Arrives = 0.0; // Nobody counts as living there before this.
	bool OwnTongue = false;

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Where), Ar.Io(Name), Ar.Io(People), Ar.Io(Skills), Ar.Io(Founded), Ar.Io(Arrives), Ar.Io(OwnTongue);
	}
};

struct Event
{
	double When = 0.0;
	std::string Text;

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(When), Ar.Io(Text);
	}
};

class Colonies
{
public:
	explicit Colonies(uint64_t Seed = 315000) : Random(Seed ^ 0x5BACEull) {}

	// Moves on to T. Earth sends ships while EarthPeople live there; every colony with the skills sends them too.
	void Advance(double T, double EarthPeople, SkillSet EarthSkills)
	{
		const double Dt = Now < -1.0e17 ? 0.0 : std::max(0.0, T - Now);
		Now = T;
		if (Dt <= 0.0) return;
		const int Settle = SkillIndex("settling other worlds"), Starships = SkillIndex("starships");
		const double YearsApart = 3000.0; // About how long until a cut-off people speak a new language.
		for (Colony& C : List)
		{
			if (C.People <= 0.0 || T < C.Arrives) continue;
			const double Live = Livable(C.Where, T);
			const double Room = Places()[static_cast<size_t>(C.Where)].Room * Live;
			if (Live <= 0.0)
			{
				C.People *= std::exp(-Dt / 20.0); // The place can no longer hold them.
			}
			else
			{
				// Logistic growth, closed form so a 2,000-year step is as right as 2,000 one-year steps.
				const double R = 0.02 * Live;
				const double E = std::exp(-R * Dt);
				C.People = Room / (1.0 + (Room / std::max(C.People, 1.0) - 1.0) * E);
			}
			// Small colonies fail now and then (a plague, a failed harvest under the dome, a war).
			const double Risk = (C.People < 1000.0 ? 1.0 / 2000.0 : 1.0 / 2.0e6) * Dt;
			if (C.People < 2.0 || Random.Chance(std::min(1.0, Risk)))
			{
				C.People = 0.0;
				Note(T, "The colony on " + C.Name + " has died out");
				continue;
			}
			if (!C.OwnTongue && T - C.Arrives > YearsApart)
			{
				C.OwnTongue = true;
				Note(T, "The people of " + C.Name + " now speak their own language");
			}
		}
		// Ships out, from Earth and from every colony that knows how. About one launch every 200 years
		// from a source that can, at most one new colony per step.
		struct Source
		{
			double People;
			SkillSet Skills;
		};
		std::vector<Source> From;
		if (EarthPeople > 0.0 && T < kEarthDies) From.push_back({EarthPeople, EarthSkills});
		for (const Colony& C : List)
			if (C.People > 0.0 && T >= C.Arrives) From.push_back({C.People, C.Skills});
		for (const Source& S : From)
		{
			if (S.People < 500.0 || !Random.Chance(std::min(1.0, Dt / 200.0))) continue;
			const bool CanStar = Starships >= 0 && S.Skills.Test(Starships);
			const bool CanNear = Settle >= 0 && S.Skills.Test(Settle);
			int To = -1;
			for (int P = 0; P < kStar && CanNear; ++P)
				if (Livable(P, T) > 0.0 && !Settled(P)) To = To < 0 || Livable(P, T) > Livable(To, T) ? P : To;
			if (To < 0 && CanStar && List.size() < kMaxColonies) To = kStar;
			if (To < 0) continue;
			const Place& P = Places()[static_cast<size_t>(To)];
			const std::string Name = To == kStar ? StarName(Stars++) : P.Name;
			if (Random.Chance(P.LostShips))
			{
				Note(T, "A ship bound for " + Name + " is lost on the way");
				continue;
			}
			Colony C;
			C.Where = To;
			C.Name = Name;
			C.People = std::min(To == kStar ? 2000.0 : 1000.0, S.People * 0.1);
			C.Skills = S.Skills;
			C.Founded = T;
			C.Arrives = T + P.TripYears;
			List.push_back(C);
			Note(T, To == kStar ? "A ship leaves for " + Name + ", " + std::to_string(static_cast<int>(P.TripYears)) +
					" years away, with " + std::to_string(static_cast<int>(C.People)) + " people aboard"
								: "The first settlers land on " + Name);
			break;
		}
	}

	double Alive() const
	{
		double N = 0.0;
		for (const Colony& C : List)
			if (Now >= C.Arrives) N += C.People;
		return N;
	}
	// Colonies alive, ships on the way included (so a run does not end while one is still travelling).
	int Count() const
	{
		return static_cast<int>(std::count_if(List.begin(), List.end(), [](const Colony& C) { return C.People > 0.0; }));
	}
	const std::vector<Colony>& All() const { return List; }
	const std::vector<Event>& Events() const { return Log; }
	double Time() const { return Now; }

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Now), Ar.Io(List), Ar.Io(Log), Ar.Io(Stars), Random.Serialize(Ar);
	}

private:
	static constexpr size_t kMaxColonies = 64;

	static int SkillIndex(const std::string& Name)
	{
		const auto& S = AllSkills();
		for (size_t i = 0; i < S.size(); ++i)
			if (S[i].Name == Name) return static_cast<int>(i);
		return -1;
	}
	bool Settled(int P) const
	{
		return std::any_of(List.begin(), List.end(), [&](const Colony& C) { return C.Where == P && C.People > 0.0; });
	}
	void Note(double T, const std::string& Text) { Log.push_back({T, Text}); }

	double Now = -1.0e18; // Set by the first Advance.
	std::vector<Colony> List;
	std::vector<Event> Log;
	int Stars = 0;
	Rng Random;
};
} // namespace space
} // namespace earth

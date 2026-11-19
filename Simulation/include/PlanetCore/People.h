// The NPCs: people with early-human bodies and needs, a small brain each, and no knowledge of
// the world. What they do comes from three things, all of which change over time:
//
//   Instincts  the brain wiring they are born with, passed from both parents with small mutations.
//   Learning   each NPC adjusts its own choices by what made it feel better or worse that day.
//   Culture    NPCs copy the habits of camp mates who are doing well, and children copy their carers.
//
// People live in camps (bands). Each day every adult decides for itself what to do; the camp
// moves on when food or water runs low, heading for the best place its scouts have found, and
// splits when it grows too big. Small camps that meet merge.
//
// Plain C++ with no Unreal dependencies, stepped once per simulated day after the Ecosystem.
#pragma once

#include "Archive.h"
#include "Ecosystem.h"
#include "Hominins.h"
#include "Beliefs.h"
#include "Crops.h"
#include "Diseases.h"
#include "Inventions.h"
#include "Minds.h"
#include "Parallel.h"
#include "Polities.h"
#include "Random.h"
#include "Skills.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace earth
{

constexpr int kBrainInputs = 16;
constexpr int kBrainHidden = 12; // Hidden neurons the first people are born with; brains can grow or shrink from there.
constexpr int kBrainActions = 8;

enum class Action : int
{
	Forage,
	Hunt,
	Drink,
	Rest,
	ShareFood,
	Explore,
	Hide,
	Socialize
};

inline const char* ActionName(int A)
{
	static const char* Names[kBrainActions] = {"Forage", "Hunt", "Drink", "Rest", "Share food", "Explore", "Hide", "Socialize"};
	return (A >= 0 && A < kBrainActions) ? Names[A] : "?";
}

inline const char* InputName(int I)
{
	static const char* Names[kBrainInputs] = {"Hunger", "Thirst", "Tiredness", "Fear", "Loneliness", "Urge to mate",
		"Plants here", "Game here", "Water here", "Season", "Age", "Health", "Band size", "Food carried", "Hunting skill", "Always on"};
	return (I >= 0 && I < kBrainInputs) ? Names[I] : "?";
}

// What an NPC is born with: both layers of the brain plus three learning traits. The number of
// hidden neurons can change from parent to child, with no upper limit.
struct Genome
{
	std::vector<float> W1 = std::vector<float>(kBrainHidden * kBrainInputs, 0.0f); // [hidden * input]
	std::vector<float> B1 = std::vector<float>(kBrainHidden, 0.0f);
	std::vector<float> W2 = std::vector<float>(kBrainHidden * kBrainActions, 0.0f); // [hidden * action]
	std::array<float, kBrainActions> B2{};
	float LearningRate = 0.05f; // How strongly a good or bad day changes later choices.
	float Curiosity = 0.6f;     // How often it tries something other than its favourite choice.
	float Imitation = 0.1f;     // How often it copies a successful camp mate.
	float Kindness = 0.5f;      // Personality: how much of what it carries it gives when it shares.

	int Hidden() const { return static_cast<int>(B1.size()); }

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(W1);
		Ar.Io(B1);
		Ar.Io(W2);
		Ar.Io(B2);
		Ar.Io(LearningRate);
		Ar.Io(Curiosity);
		Ar.Io(Imitation);
		if (Ar.Version >= 6) Ar.Io(Kindness);
	}

	// Adds a hidden neuron that starts out with weak, random connections.
	void AddNeuron(Rng& Dice)
	{
		for (int i = 0; i < kBrainInputs; ++i) W1.push_back(static_cast<float>(Dice.Gaussian()) * 0.5f);
		B1.push_back(static_cast<float>(Dice.Gaussian()) * 0.5f);
		for (int a = 0; a < kBrainActions; ++a) W2.push_back(static_cast<float>(Dice.Gaussian()) * 0.2f);
	}

	void RemoveNeuron(int H)
	{
		W1.erase(W1.begin() + H * kBrainInputs, W1.begin() + (H + 1) * kBrainInputs);
		B1.erase(B1.begin() + H);
		W2.erase(W2.begin() + H * kBrainActions, W2.begin() + (H + 1) * kBrainActions);
	}
};

struct Npc
{
	uint32_t Id = 0;
	uint32_t Mother = 0;
	uint32_t Father = 0;
	int Generation = 0;
	bool Female = false;
	double Lat = 0.0; // Where the NPC is today: in camp, or out scouting.
	double Lon = 0.0;
	double AgeDays = 0.0;
	int Camp = 0; // Camp id.

	// Needs and state, 0..1.
	float Energy = 0.8f; // Food in the body; 0 is starving.
	float Water = 0.8f;  // 0 is dying of thirst.
	float Fatigue = 0.2f;
	float Health = 1.0f;
	float Fear = 0.0f;
	float Loneliness = 0.0f;
	float SocialNeed = 0.0f;

	float CarriedKg = 0.0f;
	float HuntSkill = 0.2f;
	float ForageSkill = 0.5f;
	float Heading = 0.0f;
	float OffsetLat = 0.0f; // Where in camp this NPC sits, so markers don't overlap.
	float OffsetLon = 0.0f;

	double PregnantDays = -1.0; // -1 when not pregnant.
	double LastBirthDay = -1e9;
	uint32_t BabyFather = 0;
	std::shared_ptr<const Genome> BabyFatherGenes;
	int Children = 0;

	// Brain: inborn wiring plus what this NPC has learned in its own life (added to the output layer).
	std::shared_ptr<const Genome> Genes;
	std::vector<float> LearnedW2 = std::vector<float>(kBrainHidden * kBrainActions, 0.0f);
	std::array<float, kBrainActions> LearnedB2{};

	// The last decision, kept so it can be shown.
	std::array<float, kBrainInputs> LastInputs{};
	std::vector<float> LastHidden = std::vector<float>(kBrainHidden, 0.0f);
	std::array<float, kBrainActions> LastChoice{}; // Probability of each action.
	int LastAction = static_cast<int>(Action::Rest);
	std::array<int, kBrainActions> ActionCounts{};

	SkillSet Skills = 0; // What this NPC knows how to do.
	SkillEffects Own;

	// Who they are to others: a name in their band's speech, and the few people they know best,
	// with how much they trust each (-1 a cheat they won't help, +1 someone they'd trust with anything).
	uint16_t Name = 0;
	static constexpr int kBonds = 4;
	std::array<uint32_t, kBonds> BondWith{};
	std::array<float, kBonds> Trust{};
	float Reputation = 0.0f; // How much those who know them trust them, summed; worked out each step.
	bool Leads = false;      // Whether they led their band last step.
	// The best-watered place they have lived, remembered for droughts; taught to children.
	float RefugeLat = 0.0f;
	float RefugeLon = 0.0f;
	float RefugeWater = 0.0f; // How wet it was, fading slowly as the memory grows old.
	float Grief = 0.0f;       // Sorrow for someone they loved who died; it weighs on them and fades.
	uint8_t Kind = 0;         // 0 an NPC (early Homo sapiens); otherwise one of OtherKinds().
	float OtherShare = 0.0f;  // Share of their ancestry from the other kinds of human.
	float BabyFatherShare = 0.0f; // The same for the father of the child she carries.

	float TrustIn(uint32_t Other) const
	{
		for (int b = 0; b < kBonds; ++b)
		{
			if (BondWith[b] == Other) return Trust[b];
		}
		return 0.0f;
	}
	// Changes how much they trust someone, making room by forgetting whoever matters least.
	void Feel(uint32_t Other, float Delta)
	{
		int Slot = -1;
		for (int b = 0; b < kBonds && Slot < 0; ++b)
		{
			if (BondWith[b] == Other) Slot = b;
		}
		if (Slot < 0)
		{
			Slot = 0;
			for (int b = 1; b < kBonds; ++b)
			{
				if (std::abs(Trust[b]) < std::abs(Trust[Slot])) Slot = b;
			}
			if (BondWith[Slot] != 0 && std::abs(Trust[Slot]) >= std::abs(Delta)) return;
			BondWith[Slot] = Other;
			Trust[Slot] = 0.0f;
		}
		Trust[Slot] = std::clamp(Trust[Slot] + Delta, -1.0f, 1.0f);
	}    // What those skills do for this NPC's own gathering, hunting and carrying.

	void LearnSkill(int Skill)
	{
		Skills |= SkillSet(1) << Skill;
		Own = EffectsOf(Skills);
	}

	float Pleasure = 0.0f; // One-off good feelings today, such as company or a kill.
	float RewardBaseline = 0.0f;
	float Wellbeing = 0.0f; // Smoothed over recent days.
	int BandSize = 1;
	bool Alive = true;

	double AgeYears() const { return AgeDays / 365.0; }
	int BrainSize() const { return Genes ? Genes->Hidden() : kBrainHidden; }

	// Sizes what the NPC learns in its life to the brain it was born with.
	void FitBrain()
	{
		LearnedW2.assign(static_cast<size_t>(BrainSize()) * kBrainActions, 0.0f);
		LastHidden.assign(static_cast<size_t>(BrainSize()), 0.0f);
	}
	bool IsChild() const { return AgeYears() < 12.0; }

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Mother), Ar.Io(Father), Ar.Io(Generation), Ar.Io(Female), Ar.Io(Lat), Ar.Io(Lon), Ar.Io(AgeDays), Ar.Io(Camp);
		Ar.Io(Energy), Ar.Io(Water), Ar.Io(Fatigue), Ar.Io(Health), Ar.Io(Fear), Ar.Io(Loneliness), Ar.Io(SocialNeed);
		Ar.Io(CarriedKg), Ar.Io(HuntSkill), Ar.Io(ForageSkill), Ar.Io(Heading), Ar.Io(OffsetLat), Ar.Io(OffsetLon);
		Ar.Io(PregnantDays), Ar.Io(LastBirthDay), Ar.Io(BabyFather), Ar.Io(BabyFatherGenes), Ar.Io(Children);
		Ar.Io(Genes), Ar.Io(LearnedW2), Ar.Io(LearnedB2);
		Ar.Io(LastInputs), Ar.Io(LastHidden), Ar.Io(LastChoice), Ar.Io(LastAction), Ar.Io(ActionCounts);
		Ar.Io(Skills), Ar.Io(Pleasure), Ar.Io(RewardBaseline), Ar.Io(Wellbeing), Ar.Io(BandSize), Ar.Io(Alive);
		if (Ar.Version >= 4) Ar.Io(Name), Ar.Io(BondWith), Ar.Io(Trust), Ar.Io(Leads);
		if (Ar.Version >= 5) Ar.Io(RefugeLat), Ar.Io(RefugeLon), Ar.Io(RefugeWater);
		if (Ar.Version >= 6) Ar.Io(Grief);
		if (Ar.Version >= 8) Ar.Io(Kind), Ar.Io(OtherShare), Ar.Io(BabyFatherShare);
		if (Ar.IsReading()) Own = EffectsOf(Skills);
	}
};

inline constexpr int kWords = 16;

// What each word slot stands for. The sounds are the people's own; the meanings are fixed.
inline const char* WordMeaning(int Slot)
{
	static const char* Meanings[kWords] = {"water", "food", "danger", "lion", "fire", "mother", "child", "come",
		"go", "hunt", "meat", "plant", "sun", "night", "friend", "no"};
	return (Slot >= 0 && Slot < kWords) ? Meanings[Slot] : "?";
}

// Turns a word's number into something sayable: one to three syllables.
inline std::string SayWord(uint16_t Word)
{
	static const char* Starts[] = {"", "b", "d", "g", "h", "k", "l", "m", "n", "p", "r", "s", "t", "w", "y", "z"};
	static const char* Vowels[] = {"a", "e", "i", "o", "u", "aa", "ee", "oo"};
	std::string Out;
	const int Syllables = 1 + Word % 3;
	uint32_t Bits = Word / 3u;
	for (int i = 0; i < Syllables; ++i)
	{
		Out += Starts[Bits % 16u];
		Bits /= 16u;
		Out += Vowels[(Bits + static_cast<uint32_t>(i) * 3u) % 8u];
		Bits /= 4u;
	}
	return Out;
}

struct Camp
{
	int Id = 0;
	double Lat = 0.0;
	double Lon = 0.0;
	std::vector<int> Members; // Indices into the NPC list, rebuilt daily.
	// The best place the camp's scouts have reported, and how good it looked.
	double ScoutLat = 0.0;
	double ScoutLon = 0.0;
	double ScoutScore = -1.0;
	int DaysHere = 0;
	// What anyone at camp can see today, read once per camp instead of once per person.
	float SeenDanger = 0.0f;
	float SeenFood = 0.0f;
	float SeenGame = 0.0f;
	float SeenWater = 0.0f;
	float SeenSeason = 0.0f;
	// Share of what the land around the camp can feed that is still free (0 = full).
	float Room = 1.0f;
	// What anyone in camp knows, and what that does for everyone there.
	SkillSet Known = 0;
	SkillEffects Effects;
	// What camps within a few days' walk know: people pick that up too, more slowly, but only
	// from camps whose speech they can follow.
	SkillSet Nearby = 0;
	// The band's calls and words: each slot drifts to a new random word now and then, and bands that
	// meet borrow words from each other. Bands sharing fewer than half can't understand each other.
	std::array<uint16_t, kWords> Words{};
	float Store = 0.0f; // Kg of dried and smoked food put by for lean times.
	uint8_t Kind = 0;   // The kind of human the band started as.
	uint16_t Crops = 0; // What the band sows (bits of AllCrops()).
	int Neighbour = 0; // Id of a camp within reach this step, 0 if none.
	// For Step(): the wettest place within reach, and the squares the band gathers in and how
	// much food they hold on average.
	float BestWater = 0.0f;
	std::array<int64_t, 5> ForageSquares{};
	float ForageKg = 0.0f;
	// Materials the band has come across, what it has made from them, and its words for both,
	// kept sorted by what they name (see People::MaterialKey and InventionKey).
	uint64_t Met = 0;
	std::vector<uint32_t> Inventions;
	std::vector<std::pair<uint32_t, uint16_t>> Lexicon;
	// What the band believes (ids into People's list of beliefs), at most three.
	std::vector<uint32_t> Faith;
	float Cohesion = 0.0f; // From its beliefs' practices, worked out each step.
	int DeathsLately = 0;  // Deaths since the band last wondered why.
	uint32_t Leader = 0;   // The most trusted adult, worked out each step.
	uint32_t State = 0;    // The chiefdom, kingdom or other state it belongs to (see Polities.h), 0 none.
	uint16_t Sick = 0;     // The sickness going through the band now (see Diseases.h), 0 none.
	int SickDays = 0;
	std::vector<std::pair<uint16_t, float>> Exposed; // Sicknesses the band's people have lived through, and the last year.

	// What the camp sees, knows and who belongs to it are worked out again each step, so only the
	// rest is saved.
	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Lat), Ar.Io(Lon), Ar.Io(ScoutLat), Ar.Io(ScoutLon), Ar.Io(ScoutScore), Ar.Io(DaysHere), Ar.Io(Room), Ar.Io(Words);
		if (Ar.Version >= 2) Ar.Io(Met), Ar.Io(Inventions), Ar.Io(Lexicon);
		if (Ar.Version >= 3) Ar.Io(Faith), Ar.Io(DeathsLately);
		if (Ar.Version >= 7) Ar.Io(Store);
		if (Ar.Version >= 8) Ar.Io(Kind);
		if (Ar.Version >= 9) Ar.Io(Crops);
		if (Ar.Version >= 4) Ar.Io(Leader);
		if (Ar.Version >= 14) Ar.Io(State);
		if (Ar.Version >= 15) Ar.Io(Sick), Ar.Io(SickDays), Ar.Io(Exposed);
	}
};

// Someone who made a difference: found a skill, was born with a record brain or first reached
// new land. Their family and life are kept so their story can be told.
struct Notable
{
	uint32_t Id = 0;
	uint32_t Mother = 0;
	uint32_t Father = 0;
	bool Female = false;
	int Generation = 0;
	int Brain = 0;
	double BornYear = 0.0;
	std::string Speech; // Their band's word for water when they were noted.
	uint16_t Name = 0;
	std::vector<std::string> Deeds;
	std::vector<uint32_t> Partners;
	std::vector<uint32_t> Children;
	bool Dead = false;
	std::string Death;
	double LastLat = 0.0;
	double LastLon = 0.0;
	uint8_t Kind = 0;

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Mother), Ar.Io(Father), Ar.Io(Female), Ar.Io(Generation), Ar.Io(Brain), Ar.Io(BornYear), Ar.Io(Speech);
		Ar.Io(Deeds), Ar.Io(Partners), Ar.Io(Children), Ar.Io(Dead), Ar.Io(Death), Ar.Io(LastLat), Ar.Io(LastLon);
		if (Ar.Version >= 4) Ar.Io(Name);
		if (Ar.Version >= 8) Ar.Io(Kind);
	}
};

// The first time something was made, and how often bands hit on it again by themselves.
struct InventionRecord
{
	uint32_t Id = 0;
	double Year = 0.0;
	uint32_t Inventor = 0;
	int CampId = 0;
	double Lat = 0.0;
	double Lon = 0.0;
	uint16_t Word = 0; // What its makers called it.
	int Times = 0;     // Bands that made it by themselves, without learning it.
	// How it came about: 0 by people trying things. Kept so ideas from other sources can be told apart later.
	int Origin = 0;

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Year), Ar.Io(Inventor), Ar.Io(CampId), Ar.Io(Lat), Ar.Io(Lon), Ar.Io(Word), Ar.Io(Times), Ar.Io(Origin);
	}
};

// A belief as it was first taught, and how far it has spread.
struct BeliefRecord
{
	uint32_t Id = 0;
	int Subject = 0;
	int Claim = 0;
	int Practice = 0;
	uint32_t Parent = 0; // The belief it split from, 0 if none.
	double Year = 0.0;
	uint32_t Founder = 0;
	int CampId = 0;
	double Lat = 0.0;
	double Lon = 0.0;
	uint16_t Word = 0; // What its first believers called it.
	int Peak = 0;      // Most bands that ever held it at once.
	int NextMark = 50; // Bands it must reach for the next event (the first makes it a religion).
	int Origin = 0;    // 0: from people wondering, as with inventions.

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Subject), Ar.Io(Claim), Ar.Io(Practice), Ar.Io(Parent), Ar.Io(Year), Ar.Io(Founder), Ar.Io(CampId), Ar.Io(Lat), Ar.Io(Lon);
		Ar.Io(Word), Ar.Io(Peak), Ar.Io(NextMark), Ar.Io(Origin);
		if (Ar.IsReading() && Ar.Version < 11 && Subject >= kOldSubjectCount) Subject += SubjectCount() - kOldSubjectCount;
	}
};

struct PeopleSettings
{
	// Omo Kibish in southern Ethiopia, among the oldest Homo sapiens sites in East Africa.
	double StartLat = 5.4;
	double StartLon = 35.9;
	int StartCount = 30;
	// Optional hard limit on the number of people; 0 means none. Normally the land sets the limit.
	int MaxPeople = 0;
	// How many people a square km of the greenest land can feed. The real figure for hunter-gatherers
	// is about 0.05-0.1; this is much lower so the whole world's people stay affordable to simulate.
	double PeoplePerKm2 = 0.0003;
	// A camp this big starts to think about splitting.
	int SplitSize = 40;
	// Food people can gather from plants (fruit, nuts, roots) in a quarter-degree square at full plant cover.
	double GatherableKg = 3000.0;
	// If everyone dies, a new band arrives at the start, born with the instincts of the most
	// successful NPCs so far (lightly mutated), so the run goes on and keeps what evolved.
	bool NewBandWhenGone = true;
	// Neanderthals, Denisovans and the other kinds of human alive at the start, in their real homes.
	// shortcut: off by default until tuned; in tests Neanderthals die out within centuries and
	// Homo naledi spreads across Africa, neither like the real record.
	bool OtherHumans = false;
	// How fast people hit on skills, compared with real history: at 1, a world of about 3,000
	// people who know what a skill builds on takes about as long to find it as people really did
	// (Skill::YearsToFind). More people find things sooner, fewer later; the clever and curious
	// are likelier to be the ones who do.
	double DiscoveryPace = 1.0;
	// Tries a year at making something new from materials, per person of average wits and curiosity.
	double InventionTriesPerYear = 2e-4;
	// Chance a year that one person of average wits, in a band that can talk, explains something
	// with a new belief; hardship makes it up to four times likelier.
	double BeliefTriesPerYear = 1e-5; // Run 4 had 50 religions 3,000 years after the first belief at 5e-5.
	// CPU threads for Step(); 0 uses all cores. Results are the same for any number.
	int Threads = 0;
};

class People
{
public:
	People(Ecosystem* InEco, uint64_t Seed, const PeopleSettings& InSettings = PeopleSettings())
		: Eco(InEco), Dice(Seed ^ 0x5eed0fa11ull), BaseSeed(Seed), Settings(InSettings)
	{
		Regions.assign(6, false);
		Regions[0] = true; // Africa
		StartBand();
		PlaceNames[0] = LandName(Camps[0], 0);
		AddEvent("A band of " + std::to_string(Settings.StartCount) + " people wakes up in East Africa, knowing nothing. They call their land " + PlaceNames[0]);
	}

	// ---- Queries ----------------------------------------------------------------------------

	// Which land a place is in: 0 Africa, 1 Asia, 2 Europe, 3 Australia, 4 the Americas, 5 elsewhere.
	static int RegionOf(double Lat, double Lon)
	{
		if (Lon < -30.0) return Lon > -169.0 ? 4 : 1; // West of the Bering Strait is Siberia, in Asia.
		if (Lat < -10.0 && Lon > 112.0) return 3;
		if (Lat < 37.5 && Lon > -20.0 && Lon < 52.0)
		{
			if (Lat < 12.0) return 0;
			// Southern Europe's shores reach below Africa's northern tip: Spain, Sicily and Malta, Greece and Crete.
			if ((Lon > -10.0 && Lon < -1.5 && Lat > 36.0) || (Lon >= 12.0 && Lon < 16.0 && Lat > 35.7) ||
				(Lon >= 19.0 && Lon < 27.0 && Lat > 34.7))
				return 2;
			if (Lat >= 31.7 && Lon >= 26.0) return 1; // Anatolia, Cyprus and the Levant: Africa's shore is all south of here.
			// The Red Sea and Sinai separate Africa from Arabia and the Levant.
			const double Edge = Lat < 29.0 ? 43.0 - (Lat - 13.0) * 9.0 / 16.0 : 32.5;
			return Lon < Edge ? 0 : 1;
		}
		if (Lat >= 36.0 && Lon > -12.0 && Lon < 40.0 && !(Lat < 42.0 && Lon > 26.0)) return 2;
		if (Lon >= 26.0) return 1;
		return 5;
	}

	const std::vector<Npc>& All() const { return Npcs; }
	const std::vector<Camp>& AllCamps() const { return Camps; }
	const std::vector<Polity>& AllStates() const { return States; }
	const std::vector<Disease>& AllDiseases() const { return Diseases; }
	long long DiseaseDeathsSoFar() const { return DiseaseDeaths; }
	// Bands sick right now.
	int SickBands() const { return static_cast<int>(std::count_if(Camps.begin(), Camps.end(), [](const Camp& C) { return C.Sick != 0; })); }
	// Starts a sickness in a band, as if it had just jumped to people there; returns its id.
	uint16_t StartDisease(size_t Slot, int Source, float Deadliness, float Spread, int Days)
	{
		Camp& C = Camps[Slot];
		Disease D;
		D.Id = static_cast<uint16_t>(Diseases.size() + 1);
		D.Word = static_cast<uint16_t>(C.Words[1] * 613u + C.Words[9] * 7u + D.Id * 4099u);
		D.Source = Source, D.Deadliness = Deadliness, D.Spread = Spread, D.Days = Days;
		D.Year = Eco->Years(), D.CampId = C.Id, D.Lat = C.Lat, D.Lon = C.Lon;
		Diseases.push_back(D);
		C.Sick = D.Id, C.SickDays = 0;
		Diseases.back().Bands = 1;
		return D.Id;
	}
	std::string DiseaseName(const Disease& D) const
	{
		return "\"" + SayWord(D.Word) + "\" (" + (D.Source >= 0 ? "caught from " + std::string(Eco->GetSpecies()[static_cast<size_t>(D.Source)].Name) : std::string("a sickness of crowds")) + ")";
	}
	// Every sickness that has killed anyone, deadliest first.
	std::string DiseasesText() const
	{
		std::vector<const Disease*> L;
		for (const Disease& D : Diseases) L.push_back(&D);
		std::sort(L.begin(), L.end(), [](const Disease* A, const Disease* B) { return A->Killed != B->Killed ? A->Killed > B->Killed : A->Id < B->Id; });
		std::string Out = std::to_string(Diseases.size()) + " sicknesses have arisen; " + std::to_string(DiseaseDeaths) + " people have died of them; " +
			std::to_string(SickBands()) + " bands are sick now\n\n";
		char Buf[200];
		for (const Disease* D : L)
		{
			int Now = 0;
			for (const Camp& C : Camps) Now += C.Sick == D->Id;
			std::snprintf(Buf, sizeof(Buf), ": first in year %.0f near %.1f, %.1f; kills %.0f%% of those who never met it; %lld dead in %lld bands; %d great plagues; %s\n",
				D->Year, D->Lat, D->Lon, D->Deadliness * 100.0, D->Killed, D->Bands, D->Waves, Now ? ("in " + std::to_string(Now) + " bands now").c_str() : "gone for now");
			Out += DiseaseName(*D) + Buf;
		}
		return Out;
	}
	int PolityCount() const { return static_cast<int>(States.size()); }
	int TradeRoutes() const
	{
		size_t N = 0;
		for (const Polity& S : States) N += S.Partners.size();
		return static_cast<int>(N / 2);
	}
	double WorldTradePerYear() const { return WorldTrade; }
	// Farmed squares and how worn their soil is on average (0 .. 0.5), and forest cleared for fields in km2.
	int FarmedSquares() const { return static_cast<int>(Worn.size()); }
	double AverageWear() const
	{
		double T = 0.0;
		for (const auto& [Key, W] : Worn) T += W;
		return Worn.empty() ? 0.0 : T / static_cast<double>(Worn.size());
	}
	double ForestClearedKm2() const { return ForestCleared * 750.0; } // shortcut: a quarter-degree square is about 750 km2 at mid latitudes.
	// Sends settlers from a state across the sea, up to Range km; true if they landed.
	bool SendVoyage(uint32_t StateId, double Range)
	{
		Polity* S = StateById(StateId);
		return S && Voyage(*S, Range);
	}
	// What the minds have written, and all of it as one text, oldest first.
	const std::vector<Writing>& Writings() const { return Library; }
	std::string LibraryText() const
	{
		std::string Out = std::to_string(Library.size()) + " writings\n\n";
		char Buf[40];
		for (const Writing& W : Library)
		{
			std::snprintf(Buf, sizeof(Buf), "Year %.0f: ", W.Year);
			Out += Buf + W.Kind + " by " + W.Author + ", of " + W.Place + "\n  " + W.Text + "\n\n";
		}
		return Out;
	}
	// Craft guilds in a state's towns: from money and writing until factories take their work.
	static int Guilds(const Polity& S)
	{
		static const int Money = SkillNamed("money"), Writing = SkillNamed("writing"), Factories = SkillNamed("factories");
		return Has(S.Known, Money) && Has(S.Known, Writing) && !Has(S.Known, Factories) ? S.Towns : 0;
	}
	// How unequal the world is: the Gini index of food put by per head across all bands, 0 equal .. 1.
	double WorldGini() const
	{
		std::vector<double> V;
		for (const Camp& C : Camps)
			if (!C.Members.empty()) V.push_back(C.Store / static_cast<double>(C.Members.size()));
		if (V.size() < 2) return 0.0;
		std::sort(V.begin(), V.end());
		double Sum = 0.0, Weighted = 0.0;
		for (size_t i = 0; i < V.size(); ++i) Sum += V[i], Weighted += (2.0 * static_cast<double>(i + 1) - static_cast<double>(V.size()) - 1.0) * V[i];
		return Sum > 0.0 ? Weighted / (static_cast<double>(V.size()) * Sum) : 0.0;
	}
	// How many people on the real Earth each person here stands for, by how their band feeds itself:
	// the sim's land holds about a three-hundredth of the real foragers' density, and each way of
	// farming more. Calibrated so a world that knows it all holds about 8 billion.
	static double RealPerPerson(SkillSet Known)
	{
		static const int Farming = SkillNamed("farming"), Irrigation = SkillNamed("irrigation"), Iron = SkillNamed("iron"), Steam = SkillNamed("steam engines"),
			Fertilizers = SkillNamed("fertilizers"), Vaccines = SkillNamed("vaccines");
		const double K = Has(Known, Fertilizers) ? 12000.0 : Has(Known, Steam) ? 5000.0 : Has(Known, Iron) ? 3000.0 : Has(Known, Irrigation) ? 2000.0 : Has(Known, Farming) ? 1000.0 : 100.0;
		return K * (Has(Known, Vaccines) ? 1.5 : 1.0);
	}
	// The world's people on the real Earth's scale.
	double RealPopulation() const
	{
		double N = 0.0;
		for (const Camp& C : Camps) N += static_cast<double>(C.Members.size()) * RealPerPerson(C.Known);
		return N;
	}
	// Families of languages: tongues that still share a few words (cognates) with each other.
	int LanguageFamilies() const
	{
		std::vector<const Camp*> Firsts;
		for (const Camp* L : LanguageCamps())
		{
			bool Related = false;
			for (const Camp* F : Firsts)
			{
				int Shared = 0;
				for (int w = 0; w < kWords; ++w) Shared += L->Words[static_cast<size_t>(w)] == F->Words[static_cast<size_t>(w)];
				if (L->Kind == F->Kind && Shared >= 3) Related = true;
				if (Related) break;
			}
			if (!Related) Firsts.push_back(L);
		}
		return static_cast<int>(Firsts.size());
	}
	// Scripts in use by states.
	int ScriptsInUse() const
	{
		std::vector<uint16_t> Used;
		for (const Polity& S : States)
			if (S.Script) Used.push_back(S.Script);
		std::sort(Used.begin(), Used.end());
		return static_cast<int>(std::unique(Used.begin(), Used.end()) - Used.begin());
	}
	// How well the world's people in states live on average, 0 .. 1.
	double WorldWelfare() const
	{
		double W = 0.0, N = 0.0;
		for (const Polity& S : States) W += S.Welfare * S.Members, N += S.Members;
		return N > 0.0 ? W / N : 0.0;
	}
	int WorldWarsSoFar() const { return WorldWars; }
	int NuclearStates() const { return static_cast<int>(std::count_if(States.begin(), States.end(), [](const Polity& S) { return Nuclear(S); })); }
	int ThinkerSchools() const { return static_cast<int>(std::count_if(States.begin(), States.end(), [](const Polity& S) { return S.Discoveries >= 3; })); }
	// Everyone killed by quakes, tsunamis, storms, floods and droughts; and how many of each there have been.
	long long DisasterDeaths() const { return DisasterDead; }
	const std::array<int, 5>& DisasterCounts() const { return Struck; }
	int SlaveStates() const { return static_cast<int>(std::count_if(States.begin(), States.end(), [](const Polity& S) { return S.Slavery; })); }
	int RevolutionsSoFar() const
	{
		int N = 0;
		for (const Polity& S : States) N += S.Revolutions;
		return N;
	}
	// How many states are ruled each way (see GovName).
	int StatesRuledBy(int Gov) const
	{
		return static_cast<int>(std::count_if(States.begin(), States.end(), [Gov](const Polity& S) { return S.Gov == Gov; }));
	}
	int MonumentsBuilt() const
	{
		int N = 0;
		for (const Polity& S : States) N += S.Monuments;
		return N;
	}
	// Bands settled across the open sea so far.
	int ColoniesFounded() const { return Colonies; }
	// Over the last year: mean age at death, and births and deaths for every thousand alive.
	double LifeExpectancyYears() const { return LifeExpectancy; }
	double BirthsPer1000() const { return BirthRate; }
	double DeathsPer1000() const { return DeathRate; }
	// Share of the world's people who can read, and its universities.
	double WorldLiteracy() const
	{
		double R = 0.0;
		for (const Polity& S : States) R += S.Literacy * S.Members;
		return R / std::max(1, Alive());
	}
	int WorldUniversities() const
	{
		int N = 0;
		for (const Polity& S : States) N += S.Universities;
		return N;
	}
	// Gt of CO2 burned last year.
	double FuelPerYear() const { return FuelThisYear; }
	// Everyone killed in war and driven from home by it, and the latest battles, newest first.
	long long WarDeaths() const { return WarDead; }
	long long WarRefugees() const { return Refugees; }
	const std::vector<Battle>& AllBattles() const { return Battles; }
	std::string BattlesText(size_t Most = 300) const
	{
		std::string Out = std::to_string(WarDead) + " killed in war and " + std::to_string(Refugees) + " driven from home so far\n\n";
		char Buf[120];
		for (size_t k = Battles.size(); k-- > 0 && Battles.size() - k <= Most;)
		{
			const Battle& B = Battles[k];
			std::snprintf(Buf, sizeof(Buf), "Year %.0f, %s of %s (%.1f, %.1f): ", B.Year, B.Siege ? "siege" : "battle", PersonName(B.Place).c_str(), B.Lat, B.Lon);
			Out += Buf + B.Winner + " beats " + B.Loser + "; " + std::to_string(B.WinnerDead) + " and " + std::to_string(B.LoserDead) + " killed" +
				(B.Refugees ? ", " + std::to_string(B.Refugees) + " flee" : std::string()) + "\n";
		}
		return Out;
	}
	// People living in cities, in the whole world and in its biggest city.
	long long CityPeopleTotal() const
	{
		long long N = 0;
		for (const Polity& S : States) N += S.CityPeople;
		return N;
	}
	int BiggestCity() const
	{
		int N = 0;
		for (const Polity& S : States) N = std::max(N, S.CapitalPeople);
		return N;
	}
	// The cities of the world, biggest first.
	std::string CitiesText() const
	{
		std::vector<const Polity*> List;
		for (const Polity& S : States)
			if (S.City) List.push_back(&S);
		std::sort(List.begin(), List.end(), [](const Polity* A, const Polity* B) { return A->CapitalPeople != B->CapitalPeople ? A->CapitalPeople > B->CapitalPeople : A->Id < B->Id; });
		long long Urban = CityPeopleTotal();
		std::string Out = std::to_string(Urban) + " people live in cities, " + std::to_string(static_cast<int>(100.0 * static_cast<double>(Urban) / std::max(1, Alive()))) +
			"% of the world\n\n";
		char Buf[200];
		for (const Polity* S : List)
		{
			const double Scale = S->Members ? S->RealPeople / S->Members : 0.0;
			std::snprintf(Buf, sizeof(Buf), " (%.1f, %.1f): %d people (about %.0f real); %d cities in the state, %d people in them (%d%% of its people)\n", S->Lat, S->Lon, S->CapitalPeople,
				S->CapitalPeople * Scale, S->Towns, S->CityPeople, static_cast<int>(100.0 * S->CityPeople / std::max(1, S->Members)));
			Out += PersonName(S->City) + ", capital of " + StateName(*S) + Buf;
		}
		return Out;
	}
	const Polity* StateById(uint32_t Id) const
	{
		auto It = std::lower_bound(States.begin(), States.end(), Id, [](const Polity& S, uint32_t V) { return S.Id < V; });
		return It != States.end() && It->Id == Id ? &*It : nullptr;
	}
	Polity* StateById(uint32_t Id) { return const_cast<Polity*>(static_cast<const People*>(this)->StateById(Id)); }
	static std::string StateName(const Polity& S)
	{
		const std::string W = PersonName(S.Word);
		switch (S.Tier)
		{
		case Kingdom: return "the kingdom of " + W;
		case Empire: return "the " + W + " empire";
		case Nation: return "the nation of " + W;
		default: return "the " + W + " chiefdom";
		}
	}
	// The living states, biggest first: size, capital, ruler, religion, wars and laws.
	std::string StatesText() const
	{
		static const char* Lands[6] = {"Africa", "Asia", "Europe", "Australia", "the Americas", "elsewhere"};
		std::vector<const Polity*> List;
		for (const Polity& S : States) List.push_back(&S);
		std::sort(List.begin(), List.end(), [](const Polity* A, const Polity* B) { return A->Members != B->Members ? A->Members > B->Members : A->Id < B->Id; });
		std::string Out = std::to_string(States.size()) + " states (" + std::to_string(StatesFounded) + " founded since the first)\n\n";
		char Buf[160];
		for (const Polity* S : List)
		{
			std::string N = StateName(*S);
			N[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(N[0])));
			std::snprintf(Buf, sizeof(Buf), ", a %s of %d bands and %d people (about %.0f on the real Earth's scale), founded in year %.0f\n", TierName(S->Tier), S->Bands, S->Members,
				S->RealPeople, S->Founded);
			Out += N + Buf;
			std::snprintf(Buf, sizeof(Buf), " near %.1f, %.1f in %s\n", S->Lat, S->Lon, Lands[RegionOf(S->Lat, S->Lon)]);
			Out += "  Capital: " + (S->City ? "the city of " + PersonName(S->City) + " (" + std::to_string(S->CapitalPeople) + " people)" : std::string("the ruler's camp")) + Buf;
			Out += "  Ruler: " + (S->Ruler ? PersonName(S->RulerName) + " #" + std::to_string(S->Ruler) : std::string("nobody")) + ", ruler number " +
				std::to_string(S->Rulers) + "; the next is " + (S->Rule == 0 ? "the ruler's eldest child" : "whoever the capital trusts most") + "\n";
			if (S->Faith && S->Faith <= BeliefLog.size())
			{
				const BeliefRecord& B = BeliefLog[S->Faith - 1];
				Out += "  State religion: \"" + SayWord(B.Word) + "\" (" + BeliefGloss(B.Subject, B.Claim, B.Practice) + ")\n";
			}
			std::snprintf(Buf, sizeof(Buf), "  Tribute paid so far: %.0f kg of food; conquests: %d; wealth from trade: %.0f; richest band has %.1fx the food per head of the poorest\n",
				S->Treasury, S->Conquests, S->Wealth, S->Inequality);
			Out += Buf;
			std::string Traders;
			for (uint32_t P : S->Partners)
				if (const Polity* T = StateById(P)) Traders += (Traders.empty() ? "" : ", ") + StateName(*T);
			if (!Traders.empty()) Out += "  Trades with: " + Traders + "\n";
			std::snprintf(Buf, sizeof(Buf), "  Food costs %.2fx a good year; %.0f in coins; debts %.0f%s; %d guilds\n", S->Price, S->Coins, S->Debt,
				S->Slavery ? "; keeps slaves" : S->Abolished ? "; has abolished slavery" : "", Guilds(*S));
			Out += Buf;
			if (S->Script) Out += "  Writes in the " + PersonName(S->Script) + " script\n";
			std::snprintf(Buf, sizeof(Buf), "  People live %.0f years; welfare %.2f; %d hospitals%s\n", S->LifeExpectancy, S->Welfare, Hospitals(*S), Sanitation(*S) ? "; clean water and sewers" : "");
			Out += Buf;
			if (S->Ideology || S->Revolutions) Out += "  Idea most hold: " + std::string(IdeologyName(S->Ideology)) + "; revolutions: " + std::to_string(S->Revolutions) + "\n";
			std::snprintf(Buf, sizeof(Buf), "  Government: %s; taxes %.0f%% a year; %.0f officials; %d coups and %d civil wars so far\n", GovName(S->Gov), 100.0 * TaxRate(S->Gov),
				OfficialsPerBand(S->Known) * S->Bands, S->Coups, S->CivilWars);
			Out += Buf;
			if (S->Schools || S->Libraries || S->Universities)
				Out += "  Schools: " + std::to_string(S->Schools) + ", libraries: " + std::to_string(S->Libraries) + ", universities: " + std::to_string(S->Universities) + "; " +
					std::to_string(static_cast<int>(100.0f * S->Literacy)) + "% can read\n";
			if (const Polity* E = StateById(S->Enemy)) Out += "  At war with " + StateName(*E) + " for " + std::to_string(S->WarYears) + " years\n";
			std::string Friends;
			for (uint32_t A : S->Allies)
				if (const Polity* F = StateById(A)) Friends += (Friends.empty() ? "" : ", ") + StateName(*F);
			if (!Friends.empty()) Out += "  Allies: " + Friends + "\n";
			for (const std::string& L : S->Laws) Out += "  Law: " + L + "\n";
			Out += "\n";
		}
		return Out;
	}
	const std::vector<BeliefRecord>& AllBeliefs() const { return BeliefLog; }
	const std::vector<EcoEvent>& Firsts() const { return Events; }
	int Alive() const { return static_cast<int>(Npcs.size()); }
	// How many of one kind of human are alive.
	int AliveOfKind(int Kind) const
	{
		return static_cast<int>(std::count_if(Npcs.begin(), Npcs.end(), [Kind](const Npc& P) { return P.Kind == Kind; }));
	}

	int BandCount() const { return static_cast<int>(Camps.size()); }
	int MaxGeneration() const { return HighestGeneration; }
	long long TotalBirths() const { return Births; }
	long long TotalDeaths() const { return Deaths; }
	// Deaths by cause: old age and illness, predators, hunger, thirst, injuries.
	const std::array<long long, 5>& DeathCauses() const { return Causes; }
	long long HuntsWon() const { return Kills; }
	// Skills someone alive knows, and how many people know each.
	int SkillsKnown() const { return KnownNow.Count(); }
	SkillSet EverKnownSkills() const { return EverKnown; }
	SkillSet KnownSkills() const { return KnownNow; }
	// Groups of camps that can understand each other, counted greedily.
	int Languages() const { return static_cast<int>(LanguageCamps().size()); }

	// One camp for each group of camps that understand each other, with how many people speak it.
	std::vector<std::pair<const Camp*, int>> LanguageSpeakers() const
	{
		std::vector<std::pair<const Camp*, int>> Out;
		for (const Camp* F : LanguageCamps()) Out.push_back({F, 0});
		for (const Camp& C : Camps)
		{
			for (auto& L : Out)
			{
				if (Understand(C, *L.first))
				{
					L.second += static_cast<int>(C.Members.size());
					break;
				}
			}
		}
		std::sort(Out.begin(), Out.end(), [](const auto& A, const auto& B) { return A.second > B.second; });
		return Out;
	}

	const std::unordered_map<uint32_t, Notable>& NotablePeople() const { return Notables; }

	template <class A>
	void Serialize(A& Ar)
	{
		if (Ar.Version >= 2) Ar.Io(BaseSeed);
		Ar.Io(Dice), Ar.Io(NextId), Ar.Io(NextCampId), Ar.Io(HighestGeneration), Ar.Io(Births), Ar.Io(Deaths), Ar.Io(Kills), Ar.Io(Causes);
		AnyNpcChild = Births > 0;
		Ar.Io(Regions), Ar.Io(SpeciesHunted), Ar.Io(NextPopulationMark), Ar.Io(NextBandMark), Ar.Io(NextGenerationMark), Ar.Io(NextAgeMark);
		Ar.Io(AnyHunt), Ar.Io(AnyShare), Ar.Io(AnyDeathBy), Ar.Io(Arrivals), Ar.Io(HallOfFame);
		Ar.Io(Notables), Ar.Io(PlaceNames), Ar.Io(RecordBrain), Ar.Io(NextLanguageMark), Ar.Io(LanguageYear), Ar.Io(KnownNow), Ar.Io(EverKnown);
		Ar.Io(Gathered), Ar.Io(Npcs), Ar.Io(Camps), Ar.Io(Events);
		if (Ar.Version >= 2) Ar.Io(InventionLog);
		if (Ar.Version >= 3) Ar.Io(BeliefLog), Ar.Io(Shunned), Ar.Io(ReligionYear);
		if (Ar.Version >= 4) Ar.Io(KnownNames), Ar.Io(FirstLeader);
		if (Ar.Version >= 5) Ar.Io(AnyRefuge);
		if (Ar.Version >= 6) Ar.Io(AnyGrief);
		if (Ar.Version >= 7) Ar.Io(AnyStore), Ar.Io(AnyToolmaker);
		if (Ar.Version >= 8) Ar.Io(OthersStarted), Ar.Io(AnyMixedChild), Ar.Io(MetKinds);
		if (Ar.Version >= 9) Ar.Io(EverKept), Ar.Io(EverSown);
		if (Ar.Version >= 10) Ar.Io(Findable);
		if (Ar.Version >= 14) Ar.Io(States), Ar.Io(NextStateId), Ar.Io(StateDays), Ar.Io(TiersSeen), Ar.Io(NextStateMark), Ar.Io(StatesFounded), Ar.Io(AnyLaw);
		if (Ar.Version >= 15) Ar.Io(Diseases), Ar.Io(DiseaseDays), Ar.Io(DiseaseDeaths);
		if (Ar.Version >= 16) Ar.Io(AnyRoute), Ar.Io(AnySeaRoute), Ar.Io(WorldTrade);
		if (Ar.Version >= 17) Ar.Io(NextCityMark);
		if (Ar.Version >= 18) Ar.Io(Battles), Ar.Io(WarDead), Ar.Io(Refugees), Ar.Io(DeadliestBattle);
		if (Ar.Version >= 19) Ar.Io(NextWarmMark), Ar.Io(FuelRunningOut);
		if (Ar.Version >= 20) Ar.Io(Worn), Ar.Io(ForestCleared), Ar.Io(AnyCollapse);
		if (Ar.Version >= 21) Ar.Io(AnySchool), Ar.Io(AnyLibrary), Ar.Io(AnyUniversity);
		if (Ar.Version >= 22) Ar.Io(NextLifeMark);
		if (Ar.Version >= 23) Ar.Io(Colonies), Ar.Io(AnyVoyage);
		if (Ar.Version >= 24) Ar.Io(AnyCalendar), Ar.Io(AnyFestival), Ar.Io(AnyMonument), Ar.Io(Library);
		if (Ar.Version >= 25) Ar.Io(GovSeen);
		if (Ar.Version >= 26) Ar.Io(IdeasSeen), Ar.Io(AnyMission), Ar.Io(AnyRevolution);
		if (Ar.Version >= 27) Ar.Io(AnySlavery), Ar.Io(AnyAbolition);
		if (Ar.Version >= 28) Ar.Io(DisasterDead), Ar.Io(Struck), Ar.Io(DisastersSeen);
		if (Ar.Version >= 31) Ar.Io(AnyScript);
		if (Ar.Version >= 32) Ar.Io(NextRealMark);
		else if (Ar.IsReading()) NextRealMark = -1.0; // Set from the world as it is, so an old save doesn't announce every mark at once.
		if (Ar.Version >= 30) Ar.Io(WorldWars), Ar.Io(NuclearWars), Ar.Io(InWorldWar), Ar.Io(AnyBomb), Ar.Io(AnyWeb), Ar.Io(AnyAI), Ar.Io(LeagueYear);
		if (Ar.IsReading()) BirthsAtCensus = -1;
		if (Ar.IsReading())
		{
			IndexCamps();
			LandClimate = -1;
			TallyStates();
			for (Polity& S : States)
			{
				auto It = CampSlots.find(S.Capital);
				if (It != CampSlots.end()) S.Lat = Camps[static_cast<size_t>(It->second)].Lat, S.Lon = Camps[static_cast<size_t>(It->second)].Lon;
			}
		}
	}

	// Notes in the history that the world was carried on from a save, maybe by newer code.
	void NoteResumed(uint32_t Version) { AddEvent("Carried on from a save made by save format " + std::to_string(Version)); }
	// What the first people to reach each land called it: Africa, Asia, Europe, Australia, the Americas, new land.
	const std::array<std::string, 6>& LandNames() const { return PlaceNames; }

	// The life stories of everyone who made a difference, in the order they first did.
	std::string StoriesText() const
	{
		std::vector<const Notable*> List;
		for (const auto& [Id, N] : Notables) List.push_back(&N);
		std::sort(List.begin(), List.end(), [](const Notable* A, const Notable* B) { return A->Id < B->Id; });
		auto Who = [&](uint32_t Id) -> std::string
		{
			if (Id == 0) return "unknown";
			auto It = Notables.find(Id);
			auto Nm = KnownNames.find(Id);
			return (Nm != KnownNames.end() ? PersonName(Nm->second) + " " : std::string()) + "#" + std::to_string(Id) +
				(It != Notables.end() ? " (has a story here)" : "");
		};
		std::string Out;
		char Line[256];
		for (const Notable* N : List)
		{
			std::snprintf(Line, sizeof(Line), "#%u, a %s%s of generation %d with a %d-neuron brain, born about year %.0f\n", N->Id,
				N->Kind ? (std::string(OtherKinds()[N->Kind].Name) + " ").c_str() : "", N->Female ? "woman" : "man",
				N->Generation, N->Brain, N->BornYear);
			Out += (N->Name ? PersonName(N->Name) + " " : std::string()) + Line;
			Out += "  Mother " + Who(N->Mother) + ", father " + Who(N->Father) + "\n";
			// Ancestors with stories of their own, a few generations back.
			std::string Line2;
			uint32_t Up[2] = {N->Mother, N->Father};
			for (int Side = 0; Side < 2; ++Side)
			{
				uint32_t Id = Up[Side];
				for (int Depth = 1; Depth <= 4 && Id; ++Depth)
				{
					auto It = Notables.find(Id);
					if (It == Notables.end()) break;
					static const char* Rel[2][4] = {{"mother", "grandmother", "great-grandmother", "great-great-grandmother"},
						{"father", "grandfather", "great-grandfather", "great-great-grandfather"}};
					Line2 += std::string(Line2.empty() ? "" : ", ") + Rel[Side][Depth - 1] + " #" + std::to_string(Id);
					Id = Side == 0 ? It->second.Mother : It->second.Father;
				}
			}
			if (!Line2.empty()) Out += "  Notable ancestors: " + Line2 + "\n";
			if (!N->Speech.empty()) Out += "  Spoke a language where water is \"" + N->Speech + "\"\n";
			for (const std::string& D : N->Deeds) Out += "  " + D + "\n";
			if (!N->Partners.empty())
			{
				Out += "  Had children with ";
				for (size_t k = 0; k < N->Partners.size(); ++k) Out += (k ? ", " : "") + Who(N->Partners[k]);
				Out += "\n";
			}
			Out += "  " + std::to_string(N->Children.size()) + (N->Children.size() == 1 ? " child" : " children");
			for (size_t c = 0; c < N->Children.size() && c < 12; ++c) Out += std::string(c ? ", " : ": ") + Who(N->Children[c]);
			Out += "\n";
			if (N->Dead)
			{
				Out += "  " + N->Death + "\n";
			}
			else
			{
				std::snprintf(Line, sizeof(Line), "  Still alive, last seen near %.0f, %.0f\n", N->LastLat, N->LastLon);
				Out += Line;
			}
			Out += "\n";
		}
		return Out;
	}

	// Each language's words, its biggest first, with their English meanings.
	std::string LanguagesText(int Most = 20) const
	{
		std::string Out;
		int n = 0;
		for (const auto& [C, Speakers] : LanguageSpeakers())
		{
			if (n++ >= Most) break;
			char Head[96];
			std::snprintf(Head, sizeof(Head), "Language %d: %d speakers, around %.0f, %.0f\n", n, Speakers, C->Lat, C->Lon);
			Out += Head;
			for (int w = 0; w < kWords; ++w)
			{
				Out += "  " + SayWord(C->Words[w]) + " = " + WordMeaning(w) + "\n";
			}
			for (const auto& [Key, Word] : C->Lexicon)
			{
				const uint32_t Id = Key & 0xFFFFFFu;
				const uint32_t Kind = Key >> 24;
				Out += "  " + SayWord(Word) + " = " + (Kind == 1u ? std::string(AllMaterials()[Id].Name) : Kind == 2u ? InventionGloss(Id) : std::string(Concepts()[Id])) + "\n";
			}
		}
		return Out;
	}

	// Things made from materials, in the order they were first made, with how many bands make them now.
	std::string InventionsText() const
	{
		std::vector<const InventionRecord*> List;
		for (const auto& [Id, R] : InventionLog) List.push_back(&R);
		std::sort(List.begin(), List.end(), [](const InventionRecord* A, const InventionRecord* B) { return A->Year != B->Year ? A->Year < B->Year : A->Id < B->Id; });
		std::string Out;
		for (const InventionRecord* R : List)
		{
			int Bands = 0;
			for (const Camp& C : Camps) Bands += HasInvention(C, R->Id);
			char Line[160];
			std::snprintf(Line, sizeof(Line), "Year %.0f near %.0f, %.0f: ", R->Year, R->Lat, R->Lon);
			Out += Line + InventionGloss(R->Id) + ", first called \"" + SayWord(R->Word) + "\". Made independently by " + std::to_string(R->Times) +
				(R->Times == 1 ? " band" : " bands") + "; " + (Bands ? "known by " + std::to_string(Bands) + " bands now" : "forgotten") + "\n";
		}
		return Out;
	}
	// Beliefs held now, most widely held first, with where they came from.
	std::string BeliefsText(int Most = 60) const
	{
		std::vector<int> Bands(BeliefLog.size() + 1, 0);
		for (const Camp& C : Camps)
		{
			for (uint32_t F : C.Faith) Bands[F] += 1;
		}
		std::vector<const BeliefRecord*> List;
		for (const BeliefRecord& B : BeliefLog)
		{
			if (Bands[B.Id] > 0) List.push_back(&B);
		}
		std::sort(List.begin(), List.end(), [&](const BeliefRecord* A, const BeliefRecord* B) { return Bands[A->Id] != Bands[B->Id] ? Bands[A->Id] > Bands[B->Id] : A->Id < B->Id; });
		std::string Out = std::to_string(List.size()) + " beliefs are held now; " + std::to_string(BeliefLog.size()) + " have ever been held. " +
			std::to_string(Shunned) + " times a band refused an invention because of what it believed.\n\n";
		int n = 0;
		for (const BeliefRecord* B : List)
		{
			if (n++ >= Most) break;
			char Line[160];
			std::snprintf(Line, sizeof(Line), "\"%s\", held by %d bands (at most %d), first taught in year %.0f near %.0f, %.0f",
				SayWord(B->Word).c_str(), Bands[B->Id], B->Peak, B->Year, B->Lat, B->Lon);
			Out += Line;
			if (B->Parent) Out += ", broke away from \"" + SayWord(BeliefOf(B->Parent).Word) + "\"";
			Out += "\n  " + BeliefGloss(B->Subject, B->Claim, B->Practice) + "\n";
		}
		return Out;
	}
	int BeliefsHeld() const
	{
		std::vector<uint32_t> All;
		for (const Camp& C : Camps) All.insert(All.end(), C.Faith.begin(), C.Faith.end());
		std::sort(All.begin(), All.end());
		return static_cast<int>(std::unique(All.begin(), All.end()) - All.begin());
	}
	// Beliefs held by at least ten bands.
	int Religions() const
	{
		std::unordered_map<uint32_t, int> Bands;
		for (const Camp& C : Camps)
		{
			for (uint32_t F : C.Faith) Bands[F] += 1;
		}
		int N = 0;
		for (const auto& [Id, Count] : Bands) N += Count >= kReligionBands;
		return N;
	}
	int InventionsKnown() const
	{
		std::vector<uint32_t> All;
		for (const Camp& C : Camps) All.insert(All.end(), C.Inventions.begin(), C.Inventions.end());
		std::sort(All.begin(), All.end());
		return static_cast<int>(std::unique(All.begin(), All.end()) - All.begin());
	}

	std::vector<const Camp*> LanguageCamps() const
	{
		std::vector<const Camp*> Firsts;
		for (const Camp& C : Camps)
		{
			if (C.Members.empty()) continue;
			bool Found = false;
			for (const Camp* F : Firsts)
			{
				if (Understand(C, *F))
				{
					Found = true;
					break;
				}
			}
			if (!Found) Firsts.push_back(&C);
		}
		return Firsts;
	}
	static bool Understand(const Camp& A, const Camp& B)
	{
		if (A.Kind != B.Kind) return false;
		int Shared = 0;
		for (int w = 0; w < kWords; ++w) Shared += A.Words[w] == B.Words[w];
		return Shared * 2 >= kWords;
	}
	std::vector<int> SkillHolders() const
	{
		std::vector<int> Holders(AllSkills().size(), 0);
		for (const Npc& P : Npcs)
		{
			for (size_t k = 0; k < Holders.size(); ++k) Holders[k] += Knows(P.Skills, static_cast<int>(k));
		}
		return Holders;
	}

	// Hidden neurons: the biggest brain alive and the average.
	int BiggestBrain() const
	{
		int Most = 0;
		for (const Npc& P : Npcs) Most = std::max(Most, P.BrainSize());
		return Most;
	}
	// Average personality: how kind (share of food given when sharing) and how curious people are.
	double AverageKindness() const
	{
		double Sum = 0.0;
		for (const Npc& P : Npcs) Sum += P.Genes ? P.Genes->Kindness : 0.5f;
		return Npcs.empty() ? 0.0 : Sum / static_cast<double>(Npcs.size());
	}
	double AverageCuriosity() const
	{
		double Sum = 0.0;
		for (const Npc& P : Npcs) Sum += P.Genes ? P.Genes->Curiosity : 0.6f;
		return Npcs.empty() ? 0.0 : Sum / static_cast<double>(Npcs.size());
	}
	double AverageBrain() const
	{
		double Sum = 0.0;
		for (const Npc& P : Npcs) Sum += P.BrainSize();
		return Npcs.empty() ? 0.0 : Sum / static_cast<double>(Npcs.size());
	}

	// About how many people the whole world's land can feed.
	double LandCapacity()
	{
		if (SquareCapacity.empty())
		{
			MeasureLand();
		}
		double Total = 0.0;
		for (float V : SquareCapacity) Total += V;
		return Total;
	}
	// How many bands have started from scratch (1 = the first band is still going).
	int BandsStarted() const { return Arrivals; }
	// No new band arrives when everyone is gone (the run has passed today).
	void StopNewBands() { Settings.NewBandWhenGone = false; }

	// NPCs are kept in the order they were born, so ids only go up along the list.
	const Npc* Find(uint32_t Id) const
	{
		auto It = std::lower_bound(Npcs.begin(), Npcs.end(), Id, [](const Npc& P, uint32_t V) { return P.Id < V; });
		return It != Npcs.end() && It->Id == Id ? &*It : nullptr;
	}

	// Share of each action among adults, today.
	std::array<double, kBrainActions> ActionShareToday() const
	{
		std::array<double, kBrainActions> Share{};
		double Total = 0.0;
		for (const Npc& P : Npcs)
		{
			if (!P.IsChild())
			{
				Share[P.LastAction] += 1.0;
				Total += 1.0;
			}
		}
		for (double& S : Share)
		{
			S = Total > 0.0 ? S / Total : 0.0;
		}
		return Share;
	}

	// The output-layer weights an NPC actually decides with: inborn plus learned.
	static float OutputWeight(const Npc& P, int Hidden, int Act)
	{
		const int K = Hidden * kBrainActions + Act;
		return P.Genes->W2[K] + P.LearnedW2[K];
	}

	static float OutputBias(const Npc& P, int Act) { return P.Genes->B2[Act] + P.LearnedB2[Act]; }

	// Everything about one NPC's brain, as JSON, for viewing tools.
	std::string BrainJson(const Npc& P) const
	{
		std::string J = "{";
		J += "\"id\":" + std::to_string(P.Id) + ",\"generation\":" + std::to_string(P.Generation);
		J += ",\"female\":" + std::string(P.Female ? "true" : "false");
		J += ",\"age_years\":" + Num(P.AgeYears()) + ",\"sim_year\":" + Num(Eco->Years());
		J += ",\"children\":" + std::to_string(P.Children) + ",\"band_size\":" + std::to_string(P.BandSize);
		J += ",\"traits\":{\"learning_rate\":" + Num(P.Genes->LearningRate) + ",\"curiosity\":" + Num(P.Genes->Curiosity) +
			",\"imitation\":" + Num(P.Genes->Imitation) + ",\"hunting_skill\":" + Num(P.HuntSkill) + ",\"foraging_skill\":" + Num(P.ForageSkill) + "}";
		J += ",\"inputs\":[";
		for (int i = 0; i < kBrainInputs; ++i) J += std::string(i ? "," : "") + "\"" + InputName(i) + "\"";
		J += "],\"actions\":[";
		for (int a = 0; a < kBrainActions; ++a) J += std::string(a ? "," : "") + "\"" + ActionName(a) + "\"";
		// w1 is written [input][hidden], as before brains could grow.
		const int H = P.BrainSize();
		std::vector<float> W1(static_cast<size_t>(kBrainInputs) * H);
		for (int i = 0; i < kBrainInputs; ++i)
			for (int h = 0; h < H; ++h) W1[static_cast<size_t>(i) * H + h] = P.Genes->W1[static_cast<size_t>(h) * kBrainInputs + i];
		J += "],\"hidden_neurons\":" + std::to_string(H);
		J += ",\"w1\":" + Matrix(W1.data(), kBrainInputs, H);
		J += ",\"b1\":" + Matrix(P.Genes->B1.data(), 1, H);
		J += ",\"w2_inborn\":" + Matrix(P.Genes->W2.data(), H, kBrainActions);
		J += ",\"w2_learned\":" + Matrix(P.LearnedW2.data(), H, kBrainActions);
		J += ",\"b2_inborn\":" + Matrix(P.Genes->B2.data(), 1, kBrainActions);
		J += ",\"b2_learned\":" + Matrix(P.LearnedB2.data(), 1, kBrainActions);
		J += ",\"last_inputs\":" + Matrix(P.LastInputs.data(), 1, kBrainInputs);
		J += ",\"last_hidden\":" + Matrix(P.LastHidden.data(), 1, H);
		J += ",\"last_choice\":" + Matrix(P.LastChoice.data(), 1, kBrainActions);
		J += ",\"last_action\":\"" + std::string(ActionName(P.LastAction)) + "\"";
		J += ",\"skills\":[";
		bool AnySkill = false;
		for (size_t k = 0; k < AllSkills().size(); ++k)
		{
			if (Knows(P.Skills, static_cast<int>(k)))
			{
				J += std::string(AnySkill ? "," : "") + "\"" + AllSkills()[k].Name + "\"";
				AnySkill = true;
			}
		}
		J += "]";
		J += ",\"action_counts\":[";
		for (int a = 0; a < kBrainActions; ++a) J += std::string(a ? "," : "") + std::to_string(P.ActionCounts[a]);
		J += "]}";
		return J;
	}

	// ---- Simulation -------------------------------------------------------------------------

	// Call once per day, after Ecosystem::StepDay().
	void StepDay()
	{
		if (!RestartIfGone())
		{
			return;
		}
		Eco->RefreshIndex();
		SurviveBlast();
		WatchSky(1);
		RegrowGatherable();
		IndexCamps();
		SenseCamps(1);

		// Decide first, so everyone acts on the same picture of the day, then act.
		std::vector<int> Chosen(Npcs.size(), -1);
		std::vector<float> WellbeingBefore(Npcs.size(), 0.0f);
		std::vector<int> HuntersIn(Camps.size(), 0);
		for (size_t i = 0; i < Npcs.size(); ++i)
		{
			Npc& P = Npcs[i];
			Sense(P);
			WellbeingBefore[i] = WellbeingOf(P);
			if (P.IsChild() && FindCarer(i) >= 0)
			{
				continue;
			}
			Chosen[i] = Decide(P, Dice);
			if (Chosen[i] == static_cast<int>(Action::Hunt))
			{
				HuntersIn[CampSlot(P.Camp)] += 1;
			}
		}

		std::vector<double> Meat(Camps.size(), 0.0);
		for (size_t i = 0; i < Npcs.size(); ++i)
		{
			Npc& P = Npcs[i];
			const int Slot = CampSlot(P.Camp);
			Camp& C = Camps[Slot];
			P.Lat = C.Lat + P.OffsetLat;
			P.Lon = C.Lon + P.OffsetLon;
			if (Chosen[i] >= 0)
			{
				Act(i, C, Chosen[i], HuntersIn[Slot], Meat[Slot]);
			}
			else
			{
				BeCaredFor(i);
			}
		}

		for (size_t i = 0; i < Npcs.size(); ++i)
		{
			Npc& P = Npcs[i];
			const int Slot = CampSlot(P.Camp);
			if (Meat[Slot] > 0.0)
			{
				// Successful hunters bring the meat back to camp and everyone gets a share.
				const double Share = std::min(4.0, Meat[Slot] / static_cast<double>(std::max<size_t>(1, Camps[Slot].Members.size())));
				P.CarriedKg = static_cast<float>(std::min(10.0 * P.Own.Carry, P.CarriedKg + Share));
			}
			MaybeConceive(i, Chosen[i] == static_cast<int>(Action::Socialize));
			LiveThroughDay(P, Chosen[i]);
			UpdateLoneliness(P);
			if (Chosen[i] >= 0)
			{
				Learn(P, Chosen[i], WellbeingOf(P) - WellbeingBefore[i] + P.Pleasure);
			}
			P.Pleasure = 0.0f;
			P.Wellbeing = P.Wellbeing * 0.95f + WellbeingOf(P) * 0.05f;
			Imitate(i);
		}

		LearnSkills(1);
		Invent(1);
		Believe(1);
		DriftLanguages(1);
		BirthsAndDeaths();
		IndexCamps();
		Sicken(1);
		MoveCamps();
		SplitAndMergeCamps();
		KeepHerdsAndCrops(1);
		Govern(1);
		CheckFirsts();
	}

	// Moves people Days days in one go (up to 30), for long runs. Each NPC's brain decides once
	// for the whole span: half the days go to the action it picks, the rest are spread the way it
	// would choose on a typical day. Drinking and sleeping happen as needed along the way, and the
	// brain learns from how the span went. Coarser than StepDay() but much faster, and the brains
	// run on all CPU cores. Call after Ecosystem::Step(Days).
	void Step(int Days)
	{
		Days = std::clamp(Days, 1, 30);
		if (Days == 1)
		{
			StepDay();
			return;
		}
		if (!RestartIfGone())
		{
			return;
		}
		if (!Pool)
		{
			Pool = std::make_unique<WorkerPool>(Settings.Threads > 0 ? Settings.Threads - 1 : 0);
		}
		Eco->RefreshIndex();
		SurviveBlast();
		WatchSky(Days);
		RegrowGatherable(Days);
		IndexCamps();
		SenseCamps(Days);
		for (Camp& C : Camps)
		{
			C.BestWater = static_cast<float>(std::max(BestWaterNear(C.Lat, C.Lon), WaterFor(C, C.Lat, C.Lon)));
			FindForage(C);
		}

		const size_t Count = Npcs.size();
		std::vector<int> Carer(Count, -1);
		std::vector<int> Dependents(Count, 0);
		for (size_t i = 0; i < Count; ++i)
		{
			if (Npcs[i].IsChild())
			{
				Carer[i] = FindCarer(i);
				if (Carer[i] >= 0)
				{
					Dependents[static_cast<size_t>(Carer[i])] += 1;
				}
			}
		}
		std::vector<int> Chosen(Count, -1);
		std::vector<float> WellbeingBefore(Count, 0.0f);
		const uint64_t Today = static_cast<uint64_t>(Eco->Day());
		Pool->For(Count, [&](size_t i)
		{
			Npc& P = Npcs[i];
			Sense(P);
			WellbeingBefore[i] = WellbeingOf(P);
			if (Carer[i] < 0)
			{
				Rng Mine(BaseSeed ^ (Today * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(P.Id) * 0xBF58476D1CE4E5B9ull));
				Chosen[i] = Decide(P, Mine);
			}
		});

		// Share of the days each NPC spends on each action.
		std::vector<std::array<float, kBrainActions>> Shares(Count);
		std::vector<float> HuntersIn(Camps.size(), 0.0f);
		for (size_t i = 0; i < Count; ++i)
		{
			if (Chosen[i] >= 0)
			{
				for (int a = 0; a < kBrainActions; ++a)
				{
					Shares[i][a] = 0.5f * Npcs[i].LastChoice[a] + (a == Chosen[i] ? 0.5f : 0.0f);
				}
				HuntersIn[CampSlot(Npcs[i].Camp)] += Shares[i][static_cast<int>(Action::Hunt)];
			}
		}
		std::vector<double> Meat(Camps.size(), 0.0);
		HuntOverDays(Shares, HuntersIn, Meat, Days);
		std::vector<float> DailyFood(Count, 0.0f);
		std::vector<uint8_t> Occasional(Count, 0);
		Pool->For(Count, [&](size_t i)
		{
			Npc& P = Npcs[i];
			const Camp& C = Camps[CampSlot(P.Camp)];
			P.Lat = C.Lat + P.OffsetLat;
			P.Lon = C.Lon + P.OffsetLon;
			if (Chosen[i] >= 0)
			{
				Rng Mine(BaseSeed ^ (Today * 0xD1B54A32D192ED03ull) ^ (static_cast<uint64_t>(P.Id) * 0x9E3779B97F4A7C15ull));
				Occasional[i] = WorkOverDays(P, C, Shares[i], HuntersIn[CampSlot(P.Camp)], Days, DailyFood[i], Mine);
			}
		});
		std::vector<double> CampGathered(Camps.size(), 0.0);
		for (size_t i = 0; i < Count; ++i)
		{
			const int Slot = CampSlot(Npcs[i].Camp);
			CampGathered[Slot] += static_cast<double>(DailyFood[i]) * Days;
			// Things done now and then, which touch others: sharing, scouting and company.
			for (int a = 0; a < kBrainActions; ++a)
			{
				if (Occasional[i] & (1 << a))
				{
					double Unused = 0.0;
					Act(i, Camps[Slot], a, 0, Unused);
				}
			}
		}
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			GatherFrom(Camps[c], CampGathered[c]);
		}

		// Everyone who fends for themselves lives through the span; what they don't eat is left over.
		std::vector<float> Spare(Count, 0.0f);
		std::vector<float> ExtraFound(Count, 0.0f);
		Pool->For(Count, [&](size_t i)
		{
			Npc& P = Npcs[i];
			const int Slot = CampSlot(P.Camp);
			const Camp& C = Camps[Slot];
			// Successful hunters bring the meat back to camp and everyone gets a share.
			const float MeatPerDay = static_cast<float>(std::min(4.0, Meat[Slot] / Days / static_cast<double>(std::max<size_t>(1, C.Members.size()))));
			if (Carer[i] < 0)
			{
				const float ForagePerDay = static_cast<float>(ForageYield(P, C));
				Spare[i] = LiveThroughDays(P, &Shares[i], DailyFood[i], MeatPerDay, C.BestWater, Days, ForagePerDay, Dependents[i], &ExtraFound[i]);
			}
		});
		for (size_t i = 0; i < Count; ++i)
		{
			if (ExtraFound[i] > 0.0f)
			{
				GatherFrom(Camps[CampSlot(Npcs[i].Camp)], ExtraFound[i]);
			}
		}
		// Young children are fed from what their carer and the camp have left over.
		for (size_t i = 0; i < Count; ++i)
		{
			if (Carer[i] >= 0)
			{
				BeCaredForDays(i, static_cast<size_t>(Carer[i]), Spare, Days);
			}
		}
		Pool->For(Count, [&](size_t i)
		{
			Npc& P = Npcs[i];
			if (Carer[i] < 0)
			{
				P.CarriedKg = std::min(10.0f * P.Own.Carry, Spare[i] * 0.85f);
			}
			UpdateLoneliness(P);
			if (Chosen[i] >= 0)
			{
				Learn(P, Chosen[i], WellbeingOf(P) - WellbeingBefore[i] + P.Pleasure);
			}
			P.Pleasure = 0.0f;
			P.Wellbeing = P.Wellbeing * 0.95f + WellbeingOf(P) * 0.05f;
		});
		StoreFood(Carer, Spare, Days);
		// Courting and copying stay within a band, so bands take their turns side by side, each with
		// its own dice.
		InParallel(Camps.size(), [&](size_t Here)
		{
			Rng Mine(BaseSeed ^ (Today * 0x94D049BB133111EBull) ^ (static_cast<uint64_t>(Camps[Here].Id) * 0xBF58476D1CE4E5B9ull));
			for (int i : Camps[Here].Members)
			{
				const size_t I = static_cast<size_t>(i);
				MaybeConceive(I, Chosen[I] >= 0 && Mine.Chance(OverDays(Shares[I][static_cast<int>(Action::Socialize)], Days)), Days, Mine);
				Imitate(I, Days, Mine);
			}
		});

		LearnSkills(Days);
		Invent(Days);
		Believe(Days);
		DriftLanguages(Days);
		BirthsAndDeaths(Days);
		IndexCamps();
		Sicken(Days);
		MoveCamps(Days);
		SplitAndMergeCamps(Days);
		KeepHerdsAndCrops(Days);
		Govern(Days);
		CheckFirsts();
	}

	// ---- Thinking minds (see Minds.h) ----------------------------------------------------------

	struct ThoughtCounts
	{
		int Asked = 0, Answered = 0, Made = 0, Found = 0, Dreams = 0, Failed = 0, Unknown = 0;
	};
	const ThoughtCounts& Thoughts() const { return MindCounts; }

	// The most inventive adult in each of the Budget biggest bands thinks once with a real mind.
	// The mind sees only what its band knows, in plain words, with no names or dates from our
	// history; whatever it proposes is checked against the world's own rules: a material it has
	// never met is refused (and logged, as a possible leak from the model's own knowledge), a way of
	// working that needs a skill the band lacks is kept as a dream ahead of its time, and the rest
	// works or doesn't by the materials' properties. Returns one log line per thought.
	std::string Think(const Thinker& Mind, int Budget)
	{
		std::string Log;
		if (Budget <= 0 || Camps.empty()) return Log;
		std::vector<size_t> Order(Camps.size());
		for (size_t c = 0; c < Order.size(); ++c) Order[c] = c;
		const size_t Top = std::min(Order.size(), static_cast<size_t>(Budget));
		std::partial_sort(Order.begin(), Order.begin() + static_cast<std::ptrdiff_t>(Top), Order.end(),
			[&](size_t A, size_t B) { return Camps[A].Members.size() > Camps[B].Members.size(); });
		const auto& Skills = AllSkills();
		const auto& Ms = AllMaterials();
		const auto& Ws = AllWorkings();
		auto Lower = [](std::string S)
		{
			for (char& Ch : S) Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
			while (!S.empty() && std::isspace(static_cast<unsigned char>(S.back()))) S.pop_back();
			while (!S.empty() && std::isspace(static_cast<unsigned char>(S.front()))) S.erase(S.begin());
			return S;
		};
		for (size_t t = 0; t < Top; ++t)
		{
			Camp& C = Camps[Order[t]];
			int Who = -1;
			double Best = -1.0;
			for (int i : C.Members)
			{
				const Npc& P = Npcs[static_cast<size_t>(i)];
				const double K = Smarts(P) * Smarts(P) * P.Genes->Curiosity;
				if (P.AgeYears() >= 15.0 && K > Best) Best = K, Who = i;
			}
			// In a state's capital, the ruler does the thinking for the realm.
			const Polity* Realm = C.State ? StateById(C.State) : nullptr;
			for (int i : C.Members)
				if (Realm && Realm->Capital == C.Id && Npcs[static_cast<size_t>(i)].Id == Realm->Ruler) Who = i;
			if (Who < 0) continue;
			Npc& P = Npcs[static_cast<size_t>(Who)];
			std::string Known, Toward, Met, Made, Ways;
			auto Add = [](std::string& To, const std::string& What) { To += (To.empty() ? "" : ", ") + What; };
			for (size_t k = 0; k < Skills.size(); ++k)
			{
				if (Knows(C.Known, static_cast<int>(k))) Add(Known, Skills[k].Name);
				else if (CanDiscover(C.Known, static_cast<int>(k))) Add(Toward, Skills[k].Name);
			}
			for (size_t m = 0; m < Ms.size(); ++m)
				if ((C.Met >> m) & 1u) Add(Met, Ms[m].Name);
			for (uint32_t Id : C.Inventions) Add(Made, InventionGloss(Id));
			for (size_t w = 0; w < Ws.size(); ++w)
			{
				std::string G = Ws[w].Gloss;
				for (const char* Key : {"{A}", "{B}"})
				{
					const size_t At = G.find(Key);
					if (At != std::string::npos) G.replace(At, 3, Key[1] == 'A' ? "X" : "Y");
				}
				Ways += "\n" + std::to_string(w + 1) + ". " + G;
			}
			const double Lat = std::abs(C.Lat);
			const char* Where = Lat < 15.0 ? "in hot lands near the middle of the world" : Lat < 35.0 ? "in warm lands" : Lat < 55.0 ? "in lands with cold winters" : "in cold lands with long, dark winters";
			const std::string Name = PersonName(P.Name);
			// What sets this person and moment apart, so minds don't all think the same thought.
			std::string Life;
			if (C.Leader == P.Id) Life += "Your band trusts you most and looks to you to lead. ";
			if (P.Grief > 0.3f) Life += "You are grieving someone you loved. ";
			if (C.DeathsLately > 2) Life += std::to_string(C.DeathsLately) + " of your people have died lately. ";
			if (C.SeenDanger > 0.5f) Life += "Dangerous animals are close to camp. ";
			if (Eco->IceCover() > 0.5) Life += "The winters have grown long and hard. ";
			Life += C.Store > 50.0f * static_cast<float>(C.Members.size()) ? "Your band has plenty of food put by. " : C.Store > 0.0f ? "Your band has a little food put by. " : "Your band has no food put by. ";
			for (uint32_t F : C.Faith)
				for (const BeliefRecord& R : BeliefLog)
					if (R.Id == F) Life += "Your people believe: " + BeliefGloss(R.Subject, R.Claim, R.Practice) + ". ";
			// The person's place in a state, and what they could do about it.
			const bool Rules = Realm && Realm->Ruler == P.Id;
			std::string Deeds;
			if (Realm)
			{
				Life += "Your band belongs to " + StateName(*Realm) + ", " + std::to_string(Realm->Bands) + " bands and " + std::to_string(Realm->Members) +
					" people" + (Rules ? ", and you are its ruler. " : Realm->Ruler ? ", ruled by " + PersonName(Realm->RulerName) + ". " : ". ");
				if (Realm->Gov >= Aristocracy) Life += "It is a " + std::string(GovName(Realm->Gov)) + ". ";
				if (const Polity* E = StateById(Realm->Enemy)) Life += "It is at war with " + StateName(*E) + ". ";
				for (const std::string& L : Realm->Laws) Life += "One of its laws: " + L + ". ";
				Deeds = Rules ? "\"war\" (attack the nearest other state), \"peace\" (end a war), \"ally\" (make friends with the nearest other state), \"trade\" (send merchants to a new state), "
								"\"city\" (build a city as your capital), \"school\" (open a school), \"library\" (gather the state's writings), \"law\" (give a law, written in \"law\") or \"\""
							  : "\"rebel\" (lead your band to break away), \"revolt\" (rise up to overthrow the rulers), \"preach\" (spread your band's beliefs to the whole state) or \"\"";
			}
			else if (C.Leader == P.Id && (Has(C.Known, SkillNamed("villages")) || Has(C.Known, SkillNamed("farming"))))
			{
				Deeds = "\"found\" (unite the bands near you under your rule) or \"\"";
			}
			static const int WritingSkill = SkillNamed("writing");
			const bool Writes = Has(C.Known, WritingSkill) && P.AgeYears() >= 12.0;
			std::vector<std::string>& Past = PastThoughts[P.Id];
			std::string Before;
			for (const std::string& T : Past) Before += "\n- \"" + T + "\"";
			const std::string Prompt = "You are " + Name + ", aged " + std::to_string(static_cast<int>(P.AgeYears())) + ", in a band of " +
				std::to_string(C.Members.size()) + " people living " + Where + ". " + Life + "\n" +
				(Before.empty() ? "" : "Things you have thought before:" + Before + "\nThink of something new this time, not the same idea again.\n") +
				"Your band knows how to: " + (Known.empty() ? "nothing beyond what everyone is born knowing" : Known) + ".\n" +
				"Materials you have come across: " + (Met.empty() ? "none yet" : Met) + ".\n" +
				"Things your band has made: " + (Made.empty() ? "nothing yet" : Made) + ".\n" +
				"Ways anyone could try to work materials (X and Y stand for materials):" + Ways + "\n" +
				"Things your band could work toward learning next: " + (Toward.empty() ? "nothing" : Toward) + ".\n" +
				"Think as yourself about your band's life: food, danger, cold, children, the sick, the dead, your gods, your neighbours, "
				"or a better way to make something. You know only what is written here; do not use anything else you know. Speak "
				"plainly, as someone of your time would, with no modern words. Answer with one JSON object with the keys \"thought\" (one or two sentences in "
				"your own voice), \"way\" (the number of one way of working to try), \"material\" (a material you have come across, "
				"for X), \"material2\" (another one for Y, or \"\") and \"work_toward\" (one thing from the list to work toward, or \"\")" +
				(Writes ? std::string(", \"writing\" (if you wish, a few sentences you write down for others to read, or \"\") and \"kind\" (prayer, poem, story, law, record or philosophy)") : std::string()) +
				(Deeds.empty() ? std::string(".") : ", and \"deed\": one of " + Deeds + ".");
			++MindCounts.Asked;
			const std::string Answer = Mind(Prompt);
			if (Answer.empty()) continue;
			++MindCounts.Answered;
			std::string Thought = minds::JsonString(Answer, "thought");
			if (Thought.size() > 300) Thought.resize(300);
			if (!Thought.empty())
			{
				Past.push_back(Thought);
				if (Past.size() > 3) Past.erase(Past.begin());
			}
			if (MindCounts.Answered == 1 && !Thought.empty())
				AddEvent(Name + " has the first real thought anyone in the world has had: \"" + Thought + "\"");
			std::string Line = "Year " + std::to_string(static_cast<long long>(Eco->Years())) + " | " + Name + " (band of " +
				std::to_string(C.Members.size()) + ") thinks: \"" + Thought + "\"";

			// Something to make: a way of working and the materials for it.
			auto MaterialNamed = [&](const std::string& Text) -> int
			{
				const std::string L = Lower(Text);
				if (L.empty()) return -1;
				for (size_t m = 0; m < Ms.size(); ++m)
					if (L == Ms[m].Name) return static_cast<int>(m);
				// Everyone knows these, but they aren't materials the world lets anyone work.
				static const char* Everyday[] = {"water", "snow", "ice", "fire", "air", "earth", "mud", "dirt", "sand", "soil", "rain",
					"wind", "sun", "moon", "stars", "sky", "river", "sea", "grass", "leaves"};
				for (const char* E : Everyday)
					if (L == E) return -1;
				return -2; // Named, but no such material.
			};
			const int W = std::atoi(minds::JsonString(Answer, "way").c_str()) - 1;
			const int A = MaterialNamed(minds::JsonString(Answer, "material"));
			const int B = MaterialNamed(minds::JsonString(Answer, "material2"));
			const bool NeedsB = W >= 0 && W < static_cast<int>(Ws.size()) && Ws[static_cast<size_t>(W)].NeedB >= 0;
			auto Unmet = [&](int M) { return M == -2 || (M >= 0 && !((C.Met >> M) & 1u)); };
			if (Unmet(A) || (NeedsB && Unmet(B)))
			{
				++MindCounts.Unknown;
				Line += " | REFUSED: names a material the band has never met (" + minds::JsonString(Answer, "material") + ", " +
					minds::JsonString(Answer, "material2") + ")";
			}
			else if (W < 0 || W >= static_cast<int>(Ws.size()) || A < 0 || (NeedsB && B < 0))
			{
				Line += " | tries nothing";
			}
			else
			{
				const Working& K = Ws[static_cast<size_t>(W)];
				const uint32_t Id = InventionId(W, A, NeedsB ? B : -1);
				const std::string Gloss = InventionGloss(Id);
				if (K.NeedsSkill >= 0 && !Knows(C.Known, K.NeedsSkill))
				{
					++MindCounts.Dreams;
					Line += " | dreams of " + Gloss + ", which needs " + Skills[static_cast<size_t>(K.NeedsSkill)].Name + " first";
					Remember(P, "dreamed of " + Gloss + " long before anyone could make it");
				}
				else if (InventionStrength(W, A, NeedsB ? B : -1, C.Known) <= 0.0f)
				{
					++MindCounts.Failed;
					Line += " | tries " + Gloss + ": it doesn't work";
				}
				else if (HasInvention(C, Id))
				{
					Line += " | " + Gloss + ": the band already makes it";
				}
				else if (const BeliefRecord* No = Forbids(C, Id))
				{
					Shun(C, Id, P, *No);
					Line += " | " + Gloss + ": forbidden by the band's beliefs";
				}
				else
				{
					++MindCounts.Made;
					MakeFirst(C, Id, P);
					Remember(P, "thought it through and made " + Gloss + (Thought.empty() ? "" : ": \"" + Thought + "\""));
					Line += " | makes " + Gloss;
				}
			}

			// Something to work toward: a skill the band is ready for. A mind set on it finds it far
			// sooner than chance would, the quicker the less it took real people.
			const std::string Aim = Lower(minds::JsonString(Answer, "work_toward"));
			int Goal = -1;
			for (size_t k = 0; k < Skills.size() && !Aim.empty(); ++k)
				if (Aim == Lower(Skills[k].Name)) Goal = static_cast<int>(k);
			constexpr int kPlanting = 25;
			if (Aim.empty())
			{
			}
			else if (Goal < 0 || !CanDiscover(C.Known, Goal))
			{
				++MindCounts.Unknown;
				Line += " | REFUSED goal \"" + Aim + "\": not something the band is ready for";
			}
			else if (Goal == kPlanting && Eco->IceCover() > 0.15)
			{
				Line += " | works toward planting, but the seasons swing too hard in this ice age";
			}
			else if (Dice.Chance(std::min(0.25, 50.0 / std::max(50.0, static_cast<double>(Skills[static_cast<size_t>(Goal)].YearsToFind)))))
			{
				++MindCounts.Found;
				Discover(P, Goal);
				Remember(P, "set their mind on " + Aim + " and worked it out");
				Line += " | works toward " + Aim + " and FINDS it";
			}
			else
			{
				Line += " | works toward " + Aim + ", not yet";
			}
			if (!Deeds.empty()) Line += DoDeed(P, static_cast<int>(Order[t]), Lower(minds::JsonString(Answer, "deed")), minds::JsonString(Answer, "law"), Thought);
			if (std::string Text = minds::JsonString(Answer, "writing"); Writes && Text.size() >= 10)
			{
				if (Text.size() > 600) Text.resize(600);
				std::string Kind = Lower(minds::JsonString(Answer, "kind"));
				if (Kind != "prayer" && Kind != "poem" && Kind != "story" && Kind != "law" && Kind != "record" && Kind != "philosophy") Kind = "writing";
				const Polity* Home = C.State ? StateById(C.State) : nullptr;
				Writing Piece;
				Piece.Year = Eco->Years(), Piece.Author = Name, Piece.Place = Home ? StateName(*Home) : "a band that calls its land " + LandName(C, RegionOf(C.Lat, C.Lon));
				Piece.Kind = Kind, Piece.Text = Text;
				if (Library.empty()) AddEvent(Name + " writes the first " + Kind + " anyone has written down: \"" + Text.substr(0, 160) + "\"");
				Library.push_back(Piece);
				Line += " | WRITES a " + Kind;
			}
			Log += Line + "\n";
		}
		return Log;
	}

private:
	ThoughtCounts MindCounts;
	std::mutex EventMutex;
	// Each thinker's last few thoughts, so a mind builds on them instead of starting over.
	// shortcut: not saved, so minds forget them on resume; save them if long memories matter.
	std::unordered_map<uint32_t, std::vector<std::string>> PastThoughts;
	Ecosystem* Eco;
	Rng Dice;
	uint64_t BaseSeed = 0;
	std::unique_ptr<WorkerPool> Pool;
	PeopleSettings Settings;
	std::vector<Npc> Npcs;
	std::vector<Camp> Camps;
	std::unordered_map<int, int> CampSlots; // Camp id -> index in Camps.
	std::vector<EcoEvent> Events;
	uint32_t NextId = 1;
	int NextCampId = 1;
	int HighestGeneration = 0;
	long long Births = 0;
	long long Deaths = 0;
	long long Kills = 0;
	std::array<long long, 5> Causes{};
	std::vector<bool> Regions;
	std::vector<bool> SpeciesHunted;
	int NextPopulationMark = 50;
	int NextBandMark = 2;
	int NextGenerationMark = 1;
	int NextAgeMark = 50;
	bool AnyHunt = false;
	bool AnyShare = false;
	std::array<bool, 5> AnyDeathBy{};
	int Arrivals = 0;
	// The most successful NPCs so far (most children, then longest lives), whose instincts a new band starts from.
	std::vector<std::pair<double, std::shared_ptr<const Genome>>> HallOfFame;

	std::unordered_map<uint32_t, Notable> Notables;
	std::array<std::string, 6> PlaceNames;
	int RecordBrain = kBrainHidden; // Only brains bigger than the first band's count as records.
	int NextLanguageMark = 2;
	int LanguageYear = 0;
	SkillSet KnownNow = 0;  // Skills someone alive knows.
	SkillSet EverKnown = 0; // Skills anyone has ever known.
	// The year each skill first became findable (all it builds on known somewhere); -1 not yet.
	// An older save starts the wait afresh for those already findable.
	std::vector<double> Findable;
	// Every invention ever made, keyed by InventionId: where, when and by whom it was first made.
	std::unordered_map<uint32_t, InventionRecord> InventionLog;
	// Every belief ever held; a belief's id is its place in this list plus one.
	std::vector<BeliefRecord> BeliefLog;
	int64_t Shunned = 0; // Inventions a band refused because its beliefs forbade them.
	int ReligionYear = -1;
	// Names of people with stories and of their parents, partners and children.
	std::unordered_map<uint32_t, uint16_t> KnownNames;
	uint32_t FirstLeader = 0;
	// States above the band, kept in order of id (see Polities.h and Govern).
	std::vector<Polity> States;
	uint32_t NextStateId = 1;
	int StateDays = 0;          // Days since states last took their yearly turn.
	uint8_t TiersSeen = 0;      // Bits of the tiers that have ever existed.
	int NextStateMark = 10;
	int NextCityMark = 100;
	std::vector<Battle> Battles; // The latest years of fighting, oldest first.
	long long WarDead = 0, Refugees = 0;
	int DeadliestBattle = 0;
	int NextWarmMark = 1;
	std::unordered_map<int64_t, double> Worn; // How worn out each farmed square's soil is, 0 .. 0.5.
	int ForestCleared = 0;                     // Forest squares cleared for fields.
	bool AnyCollapse = false;
	bool AnySchool = false, AnyLibrary = false, AnyUniversity = false;
	int NextLifeMark = 40;
	int Colonies = 0; // Bands settled across the open sea.
	bool AnyCalendar = false, AnyFestival = false, AnyMonument = false;
	int IdeasSeen = 1;
	bool AnyMission = false, AnyRevolution = false;
	bool AnySlavery = false, AnyAbolition = false;
	bool AnyScript = false;
	double NextRealMark = 1e6;
	int WorldWars = 0, NuclearWars = 0;
	bool InWorldWar = false, AnyBomb = false, AnyWeb = false, AnyAI = false;
	double LeagueYear = 0.0;
	long long DisasterDead = 0;
	std::array<int, 5> Struck{}; // Earthquakes, tsunamis, hurricanes, floods and droughts that hit anyone or not.
	int DisastersSeen = 0;
	int GovSeen = 3; // Bits of the kinds of government that have ever existed; chieftains and monarchs are noted as tiers.
	std::vector<Writing> Library; // Everything the thinking minds have written down, oldest first.
	bool AnyVoyage = false;
	double AgeAtDeath = 0.0, LifeExpectancy = 0.0, BirthRate = 0.0, DeathRate = 0.0; // Not saved: counted afresh each year.
	long long DeathsThisYear = 0, BirthsAtCensus = -1;
	std::unordered_map<uint32_t, std::pair<double, int>> StateAges; // Ages at death this year by state; not saved.
	bool FuelRunningOut = false;
	double FuelThisYear = 0.0; // Gt of CO2; not saved.
	int StatesFounded = 0;
	bool AnyLaw = false;
	bool AnyRoute = false, AnySeaRoute = false;
	double TradeThisYear = 0.0, WorldTrade = 0.0;
	std::vector<Disease> Diseases; // Every sickness that has ever arisen; a sickness's id is its place plus one.
	int DiseaseDays = 0;
	long long DiseaseDeaths = 0;
	bool AnyRefuge = false;
	bool AnyGrief = false;
	bool AnyStore = false;
	bool AnyToolmaker = false;
	bool OthersStarted = false;
	std::vector<double> LastReturn; // When each other kind last came back from a refuge; not saved.
	bool AnyNpcChild = false; // Not saved: a resumed run already had its first child.
	bool AnyMixedChild = false;
	uint32_t MetKinds = 0;
	std::vector<bool> EverKept; // Per species: anyone has kept a herd of it.
	uint16_t EverSown = 0;      // Crops anyone has sown.

	// Camps by one-degree square, so finding nearby camps doesn't look at every camp.
	std::unordered_map<int64_t, std::vector<int>> CampGrid;
	// People each one-degree square can feed, from its plant cover at the best time of year.
	std::vector<float> SquareCapacity;
	// Squares on the sea shore, and the people their shellfish and fish feed on top, for bands that fish.
	std::vector<uint8_t> ShoreSquare;
	std::vector<float> ShoreCapacity;
	int LandClimate = -1;
	// The latest sign in the sky everyone saw: an eclipse (the moon) or a great comet (the stars),
	// remembered for a year. Not saved.
	int OmenDays = 0, OmenSubject = 0;
	bool SawEclipse = false, SawComet = false;

	// A great comet comes about every 20 years, and a total eclipse crosses some band's land
	// about every 50 (a band's own sky sees one only every few centuries).
	void WatchSky(int Days)
	{
		OmenDays = std::max(0, OmenDays - Days);
		// shortcut: separate dice from the people's, so the rest of the run is unchanged by the sky.
		Rng Sky(static_cast<uint64_t>(Eco->Day()) * 0x94D049BB133111EBull ^ 0x5EEDull);
		const double PerDay = Days / Ecosystem::kDaysPerYear;
		if (Sky.Chance(PerDay / 50.0))
		{
			OmenDays = 365, OmenSubject = kMoonSubject;
			if (!SawEclipse) AddEvent("The sun goes dark at midday as the moon passes over it: the first eclipse anyone wonders at");
			SawEclipse = true;
		}
		else if (Sky.Chance(PerDay / 20.0))
		{
			OmenDays = 365, OmenSubject = kStarsSubject;
			if (!SawComet) AddEvent("A great comet hangs in the night sky for weeks: the first anyone wonders at");
			SawComet = true;
		}
	}

	int SeenBlast = -1; // Eco->LastBlast().Count already dealt with; not saved, so a reload skips old blasts.

	// Everyone near an eruption or an impact dies in the blast.
	void SurviveBlast()
	{
		const auto& B = Eco->LastBlast();
		if (SeenBlast >= 0 && B.Count != SeenBlast)
			for (Npc& P : Npcs)
				if (P.Alive && DistanceKm(P.Lat, P.Lon, B.Lat, B.Lon) < B.RadiusKm) Die(P, 4);
		SeenBlast = B.Count;
	}

	// Gatherable plant food people have used up, per quarter-degree square.
	std::unordered_map<int64_t, double> Gathered;

	static constexpr double kPi = 3.14159265358979323846;
	static constexpr double kKmPerDegree = 111.0;

	// ---- Helpers ----------------------------------------------------------------------------

	static std::string Num(double V)
	{
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%.4g", V);
		return Buf;
	}

	static std::string Matrix(const float* Data, int RowCount, int ColCount)
	{
		std::string J = "[";
		for (int r = 0; r < RowCount; ++r)
		{
			if (RowCount > 1) J += r ? ",[" : "[";
			for (int c = 0; c < ColCount; ++c)
			{
				J += (c ? "," : "") + Num(Data[r * ColCount + c]);
			}
			if (RowCount > 1) J += "]";
		}
		return J + "]";
	}

	void AddEvent(const std::string& Text) { Events.push_back({Eco->Years(), Text}); }

	// Chance of something with daily chance P happening at least once in Days days.
	static double OverDays(double P, int Days)
	{
		if (Days == 1)
		{
			return P;
		}
		const double X = P * Days;
		// For rare things the first terms are exact enough, and much cheaper than pow().
		return X < 0.05 ? X - 0.5 * X * X * (1.0 - 1.0 / Days) : 1.0 - std::pow(1.0 - P, Days);
	}

	// False if everyone is gone and no new band comes.
	bool RestartIfGone()
	{
		if (Settings.OtherHumans && !OthersStarted && !Npcs.empty()) StartOthers();
		else if (OthersStarted) BringBackOthers();
		if (std::any_of(Npcs.begin(), Npcs.end(), [](const Npc& P) { return P.Kind == 0; }))
		{
			return true;
		}
		if (!Settings.NewBandWhenGone)
		{
			return !Npcs.empty();
		}
		StartBand();
		AddEvent("A new band of " + std::to_string(Settings.StartCount) + " arrives in East Africa, born with the instincts of the most successful of those before (band " +
			std::to_string(Arrivals) + ")");
		return true;
	}

	static int64_t KeyOf(double Lat, double Lon)
	{
		const int64_t R = static_cast<int64_t>(std::floor((Lat + 90.0) * 4.0));
		int64_t C = static_cast<int64_t>(std::floor((Lon + 180.0) * 4.0));
		C = ((C % 1440) + 1440) % 1440;
		return R * 1440 + C;
	}

	static double DistanceKm(double LatA, double LonA, double LatB, double LonB)
	{
		const double DLat = (LatB - LatA) * kKmPerDegree;
		double DLon = LonB - LonA;
		if (DLon > 180.0) DLon -= 360.0;
		if (DLon < -180.0) DLon += 360.0;
		DLon *= kKmPerDegree * std::cos((LatA + LatB) * 0.5 * kPi / 180.0);
		return std::sqrt(DLat * DLat + DLon * DLon);
	}

	// The point Km away from (Lat, Lon) in direction Angle (0 = east, pi/2 = north).
	static void Offset(double Lat, double Lon, double Angle, double Km, double& OutLat, double& OutLon)
	{
		const double CosLat = std::max(0.1, std::cos(Lat * kPi / 180.0));
		OutLat = Lat + std::sin(Angle) * Km / kKmPerDegree;
		OutLon = Lon + std::cos(Angle) * Km / (kKmPerDegree * CosLat);
		if (OutLon > 180.0) OutLon -= 360.0;
		if (OutLon < -180.0) OutLon += 360.0;
	}

	bool Livable(double Lat, double Lon, bool Fisher = false) const
	{
		// Deep desert with no river or spring is no place to live, only to cross in a wet spell,
		// except on the shore for a band that knows its springs.
		return Lat < 80.0 && Lat > -60.0 && Eco->IsLand(Lat, Lon) && Eco->AltitudeAt(Lat, Lon) < 4000.0 &&
			(Eco->WaterAt(Lat, Lon) >= 0.1 || (Fisher && ShoreFood(Lat, Lon) > 0.0));
	}

	// How much room a kind's land gives it. In the 5,000 years before another kind's real end date
	// its land runs short, so it dies out close to when the real ones did.
	// shortcut: a timed decline, not competition or climate; replace if those are ever modelled well enough to do it.
	float NumbersNow(uint8_t Kind) const
	{
		const HomininKind& K = OtherKinds()[Kind];
		if (Kind == 0) return K.Numbers;
		const double Past = K.LastYearsAgo + 5000.0 - Eco->YearsAgo();
		return K.Numbers * static_cast<float>(std::clamp(1.0 - Past / 5000.0, 0.0, 1.0));
	}

	// Livable, and within where the band's kind of human could live.
	bool LivableFor(uint8_t Kind, double Lat, double Lon, bool Fisher = false) const
	{
		const std::vector<RangeBox>& R = OtherKinds()[Kind].Range;
		bool In = R.empty();
		for (const RangeBox& B : R) In = In || B.Contains(Lat, Lon);
		return In && Livable(Lat, Lon, Fisher);
	}

	// Runs Body(i) for i in [0, N) on all cores when the pool is up (Step), else in turn (StepDay).
	void InParallel(size_t N, const std::function<void(size_t)>& Body)
	{
		if (Pool) Pool->For(N, Body);
		else
			for (size_t i = 0; i < N; ++i) Body(i);
	}

	int CampSlot(int Id) const
	{
		auto It = CampSlots.find(Id);
		return It == CampSlots.end() ? 0 : It->second;
	}

	// ---- Inborn wiring ----------------------------------------------------------------------

	// The starting instincts: eat when hungry, drink when thirsty, rest when tired, hide from
	// danger, seek company, and a weak urge to chase game. Nothing about where things are, how
	// to hunt well, sharing or exploring: that has to be learned or evolved.
	static Genome InstinctGenome()
	{
		Genome G;
		// Each of the 12 hidden neurons starts out mostly watching one input.
		for (int h = 0; h < kBrainHidden; ++h)
		{
			G.W1[h * kBrainInputs + h] = 3.0f;
			G.B1[h] = -1.5f;
		}
		auto Link = [&G](int Hidden, Action A, float W) { G.W2[Hidden * kBrainActions + static_cast<int>(A)] = W; };
		Link(0, Action::Forage, 1.5f);
		Link(1, Action::Drink, 3.0f);
		Link(2, Action::Rest, 1.5f);
		Link(3, Action::Hide, 2.0f);
		Link(4, Action::Socialize, 1.0f);
		Link(7, Action::Hunt, 0.8f); // Early humans already chased game; how well is up to them.
		G.B2[static_cast<int>(Action::Forage)] = 0.5f;
		G.B2[static_cast<int>(Action::Hunt)] = -0.5f;
		G.B2[static_cast<int>(Action::ShareFood)] = -1.0f;
		G.B2[static_cast<int>(Action::Explore)] = -0.5f;
		return G;
	}

	Genome Mutate(const Genome& From, float Amount)
	{
		Genome G = From;
		auto Nudge = [&](float& W)
		{
			if (Dice.Chance(0.1))
			{
				W += static_cast<float>(Dice.Gaussian()) * Amount;
			}
		};
		for (float& W : G.W1) Nudge(W);
		for (float& W : G.B1) Nudge(W);
		for (float& W : G.W2) Nudge(W);
		for (float& W : G.B2) Nudge(W);
		// Now and then a child is born with a neuron more, or one fewer.
		if (Dice.Chance(0.02))
		{
			G.AddNeuron(Dice);
		}
		else if (G.Hidden() > 4 && Dice.Chance(0.01))
		{
			G.RemoveNeuron(static_cast<int>(Dice.Next() % static_cast<uint64_t>(G.Hidden())));
		}
		G.LearningRate = std::clamp(G.LearningRate * static_cast<float>(std::exp(Dice.Gaussian() * 0.1)), 0.001f, 0.5f);
		G.Curiosity = std::clamp(G.Curiosity * static_cast<float>(std::exp(Dice.Gaussian() * 0.1)), 0.1f, 2.0f);
		G.Imitation = std::clamp(G.Imitation * static_cast<float>(std::exp(Dice.Gaussian() * 0.1)), 0.0f, 0.5f);
		G.Kindness = std::clamp(G.Kindness + static_cast<float>(Dice.Gaussian() * 0.05), 0.05f, 0.95f);
		return G;
	}

	// The child's brain is as big as one parent's; each weight comes from one parent or the
	// other where both have that neuron, then a few mutate.
	Genome Cross(const Genome& Mum, const Genome& Dad)
	{
		const bool LikeMum = Dice.Chance(0.5);
		Genome G = LikeMum ? Mum : Dad;
		const Genome& Rest = LikeMum ? Dad : Mum;
		const size_t Shared = static_cast<size_t>(std::min(G.Hidden(), Rest.Hidden()));
		for (size_t k = 0; k < Shared * kBrainInputs; ++k) if (Dice.Chance(0.5)) G.W1[k] = Rest.W1[k];
		for (size_t k = 0; k < Shared; ++k) if (Dice.Chance(0.5)) G.B1[k] = Rest.B1[k];
		for (size_t k = 0; k < Shared * kBrainActions; ++k) if (Dice.Chance(0.5)) G.W2[k] = Rest.W2[k];
		for (size_t k = 0; k < G.B2.size(); ++k) if (Dice.Chance(0.5)) G.B2[k] = Rest.B2[k];
		if (Dice.Chance(0.5)) G.LearningRate = Rest.LearningRate;
		if (Dice.Chance(0.5)) G.Curiosity = Rest.Curiosity;
		if (Dice.Chance(0.5)) G.Imitation = Rest.Imitation;
		if (Dice.Chance(0.5)) G.Kindness = Rest.Kindness;
		return Mutate(G, 0.15f);
	}

	// ---- Places -----------------------------------------------------------------------------

	void RegrowGatherable(int Days = 1)
	{
		const double Left = Days == 1 ? 0.97 : std::pow(0.97, Days);
		for (auto It = Gathered.begin(); It != Gathered.end();)
		{
			It->second *= Left;
			It = It->second < 1.0 ? Gathered.erase(It) : std::next(It);
		}
	}

	// Plant food left at a spot, plus shellfish and fish on a shore for a band that fishes.
	double GatherableAt(double Lat, double Lon, bool Shore = false) const
	{
		double Full = (Eco->PlantCover(Lat, Lon) + (Shore ? ShoreFood(Lat, Lon) : 0.0)) * Settings.GatherableKg;
		if (auto W = Worn.find(KeyOf(Lat, Lon)); W != Worn.end()) Full *= 1.0 - W->second;
		auto It = Gathered.find(KeyOf(Lat, Lon));
		return std::max(0.0, Full - (It == Gathered.end() ? 0.0 : It->second));
	}

	// The shore counts as this much plant cover (shellfish and fish were the food that let people live along dry coasts).
	// shortcut: a whole one-degree square counts as shore if any part of it is sea; use a finer coast mask if coasts matter more.
	static constexpr double kShoreCover = 0.3;
	static constexpr int kFishing = 17;
	double ShoreFood(double Lat, double Lon) const
	{
		if (ShoreSquare.empty()) return 0.0;
		// The lowest tides, at new and full moon, bare the most shellfish.
		const double Tide = 1.0 + 0.15 * std::cos(2.0 * sky::kTau * sky::MoonAge(Eco->Day()) / sky::kLunarMonth);
		return ShoreSquare[static_cast<size_t>(GridKey(std::clamp(GridRow(Lat), 0, 179), GridCol(Lon)))] ? kShoreCover * Tide : 0.0;
	}
	static bool Fishes(const Camp& C) { return Knows(C.Known, kFishing); }
	// Bands that live off the sea also know its shore springs, where fresh groundwater seeps out even on desert coasts.
	// shortcut: every shore square has springs for a fishing band; use real spring sites if coasts need to be finer.
	static constexpr double kShoreWater = 0.5;
	double WaterFor(const Camp& C, double Lat, double Lon) const
	{
		const double W = Eco->WaterAt(Lat, Lon);
		return Fishes(C) && ShoreFood(Lat, Lon) > 0.0 ? std::max(W, kShoreWater) : W;
	}

	static int64_t GridKey(int Row, int Col) { return static_cast<int64_t>(Row) * 360 + ((Col % 360) + 360) % 360; }
	static int GridRow(double Lat) { return static_cast<int>(std::floor(Lat + 90.0)); }
	static int GridCol(double Lon) { return static_cast<int>(std::floor(Lon + 180.0)); }

	void GridAdd(int Slot)
	{
		CampGrid[GridKey(GridRow(Camps[Slot].Lat), GridCol(Camps[Slot].Lon))].push_back(Slot);
	}

	void GridRemove(int Slot)
	{
		std::vector<int>& List = CampGrid[GridKey(GridRow(Camps[Slot].Lat), GridCol(Camps[Slot].Lon))];
		List.erase(std::remove(List.begin(), List.end(), Slot), List.end());
	}

	// Other camps within Km of a point, not counting camp SelfId.
	// Counts other camps within Km, stopping once Limit are found.
	int CampsNear(double Lat, double Lon, double Km, int SelfId, int Limit = 1 << 30) const
	{
		const int Rows = static_cast<int>(std::ceil(Km / kKmPerDegree));
		const int Cols = std::min(180, static_cast<int>(std::ceil(Km / (kKmPerDegree * std::max(0.05, std::cos(Lat * kPi / 180.0))))));
		const int R0 = GridRow(Lat);
		const int C0 = GridCol(Lon);
		int Count = 0;
		for (int r = R0 - Rows; r <= R0 + Rows; ++r)
		{
			for (int c = C0 - Cols; c <= C0 + Cols; ++c)
			{
				auto It = CampGrid.find(GridKey(r, c));
				if (It == CampGrid.end())
				{
					continue;
				}
				for (int Slot : It->second)
				{
					const Camp& C = Camps[Slot];
					Count += C.Id != SelfId && DistanceKm(Lat, Lon, C.Lat, C.Lon) < Km;
					if (Count >= Limit) return Count;
				}
			}
		}
		return Count;
	}

	void MeasureLand()
	{
		SquareCapacity.assign(180 * 360, 0.0f);
		ShoreSquare.assign(180 * 360, 0);
		ShoreCapacity.assign(180 * 360, 0.0f);
		for (int r = 0; r < 180; ++r)
		{
			for (int c = 0; c < 360; ++c)
			{
				double Cover = 0.0;
				int Lands = 0;
				int Shores = 0; // Land a fishing band can live on, desert shore included.
				int Seas = 0;
				for (int k = 0; k < 16; ++k)
				{
					const double Lat = r - 90.0 + (k / 4 + 0.5) / 4.0;
					const double Lon = c - 180.0 + (k % 4 + 0.5) / 4.0;
					const bool Ok = Livable(Lat, Lon);
					Cover += Ok ? Eco->BestCover(Lat, Lon) / 16.0 : 0.0;
					Lands += Ok;
					Shores += Ok || (Eco->IsLand(Lat, Lon) && Eco->AltitudeAt(Lat, Lon) < 4000.0 && Lat < 80.0 && Lat > -60.0);
					Seas += !Eco->IsLand(Lat, Lon);
				}
				const double AreaKm2 = kKmPerDegree * kKmPerDegree * std::cos((r - 89.5) * kPi / 180.0);
				const size_t Sq = static_cast<size_t>(r) * 360 + c;
				SquareCapacity[Sq] = static_cast<float>(Cover * AreaKm2 * Settings.PeoplePerKm2);
				ShoreSquare[Sq] = Shores > 0 && Seas > 0;
				// Shellfish beds and fish feed far more people per km of coast than the land behind it does, as
				// many as the best grassland over the whole square, which lets a band string out along a dry shore.
				ShoreCapacity[Sq] = ShoreSquare[Sq] ? static_cast<float>(AreaKm2 * Settings.PeoplePerKm2) : 0.0f;
			}
		}
	}

	// How much of what the land around a camp can feed is still free: its own square and two rings around it.
	void MeasureRoom()
	{
		// Ice ages change how much land there is and how much it grows.
		if (SquareCapacity.empty() || LandClimate != Eco->ClimateVersion())
		{
			LandClimate = Eco->ClimateVersion();
			MeasureLand();
		}
		InParallel(Camps.size(), [&](size_t Here)
		{
			Camp& C = Camps[Here];
			const int R0 = GridRow(C.Lat);
			const int C0 = GridCol(C.Lon);
			double Capacity = 0.0;
			double Living = 0.0;
			for (int r = std::max(0, R0 - 2); r <= std::min(179, R0 + 2); ++r)
			{
				for (int c = C0 - 2; c <= C0 + 2; ++c)
				{
					const int Col = ((c % 360) + 360) % 360;
					Capacity += SquareCapacity[static_cast<size_t>(r) * 360 + Col] + (Fishes(C) ? ShoreCapacity[static_cast<size_t>(r) * 360 + Col] : 0.0f);
					auto It = CampGrid.find(GridKey(r, Col));
					if (It != CampGrid.end())
					{
						for (int Slot : It->second)
						{
							Living += static_cast<double>(Camps[Slot].Members.size());
						}
					}
				}
			}
			Capacity *= C.Effects.Land * NumbersNow(C.Kind);
			C.Room = Capacity > 0.0 ? static_cast<float>(std::clamp(1.0 - Living / Capacity, 0.0, 1.0)) : 0.0f;
		});
	}

	// Reads what each camp can see and knows today.
	void SenseCamps(int Days)
	{
		// Each camp only writes its own fields here, so camps are worked out on all cores.
		InParallel(Camps.size(), [&](size_t Here)
		{
			Camp& C = Camps[Here];
			C.Known = 0;
			for (int i : C.Members)
			{
				C.Known |= Npcs[i].Skills;
			}
			C.Effects = EffectsOf(C.Known);
			AddInventionEffects(C.Inventions, C.Known, C.Effects);
			AddBeliefEffects(C);
		});
		WeighReputations();
		RememberPlaces(Days);
		InParallel(Camps.size(), [&](size_t Here)
		{
			Camp& C = Camps[Here];
			C.Nearby = 0;
			C.Neighbour = 0;
			for (int r = GridRow(C.Lat) - 1; r <= GridRow(C.Lat) + 1; ++r)
			{
				for (int c = GridCol(C.Lon) - 1; c <= GridCol(C.Lon) + 1; ++c)
				{
					auto It = CampGrid.find(GridKey(r, c));
					if (It == CampGrid.end()) continue;
					for (int Slot : It->second)
					{
						const Camp& O = Camps[Slot];
						if (O.Id == C.Id || DistanceKm(C.Lat, C.Lon, O.Lat, O.Lon) >= 100.0) continue;
						C.Neighbour = O.Id;
						if (Understand(C, O)) C.Nearby |= O.Known;
					}
				}
			}
			// What anyone in its state knows travels along its roads and through its rulers.
			if (const Polity* S = C.State ? StateById(C.State) : nullptr) C.Nearby |= S->Known | S->Heard;
		});
		MeasureRoom();
		InParallel(Camps.size(), [&](size_t Here)
		{
			Camp& C = Camps[Here];
			C.SeenDanger = static_cast<float>(std::clamp(Eco->PredatorsNear(C.Lat, C.Lon) / 40.0, 0.0, 1.0));
			C.SeenFood = static_cast<float>(std::clamp(GatherableAt(C.Lat, C.Lon, Fishes(C)) / Settings.GatherableKg, 0.0, 1.0));
			C.SeenGame = static_cast<float>(std::clamp(Eco->GameNear(C.Lat, C.Lon, 1000.0) / 2000.0, 0.0, 1.0));
			C.SeenWater = static_cast<float>(WaterFor(C, C.Lat, C.Lon));
			C.SeenSeason = static_cast<float>(Eco->GrowingSeason(C.Lat));
		});
	}

	// How good a place looks for camp SelfId: food to gather, water and game, minus danger and
	// minus other camps already there (bands keep their own range, so they spread out).
	double PlaceScore(double Lat, double Lon, const Camp& Self) const
	{
		const int SelfId = Self.Id;
		if (!Livable(Lat, Lon, Fishes(Self)))
		{
			return -1.0;
		}
		const double Crowd = 0.1 * CampsNear(Lat, Lon, 50.0, SelfId, 4);
		const double Food = GatherableAt(Lat, Lon, Fishes(Self)) / Settings.GatherableKg;
		const double Water = WaterFor(Self, Lat, Lon);
		const double Game = std::clamp(Eco->GameNear(Lat, Lon, 1000.0) / 2000.0, 0.0, 1.0);
		const double Danger = std::clamp(Eco->PredatorsNear(Lat, Lon) / 40.0, 0.0, 1.0);
		return 0.5 * Food + 0.3 * std::min(Water, 0.6) + 0.2 * Game - 0.1 * Danger - Crowd;
	}

	// ---- Camps ------------------------------------------------------------------------------

	// Rebuilds each camp's member list, drops empty camps and refreshes everyone's band size.
	void IndexCamps()
	{
		CampSlots.clear();
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			Camps[c].Members.clear();
			CampSlots[Camps[c].Id] = static_cast<int>(c);
		}
		for (size_t i = 0; i < Npcs.size(); ++i)
		{
			Camps[CampSlot(Npcs[i].Camp)].Members.push_back(static_cast<int>(i));
		}
		Camps.erase(std::remove_if(Camps.begin(), Camps.end(), [](const Camp& C) { return C.Members.empty(); }), Camps.end());
		CampSlots.clear();
		CampGrid.clear();
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			CampSlots[Camps[c].Id] = static_cast<int>(c);
			GridAdd(static_cast<int>(c));
			for (int i : Camps[c].Members)
			{
				Npcs[i].BandSize = static_cast<int>(Camps[c].Members.size());
			}
		}
	}

	// A camp stays while food and water hold out, then heads for the best place its scouts
	// have found, or failing that the best-looking place within a day's walk.
	void MoveCamps(int Days = 1)
	{
		// Each band weighs where to go on all cores, seeing where everyone was at the start, with its
		// own dice; then they move in turn.
		struct Plan
		{
			bool Go = false;
			double Lat = 0.0, Lon = 0.0;
			const Npc* Elder = nullptr;
		};
		std::vector<Plan> Plans(Camps.size());
		const uint64_t Today = static_cast<uint64_t>(Eco->Day());
		InParallel(Camps.size(), [&](size_t Slot)
		{
			Camp& C = Camps[Slot];
			Rng Mine(BaseSeed ^ (Today * 0xD6E8FEB86659FD93ull) ^ (static_cast<uint64_t>(C.Id) * 0x9E3779B97F4A7C15ull));
			C.ScoutScore *= Days == 1 ? 0.995 : std::pow(0.995, Days); // Old news gets less trustworthy.
			C.DaysHere += Days;
			const double Here = PlaceScore(C.Lat, C.Lon, C);
			const double Food = GatherableAt(C.Lat, C.Lon, Fishes(C)) / std::max(1.0, (Eco->PlantCover(C.Lat, C.Lon) + (Fishes(C) ? ShoreFood(C.Lat, C.Lon) : 0.0)) * Settings.GatherableKg);
			const bool Crowded = C.DaysHere > 30 && CampsNear(C.Lat, C.Lon, 30.0, C.Id, 1) > 0;
			const bool Stressed = Food < 0.4 || WaterFor(C, C.Lat, C.Lon) < 0.2 || Crowded || (C.DaysHere > 90 && Mine.Chance(OverDays(0.02, Days)));
			if (!Stressed)
			{
				return;
			}
			double TargetLat = C.Lat;
			double TargetLon = C.Lon;
			const Npc* Elder = nullptr;
			if (WaterFor(C, C.Lat, C.Lon) < 0.3 || Food < 0.4)
			{
				// In hard times the band asks its oldest: where was there water before?
				for (int i : C.Members)
				{
					const Npc& P = Npcs[static_cast<size_t>(i)];
					if (P.RefugeWater > 0.0f && P.AgeYears() >= 25.0 && (!Elder || P.AgeDays > Elder->AgeDays)) Elder = &P;
				}
				if (Elder && (DistanceKm(C.Lat, C.Lon, Elder->RefugeLat, Elder->RefugeLon) > 1500.0 ||
					PlaceScore(Elder->RefugeLat, Elder->RefugeLon, C) < Here + 0.05)) Elder = nullptr;
			}
			if (Elder)
			{
				TargetLat = Elder->RefugeLat;
				TargetLon = Elder->RefugeLon;
			}
			else if (C.ScoutScore > Here + 0.05)
			{
				TargetLat = C.ScoutLat;
				TargetLon = C.ScoutLon;
			}
			else
			{
				double Best = Here;
				for (int d = 0; d < 8; ++d)
				{
					double La = 0.0;
					double Lo = 0.0;
					Offset(C.Lat, C.Lon, (d + Mine.Uniform()) * kPi / 4.0, 25.0, La, Lo);
					const double S = PlaceScore(La, Lo, C);
					if (S > Best)
					{
						Best = S;
						TargetLat = La;
						TargetLon = Lo;
					}
				}
			}
			Plans[Slot] = {true, TargetLat, TargetLon, Elder};
		});
		for (size_t Slot = 0; Slot < Camps.size(); ++Slot)
		{
			if (!Plans[Slot].Go) continue;
			Camp& C = Camps[Slot];
			const double TargetLat = Plans[Slot].Lat, TargetLon = Plans[Slot].Lon;
			if (const Npc* Elder = Plans[Slot].Elder; Elder && !AnyRefuge)
			{
				AnyRefuge = true;
				AddEvent(PersonName(Elder->Name) + ", aged " + std::to_string(static_cast<int>(Elder->AgeYears())) +
					", leads their band in a hard time to a place they remember from years before");
				Remember(*Elder, "led their band in a hard time to a well-watered place they remembered from years before" +
					Say(C, NameWord(*Elder), {C.Words[7], "come"}, {C.Words[0], "water"}));
			}
			const double Km = DistanceKm(C.Lat, C.Lon, TargetLat, TargetLon);
			if (Km < 1.0)
			{
				continue;
			}
			const double Step = std::min(1.0, 25.0 * Days / Km); // A day's walk per day.
			double DLon = TargetLon - C.Lon;
			if (DLon > 180.0) DLon -= 360.0;
			if (DLon < -180.0) DLon += 360.0;
			const double NewLat = C.Lat + (TargetLat - C.Lat) * Step;
			double NewLon = C.Lon + DLon * Step;
			if (NewLon > 180.0) NewLon -= 360.0;
			if (NewLon < -180.0) NewLon += 360.0;
			if (LivableFor(C.Kind, NewLat, NewLon, Fishes(C)))
			{
				GridRemove(static_cast<int>(Slot));
				C.Lat = NewLat;
				C.Lon = NewLon;
				GridAdd(static_cast<int>(Slot));
				C.DaysHere = 0;
				if (Step >= 1.0)
				{
					C.ScoutScore = -1.0; // Arrived.
				}
			}
			else
			{
				C.ScoutScore = -1.0; // Can't get there from here.
			}
		}
	}

	void SplitAndMergeCamps(int Days = 1)
	{
		const size_t Existing = Camps.size();
		for (size_t c = 0; c < Existing; ++c)
		{
			// Big camps split, and so do smaller ones once the land around can't feed more; then the
			// ones who leave go far, looking for land of their own.
			const int Size = static_cast<int>(Camps[c].Members.size());
			// Crowded bands split from 30: real foraging bands rarely fall below about 25, and smaller ones
			// keep losing what they know when the few who knew it die.
			const bool Full = Size >= 30 && Camps[c].Room < 0.2f;
			// Bands held together by shared beliefs grow bigger before they split.
			const int SplitAt = static_cast<int>(Settings.SplitSize * (1.0f + 0.5f * Camps[c].Cohesion));
			if ((Size < SplitAt && !Full) || !Dice.Chance(OverDays(0.01, Days)))
			{
				continue;
			}
			// About half leave, families together: children go where their mothers go.
			Camp New;
			New.Id = NextCampId++;
			New.Words = Camps[c].Words;
			New.Kind = Camps[c].Kind;
			New.Met = Camps[c].Met;
			New.Inventions = Camps[c].Inventions;
			New.Lexicon = Camps[c].Lexicon;
			// The ones who leave don't always carry every belief with them.
			for (uint32_t F : Camps[c].Faith) if (Dice.Chance(0.85)) New.Faith.push_back(F);
			New.State = Camps[c].State; // A daughter band stays in its state.
			New.Exposed = Camps[c].Exposed;
			New.Cohesion = Camps[c].Cohesion;
			MaybeSchism(New);
			double Angle = Dice.Range(0.0, 2.0 * kPi);
			if (Camps[c].ScoutScore > 0.0)
			{
				Angle = std::atan2(Camps[c].ScoutLat - Camps[c].Lat, Camps[c].ScoutLon - Camps[c].Lon);
			}
			bool Placed = false;
			for (int Try = 0; Try < 8 && !Placed; ++Try)
			{
				const double Far = Full ? Dice.Range(60.0, 150.0) * Camps[c].Effects.Travel : Dice.Range(30.0, 60.0);
				Offset(Camps[c].Lat, Camps[c].Lon, Angle + Try * 0.8, Far, New.Lat, New.Lon);
				Placed = LivableFor(New.Kind, New.Lat, New.Lon, Fishes(Camps[c]));
			}
			if (!Placed)
			{
				continue;
			}
			std::unordered_map<uint32_t, bool> Leaving;
			for (int i : Camps[c].Members)
			{
				if (!Npcs[i].IsChild())
				{
					Leaving[Npcs[i].Id] = Dice.Chance(0.5);
				}
			}
			for (int i : Camps[c].Members)
			{
				Npc& P = Npcs[i];
				auto It = Leaving.find(P.IsChild() ? P.Mother : P.Id);
				if (It != Leaving.end() && It->second)
				{
					P.Camp = New.Id;
				}
			}
			Camps.push_back(New);
		}
		IndexCamps();

		// Small camps that run into each other join up.
		bool Merged = false;
		std::vector<int> Near;
		for (size_t a = 0; a < Camps.size(); ++a)
		{
			// Only camps in the same or a neighbouring one-degree square can be within 15 km.
			Near.clear();
			if (Camps[a].Members.empty() || Camps[a].Members.size() >= static_cast<size_t>(Settings.SplitSize) * 3 / 4)
			{
				continue;
			}
			for (int r = GridRow(Camps[a].Lat) - 1; r <= GridRow(Camps[a].Lat) + 1; ++r)
			{
				for (int c = GridCol(Camps[a].Lon) - 1; c <= GridCol(Camps[a].Lon) + 1; ++c)
				{
					auto It = CampGrid.find(GridKey(r, c));
					if (It != CampGrid.end())
					{
						for (int Slot : It->second)
						{
							if (Slot > static_cast<int>(a))
							{
								Near.push_back(Slot);
							}
						}
					}
				}
			}
			std::sort(Near.begin(), Near.end());
			for (int Slot : Near)
			{
				const size_t b = static_cast<size_t>(Slot);
				const size_t Together = Camps[a].Members.size() + Camps[b].Members.size();
				if (Camps[a].Members.empty() || Camps[b].Members.empty() || Together > static_cast<size_t>(Settings.SplitSize) * 3 / 4 ||
					DistanceKm(Camps[a].Lat, Camps[a].Lon, Camps[b].Lat, Camps[b].Lon) > 15.0)
				{
					continue;
				}
				// Bands of different kinds of human mostly keep apart; now and then they join.
				if (Camps[a].Kind != Camps[b].Kind)
				{
					NoteMeeting(Camps[a], Camps[b]);
					if (!Dice.Chance(0.05)) continue;
				}
				for (int i : Camps[b].Members)
				{
					Npcs[i].Camp = Camps[a].Id;
					Camps[a].Members.push_back(i);
				}
				Camps[b].Members.clear();
				Camps[a].Met |= Camps[b].Met;
				for (uint32_t Id : Camps[b].Inventions) TakeUp(Camps[a], Id, Camps[b]);
				for (uint32_t Id : Camps[b].Faith)
				{
					if (Camps[a].Faith.size() < kMaxFaith && std::find(Camps[a].Faith.begin(), Camps[a].Faith.end(), Id) == Camps[a].Faith.end()) Camps[a].Faith.push_back(Id);
				}
				Merged = true;
			}
		}
		if (Merged)
		{
			IndexCamps();
		}
	}

	// ---- One NPC's day ----------------------------------------------------------------------

	bool Fertile(const Npc& P) const
	{
		const double Age = P.AgeYears();
		if (P.Female)
		{
			return Age >= 15.0 && Age < 45.0 && P.PregnantDays < 0.0 && Eco->Day() - P.LastBirthDay > 365.0 * 3.0;
		}
		return Age >= 15.0 && Age < 65.0;
	}

	static void UpdateLoneliness(Npc& P)
	{
		P.Loneliness = std::clamp(std::max(1.0f - static_cast<float>(P.BandSize - 1) / 8.0f, P.SocialNeed), 0.0f, 1.0f);
	}

	void Sense(Npc& P)
	{
		const Camp& C = Camps[CampSlot(P.Camp)];
		P.Fear = C.SeenDanger;
		UpdateLoneliness(P);

		std::array<float, kBrainInputs>& In = P.LastInputs;
		In[0] = 1.0f - P.Energy;
		In[1] = 1.0f - P.Water;
		In[2] = P.Fatigue;
		In[3] = P.Fear;
		In[4] = P.Loneliness;
		In[5] = Fertile(P) ? 1.0f : 0.0f;
		In[6] = C.SeenFood;
		In[7] = C.SeenGame;
		In[8] = C.SeenWater;
		In[9] = C.SeenSeason;
		In[10] = static_cast<float>(std::clamp(P.AgeYears() / 70.0, 0.0, 1.0));
		In[11] = P.Health;
		In[12] = std::clamp(static_cast<float>(P.BandSize) / 30.0f, 0.0f, 1.0f);
		In[13] = std::clamp(P.CarriedKg / 10.0f, 0.0f, 1.0f);
		In[14] = P.HuntSkill;
		In[15] = 1.0f;
	}

	static int Decide(Npc& P, Rng& Dice)
	{
		const Genome& G = *P.Genes;
		const int Hidden = G.Hidden();
		for (int h = 0; h < Hidden; ++h)
		{
			float Sum = G.B1[h];
			const float* W = &G.W1[static_cast<size_t>(h) * kBrainInputs];
			for (int i = 0; i < kBrainInputs; ++i)
			{
				Sum += P.LastInputs[i] * W[i];
			}
			P.LastHidden[h] = std::tanh(Sum);
		}
		std::array<float, kBrainActions> Logit{};
		float Top = -1e9f;
		for (int a = 0; a < kBrainActions; ++a)
		{
			float Sum = OutputBias(P, a);
			for (int h = 0; h < Hidden; ++h)
			{
				Sum += P.LastHidden[h] * OutputWeight(P, h, a);
			}
			Logit[a] = Sum / G.Curiosity;
			Top = std::max(Top, Logit[a]);
		}
		float Total = 0.0f;
		for (int a = 0; a < kBrainActions; ++a)
		{
			P.LastChoice[a] = std::exp(Logit[a] - Top);
			Total += P.LastChoice[a];
		}
		// Now and then anyone tries something at random, so no habit is ever completely lost.
		const float Random = 0.04f * G.Curiosity;
		for (float& C : P.LastChoice)
		{
			C = (1.0f - Random) * C / Total + Random / kBrainActions;
		}
		double Pick = Dice.Uniform();
		int Chosen = 0;
		while (Chosen < kBrainActions - 1 && (Pick -= P.LastChoice[Chosen]) > 0.0)
		{
			++Chosen;
		}
		P.LastAction = Chosen;
		P.ActionCounts[Chosen] += 1;
		return Chosen;
	}

	void Act(size_t Index, Camp& C, int Chosen, int Hunters, double& MeatForCamp)
	{
		Npc& P = Npcs[Index];
		switch (static_cast<Action>(Chosen))
		{
		case Action::Forage:
		{
			// Gather in the camp's square or one next to it, whichever has the most left.
			double BestLat = C.Lat;
			double BestLon = C.Lon;
			double Best = GatherableAt(C.Lat, C.Lon, Fishes(C));
			for (int d = 0; d < 4; ++d)
			{
				double La = 0.0;
				double Lo = 0.0;
				Offset(C.Lat, C.Lon, d * kPi / 2.0 + Dice.Range(-0.4, 0.4), 20.0, La, Lo);
				const double Avail = Eco->IsLand(La, Lo) ? GatherableAt(La, Lo, Fishes(C)) * 0.8 : 0.0; // Farther is more work.
				if (Avail > Best)
				{
					Best = Avail;
					BestLat = La;
					BestLon = Lo;
				}
			}
			const double Found = 6.0 * P.ForageSkill * std::min(1.0, Best / 500.0) * P.Own.Forage * Craft(P);
			Gathered[KeyOf(BestLat, BestLon)] += Found;
			P.CarriedKg = static_cast<float>(std::min(10.0 * P.Own.Carry, P.CarriedKg + Found));
			P.ForageSkill = std::min(1.0f, P.ForageSkill + 0.002f);
			P.Fatigue += 0.08f;
			break;
		}
		case Action::Hunt:
		{
			// Bigger hunting parties can take bigger animals and succeed more often.
			const int Party = std::max(1, Hunters);
			const double MaxMass = 250.0 * std::pow(static_cast<double>(Party), 1.5);
			const double Game = std::clamp(Eco->GameNear(C.Lat, C.Lon, MaxMass) / 300.0, 0.0, 1.0);
			const double Success = std::min(0.6, (0.05 + 0.05 * std::min(Party, 8)) * Game * (0.4 + P.HuntSkill) * P.Own.Hunt * Craft(P));
			P.HuntSkill = std::min(1.0f, P.HuntSkill + 0.003f);
			P.Fatigue += 0.15f;
			// Once the camp has all the meat it can eat today, nobody kills more; it would only rot.
			if (MeatForCamp < 4.0 * static_cast<double>(C.Members.size()) && Dice.Chance(Success))
			{
				const int Kind = Eco->KillOneAnimal(C.Lat, C.Lon, MaxMass, Dice);
				if (Kind >= 0)
				{
					MeatForCamp += Eco->GetSpecies()[Kind].BodyMassKg * 0.5;
					P.HuntSkill = std::min(1.0f, P.HuntSkill + 0.02f);
					P.Pleasure += 0.3f; // The thrill of a kill.
					++Kills;
					NoteHunt(Kind, Party, P.Kind);
				}
			}
			if (Dice.Chance(0.0001 * std::min(MaxMass, 2000.0) / 250.0))
			{
				P.Health -= 0.6f; // Gored, kicked or trampled.
			}
			break;
		}
		case Action::Drink:
		{
			// Drink at camp, or walk to the wettest place within reach.
			double Best = WaterFor(C, C.Lat, C.Lon);
			for (int d = 0; d < 6 && Best < 0.5; ++d)
			{
				double La = 0.0;
				double Lo = 0.0;
				Offset(C.Lat, C.Lon, Dice.Range(0.0, 2.0 * kPi), 15.0, La, Lo);
				Best = std::max(Best, Eco->IsLand(La, Lo) ? Eco->WaterAt(La, Lo) : 0.0);
			}
			P.Water = std::min(1.0f, P.Water + static_cast<float>(Best) * 1.5f);
			P.Fatigue += 0.05f;
			break;
		}
		case Action::Rest:
			P.Fatigue -= 0.5f;
			break;
		case Action::ShareFood:
		{
			// Give half of what you carry to whoever in camp needs it most, leaning towards those you
			// trust and passing over anyone who has cheated you.
			int Hungriest = -1;
			float Best = -1e9f;
			for (int j : C.Members)
			{
				if (j == static_cast<int>(Index)) continue;
				const float T = P.TrustIn(Npcs[j].Id);
				const float Score = (1.0f - Npcs[j].Energy) + 0.3f * T;
				if (T > -0.3f && Score > Best)
				{
					Best = Score;
					Hungriest = j;
				}
			}
			if (Hungriest >= 0 && P.CarriedKg > 0.5f)
			{
				const float Give = P.CarriedKg * P.Genes->Kindness;
				P.CarriedKg -= Give;
				Npcs[Hungriest].CarriedKg = std::min(10.0f, Npcs[Hungriest].CarriedKg + Give);
				P.SocialNeed = std::max(0.0f, P.SocialNeed - 0.3f);
				Npcs[Hungriest].Feel(P.Id, 0.2f);
				P.Feel(Npcs[Hungriest].Id, 0.05f);
				if (!AnyShare && P.Kind == 0)
				{
					AnyShare = true;
					AddEvent("First NPC shares food with a hungry camp mate");
				}
			}
			break;
		}
		case Action::Explore:
		{
			// Scout a day's walk away and report back what the land is like.
			P.Heading += static_cast<float>(Dice.Range(-0.6, 0.6));
			double La = 0.0;
			double Lo = 0.0;
			// Rafts and boats let scouts reach land across the water.
			Offset(C.Lat, C.Lon, P.Heading, Dice.Range(30.0, 60.0) * C.Effects.Travel, La, Lo);
			const double Score = PlaceScore(La, Lo, C);
			if (Score < 0.0)
			{
				P.Heading += static_cast<float>(kPi * Dice.Range(0.5, 1.5));
			}
			else
			{
				P.Lat = La;
				P.Lon = Lo;
				P.Fear = std::max(P.Fear, static_cast<float>(std::clamp(Eco->PredatorsNear(La, Lo) / 40.0, 0.0, 1.0)));
				if (Score > C.ScoutScore)
				{
					C.ScoutScore = Score;
					C.ScoutLat = La;
					C.ScoutLon = Lo;
					P.Pleasure += 0.1f; // Finding somewhere good feels good.
				}
				if (P.Kind == 0) NoteRegion(La, Lo, &P);
			}
			P.Fatigue += 0.25f;
			break;
		}
		case Action::Hide:
			P.Fatigue += 0.05f;
			break;
		case Action::Socialize:
			if (P.BandSize > 1)
			{
				P.SocialNeed = 0.0f;
				P.Pleasure += 0.1f; // Company feels good.
				// Time together makes friends.
				const int j = C.Members[Dice.Next() % C.Members.size()];
				if (j != static_cast<int>(Index))
				{
					P.Feel(Npcs[j].Id, 0.03f);
					Npcs[j].Feel(P.Id, 0.03f);
				}
			}
			break;
		}
	}

	// Each camp's hunters over Days days: a party as big as the camp's hunters on an average day,
	// each hunter succeeding as often as in StepDay(). Meat goes to the camp.
	void HuntOverDays(const std::vector<std::array<float, kBrainActions>>& Shares, const std::vector<float>& HuntersIn, std::vector<double>& Meat, int Days)
	{
		std::vector<double> Chance(Camps.size(), 0.0);
		std::vector<double> Expected(Camps.size(), 0.0);
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			if (HuntersIn[c] > 0.01f)
			{
				const int Party = std::max(1, static_cast<int>(std::lround(HuntersIn[c])));
				const double MaxMass = 250.0 * std::pow(static_cast<double>(Party), 1.5);
				const double Game = std::clamp(Eco->GameNear(Camps[c].Lat, Camps[c].Lon, MaxMass) / 300.0, 0.0, 1.0);
				Chance[c] = (0.05 + 0.05 * std::min(Party, 8)) * Game;
			}
		}
		std::vector<double> Share(Npcs.size(), 0.0);
		for (size_t i = 0; i < Npcs.size(); ++i)
		{
			const int Slot = CampSlot(Npcs[i].Camp);
			const float Hunting = Shares[i][static_cast<int>(Action::Hunt)];
			if (Hunting > 0.01f && Chance[Slot] > 0.0)
			{
				Share[i] = std::min(0.6, Chance[Slot] * (0.4 + Npcs[i].HuntSkill) * Npcs[i].Own.Hunt * Craft(Npcs[i])) * Hunting * Days;
				Expected[Slot] += Share[i];
			}
		}
		std::vector<double> Landed(Camps.size(), 0.0);
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			if (Expected[c] <= 0.0)
			{
				continue;
			}
			const int Party = std::max(1, static_cast<int>(std::lround(HuntersIn[c])));
			const int Wanted = static_cast<int>(Expected[c] + Dice.Uniform());
			// Hunters stop once the camp has all the meat it can eat; the rest would only rot.
			const double Enough = 2.0 * 4.0 * Days * static_cast<double>(Camps[c].Members.size());
			const std::vector<int> Killed = Eco->KillAnimals(Camps[c].Lat, Camps[c].Lon, 250.0 * std::pow(static_cast<double>(Party), 1.5), Wanted, Enough, Dice);
			for (size_t k = 0; k < Killed.size(); ++k)
			{
				if (Killed[k] > 0)
				{
					Meat[c] += Eco->GetSpecies()[k].BodyMassKg * 0.5 * Killed[k];
					Landed[c] += Killed[k];
					Kills += Killed[k];
					NoteHunt(static_cast<int>(k), Party, Camps[c].Kind);
				}
			}
		}
		// Each hunter gets the skill and the thrill of their part of the kills.
		for (size_t i = 0; i < Npcs.size(); ++i)
		{
			const int Slot = CampSlot(Npcs[i].Camp);
			if (Share[i] > 0.0)
			{
				const float Mine = static_cast<float>(Share[i] / Expected[Slot] * Landed[Slot]);
				Npcs[i].HuntSkill = std::min(1.0f, Npcs[i].HuntSkill + 0.02f * Mine);
				Npcs[i].Pleasure += 0.3f * Mine / Days;
			}
		}
	}

	// Days days of one NPC's own work, with Shares of the days on each action. Plant food gathered
	// per day goes to DailyFood; hunting is done by HuntOverDays(). Returns, as bits, the actions
	// done now and then that involve others (sharing, scouting, company), for Act() to carry out.
	static uint8_t WorkOverDays(Npc& P, const Camp& C, const std::array<float, kBrainActions>& Shares, float Hunters, int Days, float& DailyFood, Rng& Dice)
	{
		const float Foraging = Shares[static_cast<int>(Action::Forage)];
		if (Foraging > 0.01f)
		{
			DailyFood += static_cast<float>(ForageYield(P, C) * Foraging);
			P.ForageSkill = std::min(1.0f, P.ForageSkill + 0.002f * Days * Foraging);
		}
		const float Hunting = Shares[static_cast<int>(Action::Hunt)];
		if (Hunting > 0.01f)
		{
			// The camp's hunters go out together; see HuntOverDays().
			const double MaxMass = 250.0 * std::pow(static_cast<double>(std::max(1, static_cast<int>(std::lround(Hunters)))), 1.5);
			P.HuntSkill = std::min(1.0f, P.HuntSkill + 0.003f * Days * Hunting);
			if (Dice.Chance(OverDays(0.0001 * std::min(MaxMass, 2000.0) / 250.0 * Hunting, Days)))
			{
				P.Health -= 0.6f; // Gored, kicked or trampled.
			}
		}
		uint8_t Occasional = 0;
		for (Action A : {Action::ShareFood, Action::Explore, Action::Socialize})
		{
			if (Dice.Chance(OverDays(Shares[static_cast<int>(A)], Days)))
			{
				Occasional |= static_cast<uint8_t>(1 << static_cast<int>(A));
			}
		}
		return Occasional;
	}

	// A fertile woman with a fertile man in camp may conceive; more likely if she spent the day with others.
	void MaybeConceive(size_t Index, bool Socialized, int Days = 1) { MaybeConceive(Index, Socialized, Days, Dice); }
	// R: the dice to throw. Writes only her and her mate, who share her camp.
	void MaybeConceive(size_t Index, bool Socialized, int Days, Rng& R)
	{
		Npc& P = Npcs[Index];
		// Fewer children as the land around fills up, so numbers level off gently where food runs short.
		double Room = Camps[CampSlot(P.Camp)].Room;
		if (Settings.MaxPeople > 0)
		{
			Room *= 1.0 - static_cast<double>(Npcs.size()) / Settings.MaxPeople;
		}
		const Camp& Home = Camps[CampSlot(P.Camp)];
		if (const Polity* S = Home.State ? StateById(Home.State) : nullptr) Room *= S->Fertility;
		if (!P.Female || !Fertile(P) || P.Energy < 0.4f || Room <= 0.0 || !R.Chance(OverDays((Socialized ? 0.04 : 0.01) * Room * OtherKinds()[P.Kind].Fertility, Days)))
		{
			return;
		}
		int Mate = -1;
		int Seen = 0;
		double Best = 0.0;
		for (int j : Camps[CampSlot(P.Camp)].Members)
		{
			const Npc& Q = Npcs[j];
			// No brothers or sons (as far as anyone can tell, by mother).
			// She leans towards men she trusts and whom others think well of.
			if (!Q.Female && Fertile(Q) && (P.Mother == 0 || Q.Mother != P.Mother) && Q.Mother != P.Id)
			{
				// Couples tend to stay together: the father of her last child comes first if he's still here.
				const double Score = R.Uniform() + 0.5 * P.TrustIn(Q.Id) + 0.1 * std::min(Q.Reputation, 3.0f) + (Q.Id == P.BabyFather ? 0.7 : 0.0) -
					(Q.Kind != P.Kind ? 0.8 : 0.0); // Seldom someone of another kind of human.
				if (Mate < 0 || Score > Best)
				{
					Best = Score;
					Mate = j;
				}
				++Seen;
			}
		}
		if (Mate < 0)
		{
			return;
		}
		P.Feel(Npcs[Mate].Id, 0.2f);
		Npcs[Mate].Feel(P.Id, 0.2f);
		P.PregnantDays = 0.0;
		P.BabyFather = Npcs[Mate].Id;
		P.BabyFatherGenes = Npcs[Mate].Genes;
		P.BabyFatherShare = Npcs[Mate].OtherShare;
		if (Npcs[Mate].Kind != P.Kind && !AnyMixedChild)
		{
			std::lock_guard<std::mutex> Lock(EventMutex); // Bands work this out side by side in Step.
			if (AnyMixedChild) return;
			AnyMixedChild = true;
			AddEvent(PersonName(P.Name) + ", " + KindName(P.Kind) + ", and " + PersonName(Npcs[Mate].Name) + ", " + KindName(Npcs[Mate].Kind) +
				", have a child together: the first child of two kinds of human");
		}
	}

	// Bands that know dogs or herding tame the wild animals around them, where those live; kept
	// herds walk with their band. Bands that plant sow the crops whose wild plants grow where they
	// are, and pick up their neighbours' crops if those grow there too.
	void KeepHerdsAndCrops(int Days)
	{
		const std::vector<Species>& All = Eco->GetSpecies();
		EverKept.resize(All.size(), false);
		const double TryTame = OverDays(1.0 / 500.0 / 365.0, Days);
		const double Borrow = OverDays(1.0 / 200.0 / 365.0, Days);
		const std::vector<Crop>& CropList = AllCrops();
		for (Camp& C : Camps)
		{
			if (C.Members.empty()) continue;
			for (size_t k = 0; k < All.size(); ++k)
			{
				const Species& K = All[k];
				if (K.TameSkill < 0 || !Knows(C.Known, K.TameSkill) || !Dice.Chance(TryTame)) continue;
				const uint32_t Herder = static_cast<uint32_t>(C.Id);
				if (Eco->Keeps(Herder, static_cast<int>(k)) || !Eco->Tame(static_cast<int>(k), Herder, C.Lat, C.Lon)) continue;
				if (!EverKept[k])
				{
					EverKept[k] = true;
					const Npc& P = Npcs[static_cast<size_t>(C.Members[Dice.Next() % C.Members.size()])];
					AddEvent(K.Companion ? "First dogs: " + PersonName(P.Name) + "'s band raises " + Lower(K.TamedFrom) + " pups at its camp"
										 : "First kept " + Lower(K.Name) + " herd: " + PersonName(P.Name) + "'s band tames " + Lower(K.TamedFrom) + " near its camp");
					Remember(P, K.Companion ? "raised the first dogs from wolf pups" : "was among the first to keep " + Lower(K.Name) + ", tamed from " + Lower(K.TamedFrom));
				}
			}
			if (!Knows(C.Known, 25)) continue; // Planting.
			uint16_t Add = 0;
			for (size_t k = 0; k < CropList.size(); ++k)
			{
				if (CropList[k].Wild.Contains(C.Lat, C.Lon)) Add |= static_cast<uint16_t>(1u << k);
			}
			if (C.Neighbour != 0 && CampSlots.count(C.Neighbour) && Dice.Chance(Borrow))
			{
				const uint16_t Theirs = Camps[static_cast<size_t>(CampSlot(C.Neighbour))].Crops;
				for (size_t k = 0; k < CropList.size(); ++k)
				{
					if ((Theirs >> k & 1u) && CropList[k].Grows(C.Lat)) Add |= static_cast<uint16_t>(1u << k);
				}
			}
			const uint16_t New = static_cast<uint16_t>(Add & ~C.Crops);
			C.Crops |= Add;
			for (size_t k = 0; k < CropList.size(); ++k)
			{
				if (!(New >> k & 1u) || (EverSown >> k & 1u)) continue;
				EverSown |= static_cast<uint16_t>(1u << k);
				const Npc& P = Npcs[static_cast<size_t>(C.Members[Dice.Next() % C.Members.size()])];
				AddEvent("First " + std::string(CropList[k].Name) + " sown, by " + PersonName(P.Name) + "'s band, from the wild plants around its camp");
				Remember(P, "was among the first to sow " + std::string(CropList[k].Name));
			}
		}
		// Herds follow their band; a merged band's herds pass to the nearest band, else they go wild.
		Eco->FollowHerders([&](uint32_t Herder, double& Lat, double& Lon, double& Keepers) -> uint32_t
		{
			auto It = CampSlots.find(static_cast<int>(Herder));
			const Camp* Best = It == CampSlots.end() || Camps[static_cast<size_t>(It->second)].Members.empty() ? nullptr : &Camps[static_cast<size_t>(It->second)];
			for (size_t c = 0; !Best && c < Camps.size(); ++c)
			{
				const Camp& O = Camps[c];
				if (!O.Members.empty() && std::abs(O.Lat - Lat) < 1.0 && std::abs(O.Lon - Lon) < 1.0) Best = &O;
			}
			if (!Best) return 0;
			Lat = Best->Lat;
			Lon = Best->Lon;
			Keepers = static_cast<double>(Best->Members.size());
			return static_cast<uint32_t>(Best->Id);
		});
	}

	// The firsts are the NPCs' own: hunts by the other kinds of human don't count.
	void NoteHunt(int Kind, int Party, uint8_t HunterKind)
	{
		if (HunterKind != 0) return;
		SpeciesHunted.resize(Eco->GetSpecies().size(), false); // Grows when a newer build adds kinds.
		if (SpeciesHunted[Kind] || Eco->GetSpecies()[Kind].TameSkill >= 0) // Killing a kept animal isn't a hunt.
		{
			return;
		}
		SpeciesHunted[Kind] = true;
		const std::string& Name = Eco->GetSpecies()[Kind].Name;
		AddEvent(AnyHunt ? "First " + Name + " hunted (party of " + std::to_string(Party) + ")"
						 : "First successful hunt: a " + Name + ", by a party of " + std::to_string(Party));
		AnyHunt = true;
	}

	// A young child stays with its mother, or another woman of the camp if she has died.
	int FindCarer(size_t Index) const
	{
		const Npc& Child = Npcs[Index];
		int Carer = -1;
		for (int j : Camps[CampSlot(Child.Camp)].Members)
		{
			const Npc& Q = Npcs[j];
			if (Q.Id == Child.Mother)
			{
				return j;
			}
			if (Carer < 0 && Q.Female && Q.AgeYears() >= 15.0 && Child.AgeYears() >= 1.0)
			{
				Carer = j;
			}
		}
		return Carer;
	}

	// A young child's father and its mother's mother, if alive and in the same camp (-1 if not).
	std::array<int, 2> KinOf(const Npc& Child, const Npc& Mum) const
	{
		std::array<int, 2> Kin = {-1, -1};
		const uint32_t Ids[2] = {Child.Father, Mum.Mother};
		for (int k = 0; k < 2; ++k)
		{
			const Npc* Q = Ids[k] ? Find(Ids[k]) : nullptr;
			if (Q && Q->Camp == Child.Camp && !Q->IsChild()) Kin[k] = static_cast<int>(Q - Npcs.data());
		}
		return Kin;
	}

	// Children play with others their age and grow up as friends.
	void Play(Npc& Child, int Days)
	{
		const std::vector<int>& M = Camps[CampSlot(Child.Camp)].Members;
		Npc& Other = Npcs[static_cast<size_t>(M[Dice.Next() % M.size()])];
		if (&Other != &Child && Other.IsChild())
		{
			const float D = static_cast<float>(0.02 * Days);
			Child.Feel(Other.Id, D);
			Other.Feel(Child.Id, D);
		}
	}

	void BeCaredFor(size_t Index)
	{
		Npc& Child = Npcs[Index];
		const int Carer = FindCarer(Index);
		if (Carer < 0)
		{
			return;
		}
		Npc& Mum = Npcs[Carer];
		Child.Water = std::max(Child.Water, Mum.Water);
		Child.Fatigue = 0.1f; // Carried, and sleeps when tired.
		Child.LastAction = Mum.LastAction;
		// The whole camp feeds its children: the carer first, then anyone with food to spare, each
		// keeping a day's meal for themselves. Only then does the carer give from her own reserves,
		// and only while she is well fed, so mothers don't starve feeding their young.
		float Wanted = std::min(1.5f, (1.0f - Child.Energy) * 5.0f);
		auto Give = [&](Npc& Q)
		{
			const float Spare = std::min(Wanted, std::max(0.0f, Q.CarriedKg - 3.0f));
			Q.CarriedKg -= Spare;
			Child.Energy += Spare * 0.2f;
			Wanted -= Spare;
		};
		Give(Mum);
		// Then the child's father and grandmother, who come to be loved for it, then anyone.
		const std::array<int, 2> Kin = KinOf(Child, Mum);
		for (int k : Kin)
		{
			if (k < 0 || k == Carer || Wanted <= 0.0f) continue;
			const float Before = Wanted;
			Give(Npcs[static_cast<size_t>(k)]);
			if (Wanted < Before) Child.Feel(Npcs[static_cast<size_t>(k)].Id, 0.01f);
		}
		for (int j : Camps[CampSlot(Child.Camp)].Members)
		{
			if (Wanted <= 0.0f)
			{
				break;
			}
			if (j != Carer && j != Kin[0] && j != Kin[1] && !Npcs[j].IsChild())
			{
				Give(Npcs[j]);
			}
		}
		if (Dice.Chance(0.05)) Play(Child, 1);
		const float FromMum = std::min(1.0f - Child.Energy, std::max(0.0f, Mum.Energy - 0.5f) * 0.5f);
		Child.Energy += FromMum;
		Mum.Energy -= FromMum * 0.5f;
		// Children pick up their carer's habits.
		Blend(Child, Mum, 0.02f);
	}

	void LiveThroughDay(Npc& P, int Chosen)
	{
		// Eat from what is carried, a day's worth at most.
		const float Hungry = 1.0f - P.Energy;
		const float Eat = std::min({P.CarriedKg, 3.0f, Hungry * 5.0f});
		P.CarriedKg = (P.CarriedKg - Eat) * 0.85f; // Food spoils.
		P.Energy += Eat * 0.2f;

		if (P.IsChild() && Chosen < 0)
		{
			P.Energy -= 0.15f; // Small bodies need less.
		}
		else
		{
			const bool Busy = Chosen == static_cast<int>(Action::Hunt) || Chosen == static_cast<int>(Action::Explore);
			P.Energy -= Busy ? 0.32f : (Chosen == static_cast<int>(Action::Rest) ? 0.22f : 0.27f);
		}
		P.Energy -= ExtraBurn(P);
		P.Water -= 0.3f;
		if (WaterFor(Camps[CampSlot(P.Camp)], P.Lat, P.Lon) > 0.4)
		{
			P.Water += 0.25f; // Streams and pools along the way.
		}
		P.Fatigue += 0.02f; // A night's sleep is included; this is what builds up over days.
		P.SocialNeed = std::min(1.0f, P.SocialNeed + 0.03f);

		if (P.Energy <= 0.0f) P.Health -= 0.04f;
		if (P.Water <= 0.0f) P.Health -= 0.15f;
		if (P.Fatigue >= 1.0f) P.Health -= 0.01f;
		if (P.Energy > 0.3f && P.Water > 0.3f && P.Fatigue < 0.8f) P.Health += 0.02f;

		P.Energy = std::clamp(P.Energy, 0.0f, 1.0f);
		P.Water = std::clamp(P.Water, 0.0f, 1.0f);
		P.Fatigue = std::clamp(P.Fatigue, 0.0f, 1.0f);
		P.Health = std::clamp(P.Health, 0.0f, 1.0f);
		P.AgeDays += 1.0;
		if (P.PregnantDays >= 0.0)
		{
			P.PregnantDays += 1.0;
			P.Energy -= 0.03f;
		}
	}

	// Over weeks a band ranges over its camp's square and the four around it, so it gathers from all five.
	void FindForage(Camp& C)
	{
		C.ForageSquares[0] = KeyOf(C.Lat, C.Lon);
		double Total = GatherableAt(C.Lat, C.Lon, Fishes(C));
		for (int d = 0; d < 4; ++d)
		{
			double La = 0.0;
			double Lo = 0.0;
			Offset(C.Lat, C.Lon, d * kPi / 2.0, 28.0, La, Lo);
			C.ForageSquares[d + 1] = KeyOf(La, Lo);
			Total += Eco->IsLand(La, Lo) ? GatherableAt(La, Lo, Fishes(C)) * 0.8 : 0.0; // Farther is more work.
		}
		C.ForageKg = static_cast<float>(Total / 5.0);
	}

	void GatherFrom(const Camp& C, double Kg)
	{
		for (int64_t Key : C.ForageSquares)
		{
			Gathered[Key] += Kg / 5.0;
		}
	}

	// Plant food an NPC gathers in a full day at its camp's best spot.
	static double ForageYield(const Npc& P, const Camp& C) { return 6.0 * P.ForageSkill * std::min(1.0, C.ForageKg / 500.0) * P.Own.Forage * Craft(P); }

	// The wettest place within a short walk of camp.
	double BestWaterNear(double Lat, double Lon)
	{
		double Best = Eco->WaterAt(Lat, Lon);
		for (int d = 0; d < 6 && Best < 0.5; ++d)
		{
			double La = 0.0;
			double Lo = 0.0;
			Offset(Lat, Lon, Dice.Range(0.0, 2.0 * kPi), 15.0, La, Lo);
			Best = std::max(Best, Eco->IsLand(La, Lo) ? Eco->WaterAt(La, Lo) : 0.0);
		}
		return Best;
	}

	// Days days of needs for an NPC spending Shares of its days on each action (null for a child
	// being looked after), gathering DailyFood a day and getting Meat a day from the camp. Extra
	// days needed for fetching water and resting earn nothing. If that isn't enough to feed itself
	// and the Dependents it looks after, it spends spare days gathering (ForagePerDay full time),
	// adding what it gathers that way to ExtraFound. Returns the food left over at the end.
	float LiveThroughDays(Npc& P, const std::array<float, kBrainActions>* Shares, float DailyFood, float Meat, float BestWater, int Days,
		float ForagePerDay = 0.0f, int Dependents = 0, float* ExtraFound = nullptr) const
	{
		// Food burned a day, and the effort that builds tiredness, for each action.
		static const float Burns[kBrainActions] = {0.27f, 0.32f, 0.27f, 0.22f, 0.27f, 0.32f, 0.27f, 0.27f};
		static const float Effort[kBrainActions] = {0.08f, 0.15f, 0.05f, 0.0f, 0.0f, 0.25f, 0.05f, 0.0f};
		float Burn = 0.15f; // Small bodies need less.
		float Work = 0.02f;
		float Resting = 0.0f;
		float Drinking = 0.0f;
		if (Shares)
		{
			Burn = 0.0f;
			for (int a = 0; a < kBrainActions; ++a)
			{
				Burn += (*Shares)[a] * Burns[a];
				Work += (*Shares)[a] * Effort[a];
			}
			Resting = (*Shares)[static_cast<int>(Action::Rest)];
			Drinking = (*Shares)[static_cast<int>(Action::Drink)];
		}
		Burn = std::max(0.05f, Burn + ExtraBurn(P));
		if (P.PregnantDays >= 0.0)
		{
			Burn += 0.03f;
		}

		// Water: streams along the way cover some of it; the rest means walking to water.
		const float Loss = WaterFor(Camps[CampSlot(P.Camp)], P.Lat, P.Lon) > 0.4 ? 0.05f : 0.3f;
		const float DrinkShare = std::min(1.0f, Loss / std::max(0.01f, 1.5f * BestWater));
		float ThirstyDays = 0.0f;
		if (DrinkShare < 0.6f)
		{
			P.Water = std::max(P.Water, 0.75f);
		}
		else
		{
			const float Short = Loss * Days * (1.0f - 0.6f / DrinkShare);
			ThirstyDays = std::max(0.0f, (Short - P.Water) / Loss);
			P.Water = std::max(0.0f, P.Water - Short);
		}

		// Effort builds tiredness, which a day's rest takes away. Rest and water trips beyond what the
		// brain already planned for come out of working days.
		const float RestNeeded = Work / (Work + 0.5f);
		P.Fatigue = std::clamp(0.2f + Work - Resting, 0.0f, 1.0f);
		const float Lost = std::max(0.0f, RestNeeded - Resting) + std::max(0.0f, std::min(DrinkShare, 0.6f) - Drinking);

		// Food: eat what is carried and found, up to a day's worth a day.
		float Food = P.CarriedKg + (DailyFood * std::max(0.0f, 1.0f - Lost) + Meat) * Days;
		const float Needed = (Burn / 0.2f + 0.75f * Dependents) * Days + (1.0f - P.Energy) * 5.0f;
		if (Shares && Food < Needed && ForagePerDay > 0.0f)
		{
			// Hunger wins: days not needed for gathering, rest or water go to gathering, even hunting
			// days, since a hunter who comes back empty-handed for weeks goes looking for roots instead.
			const float Busy = (*Shares)[static_cast<int>(Action::Forage)] + std::max(RestNeeded, Resting) + std::max(std::min(DrinkShare, 0.6f), Drinking);
			const float Extra = std::min(std::max(0.0f, 1.0f - Busy), (Needed - Food) / (ForagePerDay * Days));
			Food += Extra * ForagePerDay * Days;
			*ExtraFound += Extra * ForagePerDay * Days;
		}
		const float Eat = std::min({Food, 3.0f * Days, Burn * Days / 0.2f + (1.0f - P.Energy) * 5.0f});
		const float EnergyLeft = P.Energy + Eat * 0.2f - Burn * Days;
		const float HungryDays = EnergyLeft < 0.0f ? -EnergyLeft / Burn : 0.0f;
		P.Energy = std::clamp(EnergyLeft, 0.0f, 1.0f);

		P.Health -= 0.04f * HungryDays + 0.15f * ThirstyDays;
		const float GoodDays = std::max(0.0f, Days - HungryDays - ThirstyDays);
		if (P.Energy > 0.3f && P.Water > 0.3f)
		{
			P.Health += 0.02f * GoodDays;
		}
		P.Health = std::clamp(P.Health, 0.0f, 1.0f);
		P.SocialNeed = std::min(1.0f, P.SocialNeed + 0.03f * Days);
		P.AgeDays += Days;
		if (P.PregnantDays >= 0.0)
		{
			P.PregnantDays += Days;
		}
		return Food - Eat;
	}

	// Planning ahead: bands that know how to dry and smoke food put by what they can't carry, and
	// draw on it when people go hungry, so a good season tides them over a lean one.
	// shortcut: only in multi-day steps (long runs); the daily viewer step keeps no store.
	void StoreFood(const std::vector<int>& Carer, const std::vector<float>& Spare, int Days)
	{
		constexpr int kDrying = 15;
		const float Keep = static_cast<float>(std::pow(0.995, Days)); // Dried food lasts months, not years.
		for (Camp& C : Camps) C.Store *= Keep;
		for (size_t i = 0; i < Npcs.size() && i < Carer.size(); ++i)
		{
			Npc& P = Npcs[i];
			Camp& C = Camps[CampSlot(P.Camp)];
			if (!Knows(C.Known, kDrying)) continue;
			if (Carer[i] < 0) C.Store += std::max(0.0f, Spare[i] * 0.85f - P.CarriedKg) * 0.5f; // Half survives drying.
			C.Store = std::min(C.Store, 200.0f * static_cast<float>(C.Members.size()));
			if (P.Energy < 0.4f && C.Store > 0.0f)
			{
				const float Take = std::min(C.Store, std::max(0.0f, 10.0f * P.Own.Carry - P.CarriedKg));
				C.Store -= Take;
				P.CarriedKg += Take;
				if (Take > 0.0f && !AnyStore)
				{
					AnyStore = true;
					AddEvent(PersonName(P.Name) + " eats from food their band dried and stored in better days: the first planning for a lean season");
				}
			}
		}
	}

	// A young child over Days days: fed from its carer's leftovers, then the camp's, then its carer's own reserves.
	void BeCaredForDays(size_t Index, size_t CarerIndex, std::vector<float>& Spare, int Days)
	{
		Npc& Child = Npcs[Index];
		Npc& Mum = Npcs[CarerIndex];
		Child.Water = std::max(Child.Water, Mum.Water);
		Child.LastAction = Mum.LastAction;
		float Wanted = 0.75f * Days + (1.0f - Child.Energy) * 5.0f;
		float Given = 0.0f;
		auto Give = [&](size_t j)
		{
			const float Take = std::min(Wanted, std::max(0.0f, Spare[j]));
			Spare[j] -= Take;
			Given += Take;
			Wanted -= Take;
		};
		Give(CarerIndex);
		// Then the child's father and grandmother, who come to be loved for it, then anyone.
		const std::array<int, 2> Kin = KinOf(Child, Mum);
		for (int k : Kin)
		{
			if (k < 0 || k == static_cast<int>(CarerIndex) || Wanted <= 0.0f) continue;
			const float Before = Wanted;
			Give(static_cast<size_t>(k));
			if (Wanted < Before) Child.Feel(Npcs[static_cast<size_t>(k)].Id, static_cast<float>(0.01 * Days));
		}
		for (int j : Camps[CampSlot(Child.Camp)].Members)
		{
			if (Wanted <= 0.0f)
			{
				break;
			}
			if (j != static_cast<int>(CarerIndex) && j != Kin[0] && j != Kin[1] && !Npcs[j].IsChild())
			{
				Give(static_cast<size_t>(j));
			}
		}
		Play(Child, Days);
		const float FromMum = std::min(1.0f - Child.Energy, std::max(0.0f, Mum.Energy - 0.5f) * 0.5f);
		Child.Energy += FromMum;
		Mum.Energy -= FromMum * 0.5f;
		Child.CarriedKg = 0.0f;
		LiveThroughDays(Child, nullptr, Given / Days, 0.0f, 1.0f, Days);
		Child.Fatigue = 0.1f;
		Blend(Child, Mum, static_cast<float>(OverDays(0.02, Days)));
	}

	// How quick-witted an NPC is: bigger brains find and learn new skills faster.
	// A bigger brain also pays its owner directly: a little more food from the same work, up to 60% more.
	static double Craft(const Npc& P) { return std::clamp(0.85 + 0.15 * Smarts(P), 0.6, 1.6); }
	static double Smarts(const Npc& P) { return static_cast<double>(P.BrainSize()) / kBrainHidden; }

	// When each skill became findable, read back from the firsts: the latest first discovery of
	// what it builds on (year 0 for what was known from the start); -1 if something is still unknown.
	std::vector<double> FindableFromFirsts() const
	{
		const std::vector<Skill>& List = AllSkills();
		std::vector<double> Found(List.size(), 0.0);
		for (size_t k = 0; k < List.size(); ++k)
		{
			if (!Knows(EverKnown, static_cast<int>(k))) Found[k] = -1.0;
			const std::string Text = "First discovery of " + std::string(List[k].Name);
			for (const auto& F : Events)
				if (F.Text == Text) { Found[k] = F.Year; break; }
		}
		std::vector<double> Out(List.size(), 0.0);
		for (size_t k = 0; k < List.size(); ++k)
			for (int N : List[k].Needs)
				Out[k] = Found[static_cast<size_t>(N)] < 0.0 || Out[k] < 0.0 ? -1.0 : std::max(Out[k], Found[static_cast<size_t>(N)]);
		return Out;
	}

	// Over Days days, each person may hit on a skill they have what it takes for, and picks up
	// skills others in camp know. Young children are too small to learn them.
	void LearnSkills(int Days)
	{
		const std::vector<Skill>& List = AllSkills();
		const int Count = static_cast<int>(List.size());
		// Odds a year for one person of average wits and curiosity: kPacePeople who know what a skill
		// builds on find it in YearsToFind on average. The reference scales with the density setting,
		// so a denser world isn't also a faster one.
		// Beyond that many able people the world finds things no faster: the real pace already counts
		// everyone alive then, and a run that grows past the reference would race ahead of history.
		// Only people able to make the find count, so a skill isn't held back by the many who can't.
		constexpr double kPacePeople = 3000.0;
		const double PaceRef = kPacePeople * Settings.PeoplePerKm2 / 0.0003;
		std::array<int, kMaxSkills> Able{};
		constexpr size_t kBlock = 4096;
		std::vector<std::array<int, kMaxSkills>> BlockAble((Npcs.size() + kBlock - 1) / kBlock);
		InParallel(BlockAble.size(), [&](size_t b)
		{
			for (size_t i = b * kBlock; i < std::min(Npcs.size(), (b + 1) * kBlock); ++i)
				if (Npcs[i].AgeYears() >= 5.0)
					for (int k = 0; k < Count; ++k) BlockAble[b][static_cast<size_t>(k)] += CanDiscover(Npcs[i].Skills, k);
		});
		for (const auto& B : BlockAble)
			for (int k = 0; k < Count; ++k) Able[static_cast<size_t>(k)] += B[static_cast<size_t>(k)];
		// The chance grows with time since a skill became findable (a Weibull wait of shape 3 with
		// mean YearsToFind), so a first find long before its time is rare, and so is one long overdue.
		const double Now = Eco->Years();
		if (Findable.empty()) Findable = FindableFromFirsts(); // A new run, or a save from before they were kept.
		Findable.resize(static_cast<size_t>(Count), -1.0);
		std::array<double, kMaxSkills> TierOdds{};
		for (int k = 0; k < Count; ++k)
		{
			const auto& Needs = List[k].Needs;
			if (Findable[k] < 0.0 && std::all_of(Needs.begin(), Needs.end(), [&](int N) { return Knows(EverKnown, N); })) Findable[k] = Now;
			if (Findable[k] < 0.0) continue;
			// Something once known and lost comes back sooner than it was first found: someone half
			// remembers it, or finds the old work.
			constexpr double kRefindYears = 2000.0;
			const double Wait = Knows(EverKnown, k) && !Knows(KnownNow, k) ? std::min(List[k].YearsToFind, kRefindYears) : List[k].YearsToFind;
			const double Scale = Wait / (0.893 * Settings.DiscoveryPace); // The mean of shape 3 is 0.893 x scale.
			// Long overdue (past 3 scales, almost never reached) the chance stops growing.
			const double T = std::min(3.0, (Now - Findable[k]) / Scale + 1.0 / 365.0);
			TierOdds[k] = 3.0 * T * T / Scale / std::max(PaceRef, static_cast<double>(Able[static_cast<size_t>(k)]));
		}
		// Planting needs a warm, steady climate: in an ice age, seasons swing too hard to bet on a crop.
		constexpr int kPlanting = 25;
		if (Eco->IceCover() > 0.15) TierOdds[kPlanting] = 0.0;
		// Wits and curiosity decide who finds things, not how fast the world does: brains grew
		// across the run, and history's pace already includes what minds could do.
		std::vector<float> Scholar(Camps.size(), 1.0f);
		std::vector<SkillSet> Books(Camps.size(), SkillSet(0));
		for (size_t c = 0; c < Camps.size(); ++c)
			if (Camps[c].State)
			{
				Scholar[c] = static_cast<float>(Scholarship(Camps[c]));
				if (const Polity* S = StateById(Camps[c].State)) Books[c] = PrintedFor(*S);
			}
		double MeanGift = 0.0;
		for (const Npc& P : Npcs) MeanGift += Smarts(P) * Smarts(P) * P.Genes->Curiosity * Scholar[static_cast<size_t>(CampSlot(P.Camp))];
		MeanGift = Npcs.empty() ? 1.0 : std::max(1e-6, MeanGift / static_cast<double>(Npcs.size()));
		// Everyone at once on all cores, each with their own dice so the result doesn't depend on
		// how the work is split; first finds, which touch the whole world's record, are noted after.
		std::vector<int8_t> Found(Npcs.size(), -1);
		const uint64_t Today = static_cast<uint64_t>(Eco->Day());
		InParallel(Npcs.size(), [&](size_t Index)
		{
			Npc& P = Npcs[Index];
			if (P.AgeYears() < 5.0)
			{
				return;
			}
			Rng Mine(BaseSeed ^ (Today * 0xA24BAED4963EE407ull) ^ (static_cast<uint64_t>(P.Id) * 0x9FB21C651E98DF25ull));
			const double Wits = Smarts(P);
			const Camp& C = Camps[CampSlot(P.Camp)];
			// Something new: rarer the deeper it is, likelier for the curious and the clever.
			double Total = 0.0;
			std::array<double, kMaxSkills> Odds{};
			for (int k = 0; k < Count; ++k)
			{
				if (CanDiscover(P.Skills, k))
				{
					Odds[k] = TierOdds[k] * Days / 365.0 * Wits * Wits * P.Genes->Curiosity * Scholar[static_cast<size_t>(CampSlot(P.Camp))] / MeanGift;
					Total += Odds[k];
				}
			}
			double Pick = Mine.Uniform();
			if (Pick < Total)
			{
				for (int k = 0; k < Count; ++k)
				{
					if ((Pick -= Odds[k]) < 0.0)
					{
						P.LearnSkill(k);
						Found[Index] = static_cast<int8_t>(k);
						break;
					}
				}
			}
			// Something others in camp, or in camps nearby, already know, once what it builds on is known.
			const SkillSet Written = Books[static_cast<size_t>(CampSlot(P.Camp))];
			const SkillSet Missing = (C.Known | C.Nearby | Written) & ~P.Skills;
			if (Missing)
			{
				const double InCamp = OverDays(std::min(0.5, 0.002 * C.Effects.Teaching * Wits), Days);
				const double Neighbours = OverDays(std::min(0.5, 0.0002 * C.Effects.Teaching * Wits), Days);
				for (int k = 0; k < Count; ++k)
				{
					if (Knows(Missing, k) && CanDiscover(P.Skills, k) && Mine.Chance(Knows(C.Known, k) || Knows(Written, k) ? InCamp : Neighbours))
					{
						P.LearnSkill(k);
					}
				}
			}
		});
		for (size_t i = 0; i < Npcs.size(); ++i)
			if (Found[i] >= 0) Discover(Npcs[i], Found[i]);
		NoteSkills();
	}


	// ---- Inventions -------------------------------------------------------------------------

	static uint32_t MaterialKey(int M) { return 0x1000000u | static_cast<uint32_t>(M); }
	static uint32_t InventionKey(uint32_t Id) { return 0x2000000u | Id; }
	static uint32_t ConceptKey(int K) { return 0x3000000u | static_cast<uint32_t>(K); }

	// Words a band coins the first time it has something new to tell: its vocabulary grows with its story.
	static const std::vector<const char*>& Concepts()
	{
		static const std::vector<const char*> List = {"make", "lead", "teach", "find", "arrive", "us", "the dead", "the sea", "the cold",
			"the land", "beast", "animal", "the moon", "the stars"};
		return List;
	}
	enum ConceptWord { Make, Lead, Teach, FindIt, Arrive, Us, TheDead, TheSea, TheCold, TheLand, Beast, Animal, TheMoon, TheStars };

	// The band's word for a concept, coined now if they have none.
	uint16_t Coin(Camp& C, int K)
	{
		if (const uint16_t* W = WordFor(C, ConceptKey(K))) return *W;
		const uint16_t W = static_cast<uint16_t>(Dice.Next());
		Name(C, ConceptKey(K), W);
		return W;
	}

	// The band's word for a belief subject: one of its first words where there is one, else coined.
	uint16_t SubjectWord(Camp& C, int Subject)
	{
		static const int Slots[] = {-TheDead, 12, 13, 0, 4, -TheLand, -Beast, -Animal, -TheSea, -TheCold, 6, -TheMoon, -TheStars};
		if (Subject >= SubjectCount())
		{
			const uint16_t* W = WordFor(C, MaterialKey(Subject - SubjectCount()));
			return W ? *W : Coin(C, TheLand);
		}
		const int S = Slots[Subject];
		return S > 0 || Subject == 3 ? C.Words[static_cast<size_t>(S)] : Coin(C, -S);
	}

	// A sentence in the band's own speech, with a word-by-word gloss. Each language's word order
	// (doer-deed-thing, doer-thing-deed or deed-doer-thing) drifts with its words for come and go.
	static std::string Say(const Camp& C, std::pair<uint16_t, std::string> Doer, std::pair<uint16_t, std::string> Deed, std::pair<uint16_t, std::string> Thing)
	{
		const int Order = (C.Words[7] ^ C.Words[8]) % 3;
		const std::pair<uint16_t, std::string>* Parts[3] = {&Doer, &Deed, &Thing};
		if (Order == 1) std::swap(Parts[1], Parts[2]);
		if (Order == 2) std::swap(Parts[0], Parts[1]);
		std::string Said, Gloss;
		for (const auto* P : Parts)
		{
			Said += (Said.empty() ? "" : " ") + SayWord(P->first);
			Gloss += (Gloss.empty() ? "" : " ") + P->second;
		}
		Said[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Said[0])));
		return " (in their words: \"" + Said + "\", " + Gloss + ")";
	}
	static std::pair<uint16_t, std::string> NameWord(const Npc& P) { return {P.Name, PersonName(P.Name)}; }

	static const uint16_t* WordFor(const Camp& C, uint32_t Key)
	{
		auto It = std::lower_bound(C.Lexicon.begin(), C.Lexicon.end(), Key, [](const auto& E, uint32_t K) { return E.first < K; });
		return It != C.Lexicon.end() && It->first == Key ? &It->second : nullptr;
	}
	static void Name(Camp& C, uint32_t Key, uint16_t Word)
	{
		auto It = std::lower_bound(C.Lexicon.begin(), C.Lexicon.end(), Key, [](const auto& E, uint32_t K) { return E.first < K; });
		if (It != C.Lexicon.end() && It->first == Key) return;
		C.Lexicon.insert(It, {Key, Word});
	}
	static bool HasInvention(const Camp& C, uint32_t Id) { return std::binary_search(C.Inventions.begin(), C.Inventions.end(), Id); }

	// A band takes up something another band makes, along with the other band's word for it.
	static void TakeUp(Camp& C, uint32_t Id, const Camp& From)
	{
		if (HasInvention(C, Id)) return;
		C.Inventions.insert(std::upper_bound(C.Inventions.begin(), C.Inventions.end(), Id), Id);
		const uint16_t* W = WordFor(From, InventionKey(Id));
		if (W) Name(C, InventionKey(Id), *W);
		for (int M : {MaterialA(Id), MaterialB(Id)})
		{
			if (M < 0) continue;
			C.Met |= uint64_t(1) << M;
			const uint16_t* MW = WordFor(From, MaterialKey(M));
			if (MW) Name(C, MaterialKey(M), *MW);
		}
	}

	// Whether a material is to be had around a camp.
	bool Present(const Camp& C, int Index) const
	{
		const Material& M = AllMaterials()[static_cast<size_t>(Index)];
		const double AbsLat = std::abs(C.Lat);
		switch (M.Where)
		{
		case Found::Anywhere: return true;
		case Found::Stone: return RegionHas(C.Lat, C.Lon, Index, M.Chance);
		case Found::Places: return InArea(C.Lat, C.Lon, M.Areas);
		case Found::Water: return Eco->WaterAt(C.Lat, C.Lon) > 0.4;
		case Found::Trees: return Eco->BestCover(C.Lat, C.Lon) > 0.3;
		case Found::WetTropics: return AbsLat < 25.0 && Eco->BestCover(C.Lat, C.Lon) > 0.5;
		case Found::Game: return C.SeenGame > 0.05f;
		case Found::ColdGame: return C.SeenGame > 0.05f && AbsLat > 35.0;
		case Found::Cold: return AbsLat > 55.0 - 15.0 * Eco->IceCover();
		case Found::Fire: return Knows(C.Known, 2);
		case Found::Coast:
		{
			for (int d = 0; d < 4; ++d)
			{
				double La = 0.0, Lo = 0.0;
				Offset(C.Lat, C.Lon, d * kPi / 2.0, 40.0, La, Lo);
				if (!Eco->IsLand(La, Lo)) return true;
			}
			return false;
		}
		}
		return false;
	}

	// Bands notice materials around them, now and then try working what they know in a new way,
	// and pick up what their neighbours make.
	void Invent(int Days)
	{
		const int Materials = static_cast<int>(AllMaterials().size());
		const int Workings = static_cast<int>(AllWorkings().size());
		std::vector<double> Knack;
		std::array<int, 64> MetList{};
		for (Camp& C : Camps)
		{
			if (C.Members.empty()) continue;
			for (int m = 0; m < Materials; ++m)
			{
				if (!((C.Met >> m) & 1u) && Present(C, m))
				{
					C.Met |= uint64_t(1) << m;
					Name(C, MaterialKey(m), static_cast<uint16_t>(Dice.Next()));
				}
			}
			int MetCount = 0;
			for (int m = 0; m < Materials; ++m)
			{
				if ((C.Met >> m) & 1u) MetList[static_cast<size_t>(MetCount++)] = m;
			}
			// The curious and the clever try more, and are likelier to be the one who gets it.
			Knack.assign(C.Members.size(), 0.0);
			double Sum = 0.0;
			for (size_t k = 0; k < C.Members.size(); ++k)
			{
				const Npc& P = Npcs[static_cast<size_t>(C.Members[k])];
				if (P.AgeYears() < 12.0) continue;
				const double Wits = Smarts(P);
				Knack[k] = Wits * Wits * P.Genes->Curiosity / 0.6;
				Sum += Knack[k];
			}
			// The first specialists: a big band with food put by can feed its most inventive member to
			// spend their days making tools instead of foraging.
			const double Feed = Days * 1.0; // A kg of stored food a day.
			if (C.Members.size() >= 20 && C.Store > Feed + 2.0f * C.Members.size() && Sum > 0.0)
			{
				const size_t Best = static_cast<size_t>(std::max_element(Knack.begin(), Knack.end()) - Knack.begin());
				Sum += Knack[Best] * 3.0;
				Knack[Best] *= 4.0;
				C.Store -= static_cast<float>(Feed);
				const Npc& Maker = Npcs[static_cast<size_t>(C.Members[Best])];
				if (!AnyToolmaker)
				{
					AnyToolmaker = true;
					AddEvent(PersonName(Maker.Name) + " becomes the first toolmaker: their band feeds them from its stores while they work");
					Remember(Maker, "became the first toolmaker, fed by their band from its stored food so they could spend their days making things");
				}
			}
			const double Tries = Sum * Settings.InventionTriesPerYear * Days / 365.0;
			int N = static_cast<int>(Tries);
			if (Dice.Chance(Tries - N)) ++N;
			for (int t = 0; t < N && MetCount > 0; ++t)
			{
				const int W = static_cast<int>(Dice.Next() % static_cast<uint64_t>(Workings));
				const int A = MetList[Dice.Next() % static_cast<uint64_t>(MetCount)];
				const int B = AllWorkings()[static_cast<size_t>(W)].NeedB < 0 ? -1 : MetList[Dice.Next() % static_cast<uint64_t>(MetCount)];
				const uint32_t Id = InventionId(W, A, B);
				if (InventionStrength(W, A, B, C.Known) <= 0.0f || HasInvention(C, Id)) continue;
				double Pick = Dice.Uniform() * Sum;
				size_t Who = 0;
				for (; Who + 1 < Knack.size(); ++Who)
				{
					if ((Pick -= Knack[Who]) < 0.0) break;
				}
				const Npc& Maker = Npcs[static_cast<size_t>(C.Members[Who])];
				if (const BeliefRecord* No = Forbids(C, Id))
				{
					Shun(C, Id, Maker, *No);
					continue;
				}
				MakeFirst(C, Id, Maker);
			}
			// Neighbours' things are copied more easily when their speech can be followed.
			if (C.Neighbour != 0)
			{
				const Camp& O = Camps[CampSlot(C.Neighbour)];
				const double Copy = OverDays(1.0 / ((Understand(C, O) ? 300.0 : 3000.0) * 365.0), Days);
				if (!O.Inventions.empty() && Dice.Chance(Copy))
				{
					const uint32_t Id = O.Inventions[Dice.Next() % O.Inventions.size()];
					if (InventionStrength(WorkingOf(Id), MaterialA(Id), MaterialB(Id), C.Known) > 0.0f && !Forbids(C, Id)) TakeUp(C, Id, O);
				}
			}
		}
	}



	// ---- Memory of places ----------------------------------------------------------------------

	// Everyone keeps the best-watered place they have lived in mind; the memory fades slowly, and
	// children on growing up take their mother's if it is better than their own.
	void RememberPlaces(int Days)
	{
		const float Fade = static_cast<float>(std::pow(0.9999, Days)); // Places: half forgotten in about 20 years.
		const float Heal = static_cast<float>(std::pow(0.99, Days));   // Grief: half gone in about 70 days.
		for (const Camp& C : Camps)
		{
			const float W = static_cast<float>(Eco->WaterAt(C.Lat, C.Lon));
			for (int i : C.Members)
			{
				Npc& P = Npcs[static_cast<size_t>(i)];
				P.RefugeWater *= Fade;
				P.Grief *= Heal;
				if (W > P.RefugeWater)
				{
					P.RefugeWater = W;
					P.RefugeLat = static_cast<float>(C.Lat);
					P.RefugeLon = static_cast<float>(C.Lon);
				}
				if (P.IsChild() && P.AgeYears() > 11.0)
				{
					auto It = std::lower_bound(Npcs.begin(), Npcs.end(), P.Mother, [](const Npc& N, uint32_t V) { return N.Id < V; });
					if (It != Npcs.end() && It->Id == P.Mother && It->RefugeWater > P.RefugeWater)
					{
						P.RefugeWater = It->RefugeWater;
						P.RefugeLat = It->RefugeLat;
						P.RefugeLon = It->RefugeLon;
					}
				}
			}
		}
	}

	// ---- Trust, reputation and leaders --------------------------------------------------------

	// Sums how much each person is trusted, lets the hungry resent whoever hoards, and finds
	// each band's most trusted adult.
	void WeighReputations()
	{
		// Npcs are sorted by id; searching a flat list of ids is far faster than the records themselves.
		std::vector<uint32_t> Ids(Npcs.size());
		for (size_t i = 0; i < Npcs.size(); ++i) Ids[i] = Npcs[i].Id;
		auto IndexOf = [&](uint32_t Id) -> long
		{
			auto It = std::lower_bound(Ids.begin(), Ids.end(), Id);
			return It != Ids.end() && *It == Id ? static_cast<long>(It - Ids.begin()) : -1;
		};
		// Finding who each bond points at is the slow part, so that runs on all cores; adding up stays
		// in order, so the sums come out the same however the work is split.
		std::vector<long> Target(Npcs.size() * Npc::kBonds, -1);
		InParallel(Npcs.size(), [&](size_t i)
		{
			for (int b = 0; b < Npc::kBonds; ++b)
				if (Npcs[i].BondWith[b] != 0) Target[i * Npc::kBonds + static_cast<size_t>(b)] = IndexOf(Npcs[i].BondWith[b]);
		});
		for (Npc& P : Npcs) P.Reputation = 0.0f;
		for (size_t i = 0; i < Npcs.size(); ++i)
			for (int b = 0; b < Npc::kBonds; ++b)
			{
				const long At = Target[i * Npc::kBonds + static_cast<size_t>(b)];
				if (At >= 0) Npcs[static_cast<size_t>(At)].Reputation += Npcs[i].Trust[b];
			}
		for (Camp& C : Camps)
		{
			int Hungriest = -1, Richest = -1, Leader = -1, Sitting = -1;
			for (int i : C.Members)
			{
				const Npc& P = Npcs[static_cast<size_t>(i)];
				if (Hungriest < 0 || P.Energy < Npcs[static_cast<size_t>(Hungriest)].Energy) Hungriest = i;
				if (Richest < 0 || P.CarriedKg > Npcs[static_cast<size_t>(Richest)].CarriedKg) Richest = i;
				if (P.AgeYears() >= 18.0 && (Leader < 0 || P.Reputation > Npcs[static_cast<size_t>(Leader)].Reputation)) Leader = i;
				if (P.Id == C.Leader) Sitting = i;
			}
			// A leader keeps the role until someone is clearly more trusted, or they lose most of it.
			if (Sitting >= 0 && Npcs[static_cast<size_t>(Sitting)].Reputation >= 1.0f &&
				Npcs[static_cast<size_t>(Leader)].Reputation < Npcs[static_cast<size_t>(Sitting)].Reputation + 0.5f)
			{
				Leader = Sitting;
			}
			// Going hungry while a camp mate sits on plenty is not forgotten.
			if (Hungriest >= 0 && Richest >= 0 && Hungriest != Richest && Npcs[static_cast<size_t>(Hungriest)].Energy < 0.25f &&
				Npcs[static_cast<size_t>(Richest)].CarriedKg > 4.0f)
			{
				Npcs[static_cast<size_t>(Hungriest)].Feel(Npcs[static_cast<size_t>(Richest)].Id, -0.1f);
			}
			C.Leader = Leader >= 0 && Npcs[static_cast<size_t>(Leader)].Reputation >= (Leader == Sitting ? 1.0f : 2.0f) ? Npcs[static_cast<size_t>(Leader)].Id : 0;
			if (C.Leader && !Npcs[static_cast<size_t>(Leader)].Leads)
			{
				const Npc& L = Npcs[static_cast<size_t>(Leader)];
				if (!FirstLeader)
				{
					FirstLeader = L.Id;
					AddEvent(PersonName(L.Name) + " becomes the first leader: the person their band trusts most");
					Remember(L, "became the first leader anyone followed, because their band trusted them most" +
						Say(C, NameWord(L), {Coin(C, Lead), "lead"}, {Coin(C, Us), "us"}));
				}
				else if (Notables.count(L.Id))
				{
					Remember(L, "became the leader of their band");
				}
			}
		}
		for (Npc& P : Npcs) P.Leads = false;
		for (const Camp& C : Camps)
		{
			const long At = C.Leader ? IndexOf(C.Leader) : -1;
			if (At >= 0) Npcs[static_cast<size_t>(At)].Leads = true;
		}
	}

	// ---- Beliefs ----------------------------------------------------------------------------

	static constexpr size_t kMaxFaith = 3;
	static std::string Lower(std::string S)
	{
		if (!S.empty()) S[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(S[0])));
		return S;
	}
	static constexpr int kShellMaterial = 20; // Found on coasts, so it doubles as "by the sea".

	const BeliefRecord& BeliefOf(uint32_t Id) const { return BeliefLog[Id - 1]; }

	// A belief this band holds that forbids a material the invention uses, if any.
	const BeliefRecord* Forbids(const Camp& C, uint32_t InventionId) const
	{
		for (uint32_t F : C.Faith)
		{
			const BeliefRecord& B = BeliefOf(F);
			if (B.Claim != kForbidden || B.Subject < SubjectCount()) continue;
			const int M = B.Subject - SubjectCount();
			if (MaterialA(InventionId) == M || MaterialB(InventionId) == M) return &B;
		}
		return nullptr;
	}

	// Someone made something their band's beliefs forbid: the band won't have it, and they lose face.
	void Shun(const Camp& C, uint32_t Id, const Npc& Maker, const BeliefRecord& Why)
	{
		++Shunned;
		const bool First = InventionLog.find(Id) == InventionLog.end();
		if (!First) return;
		(void)C;
		Remember(Maker, "made " + InventionGloss(Id) + ", ahead of anyone, but the band would not have it: they believe that " +
			Lower(BeliefGloss(Why.Subject, Why.Claim, Why.Practice)) + ". The maker was shunned");
		if (Shunned == 1) AddEvent("First invention refused because of a belief: " + InventionGloss(Id));
	}

	// Whether a band could keep up a practice at all.
	bool CanPractise(const Camp& C, int Subject, int P) const
	{
		const Practice& Pr = BeliefPractices()[static_cast<size_t>(P)];
		if (Pr.AnimalsOnly && Subject != 6 && Subject != 7) return false;
		if (P == kMoonPractice && Subject != kMoonSubject) return false;
		if (Pr.NeedsSkill >= 0 && !Knows(C.Known, Pr.NeedsSkill)) return false;
		if (Pr.NeedsMaterial >= 0 && !((C.Met >> Pr.NeedsMaterial) & 1u)) return false;
		if (Pr.NeedsWorking >= 0)
		{
			bool Any = false;
			for (uint32_t Id : C.Inventions) Any = Any || WorkingOf(Id) == Pr.NeedsWorking;
			if (!Any) return false;
		}
		return true;
	}

	void AddBeliefEffects(Camp& C) const
	{
		C.Cohesion = 0.0f;
		SkillEffects Add;
		float* Fields[] = {&Add.Forage, &Add.Hunt, &Add.PredatorDeaths, &Add.ChildDeaths, &Add.Warmth, &Add.Travel, &Add.Land, &Add.Carry, &Add.Teaching};
		for (uint32_t F : C.Faith)
		{
			const Practice& Pr = BeliefPractices()[static_cast<size_t>(BeliefOf(F).Practice)];
			C.Cohesion += Pr.Cohesion;
			if (Pr.Effect == 4) *Fields[4] += Pr.Factor - 1.0f;
			else if (Pr.Effect >= 0) *Fields[Pr.Effect] *= Pr.Factor;
		}
		C.Cohesion = std::min(C.Cohesion, 1.0f);
		C.Effects.Add(Add);
	}

	// What a band facing hardship puts it down to: what struck it, or what is around it.
	int PickSubject(const Camp& C, double DeathShare)
	{
		const double R = Dice.Uniform();
		// Sky omens are blamed first: a sun hidden by ash, the moon eating the sun, a burning star.
		if (Eco->DisasterWinter() > 0.1 && R < 0.5) return 1;
		if (OmenDays > 0 && R < 0.5) return OmenSubject;
		if (DeathShare > 0.05 && R < 0.4) return 0;
		if (C.SeenDanger > 0.3f && R < 0.6) return 6;
		if (R < 0.85)
		{
			// Only what they live with: the cold away from the tropics, the sea on a coast.
			const int S = static_cast<int>(Dice.Next() % static_cast<uint64_t>(SubjectCount()));
			if (S == 9 && std::abs(C.Lat) < 35.0 && Eco->IceCover() < 0.5) return 1;
			if (S == 8 && !Present(C, kShellMaterial)) return 3;
			return S;
		}
		// Now and then something from the band's own world of materials.
		int Count = 0;
		std::array<int, 64> Met{};
		for (int m = 0; m < static_cast<int>(AllMaterials().size()); ++m)
		{
			if ((C.Met >> m) & 1u) Met[static_cast<size_t>(Count++)] = m;
		}
		return Count ? SubjectCount() + Met[Dice.Next() % static_cast<uint64_t>(Count)] : 1;
	}

	// A band takes up a belief, dropping its oldest one if it already holds three.
	static void Adopt(Camp& C, uint32_t Id)
	{
		if (std::find(C.Faith.begin(), C.Faith.end(), Id) != C.Faith.end()) return;
		if (C.Faith.size() >= kMaxFaith) C.Faith.erase(C.Faith.begin());
		C.Faith.push_back(Id);
	}

	uint32_t FoundBelief(const Camp& C, int Subject, int Claim, int Practice, uint32_t Parent, uint32_t Founder)
	{
		BeliefRecord B;
		B.Id = static_cast<uint32_t>(BeliefLog.size() + 1);
		B.Subject = Subject;
		B.Claim = Claim;
		B.Practice = Practice;
		B.Parent = Parent;
		B.Year = Eco->Years();
		B.Founder = Founder;
		B.CampId = C.Id;
		B.Lat = C.Lat;
		B.Lon = C.Lon;
		B.Word = static_cast<uint16_t>(Dice.Next());
		BeliefLog.push_back(B);
		return B.Id;
	}

	// Bands that can talk wonder about what happens to them, believe, and pass beliefs on.
	void Believe(int Days)
	{
		const int Claims = static_cast<int>(BeliefClaims().size());
		const int Practices = static_cast<int>(BeliefPractices().size());
		for (Camp& C : Camps)
		{
			if (C.Members.empty()) continue;
			const double DeathShare = static_cast<double>(C.DeathsLately) / static_cast<double>(C.Members.size());
			C.DeathsLately = 0;
			// Beliefs need words: only bands with spoken language make them.
			if (Knows(C.Known, 13))
			{
				const double Hardship = std::clamp(DeathShare * 10.0 + C.SeenDanger, 0.0, 1.0);
				double Knack = 0.0;
				for (int i : C.Members)
				{
					const Npc& P = Npcs[static_cast<size_t>(i)];
					if (P.AgeYears() >= 12.0) Knack += Smarts(P) * Smarts(P) * P.Genes->Curiosity / 0.6;
				}
				if (Dice.Chance(std::min(1.0, Knack * Settings.BeliefTriesPerYear * (1.0 + 3.0 * Hardship) * Days / 365.0)))
				{
					const int Subject = PickSubject(C, DeathShare);
					const int Claim = static_cast<int>(Dice.Next() % static_cast<uint64_t>(Claims));
					const int Practice = static_cast<int>(Dice.Next() % static_cast<uint64_t>(Practices));
					if (CanPractise(C, Subject, Practice))
					{
						// Usually the oldest adult is the one who explains; elders keep the stories.
						const Npc* Teller = nullptr;
						for (int i : C.Members)
						{
							const Npc& P = Npcs[static_cast<size_t>(i)];
							if (!Teller || P.AgeDays > Teller->AgeDays) Teller = &P;
						}
						const uint32_t Id = FoundBelief(C, Subject, Claim, Practice, 0, Teller->Id);
						Adopt(C, Id);
						const BeliefRecord& B = BeliefOf(Id);
						const std::string Gloss = BeliefGloss(Subject, Claim, Practice);
						if (BeliefLog.size() == 1) AddEvent("The first belief: " + Gloss + " (called \"" + SayWord(B.Word) + "\")");
						Remember(*Teller, "first taught that " + Lower(Gloss) + ", a belief their band called \"" + SayWord(B.Word) + "\"" +
							Say(C, {SubjectWord(C, Subject), SubjectName(Subject)}, {Coin(C, Teach), "teach"}, {B.Word, SayWord(B.Word)}));
					}
				}
			}
			// Without anything to keep it alive, a belief is now and then let go (about once in 2,000 years).
			if (!C.Faith.empty() && Dice.Chance(OverDays(1.0 / (2000.0 * 365.0), Days)))
			{
				C.Faith.erase(C.Faith.begin() + static_cast<std::ptrdiff_t>(Dice.Next() % C.Faith.size()));
			}
			// Neighbours' beliefs spread, more easily between bands that understand each other and
			// less into bands already held together by their own.
			if (C.Neighbour != 0)
			{
				const Camp& O = Camps[CampSlot(C.Neighbour)];
				const double Rate = 1.0 / ((Understand(C, O) ? 200.0 : 2000.0) * 365.0) * (1.0 - 0.5 * C.Cohesion);
				if (!O.Faith.empty() && Dice.Chance(OverDays(Rate, Days)))
				{
					const uint32_t Id = O.Faith[Dice.Next() % O.Faith.size()];
					if (CanPractise(C, BeliefOf(Id).Subject, BeliefOf(Id).Practice)) Adopt(C, Id);
				}
			}
		}
		NoteReligions();
	}

	// When a band splits off, the ones who leave sometimes change what they believe a little: a new sect.
	void MaybeSchism(Camp& New)
	{
		for (uint32_t& F : New.Faith)
		{
			if (!Dice.Chance(0.02)) continue;
			const BeliefRecord Old = BeliefOf(F);
			int Claim = Old.Claim, Practice = Old.Practice;
			if (Dice.Chance(0.5)) Claim = static_cast<int>(Dice.Next() % BeliefClaims().size());
			else Practice = static_cast<int>(Dice.Next() % BeliefPractices().size());
			if ((Claim == Old.Claim && Practice == Old.Practice) || !CanPractise(New, Old.Subject, Practice)) continue;
			F = FoundBelief(New, Old.Subject, Claim, Practice, Old.Id, 0);
			if (Old.Peak >= 10)
			{
				AddEvent("A schism: some of those who believe \"" + SayWord(Old.Word) + "\" break away, believing that " + Lower(BeliefGloss(Old.Subject, Claim, Practice)));
			}
		}
	}

	// Counts each belief's bands once a year, and notes beliefs that become religions.
	void NoteReligions()
	{
		const int Year = static_cast<int>(Eco->Years());
		if (Year == ReligionYear) return;
		ReligionYear = Year;
		std::vector<int> Bands(BeliefLog.size() + 1, 0);
		for (const Camp& C : Camps)
		{
			for (uint32_t F : C.Faith) Bands[F] += 1;
		}
		for (BeliefRecord& B : BeliefLog)
		{
			const int N = Bands[B.Id];
			B.Peak = std::max(B.Peak, N);
			if (B.NextMark < kReligionBands) B.NextMark = kReligionBands; // Saves from before the bar was raised.
			if (N >= B.NextMark)
			{
				AddEvent("The belief \"" + SayWord(B.Word) + "\" (" + BeliefGloss(B.Subject, B.Claim, B.Practice) + ") is now held by " + std::to_string(N) +
					" bands" + (B.NextMark == kReligionBands ? ": a religion is born" : ""));
				while (B.NextMark <= N) B.NextMark *= 10;
			}
		}
	}

	// A band makes something by itself.
	void MakeFirst(Camp& C, uint32_t Id, const Npc& Maker)
	{
		C.Inventions.insert(std::upper_bound(C.Inventions.begin(), C.Inventions.end(), Id), Id);
		const uint16_t Word = static_cast<uint16_t>(Dice.Next());
		Name(C, InventionKey(Id), Word);
		InventionRecord& R = InventionLog[Id];
		R.Times += 1;
		if (R.Times > 1) return;
		R.Id = Id;
		R.Year = Eco->Years();
		R.Inventor = Maker.Id;
		R.CampId = C.Id;
		R.Lat = C.Lat;
		R.Lon = C.Lon;
		R.Word = Word;
		const std::string Gloss = InventionGloss(Id);
		AddEvent("First invention of " + Gloss + ", called \"" + SayWord(Word) + "\" by its makers");
		Remember(Maker, "was the first ever to make " + Gloss + ", which their band called \"" + SayWord(Word) + "\"" +
			Say(C, NameWord(Maker), {Coin(C, Make), "make"}, {Word, Gloss}));
	}
	void NoteName(uint32_t Id, uint16_t Name) { KnownNames[Id] = Name; }
	static std::string PersonName(uint16_t Name)
	{
		std::string S = SayWord(Name);
		if (!S.empty()) S[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(S[0])));
		return S;
	}

	// Starts or adds to someone's story.
	void Remember(const Npc& P, const std::string& Deed)
	{
		Notable& N = Notables[P.Id];
		if (N.Id == 0)
		{
			N.Id = P.Id;
			N.Name = P.Name;
			NoteName(P.Id, P.Name);
			for (uint32_t Parent : {P.Mother, P.Father})
			{
				if (const Npc* Q = Find(Parent)) NoteName(Q->Id, Q->Name);
			}
			N.Mother = P.Mother;
			N.Father = P.Father;
			N.Female = P.Female;
			N.Kind = P.Kind;
			N.Generation = P.Generation;
			N.Brain = P.BrainSize();
			N.BornYear = Eco->Years() - P.AgeYears();
			if (!Camps.empty()) N.Speech = SayWord(Camps[CampSlot(P.Camp)].Words[0]);
		}
		char Head[96];
		std::snprintf(Head, sizeof(Head), "Year %.0f, aged %.0f, near %.0f, %.0f: ", Eco->Years(), P.AgeYears(), P.Lat, P.Lon);
		N.Deeds.push_back(Head + Deed);
		N.LastLat = P.Lat;
		N.LastLon = P.Lon;
	}

	void Discover(Npc& P, int Skill)
	{
		P.LearnSkill(Skill);
		if (!Knows(KnownNow, Skill))
		{
			const std::string Name = AllSkills()[Skill].Name;
			AddEvent(Knows(EverKnown, Skill) ? "Someone rediscovers " + Name : "First discovery of " + Name);
			Remember(P, (Knows(EverKnown, Skill) ? "rediscovered " : "was the first ever to discover ") + Name);
			if (!Knows(EverKnown, Skill)) RecordDiscovery(P, Name);
			KnownNow |= SkillSet(1) << Skill;
			EverKnown |= SkillSet(1) << Skill;
		}
	}

	// A first-ever find by someone in a state that writes goes into the world's library; a state
	// whose people make three becomes known for its school of thinkers.
	void RecordDiscovery(const Npc& P, const std::string& What)
	{
		static const int Script = SkillNamed("writing");
		const Camp& C = Camps[CampSlot(P.Camp)];
		Polity* S = C.State ? StateById(C.State) : nullptr;
		if (!S || !Has(S->Known, Script)) return;
		Writing Piece;
		Piece.Year = Eco->Years(), Piece.Author = PersonName(P.Name), Piece.Place = StateName(*S), Piece.Kind = "discovery";
		Piece.Text = "Wrote down how to know " + What + ", the first anyone ever found it.";
		Library.push_back(Piece);
		if (++S->Discoveries == 3 && S->City)
			AddEvent("The thinkers of " + PersonName(S->City) + " in " + StateName(*S) + " are a school of their own: three first discoveries, the latest " + What);
	}

	// Each word changes about once in 5,000 years, and bands within reach borrow a word about every
	// 500 years, so neighbours keep understanding each other while cut-off bands drift apart.
	void DriftLanguages(int Days)
	{
		const double Change = OverDays(1.0 / (5000.0 * 365.0), Days);
		const double Borrow = OverDays(1.0 / (500.0 * 365.0), Days);
		for (Camp& C : Camps)
		{
			for (uint16_t& W : C.Words)
			{
				if (Dice.Chance(Change)) W = static_cast<uint16_t>(Dice.Next());
			}
			for (auto& Entry : C.Lexicon)
			{
				if (Dice.Chance(Change)) Entry.second = static_cast<uint16_t>(Dice.Next());
			}
			if (C.Neighbour != 0 && Dice.Chance(Borrow))
			{
				const int w = static_cast<int>(Dice.Next() % kWords);
				C.Words[w] = Camps[CampSlot(C.Neighbour)].Words[w];
			}
		}
		const int Year = static_cast<int>(Eco->Years());
		if (Year != LanguageYear)
		{
			LanguageYear = Year;
			const int N = Languages();
			if (N >= NextLanguageMark)
			{
				AddEvent("The people now speak " + std::to_string(N) + " languages that can't understand each other");
				while (NextLanguageMark <= N) NextLanguageMark *= 2;
			}
		}
	}

	// Notes skills nobody alive knows any more.
	void NoteSkills()
	{
		SkillSet Now = 0;
		for (const Npc& P : Npcs)
		{
			Now |= P.Skills;
		}
		const SkillSet Lost = KnownNow & ~Now;
		for (int k = 0; k < static_cast<int>(AllSkills().size()); ++k)
		{
			if (Knows(Lost, k))
			{
				AddEvent(std::string(AllSkills()[k].Name) + " is forgotten: nobody alive knows it");
				// The wait to find it again starts now (see LearnSkills).
				if (static_cast<size_t>(k) < Findable.size()) Findable[static_cast<size_t>(k)] = Eco->Years();
			}
		}
		KnownNow = Now;
		// What other kinds of human were born knowing counts as known too, or what builds on it never becomes findable.
		EverKnown |= Now;
	}

	// Energy a day beyond the basics: a bigger brain costs more (a smaller one saves a little), and
	// the cold costs more far from the tropics in winter, unless fire, clothing and shelter keep it out.
	float ExtraBurn(const Npc& P) const
	{
		const double Far = std::clamp((std::abs(P.Lat) - 30.0) / 30.0, 0.0, 1.0);
		const double Cold = Far * (0.5 + 0.5 * (1.0 - Eco->GrowingSeason(P.Lat)));
		const float Warmth = Camps[CampSlot(P.Camp)].Effects.Warmth;
		const HomininKind& K = OtherKinds()[P.Kind];
		return 0.0015f * static_cast<float>(P.BrainSize() - kBrainHidden) + static_cast<float>(0.12 * Cold) * (1.0f - Warmth) * (1.0f - K.ColdHardy) + K.Appetite;
	}

	// How good an NPC feels right now; learning tries to make this go up.
	static float WellbeingOf(const Npc& P)
	{
		const float Hunger = 1.0f - P.Energy;
		const float Thirst = 1.0f - P.Water;
		// Food in hand counts too: it is tomorrow's meal, which lets effort that pays off later be learned.
		return -(Hunger * Hunger + Thirst * Thirst + 0.5f * P.Fatigue * P.Fatigue + 0.5f * P.Fear + 0.3f * P.Loneliness + 0.3f * P.Grief) +
			(P.Health - 1.0f) + 0.04f * std::min(P.CarriedKg, 10.0f);
	}

	// Strengthens choices that made the day better than usual and weakens ones that made it worse.
	static void Learn(Npc& P, int Chosen, float Reward)
	{
		const float Advantage = Reward - P.RewardBaseline;
		P.RewardBaseline = P.RewardBaseline * 0.95f + Reward * 0.05f;
		const float Rate = P.Genes->LearningRate * Advantage / P.Genes->Curiosity;
		for (int a = 0; a < kBrainActions; ++a)
		{
			const float Push = Rate * ((a == Chosen ? 1.0f : 0.0f) - P.LastChoice[a]);
			for (int h = 0; h < P.BrainSize(); ++h)
			{
				// Lessons fade unless they keep being confirmed.
				float& W = P.LearnedW2[h * kBrainActions + a];
				W = std::clamp(W * 0.9995f + Push * P.LastHidden[h], -4.0f, 4.0f);
			}
			P.LearnedB2[a] = std::clamp(P.LearnedB2[a] * 0.9995f + Push, -4.0f, 4.0f);
		}
	}

	static void Blend(Npc& Learner, const Npc& Teacher, float Amount)
	{
		// Only neurons both have can be copied.
		for (size_t k = 0; k < std::min(Learner.LearnedW2.size(), Teacher.LearnedW2.size()); ++k)
		{
			Learner.LearnedW2[k] += (Teacher.LearnedW2[k] - Learner.LearnedW2[k]) * Amount;
		}
		for (size_t k = 0; k < Learner.LearnedB2.size(); ++k)
		{
			Learner.LearnedB2[k] += (Teacher.LearnedB2[k] - Learner.LearnedB2[k]) * Amount;
		}
		Learner.HuntSkill += (std::max(Learner.HuntSkill, Teacher.HuntSkill) - Learner.HuntSkill) * Amount;
		Learner.ForageSkill += (std::max(Learner.ForageSkill, Teacher.ForageSkill) - Learner.ForageSkill) * Amount;
	}

	// Sometimes copies the habits of an older camp mate who is doing better.
	void Imitate(size_t Index, int Days = 1) { Imitate(Index, Days, Dice); }
	// Copies only from camp mates, so camps can imitate side by side.
	void Imitate(size_t Index, int Days, Rng& R)
	{
		Npc& P = Npcs[Index];
		if (P.IsChild() || !R.Chance(OverDays(P.Genes->Imitation, Days)))
		{
			return;
		}
		const std::vector<int>& Mates = Camps[CampSlot(P.Camp)].Members;
		int Best = -1;
		for (int Try = 0; Try < 5 && !Mates.empty(); ++Try)
		{
			const int j = Mates[static_cast<size_t>(R.Next() % Mates.size())];
			const Npc& Q = Npcs[j];
			if (j != static_cast<int>(Index) && Q.AgeDays > P.AgeDays && Q.Wellbeing > P.Wellbeing &&
				(Best < 0 || Q.Wellbeing > Npcs[Best].Wellbeing))
			{
				Best = j;
			}
		}
		if (Best >= 0)
		{
			Blend(P, Npcs[Best], 0.1f);
		}
	}

	void BirthsAndDeaths(int Days = 1)
	{
		std::vector<Npc> Born;
		for (Npc& P : Npcs)
		{
			if (P.PregnantDays >= 270.0)
			{
				Npc Baby;
				Baby.Id = NextId++;
				Baby.Mother = P.Id;
				Baby.Father = P.BabyFather;
				Baby.Kind = P.Kind; // Raised in its mother's band, as one of her people.
				Baby.OtherShare = 0.5f * (P.OtherShare + P.BabyFatherShare);
				Baby.Female = Dice.Chance(0.5);
				Baby.Camp = P.Camp;
				Baby.Lat = P.Lat;
				Baby.Lon = P.Lon;
				Baby.OffsetLat = P.OffsetLat + static_cast<float>(Dice.Range(-0.005, 0.005));
				Baby.OffsetLon = P.OffsetLon + static_cast<float>(Dice.Range(-0.005, 0.005));
				const Genome& Dad = P.BabyFatherGenes ? *P.BabyFatherGenes : *P.Genes;
				Baby.Genes = std::make_shared<const Genome>(Cross(*P.Genes, Dad));
				Baby.FitBrain();
				Baby.Energy = 0.8f;
				Baby.Water = 0.8f;
				Baby.HuntSkill = 0.1f;
				Baby.ForageSkill = 0.3f;
				Baby.Heading = static_cast<float>(Dice.Range(0.0, 6.283185307179586));
				Baby.Name = static_cast<uint16_t>(Dice.Next());
				// A newborn starts out trusting its mother, and she it.
				Baby.Feel(P.Id, 0.5f);
				P.Feel(Baby.Id, 0.5f);
				int FatherGeneration = 0;
				if (const Npc* Father = Find(P.BabyFather))
				{
					FatherGeneration = Father->Generation;
				}
				Baby.Generation = std::max(P.Generation, FatherGeneration) + 1;
				P.PregnantDays = -1.0;
				P.LastBirthDay = Eco->Day();
				P.BabyFatherGenes.reset();
				P.Children += 1;
				for (uint32_t Parent : {Baby.Mother, Baby.Father})
				{
					auto It = Notables.find(Parent);
					if (It == Notables.end()) continue;
					It->second.Children.push_back(Baby.Id);
					const uint32_t Other = Parent == Baby.Mother ? Baby.Father : Baby.Mother;
					NoteName(Baby.Id, Baby.Name);
					if (const Npc* O = Find(Other)) NoteName(O->Id, O->Name);
					std::vector<uint32_t>& Pa = It->second.Partners;
					if (Other && std::find(Pa.begin(), Pa.end(), Other) == Pa.end()) Pa.push_back(Other);
				}
				if (Baby.BrainSize() > RecordBrain)
				{
					RecordBrain = Baby.BrainSize();
					if (Eco->Years() > 0.0) Remember(Baby, "was born with the biggest brain yet, " + std::to_string(RecordBrain) + " neurons");
				}
				Born.push_back(Baby);
				++Births;
				if (!AnyNpcChild && Baby.Kind == 0)
				{
					AnyNpcChild = true;
					AddEvent("First child born");
				}
			}

			// Old age, childhood illness and accidents; per year, spread over the days.
			const double Age = P.AgeYears();
			// Infancy and childhood were dangerous: about four in ten children died before 15.
			const SkillEffects& Known = Camps[CampSlot(P.Camp)].Effects;
			const double Young = (Age < 1.0 ? 0.15 : (Age < 5.0 ? 0.06 : (Age < 15.0 ? 0.01 : 0.0))) * Known.ChildDeaths;
			const double Yearly = 0.0004 * std::exp(0.08 * Age) + Young + 0.005;
			// Predators take the lone more than people in a big camp, and miss those who hide.
			const double Alone = 1.0 / (1.0 + 0.3 * std::min(P.BandSize - 1, 20));
			const double Hidden = P.LastAction == static_cast<int>(Action::Hide) ? 0.3 : 1.0;
			if (Dice.Chance(OverDays(P.Fear * 0.0003 * Alone * Hidden * Known.PredatorDeaths, Days)))
			{
				Die(P, 1);
			}
			else if (Dice.Chance(OverDays(Yearly / 365.0, Days)))
			{
				Die(P, 0);
			}
			else if (P.Health <= 0.0f)
			{
				Die(P, P.Water <= 0.0f ? 3 : (P.Energy <= 0.0f ? 2 : 4));
			}
		}
		Npcs.erase(std::remove_if(Npcs.begin(), Npcs.end(), [](const Npc& P) { return !P.Alive; }), Npcs.end());
		for (Npc& B : Born)
		{
			HighestGeneration = std::max(HighestGeneration, B.Generation);
			Npcs.push_back(std::move(B));
		}
		if (Npcs.empty())
		{
			AddEvent("The last NPC died; the people are gone");
		}
	}

	void StartBand()
	{
		Camp First;
		First.Id = NextCampId++;
		First.Lat = Settings.StartLat;
		First.Lon = Settings.StartLon;
		for (uint16_t& W : First.Words) W = static_cast<uint16_t>(Dice.Next());
		Camps.push_back(First);

		// A band starting later than the run's usual start knows what people really knew by then.
		SkillSet Known = 0;
		for (size_t k = 0; k < AllSkills().size(); ++k)
			if (AllSkills()[k].YearsAgo > Eco->YearsAgo() + 1.0) Known |= SkillSet(1) << k;
		EverKnown |= Known;
		const Genome Base = InstinctGenome();
		for (int i = 0; i < Settings.StartCount; ++i)
		{
			Npc P;
			P.Id = NextId++;
			P.Name = static_cast<uint16_t>(Dice.Next());
			P.Female = i % 2 == 0;
			P.AgeDays = Dice.Range(14.0, 35.0) * 365.0;
			if (HallOfFame.empty())
			{
				P.Genes = std::make_shared<const Genome>(Mutate(Base, 0.3f));
			}
			else
			{
				P.Genes = std::make_shared<const Genome>(Mutate(*HallOfFame[static_cast<size_t>(i) % HallOfFame.size()].second, 0.15f));
			}
			P.FitBrain();
			P.Camp = First.Id;
			P.Skills = Known;
			P.Heading = static_cast<float>(Dice.Range(0.0, 6.283185307179586));
			P.OffsetLat = static_cast<float>(Dice.Range(-0.04, 0.04));
			P.OffsetLon = static_cast<float>(Dice.Range(-0.04, 0.04));
			P.Lat = First.Lat + P.OffsetLat;
			P.Lon = First.Lon + P.OffsetLon;
			Npcs.push_back(P);
		}
		++Arrivals;
		IndexCamps();
	}

	// The other kinds of human alive at this point in the run, a few bands each in their real homes.
	void StartOthers()
	{
		OthersStarted = true;
		for (size_t k = 1; k < OtherKinds().size(); ++k)
			if (StartKind(k)) AddEvent(std::string(OtherKinds()[k].Plural) + " live here too, in their own lands");
	}

	// The real kinds lived on for many thousands of years in places the run is too coarse to hold
	// them through hard times, so until their real last date one that dies out comes back from a
	// refuge; after it, whether they last is up to them.
	// shortcut: stands in for refuges the grid can't see; drop it if the run's density is raised.
	void BringBackOthers()
	{
		uint32_t Alive = 0;
		for (const Npc& P : Npcs) Alive |= 1u << P.Kind;
		// At most once in 1,000 years each: a home too poor to hold them would otherwise refill and
		// empty again every step.
		const double Now = Eco->Years();
		LastReturn.resize(OtherKinds().size(), -1.0e9);
		for (size_t k = 1; k < OtherKinds().size(); ++k)
		{
			if ((Alive >> k & 1u) || Now - LastReturn[k] < 1000.0) continue;
			LastReturn[k] = Now;
			if (StartKind(k)) AddEvent(std::string("A hidden group of ") + OtherKinds()[k].Plural + " comes back from a refuge");
		}
	}

	// A few bands of one other kind in its real homes; false if it was gone by now or nowhere fits.
	bool StartKind(size_t k)
	{
		const HomininKind& K = OtherKinds()[k];
		if (Eco->YearsAgo() < K.LastYearsAgo) return false;
		const Genome Base = InstinctGenome();
		bool Any = false;
		{
			for (const auto& [Lat, Lon] : K.Homes)
			{
				if (!Livable(Lat, Lon)) continue;
				Camp C;
				C.Id = NextCampId++;
				C.Kind = static_cast<uint8_t>(k);
				C.Lat = Lat;
				C.Lon = Lon;
				for (uint16_t& W : C.Words) W = static_cast<uint16_t>(Dice.Next());
				Camps.push_back(C);
				for (int i = 0; i < Settings.StartCount; ++i)
				{
					Npc P;
					P.Id = NextId++;
					P.Name = static_cast<uint16_t>(Dice.Next());
					P.Kind = static_cast<uint8_t>(k);
					P.OtherShare = 1.0f;
					for (int Sk : K.Skills) P.Skills |= SkillSet(1) << Sk;
					P.Own = EffectsOf(P.Skills);
					P.Female = i % 2 == 0;
					P.AgeDays = Dice.Range(14.0, 35.0) * 365.0;
					Genome G = Mutate(Base, 0.3f);
					for (int n = 0; n < K.FewerNeurons && G.Hidden() > 4; ++n) G.RemoveNeuron(G.Hidden() - 1);
					P.Genes = std::make_shared<const Genome>(std::move(G));
					P.FitBrain();
					P.Camp = C.Id;
					P.Heading = static_cast<float>(Dice.Range(0.0, 6.283185307179586));
					P.OffsetLat = static_cast<float>(Dice.Range(-0.04, 0.04));
					P.OffsetLon = static_cast<float>(Dice.Range(-0.04, 0.04));
					P.Lat = Lat + P.OffsetLat;
					P.Lon = Lon + P.OffsetLon;
					Npcs.push_back(P);
				}
				Any = true;
			}
		}
		return Any;
		IndexCamps();
	}

	static std::string KindName(int Kind) { return Kind ? std::string("a ") + OtherKinds()[static_cast<size_t>(Kind)].Name : "an NPC"; }

	// The first time NPCs and each other kind of human come face to face.
	void NoteMeeting(const Camp& A, const Camp& B)
	{
		const int Other = A.Kind ? A.Kind : B.Kind;
		if ((A.Kind && B.Kind) || ((MetKinds >> Other) & 1u)) return;
		MetKinds |= 1u << Other;
		char At[48];
		std::snprintf(At, sizeof(At), " near %.0f, %.0f", A.Lat, A.Lon);
		AddEvent(std::string("NPCs meet ") + OtherKinds()[static_cast<size_t>(Other)].Plural + " for the first time" + At);
	}

	void RememberIfSuccessful(const Npc& P)
	{
		if (P.Kind != 0) return; // New NPC bands take after NPCs only.
		const double Score = P.Children * 100.0 + P.AgeYears();
		if (HallOfFame.size() >= 10 && Score <= HallOfFame.back().first)
		{
			return;
		}
		HallOfFame.emplace_back(Score, P.Genes);
		std::stable_sort(HallOfFame.begin(), HallOfFame.end(), [](const auto& A, const auto& B) { return A.first > B.first; });
		if (HallOfFame.size() > 10)
		{
			HallOfFame.pop_back();
		}
	}

	void Die(Npc& P, int Cause)
	{
		RememberIfSuccessful(P);
		static const char* FirstDeath[5] = {nullptr, "First NPC killed by a predator", "First NPC starved",
			"First NPC died of thirst", "First NPC died of an injury"};
		P.Alive = false;
		++Deaths;
		AgeAtDeath += P.AgeYears(), ++DeathsThisYear;
		if (!Camps.empty())
			if (const uint32_t St = Camps[CampSlot(P.Camp)].State)
			{
				auto& A = StateAges[St];
				A.first += P.AgeYears(), A.second += 1;
			}
		if (!Camps.empty())
		{
			Camp& C = Camps[CampSlot(P.Camp)];
			C.DeathsLately += 1;
			// Those who loved them grieve.
			for (int i : C.Members)
			{
				Npc& Q = Npcs[static_cast<size_t>(i)];
				const float Love = Q.TrustIn(P.Id);
				if (&Q == &P || Love < 0.3f) continue;
				Q.Grief = std::max(Q.Grief, Love);
				if (!AnyGrief)
				{
					AnyGrief = true;
					AddEvent(PersonName(Q.Name) + " grieves for " + PersonName(P.Name) + ": the first sorrow anyone feels for someone they loved");
				}
			}
		}
		auto Story = Notables.find(P.Id);
		if (Story != Notables.end())
		{
			static const char* How[5] = {"of old age or illness", "killed by a predator", "of hunger", "of thirst", "of an injury"};
			char Line[160];
			std::snprintf(Line, sizeof(Line), "Died in year %.0f, aged %.0f, %s, near %.0f, %.0f, knowing %d skills", Eco->Years(), P.AgeYears(), How[Cause],
				P.Lat, P.Lon, P.Skills.Count());
			Story->second.Dead = true;
			Story->second.Death = Line;
		}
		Causes[Cause] += 1;
		if (FirstDeath[Cause] && !AnyDeathBy[Cause] && P.Kind == 0)
		{
			AnyDeathBy[Cause] = true;
			AddEvent(FirstDeath[Cause]);
		}
	}

	// 0 Africa, 1 Asia, 2 Europe, 3 Australia, 4 the Americas, 5 elsewhere.
	// A band names a land in its own speech: a new word built from its words for sun and plant.
	static std::string LandName(const Camp& C, int Region)
	{
		std::string Called = SayWord(static_cast<uint16_t>(C.Words[12] * 31u + C.Words[11] + static_cast<uint32_t>(Region) * 7919u));
		Called[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(Called[0])));
		return Called;
	}

	void NoteRegion(double Lat, double Lon, const Npc* Who = nullptr)
	{
		static const char* RegionNames[6] = {"Africa", "Asia", "Europe", "Australia", "the Americas", "a new land"};
		const int Region = RegionOf(Lat, Lon);
		if (!Regions[Region])
		{
			Regions[Region] = true;
			std::string Called;
			if (Who && !Camps.empty())
			{
				Called = LandName(Camps[CampSlot(Who->Camp)], Region);
				PlaceNames[Region] = Called;
			}
			char At[48];
			std::snprintf(At, sizeof(At), " (near %.0f, %.0f)", Lat, Lon);
			const std::string Where = std::string(RegionNames[Region]) + (Called.empty() ? "" : ", which they call " + Called) + At;
			AddEvent("First NPC sets foot in " + Where);
			if (Who) Remember(*Who, "was the first to set foot in " + Where);
		}
	}

	// ---- Sickness (see Diseases.h) ----------------------------------------------------------

	static float ResistanceTo(const Camp& C, uint16_t Id, double Year, bool* Immune = nullptr)
	{
		for (const auto& [D, When] : C.Exposed)
			if (D == Id)
			{
				if (Immune) *Immune = Year - When < 25.0; // Those who lived through it are still alive.
				return 0.35f;
			}
		if (Immune) *Immune = false;
		return 1.0f;
	}

	void Catch(Camp& C, uint16_t Id)
	{
		bool Immune = false;
		if (C.Sick || C.Members.empty()) return;
		ResistanceTo(C, Id, Eco->Years(), &Immune);
		if (Immune) return;
		static const int Vaccines = SkillNamed("vaccines");
		if (Has(C.Known, Vaccines) && Dice.Chance(0.8)) return;
		C.Sick = Id, C.SickDays = 0;
		Diseases[Id - 1u].Bands += 1;
	}

	// Sick bands lose people and pass the sickness on; once a year new sicknesses may arise.
	void Sicken(int Days)
	{
		static const int Medicine = SkillNamed("medicine"), Vaccines = SkillNamed("vaccines"), Antibiotics = SkillNamed("antibiotics"),
			Roads = SkillNamed("roads"), Cities = SkillNamed("cities");
		const double Year = Eco->Years();
		bool AnyDied = false;
		std::vector<std::pair<size_t, uint16_t>> Passed;
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			Camp& C = Camps[c];
			if (!C.Sick) continue;
			Disease& D = Diseases[C.Sick - 1u];
			const Polity* Home = C.State ? StateById(C.State) : nullptr;
			const double Cure = (Has(C.Known, Medicine) ? 0.75 : 1.0) * (Has(C.Known, Antibiotics) ? 0.4 : 1.0) * (Has(C.Known, Vaccines) ? 0.5 : 1.0) *
				(Home && Hospitals(*Home) ? 0.7 : 1.0);
			const double Toll = D.Deadliness * ResistanceTo(C, D.Id, Year) * Cure * std::min(1.0, static_cast<double>(Days) / D.Days);
			for (int i : C.Members)
			{
				Npc& P = Npcs[static_cast<size_t>(i)];
				const double Age = P.AgeYears();
				const double Frail = Age < 5.0 || Age > 60.0 ? 1.6 : (P.Energy < 0.3f ? 1.5 : 1.0); // The young, old and hungry die more.
				if (P.Alive && Dice.Chance(std::min(0.95, Toll * Frail)))
				{
					Die(P, 0);
					AnyDied = true;
					D.Killed += 1, D.KilledThisYear += 1, ++DiseaseDeaths;
				}
			}
			// It passes to bands nearby, along the state's roads to its capital, and with armies to the enemy.
			const double Pass = OverDays(0.02 * D.Spread * (Home && Sanitation(*Home) ? 0.4 : 1.0), Days); // Clean water and sewers stop much of it.
			for (int N : CampSlotsNear(C.Lat, C.Lon, Has(C.Known, Roads) ? 150.0 : 80.0))
				if (static_cast<size_t>(N) != c && Dice.Chance(Pass)) Passed.push_back({static_cast<size_t>(N), C.Sick});
			if (const Polity* S = C.State ? StateById(C.State) : nullptr)
			{
				auto It = CampSlots.find(S->Capital);
				if (It != CampSlots.end() && Dice.Chance(OverDays(0.01 * D.Spread, Days))) Passed.push_back({static_cast<size_t>(It->second), C.Sick});
				for (uint32_t T : S->Partners)
					if (const Polity* P = StateById(T))
					{
						auto Ip = CampSlots.find(P->Capital);
						if (Ip != CampSlots.end() && C.Id == S->Capital && Dice.Chance(OverDays(0.01 * D.Spread, Days))) Passed.push_back({static_cast<size_t>(Ip->second), C.Sick});
					}
				if (const Polity* E = StateById(S->Enemy))
				{
					auto Ie = CampSlots.find(E->Capital);
					if (Ie != CampSlots.end() && Dice.Chance(OverDays(0.005 * D.Spread, Days))) Passed.push_back({static_cast<size_t>(Ie->second), C.Sick});
				}
			}
			C.SickDays += Days;
			if (C.SickDays >= D.Days)
			{
				bool Found = false;
				for (auto& E : C.Exposed)
					if (E.first == C.Sick) E.second = static_cast<float>(Year), Found = true;
				if (!Found) C.Exposed.push_back({C.Sick, static_cast<float>(Year)});
				C.Sick = 0, C.SickDays = 0;
			}
		}
		for (const auto& [Slot, Id] : Passed) Catch(Camps[Slot], Id);
		if (AnyDied)
		{
			Npcs.erase(std::remove_if(Npcs.begin(), Npcs.end(), [](const Npc& P) { return !P.Alive; }), Npcs.end());
			IndexCamps();
		}

		DiseaseDays += Days;
		if (DiseaseDays < 365) return;
		DiseaseDays -= 365;
		// Great plagues: a sickness that killed one in fifty this year.
		const double Alive = std::max<double>(1.0, static_cast<double>(Npcs.size()));
		for (Disease& D : Diseases)
		{
			if (D.KilledThisYear >= 200 && static_cast<double>(D.KilledThisYear) > Alive * 0.02)
			{
				D.Waves += 1;
				char Pct[32];
				std::snprintf(Pct, sizeof(Pct), "%.1f%%", 100.0 * static_cast<double>(D.KilledThisYear) / (Alive + static_cast<double>(D.KilledThisYear)));
				AddEvent("A great plague: the sickness " + DiseaseName(D) + " kills " + std::to_string(D.KilledThisYear) + " people this year, " + Pct + " of the world");
			}
			D.KilledThisYear = 0;
		}
		// New sicknesses jump from kept animals, or arise in crowded cities.
		std::unordered_map<uint32_t, int> Kept;
		for (const AnimalGroup& G : Eco->Groups())
			if (G.Herder && G.Count >= 1.0) Kept[G.Herder] = G.SpeciesIndex;
		for (const auto& [Herder, Kind] : Kept)
		{
			auto It = CampSlots.find(static_cast<int>(Herder));
			if (It == CampSlots.end()) continue;
			const Camp& C = Camps[static_cast<size_t>(It->second)];
			const double Crowd = C.State ? 3.0 : 1.0;
			if (C.Sick || C.Members.size() < 10 || !Dice.Chance(1e-5 * Crowd) || Diseases.size() >= 60000) continue;
			NewDisease(static_cast<size_t>(It->second), Kind);
		}
		for (const Polity& S : States)
		{
			auto It = CampSlots.find(S.Capital);
			if (S.City && It != CampSlots.end() && !Camps[static_cast<size_t>(It->second)].Sick && Has(S.Known, Cities) && Dice.Chance(1e-4 * std::clamp(S.CapitalPeople / 500.0, 1.0, 10.0)) && Diseases.size() < 60000)
				NewDisease(static_cast<size_t>(It->second), -1);
		}
	}

	void NewDisease(size_t Slot, int Source)
	{
		const Camp& C = Camps[Slot];
		const uint16_t Id = StartDisease(Slot, Source, static_cast<float>(Dice.Range(0.03, 0.45)), static_cast<float>(Dice.Range(0.4, 2.0)),
			static_cast<int>(Dice.Range(30.0, 120.0)));
		const Disease& D = Diseases[Id - 1u];
		static const char* Lands[6] = {"Africa", "Asia", "Europe", "Australia", "the Americas", "elsewhere"};
		char Pct[24];
		std::snprintf(Pct, sizeof(Pct), "%.0f%%", D.Deadliness * 100.0);
		const std::string Where = std::string(" in ") + Lands[RegionOf(C.Lat, C.Lon)];
		if (Diseases.size() == 1)
			AddEvent("The first sickness to pass from band to band, " + DiseaseName(D) + ", appears" + Where + ". It kills up to " + Pct + " of those who catch it");
		else if (D.Deadliness >= 0.25f)
			AddEvent("A deadly new sickness, " + DiseaseName(D) + ", appears" + Where + ". It kills up to " + Pct + " of those who catch it");
	}

	// ---- States above the band (see Polities.h) -----------------------------------------------

	static int SkillNamed(const char* Name)
	{
		const auto& L = AllSkills();
		for (size_t k = 0; k < L.size(); ++k)
			if (L[k].Name == Name) return static_cast<int>(k);
		return -1;
	}
	static bool Has(SkillSet Known, int Skill) { return Skill >= 0 && Knows(Known, Skill); }

	// Camps within Km of a point, as slots into Camps.
	std::vector<int> CampSlotsNear(double Lat, double Lon, double Km) const
	{
		std::vector<int> Out;
		const int Rows = static_cast<int>(std::ceil(Km / kKmPerDegree));
		const int Cols = std::min(180, static_cast<int>(std::ceil(Km / (kKmPerDegree * std::max(0.05, std::cos(Lat * kPi / 180.0))))));
		const int R0 = GridRow(Lat), C0 = GridCol(Lon);
		for (int r = R0 - Rows; r <= R0 + Rows; ++r)
			for (int c = C0 - Cols; c <= C0 + Cols; ++c)
			{
				auto It = CampGrid.find(GridKey(r, c));
				if (It == CampGrid.end()) continue;
				for (int Slot : It->second)
					if (DistanceKm(Lat, Lon, Camps[Slot].Lat, Camps[Slot].Lon) < Km) Out.push_back(Slot);
			}
		std::sort(Out.begin(), Out.end()); // The grid is a hash map: keep the order fixed so the dice fall the same way.
		return Out;
	}
	static bool Shares(const Camp& C, uint32_t Faith) { return Faith && std::find(C.Faith.begin(), C.Faith.end(), Faith) != C.Faith.end(); }

	// Once a year: kingdoms with ocean-going ships send a band of settlers to a far shore. Where
	// they land among strangers, the sicknesses of home come with them, and the people there have
	// never met them.
	void Voyages()
	{
		static const int Ships = SkillNamed("sailing ships"), Compass = SkillNamed("compass"), Steam = SkillNamed("steam engines");
		for (size_t s = 0; s < States.size(); ++s)
		{
			Polity& S = States[s];
			if (S.Tier < Kingdom || !Has(S.Known, Ships) || !Dice.Chance(Has(S.Known, Compass) ? 0.04 : 0.02)) continue;
			Voyage(S, Has(S.Known, Steam) ? 20000.0 : Has(S.Known, Compass) ? 8000.0 : 3000.0);
		}
	}

	// One voyage from a state: true if settlers landed somewhere.
	bool Voyage(Polity& S, double Range)
	{
		// A shore band of the state, not its capital, sets sail.
		std::vector<int> Shore;
		for (size_t c = 0; c < Camps.size(); ++c)
			if (Camps[c].State == S.Id && Camps[c].Id != S.Capital && !Camps[c].Members.empty() && ShoreFood(Camps[c].Lat, Camps[c].Lon) > 0.0) Shore.push_back(static_cast<int>(c));
		const int From = Shore.empty() ? -1 : Shore[std::min(Shore.size() - 1, static_cast<size_t>(Dice.Uniform() * static_cast<double>(Shore.size())))];
		if (From < 0) return false;
		Camp& C = Camps[static_cast<size_t>(From)];
		const int Home = RegionOf(C.Lat, C.Lon);
		// A far shore where the band can live.
		double La = 0.0, Lo = 0.0;
		bool Found = false;
		for (int Tries = 0; Tries < 60 && !Found; ++Tries)
		{
			Offset(C.Lat, C.Lon, Dice.Uniform() * 2.0 * kPi, Range * (0.3 + 0.7 * Dice.Uniform()), La, Lo);
			Found = std::abs(La) < 70.0 && Eco->IsLand(La, Lo) && ShoreFood(La, Lo) > 0.0 && LivableFor(C.Kind, La, Lo, true) && DistanceKm(C.Lat, C.Lon, La, Lo) > 800.0;
		}
		if (!Found) return false;
		GridRemove(From);
		C.Lat = La, C.Lon = Lo, C.DaysHere = 0;
		GridAdd(From);
		++Colonies;
		const int There = RegionOf(La, Lo);
		const std::string Name = StateName(S), Land = PlaceNames[static_cast<size_t>(There)].empty() ? std::string("a new land") : PlaceNames[static_cast<size_t>(There)];
		if (!AnyVoyage) AnyVoyage = true, AddEvent("The first voyage across the open sea: settlers from " + Name + " sail to " + Land);
		// First contact: strangers already living near the landing.
		bool Strangers = false;
		for (int N : CampSlotsNear(La, Lo, 300.0))
			if (N != From && !Camps[static_cast<size_t>(N)].Members.empty() && Camps[static_cast<size_t>(N)].State != S.Id && !Understand(Camps[static_cast<size_t>(N)], C)) Strangers = true;
		if (!Strangers) return true;
		// The deadliest sickness that ever arose in the settlers' homeland comes ashore with them.
		const Disease* Worst = nullptr;
		for (const Disease& D : Diseases)
			if (RegionOf(D.Lat, D.Lon) == Home && (!Worst || D.Deadliness > Worst->Deadliness)) Worst = &D;
		std::string Text = "Settlers from " + Name + " land in " + Land + ", among people who have never met them";
		if (Worst && There != Home && !C.Sick)
		{
			C.Sick = Worst->Id, C.SickDays = 0;
			Text += "; they bring the sickness " + DiseaseName(*Worst) + " with them";
		}
		if (There != Home || S.Tier >= Empire) AddEvent(Text);
		return true;
	}

	// Once a year: earthquakes where the plates meet (with tsunamis on the coast), hurricanes in
	// the warm-sea belts, river floods and droughts strike the bands there. Cities suffer most from
	// quakes; warning systems and weather forecasts cut the toll in the computer age.
	void Disasters()
	{
		struct Zone { double La0, La1, Lo0, Lo1; };
		static const Zone Faults[] = {{30, 45, 130, 146}, {-10, 5, 95, 130}, {5, 20, 118, 127}, {-45, 5, -80, -68}, {10, 20, -105, -83}, {32, 42, -125, -115},
			{52, 62, -165, -140}, {34, 40, 26, 60}, {26, 36, 70, 95}, {36, 46, 10, 28}, {26, 40, 98, 106}, {-46, -35, 166, 179}};
		static const Zone Storms[] = {{10, 30, -98, -60}, {8, 30, 110, 150}, {12, 23, 80, 95}, {-25, -10, 110, 155}, {-25, -12, 35, 55}};
		static const int Computers = SkillNamed("computers"), Farming = SkillNamed("farming");
		auto Strike = [&](double La, double Lo, double Km, double Share, double Spoil, bool ShoreOnly, int Kind, const char* What)
		{
			int Dead = 0;
			const Polity* HitCity = nullptr;
			for (int N : CampSlotsNear(La, Lo, Km))
			{
				Camp& C = Camps[static_cast<size_t>(N)];
				if (C.Members.empty() || (ShoreOnly && ShoreFood(C.Lat, C.Lon) <= 0.0)) continue;
				Polity* S = C.State ? StateById(C.State) : nullptr;
				const bool InCity = S && S->City && S->Capital == C.Id;
				if (InCity) HitCity = S, S->Treasury *= Kind == 0 ? 0.7f : 0.9f;
				const double Rate = Share * (InCity && Kind == 0 ? 2.0 : 1.0) * (Has(C.Known, Computers) ? 0.3 : 1.0);
				for (int i : C.Members)
				{
					Npc& P = Npcs[static_cast<size_t>(i)];
					if (P.Health > 0.0f && Dice.Chance(Rate)) P.Health = 0.0f, ++Dead;
				}
				C.Store *= static_cast<float>(1.0 - Spoil);
			}
			DisasterDead += Dead;
			Struck[static_cast<size_t>(Kind)] += 1;
			char Buf[96];
			std::snprintf(Buf, sizeof(Buf), " near %.1f, %.1f kills %d", La, Lo, Dead);
			if (HitCity && HitCity->Tier >= Kingdom) AddEvent(std::string(What) + Buf + " and strikes the city of " + PersonName(HitCity->City) + " in " + StateName(*HitCity));
			else if (Dead >= 50 || (Dead > 0 && !(DisastersSeen & (1 << Kind)))) AddEvent(std::string(What) + Buf);
			if (Dead > 0) DisastersSeen |= 1 << Kind;
		};
		for (const Zone& Z : Faults)
		{
			if (!Dice.Chance(0.3)) continue;
			const double La = Z.La0 + Dice.Uniform() * (Z.La1 - Z.La0), Lo = Z.Lo0 + Dice.Uniform() * (Z.Lo1 - Z.Lo0);
			Strike(La, Lo, 100.0, 0.05, 0.3, false, 0, "An earthquake");
			if (ShoreFood(La, Lo) > 0.0 && Dice.Chance(0.3)) Strike(La, Lo, 500.0, 0.1, 0.5, true, 1, "A tsunami");
		}
		for (const Zone& Z : Storms)
			if (Dice.Chance(0.5)) Strike(Z.La0 + Dice.Uniform() * (Z.La1 - Z.La0), Z.Lo0 + Dice.Uniform() * (Z.Lo1 - Z.Lo0), 200.0, 0.02, 0.5, true, 2, "A hurricane");
		// River floods: farmers live on the rich flood plains.
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			const Camp& C = Camps[c];
			if (C.Members.empty() || !Has(C.Known, Farming) || !Dice.Chance(0.002) || Eco->RiverAt(C.Lat, C.Lon) < 0.5) continue;
			Strike(C.Lat, C.Lon, 30.0, 0.03, 0.6, false, 3, "A great river flood");
		}
		// Droughts: a season or more without rain somewhere on land.
		for (int k = 0; k < 3; ++k)
		{
			const double La = -40.0 + Dice.Uniform() * 100.0, Lo = -180.0 + Dice.Uniform() * 360.0;
			if (!Eco->IsLand(La, Lo) || !Dice.Chance(0.3)) continue;
			Strike(La, Lo, 500.0, 0.0, 0.7, false, 4, "A drought");
		}
	}

	// A state that has learned to write takes up the script of a state it trades with, or makes its own.
	void TakeScript(Polity& S)
	{
		static const int Writing = SkillNamed("writing");
		if (S.Script || !Has(S.Known, Writing)) return;
		for (uint32_t P : S.Partners)
			if (const Polity* T = StateById(P); T && T->Script && Dice.Chance(0.7))
			{
				S.Script = T->Script;
				return;
			}
		S.Script = static_cast<uint16_t>(S.Word * 389u + S.Id * 11u + 1u);
		std::string T = StateName(S) + " makes a script of its own, the " + PersonName(S.Script) + " letters";
		T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
		if (!AnyScript || S.Tier >= Kingdom) AddEvent(AnyScript ? T : "The first script: " + StateName(S) + " makes its own, the " + PersonName(S.Script) + " letters");
		AnyScript = true;
	}

	// Hospitals in a state's towns, once medicine and writing are known.
	static int Hospitals(const Polity& S)
	{
		static const int Medicine = SkillNamed("medicine"), Writing = SkillNamed("writing");
		return Has(S.Known, Medicine) && Has(S.Known, Writing) ? std::max(1, S.Towns) : 0;
	}
	// Clean water and sewers: cities with medicine and steam pumps.
	static bool Sanitation(const Polity& S)
	{
		static const int Medicine = SkillNamed("medicine"), Steam = SkillNamed("steam engines");
		return S.City && Has(S.Known, Medicine) && Has(S.Known, Steam);
	}

	// Whether a state has nuclear weapons: atomic power and rockets to carry them.
	static bool Nuclear(const Polity& S)
	{
		static const int Atom = SkillNamed("nuclear power"), Rockets = SkillNamed("rockets");
		return Has(S.Known, Atom) && Has(S.Known, Rockets);
	}
	// Two nuclear states seldom dare to fight.
	bool Deterred(const Polity& A, const Polity& B) { return Nuclear(A) && Nuclear(B) && !Dice.Chance(0.1); }
	// Members of the league of nations: states with the telegraph once the league exists.
	bool InLeague(const Polity& S) const
	{
		static const int Telegraph = SkillNamed("telegraph");
		return LeagueYear > 0.0 && S.Tier >= Kingdom && Has(S.Known, Telegraph);
	}

	// Once a year, the world as a whole: wars between alliances big enough to be world wars;
	// nuclear weapons, their use in war and their accidents; a league of nations once the
	// telegraph ties states together; the internet sharing all knowledge; and thinking machines.
	void GlobalAge()
	{
		static const int Telegraph = SkillNamed("telegraph"), Internet = SkillNamed("the internet"), AI = SkillNamed("artificial intelligence"), Engines = SkillNamed("engines");
		const double World = std::max(1, Alive());
		// World war: industrial states holding a third of the world's people fight at once.
		int Fighting = 0, Sides = 0;
		for (const Polity& S : States)
			if (S.Enemy && Has(S.Known, Engines)) Fighting += S.Members, ++Sides;
		const bool Big = Sides >= 4 && Fighting >= World / 3.0;
		if (Big && !InWorldWar)
		{
			InWorldWar = true, ++WorldWars;
			AddEvent("World war " + std::to_string(WorldWars) + ": " + std::to_string(Sides) + " industrial states, with " + std::to_string(Fighting) + " people, are at war at once");
		}
		else if (!Big && InWorldWar && Fighting < World / 6.0) InWorldWar = false, AddEvent("World war " + std::to_string(WorldWars) + " ends");
		// Nuclear weapons: the first, their use in a war, and accidents.
		for (Polity& S : States)
		{
			if (!Nuclear(S)) continue;
			if (!AnyBomb)
			{
				std::string T = StateName(S) + " builds the first nuclear weapon";
				T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
				AnyBomb = true, AddEvent(T);
			}
			Polity* E = StateById(S.Enemy);
			if (E && Dice.Chance(Nuclear(*E) ? 0.002 : 0.01))
			{
				AddEvent("Nuclear war: " + StateName(S) + " destroys the capital of " + StateName(*E) + (Nuclear(*E) ? ", and is destroyed in turn" : ""));
				Eco->Strike("A nuclear strike", E->Lat, E->Lon, 0.1, 3.0, 60.0, 0.0);
				if (Nuclear(*E)) Eco->Strike("A nuclear strike", S.Lat, S.Lon, 0.3, 5.0, 60.0, 0.0);
				++NuclearWars;
			}
			else if (Dice.Chance(0.0005)) AddEvent("A nuclear accident in " + StateName(S) + " poisons the land around it"), Eco->Strike("A nuclear accident", S.Lat, S.Lon, 0.0, 0.0, 15.0, 0.0);
		}
		// The league of nations.
		if (LeagueYear <= 0.0)
		{
			int Wired = 0;
			for (const Polity& S : States) Wired += S.Tier >= Kingdom && Has(S.Known, Telegraph);
			if (Wired >= 5) LeagueYear = Eco->Years(), AddEvent("The league of nations: " + std::to_string(Wired) + " states agree to settle quarrels by talking");
		}
		// The internet: once half the world is online, everyone online can learn what anyone online knows.
		SkillSet Online = 0;
		double OnlinePeople = 0.0;
		for (const Polity& S : States)
			if (Has(S.Known, Internet)) Online |= S.Known, OnlinePeople += S.Members;
		if (OnlinePeople >= World / 2.0)
		{
			if (!AnyWeb) AnyWeb = true, AddEvent("Half the world is online: anyone connected can learn what anyone connected knows");
			for (Polity& S : States)
				if (Has(S.Known, Internet)) S.Heard |= Online;
		}
		for (const Polity& S : States)
			if (Has(S.Known, AI) && !AnyAI) AnyAI = true, AddEvent("In " + StateName(S) + ", thinking machines begin to work beside people");
	}

	// Once a year: how long people live, and how many are born and die for every thousand alive.
	void Census()
	{
		const double Alive = std::max(1.0, static_cast<double>(Npcs.size()));
		if (DeathsThisYear > 0) LifeExpectancy = AgeAtDeath / static_cast<double>(DeathsThisYear); // shortcut: mean age at death, right for a steady population.
		BirthRate = BirthsAtCensus < 0 ? 0.0 : 1000.0 * static_cast<double>(Births - BirthsAtCensus) / Alive; // The first year after a start or a load is partial.
		DeathRate = 1000.0 * static_cast<double>(DeathsThisYear) / Alive;
		const bool Enough = DeathsThisYear >= 50 && BirthsAtCensus >= 0;
		AgeAtDeath = 0.0, DeathsThisYear = 0, BirthsAtCensus = Births;
		for (Polity& S : States)
		{
			auto A = StateAges.find(S.Id);
			if (A != StateAges.end() && A->second.second >= 5) S.LifeExpectancy = static_cast<float>(A->second.first / A->second.second);
			// Welfare: long lives, enough food, reading, a say in who rules, peace and fairness.
			const double Free = S.Gov == Democracy ? 1.0 : S.Gov == Republic ? 0.7 : S.Gov == Dictatorship ? 0.1 : 0.3;
			S.Welfare = static_cast<float>(std::clamp(0.3 * std::min(1.0, S.LifeExpectancy / 80.0) + 0.2 * std::min(1.0, 1.0 / std::max(0.2, static_cast<double>(S.Price))) +
				0.15 * S.Literacy + 0.15 * Free + 0.1 * (S.Enemy ? 0.0 : 1.0) + 0.1 / std::max(1.0f, S.Inequality), 0.0, 1.0));
		}
		StateAges.clear();
		if (NextRealMark < 0.0)
		{
			NextRealMark = 1e6;
			while (RealPopulation() >= NextRealMark) NextRealMark = NextRealMark < 1e9 ? NextRealMark * 10.0 : NextRealMark + 1e9;
		}
		for (const double Real = RealPopulation(); Real >= NextRealMark;)
		{
			char Buf[96];
			std::snprintf(Buf, sizeof(Buf), NextRealMark >= 1e9 ? "%.0f billion people in the world, on the real Earth's scale" : "%.0f million people in the world, on the real Earth's scale",
				NextRealMark >= 1e9 ? NextRealMark / 1e9 : NextRealMark / 1e6);
			AddEvent(Buf);
			NextRealMark = NextRealMark < 1e9 ? NextRealMark * 10.0 : NextRealMark + 1e9;
		}
		while (Enough && LifeExpectancy >= NextLifeMark)
		{
			AddEvent("People now live " + std::to_string(NextLifeMark) + " years on average");
			NextLifeMark += 10;
		}
	}

	// Once a year: states with writing open schools and, in their cities, libraries; once there are
	// universities, those too. Printing, then electricity, put a school within reach of every child.
	void Institutions(Polity& S)
	{
		static const int Writing = SkillNamed("writing"), Paper = SkillNamed("paper"), Printing = SkillNamed("printing"), Univ = SkillNamed("universities"),
			Power = SkillNamed("electricity");
		if (!Has(S.Known, Writing)) return;
		const std::string Name = StateName(S);
		// shortcut: the sim holds far fewer people than Earth did, so a school here stands for many; about 10 of the sim's pupils each.
		const int PerSchool = Has(S.Known, Power) ? 10 : Has(S.Known, Printing) ? 20 : Has(S.Known, Paper) ? 100 : 300;
		if (S.Schools < S.Members / PerSchool && Dice.Chance(0.3))
		{
			if (!S.Schools && !AnySchool) AnySchool = true, AddEvent("The first school in the world opens in " + Name + ", teaching children to read and write");
			++S.Schools;
		}
		else if (S.Schools > S.Members / PerSchool + 1) --S.Schools; // Too few pupils left to keep it open.
		if (S.City && S.Libraries < std::max(1, S.Towns) && Dice.Chance(0.02))
		{
			if (!AnyLibrary) AnyLibrary = true, AddEvent("The first library in the world: " + PersonName(S.City) + " in " + Name + " keeps its writings");
			++S.Libraries;
		}
		if (S.City && Has(S.Known, Univ) && S.Universities < std::max(1, S.Towns / 3) && Dice.Chance(0.03))
		{
			if (!AnyUniversity) AnyUniversity = true, AddEvent("The first university in the world opens in the city of " + PersonName(S.City) + " in " + Name);
			++S.Universities;
		}
		if (S.Libraries) S.Written |= S.Known;
	}

	// Clerks, scribes and officials a state keeps for each band: none without writing, more with paper, printing and computers.
	static double OfficialsPerBand(SkillSet Known)
	{
		static const int Writing = SkillNamed("writing"), Paper = SkillNamed("paper"), Printing = SkillNamed("printing"), Computers = SkillNamed("computers");
		if (!Has(Known, Writing)) return 0.0;
		return 1.0 + (Has(Known, Paper) ? 1.0 : 0.0) + (Has(Known, Printing) ? 1.0 : 0.0) + (Has(Known, Computers) ? 2.0 : 0.0);
	}

	// Once a year: how a state is ruled changes. Kingdoms crown monarchs; nobles curb them; with
	// laws, money and readers, citizens choose their rulers; with printing, everyone does; and in a
	// long war or hard times a general may seize power, until the people take it back.
	void Politics(Polity& S, const Camp& Seat)
	{
		static const int LawSkill = SkillNamed("laws"), Money = SkillNamed("money"), Printing = SkillNamed("printing"), Writing = SkillNamed("writing");
		const uint8_t Was = S.Gov;
		const double Year = Eco->Years();
		const bool Free = S.Gov == Republic || S.Gov == Democracy;
		if (S.Gov == Chieftain && S.Tier >= Kingdom) S.Gov = Monarchy;
		else if (S.Gov == Monarchy && S.Inequality > 3.0f && !S.Laws.empty() && Dice.Chance(0.01)) S.Gov = Aristocracy;
		else if ((S.Gov == Monarchy || S.Gov == Aristocracy) && S.City && Has(S.Known, LawSkill) && Has(S.Known, Money) && Has(S.Known, Writing) &&
			S.Literacy > 0.1f && Dice.Chance(0.005 * (1.0 + static_cast<double>(S.Laws.size()))))
			S.Gov = Republic;
		else if (S.Gov == Republic && Has(S.Known, Printing) && S.Literacy > 0.5f && Dice.Chance(0.02)) S.Gov = Democracy;
		else if (S.Gov == Dictatorship && Dice.Chance(S.Literacy > 0.6f ? 0.03 : 0.01)) S.Gov = S.Literacy > 0.6f ? Democracy : Monarchy;
		else if (S.Gov >= Monarchy && S.Gov != Dictatorship && Dice.Chance((S.WarYears > 3 ? 0.02 : 0.003) * (S.Gov == Democracy ? 0.3 : 1.0)))
		{
			S.Gov = Dictatorship;
			S.Coups += 1;
			// The general who takes power: the most trusted grown man or woman in the capital.
			const Npc* Strong = nullptr;
			for (int i : Seat.Members)
			{
				const Npc& P = Npcs[static_cast<size_t>(i)];
				if (P.Id != S.Ruler && P.AgeYears() >= 25.0 && P.AgeYears() < 60.0 && (!Strong || P.Reputation > Strong->Reputation)) Strong = &P;
			}
			if (Strong)
			{
				S.Ruler = Strong->Id, S.RulerName = Strong->Name, S.Rulers += 1;
				Remember(*Strong, "seized power over " + StateName(S));
			}
			if (S.Tier >= Kingdom) AddEvent((Strong ? PersonName(Strong->Name) + " seizes power in a coup: " : std::string("A coup: ")) + StateName(S) + " becomes a dictatorship");
		}
		S.Rule = S.Gov == Monarchy || S.Gov == Aristocracy || S.Gov == Chieftain ? 0 : 1;
		// Republics choose every few years, democracies every four.
		if (Free && Year - S.Elected >= (S.Gov == Democracy ? 4.0 : 5.0))
		{
			S.Elected = static_cast<int>(Year);
			const Npc* Best = nullptr;
			for (int i : Seat.Members)
			{
				const Npc& P = Npcs[static_cast<size_t>(i)];
				if (P.AgeYears() >= 30.0 && (!Best || P.Reputation > Best->Reputation)) Best = &P;
			}
			if (Best && Best->Id != S.Ruler) S.Ruler = Best->Id, S.RulerName = Best->Name, S.Rulers += 1, Remember(*Best, "was chosen to lead " + StateName(S));
		}
		if (S.Gov == Was) return;
		const int Bit = 1 << S.Gov;
		std::string T = StateName(S) + " becomes a " + GovName(S.Gov);
		if (S.Gov == Aristocracy) T = "The nobles of " + StateName(S) + " take a share of power from its ruler";
		T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
		if (!(GovSeen & Bit)) GovSeen |= Bit, AddEvent("The first " + std::string(GovName(S.Gov)) + " in the world: " + T);
		else if (S.Tier >= Empire && S.Gov != Dictatorship) AddEvent(T);
	}

	// Once a year: once printing spreads ideas, a state's people take up a political idea from what
	// they live through, or from those they trade with. Unequal states under kings or dictators
	// whose people read may rise in revolution. Printed holy books split faiths (reformations),
	// missionaries carry the state's faith to its partners, and faith against faith starts holy wars.
	void Ideas(Polity& S, const Camp& Seat)
	{
		static const int Printing = SkillNamed("printing"), Factories = SkillNamed("factories");
		const std::string Name = StateName(S);
		if (Has(S.Known, Printing) && Dice.Chance(0.05))
		{
			uint8_t Idea = S.Ideology;
			if (Eco->Greenhouse() > 1.0 && Dice.Chance(0.2)) Idea = Environmentalism;
			else if (Has(S.Known, Factories) && S.Inequality > 4.0f) Idea = Socialism;
			else if (S.WarYears > 5 || S.Conquests > 3) Idea = Militarism;
			else if (S.Tier == Nation) Idea = Dice.Chance(0.5) ? Nationalism : Liberalism;
			else if (S.Literacy > 0.3f) Idea = Liberalism;
			for (uint32_t P : S.Partners) // Ideas travel with trade.
				if (const Polity* T = StateById(P); T && T->Ideology && Dice.Chance(0.2)) Idea = T->Ideology;
			if (Idea != S.Ideology)
			{
				S.Ideology = Idea;
				const int Bit = 1 << Idea;
				if (!(IdeasSeen & Bit)) IdeasSeen |= Bit, AddEvent("A new idea spreads in " + Name + ": " + IdeologyName(Idea));
			}
		}
		// Revolution: the people overthrow those who rule them.
		const bool Unfree = S.Gov == Monarchy || S.Gov == Aristocracy || S.Gov == Dictatorship;
		if (Unfree && (S.Ideology == Liberalism || S.Ideology == Socialism || S.Ideology == Nationalism) && S.Literacy > 0.3f && S.Inequality > 3.0f && Dice.Chance(0.02))
			Revolt(S, Seat, nullptr);
		// Reformation: readers of printed holy books break with the state's faith.
		if (S.Faith && Has(S.Known, Printing) && S.Literacy > 0.2f && S.Bands >= 10 && Dice.Chance(0.003))
		{
			int Left = 0;
			for (Camp& C : Camps)
				if (C.State == S.Id && C.Id != S.Capital && Dice.Chance(0.3))
				{
					auto F = std::find(C.Faith.begin(), C.Faith.end(), S.Faith);
					if (F != C.Faith.end()) C.Faith.erase(F), ++Left;
				}
			if (Left) AddEvent("A reformation in " + Name + ": " + std::to_string(Left) + " bands break with its state faith");
		}
		// Missionaries carry the faith along the trade routes.
		if (S.Faith)
			for (uint32_t P : S.Partners)
				if (Polity* T = StateById(P); T && T->Faith != S.Faith && Dice.Chance(0.03))
				{
					auto It = CampSlots.find(T->Capital);
					if (It == CampSlots.end()) continue;
					Camp& Them = Camps[static_cast<size_t>(It->second)];
					if (Them.Faith.size() < 3 && !Shares(Them, S.Faith)) Them.Faith.push_back(S.Faith);
					if (!T->Faith || Dice.Chance(0.2))
					{
						T->Faith = S.Faith;
						if (T->Tier >= Kingdom || !AnyMission) AddEvent("Missionaries from " + Name + " convert " + StateName(*T) + " to its faith");
						AnyMission = true;
					}
				}
		// Holy war against a neighbour of another faith.
		if (S.Faith && !S.Enemy && S.Tier >= Kingdom && Dice.Chance(0.01))
			if (Polity* O = NearestOther(S); O && O->Faith && O->Faith != S.Faith && !O->Enemy && !Deterred(S, *O) && std::find(S.Allies.begin(), S.Allies.end(), O->Id) == S.Allies.end())
			{
				AddEvent("A holy war: " + Name + " fights " + StateName(*O) + " over their gods");
				StartWar(S, *O);
			}
	}

	// Once a year: food prices from stores and coins; states with money mint coins (more in war,
	// which drives prices up), borrow for wars once they can bank, and default when debts grow too
	// big. Conquering farm states take slaves until ideas of rights abolish it. When food is dear the
	// treasury feeds the hungriest bands.
	void Economy(Polity& S)
	{
		static const int Money = SkillNamed("money"), Maths = SkillNamed("mathematics"), Farming = SkillNamed("farming");
		double Food = 0.0;
		std::vector<Camp*> Mine;
		for (Camp& C : Camps)
			if (C.State == S.Id) Food += C.Store, Mine.push_back(&C);
		if (Mine.empty()) return;
		const double PerHead = Food / std::max(1, S.Members);
		const bool Coined = Has(S.Known, Money), Banks = Coined && Has(S.Known, Maths) && S.City;
		if (Coined) S.Coins += (S.Enemy ? 0.1 : 0.02) * S.Treasury * 0.01; // Rulers pay soldiers by minting.
		const double MoneyLevel = 1.0 + S.Coins / (std::max(1, S.Members) * 50.0);
		S.Price = static_cast<float>(std::clamp(30.0 / (PerHead + 5.0) * MoneyLevel, 0.05, 1000.0));
		const std::string Name = StateName(S);
		if (S.Price > 3.0f && S.LastPrice <= 3.0f && S.Tier >= Kingdom) AddEvent("Famine in " + Name + ": food costs " + std::to_string(static_cast<int>(S.Price)) + " times what it does in good years");
		// Relief from the treasury when food is dear.
		if (S.Price > 2.0f && S.Treasury > 0.0f)
		{
			std::sort(Mine.begin(), Mine.end(), [](const Camp* A, const Camp* B) { return A->Store / (A->Members.size() + 1.0f) < B->Store / (B->Members.size() + 1.0f); });
			for (size_t k = 0; k < std::min<size_t>(3, Mine.size()) && S.Treasury > 0.0f; ++k)
			{
				const float Give = std::min(S.Treasury, 2.0f * static_cast<float>(Mine[k]->Members.size()));
				Mine[k]->Store += Give, S.Treasury -= Give;
			}
		}
		S.LastPrice = S.Price;
		// Debts: wars are paid for on credit, repaid from the treasury.
		if (Banks && S.Enemy) S.Debt += S.Members * 0.5;
		const float Repay = static_cast<float>(std::min<double>(S.Debt, 0.05 * S.Treasury));
		S.Debt -= Repay, S.Treasury -= Repay;
		if (S.Debt > 20.0 * std::max(1, S.Members))
		{
			std::string T = Name + " cannot pay its debts and defaults";
			T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
			if (S.Tier >= Kingdom) AddEvent(T);
			S.Debt = 0.0, S.Wealth *= 0.5, S.Partners.clear();
		}
		// Slavery and its end.
		if (!S.Slavery && !S.Abolished && S.Conquests > 0 && Has(S.Known, Farming) && S.Ideology != Liberalism && Dice.Chance(0.05))
		{
			S.Slavery = true;
			if (!AnySlavery || S.Tier >= Empire) AddEvent(Name + " puts the people it conquered to forced work: slavery");
			AnySlavery = true;
		}
		if (S.Slavery && (S.Ideology == Liberalism || S.Gov == Democracy || S.Revolutions > 0) && S.Literacy > 0.3f && Dice.Chance(0.05))
		{
			S.Slavery = false, S.Abolished = true;
			std::string T = Name + " abolishes slavery" + (AnyAbolition ? "" : ", the first state to do so");
			T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
			AddEvent(T);
			AnyAbolition = true;
		}
	}

	// The people rise and overthrow their rulers; the state becomes a republic, or a dictatorship of
	// the revolutionaries, and the rich bands' stores are shared out.
	void Revolt(Polity& S, const Camp& Seat, const Npc* Leader)
	{
		if (!Leader)
			for (int i : Seat.Members)
			{
				const Npc& P = Npcs[static_cast<size_t>(i)];
				if (P.Id != S.Ruler && P.AgeYears() >= 18.0 && P.AgeYears() < 45.0 && (!Leader || P.Reputation > Leader->Reputation)) Leader = &P;
			}
		const std::string Old = StateName(S);
		S.Gov = S.Ideology == Socialism ? Dictatorship : Republic;
		S.Revolutions += 1;
		if (Leader) S.Ruler = Leader->Id, S.RulerName = Leader->Name, S.Rulers += 1, Remember(*Leader, "led the revolution in " + Old);
		double Total = 0.0;
		int N = 0;
		for (Camp& C : Camps)
			if (C.State == S.Id) Total += C.Store, ++N;
		for (Camp& C : Camps)
			if (C.State == S.Id) C.Store = static_cast<float>(0.5 * C.Store + 0.5 * Total / std::max(1, N));
		std::string T = "Revolution in " + Old + (Leader ? ", led by " + PersonName(Leader->Name) : std::string()) + ": the rulers are overthrown and it becomes a " + GovName(S.Gov);
		if (!AnyRevolution || S.Tier >= Kingdom) AddEvent(T);
		AnyRevolution = true;
	}

	// The books a state's people can read: its own libraries', and with printing, those of the states it trades with.
	SkillSet PrintedFor(const Polity& S) const
	{
		static const int Printing = SkillNamed("printing");
		SkillSet Books = S.Written;
		if (Has(S.Known, Printing))
			for (uint32_t P : S.Partners)
				if (const Polity* T = StateById(P)) Books |= T->Written;
		return Books;
	}

	// Once a year: states that read the sky keep a calendar, their faith gets a great festival, and
	// a rich treasury raises temples and monuments in the capital.
	void Culture(Polity& S)
	{
		static const int Astronomy = SkillNamed("astronomy"), Writing = SkillNamed("writing");
		const std::string Name = StateName(S);
		if (!S.Calendar && Has(S.Known, Astronomy) && Has(S.Known, Writing))
		{
			S.Calendar = true;
			if (!AnyCalendar) AnyCalendar = true, AddEvent("The first calendar: the sky-watchers of " + Name + " write down a year of 365 days");
		}
		if (S.Faith && !S.Festival && Dice.Chance(0.05))
		{
			const Camp* Seat = nullptr;
			if (auto It = CampSlots.find(S.Capital); It != CampSlots.end()) Seat = &Camps[static_cast<size_t>(It->second)];
			S.Festival = static_cast<uint16_t>((Seat ? Seat->Words[5] : 1u) * 131u + S.Id * 7u + 1u);
			if (!AnyFestival || S.Tier >= Kingdom)
			{
				std::string T = Name + " holds its first festival of " + PersonName(S.Festival) + " for its gods";
				T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
				AddEvent(T);
			}
			AnyFestival = true;
		}
		const float Cost = 2000.0f * static_cast<float>(S.Monuments + 1);
		if (S.City && S.Treasury > Cost && Dice.Chance(0.1))
		{
			S.Treasury -= Cost;
			++S.Monuments;
			static const char* Kinds[] = {"a great temple", "a tomb for its rulers", "a tall monument", "a palace", "great walls"};
			const std::string What = Kinds[(S.Id + S.Monuments) % 5];
			if (!AnyMonument || S.Tier >= Kingdom)
			{
				std::string T = Name + " raises " + What + " in the city of " + PersonName(S.City) + (AnyMonument ? "" : ", the first great building in the world");
				T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
				AddEvent(T);
			}
			AnyMonument = true;
		}
	}

	// How much likelier than others someone in a camp is to find new things: those who can read,
	// and above all those near libraries and universities. Shares out the world's finds; the pace
	// stays history's.
	double Scholarship(const Camp& C) const
	{
		const Polity* S = C.State ? StateById(C.State) : nullptr;
		return S ? 1.0 + 2.0 * S->Literacy + (S->Libraries ? 0.5 : 0.0) + (S->Universities ? 2.0 : 0.0) : 1.0;
	}

	// Once a year: fields farmed year after year wear out (faster where they are watered, as salt
	// builds up; slower with herds' dung and far slower with fertilizers), and rest back when left.
	// Farming forest clears it. A state whose fields are worn out may collapse.
	void WearLand()
	{
		static const int Farming = SkillNamed("farming"), Irrigation = SkillNamed("irrigation"), Herding = SkillNamed("herding"), Fertilizers = SkillNamed("fertilizers");
		std::unordered_set<int64_t> Farmed;
		std::unordered_map<uint32_t, std::pair<double, int>> StateWear;
		for (const Camp& C : Camps)
		{
			if (!Has(C.Known, Farming) || C.Members.empty()) continue;
			const int64_t Key = KeyOf(C.Lat, C.Lon);
			if (!Farmed.insert(Key).second) continue;
			auto [It, New] = Worn.emplace(Key, 0.0);
			if (New && Eco->PlantCover(C.Lat, C.Lon) > 0.6) ForestCleared += 1;
			const double Wear = 0.0005 * (Has(C.Known, Irrigation) ? 2.0 : 1.0) * (Has(C.Known, Herding) ? 0.6 : 1.0) * (Has(C.Known, Fertilizers) ? 0.2 : 1.0);
			It->second = std::min(0.5, It->second + Wear);
			if (C.State) StateWear[C.State].first += It->second, StateWear[C.State].second += 1;
		}
		for (auto It = Worn.begin(); It != Worn.end();)
		{
			if (!Farmed.count(It->first)) It->second -= 0.02;
			It = It->second <= 0.0 ? Worn.erase(It) : std::next(It);
		}
		for (Polity& S : States)
		{
			auto W = StateWear.find(S.Id);
			if (W == StateWear.end() || W->second.first / W->second.second < 0.35 || !Dice.Chance(0.05)) continue;
			for (Camp& C : Camps)
				if (C.State == S.Id) C.State = 0;
			S.Conquests = -1; // Not a plain fall: told here.
			std::string T = StateName(S) + " collapses: its worn-out fields can no longer feed its people";
			T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
			if (S.Tier >= Kingdom || !AnyCollapse) AddEvent(T);
			AnyCollapse = true;
		}
	}

	// Tonnes of CO2 a year each person in a state burns, from steam to engines and power stations;
	// clean power cuts it.
	static double FuelPerHead(SkillSet Known)
	{
		static const std::pair<int, double> Burners[] = {{SkillNamed("steam engines"), 0.5}, {SkillNamed("factories"), 1.0}, {SkillNamed("railways"), 0.5},
			{SkillNamed("engines"), 1.5}, {SkillNamed("electricity"), 1.5}, {SkillNamed("flight"), 0.5}, {SkillNamed("computers"), 0.5}};
		static const std::pair<int, double> Clean[] = {{SkillNamed("solar power"), 0.6}, {SkillNamed("nuclear power"), 0.8}, {SkillNamed("fusion power"), 0.1}};
		double T = 0.0;
		for (const auto& [K, More] : Burners) T += Has(Known, K) ? More : 0.0;
		for (const auto& [K, Times] : Clean) T *= Has(Known, K) ? Times : 1.0;
		return T;
	}

	// Once a year: industry burns coal, oil and gas, warming the world, until the fuel runs low.
	void BurnFuel()
	{
		double Tonnes = 0.0;
		for (const Polity& S : States) Tonnes += S.RealPeople * FuelPerHead(S.Known); // Real-scale people, so a world of factories burns what ours does.
		const double Left = std::max(0.0, 1.0 - Eco->FuelBurnedGt() / 18000.0); // About 18,000 Gt of CO2 in all the coal, oil and gas there is.
		const double Gt = Tonnes / 1e9 * std::min(1.0, 4.0 * Left);
		FuelThisYear = Gt;
		if (Gt <= 0.0) return;
		Eco->Burn(Gt);
		char Buf[160];
		while (Eco->Greenhouse() >= NextWarmMark)
		{
			std::snprintf(Buf, sizeof(Buf), "Burning fuel has warmed the world by %d degrees: the air holds %.0f ppm of CO2", NextWarmMark, Eco->Co2Ppm());
			AddEvent(Buf);
			++NextWarmMark;
		}
		if (Left < 0.25 && !FuelRunningOut) FuelRunningOut = true, AddEvent("The world's coal, oil and gas are running out");
	}

	// How deadly the weapons in a set of skills are: 1 for spears, more for bronze, iron, guns, engines and planes.
	static double WeaponsOf(SkillSet Known)
	{
		static const std::pair<int, double> Arms[] = {{SkillNamed("bows and arrows"), 0.3}, {SkillNamed("bronze"), 0.3}, {SkillNamed("iron"), 0.4},
			{SkillNamed("gunpowder"), 1.0}, {SkillNamed("engines"), 1.5}, {SkillNamed("flight"), 1.5}, {SkillNamed("rockets"), 1.0}};
		double W = 1.0;
		for (const auto& [K, More] : Arms) W += Has(Known, K) ? More : 0.0;
		return W;
	}

	// Keeps a year's fighting, and tells the history of sieges and the deadliest battles.
	void NoteBattle(const Battle& B, bool Big)
	{
		Battles.push_back(B);
		if (Battles.size() > 2000) Battles.erase(Battles.begin(), Battles.begin() + 1000);
		const int Dead = B.WinnerDead + B.LoserDead;
		std::string Where = (B.Siege ? "The siege of " : "The battle of ") + PersonName(B.Place);
		if (Dead > DeadliestBattle && Dead >= 20)
		{
			DeadliestBattle = Dead;
			AddEvent(Where + ", the deadliest yet: " + B.Winner + " defeats " + B.Loser + "; " + std::to_string(Dead) + " killed" +
				(B.Refugees ? ", " + std::to_string(B.Refugees) + " flee" : std::string()));
		}
		else if (Big && B.Siege)
			AddEvent(Where + ": " + B.Winner + " besieges " + B.Loser + "'s capital; " + std::to_string(Dead) + " killed");
	}

	// How much fighting a state can field: its people, armed with what any of them knows how to make.
	double StrengthOf(const Polity& S) const
	{
		static const std::vector<int> Arms = []
		{
			std::vector<int> A;
			for (const char* N : {"bows and arrows", "bronze", "the wheel", "iron", "sailing ships", "gunpowder", "engines", "flight", "rockets", "computers"})
				A.push_back(SkillNamed(N));
			return A;
		}();
		double Armed = 1.0;
		for (int K : Arms) Armed += Has(S.Known, K) ? 0.4 : 0.0;
		double Friends = 0.0;
		for (uint32_t A : S.Allies)
			if (const Polity* F = StateById(A)) Friends += 0.5 * F->Members;
		return (S.Members + Friends) * Armed * (S.City ? 1.2 : 1.0);
	}

	void NoteState(const Polity& S, const std::string& Text, bool Always = false)
	{
		const int Bit = 1 << S.Tier;
		if (!(TiersSeen & Bit))
		{
			TiersSeen = static_cast<uint8_t>(TiersSeen | Bit);
			std::string T = StateName(S);
			AddEvent("The first " + std::string(TierName(S.Tier)) + " in the world: " + T + ", with " + std::to_string(S.Bands) + " bands and " +
				std::to_string(S.Members) + " people" + (S.Ruler ? ", ruled by " + PersonName(S.RulerName) : std::string()));
			if (const Npc* R = Find(S.Ruler)) Remember(*R, "ruled " + T + ", the first " + TierName(S.Tier) + " in the world");
			return;
		}
		if (Always || S.Tier >= Kingdom) AddEvent(Text);
	}

	Polity& FoundState(int Slot, const Npc* Ruler, uint8_t Tier)
	{
		Camp& C = Camps[static_cast<size_t>(Slot)];
		Polity S;
		S.Id = NextStateId++;
		S.Word = static_cast<uint16_t>(C.Words[3] * 131u + C.Words[5] * 17u + S.Id * 7919u);
		S.Tier = Tier;
		S.Rule = Dice.Chance(0.6) ? 0 : 1;
		S.Capital = C.Id;
		S.Founded = Eco->Years();
		S.Lat = C.Lat, S.Lon = C.Lon;
		if (Ruler) S.Ruler = Ruler->Id, S.RulerName = Ruler->Name, S.Founder = Ruler->Id;
		S.Faith = C.Faith.empty() ? 0 : C.Faith.front();
		S.Bands = 1;
		S.Members = static_cast<int>(C.Members.size());
		S.Known = C.Known;
		C.State = S.Id;
		++StatesFounded;
		States.push_back(S);
		return States.back();
	}

	// The camps of each state, as slots, and its size and knowledge, from scratch.
	std::unordered_map<uint32_t, std::vector<int>> TallyStates()
	{
		std::unordered_map<uint32_t, std::vector<int>> Of;
		for (Polity& S : States) S.Bands = S.Members = 0, S.Known = 0;
		std::unordered_map<uint32_t, std::pair<float, float>> Rich; // Least and most food put by per head.
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			Camp& C = Camps[c];
			if (!C.State) continue;
			Polity* S = StateById(C.State);
			if (!S)
			{
				C.State = 0;
				continue;
			}
			if (C.Members.empty()) continue;
			S->Bands += 1;
			S->Members += static_cast<int>(C.Members.size());
			S->Known |= C.Known;
			Of[S->Id].push_back(static_cast<int>(c));
			const float PerHead = C.Store / static_cast<float>(C.Members.size());
			auto R = Rich.emplace(S->Id, std::make_pair(PerHead, PerHead));
			R.first->second.first = std::min(R.first->second.first, PerHead), R.first->second.second = std::max(R.first->second.second, PerHead);
		}
		for (Polity& S : States)
		{
			auto R = Rich.find(S.Id);
			S.Inequality = R == Rich.end() ? 1.0f : (R->second.second + 1.0f) / (R->second.first + 1.0f);
			S.Fertility = 1.0f; // Set with the cities below.
			S.Literacy = std::min(0.95f, static_cast<float>(S.Schools) * 10.0f / static_cast<float>(std::max(1, S.Members)));
			// Its cities: the share of its people who live in towns grows with what feeds and moves them.
			S.RealPeople = S.Members * RealPerPerson(S.Known);
			S.CityPeople = S.CapitalPeople = S.Towns = 0;
			if (!S.City) continue;
			static const std::pair<int, double> Urbanizing[] = {{SkillNamed("cities"), 0.05}, {SkillNamed("money"), 0.02}, {SkillNamed("roads"), 0.02},
				{SkillNamed("irrigation"), 0.01}, {SkillNamed("printing"), 0.02}, {SkillNamed("factories"), 0.08}, {SkillNamed("railways"), 0.07},
				{SkillNamed("electricity"), 0.08}, {SkillNamed("fertilizers"), 0.06}, {SkillNamed("engines"), 0.05}, {SkillNamed("computers"), 0.05},
				{SkillNamed("the internet"), 0.03}};
			double Share = 0.03;
			for (const auto& [Skill, More] : Urbanizing)
				if (Has(S.Known, Skill)) Share += More;
			S.Towns = std::max(1, S.Bands / 6);
			S.CityPeople = static_cast<int>(S.Members * Share);
			S.CapitalPeople = S.Towns == 1 ? S.CityPeople : static_cast<int>(S.CityPeople * 0.3);
			// The demographic transition: where most read and many live in towns, families get smaller.
			S.Fertility = static_cast<float>(1.0 - 0.65 * S.Literacy * std::min(1.0, 2.0 * Share));
		}
		return Of;
	}

	// A band, and those near it that follow it, break away from their state into one of their own.
	void BreakAway(int Slot, const Npc* Leader, const std::string& Why)
	{
		Camp& C = Camps[static_cast<size_t>(Slot)];
		Polity* Old = StateById(C.State);
		if (!Old) return;
		const std::string OldName = StateName(*Old);
		const uint32_t OldId = Old->Id;
		const uint8_t Tier = static_cast<uint8_t>(Old->Tier >= Empire ? Kingdom : Chiefdom);
		const bool Loud = Old->Tier >= Kingdom;
		const double Lat = C.Lat, Lon = C.Lon;
		const uint32_t NewId = FoundState(Slot, Leader, Tier).Id;
		for (int N : CampSlotsNear(Lat, Lon, 80.0))
			if (Camps[static_cast<size_t>(N)].State == OldId && Camps[static_cast<size_t>(N)].Id != StateById(OldId)->Capital && Dice.Chance(0.5))
				Camps[static_cast<size_t>(N)].State = NewId;
		Polity& New = *StateById(NewId);
		if (Loud || Leader)
		{
			const std::string Text = (Leader ? PersonName(Leader->Name) + " " + Why : Why) + ": " + StateName(New) + " breaks away from " + OldName;
			NoteState(New, Text, Loud);
			if (Leader) Remember(*Leader, Why + " and founded " + StateName(New) + ", breaking away from " + OldName);
		}
	}

	// Once a year: states gather tribute, crown rulers, grow, go to war, break up and fall, and new
	// ones rise where bands that farm or live in villages draw together.
	void Govern(int Days)
	{
		StateDays += Days;
		if (StateDays < 365) return;
		StateDays -= 365;
		Census();
		Disasters();
		static const int Villages = SkillNamed("villages"), Farming = SkillNamed("farming"), Writing = SkillNamed("writing"),
			Cities = SkillNamed("cities"), LawSkill = SkillNamed("laws"), Money = SkillNamed("money"), Roads = SkillNamed("roads"),
			Printing = SkillNamed("printing");
		auto Of = TallyStates();
		const double Year = Eco->Years();
		for (size_t s = 0; s < States.size(); ++s)
		{
			Polity& S = States[s];
			std::vector<int>& Mine = Of[S.Id];
			if (Mine.empty()) continue;
			// The capital: the ruler's camp, or the biggest left if it has gone.
			int Capital = -1;
			auto It = CampSlots.find(S.Capital);
			if (It != CampSlots.end() && Camps[static_cast<size_t>(It->second)].State == S.Id && !Camps[static_cast<size_t>(It->second)].Members.empty())
				Capital = It->second;
			if (Capital < 0)
			{
				Capital = Mine.front();
				for (int c : Mine)
					if (Camps[static_cast<size_t>(c)].Members.size() > Camps[static_cast<size_t>(Capital)].Members.size()) Capital = c;
				S.Capital = Camps[static_cast<size_t>(Capital)].Id;
				S.City = 0;
			}
			Camp& Seat = Camps[static_cast<size_t>(Capital)];
			S.Lat = Seat.Lat, S.Lon = Seat.Lon;
			// A new ruler when the old one dies or leaves.
			int Split = -1; // A band that breaks away at the end of this state's turn.
			const char* Why = "";
			const Npc* R = Find(S.Ruler);
			if (!R || Camps[static_cast<size_t>(CampSlot(R->Camp))].State != S.Id)
			{
				const Npc* Next = nullptr;
				if (S.Rule == 0 && S.Ruler)
					for (int c : Mine)
						for (int i : Camps[static_cast<size_t>(c)].Members)
						{
							const Npc& P = Npcs[static_cast<size_t>(i)];
							if ((P.Mother == S.Ruler || P.Father == S.Ruler) && P.AgeYears() >= 14.0 && (!Next || P.AgeDays > Next->AgeDays)) Next = &P;
						}
				// Without an heir, or now and then anyway, a big state fights over who rules, and splits.
				if (S.Bands >= 6 && Mine.size() >= 6 && S.Gov != Republic && S.Gov != Democracy && ((S.Rule == 0 && !Next) || Dice.Chance(0.04)))
				{
					int Far = -1;
					double FarKm = 0.0;
					for (int c : Mine)
					{
						const Camp& C = Camps[static_cast<size_t>(c)];
						const double Km = DistanceKm(C.Lat, C.Lon, Seat.Lat, Seat.Lon);
						if (c != Capital && C.Leader && Km > FarKm) Far = c, FarKm = Km;
					}
					if (Far >= 0) Split = Far, Why = S.Bands >= 20 ? "starts a civil war and claims the throne" : "claims the throne";
					if (Far >= 0 && S.Bands >= 20) S.CivilWars += 1;
				}
				if (!Next) Next = Find(Seat.Leader);
				if (!Next)
					for (int i : Seat.Members)
						if (Npcs[static_cast<size_t>(i)].AgeYears() >= 16.0 && (!Next || Npcs[static_cast<size_t>(i)].AgeDays > Next->AgeDays)) Next = &Npcs[static_cast<size_t>(i)];
				S.Ruler = Next ? Next->Id : 0;
				S.RulerName = Next ? Next->Name : 0;
				S.Rulers += 1;
				if (Next && S.Tier >= Empire)
					AddEvent(PersonName(Next->Name) + " becomes ruler number " + std::to_string(S.Rulers) + " of " + StateName(S));
			}
			// Tribute flows to the capital; the state's gods become everyone's.
			for (int c : Mine)
			{
				Camp& C = Camps[static_cast<size_t>(c)];
				if (c == Capital) continue;
				const float Paid = TaxRate(S.Gov) * (S.Slavery ? 1.2f : 1.0f) * C.Store;
				C.Store -= Paid;
				Seat.Store += 0.8f * Paid; // Some is lost on the way.
				S.Treasury += Paid;
				if (S.Faith && !Shares(C, S.Faith) && C.Faith.size() < 3 && Dice.Chance(0.1)) C.Faith.push_back(S.Faith);
				// The capital's speech spreads through a kingdom: a word at a time, bands far off come to speak it.
				if (S.Tier >= Kingdom && C.Kind == Seat.Kind && Dice.Chance(0.2))
				{
					const int w = static_cast<int>(Dice.Next() % kWords);
					C.Words[static_cast<size_t>(w)] = Seat.Words[static_cast<size_t>(w)];
				}
			}
			if (!S.Faith && !Seat.Faith.empty()) S.Faith = Seat.Faith.front();
			if (S.Bands == 1 && S.Tier == Chiefdom && Split < 0 && Dice.Chance(0.2)) // One band alone is no chiefdom for long.
			{
				Seat.State = 0;
				continue;
			}
			// Growing up: more bands, writing, cities, laws and money.
			const std::string Was = StateName(S);
			if (S.Tier == Chiefdom && S.Bands >= 10 && (Has(S.Known, Writing) || Has(S.Known, Cities))) S.Tier = Kingdom;
			else if (S.Tier == Kingdom && S.Bands >= 40 && S.Conquests >= 1 && Has(S.Known, LawSkill) && Has(S.Known, Money)) S.Tier = Empire;
			else if ((S.Tier == Kingdom || S.Tier == Empire) && S.Bands >= 20 && Has(S.Known, Printing)) S.Tier = Nation;
			if (StateName(S) != Was)
			{
				std::string T = Was + " grows into " + StateName(S) + ", with " + std::to_string(S.Bands) + " bands and " + std::to_string(S.Members) + " people";
				T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
				NoteState(S, T);
			}
			if (!S.City && Has(Seat.Known, Cities))
			{
				S.City = static_cast<uint16_t>(Seat.Words[2] * 977u + S.Id * 31u + 1u);
				NoteState(S, PersonName(S.RulerName) + " makes the city of " + PersonName(S.City) + " the capital of " + StateName(S));
			}
			Institutions(S);
			TakeScript(S);
			Culture(S);
			Politics(S, Seat);
			Ideas(S, Seat);
			Economy(S);
			// Bands too far from the capital, or who no longer speak its tongue or share its gods, may break away.
			const double Reach = ReachKm(S.Tier, Has(S.Known, Roads));
			const double Order = 1.0 / (1.0 + 0.3 * static_cast<double>(S.Laws.size()) + 0.15 * OfficialsPerBand(S.Known));
			for (int c : Mine)
			{
				if (c == Capital) continue;
				Camp& C = Camps[static_cast<size_t>(c)];
				const double Km = DistanceKm(C.Lat, C.Lon, Seat.Lat, Seat.Lon);
				if (Km > Reach * 1.3 && Dice.Chance(0.2))
				{
					C.State = 0;
					continue;
				}
				const double Risk = Order * (0.002 + (Understand(C, Seat) ? 0.0 : 0.01) + (S.Faith && !Shares(C, S.Faith) ? 0.01 : 0.0) + (Km > Reach ? 0.02 : 0.0));
				if (Split < 0 && C.Leader && C.Members.size() >= 15 && Dice.Chance(Risk)) Split = c, Why = "leads a revolt";
			}
			if (Split >= 0)
			{
				BreakAway(Split, Find(Camps[static_cast<size_t>(Split)].Leader), Why);
				break; // The list of states changed; the rest take their turn next year.
			}
		}

		// Wars: each year of fighting, the weaker side tends to lose its nearest bands to the stronger;
		// lose too many, or the capital, and it is conquered.
		Of = TallyStates();
		for (size_t s = 0; s < States.size(); ++s)
		{
			Polity& A = States[s];
			Polity* B = StateById(A.Enemy);
			if (!B || B->Enemy != A.Id || A.Bands == 0 || B->Bands == 0)
			{
				A.Enemy = 0, A.WarYears = 0;
				continue;
			}
			if (A.Id > B->Id) continue; // Each war is fought once a year, by the side with the lower id.
			A.WarYears += 1, B->WarYears += 1;
			const double SA = StrengthOf(A), SB = StrengthOf(*B);
			const bool AWins = Dice.Chance(SA / std::max(1.0, SA + SB));
			Polity& Win = AWins ? A : *B;
			Polity& Lose = AWins ? *B : A;
			std::vector<int>& Lost = Of[Lose.Id];
			std::sort(Lost.begin(), Lost.end(), [&](int X, int Y)
			{
				const Camp& P = Camps[static_cast<size_t>(X)];
				const Camp& Q = Camps[static_cast<size_t>(Y)];
				return DistanceKm(P.Lat, P.Lon, Win.Lat, Win.Lon) < DistanceKm(Q.Lat, Q.Lon, Win.Lat, Win.Lon);
			});
			size_t Taken = std::min(Lost.size(), static_cast<size_t>(1 + Lose.Bands / 8));
			// A walled capital city holds out for a year unless the attackers are far stronger.
			Battle Fight;
			Fight.Year = Eco->Years(), Fight.Winner = StateName(Win), Fight.Loser = StateName(Lose);
			for (size_t k = 0; k < Taken; ++k)
				if (Camps[static_cast<size_t>(Lost[k])].Id == Lose.Capital && Lose.City)
				{
					Fight.Siege = true;
					if (StrengthOf(Win) < 2.0 * StrengthOf(Lose) && Dice.Chance(0.6)) Taken = k; // The siege fails this year.
					break;
				}
			// Deadlier weapons kill more of those who fight; the winners lose fewer.
			const double Deadly = std::min(0.6, 0.06 * WeaponsOf(Win.Known | Lose.Known));
			auto Fighting = [&](Camp& C, double Rate)
			{
				int Dead = 0;
				for (int i : C.Members)
				{
					Npc& P = Npcs[static_cast<size_t>(i)];
					if (P.Health > 0.0f && P.AgeYears() >= 16.0 && P.AgeYears() < 50.0 && Dice.Chance(Rate)) P.Health = 0.0f, ++Dead;
				}
				return Dead;
			};
			const auto Front = Of[Win.Id];
			for (size_t k = 0; k < std::min(Front.size(), std::max<size_t>(Taken, 1)); ++k)
				Fight.WinnerDead += Fighting(Camps[static_cast<size_t>(Front[Front.size() - 1 - k])], Deadly * 0.4);
			bool CapitalTaken = false;
			for (size_t k = 0; k < std::min(Lost.size(), Taken + (Fight.Siege ? 1 : 0)); ++k)
			{
				Camp& C = Camps[static_cast<size_t>(Lost[k])];
				if (k == 0) Fight.Place = C.Words[3], Fight.Lat = C.Lat, Fight.Lon = C.Lon;
				Fight.LoserDead += Fighting(C, Deadly);
				if (k >= Taken) continue; // The besieged city starves and bleeds, but holds.
				CapitalTaken = CapitalTaken || C.Id == Lose.Capital;
				// Some flee toward their own capital, the rest are taken.
				if (C.Id != Lose.Capital && Lose.Bands > static_cast<int>(Taken) + 1 && Dice.Chance(0.3))
				{
					const double La = C.Lat + (Lose.Lat - C.Lat) * 0.5, Lo = C.Lon + (Lose.Lon - C.Lon) * 0.5;
					if (std::abs(Lo - C.Lon) < 90.0 && LivableFor(C.Kind, La, Lo, Fishes(C)))
					{
						GridRemove(Lost[k]);
						C.Lat = La, C.Lon = Lo;
						GridAdd(Lost[k]);
						C.DaysHere = 0;
						Fight.Refugees += static_cast<int>(C.Members.size());
						continue;
					}
				}
				C.State = Win.Id;
				Win.Treasury += 0.5f * C.Store, C.Store *= 0.5f; // Plunder.
			}
			WarDead += Fight.WinnerDead + Fight.LoserDead, Refugees += Fight.Refugees;
			NoteBattle(Fight, Win.Tier >= Kingdom || Lose.Tier >= Kingdom);
			int Moved = 0;
			for (size_t k = 0; k < Taken; ++k) Moved += Camps[static_cast<size_t>(Lost[k])].State == Win.Id;
			Lose.Bands -= Moved, Win.Bands += Moved;
			const std::string WinName = StateName(Win), LoseName = StateName(Lose);
			if (CapitalTaken || Lose.Bands <= 1)
			{
				for (size_t k = 0; k < Lost.size(); ++k) Camps[static_cast<size_t>(Lost[k])].State = Win.Id;
				Win.Bands += Lose.Bands, Lose.Bands = 0;
				Lose.Conquests = -1; // Marks it as conquered, not fallen.
				Win.Conquests += 1, Win.Enemy = 0, Win.WarYears = 0;
				std::string T = WinName + " conquers " + LoseName + (Win.Ruler ? " under " + PersonName(Win.RulerName) : std::string());
				T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
				if (Win.Tier >= Kingdom || Lose.Tier >= Kingdom) AddEvent(T);
				if (const Npc* R = Find(Win.Ruler); R && Win.Tier >= Kingdom) Remember(*R, "led " + WinName + " to conquer " + LoseName);
			}
			else if (A.WarYears >= 2 && Dice.Chance(((A.Laws.empty() && B->Laws.empty()) ? 0.15 : 0.3) * (InLeague(A) && InLeague(*B) ? 2.0 : 1.0)))
			{
				A.Enemy = 0, B->Enemy = 0, A.WarYears = 0, B->WarYears = 0;
				if (A.Tier >= Kingdom && B->Tier >= Kingdom) AddEvent("Peace between " + StateName(A) + " and " + StateName(*B));
			}
		}

		// Neighbours meet: wars begin between strangers, alliances between those who share tongue or gods.
		Of = TallyStates();
		for (size_t s = 0; s < States.size(); ++s)
		{
			Polity& A = States[s];
			const std::vector<int>& Mine = Of[A.Id];
			if (Mine.empty() || !Dice.Chance(0.3)) continue;
			const Camp& Edge = Camps[static_cast<size_t>(Mine[std::min(Mine.size() - 1, static_cast<size_t>(Dice.Uniform() * static_cast<double>(Mine.size())))])];
			for (int N : CampSlotsNear(Edge.Lat, Edge.Lon, 100.0))
			{
				const Camp& O = Camps[static_cast<size_t>(N)];
				Polity* B = O.State && O.State != A.Id ? StateById(O.State) : nullptr;
				if (!B || B->Bands == 0) continue;
				const bool Allied = std::find(A.Allies.begin(), A.Allies.end(), B->Id) != A.Allies.end();
				const bool Kin = Understand(Edge, O), SameGods = A.Faith && A.Faith == B->Faith;
				if (!A.Enemy && !B->Enemy && !Allied && !Deterred(A, *B) && Dice.Chance(0.04 * (Kin ? 1.0 : 2.0) * (SameGods ? 1.0 : 2.0) * (A.Laws.empty() ? 1.0 : 0.5) * (InLeague(A) && InLeague(*B) ? 0.5 : 1.0)))
					StartWar(A, *B);
				else if (!Allied && A.Enemy != B->Id && (Kin || SameGods) && A.Allies.size() < 4 && B->Allies.size() < 4 && Dice.Chance(0.03))
					Ally(A, *B);
				break;
			}
		}

		Trade();
		Voyages();
		GlobalAge();

		// New chiefdoms, where a band that farms or lives in a village leads others close by that
		// speak its tongue or share its gods.
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			Camp& C = Camps[c];
			if (C.State || C.Kind != 0 || C.Members.size() < 20 || !C.Leader || !(Has(C.Known, Villages) || Has(C.Known, Farming)) || !Dice.Chance(0.02))
				continue;
			std::vector<int> Follow;
			for (int N : CampSlotsNear(C.Lat, C.Lon, 60.0))
			{
				const Camp& O = Camps[static_cast<size_t>(N)];
				if (static_cast<size_t>(N) != c && !O.State && O.Kind == 0 && !O.Members.empty() &&
					(Understand(C, O) || (!C.Faith.empty() && Shares(O, C.Faith.front()))))
					Follow.push_back(N);
			}
			if (Follow.size() < 2) continue;
			Polity& S = FoundState(static_cast<int>(c), Find(C.Leader), Chiefdom);
			for (int N : Follow) Camps[static_cast<size_t>(N)].State = S.Id, S.Members += static_cast<int>(Camps[static_cast<size_t>(N)].Members.size());
			S.Bands += static_cast<int>(Follow.size());
			NoteState(S, "");
		}
		// Bands near a state join it, drawn by shared speech, gods, trade and its might.
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			Camp& C = Camps[c];
			if (States.empty()) break;
			if (C.State || C.Kind != 0 || C.Members.empty() || !Dice.Chance(0.2)) continue;
			for (int N : CampSlotsNear(C.Lat, C.Lon, 80.0))
			{
				const Camp& O = Camps[static_cast<size_t>(N)];
				const Polity* S = O.State ? StateById(O.State) : nullptr;
				if (!S) continue;
				if (DistanceKm(C.Lat, C.Lon, S->Lat, S->Lon) <= ReachKm(S->Tier, Has(S->Known, Roads)))
				{
					const double Pull = 0.5 + (Understand(C, O) ? 1.0 : 0.0) + (Shares(C, S->Faith) ? 1.0 : 0.0) + (C.Neighbour == O.Id ? 0.5 : 0.0) + 0.5 * S->Tier;
					if (Dice.Chance(0.05 * Pull)) C.State = S->Id;
				}
				break;
			}
		}

		WearLand();
		// States left with no bands fall.
		Of = TallyStates();
		for (const Polity& S : States)
			if (S.Bands == 0 && S.Tier >= Kingdom && S.Conquests >= 0)
			{
				std::string T = StateName(S) + " falls, " + std::to_string(static_cast<int>(Year - S.Founded)) + " years after it was founded";
				T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
				AddEvent(T);
			}
		States.erase(std::remove_if(States.begin(), States.end(), [](const Polity& S) { return S.Bands == 0; }), States.end());
		for (Polity& S : States)
		{
			if (S.Enemy && !StateById(S.Enemy)) S.Enemy = 0, S.WarYears = 0;
			S.Allies.erase(std::remove_if(S.Allies.begin(), S.Allies.end(), [&](uint32_t A) { return !StateById(A); }), S.Allies.end());
		}
		while (static_cast<int>(States.size()) >= NextStateMark)
		{
			AddEvent(std::to_string(NextStateMark) + " states in the world at once");
			NextStateMark *= 2;
		}
		BurnFuel();
		// The first city to reach 100, 1,000, 10,000 people and on.
		for (const Polity& S : States)
			while (S.City && S.CapitalPeople >= NextCityMark)
			{
				AddEvent("The city of " + PersonName(S.City) + " in " + StateName(S) + " is the first with " + std::to_string(NextCityMark) + " people");
				NextCityMark *= 10;
			}
	}

	// The nearest band of another state to a state's capital, and that state.
	Polity* NearestOther(const Polity& S)
	{
		for (int N : CampSlotsNear(S.Lat, S.Lon, std::max(ReachKm(S.Tier, false), TradeKm(S))))
			if (Camps[static_cast<size_t>(N)].State && Camps[static_cast<size_t>(N)].State != S.Id)
				if (Polity* O = StateById(Camps[static_cast<size_t>(N)].State)) return O;
		return nullptr;
	}

	// What a thinking mind chose to do with its power, if the world lets it; one note for the log.
	std::string DoDeed(const Npc& P, int Slot, const std::string& Deed, std::string Law, const std::string& Thought)
	{
		if (Deed.empty()) return "";
		Camp& C = Camps[static_cast<size_t>(Slot)];
		Polity* S = C.State ? StateById(C.State) : nullptr;
		const bool Rules = S && S->Ruler == P.Id;
		const std::string Name = PersonName(P.Name);
		auto Quote = [&] { return Thought.empty() ? std::string() : ": \"" + Thought + "\""; };
		if (Rules && Deed == "trade")
		{
			Polity* Best = nullptr;
			double BestKm = TradeKm(*S);
			for (Polity& B : States)
				if (B.Id != S->Id && B.Bands > 0 && DistanceKm(S->Lat, S->Lon, B.Lat, B.Lon) < BestKm &&
					std::find(S->Partners.begin(), S->Partners.end(), B.Id) == S->Partners.end())
					Best = &B, BestKm = DistanceKm(S->Lat, S->Lon, B.Lat, B.Lon);
			if (!Best || !OpenRoute(*S, *Best)) return " | seeks trade, but finds no one new within reach";
			Remember(P, "opened trade between " + StateName(*S) + " and " + StateName(*Best) + Quote());
			return " | DEED: opens trade with " + StateName(*Best);
		}
		if (Rules && (Deed == "war" || Deed == "ally"))
		{
			Polity* O = NearestOther(*S);
			if (!O) return " | wants to " + Deed + ", but no other state is near";
			const bool Friends = std::find(S->Allies.begin(), S->Allies.end(), O->Id) != S->Allies.end();
			const std::string Them = StateName(*O);
			if (Deed == "war")
			{
				if (S->Enemy || O->Enemy || Friends) return " | wants war on " + Them + ", but cannot now";
				StartWar(*S, *O);
				Remember(P, "led " + StateName(*S) + " to war against " + Them + Quote());
				return " | DEED: goes to war with " + Them;
			}
			if (Friends || S->Enemy == O->Id || S->Allies.size() >= 4 || O->Allies.size() >= 4) return " | seeks an alliance with " + Them + ", but cannot now";
			Ally(*S, *O);
			return " | DEED: allies with " + Them;
		}
		if (Rules && Deed == "peace")
		{
			Polity* E = StateById(S->Enemy);
			if (!E) return " | seeks peace, but there is no war";
			S->Enemy = E->Enemy = 0, S->WarYears = E->WarYears = 0;
			AddEvent(Name + " makes peace between " + StateName(*S) + " and " + StateName(*E));
			return " | DEED: makes peace with " + StateName(*E);
		}
		if (Rules && Deed == "city")
		{
			static const int Cities = SkillNamed("cities"), Villages = SkillNamed("villages");
			if (S->City) return " | the capital is already a city";
			if (!Has(C.Known, Cities) && !Has(C.Known, Villages)) return " | dreams of a city, but nobody here knows how to build one";
			S->Capital = C.Id;
			S->City = static_cast<uint16_t>(C.Words[2] * 977u + S->Id * 31u + 1u);
			const std::string T = Name + " builds the city of " + PersonName(S->City) + " as the capital of " + StateName(*S);
			AddEvent(T);
			Remember(P, "built the city of " + PersonName(S->City) + Quote());
			return " | DEED: builds the city of " + PersonName(S->City);
		}
		if (Rules && (Deed == "school" || Deed == "library"))
		{
			static const int Writing = SkillNamed("writing");
			if (!Has(S->Known, Writing)) return " | wants a " + Deed + ", but nobody in the state can write";
			if (Deed == "library" && !S->City) return " | wants a library, but there is no city to keep it";
			const bool School = Deed == "school";
			uint16_t& N = School ? S->Schools : S->Libraries;
			const std::string T = Name + " opens a " + Deed + " in " + StateName(*S);
			if (School && !AnySchool) AnySchool = true, AddEvent("The first school in the world: " + T);
			else if (!School && !AnyLibrary) AnyLibrary = true, AddEvent("The first library in the world: " + T);
			++N;
			if (!School) S->Written |= S->Known;
			Remember(P, "opened a " + Deed + " in " + StateName(*S) + Quote());
			return " | DEED: opens a " + Deed;
		}
		if (Rules && Deed == "law")
		{
			if (Law.size() > 160) Law.resize(160);
			if (Law.empty()) return " | wants to give a law, but says none";
			if (S->Laws.size() >= 5) S->Laws.erase(S->Laws.begin());
			S->Laws.push_back(Law + " (given by " + Name + ")");
			if (!AnyLaw) AnyLaw = true, AddEvent(Name + " of " + StateName(*S) + " gives the first law: \"" + Law + "\"");
			Remember(P, "gave " + StateName(*S) + " the law: \"" + Law + "\"");
			return " | DEED: gives the law \"" + Law + "\"";
		}
		if (S && !Rules && Deed == "rebel")
		{
			if (C.Id == S->Capital) return " | wants to rebel, but lives in the capital";
			BreakAway(Slot, &P, "rises up");
			return " | DEED: rebels and founds " + StateName(*StateById(C.State));
		}
		if (S && !Rules && Deed == "revolt")
		{
			if (S->Gov == Republic || S->Gov == Democracy) return " | wants a revolution, but the people already choose their rulers";
			auto It = CampSlots.find(S->Capital);
			if (It == CampSlots.end() || S->Inequality < 2.0f || !Dice.Chance(0.3)) return " | calls for revolution, but too few rise";
			Revolt(*S, Camps[static_cast<size_t>(It->second)], &P);
			return " | DEED: leads a revolution";
		}
		if (S && !Rules && Deed == "preach")
		{
			if (C.Faith.empty() || S->Faith == C.Faith.front()) return " | preaches, but the state already shares these beliefs";
			if (!Dice.Chance(0.3)) return " | preaches, but few listen";
			S->Faith = C.Faith.front();
			const BeliefRecord& B = BeliefLog[S->Faith - 1];
			AddEvent(Name + ", a prophet, turns " + StateName(*S) + " to the belief \"" + SayWord(B.Word) + "\" (" + BeliefGloss(B.Subject, B.Claim, B.Practice) + ")");
			Remember(P, "was a prophet who turned " + StateName(*S) + " to their band's beliefs" + Quote());
			return " | DEED: a prophet; the state takes up their beliefs";
		}
		if (!S && Deed == "found" && C.Leader == P.Id)
		{
			std::vector<int> Follow;
			for (int N : CampSlotsNear(C.Lat, C.Lon, 60.0))
				if (N != Slot && !Camps[static_cast<size_t>(N)].State && Camps[static_cast<size_t>(N)].Kind == C.Kind && Understand(C, Camps[static_cast<size_t>(N)])) Follow.push_back(N);
			if (Follow.empty()) return " | wants to unite the bands, but none near will follow";
			Polity& New = FoundState(Slot, &P, Chiefdom);
			for (int N : Follow) Camps[static_cast<size_t>(N)].State = New.Id, New.Members += static_cast<int>(Camps[static_cast<size_t>(N)].Members.size());
			New.Bands += static_cast<int>(Follow.size());
			const std::string T = StateName(New);
			NoteState(New, Name + " unites " + std::to_string(New.Bands) + " bands into " + T, true);
			Remember(P, "united the bands around them into " + T + Quote());
			return " | DEED: founds " + T;
		}
		return " | REFUSED deed \"" + Deed + "\": not something they can do";
	}

	// How far a state's merchants reach, in km.
	static double TradeKm(const Polity& S)
	{
		static const int Roads = SkillNamed("roads"), Wheel = SkillNamed("the wheel"), Ships = SkillNamed("sailing ships"), Rail = SkillNamed("railways"),
			Flight = SkillNamed("flight");
		double Km = 300.0 * (Has(S.Known, Roads) ? 2.0 : 1.0) * (Has(S.Known, Wheel) ? 1.5 : 1.0);
		if (Has(S.Known, Ships)) Km = std::max(Km, 3000.0);
		if (Has(S.Known, Rail)) Km = std::max(Km, 6000.0);
		if (Has(S.Known, Flight)) Km = 20000.0;
		return Km;
	}
	bool OpenRoute(Polity& A, Polity& B)
	{
		if (A.Id == B.Id || A.Enemy == B.Id || A.Partners.size() >= 6 || B.Partners.size() >= 6 ||
			std::find(A.Partners.begin(), A.Partners.end(), B.Id) != A.Partners.end())
			return false;
		A.Partners.push_back(B.Id), B.Partners.push_back(A.Id);
		const double Km = DistanceKm(A.Lat, A.Lon, B.Lat, B.Lon);
		auto Cap = [](std::string T) { T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0]))); return T; };
		if (!AnyRoute)
			AnyRoute = true, AddEvent("The first trade route: " + StateName(A) + " and " + StateName(B) + " trade goods across " + std::to_string(static_cast<int>(Km)) + " km");
		else if (Km > 1000.0 && !AnySeaRoute && Eco->IsLand(A.Lat, A.Lon) && !Eco->IsLand((A.Lat + B.Lat) / 2.0, (A.Lon + B.Lon) / 2.0))
			AnySeaRoute = true, AddEvent("The first trade across the sea: " + Cap(StateName(A)) + "'s ships reach " + StateName(B) + ", " + std::to_string(static_cast<int>(Km)) + " km away");
		return true;
	}

	// Once a year: states open trade with neighbours within their merchants' reach; goods earn both
	// sides wealth, food flows from plenty to want, and each learns what the other knows.
	void Trade()
	{
		static const int Money = SkillNamed("money");
		for (Polity& A : States)
		{
			A.Heard = A.Known;
			// Routes close with war, distance or a partner's fall.
			A.Partners.erase(std::remove_if(A.Partners.begin(), A.Partners.end(), [&](uint32_t P)
			{
				const Polity* B = StateById(P);
				return !B || B->Bands == 0 || A.Enemy == P || DistanceKm(A.Lat, A.Lon, B->Lat, B->Lon) > 1.5 * std::max(TradeKm(A), TradeKm(*B));
			}), A.Partners.end());
		}
		for (Polity& A : States)
		{
			if (A.Bands == 0 || A.Partners.size() >= 6 || !Dice.Chance(0.2)) continue;
			Polity* Best = nullptr;
			double BestKm = TradeKm(A);
			for (Polity& B : States)
			{
				if (B.Id == A.Id || B.Bands == 0 || std::find(A.Partners.begin(), A.Partners.end(), B.Id) != A.Partners.end()) continue;
				const double Km = DistanceKm(A.Lat, A.Lon, B.Lat, B.Lon);
				if (Km < BestKm && Km < TradeKm(B)) Best = &B, BestKm = Km;
			}
			if (Best) OpenRoute(A, *Best);
		}
		for (Polity& A : States)
			for (uint32_t P : A.Partners)
			{
				Polity* B = StateById(P);
				if (!B || A.Id > B->Id) continue; // Each route once.
				const double Goods = 0.01 * std::sqrt(static_cast<double>(A.Members) * B->Members) * (Has(A.Known | B->Known, Money) ? 2.0 : 1.0);
				A.Wealth += Goods, B->Wealth += Goods, TradeThisYear += Goods;
				A.Heard |= B->Known, B->Heard |= A.Known;
				auto IA = CampSlots.find(A.Capital), IB = CampSlots.find(B->Capital);
				if (IA == CampSlots.end() || IB == CampSlots.end()) continue;
				Camp& CA = Camps[static_cast<size_t>(IA->second)];
				Camp& CB = Camps[static_cast<size_t>(IB->second)];
				const float Move = 0.05f * (CA.Store - CB.Store);
				CA.Store -= Move, CB.Store += Move;
			}
		WorldTrade = TradeThisYear, TradeThisYear = 0.0;
	}

	void StartWar(Polity& A, Polity& B, bool Bloc = true)
	{
		A.Enemy = B.Id, B.Enemy = A.Id, A.WarYears = B.WarYears = 0;
		// Alliances drag their members in: each free ally of one side takes on a free ally of the other.
		if (Bloc)
			for (uint32_t XA : A.Allies)
				for (uint32_t YB : B.Allies)
				{
					Polity* X = StateById(XA);
					Polity* Y = StateById(YB);
					if (X && Y && X != Y && !X->Enemy && !Y->Enemy && X != &B && Y != &A) StartWar(*X, *Y, false);
				}
		if (A.Tier >= Kingdom || B.Tier >= Kingdom)
		{
			std::string T = StateName(A) + " goes to war with " + StateName(B);
			T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
			AddEvent(T);
		}
	}
	void Ally(Polity& A, Polity& B)
	{
		A.Allies.push_back(B.Id), B.Allies.push_back(A.Id);
		if (A.Tier >= Kingdom && B.Tier >= Kingdom)
		{
			std::string T = StateName(A) + " and " + StateName(B) + " become allies";
			T[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(T[0])));
			AddEvent(T);
		}
	}

	void CheckFirsts()
	{
		const int Count = AliveOfKind(0);
		while (Count >= NextPopulationMark)
		{
			AddEvent("The people number " + std::to_string(NextPopulationMark));
			NextPopulationMark = NextPopulationMark < 100 ? 100 : (NextPopulationMark < 250 ? 250 : (NextPopulationMark < 500 ? 500 : NextPopulationMark * 2));
		}
		const int BandsNow = static_cast<int>(std::count_if(Camps.begin(), Camps.end(), [](const Camp& C) { return C.Kind == 0 && !C.Members.empty(); }));
		while (BandsNow >= NextBandMark)
		{
			AddEvent(NextBandMark == 2 ? "The people split into two separate bands" : std::to_string(NextBandMark) + " separate bands");
			NextBandMark = NextBandMark < 5 ? 5 : NextBandMark * 2;
		}
		while (HighestGeneration >= NextGenerationMark)
		{
			AddEvent(NextGenerationMark == 1 ? "The first generation born in the world" : "Generation " + std::to_string(NextGenerationMark) + " is born");
			NextGenerationMark = NextGenerationMark < 2 ? 2 : (NextGenerationMark < 5 ? 5 : (NextGenerationMark < 10 ? 10 : NextGenerationMark * 2));
		}
		for (const Camp& C : Camps)
		{
			if (C.Kind == 0) NoteRegion(C.Lat, C.Lon, C.Members.empty() ? nullptr : &Npcs[C.Members[0]]);
		}
		for (const Npc& P : Npcs)
		{
			if (P.Kind == 0 && P.AgeYears() >= NextAgeMark)
			{
				AddEvent("First NPC to live to " + std::to_string(NextAgeMark));
				NextAgeMark += 10;
			}
		}
	}
};

} // namespace earth

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

#include "Ecosystem.h"
#include "Parallel.h"
#include "Random.h"
#include "Skills.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
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

	int Hidden() const { return static_cast<int>(B1.size()); }

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
	SkillEffects Own;    // What those skills do for this NPC's own gathering, hunting and carrying.

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
};

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
	// What camps within a few days' walk know: people pick that up too, more slowly.
	SkillSet Nearby = 0;
	// For Step(): the wettest place within reach, and the squares the band gathers in and how
	// much food they hold on average.
	float BestWater = 0.0f;
	std::array<int64_t, 5> ForageSquares{};
	float ForageKg = 0.0f;
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
	// Chance a year that one person of average wits hits on a simple skill on their own; deeper
	// skills are 4 times rarer per step down the tree.
	double DiscoveryPerYear = 2e-7; // Per person; tuned so farming takes ~100k+ years, not centuries.
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
		AddEvent("A band of " + std::to_string(Settings.StartCount) + " people wakes up in East Africa, knowing nothing");
	}

	// ---- Queries ----------------------------------------------------------------------------

	const std::vector<Npc>& All() const { return Npcs; }
	const std::vector<Camp>& AllCamps() const { return Camps; }
	const std::vector<EcoEvent>& Firsts() const { return Events; }
	int Alive() const { return static_cast<int>(Npcs.size()); }
	int BandCount() const { return static_cast<int>(Camps.size()); }
	int MaxGeneration() const { return HighestGeneration; }
	long long TotalBirths() const { return Births; }
	long long TotalDeaths() const { return Deaths; }
	// Deaths by cause: old age and illness, predators, hunger, thirst, injuries.
	const std::array<long long, 5>& DeathCauses() const { return Causes; }
	long long HuntsWon() const { return Kills; }
	// Skills someone alive knows, and how many people know each.
	int SkillsKnown() const { return static_cast<int>(std::bitset<64>(KnownNow).count()); }
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

	const Npc* Find(uint32_t Id) const
	{
		for (const Npc& P : Npcs)
		{
			if (P.Id == Id)
			{
				return &P;
			}
		}
		return nullptr;
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
		RegrowGatherable();
		IndexCamps();
		SenseCamps();

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
		BirthsAndDeaths();
		IndexCamps();
		MoveCamps();
		SplitAndMergeCamps();
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
		RegrowGatherable(Days);
		IndexCamps();
		SenseCamps();
		for (Camp& C : Camps)
		{
			C.BestWater = static_cast<float>(BestWaterNear(C.Lat, C.Lon));
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
		for (size_t i = 0; i < Count; ++i)
		{
			MaybeConceive(i, Chosen[i] >= 0 && Dice.Chance(OverDays(Shares[i][static_cast<int>(Action::Socialize)], Days)), Days);
			Imitate(i, Days);
		}

		LearnSkills(Days);
		BirthsAndDeaths(Days);
		IndexCamps();
		MoveCamps(Days);
		SplitAndMergeCamps(Days);
		CheckFirsts();
	}

private:
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

	SkillSet KnownNow = 0;  // Skills someone alive knows.
	SkillSet EverKnown = 0; // Skills anyone has ever known.

	// Camps by one-degree square, so finding nearby camps doesn't look at every camp.
	std::unordered_map<int64_t, std::vector<int>> CampGrid;
	// People each one-degree square can feed, from its plant cover at the best time of year.
	std::vector<float> SquareCapacity;
	int LandClimate = -1;

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
		if (!Npcs.empty())
		{
			return true;
		}
		if (!Settings.NewBandWhenGone)
		{
			return false;
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

	bool Livable(double Lat, double Lon) const
	{
		return Lat < 80.0 && Lat > -60.0 && Eco->IsLand(Lat, Lon) && Eco->AltitudeAt(Lat, Lon) < 4000.0;
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

	double GatherableAt(double Lat, double Lon) const
	{
		const double Full = Eco->PlantCover(Lat, Lon) * Settings.GatherableKg;
		auto It = Gathered.find(KeyOf(Lat, Lon));
		return std::max(0.0, Full - (It == Gathered.end() ? 0.0 : It->second));
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
	int CampsNear(double Lat, double Lon, double Km, int SelfId) const
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
				}
			}
		}
		return Count;
	}

	void MeasureLand()
	{
		SquareCapacity.assign(180 * 360, 0.0f);
		for (int r = 0; r < 180; ++r)
		{
			for (int c = 0; c < 360; ++c)
			{
				double Cover = 0.0;
				for (int k = 0; k < 16; ++k)
				{
					const double Lat = r - 90.0 + (k / 4 + 0.5) / 4.0;
					const double Lon = c - 180.0 + (k % 4 + 0.5) / 4.0;
					Cover += Livable(Lat, Lon) ? Eco->BestCover(Lat, Lon) / 16.0 : 0.0;
				}
				const double AreaKm2 = kKmPerDegree * kKmPerDegree * std::cos((r - 89.5) * kPi / 180.0);
				SquareCapacity[static_cast<size_t>(r) * 360 + c] = static_cast<float>(Cover * AreaKm2 * Settings.PeoplePerKm2);
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
		for (Camp& C : Camps)
		{
			const int R0 = GridRow(C.Lat);
			const int C0 = GridCol(C.Lon);
			double Capacity = 0.0;
			double Living = 0.0;
			for (int r = std::max(0, R0 - 2); r <= std::min(179, R0 + 2); ++r)
			{
				for (int c = C0 - 2; c <= C0 + 2; ++c)
				{
					const int Col = ((c % 360) + 360) % 360;
					Capacity += SquareCapacity[static_cast<size_t>(r) * 360 + Col];
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
			Capacity *= C.Effects.Land;
			C.Room = Capacity > 0.0 ? static_cast<float>(std::clamp(1.0 - Living / Capacity, 0.0, 1.0)) : 0.0f;
		}
	}

	// Reads what each camp can see and knows today.
	void SenseCamps()
	{
		for (Camp& C : Camps)
		{
			C.Known = 0;
			for (int i : C.Members)
			{
				C.Known |= Npcs[i].Skills;
			}
			C.Effects = EffectsOf(C.Known);
		}
		for (Camp& C : Camps)
		{
			C.Nearby = 0;
			for (int r = GridRow(C.Lat) - 1; r <= GridRow(C.Lat) + 1; ++r)
			{
				for (int c = GridCol(C.Lon) - 1; c <= GridCol(C.Lon) + 1; ++c)
				{
					auto It = CampGrid.find(GridKey(r, c));
					if (It == CampGrid.end()) continue;
					for (int Slot : It->second)
					{
						if (DistanceKm(C.Lat, C.Lon, Camps[Slot].Lat, Camps[Slot].Lon) < 100.0) C.Nearby |= Camps[Slot].Known;
					}
				}
			}
		}
		MeasureRoom();
		for (Camp& C : Camps)
		{
			C.SeenDanger = static_cast<float>(std::clamp(Eco->PredatorsNear(C.Lat, C.Lon) / 40.0, 0.0, 1.0));
			C.SeenFood = static_cast<float>(std::clamp(GatherableAt(C.Lat, C.Lon) / Settings.GatherableKg, 0.0, 1.0));
			C.SeenGame = static_cast<float>(std::clamp(Eco->GameNear(C.Lat, C.Lon, 1000.0) / 2000.0, 0.0, 1.0));
			C.SeenWater = static_cast<float>(Eco->WaterAt(C.Lat, C.Lon));
			C.SeenSeason = static_cast<float>(Eco->GrowingSeason(C.Lat));
		}
	}

	// How good a place looks for camp SelfId: food to gather, water and game, minus danger and
	// minus other camps already there (bands keep their own range, so they spread out).
	double PlaceScore(double Lat, double Lon, int SelfId) const
	{
		if (!Livable(Lat, Lon))
		{
			return -1.0;
		}
		const double Crowd = 0.1 * std::min(4, CampsNear(Lat, Lon, 50.0, SelfId));
		const double Food = GatherableAt(Lat, Lon) / Settings.GatherableKg;
		const double Water = Eco->WaterAt(Lat, Lon);
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
		for (size_t Slot = 0; Slot < Camps.size(); ++Slot)
		{
			Camp& C = Camps[Slot];
			C.ScoutScore *= Days == 1 ? 0.995 : std::pow(0.995, Days); // Old news gets less trustworthy.
			C.DaysHere += Days;
			const double Here = PlaceScore(C.Lat, C.Lon, C.Id);
			const double Food = GatherableAt(C.Lat, C.Lon) / std::max(1.0, Eco->PlantCover(C.Lat, C.Lon) * Settings.GatherableKg);
			const bool Crowded = C.DaysHere > 30 && CampsNear(C.Lat, C.Lon, 30.0, C.Id) > 0;
			const bool Stressed = Food < 0.4 || Eco->WaterAt(C.Lat, C.Lon) < 0.2 || Crowded || (C.DaysHere > 90 && Dice.Chance(OverDays(0.02, Days)));
			if (!Stressed)
			{
				continue;
			}
			double TargetLat = C.Lat;
			double TargetLon = C.Lon;
			if (C.ScoutScore > Here + 0.05)
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
					Offset(C.Lat, C.Lon, (d + Dice.Uniform()) * kPi / 4.0, 25.0, La, Lo);
					const double S = PlaceScore(La, Lo, C.Id);
					if (S > Best)
					{
						Best = S;
						TargetLat = La;
						TargetLon = Lo;
					}
				}
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
			if (Livable(NewLat, NewLon))
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
			const bool Full = Size >= 20 && Camps[c].Room < 0.2f;
			if ((Size < Settings.SplitSize && !Full) || !Dice.Chance(OverDays(0.01, Days)))
			{
				continue;
			}
			// About half leave, families together: children go where their mothers go.
			Camp New;
			New.Id = NextCampId++;
			double Angle = Dice.Range(0.0, 2.0 * kPi);
			if (Camps[c].ScoutScore > 0.0)
			{
				Angle = std::atan2(Camps[c].ScoutLat - Camps[c].Lat, Camps[c].ScoutLon - Camps[c].Lon);
			}
			bool Placed = false;
			for (int Try = 0; Try < 8 && !Placed; ++Try)
			{
				const double Far = Full ? Dice.Range(100.0, 250.0) * Camps[c].Effects.Travel : Dice.Range(30.0, 60.0);
				Offset(Camps[c].Lat, Camps[c].Lon, Angle + Try * 0.8, Far, New.Lat, New.Lon);
				Placed = Livable(New.Lat, New.Lon);
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
				for (int i : Camps[b].Members)
				{
					Npcs[i].Camp = Camps[a].Id;
					Camps[a].Members.push_back(i);
				}
				Camps[b].Members.clear();
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
			double Best = GatherableAt(C.Lat, C.Lon);
			for (int d = 0; d < 4; ++d)
			{
				double La = 0.0;
				double Lo = 0.0;
				Offset(C.Lat, C.Lon, d * kPi / 2.0 + Dice.Range(-0.4, 0.4), 20.0, La, Lo);
				const double Avail = Eco->IsLand(La, Lo) ? GatherableAt(La, Lo) * 0.8 : 0.0; // Farther is more work.
				if (Avail > Best)
				{
					Best = Avail;
					BestLat = La;
					BestLon = Lo;
				}
			}
			const double Found = 6.0 * P.ForageSkill * std::min(1.0, Best / 500.0) * P.Own.Forage;
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
			const double Success = std::min(0.6, (0.05 + 0.05 * std::min(Party, 8)) * Game * (0.4 + P.HuntSkill) * P.Own.Hunt);
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
					NoteHunt(Kind, Party);
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
			double Best = Eco->WaterAt(C.Lat, C.Lon);
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
			// Give half of what you carry to the hungriest person in camp.
			int Hungriest = -1;
			for (int j : C.Members)
			{
				if (j != static_cast<int>(Index) && (Hungriest < 0 || Npcs[j].Energy < Npcs[Hungriest].Energy))
				{
					Hungriest = j;
				}
			}
			if (Hungriest >= 0 && P.CarriedKg > 0.5f)
			{
				const float Give = P.CarriedKg * 0.5f;
				P.CarriedKg -= Give;
				Npcs[Hungriest].CarriedKg = std::min(10.0f, Npcs[Hungriest].CarriedKg + Give);
				P.SocialNeed = std::max(0.0f, P.SocialNeed - 0.3f);
				if (!AnyShare)
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
			const double Score = PlaceScore(La, Lo, C.Id);
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
				NoteRegion(La, Lo);
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
				Share[i] = std::min(0.6, Chance[Slot] * (0.4 + Npcs[i].HuntSkill) * Npcs[i].Own.Hunt) * Hunting * Days;
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
					NoteHunt(static_cast<int>(k), Party);
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
	void MaybeConceive(size_t Index, bool Socialized, int Days = 1)
	{
		Npc& P = Npcs[Index];
		// Fewer children as the land around fills up, so numbers level off gently where food runs short.
		double Room = Camps[CampSlot(P.Camp)].Room;
		if (Settings.MaxPeople > 0)
		{
			Room *= 1.0 - static_cast<double>(Npcs.size()) / Settings.MaxPeople;
		}
		if (!P.Female || !Fertile(P) || P.Energy < 0.4f || Room <= 0.0 || !Dice.Chance(OverDays((Socialized ? 0.04 : 0.01) * Room, Days)))
		{
			return;
		}
		int Mate = -1;
		int Seen = 0;
		for (int j : Camps[CampSlot(P.Camp)].Members)
		{
			const Npc& Q = Npcs[j];
			// No brothers or sons (as far as anyone can tell, by mother).
			if (!Q.Female && Fertile(Q) && (P.Mother == 0 || Q.Mother != P.Mother) && Q.Mother != P.Id && Dice.Chance(1.0 / ++Seen))
			{
				Mate = j;
			}
		}
		if (Mate < 0)
		{
			return;
		}
		P.PregnantDays = 0.0;
		P.BabyFather = Npcs[Mate].Id;
		P.BabyFatherGenes = Npcs[Mate].Genes;
	}

	void NoteHunt(int Kind, int Party)
	{
		if (SpeciesHunted.size() != Eco->GetSpecies().size())
		{
			SpeciesHunted.assign(Eco->GetSpecies().size(), false);
		}
		if (SpeciesHunted[Kind])
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
		for (int j : Camps[CampSlot(Child.Camp)].Members)
		{
			if (Wanted <= 0.0f)
			{
				break;
			}
			if (j != Carer && !Npcs[j].IsChild())
			{
				Give(Npcs[j]);
			}
		}
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
		if (Eco->WaterAt(P.Lat, P.Lon) > 0.4)
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
		double Total = GatherableAt(C.Lat, C.Lon);
		for (int d = 0; d < 4; ++d)
		{
			double La = 0.0;
			double Lo = 0.0;
			Offset(C.Lat, C.Lon, d * kPi / 2.0, 28.0, La, Lo);
			C.ForageSquares[d + 1] = KeyOf(La, Lo);
			Total += Eco->IsLand(La, Lo) ? GatherableAt(La, Lo) * 0.8 : 0.0; // Farther is more work.
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
	static double ForageYield(const Npc& P, const Camp& C) { return 6.0 * P.ForageSkill * std::min(1.0, C.ForageKg / 500.0) * P.Own.Forage; }

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
		const float Loss = Eco->WaterAt(P.Lat, P.Lon) > 0.4 ? 0.05f : 0.3f;
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
		for (int j : Camps[CampSlot(Child.Camp)].Members)
		{
			if (Wanted <= 0.0f)
			{
				break;
			}
			if (j != static_cast<int>(CarerIndex) && !Npcs[j].IsChild())
			{
				Give(static_cast<size_t>(j));
			}
		}
		const float FromMum = std::min(1.0f - Child.Energy, std::max(0.0f, Mum.Energy - 0.5f) * 0.5f);
		Child.Energy += FromMum;
		Mum.Energy -= FromMum * 0.5f;
		Child.CarriedKg = 0.0f;
		LiveThroughDays(Child, nullptr, Given / Days, 0.0f, 1.0f, Days);
		Child.Fatigue = 0.1f;
		Blend(Child, Mum, static_cast<float>(OverDays(0.02, Days)));
	}

	// How quick-witted an NPC is: bigger brains find and learn new skills faster.
	static double Smarts(const Npc& P) { return static_cast<double>(P.BrainSize()) / kBrainHidden; }

	// Over Days days, each person may hit on a skill they have what it takes for, and picks up
	// skills others in camp know. Young children are too small to learn them.
	void LearnSkills(int Days)
	{
		const std::vector<Skill>& List = AllSkills();
		const int Count = static_cast<int>(List.size());
		for (Npc& P : Npcs)
		{
			if (P.AgeYears() < 5.0)
			{
				continue;
			}
			const double Wits = Smarts(P);
			const Camp& C = Camps[CampSlot(P.Camp)];
			// Something new: rarer the deeper it is, likelier for the curious and the clever.
			double Total = 0.0;
			std::array<double, 64> Odds{};
			for (int k = 0; k < Count; ++k)
			{
				if (CanDiscover(P.Skills, k))
				{
					Odds[k] = Settings.DiscoveryPerYear / std::pow(4.0, List[k].Tier - 1) * Days / 365.0 * Wits * Wits * P.Genes->Curiosity / 0.6;
					Total += Odds[k];
				}
			}
			double Pick = Dice.Uniform();
			if (Pick < Total)
			{
				for (int k = 0; k < Count; ++k)
				{
					if ((Pick -= Odds[k]) < 0.0)
					{
						Discover(P, k);
						break;
					}
				}
			}
			// Something others in camp, or in camps nearby, already know, once what it builds on is known.
			const SkillSet Missing = (C.Known | C.Nearby) & ~P.Skills;
			if (Missing)
			{
				const double InCamp = OverDays(std::min(0.5, 0.002 * C.Effects.Teaching * Wits), Days);
				const double Neighbours = OverDays(std::min(0.5, 0.0002 * C.Effects.Teaching * Wits), Days);
				for (int k = 0; k < Count; ++k)
				{
					if (Knows(Missing, k) && CanDiscover(P.Skills, k) && Dice.Chance(Knows(C.Known, k) ? InCamp : Neighbours))
					{
						P.LearnSkill(k);
					}
				}
			}
		}
		NoteSkills();
	}

	void Discover(Npc& P, int Skill)
	{
		P.LearnSkill(Skill);
		if (!Knows(KnownNow, Skill))
		{
			const std::string Name = AllSkills()[Skill].Name;
			AddEvent(Knows(EverKnown, Skill) ? "Someone rediscovers " + Name : "First discovery of " + Name);
			KnownNow |= SkillSet(1) << Skill;
			EverKnown |= SkillSet(1) << Skill;
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
			}
		}
		KnownNow = Now;
	}

	// Energy a day beyond the basics: a bigger brain costs more (a smaller one saves a little), and
	// the cold costs more far from the tropics in winter, unless fire, clothing and shelter keep it out.
	float ExtraBurn(const Npc& P) const
	{
		const double Far = std::clamp((std::abs(P.Lat) - 30.0) / 30.0, 0.0, 1.0);
		const double Cold = Far * (0.5 + 0.5 * (1.0 - Eco->GrowingSeason(P.Lat)));
		const float Warmth = Camps[CampSlot(P.Camp)].Effects.Warmth;
		return 0.0015f * static_cast<float>(P.BrainSize() - kBrainHidden) + static_cast<float>(0.12 * Cold) * (1.0f - Warmth);
	}

	// How good an NPC feels right now; learning tries to make this go up.
	static float WellbeingOf(const Npc& P)
	{
		const float Hunger = 1.0f - P.Energy;
		const float Thirst = 1.0f - P.Water;
		// Food in hand counts too: it is tomorrow's meal, which lets effort that pays off later be learned.
		return -(Hunger * Hunger + Thirst * Thirst + 0.5f * P.Fatigue * P.Fatigue + 0.5f * P.Fear + 0.3f * P.Loneliness) +
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
	void Imitate(size_t Index, int Days = 1)
	{
		Npc& P = Npcs[Index];
		if (P.IsChild() || !Dice.Chance(OverDays(P.Genes->Imitation, Days)))
		{
			return;
		}
		const std::vector<int>& Mates = Camps[CampSlot(P.Camp)].Members;
		int Best = -1;
		for (int Try = 0; Try < 5 && !Mates.empty(); ++Try)
		{
			const int j = Mates[static_cast<size_t>(Dice.Next() % Mates.size())];
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
				Born.push_back(Baby);
				++Births;
				if (Births == 1)
				{
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
		Camps.assign(1, First);

		const Genome Base = InstinctGenome();
		for (int i = 0; i < Settings.StartCount; ++i)
		{
			Npc P;
			P.Id = NextId++;
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

	void RememberIfSuccessful(const Npc& P)
	{
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
		Causes[Cause] += 1;
		if (FirstDeath[Cause] && !AnyDeathBy[Cause])
		{
			AnyDeathBy[Cause] = true;
			AddEvent(FirstDeath[Cause]);
		}
	}

	// 0 Africa, 1 Asia, 2 Europe, 3 Australia, 4 the Americas, 5 elsewhere.
	static int RegionOf(double Lat, double Lon)
	{
		if (Lon < -30.0) return 4;
		if (Lat < -10.0 && Lon > 112.0) return 3;
		if (Lat < 37.0 && Lon > -20.0 && Lon < 52.0)
		{
			if (Lat < 12.0) return 0;
			// The Red Sea and Sinai separate Africa from Arabia and the Levant.
			const double Edge = Lat < 29.0 ? 43.0 - (Lat - 13.0) * 9.0 / 16.0 : 32.5;
			return Lon < Edge ? 0 : 1;
		}
		if (Lat >= 36.0 && Lon > -12.0 && Lon < 40.0 && !(Lat < 42.0 && Lon > 26.0)) return 2;
		if (Lon >= 26.0) return 1;
		return 5;
	}

	void NoteRegion(double Lat, double Lon)
	{
		static const char* RegionNames[6] = {"Africa", "Asia", "Europe", "Australia", "the Americas", "a new land"};
		const int Region = RegionOf(Lat, Lon);
		if (!Regions[Region])
		{
			Regions[Region] = true;
			AddEvent("First NPC sets foot in " + std::string(RegionNames[Region]));
		}
	}

	void CheckFirsts()
	{
		const int Count = static_cast<int>(Npcs.size());
		while (Count >= NextPopulationMark)
		{
			AddEvent("The people number " + std::to_string(NextPopulationMark));
			NextPopulationMark = NextPopulationMark < 100 ? 100 : (NextPopulationMark < 250 ? 250 : (NextPopulationMark < 500 ? 500 : NextPopulationMark * 2));
		}
		const int BandsNow = static_cast<int>(Camps.size());
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
			NoteRegion(C.Lat, C.Lon);
		}
		for (const Npc& P : Npcs)
		{
			if (P.AgeYears() >= NextAgeMark)
			{
				AddEvent("First NPC to live to " + std::to_string(NextAgeMark));
				NextAgeMark += 10;
			}
		}
	}
};

} // namespace earth

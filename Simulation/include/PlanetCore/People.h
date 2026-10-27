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
#include "Random.h"

#include <algorithm>
#include <array>
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
constexpr int kBrainHidden = 12;
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

// What an NPC is born with: both layers of the brain plus three learning traits.
struct Genome
{
	std::array<float, kBrainInputs * kBrainHidden> W1{}; // [input * hidden]
	std::array<float, kBrainHidden> B1{};
	std::array<float, kBrainHidden * kBrainActions> W2{}; // [hidden * action]
	std::array<float, kBrainActions> B2{};
	float LearningRate = 0.05f; // How strongly a good or bad day changes later choices.
	float Curiosity = 0.6f;     // How often it tries something other than its favourite choice.
	float Imitation = 0.1f;     // How often it copies a successful camp mate.
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
	std::array<float, kBrainHidden * kBrainActions> LearnedW2{};
	std::array<float, kBrainActions> LearnedB2{};

	// The last decision, kept so it can be shown.
	std::array<float, kBrainInputs> LastInputs{};
	std::array<float, kBrainHidden> LastHidden{};
	std::array<float, kBrainActions> LastChoice{}; // Probability of each action.
	int LastAction = static_cast<int>(Action::Rest);
	std::array<int, kBrainActions> ActionCounts{};

	float Pleasure = 0.0f; // One-off good feelings today, such as company or a kill.
	float RewardBaseline = 0.0f;
	float Wellbeing = 0.0f; // Smoothed over recent days.
	int BandSize = 1;
	bool Alive = true;

	double AgeYears() const { return AgeDays / 365.0; }
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
};

struct PeopleSettings
{
	// Omo Kibish in southern Ethiopia, among the oldest Homo sapiens sites in East Africa.
	double StartLat = 5.4;
	double StartLon = 35.9;
	int StartCount = 30;
	int MaxPeople = 20000;
	// A camp this big starts to think about splitting.
	int SplitSize = 40;
	// Food people can gather from plants (fruit, nuts, roots) in a quarter-degree square at full plant cover.
	double GatherableKg = 3000.0;
};

class People
{
public:
	People(Ecosystem* InEco, uint64_t Seed, const PeopleSettings& InSettings = PeopleSettings())
		: Eco(InEco), Dice(Seed ^ 0x5eed0fa11ull), Settings(InSettings)
	{
		Camp First;
		First.Id = NextCampId++;
		First.Lat = Settings.StartLat;
		First.Lon = Settings.StartLon;
		Camps.push_back(First);

		const Genome Base = InstinctGenome();
		for (int i = 0; i < Settings.StartCount; ++i)
		{
			Npc P;
			P.Id = NextId++;
			P.Female = i % 2 == 0;
			P.AgeDays = Dice.Range(14.0, 35.0) * 365.0;
			P.Genes = std::make_shared<const Genome>(Mutate(Base, 0.3f));
			P.Camp = First.Id;
			P.Heading = static_cast<float>(Dice.Range(0.0, 6.283185307179586));
			P.OffsetLat = static_cast<float>(Dice.Range(-0.04, 0.04));
			P.OffsetLon = static_cast<float>(Dice.Range(-0.04, 0.04));
			P.Lat = First.Lat + P.OffsetLat;
			P.Lon = First.Lon + P.OffsetLon;
			Npcs.push_back(P);
		}
		Regions.assign(6, false);
		Regions[0] = true; // Africa
		AddEvent("A band of " + std::to_string(Settings.StartCount) + " people wakes up in East Africa, knowing nothing");
		IndexCamps();
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
		J += "],\"w1\":" + Matrix(P.Genes->W1.data(), kBrainInputs, kBrainHidden);
		J += ",\"b1\":" + Matrix(P.Genes->B1.data(), 1, kBrainHidden);
		J += ",\"w2_inborn\":" + Matrix(P.Genes->W2.data(), kBrainHidden, kBrainActions);
		J += ",\"w2_learned\":" + Matrix(P.LearnedW2.data(), kBrainHidden, kBrainActions);
		J += ",\"b2_inborn\":" + Matrix(P.Genes->B2.data(), 1, kBrainActions);
		J += ",\"b2_learned\":" + Matrix(P.LearnedB2.data(), 1, kBrainActions);
		J += ",\"last_inputs\":" + Matrix(P.LastInputs.data(), 1, kBrainInputs);
		J += ",\"last_hidden\":" + Matrix(P.LastHidden.data(), 1, kBrainHidden);
		J += ",\"last_choice\":" + Matrix(P.LastChoice.data(), 1, kBrainActions);
		J += ",\"last_action\":\"" + std::string(ActionName(P.LastAction)) + "\"";
		J += ",\"action_counts\":[";
		for (int a = 0; a < kBrainActions; ++a) J += std::string(a ? "," : "") + std::to_string(P.ActionCounts[a]);
		J += "]}";
		return J;
	}

	// ---- Simulation -------------------------------------------------------------------------

	// Call once per day, after Ecosystem::StepDay().
	void StepDay()
	{
		if (Npcs.empty())
		{
			return;
		}
		Eco->RefreshIndex();
		RegrowGatherable();
		IndexCamps();

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
			Chosen[i] = Decide(P);
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
				P.CarriedKg = static_cast<float>(std::min(10.0, P.CarriedKg + Share));
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

		BirthsAndDeaths();
		IndexCamps();
		MoveCamps();
		SplitAndMergeCamps();
		CheckFirsts();
	}

private:
	Ecosystem* Eco;
	Rng Dice;
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
			G.W1[h * kBrainHidden + h] = 3.0f;
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
		G.LearningRate = std::clamp(G.LearningRate * static_cast<float>(std::exp(Dice.Gaussian() * 0.1)), 0.001f, 0.5f);
		G.Curiosity = std::clamp(G.Curiosity * static_cast<float>(std::exp(Dice.Gaussian() * 0.1)), 0.1f, 2.0f);
		G.Imitation = std::clamp(G.Imitation * static_cast<float>(std::exp(Dice.Gaussian() * 0.1)), 0.0f, 0.5f);
		return G;
	}

	// Each weight comes from one parent or the other, then a few mutate.
	Genome Cross(const Genome& Mum, const Genome& Dad)
	{
		Genome G = Mum;
		for (size_t k = 0; k < G.W1.size(); ++k) if (Dice.Chance(0.5)) G.W1[k] = Dad.W1[k];
		for (size_t k = 0; k < G.B1.size(); ++k) if (Dice.Chance(0.5)) G.B1[k] = Dad.B1[k];
		for (size_t k = 0; k < G.W2.size(); ++k) if (Dice.Chance(0.5)) G.W2[k] = Dad.W2[k];
		for (size_t k = 0; k < G.B2.size(); ++k) if (Dice.Chance(0.5)) G.B2[k] = Dad.B2[k];
		if (Dice.Chance(0.5)) G.LearningRate = Dad.LearningRate;
		if (Dice.Chance(0.5)) G.Curiosity = Dad.Curiosity;
		if (Dice.Chance(0.5)) G.Imitation = Dad.Imitation;
		return Mutate(G, 0.15f);
	}

	// ---- Places -----------------------------------------------------------------------------

	void RegrowGatherable()
	{
		for (auto It = Gathered.begin(); It != Gathered.end();)
		{
			It->second *= 0.97;
			It = It->second < 1.0 ? Gathered.erase(It) : std::next(It);
		}
	}

	double GatherableAt(double Lat, double Lon) const
	{
		const double Full = Eco->PlantCover(Lat, Lon) * Settings.GatherableKg;
		auto It = Gathered.find(KeyOf(Lat, Lon));
		return std::max(0.0, Full - (It == Gathered.end() ? 0.0 : It->second));
	}

	// How good a place looks for a camp: food to gather, water and game, minus danger.
	double PlaceScore(double Lat, double Lon) const
	{
		if (!Livable(Lat, Lon))
		{
			return -1.0;
		}
		const double Food = GatherableAt(Lat, Lon) / Settings.GatherableKg;
		const double Water = Eco->WaterAt(Lat, Lon);
		const double Game = std::clamp(Eco->GameNear(Lat, Lon, 1000.0) / 2000.0, 0.0, 1.0);
		const double Danger = std::clamp(Eco->PredatorsNear(Lat, Lon) / 40.0, 0.0, 1.0);
		return 0.5 * Food + 0.3 * std::min(Water, 0.6) + 0.2 * Game - 0.1 * Danger;
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
		for (size_t c = 0; c < Camps.size(); ++c)
		{
			CampSlots[Camps[c].Id] = static_cast<int>(c);
			for (int i : Camps[c].Members)
			{
				Npcs[i].BandSize = static_cast<int>(Camps[c].Members.size());
			}
		}
	}

	// A camp stays while food and water hold out, then heads for the best place its scouts
	// have found, or failing that the best-looking place within a day's walk.
	void MoveCamps()
	{
		for (Camp& C : Camps)
		{
			C.ScoutScore *= 0.995; // Old news gets less trustworthy.
			C.DaysHere += 1;
			const double Here = PlaceScore(C.Lat, C.Lon);
			const double Food = GatherableAt(C.Lat, C.Lon) / std::max(1.0, Eco->PlantCover(C.Lat, C.Lon) * Settings.GatherableKg);
			const bool Stressed = Food < 0.4 || Eco->WaterAt(C.Lat, C.Lon) < 0.2 || (C.DaysHere > 90 && Dice.Chance(0.02));
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
					const double S = PlaceScore(La, Lo);
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
			const double Step = std::min(1.0, 25.0 / Km);
			double DLon = TargetLon - C.Lon;
			if (DLon > 180.0) DLon -= 360.0;
			if (DLon < -180.0) DLon += 360.0;
			const double NewLat = C.Lat + (TargetLat - C.Lat) * Step;
			double NewLon = C.Lon + DLon * Step;
			if (NewLon > 180.0) NewLon -= 360.0;
			if (NewLon < -180.0) NewLon += 360.0;
			if (Livable(NewLat, NewLon))
			{
				C.Lat = NewLat;
				C.Lon = NewLon;
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

	void SplitAndMergeCamps()
	{
		const size_t Existing = Camps.size();
		for (size_t c = 0; c < Existing; ++c)
		{
			if (static_cast<int>(Camps[c].Members.size()) < Settings.SplitSize || !Dice.Chance(0.01))
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
				Offset(Camps[c].Lat, Camps[c].Lon, Angle + Try * 0.8, Dice.Range(30.0, 60.0), New.Lat, New.Lon);
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
		for (size_t a = 0; a < Camps.size(); ++a)
		{
			for (size_t b = a + 1; b < Camps.size(); ++b)
			{
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
			return Age >= 15.0 && Age < 45.0 && P.PregnantDays < 0.0 && Eco->Day() - P.LastBirthDay > 365.0 * 2.5;
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
		P.Fear = static_cast<float>(std::clamp(Eco->PredatorsNear(C.Lat, C.Lon) / 40.0, 0.0, 1.0));
		UpdateLoneliness(P);

		std::array<float, kBrainInputs>& In = P.LastInputs;
		In[0] = 1.0f - P.Energy;
		In[1] = 1.0f - P.Water;
		In[2] = P.Fatigue;
		In[3] = P.Fear;
		In[4] = P.Loneliness;
		In[5] = Fertile(P) ? 1.0f : 0.0f;
		In[6] = static_cast<float>(std::clamp(GatherableAt(C.Lat, C.Lon) / Settings.GatherableKg, 0.0, 1.0));
		In[7] = static_cast<float>(std::clamp(Eco->GameNear(C.Lat, C.Lon, 1000.0) / 2000.0, 0.0, 1.0));
		In[8] = static_cast<float>(Eco->WaterAt(C.Lat, C.Lon));
		In[9] = static_cast<float>(Eco->GrowingSeason(C.Lat));
		In[10] = static_cast<float>(std::clamp(P.AgeYears() / 70.0, 0.0, 1.0));
		In[11] = P.Health;
		In[12] = std::clamp(static_cast<float>(P.BandSize) / 30.0f, 0.0f, 1.0f);
		In[13] = std::clamp(P.CarriedKg / 10.0f, 0.0f, 1.0f);
		In[14] = P.HuntSkill;
		In[15] = 1.0f;
	}

	int Decide(Npc& P)
	{
		const Genome& G = *P.Genes;
		for (int h = 0; h < kBrainHidden; ++h)
		{
			float Sum = G.B1[h];
			for (int i = 0; i < kBrainInputs; ++i)
			{
				Sum += P.LastInputs[i] * G.W1[i * kBrainHidden + h];
			}
			P.LastHidden[h] = std::tanh(Sum);
		}
		std::array<float, kBrainActions> Logit{};
		float Top = -1e9f;
		for (int a = 0; a < kBrainActions; ++a)
		{
			float Sum = OutputBias(P, a);
			for (int h = 0; h < kBrainHidden; ++h)
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
			const double Found = 6.0 * P.ForageSkill * std::min(1.0, Best / 500.0);
			Gathered[KeyOf(BestLat, BestLon)] += Found;
			P.CarriedKg = static_cast<float>(std::min(10.0, P.CarriedKg + Found));
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
			const double Success = std::min(0.6, (0.05 + 0.05 * std::min(Party, 8)) * Game * (0.4 + P.HuntSkill));
			P.HuntSkill = std::min(1.0f, P.HuntSkill + 0.003f);
			P.Fatigue += 0.15f;
			if (Dice.Chance(Success))
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
			Offset(C.Lat, C.Lon, P.Heading, Dice.Range(30.0, 60.0), La, Lo);
			const double Score = PlaceScore(La, Lo);
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

	// A fertile woman with a fertile man in camp may conceive; more likely if she spent the day with others.
	void MaybeConceive(size_t Index, bool Socialized)
	{
		Npc& P = Npcs[Index];
		if (!P.Female || !Fertile(P) || P.Energy < 0.4f || static_cast<int>(Npcs.size()) >= Settings.MaxPeople ||
			!Dice.Chance(Socialized ? 0.04 : 0.01))
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
		// Feeding a child costs the carer food: first from what she carries, then from her own reserves.
		const float Need = 1.0f - Child.Energy;
		const float FromCarried = std::min({Mum.CarriedKg, 1.5f, Need * 5.0f});
		Mum.CarriedKg -= FromCarried;
		Child.Energy += FromCarried * 0.2f;
		const float FromMum = std::min(1.0f - Child.Energy, std::max(0.0f, Mum.Energy - 0.3f) * 0.5f);
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
			for (int h = 0; h < kBrainHidden; ++h)
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
		for (size_t k = 0; k < Learner.LearnedW2.size(); ++k)
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
	void Imitate(size_t Index)
	{
		Npc& P = Npcs[Index];
		if (P.IsChild() || !Dice.Chance(P.Genes->Imitation))
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

	void BirthsAndDeaths()
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
			const double Yearly = 0.0004 * std::exp(0.08 * Age) + (Age < 5.0 ? 0.05 : 0.0) + 0.005;
			// Predators take the lone more than people in a big camp, and miss those who hide.
			const double Alone = 1.0 / (1.0 + 0.3 * std::min(P.BandSize - 1, 20));
			const double Hidden = P.LastAction == static_cast<int>(Action::Hide) ? 0.3 : 1.0;
			if (Dice.Chance(P.Fear * 0.0003 * Alone * Hidden))
			{
				Die(P, 1);
			}
			else if (Dice.Chance(Yearly / 365.0))
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

	void Die(Npc& P, int Cause)
	{
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

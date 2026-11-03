// Tests for the NPCs on a small synthetic planet: a block of land around East Africa.
#include "PlanetCore/People.h"
#include "PlanetCore/WorldSave.h"

#include <cstdio>
#include <memory>
#include <utility>
#include <string>

static int Failures = 0;

#define EXPECT(Cond)                                                     \
	do                                                                   \
	{                                                                    \
		if (!(Cond))                                                     \
		{                                                                \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #Cond); \
			++Failures;                                                  \
		}                                                                \
	} while (0)

// 1-degree global grid with land (300 m) from longitude -20 to 60 and latitude -36 to 70.
static earth::ElevationGrid MakePlanet()
{
	earth::ElevationGrid G;
	G.Width = 360;
	G.Height = 180;
	G.Heights.resize(360 * 180);
	for (int R = 0; R < 180; ++R)
	{
		for (int C = 0; C < 360; ++C)
		{
			const double Lon = -180.0 + C + 0.5;
			const double Lat = 90.0 - R - 0.5;
			G.Heights[R * 360 + C] = (Lon > -20.0 && Lon < 60.0 && Lat > -36.0 && Lat < 70.0) ? 300 : -3000;
		}
	}
	return G;
}

struct World
{
	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco;
	earth::People Folk;
	explicit World(uint64_t Seed) : Eco(&Grid, earth::DefaultSpecies(), Seed), Folk(&Eco, Seed) {}
	void Days(int N)
	{
		for (int i = 0; i < N; ++i)
		{
			Eco.StepDay();
			Folk.StepDay();
		}
	}
};

static bool HasFirst(const earth::People& Folk, const std::string& Start)
{
	for (const earth::EcoEvent& E : Folk.Firsts())
	{
		if (E.Text.rfind(Start, 0) == 0)
		{
			return true;
		}
	}
	return false;
}

static void TestStart()
{
	World W(1);
	EXPECT(W.Folk.Alive() == 30);
	EXPECT(W.Folk.BandCount() == 1);
	for (const earth::Npc& P : W.Folk.All())
	{
		EXPECT(W.Eco.IsLand(P.Lat, P.Lon));
		EXPECT(P.AgeYears() >= 14.0 && P.AgeYears() < 35.0);
	}
}

static void TestDeterministic()
{
	World A(3);
	World B(3);
	A.Days(400);
	B.Days(400);
	EXPECT(A.Folk.Alive() == B.Folk.Alive());
	bool Same = A.Folk.Alive() == B.Folk.Alive();
	for (int i = 0; Same && i < A.Folk.Alive(); ++i)
	{
		Same = A.Folk.All()[i].Id == B.Folk.All()[i].Id && A.Folk.All()[i].Lat == B.Folk.All()[i].Lat &&
			A.Folk.All()[i].LearnedW2 == B.Folk.All()[i].LearnedW2;
	}
	EXPECT(Same);
}

// Over a few decades the band has children, hunts, learns, and stays alive.
static void TestBandLives()
{
	for (uint64_t Seed : {1ull, 2ull})
	{
		World W(Seed);
		W.Days(365 * 40);
		EXPECT(W.Folk.Alive() > 0);
		EXPECT(W.Folk.TotalBirths() > 20);
		EXPECT(W.Folk.MaxGeneration() >= 2);
		EXPECT(W.Folk.HuntsWon() > 0);
		EXPECT(HasFirst(W.Folk, "First child born"));
		EXPECT(HasFirst(W.Folk, "First successful hunt"));

		bool Learned = false;
		for (const earth::Npc& P : W.Folk.All())
		{
			EXPECT(W.Eco.IsLand(P.Lat, P.Lon));
			EXPECT(P.Energy >= 0.0f && P.Energy <= 1.0f && P.Water >= 0.0f && P.Water <= 1.0f);
			for (float V : P.LearnedW2)
			{
				Learned = Learned || V != 0.0f;
			}
		}
		EXPECT(Learned);
		std::printf("  Seed %llu after 40 years: %d people in %d bands, generation %d, %lld born, %lld hunted\n",
			static_cast<unsigned long long>(Seed), W.Folk.Alive(), W.Folk.BandCount(), W.Folk.MaxGeneration(),
			W.Folk.TotalBirths(), W.Folk.HuntsWon());
	}
}

// Children's inborn wiring mixes both parents' and differs from each.
static void TestInheritance()
{
	World W(5);
	W.Days(365 * 3);
	const earth::Npc* Child = nullptr;
	for (const earth::Npc& P : W.Folk.All())
	{
		if (P.Generation == 1)
		{
			Child = &P;
			break;
		}
	}
	EXPECT(Child != nullptr);
	if (!Child)
	{
		return;
	}
	const earth::Npc* Mum = W.Folk.Find(Child->Mother);
	if (Mum)
	{
		EXPECT(Child->Genes != Mum->Genes);
		int SameAsMum = 0;
		for (size_t k = 0; k < Child->Genes->W2.size(); ++k)
		{
			SameAsMum += Child->Genes->W2[k] == Mum->Genes->W2[k];
		}
		EXPECT(SameAsMum > 0 && SameAsMum < static_cast<int>(Child->Genes->W2.size()));
	}
}

static void TestBrainJson()
{
	World W(1);
	W.Days(30);
	const std::string J = W.Folk.BrainJson(W.Folk.All()[0]);
	EXPECT(J.front() == '{' && J.back() == '}');
	EXPECT(J.find("\"w2_learned\"") != std::string::npos);
	EXPECT(J.find("\"Hunger\"") != std::string::npos);
	EXPECT(J.find("nan") == std::string::npos);
}

// When everyone dies, a new band arrives and the run goes on.
static void TestNewBand()
{
	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 8);
	earth::PeopleSettings Settings;
	Settings.StartLat = 0.0;
	Settings.StartLon = -100.0; // Open ocean: nothing to eat or drink.
	earth::People Folk(&Eco, 8, Settings);
	for (int i = 0; i < 400 && Folk.BandsStarted() < 2; ++i)
	{
		Eco.StepDay();
		Folk.StepDay();
	}
	EXPECT(Folk.BandsStarted() >= 2);
	EXPECT(Folk.Alive() > 0);
	EXPECT(HasFirst(Folk, "A new band of 30 arrives"));
}

// Long runs move people a month at a time on several threads; the band still lives, and the
// result doesn't depend on the number of threads.
static void TestMonthSteps()
{
	auto Run = [](int Threads)
	{
		earth::ElevationGrid Grid = MakePlanet();
		earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 2);
		earth::PeopleSettings Settings;
		Settings.Threads = Threads;
		auto Folk = std::make_unique<earth::People>(&Eco, 2, Settings);
		for (int i = 0; i < 12 * 40; ++i)
		{
			Eco.Step(30);
			Folk->Step(30);
		}
		EXPECT(Folk->Alive() > 30);
		EXPECT(Folk->MaxGeneration() >= 2);
		EXPECT(Folk->BandsStarted() == 1);
		for (const earth::Npc& P : Folk->All())
		{
			EXPECT(P.Energy >= 0.0f && P.Energy <= 1.0f && P.Health >= 0.0f && P.Health <= 1.0f);
		}
		return std::make_pair(Folk->Alive(), Folk->TotalBirths());
	};
	const auto One = Run(1);
	const auto Four = Run(4);
	EXPECT(One == Four);
	std::printf("  Month steps after 40 years: %d people, %lld born\n", One.first, One.second);
}

// No children are born where the land can't feed any more people.
static void TestLandLimit()
{
	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 4);
	earth::PeopleSettings Settings;
	Settings.PeoplePerKm2 = 1e-7;
	earth::People Folk(&Eco, 4, Settings);
	EXPECT(Folk.LandCapacity() < 10.0);
	for (int i = 0; i < 365 * 3; ++i)
	{
		Eco.StepDay();
		Folk.StepDay();
	}
	EXPECT(Folk.TotalBirths() == 0);
}

// Every skill's needs come earlier in the list, and deeper skills sit in higher tiers.
static void TestSkillTree()
{
	const std::vector<earth::Skill>& L = earth::AllSkills();
	EXPECT(L.size() <= 64);
	for (size_t i = 0; i < L.size(); ++i)
	{
		for (int N : L[i].Needs)
		{
			EXPECT(N < static_cast<int>(i));
			EXPECT(L[N].Tier < L[i].Tier);
		}
	}
	EXPECT(earth::CanDiscover(0, 0));
	EXPECT(!earth::CanDiscover(0, 26)); // Farming needs planting first.
}

// Growing and shrinking a brain keeps its layers the same size.
static void TestBrainResize()
{
	earth::Genome G;
	earth::Rng Dice(7);
	G.AddNeuron(Dice);
	EXPECT(G.Hidden() == earth::kBrainHidden + 1);
	EXPECT(G.W1.size() == static_cast<size_t>(G.Hidden() * earth::kBrainInputs));
	EXPECT(G.W2.size() == static_cast<size_t>(G.Hidden() * earth::kBrainActions));
	G.RemoveNeuron(0);
	G.RemoveNeuron(0);
	EXPECT(G.Hidden() == earth::kBrainHidden - 1);
	EXPECT(G.W1.size() == static_cast<size_t>(G.Hidden() * earth::kBrainInputs));
}

// With discovery made easy, a band finds skills and passes them on.
static void TestSkillsSpread()
{
	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 5);
	earth::PeopleSettings Settings;
	Settings.DiscoveryPerYear = 0.05;
	earth::People Folk(&Eco, 5, Settings);
	for (int i = 0; i < 365 * 5; ++i)
	{
		Eco.StepDay();
		Folk.StepDay();
	}
	EXPECT(Folk.SkillsKnown() > 0);
	EXPECT(HasFirst(Folk, "First discovery of"));
	// Whoever found it gets a life story, and the land they started in has a name of their own.
	EXPECT(!Folk.NotablePeople().empty());
	EXPECT(Folk.StoriesText().find("was the first ever to discover") != std::string::npos);
	EXPECT(!Folk.LandNames()[0].empty());
	EXPECT(Folk.AverageBrain() > 4.0);
}

// Bands understand each other while they share at least half their words.
static void TestUnderstand()
{
	earth::Camp A;
	earth::Camp B;
	EXPECT(earth::People::Understand(A, B));
	for (int w = 0; w < earth::kWords / 2; ++w) B.Words[w] = 7;
	EXPECT(earth::People::Understand(A, B));
	B.Words[earth::kWords / 2] = 7;
	EXPECT(!earth::People::Understand(A, B));
}

// Every word number can be said, and different numbers usually sound different.
static void TestSayWord()
{
	EXPECT(!earth::SayWord(0).empty());
	EXPECT(earth::SayWord(12345) == earth::SayWord(12345));
	EXPECT(earth::SayWord(12345) != earth::SayWord(54321));
	EXPECT(std::string(earth::WordMeaning(0)) == "water");
}

// Whether a way of working suits the materials depends only on their properties.
static void TestInventionRules()
{
	auto Index = [](const char* Name)
	{
		const auto& L = earth::AllMaterials();
		for (size_t i = 0; i < L.size(); ++i)
		{
			if (std::string(L[i].Name) == Name) return static_cast<int>(i);
		}
		return -1;
	};
	const int Flint = Index("flint"), Clay = Index("clay"), Wood = Index("wood");
	EXPECT(Flint >= 0 && Clay >= 0 && Wood >= 0);
	EXPECT(earth::InventionStrength(0, Flint, -1, 0) > 0.8f); // A struck flint blade.
	EXPECT(earth::InventionStrength(0, Clay, -1, 0) == 0.0f); // Clay won't take an edge.
	const earth::SkillSet Spears = earth::SkillSet(1) << 12;
	EXPECT(earth::InventionStrength(1, Flint, Wood, 0) == 0.0f); // Hafted points grow out of wooden spears...
	EXPECT(earth::InventionStrength(1, Flint, Wood, Spears) > 0.0f); // ...a flint point on a wooden shaft...
	EXPECT(earth::InventionStrength(1, Wood, Flint, Spears) == 0.0f); // ...but not the other way round.
	EXPECT(earth::InventionStrength(7, Clay, -1, earth::SkillSet(1) << 2) == 0.0f); // Fired pots need pottery, not just fire.
	EXPECT(earth::InventionStrength(7, Clay, -1, earth::SkillSet(1) << 28) > 0.0f);
	const uint32_t Id = earth::InventionId(1, Flint, Wood);
	EXPECT(earth::WorkingOf(Id) == 1 && earth::MaterialA(Id) == Flint && earth::MaterialB(Id) == Wood);
	EXPECT(earth::InventionGloss(Id) == "flint point tied to a shaft of wood");
	EXPECT(earth::MaterialB(earth::InventionId(0, Flint, -1)) == -1);
}

// People who try often enough make things, name them, and the words join their language.
static void TestInventionsHappen()
{
	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 6);
	earth::PeopleSettings Settings;
	Settings.InventionTriesPerYear = 2.0; // Few ways of working are open before any skills are known.
	earth::People Folk(&Eco, 6, Settings);
	for (int i = 0; i < 365 * 3 / 30; ++i)
	{
		Eco.Step(30);
		Folk.Step(30);
	}
	EXPECT(Folk.InventionsKnown() > 0);
	EXPECT(HasFirst(Folk, "First invention of"));
	EXPECT(Folk.InventionsText().find("first called") != std::string::npos);
	EXPECT(Folk.StoriesText().find("was the first ever to make") != std::string::npos);
	EXPECT(Folk.LanguagesText().find(" = limestone") != std::string::npos);
}

// A world saved and loaded carries on exactly as if it had never stopped.
static void TestSaveAndResume()
{
	earth::ElevationGrid Grid = MakePlanet();
	earth::PeopleSettings Settings;
	Settings.InventionTriesPerYear = 0.2;
	Settings.Threads = 2;
	earth::Ecosystem EcoA(&Grid, earth::DefaultSpecies(), 7);
	earth::People FolkA(&EcoA, 7, Settings);
	for (int i = 0; i < 12; ++i)
	{
		EcoA.Step(30);
		FolkA.Step(30);
	}
	const std::string Path = "people_tests_save.bin";
	EXPECT(earth::SaveWorld(Path, EcoA, FolkA));
	earth::Ecosystem EcoB(&Grid, earth::DefaultSpecies(), 99);
	earth::People FolkB(&EcoB, 99, Settings);
	EXPECT(earth::LoadWorld(Path, EcoB, FolkB));
	std::remove(Path.c_str());
	for (int i = 0; i < 12; ++i)
	{
		EcoA.Step(30);
		FolkA.Step(30);
		EcoB.Step(30);
		FolkB.Step(30);
	}
	EXPECT(FolkA.Alive() == FolkB.Alive());
	EXPECT(EcoA.TotalAnimals() == EcoB.TotalAnimals());
	EXPECT(FolkA.InventionsText() == FolkB.InventionsText());
	EXPECT(FolkA.LanguagesText() == FolkB.LanguagesText());
	EXPECT(!earth::LoadWorld("no_such_save.bin", EcoB, FolkB));
}

// Bands that can talk come up with beliefs, which spread, and a belief that forbids a material
// keeps the band from using inventions made with it.
static void TestBeliefs()
{
	EXPECT(earth::BeliefGloss(1, 2, 2) == "The sun is where we came from; they dance through the night");
	EXPECT(earth::BeliefGloss(0, 0, 0) == "The dead are alive and watch us; they bury their dead with gifts");
	EXPECT(earth::SubjectName(earth::SubjectCount()) == earth::AllMaterials()[0].Name);
	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 8);
	earth::PeopleSettings Settings;
	Settings.DiscoveryPerYear = 0.2;
	Settings.BeliefTriesPerYear = 0.05;
	earth::People Folk(&Eco, 8, Settings);
	for (int i = 0; i < 365 * 6 / 30; ++i)
	{
		Eco.Step(30);
		Folk.Step(30);
	}
	EXPECT(Folk.BeliefsHeld() > 0);
	EXPECT(HasFirst(Folk, "The first belief:"));
	EXPECT(Folk.BeliefsText().find("first taught in year") != std::string::npos);
	EXPECT(Folk.StoriesText().find("first taught that") != std::string::npos);
	EXPECT(Folk.StoriesText().find("(in their words: \"") != std::string::npos); // Told as a sentence in their speech.
	EXPECT(Folk.LanguagesText().find(" = teach") != std::string::npos); // A word coined to tell it.
}

// People have names, come to trust those who feed them, and distrust whoever doesn't.
static void TestTrust()
{
	earth::Npc A;
	A.Feel(7, 0.2f);
	A.Feel(7, 0.2f);
	EXPECT(A.TrustIn(7) > 0.39f && A.TrustIn(7) < 0.41f);
	A.Feel(8, -0.5f);
	A.Feel(9, 0.1f);
	A.Feel(10, 0.1f);
	// All four slots full: a weak new feeling doesn't push out stronger ones.
	A.Feel(11, 0.05f);
	EXPECT(A.TrustIn(11) == 0.0f && A.TrustIn(8) < 0.0f && A.TrustIn(7) > 0.0f);
	for (int i = 0; i < 20; ++i) A.Feel(7, 0.2f);
	EXPECT(A.TrustIn(7) == 1.0f);

	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 9);
	earth::People Folk(&Eco, 9);
	for (int i = 0; i < 365 * 10 / 30; ++i)
	{
		Eco.Step(30);
		Folk.Step(30);
	}
	int Named = 0, Bonded = 0, Remember = 0, LoveDad = 0;
	for (const earth::Npc& P : Folk.All())
	{
		Named += P.Name != 0;
		Remember += P.RefugeWater > 0.0f;
		LoveDad += P.Father != 0 && P.TrustIn(P.Father) > 0.0f;
		for (uint32_t B : P.BondWith) Bonded += B != 0;
	}
	EXPECT(Named > 0 && Bonded > 0 && Remember > 0);
	EXPECT(LoveDad > 0); // Fathers help feed their young children.
	bool Grieved = false;
	for (const auto& E : Folk.Firsts()) Grieved = Grieved || E.Text.find(" grieves for ") != std::string::npos;
	EXPECT(Grieved); // Someone has lost a person they loved.
	EXPECT(Folk.AverageKindness() > 0.0 && Folk.AverageKindness() < 1.0);
	EXPECT(HasFirst(Folk, "") && Folk.StoriesText().find("first leader") != std::string::npos);
}

// Bands that learn to dry food store it for lean times, and stores let them feed a toolmaker.
static void TestStores()
{
	earth::ElevationGrid Grid = MakePlanet();
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), 6);
	earth::PeopleSettings Settings;
	Settings.DiscoveryPerYear = 0.05; // Skills come within years, so drying is soon known.
	earth::People Folk(&Eco, 6, Settings);
	for (int i = 0; i < 365 * 40 / 30; ++i)
	{
		Eco.Step(30);
		Folk.Step(30);
	}
	bool Stored = false, Toolmaker = false;
	for (const auto& E : Folk.Firsts())
	{
		Stored = Stored || E.Text.find("first planning for a lean season") != std::string::npos;
		Toolmaker = Toolmaker || E.Text.find("first toolmaker") != std::string::npos;
	}
	EXPECT(Stored && Toolmaker);
}

int main()
{
	TestStores();
	TestTrust();
	TestBeliefs();
	TestInventionRules();
	TestInventionsHappen();
	TestSaveAndResume();
	TestUnderstand();
	TestSayWord();
	TestSkillTree();
	TestBrainResize();
	TestSkillsSpread();
	TestStart();
	TestDeterministic();
	TestBandLives();
	TestInheritance();
	TestBrainJson();
	TestNewBand();
	TestMonthSteps();
	TestLandLimit();
	if (Failures == 0)
	{
		std::printf("All people tests passed\n");
		return 0;
	}
	std::printf("%d failure(s)\n", Failures);
	return 1;
}

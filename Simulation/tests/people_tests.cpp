// Tests for the NPCs on a small synthetic planet: a block of land around East Africa.
#include "PlanetCore/People.h"

#include <cstdio>
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

int main()
{
	TestStart();
	TestDeterministic();
	TestBandLives();
	TestInheritance();
	TestBrainJson();
	if (Failures == 0)
	{
		std::printf("All people tests passed\n");
		return 0;
	}
	std::printf("%d failure(s)\n", Failures);
	return 1;
}

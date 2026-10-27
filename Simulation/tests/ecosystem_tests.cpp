// Tests for plants and animals on a small synthetic planet.
#include "PlanetCore/Ecosystem.h"

#include <chrono>
#include <cmath>
#include <cstdio>

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

// 1-degree global grid. Land (Height metres) between longitudes -20 and 60, ocean elsewhere.
static earth::ElevationGrid MakePlanet(int16_t Height = 200)
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
			G.Heights[R * 360 + C] = (Lon > -20.0 && Lon < 60.0) ? Height : -3000;
		}
	}
	return G;
}

static earth::Species Grazer(const char* Name = "Grazer")
{
	earth::Species S;
	S.Name = Name;
	S.Eats = earth::Diet::Plants;
	S.Range = {{-40, 40, -20, 60}};
	S.BodyMassKg = 300;
	S.GroupSize = 200;
	S.StartGroups = 20;
	S.SpeedKmPerDay = 20;
	S.BirthsPerYear = 0.3;
	S.LifespanYears = 20;
	return S;
}

static earth::Species Predator()
{
	earth::Species S;
	S.Name = "Predator";
	S.Eats = earth::Diet::Meat;
	S.Range = {{-40, 40, -20, 60}};
	S.BodyMassKg = 150;
	S.GroupSize = 10;
	S.StartGroups = 10;
	S.SpeedKmPerDay = 25;
	S.BirthsPerYear = 0.3;
	S.LifespanYears = 15;
	S.PreyMassMinKg = 100;
	S.PreyMassMaxKg = 1000;
	return S;
}

static void TestDeterministic()
{
	const earth::ElevationGrid G = MakePlanet();
	earth::Ecosystem A(&G, {Grazer(), Predator()}, 7);
	earth::Ecosystem B(&G, {Grazer(), Predator()}, 7);
	A.StepDays(200);
	B.StepDays(200);
	EXPECT(A.Groups().size() == B.Groups().size());
	bool Same = A.Groups().size() == B.Groups().size();
	for (size_t i = 0; Same && i < A.Groups().size(); ++i)
	{
		Same = A.Groups()[i].Lat == B.Groups()[i].Lat && A.Groups()[i].Count == B.Groups()[i].Count;
	}
	EXPECT(Same);

	earth::Ecosystem C(&G, {Grazer(), Predator()}, 8);
	C.StepDays(200);
	EXPECT(C.Groups().empty() || A.Groups().empty() || C.Groups()[0].Lat != A.Groups()[0].Lat);
}

static void TestStayOnLandAndInRange()
{
	const earth::ElevationGrid G = MakePlanet();
	earth::Ecosystem E(&G, {Grazer(), Predator()}, 1);
	EXPECT(!E.Groups().empty());
	E.StepDays(365);
	for (const earth::AnimalGroup& Grp : E.Groups())
	{
		EXPECT(E.IsLand(Grp.Lat, Grp.Lon));
		EXPECT(E.GetSpecies()[Grp.SpeciesIndex].InRange(Grp.Lat, Grp.Lon));
	}
}

static void TestSeasons()
{
	const earth::ElevationGrid G = MakePlanet();
	earth::Ecosystem E(&G, {}, 1);
	// Day 0 is early January: northern winter.
	E.StepDays(20);
	const double NorthWinter = E.PlantCover(50.0, 20.0);
	const double SouthSummer = E.PlantCover(-30.0, 20.0);
	E.StepDays(180); // Into July.
	const double NorthSummer = E.PlantCover(50.0, 20.0);
	EXPECT(NorthSummer > NorthWinter * 1.3);
	EXPECT(E.GrowingSeason(50.0) > 0.9);
	EXPECT(E.GrowingSeason(0.0) > 0.99);
	EXPECT(SouthSummer > 0.0);
	EXPECT(E.PlantCover(0.0, -100.0) == 0.0); // Ocean has no plants.
}

static void TestGrazersGrowWithFoodAndStarveWithout()
{
	const earth::ElevationGrid Green = MakePlanet(200);
	earth::Ecosystem Fed(&Green, {Grazer()}, 3);
	const double Before = Fed.TotalAnimals();
	Fed.StepDays(365 * 3);
	EXPECT(Fed.TotalAnimals() > Before);

	// A planet of bare 5,000 m rock grows nothing.
	const earth::ElevationGrid Rock = MakePlanet(5000);
	earth::Species S = Grazer();
	S.MaxElevationM = 8000;
	S.Range = {{-40, 40, 0, 40}}; // Away from the coast, where elevation blends down to sea level.
	earth::Ecosystem Hungry(&Rock, {S}, 3);
	EXPECT(Hungry.TotalAnimals() > 0.0);
	Hungry.StepDays(365 * 3);
	EXPECT(Hungry.TotalAnimals() == 0.0);
	EXPECT(Hungry.SpeciesAliveCount() == 0);
	EXPECT(!Hungry.Events().empty() && Hungry.Events()[0].Text == "Grazer died out");
}

static void TestPredatorsEatPrey()
{
	const earth::ElevationGrid G = MakePlanet();
	earth::Ecosystem Alone(&G, {Grazer()}, 5);
	earth::Ecosystem Hunted(&G, {Grazer(), Predator()}, 5);
	Alone.StepDays(365 * 2);
	Hunted.StepDays(365 * 2);
	EXPECT(Hunted.CountOf(0) < Alone.CountOf(0));
	EXPECT(Hunted.CountOf(1) > 0.0);
}

static void TestGroupCap()
{
	const earth::ElevationGrid G = MakePlanet();
	earth::EcosystemSettings Settings;
	Settings.MaxGroups = 60;
	earth::Species S = Grazer();
	S.GroupSize = 5; // Splits constantly.
	earth::Ecosystem E(&G, {S}, 9, Settings);
	E.StepDays(365 * 2);
	EXPECT(static_cast<int>(E.Groups().size()) <= 60);
}

static void TestDefaultSpeciesSpeed()
{
	const earth::ElevationGrid G = MakePlanet();
	earth::Ecosystem E(&G, earth::DefaultSpecies(), 315000);
	const auto Start = std::chrono::steady_clock::now();
	E.StepDays(365);
	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	std::printf("  One year with the default species: %.2f s, %zu groups\n", Seconds, E.Groups().size());
	EXPECT(Seconds < 20.0);
}

int main()
{
	TestDeterministic();
	TestStayOnLandAndInRange();
	TestSeasons();
	TestGrazersGrowWithFoodAndStarveWithout();
	TestPredatorsEatPrey();
	TestGroupCap();
	TestDefaultSpeciesSpeed();
	if (Failures == 0)
	{
		std::printf("All ecosystem tests passed\n");
		return 0;
	}
	std::printf("%d failure(s)\n", Failures);
	return 1;
}

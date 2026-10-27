// Unit tests for the engine-independent planet core.
// Build: see Simulation/CMakeLists.txt
#include "PlanetCore/ElevationGrid.h"
#include "PlanetCore/Noise.h"
#include "PlanetCore/Sphere.h"
#include "PlanetCore/Terrain.h"

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

static bool Near(double A, double B, double Eps = 1e-6) { return std::abs(A - B) <= Eps; }

static earth::ElevationGrid MakeGlobalGrid()
{
	// 4 x 2 global grid: north row all 100 m, south row all -1000 m.
	earth::ElevationGrid G;
	G.Width = 4;
	G.Height = 2;
	G.Heights = {100, 100, 100, 100, -1000, -1000, -1000, -1000};
	return G;
}

static void TestGridSampling()
{
	earth::ElevationGrid G = MakeGlobalGrid();
	EXPECT(G.IsValid());
	EXPECT(G.IsGlobal());
	// Cell centres sit at lat 45 and -45.
	EXPECT(Near(G.Sample(45.0, 0.0), 100.0));
	EXPECT(Near(G.Sample(-45.0, 0.0), -1000.0));
	// Halfway between rows is the average.
	EXPECT(Near(G.Sample(0.0, 0.0), -450.0));
	// Longitude wraps: -180, 180 and 540 are the same place.
	EXPECT(Near(G.Sample(10.0, -180.0), G.Sample(10.0, 180.0)));
	EXPECT(Near(G.Sample(10.0, 540.0), G.Sample(10.0, 180.0)));

	// Bilinear across columns.
	earth::ElevationGrid H;
	H.Width = 2;
	H.Height = 2;
	H.North = 10;
	H.South = 0;
	H.West = 0;
	H.East = 10;
	H.Heights = {0, 100, 0, 100};
	EXPECT(!H.IsGlobal());
	EXPECT(Near(H.Sample(5.0, 5.0), 50.0));
	EXPECT(Near(H.Sample(5.0, 2.5), 0.0));
	EXPECT(Near(H.Sample(5.0, 7.5), 100.0));
	EXPECT(H.Contains(5.0, 5.0));
	EXPECT(!H.Contains(5.0, 20.0));
}

static void TestNoiseDeterminism()
{
	earth::SeededNoise A(315000);
	earth::SeededNoise B(315000);
	earth::SeededNoise C(42);
	int Differences = 0;
	for (int i = 0; i < 100; ++i)
	{
		const double X = i * 0.37 + 0.11;
		const double Y = i * 0.53 + 0.29;
		const double Z = i * 0.71 + 0.43;
		const double Va = A.Fractal(X, Y, Z, 5);
		EXPECT(Va == B.Fractal(X, Y, Z, 5));
		EXPECT(Va >= -1.5 && Va <= 1.5);
		if (std::abs(Va - C.Fractal(X, Y, Z, 5)) > 1e-9)
		{
			++Differences;
		}
	}
	EXPECT(Differences > 50);
}

static void TestSphere()
{
	for (int Face = 0; Face < 6; ++Face)
	{
		for (double U = -1.0; U <= 1.0; U += 0.25)
		{
			for (double V = -1.0; V <= 1.0; V += 0.25)
			{
				const earth::Vec3 D = earth::CubeFaceToDirection(Face, U, V);
				EXPECT(Near(D.Length(), 1.0, 1e-9));
			}
		}
	}

	const earth::LatLon P = earth::DirectionToLatLon(earth::LatLonToDirection(31.6, -8.0));
	EXPECT(Near(P.Lat, 31.6, 1e-9));
	EXPECT(Near(P.Lon, -8.0, 1e-9));

	const earth::LatLon North = earth::DirectionToLatLon({0, 0, 1});
	EXPECT(Near(North.Lat, 90.0));
}

static void TestDetailKeepsCoastlines()
{
	earth::ElevationGrid G = MakeGlobalGrid();
	earth::TerrainSettings S;
	S.SeaLevelMeters = -30.0;
	S.DetailAmplitudeMeters = 5000.0; // Deliberately huge to try to break coastlines.
	earth::Terrain T(&G, nullptr, 315000, S);

	for (int i = 0; i < 2000; ++i)
	{
		const double Lat = -89.0 + (i % 179);
		const double Lon = -180.0 + (i * 7 % 360);
		const earth::Vec3 Dir = earth::LatLonToDirection(Lat, Lon);
		const double Real = T.Height(Dir, false);
		const double Detailed = T.Height(Dir, true);
		EXPECT(T.IsUnderwater(Real) == T.IsUnderwater(Detailed));
	}

	// Same seed gives the same planet.
	earth::Terrain T2(&G, nullptr, 315000, S);
	const earth::Vec3 Dir = earth::LatLonToDirection(12.3, 45.6);
	EXPECT(T.Height(Dir, true) == T2.Height(Dir, true));
}

static void TestRegionPreferred()
{
	earth::ElevationGrid G = MakeGlobalGrid();
	earth::ElevationGrid R;
	R.Width = 2;
	R.Height = 2;
	R.North = 40;
	R.South = 30;
	R.West = -10;
	R.East = 0;
	R.Heights = {777, 777, 777, 777};
	earth::Terrain T(&G, &R, 1, earth::TerrainSettings{});
	EXPECT(Near(T.RealHeight(35.0, -5.0), 777.0));
	EXPECT(!Near(T.RealHeight(35.0, 50.0), 777.0));
}

static void TestColors()
{
	earth::ElevationGrid G = MakeGlobalGrid();
	earth::Terrain T(&G, nullptr, 1, earth::TerrainSettings{});
	const earth::Color Ocean = T.ColorFor(-3000.0, 0.0);
	EXPECT(Ocean.B > Ocean.R);
	const earth::Color Ice = T.ColorFor(500.0, 80.0);
	EXPECT(Ice.R > 0.9 && Ice.G > 0.9);
}

int main()
{
	TestGridSampling();
	TestNoiseDeterminism();
	TestSphere();
	TestDetailKeepsCoastlines();
	TestRegionPreferred();
	TestColors();
	if (Failures == 0)
	{
		std::printf("All core tests passed\n");
		return 0;
	}
	std::printf("%d failure(s)\n", Failures);
	return 1;
}

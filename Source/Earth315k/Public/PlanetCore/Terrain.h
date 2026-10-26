// Combines real elevation data with seeded procedural detail.
// Real data decides the shape of the world; noise only adds detail finer than the data,
// and is never allowed to turn land into sea or sea into land.
#pragma once

#include "ElevationGrid.h"
#include "Noise.h"
#include "Sphere.h"

#include <algorithm>
#include <cmath>

namespace earth
{

struct TerrainSettings
{
	// Sea level relative to today, in metres. Around 315,000 years ago the planet was
	// leaving a warm period, with sea level somewhat below today's (estimate, adjustable).
	double SeaLevelMeters = -30.0;

	// Largest height of procedural detail in metres (0 disables it).
	double DetailAmplitudeMeters = 120.0;

	// Size of the largest detail features in kilometres.
	double DetailScaleKm = 40.0;

	int DetailOctaves = 5;
};

struct Color
{
	double R = 0.0;
	double G = 0.0;
	double B = 0.0;
};

class Terrain
{
public:
	Terrain(const ElevationGrid* InGlobal, const ElevationGrid* InRegion, uint64_t Seed, const TerrainSettings& InSettings)
		: Global(InGlobal), Region(InRegion), Noise(Seed), Settings(InSettings)
	{
	}

	// Real elevation only, preferring the higher-resolution region grid where it covers.
	double RealHeight(double Lat, double Lon) const
	{
		if (Region && Region->IsValid() && Region->Contains(Lat, Lon))
		{
			return Region->Sample(Lat, Lon);
		}
		if (Global && Global->IsValid())
		{
			return Global->Sample(Lat, Lon);
		}
		return 0.0;
	}

	// Real elevation plus constrained procedural detail, in metres relative to today's sea level.
	double Height(const Vec3& Dir, bool bWithDetail) const
	{
		const LatLon P = DirectionToLatLon(Dir);
		const double Base = RealHeight(P.Lat, P.Lon);
		if (!bWithDetail || Settings.DetailAmplitudeMeters <= 0.0)
		{
			return Base;
		}
		return ApplyDetail(Base, Dir);
	}

	double ApplyDetail(double Base, const Vec3& Dir) const
	{
		const double Sea = Settings.SeaLevelMeters;
		const double Freq = kEarthRadiusMeters / (Settings.DetailScaleKm * 1000.0);
		const Vec3 N = Dir.Normalized() * Freq;
		const double D = Noise.Fractal(N.X, N.Y, N.Z, Settings.DetailOctaves);

		// More relief in high country, gentle detail on plains and the sea floor.
		const double Relief = std::clamp(0.25 + std::abs(Base - Sea) / 1500.0, 0.25, 1.0);
		const double Result = Base + D * Settings.DetailAmplitudeMeters * Relief;

		// Keep coastlines where the real data puts them.
		if (Base > Sea)
		{
			return std::max(Result, Sea + 1.0);
		}
		return std::min(Result, Sea - 1.0);
	}

	bool IsUnderwater(double HeightMeters) const { return HeightMeters <= Settings.SeaLevelMeters; }

	// Simple first-pass colouring by depth, altitude and latitude.
	Color ColorFor(double HeightMeters, double Lat) const
	{
		const double Sea = Settings.SeaLevelMeters;
		if (HeightMeters <= Sea)
		{
			const double T = std::clamp((Sea - HeightMeters) / 5000.0, 0.0, 1.0);
			return Mix({0.20, 0.55, 0.75}, {0.02, 0.08, 0.25}, std::sqrt(T));
		}

		const double E = HeightMeters - Sea;
		const double AbsLat = std::abs(Lat);
		if (AbsLat > 72.0 || E > 4500.0)
		{
			return {0.95, 0.96, 0.98};
		}

		Color C;
		if (E < 1500.0)
		{
			C = Mix({0.25, 0.45, 0.18}, {0.50, 0.42, 0.30}, E / 1500.0);
		}
		else if (E < 3000.0)
		{
			C = Mix({0.50, 0.42, 0.30}, {0.55, 0.55, 0.55}, (E - 1500.0) / 1500.0);
		}
		else
		{
			C = Mix({0.55, 0.55, 0.55}, {0.95, 0.96, 0.98}, (E - 3000.0) / 1500.0);
		}

		// Subtropical desert belt; a placeholder until a real climate model drives biomes.
		const double Desert = std::clamp(1.0 - std::abs(AbsLat - 24.0) / 8.0, 0.0, 1.0) * 0.7;
		C = Mix(C, {0.82, 0.72, 0.50}, Desert);

		// Tundra toward the poles.
		const double Tundra = std::clamp((AbsLat - 58.0) / 14.0, 0.0, 1.0);
		return Mix(C, {0.60, 0.62, 0.55}, Tundra);
	}

	const TerrainSettings& GetSettings() const { return Settings; }

private:
	const ElevationGrid* Global;
	const ElevationGrid* Region;
	SeededNoise Noise;
	TerrainSettings Settings;

	static Color Mix(const Color& A, const Color& B, double T)
	{
		T = std::clamp(T, 0.0, 1.0);
		return {A.R + (B.R - A.R) * T, A.G + (B.G - A.G) * T, A.B + (B.B - A.B) * T};
	}
};

} // namespace earth

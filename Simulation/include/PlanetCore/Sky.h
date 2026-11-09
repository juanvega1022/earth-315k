#pragma once

// The sky over the world: the Earth's orbit, which paces the ice ages; the Sun, which slowly
// brightens until the oceans boil; the Moon; and what falls from the sky or bursts from below.
// Times are in years from today (negative in the past).

#include <algorithm>
#include <cmath>

namespace earth
{
namespace sky
{
inline constexpr double kTau = 6.283185307179586;

// The orbit's three cycles (Milankovitch), each a few leading terms with today's values:
// eccentricity, the stretch of the orbit (about 405,000, 95,000 and 124,000 years; 0.017 today).
inline double Eccentricity(double T)
{
	const double E = 0.0318 - 0.0115 * std::cos(kTau * T / 405000.0) - 0.0085 * std::cos(kTau * (T - 25000.0) / 95000.0) -
		0.006 * std::cos(kTau * (T - 15000.0) / 124000.0);
	return std::max(0.001, E);
}

// Obliquity, the tilt of the axis in degrees (22.5 to 24.5 over 41,000 years; 23.4 today, falling).
inline double Tilt(double T) { return 23.5 + std::cos(kTau * (T + 10700.0) / 41000.0); }

// Climatic precession, e sin(perihelion): positive when northern summer comes near the Sun, as it
// did 11,000 years ago; today it comes far from the Sun.
inline double Precession(double T) { return Eccentricity(T) * std::sin(kTau * (T + 16750.0) / 23000.0); }

// Midsummer sunshine at 65 degrees north in W/m^2 (about 420 to 530), which decides whether
// northern snow lasts through summer and builds ice sheets.
inline double NorthernSummerSun(double T) { return 470.0 + 1050.0 * Precession(T) + 11.0 * (Tilt(T) - 23.3); }

// Ice sheet volume (0 none beyond today's, 1 the deepest glacial) after Years more years: ice
// builds slowly in weak summers and wastes slowly in strong ones, and once the sheets are big a
// strong summer brings them down in a few thousand years (the 100,000-year sawtooth).
// shortcut: a conceptual model (in the spirit of Paillard 1998) tuned by eye to the last million
// years, not a real ice sheet.
struct IceSheets
{
	double Volume = 0.0;
	bool Melting = false;

	void Advance(double FromT, double Years)
	{
		constexpr double Step = 500.0;
		for (double Done = 0.0; Done < Years; Done += Step)
		{
			const double Dt = std::min(Step, Years - Done);
			const double Sun = NorthernSummerSun(FromT + Done);
			if (Melting)
			{
				Volume -= Volume * Dt / 4000.0;
				Melting = Volume > 0.05;
				continue;
			}
			const double Push = (485.0 - Sun) / 40.0;
			Volume = std::clamp(Volume + Push * Dt / (Push > 0.0 ? 30000.0 : 45000.0), 0.0, 1.0);
			Melting = Volume > 0.7 && Sun > 480.0;
		}
	}
};

// The Sun brightens by about 1% every 110 million years (Gough 1981), as a fraction over today.
inline double Brightening(double T) { return T > 0.0 ? 0.4 * T / 4.57e9 : 0.0; }

// How much warmer the world is than today from the brighter Sun, in degrees C (about 2.3 per 1%).
// shortcut: ignores the slow carbon cycle that partly offsets it, and people's own warming.
inline double SunWarming(double T) { return 230.0 * Brightening(T); }

// How much of today's plant growth the air can still feed (1 now). As the Sun brightens, rock
// weathering draws carbon dioxide down until, some 600 million years from now, most plants can no
// longer photosynthesise, and the last ones go before the oceans boil away at about a billion.
inline double PlantAir(double T) { return std::clamp((9.5e8 - T) / 3.5e8, 0.0, 1.0); }

// Ice ages fade as the Sun warms the world: none once it is about 2.5 degrees warmer.
inline double IceAgeStrength(double T) { return std::clamp(1.0 - SunWarming(T) / 2.5, 0.0, 1.0); }

// Days since a new moon (0 new .. 14.8 full .. 29.5 new again).
inline constexpr double kLunarMonth = 29.530589;
inline double MoonAge(double Day) { return std::fmod(std::fmod(Day, kLunarMonth) + kLunarMonth, kLunarMonth); }
// How much of the Moon is lit (0 new .. 1 full).
inline double MoonLight(double Day) { return 0.5 - 0.5 * std::cos(kTau * MoonAge(Day) / kLunarMonth); }

// Rare disasters, with how often they come (on average, years between them), how hard and long
// the winter they bring is (0..1 of plant growth lost, in years) and how far the blast kills.
struct Disaster
{
	const char* Name;
	double MeanYears;
	double Winter;
	double WinterYears;
	double RadiusKm;
	double WorldKill; // Share of every animal group killed by the heat and fire worldwide.
};

inline const Disaster* Disasters(int& Count)
{
	static const Disaster List[] = {
		{"a super-eruption", 60000.0, 0.3, 6.0, 300.0, 0.0},
		{"an asteroid a kilometre wide", 600000.0, 0.4, 3.0, 500.0, 0.0},
		{"an asteroid five kilometres wide", 2.0e7, 0.8, 5.0, 2000.0, 0.2},
		{"an asteroid ten kilometres wide, like the one that ended the dinosaurs", 1.0e8, 0.97, 10.0, 4000.0, 0.6}};
	Count = static_cast<int>(sizeof(List) / sizeof(List[0]));
	return List;
}

// Where super-eruptions come from: the calderas that have made them before.
struct Caldera
{
	const char* Name;
	double Lat, Lon;
};

inline const Caldera* Calderas(int& Count)
{
	static const Caldera List[] = {{"Toba", 2.6, 98.8}, {"Yellowstone", 44.4, -110.7}, {"Campi Flegrei", 40.8, 14.1},
		{"Taupo", -38.8, 176.0}, {"Aira", 31.6, 130.7}, {"Long Valley", 37.7, -118.9}, {"La Garita", 37.9, -106.9}};
	Count = static_cast<int>(sizeof(List) / sizeof(List[0]));
	return List;
}

// Toba's real eruption, about 74,000 years ago: a short, partial winter (its effect on people is
// still argued over, so it is kept mild).
inline constexpr double kTobaYearsAgo = 74000.0;
} // namespace sky
} // namespace earth

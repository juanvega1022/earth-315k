// Inventions built from pieces: materials found in the world, each with properties, and ways of
// working them. Nobody lists the inventions themselves: a band tries a way of working one or two
// materials it has met, and if their properties suit that way, it has made something new, which
// it names in its own words and can teach. The world decides what is possible; the people find it.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Skills.h"

namespace earth
{

enum Prop : int { Hard, Edge, Flex, Fiber, Sticky, Burns, Mould, Color, Heavy, Floats, Warm, Watertight, Long, kProps };

// Where a material can be found.
enum class Found : int
{
	Anywhere,   // Some of it near almost any camp.
	Stone,      // In some regions, by geology: Chance is the share of regions that have it.
	Places,     // Only in listed areas, where it really occurs.
	Water,      // Wet places, rivers and lakes.
	Coast,      // By the sea.
	Trees,      // Wooded land.
	WetTropics, // Warm, wet forest.
	Game,       // Taken from hunted animals.
	ColdGame,   // From animals of cold lands.
	Cold,       // Snow and ice.
	Fire,       // Left behind by fires, once a band keeps fire.
};

// A rough box of latitude and longitude.
struct Area
{
	float Lat0, Lat1, Lon0, Lon1;
};

struct Material
{
	const char* Name;
	std::array<float, kProps> P;
	Found Where;
	float Chance = 1.0f;
	std::vector<Area> Areas;

	Material(const char* N, std::array<float, kProps> Props, Found W, float C = 1.0f, std::vector<Area> A = {})
		: Name(N), P(Props), Where(W), Chance(C), Areas(std::move(A))
	{
	}
};

inline const std::vector<Material>& AllMaterials()
{
	//                                   Hard Edge Flex Fibr Stik Burn Mold Colr Hevy Flot Warm Wtrt Long
	static const std::vector<Material> List = {
		{"flint",       {.9f, .95f, 0, 0, 0, 0, 0, .1f, .4f, 0, 0, 0, 0}, Found::Stone, 0.3f},
		{"obsidian",    {.8f, 1.f, 0, 0, 0, 0, 0, .3f, .4f, 0, 0, 0, 0}, Found::Places, 1.f,
			// Volcanic glass where it really occurs: the Rift Valley, Anatolia and the Caucasus, the
			// Mediterranean islands, Mexico, the Andes, the western US, Japan, New Zealand.
			{{-5, 15, 33, 43}, {36, 42, 38, 46}, {36, 41, 8, 16}, {14, 22, -104, -96}, {-20, 0, -80, -66},
			 {35, 46, -123, -112}, {32, 44, 129, 145}, {-40, -36, 174, 178}}},
		{"quartzite",   {.9f, .6f, 0, 0, 0, 0, 0, .1f, .6f, 0, 0, 0, 0}, Found::Stone, 0.5f},
		{"basalt",      {.9f, .3f, 0, 0, 0, 0, 0, .1f, .9f, 0, 0, 0, 0}, Found::Stone, 0.4f},
		{"limestone",   {.6f, .1f, 0, 0, 0, 0, 0, .2f, .8f, 0, 0, 0, 0}, Found::Anywhere},
		{"ochre",       {.2f, 0, 0, 0, 0, 0, .2f, .95f, .3f, 0, 0, 0, 0}, Found::Stone, 0.4f},
		{"clay",        {0, 0, 0, 0, .2f, 0, .9f, .3f, .3f, 0, 0, .7f, 0}, Found::Water},
		{"mud",         {0, 0, 0, 0, .1f, 0, .5f, .1f, .3f, 0, 0, .2f, 0}, Found::Water},
		{"wood",        {.5f, .1f, .5f, .2f, 0, .9f, 0, 0, .3f, .7f, .1f, .2f, .8f}, Found::Trees},
		{"bark",        {.2f, 0, .5f, .7f, 0, .6f, 0, .1f, .1f, .5f, .2f, .5f, 0}, Found::Trees},
		{"reed",        {.2f, .1f, .6f, .6f, 0, .5f, 0, 0, 0, .9f, 0, .3f, .6f}, Found::Water},
		{"grass",       {0, 0, .3f, .5f, 0, .7f, 0, 0, 0, .2f, .2f, 0, 0}, Found::Anywhere},
		{"vine",        {0, 0, .9f, .8f, 0, .2f, 0, 0, .1f, .2f, 0, 0, 0}, Found::WetTropics},
		{"resin",       {0, 0, 0, 0, .9f, .8f, .3f, .2f, 0, 0, 0, .6f, 0}, Found::Trees},
		{"bamboo",      {.5f, .3f, .7f, .3f, 0, .6f, 0, 0, .2f, .9f, 0, .6f, .9f}, Found::Places, 1.f,
			{{-10, 35, 70, 150}}},
		{"bone",        {.5f, .4f, .1f, 0, 0, .1f, 0, .2f, .3f, 0, 0, 0, .5f}, Found::Game},
		{"antler",      {.7f, .3f, .2f, 0, 0, 0, 0, .1f, .3f, 0, 0, 0, .6f}, Found::ColdGame},
		{"hide",        {.1f, 0, .6f, .3f, 0, 0, 0, .1f, .2f, .2f, .7f, .6f, 0}, Found::Game},
		{"fur",         {0, 0, .5f, .2f, 0, 0, 0, .2f, .1f, .1f, .95f, .3f, 0}, Found::ColdGame},
		{"sinew",       {0, 0, .7f, .9f, .2f, 0, 0, 0, 0, 0, 0, 0, 0}, Found::Game},
		{"shell",       {.4f, .5f, 0, 0, 0, 0, 0, .5f, .2f, .1f, 0, .5f, 0}, Found::Coast},
		{"feathers",    {0, 0, .4f, .1f, 0, .3f, 0, .7f, 0, .3f, .4f, .2f, 0}, Found::Game},
		{"snow",        {.3f, 0, 0, 0, 0, 0, .4f, 0, .5f, 0, .3f, 0, 0}, Found::Cold},
		{"charcoal",    {.1f, 0, 0, 0, 0, .7f, 0, .6f, .1f, .3f, 0, 0, 0}, Found::Fire},
		{"copper",      {.6f, .4f, .3f, 0, 0, 0, 0, .6f, .4f, 0, 0, .8f, 0}, Found::Places, 1.f,
			// Where native copper and rich ores really are: Anatolia and Iran, Cyprus, the Balkans,
			// the Sinai, the Great Lakes, the Andes, central Africa.
			{{36, 41, 30, 48}, {34, 36, 32, 35}, {42, 46, 20, 26}, {28, 31, 33, 36}, {45, 49, -92, -84},
			 {-30, -15, -72, -66}, {-14, -9, 25, 29}}},
	};
	return List;
}

// A way of working one material, or one with a second. The first must have NeedA of at least MinA,
// the second NeedB of at least MinB; how strongly they have those decides how well it works.
struct Working
{
	const char* Gloss; // English description; {A} and {B} are the materials.
	int NeedA;
	float MinA;
	int NeedB; // -1 if it works one material alone.
	float MinB;
	int NeedsSkill; // -1 if none.
	int Effect;     // A SkillEffects field: 0 forage, 1 hunt, 2 predator deaths, 3 child deaths, 4 warmth, 5 travel, 6 land, 7 carry, 8 teaching.
	float Scale;
};

inline const std::vector<Working>& AllWorkings()
{
	enum { Forage, Hunt, Predators, Children, Warmth, Travel, Land, Carry, Teaching };
	static const std::vector<Working> List = {
		{"struck {A} blade", Edge, .5f, -1, 0, -1, Hunt, .05f},
		{"{A} point tied to a shaft of {B}", Edge, .5f, Long, .5f, -1, Hunt, .12f},
		{"{B} edge set with {A}", Sticky, .5f, Edge, .5f, -1, Hunt, .06f},
		{"{A} snare tied with {B}", Flex, .5f, Fiber, .5f, -1, Hunt, .08f},
		{"{A} hand stone on a {B} slab", Hard, .6f, Heavy, .5f, -1, Forage, .08f},
		{"twisted {A} cord", Fiber, .5f, -1, 0, -1, Carry, .10f},
		{"{A} woven over {B}", Fiber, .6f, Flex, .4f, -1, Carry, .15f},
		{"fired {A} pot", Mould, .5f, -1, 0, 2, Carry, .20f},
		{"{A} cloak scraped with {B}", Warm, .5f, Edge, .5f, -1, Warmth, .12f},
		{"stacked {A} wall", Heavy, .5f, -1, 0, -1, Predators, -.15f},
		{"{A} float shaped with {B}", Floats, .5f, Edge, .5f, -1, Travel, .40f},
		{"boiling water in {A}", Watertight, .5f, -1, 0, 2, Children, -.06f},
		{"burning {A} to clear land", Burns, .6f, -1, 0, 2, Land, .10f},
		{"{A} marks on {B}", Color, .5f, Hard, .3f, -1, Teaching, .08f},
	};
	return List;
}

// An invention is a way of working plus one or two materials, packed in one number.
inline constexpr uint32_t kNoMaterial = 63;
inline uint32_t InventionId(int Working, int A, int B) { return (static_cast<uint32_t>(Working) << 12) | (static_cast<uint32_t>(A) << 6) | static_cast<uint32_t>(B < 0 ? kNoMaterial : B); }
inline int WorkingOf(uint32_t Id) { return static_cast<int>(Id >> 12); }
inline int MaterialA(uint32_t Id) { return static_cast<int>((Id >> 6) & 63u); }
inline int MaterialB(uint32_t Id) { return (Id & 63u) == kNoMaterial ? -1 : static_cast<int>(Id & 63u); }

// How well it works (0 = it doesn't): the materials' strengths in what the working needs.
inline float InventionStrength(int W, int A, int B, SkillSet Known)
{
	const std::vector<Working>& Ws = AllWorkings();
	const std::vector<Material>& Ms = AllMaterials();
	if (W < 0 || W >= static_cast<int>(Ws.size()) || A < 0 || A >= static_cast<int>(Ms.size())) return 0.0f;
	const Working& K = Ws[static_cast<size_t>(W)];
	if (K.NeedsSkill >= 0 && !Knows(Known, K.NeedsSkill)) return 0.0f;
	const float a = Ms[static_cast<size_t>(A)].P[static_cast<size_t>(K.NeedA)];
	if (a < K.MinA) return 0.0f;
	if (K.NeedB < 0) return B < 0 ? a : 0.0f;
	if (B < 0 || B == A || B >= static_cast<int>(Ms.size())) return 0.0f;
	const float b = Ms[static_cast<size_t>(B)].P[static_cast<size_t>(K.NeedB)];
	return b < K.MinB ? 0.0f : a * b;
}

inline std::string InventionGloss(uint32_t Id)
{
	std::string Out = AllWorkings()[static_cast<size_t>(WorkingOf(Id))].Gloss;
	auto Put = [&Out](const char* Key, int M)
	{
		const size_t At = Out.find(Key);
		if (At != std::string::npos && M >= 0) Out.replace(At, 3, AllMaterials()[static_cast<size_t>(M)].Name);
	};
	Put("{A}", MaterialA(Id));
	Put("{B}", MaterialB(Id));
	return Out;
}

// What a band's inventions do for it: for each way of working, only its best invention counts.
inline void AddInventionEffects(const std::vector<uint32_t>& Known, SkillSet Skills, SkillEffects& E)
{
	const std::vector<Working>& Ws = AllWorkings();
	std::array<float, 32> Best{};
	for (uint32_t Id : Known)
	{
		const int W = WorkingOf(Id);
		const float S = InventionStrength(W, MaterialA(Id), MaterialB(Id), Skills);
		if (W < static_cast<int>(Best.size()) && S > Best[static_cast<size_t>(W)]) Best[static_cast<size_t>(W)] = S;
	}
	SkillEffects Add;
	float* Fields[] = {&Add.Forage, &Add.Hunt, &Add.PredatorDeaths, &Add.ChildDeaths, &Add.Warmth, &Add.Travel, &Add.Land, &Add.Carry, &Add.Teaching};
	for (size_t w = 0; w < Ws.size(); ++w)
	{
		if (Best[w] <= 0.0f) continue;
		float& F = *Fields[Ws[w].Effect];
		if (Ws[w].Effect == 4) F += Ws[w].Scale * Best[w];
		else F *= 1.0f + Ws[w].Scale * Best[w];
	}
	E.Add(Add);
}

// Whether a region has a stone, the same answer every time: regions are 4-degree squares.
inline bool RegionHas(double Lat, double Lon, int Material, float Chance)
{
	uint32_t H = static_cast<uint32_t>(static_cast<int>(std::floor(Lat / 4.0)) + 1000) * 73856093u ^
		static_cast<uint32_t>(static_cast<int>(std::floor(Lon / 4.0)) + 1000) * 19349663u ^ static_cast<uint32_t>(Material + 1) * 83492791u;
	H ^= H >> 13;
	H *= 0x5bd1e995u;
	H ^= H >> 15;
	return (H % 10000u) < static_cast<uint32_t>(Chance * 10000.0f);
}

inline bool InArea(double Lat, double Lon, const std::vector<Area>& Areas)
{
	for (const Area& A : Areas)
	{
		if (Lat >= A.Lat0 && Lat <= A.Lat1 && Lon >= A.Lon0 && Lon <= A.Lon1) return true;
	}
	return false;
}

} // namespace earth

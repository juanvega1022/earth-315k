// The crops people really first grew, each where its wild ancestor grows. A band that knows
// planting and lives where a crop's wild plants grow starts sowing it; neighbours who can plant
// pick it up, as far as their climate lets it grow.
#pragma once

#include "Species.h"

#include <cmath>
#include <vector>

namespace earth
{

struct Crop
{
	const char* Name;
	RangeBox Wild;    // Where the wild ancestor grows, so where it can first be sown.
	double MinAbsLat; // How far from the equator it can be grown, either side.
	double MaxAbsLat;

	bool Grows(double Lat) const { return std::abs(Lat) >= MinAbsLat && std::abs(Lat) <= MaxAbsLat; }
};

// At most 16 (a band's crops are a 16-bit set); new ones go at the end, since saves store the set.
inline const std::vector<Crop>& AllCrops()
{
	static const std::vector<Crop> List = {
		{"wheat", {30, 40, 30, 50}, 20, 60},     // The Fertile Crescent.
		{"barley", {30, 40, 30, 50}, 15, 65},
		{"lentils", {30, 40, 30, 50}, 20, 50},
		{"rice", {20, 35, 100, 122}, 0, 45},     // The Yangtze.
		{"millet", {33, 42, 105, 120}, 10, 55},  // The Yellow River.
		{"maize", {14, 22, -105, -88}, 0, 50},   // Mexico.
		{"beans", {14, 22, -105, -88}, 0, 50},
		{"squash", {14, 22, -105, -88}, 0, 45},
		{"potatoes", {-20, 0, -80, -65}, 0, 60}, // The Andes.
		{"sorghum", {8, 18, -10, 40}, 0, 35},    // The Sahel.
		{"yams", {4, 12, -15, 10}, 0, 25},       // West Africa.
		{"taro and bananas", {-10, 0, 130, 150}, 0, 25}, // New Guinea.
		{"sunflowers", {30, 45, -100, -80}, 25, 55},     // Eastern North America.
	};
	return List;
}

} // namespace earth

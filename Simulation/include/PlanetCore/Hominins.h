// The other kinds of human alive when the run starts, each where its fossils and caves are found.
// They run on the same minds as the NPCs, with their own bodies: Neanderthals built for the cold,
// smaller-brained kinds that find new things more slowly. Whether they last, mix with the NPCs or
// die out is left to the world, not written in.
#pragma once

#include <vector>

namespace earth
{

struct HomininKind
{
	const char* Name;       // "Neanderthal"
	const char* Plural;     // "Neanderthals"
	double LastYearsAgo;    // When the real ones were last seen: they start only in runs before this.
	std::vector<std::pair<double, double>> Homes; // Where a band starts: lat, lon.
	int FewerNeurons;       // A smaller brain than the NPCs' at the start.
	float ColdHardy;        // Share of the cost of cold they don't feel (0..1).
	float Appetite;         // Extra food a day for a bigger body (energy, like the brain's cost).
	std::vector<int> Skills; // What they already know (indices into AllSkills()): they had long lived there.
	float Fertility;        // Children compared with NPCs: the others lived thinly spread, in small groups.
};

// Kind 0 is the NPCs themselves (early Homo sapiens); the rest are listed here from 1.
inline const std::vector<HomininKind>& OtherKinds()
{
	static const std::vector<HomininKind> List = {
		{"NPC", "NPCs", 0.0, {}, 0, 0.0f, 0.0f, {}, 1.0f},
		// Atapuerca, the Rhine, the Caucasus, the Zagros.
		{"Neanderthal", "Neanderthals", 40000.0, {{42.3, -3.5}, {50.5, 7.0}, {43.5, 42.0}, {36.8, 45.0}}, 0, 0.35f, 0.02f,
			{0, 1, 2, 3, 6, 7, 8, 9, 10, 12}, 0.85f},
		// Denisova Cave in the Altai, and Baishiya Karst Cave high on the Tibetan Plateau.
		{"Denisovan", "Denisovans", 30000.0, {{51.4, 84.7}, {35.5, 102.5}}, 0, 0.2f, 0.01f, {0, 1, 2, 3, 6, 7, 8, 9, 10, 12}, 0.85f},
		// Ngandong on the Solo River, Java.
		{"Homo erectus", "Homo erectus", 108000.0, {{-7.4, 111.4}, {-7.0, 110.0}}, 4, 0.0f, 0.0f, {0, 1, 2, 6}, 0.6f},
		// The Rising Star cave, South Africa.
		{"Homo naledi", "Homo naledi", 236000.0, {{-26.0, 27.7}}, 6, 0.0f, 0.0f, {0, 1}, 0.5f},
		// Liang Bua, Flores.
		{"Homo floresiensis", "Homo floresiensis", 50000.0, {{-8.5, 120.4}}, 6, 0.0f, -0.02f, {0, 1, 2}, 0.5f},
	};
	return List;
}

} // namespace earth

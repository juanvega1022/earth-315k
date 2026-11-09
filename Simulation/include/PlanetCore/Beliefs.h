// Beliefs built from parts: something in the people's world (a subject), what they come to think
// about it (a claim) and what they do because of it (a practice). Nobody writes the religions: a
// band facing hardship it can't explain puts it down to something it knows, and the belief spreads,
// splits and competes. Practices cost something and hold a band together; claims that a material is
// forbidden can stop the band using what someone invents with it.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

#include "Inventions.h"
#include "Skills.h"

namespace earth
{

// Subjects 0..kSubjects-1 are listed here; kSubjects + m is material m.
inline const std::vector<const char*>& BeliefSubjects()
{
	static const std::vector<const char*> List = {"the dead", "the sun", "the night sky", "water", "fire", "the land",
		"the beasts that hunt us", "the animals we hunt", "the sea", "the cold", "children", "the moon", "the stars"};
	return List;
}
inline int SubjectCount() { return static_cast<int>(BeliefSubjects().size()); }
inline constexpr int kMoonSubject = 11, kStarsSubject = 12;
// Subjects before the moon and the stars came in (save version 11): materials then started here.
inline constexpr int kOldSubjectCount = 11;

inline const std::vector<const char*>& BeliefClaims()
{
	static const std::vector<const char*> List = {"is alive and watches us", "must be fed or it will take from us", "is where we came from",
		"is forbidden to touch", "gives life and takes it", "will bring back those we lost", "speaks in the dreams of the old"};
	return List;
}

// The same claims about something plural, such as the dead.
inline const std::vector<const char*>& BeliefClaimsPlural()
{
	static const std::vector<const char*> List = {"are alive and watch us", "must be fed or they will take from us", "are where we came from",
		"are forbidden to touch", "give life and take it", "will bring back those we lost", "speak in the dreams of the old"};
	return List;
}
inline constexpr int kForbidden = 3;
// A belief held by this many bands is a religion: far more than one band and its offshoots.
inline constexpr int kReligionBands = 50;

struct Practice
{
	const char* Gloss;
	float Cohesion;   // How much it holds a band together (0..1 summed over its beliefs, capped at 1).
	int Effect;       // A SkillEffects field as in Working::Effect, or -1.
	float Factor;     // Multiplier for that field (or amount added, for warmth).
	int NeedsSkill;   // -1 if none.
	int NeedsMaterial; // -1 if none: a material the band must have met.
	int NeedsWorking;  // -1 if none: a way of working the band must have used.
	bool AnimalsOnly; // Only about animals.
};

inline const std::vector<Practice>& BeliefPractices()
{
	enum { Forage, Hunt, Predators, Children, Warmth, Travel, Land, Carry, Teaching };
	static const std::vector<Practice> List = {
		{"they bury their dead with gifts", .20f, Carry, .98f, -1, -1, -1, false},
		{"they leave food as offerings", .15f, Forage, .97f, -1, -1, -1, false},
		{"they dance through the night", .25f, -1, 1.f, -1, -1, -1, false},
		{"they paint their bodies with ochre", .20f, -1, 1.f, -1, 5, -1, false},
		{"they never hunt them", .10f, Hunt, .95f, -1, -1, -1, true},
		{"they keep a fire that must never go out", .15f, Predators, .95f, 2, -1, -1, false},
		{"they chant over the sick", .10f, Children, .98f, -1, -1, -1, false},
		{"they share everything", .30f, -1, 1.f, -1, -1, -1, false},
		{"they will not leave their sacred place", .20f, Travel, .80f, -1, -1, -1, false},
		{"they carve sacred signs", .20f, Teaching, 1.05f, -1, -1, 13, false},
		{"they sacrifice the first animal of each hunt", .25f, Carry, .97f, -1, -1, -1, false},
		// The first calendar: counting moons tells them when the herds and the fruit will come back.
		{"they count the days by the moon", .15f, Forage, 1.04f, -1, -1, -1, false},
	};
	return List;
}

inline constexpr int kMoonPractice = 11; // Only for beliefs about the moon.

inline std::string SubjectName(int S)
{
	if (S < SubjectCount()) return BeliefSubjects()[static_cast<size_t>(S)];
	const int M = S - SubjectCount();
	return M < static_cast<int>(AllMaterials().size()) ? AllMaterials()[static_cast<size_t>(M)].Name : "?";
}

// "The sun is where we came from, and they dance through the night"
inline std::string BeliefGloss(int Subject, int Claim, int Practice)
{
	std::string S = SubjectName(Subject);
	S[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(S[0])));
	const bool Plural = Subject == 0 || Subject == 6 || Subject == 7 || Subject == 10 || S.back() == 's';
	return S + " " + (Plural ? BeliefClaimsPlural() : BeliefClaims())[static_cast<size_t>(Claim)] + "; " + BeliefPractices()[static_cast<size_t>(Practice)].Gloss;
}

} // namespace earth

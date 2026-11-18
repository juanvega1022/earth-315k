#pragma once

// Sicknesses that spread from band to band. New ones jump to people from the animals they keep, or
// arise where many live crowded together; they pass to bands nearby, along a state's roads and
// with its armies, and kill a share of those who catch them. A band that lived through one keeps
// some resistance for good and cannot catch it again for a generation; a people who never met a
// sickness die of it far more (as the Americas did). Medicine, vaccines and antibiotics cut the toll.

#include <cstdint>
#include <string>

namespace earth
{
struct Disease
{
	uint16_t Id = 0;
	uint16_t Word = 0;     // What the first sick band called it.
	int Source = -1;       // The species it came from (index into the species list), -1 crowding.
	float Deadliness = 0.2f; // Share of those who catch it who die, among people who never met it.
	float Spread = 1.0f;     // How readily it passes to the next band.
	int Days = 60;           // How long it lasts in a band.
	double Year = 0.0;
	int CampId = 0;
	double Lat = 0.0, Lon = 0.0;
	long long Killed = 0;
	long long Bands = 0;     // Bands it has struck.
	long long KilledThisYear = 0;
	int Waves = 0;           // Great plagues it has caused.

	template <class A>
	void Serialize(A& Ar)
	{
		Ar.Io(Id), Ar.Io(Word), Ar.Io(Source), Ar.Io(Deadliness), Ar.Io(Spread), Ar.Io(Days), Ar.Io(Year), Ar.Io(CampId), Ar.Io(Lat), Ar.Io(Lon);
		Ar.Io(Killed), Ar.Io(Bands), Ar.Io(KilledThisYear), Ar.Io(Waves);
	}
};
} // namespace earth

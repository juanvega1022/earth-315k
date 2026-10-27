// Small deterministic random number generator, so a run with the same seed always plays out the same way.
#pragma once

#include <cstdint>

namespace earth
{

class Rng
{
public:
	explicit Rng(uint64_t Seed = 315000) : State(Seed) {}

	uint64_t Next()
	{
		uint64_t Z = (State += 0x9E3779B97F4A7C15ull);
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		return Z ^ (Z >> 31);
	}

	// Uniform in [0, 1).
	double Uniform() { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }

	// Uniform in [Lo, Hi).
	double Range(double Lo, double Hi) { return Lo + (Hi - Lo) * Uniform(); }

	bool Chance(double P) { return Uniform() < P; }

private:
	uint64_t State;
};

} // namespace earth

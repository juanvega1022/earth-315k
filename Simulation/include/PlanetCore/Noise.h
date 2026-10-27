// Seeded gradient noise, the same technique No Man's Sky style generators build on.
// The same seed always gives the same values, so detail never needs to be stored.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace earth
{

class SeededNoise
{
public:
	explicit SeededNoise(uint64_t Seed = 315000)
	{
		for (int i = 0; i < 256; ++i)
		{
			Perm[i] = static_cast<uint8_t>(i);
		}
		uint64_t State = Seed;
		for (int i = 255; i > 0; --i)
		{
			const int j = static_cast<int>(SplitMix64(State) % static_cast<uint64_t>(i + 1));
			const uint8_t Tmp = Perm[i];
			Perm[i] = Perm[j];
			Perm[j] = Tmp;
		}
		for (int i = 0; i < 256; ++i)
		{
			Perm[256 + i] = Perm[i];
		}
	}

	// Improved Perlin noise, roughly in [-1, 1].
	double Sample(double X, double Y, double Z) const
	{
		const double Fx = std::floor(X);
		const double Fy = std::floor(Y);
		const double Fz = std::floor(Z);
		const int Xi = static_cast<int>(Fx) & 255;
		const int Yi = static_cast<int>(Fy) & 255;
		const int Zi = static_cast<int>(Fz) & 255;
		X -= Fx;
		Y -= Fy;
		Z -= Fz;
		const double U = Fade(X);
		const double V = Fade(Y);
		const double W = Fade(Z);

		const int A = Perm[Xi] + Yi;
		const int AA = Perm[A] + Zi;
		const int AB = Perm[A + 1] + Zi;
		const int B = Perm[Xi + 1] + Yi;
		const int BA = Perm[B] + Zi;
		const int BB = Perm[B + 1] + Zi;

		return Lerp(W,
			Lerp(V,
				Lerp(U, Grad(Perm[AA], X, Y, Z), Grad(Perm[BA], X - 1, Y, Z)),
				Lerp(U, Grad(Perm[AB], X, Y - 1, Z), Grad(Perm[BB], X - 1, Y - 1, Z))),
			Lerp(V,
				Lerp(U, Grad(Perm[AA + 1], X, Y, Z - 1), Grad(Perm[BA + 1], X - 1, Y, Z - 1)),
				Lerp(U, Grad(Perm[AB + 1], X, Y - 1, Z - 1), Grad(Perm[BB + 1], X - 1, Y - 1, Z - 1))));
	}

	// Fractal sum of several octaves, normalised to roughly [-1, 1].
	double Fractal(double X, double Y, double Z, int Octaves, double Lacunarity = 2.0, double Gain = 0.5) const
	{
		double Sum = 0.0;
		double Amp = 1.0;
		double Norm = 0.0;
		for (int i = 0; i < Octaves; ++i)
		{
			Sum += Amp * Sample(X, Y, Z);
			Norm += Amp;
			Amp *= Gain;
			X *= Lacunarity;
			Y *= Lacunarity;
			Z *= Lacunarity;
		}
		return Norm > 0.0 ? Sum / Norm : 0.0;
	}

private:
	std::array<uint8_t, 512> Perm{};

	static uint64_t SplitMix64(uint64_t& State)
	{
		uint64_t Z = (State += 0x9E3779B97F4A7C15ull);
		Z = (Z ^ (Z >> 30)) * 0xBF58476D1CE4E5B9ull;
		Z = (Z ^ (Z >> 27)) * 0x94D049BB133111EBull;
		return Z ^ (Z >> 31);
	}

	static double Fade(double T) { return T * T * T * (T * (T * 6 - 15) + 10); }
	static double Lerp(double T, double A, double B) { return A + T * (B - A); }

	static double Grad(int Hash, double X, double Y, double Z)
	{
		const int H = Hash & 15;
		const double U = H < 8 ? X : Y;
		const double V = H < 4 ? Y : (H == 12 || H == 14 ? X : Z);
		return ((H & 1) == 0 ? U : -U) + ((H & 2) == 0 ? V : -V);
	}
};

} // namespace earth

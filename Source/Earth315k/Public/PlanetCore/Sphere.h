// Geometry helpers for building a planet out of six cube faces.
// Uses right-handed maths: +Z is the north pole, +X is latitude 0 / longitude 0,
// +Y is longitude 90 east. The Unreal layer converts to Unreal's axes.
#pragma once

#include <cmath>

namespace earth
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kEarthRadiusMeters = 6371000.0;

struct Vec3
{
	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;

	double Length() const { return std::sqrt(X * X + Y * Y + Z * Z); }
	Vec3 Normalized() const
	{
		const double L = Length();
		return L > 0.0 ? Vec3{X / L, Y / L, Z / L} : Vec3{};
	}
	Vec3 operator*(double S) const { return {X * S, Y * S, Z * S}; }
};

struct LatLon
{
	double Lat = 0.0;
	double Lon = 0.0;
};

inline LatLon DirectionToLatLon(const Vec3& Dir)
{
	const Vec3 N = Dir.Normalized();
	const double Z = N.Z < -1.0 ? -1.0 : (N.Z > 1.0 ? 1.0 : N.Z);
	return {std::asin(Z) * kRadToDeg, std::atan2(N.Y, N.X) * kRadToDeg};
}

inline Vec3 LatLonToDirection(double Lat, double Lon)
{
	const double La = Lat * kDegToRad;
	const double Lo = Lon * kDegToRad;
	return {std::cos(La) * std::cos(Lo), std::cos(La) * std::sin(Lo), std::sin(La)};
}

// Point on one of the six cube faces, U and V in [-1, 1], projected onto the unit sphere.
// The tangent warp spreads vertices more evenly than a plain normalised cube.
inline Vec3 CubeFaceToDirection(int Face, double U, double V)
{
	const double A = std::tan(U * kPi / 4.0);
	const double B = std::tan(V * kPi / 4.0);
	Vec3 P;
	switch (Face)
	{
	case 0: P = {1.0, A, B}; break;   // +X
	case 1: P = {-1.0, -A, B}; break; // -X
	case 2: P = {-A, 1.0, B}; break;  // +Y
	case 3: P = {A, -1.0, B}; break;  // -Y
	case 4: P = {-B, A, 1.0}; break;  // +Z
	default: P = {B, A, -1.0}; break; // -Z
	}
	return P.Normalized();
}

} // namespace earth

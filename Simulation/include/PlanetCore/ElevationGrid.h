// Real elevation data on a latitude/longitude grid.
// Plain C++ with no Unreal dependencies, so it can be unit tested on its own.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace earth
{

// Cell-centred grid, row 0 is the northernmost row, column 0 the westernmost.
// Heights are metres relative to present-day sea level.
struct ElevationGrid
{
	int Width = 0;
	int Height = 0;
	double North = 90.0;
	double South = -90.0;
	double West = -180.0;
	double East = 180.0;
	std::vector<int16_t> Heights;

	bool IsValid() const
	{
		return Width > 1 && Height > 1 && Heights.size() == static_cast<size_t>(Width) * Height;
	}

	// True when the grid spans every longitude, so sampling wraps east/west.
	bool IsGlobal() const { return East - West >= 359.999; }

	bool Contains(double Lat, double Lon) const
	{
		if (IsGlobal())
		{
			return Lat <= North && Lat >= South;
		}
		return Lat <= North && Lat >= South && Lon >= West && Lon <= East;
	}

	double At(int Row, int Col) const
	{
		Row = std::clamp(Row, 0, Height - 1);
		if (IsGlobal())
		{
			Col = ((Col % Width) + Width) % Width;
		}
		else
		{
			Col = std::clamp(Col, 0, Width - 1);
		}
		return Heights[static_cast<size_t>(Row) * Width + Col];
	}

	// Bilinear sample at a latitude/longitude in degrees.
	double Sample(double Lat, double Lon) const
	{
		if (IsGlobal())
		{
			while (Lon < West) Lon += 360.0;
			while (Lon >= West + 360.0) Lon -= 360.0;
		}
		const double Row = (North - Lat) / (North - South) * Height - 0.5;
		const double Col = (Lon - West) / (East - West) * Width - 0.5;
		const int R0 = static_cast<int>(std::floor(Row));
		const int C0 = static_cast<int>(std::floor(Col));
		const double Fr = Row - R0;
		const double Fc = Col - C0;
		const double Top = At(R0, C0) * (1.0 - Fc) + At(R0, C0 + 1) * Fc;
		const double Bottom = At(R0 + 1, C0) * (1.0 - Fc) + At(R0 + 1, C0 + 1) * Fc;
		return Top * (1.0 - Fr) + Bottom * Fr;
	}
};

} // namespace earth

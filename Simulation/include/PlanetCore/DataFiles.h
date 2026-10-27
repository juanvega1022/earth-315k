// Loads the elevation grids written by tools/prepare_elevation.py, without Unreal.
#pragma once

#include "ElevationGrid.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

namespace earth
{

// Reads a number from a flat JSON object such as {"width": 2160, "north": 90.0}.
inline bool JsonNumber(const std::string& Json, const std::string& Key, double& Out)
{
	const std::string Quoted = "\"" + Key + "\"";
	const size_t K = Json.find(Quoted);
	if (K == std::string::npos)
	{
		return false;
	}
	const size_t Colon = Json.find(':', K + Quoted.size());
	if (Colon == std::string::npos)
	{
		return false;
	}
	char* End = nullptr;
	Out = std::strtod(Json.c_str() + Colon + 1, &End);
	return End != Json.c_str() + Colon + 1;
}

// Loads <Folder>/<Name>.json and <Folder>/<Name>.bin. Returns false if either is missing or malformed.
inline bool LoadElevationGrid(const std::string& Folder, const std::string& Name, ElevationGrid& Out)
{
	Out = ElevationGrid();
	std::ifstream JsonFile(Folder + "/" + Name + ".json");
	if (!JsonFile)
	{
		return false;
	}
	std::stringstream Buffer;
	Buffer << JsonFile.rdbuf();
	const std::string Json = Buffer.str();

	double W = 0, H = 0;
	if (!JsonNumber(Json, "width", W) || !JsonNumber(Json, "height", H) || !JsonNumber(Json, "north", Out.North) ||
		!JsonNumber(Json, "south", Out.South) || !JsonNumber(Json, "west", Out.West) || !JsonNumber(Json, "east", Out.East))
	{
		return false;
	}
	Out.Width = static_cast<int>(W);
	Out.Height = static_cast<int>(H);

	std::ifstream Bin(Folder + "/" + Name + ".bin", std::ios::binary);
	if (!Bin)
	{
		return false;
	}
	Out.Heights.resize(static_cast<size_t>(Out.Width) * Out.Height);
	Bin.read(reinterpret_cast<char*>(Out.Heights.data()), static_cast<std::streamsize>(Out.Heights.size() * sizeof(int16_t)));
	if (!Bin || !Out.IsValid())
	{
		Out = ElevationGrid();
		return false;
	}
	return true;
}

} // namespace earth

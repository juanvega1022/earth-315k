// Runs the simulation with no graphics, as fast as the CPU allows, and prints what happened.
//
//   earth315k_sim --years 100 --seed 315000 --data Content/Data --csv out/populations.csv
#include "PlanetCore/DataFiles.h"
#include "PlanetCore/Ecosystem.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

int main(int Argc, char** Argv)
{
	double Years = 100.0;
	unsigned long long Seed = 315000;
	std::string DataDir = "Content/Data";
	std::string CsvPath;
	double SeaLevel = -30.0;

	for (int i = 1; i < Argc; ++i)
	{
		const bool HasValue = i + 1 < Argc;
		if (!std::strcmp(Argv[i], "--years") && HasValue) Years = std::atof(Argv[++i]);
		else if (!std::strcmp(Argv[i], "--seed") && HasValue) Seed = std::strtoull(Argv[++i], nullptr, 10);
		else if (!std::strcmp(Argv[i], "--data") && HasValue) DataDir = Argv[++i];
		else if (!std::strcmp(Argv[i], "--csv") && HasValue) CsvPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--sea-level") && HasValue) SeaLevel = std::atof(Argv[++i]);
		else
		{
			std::printf("Usage: earth315k_sim [--years N] [--seed S] [--data folder] [--csv file] [--sea-level metres]\n");
			return 1;
		}
	}

	earth::ElevationGrid Grid;
	if (!earth::LoadElevationGrid(DataDir, "global", Grid))
	{
		std::printf("Could not load %s/global.json and global.bin. Run tools/prepare_elevation.py first.\n", DataDir.c_str());
		return 1;
	}

	earth::EcosystemSettings Settings;
	Settings.SeaLevelMeters = SeaLevel;
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), Seed, Settings);
	const auto& Species = Eco.GetSpecies();

	std::ofstream Csv;
	if (!CsvPath.empty())
	{
		Csv.open(CsvPath);
		Csv << "year,plant_cover";
		for (const auto& S : Species) Csv << ',' << '"' << S.Name << '"';
		Csv << '\n';
	}

	const auto Start = std::chrono::steady_clock::now();
	size_t EventsShown = 0;
	const int TotalYears = static_cast<int>(Years);
	for (int Y = 0; Y <= TotalYears; ++Y)
	{
		if (Y > 0)
		{
			Eco.StepDays(365);
		}
		if (Csv)
		{
			Csv << Y << ',' << Eco.AverageLandCover();
			for (size_t s = 0; s < Species.size(); ++s) Csv << ',' << static_cast<long long>(Eco.CountOf(static_cast<int>(s)));
			Csv << '\n';
		}
		const int Every = TotalYears >= 100 ? TotalYears / 10 : 10;
		if (Y % Every == 0 || Y == TotalYears)
		{
			std::printf("Year %6d (%d years ago): %8.0f animals in %5zu groups, %2d species, plants %3.0f%%\n", Y,
				315000 - Y, Eco.TotalAnimals(), Eco.Groups().size(), Eco.SpeciesAliveCount(), Eco.AverageLandCover() * 100.0);
		}
		for (; EventsShown < Eco.Events().size(); ++EventsShown)
		{
			std::printf("  Year %.0f: %s\n", Eco.Events()[EventsShown].Year, Eco.Events()[EventsShown].Text.c_str());
		}
	}

	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	std::printf("\nFinal populations after %d years:\n", TotalYears);
	for (size_t s = 0; s < Species.size(); ++s)
	{
		std::printf("  %-26s %10.0f\n", Species[s].Name.c_str(), Eco.CountOf(static_cast<int>(s)));
	}
	std::printf("Simulated %d years in %.1f seconds.\n", TotalYears, Seconds);
	return 0;
}

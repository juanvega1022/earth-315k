// Runs the simulation with no graphics, as fast as the CPU allows, and prints what happened.
//
//   earth315k_sim --years 100 --seed 315000 --data Content/Data --csv out/populations.csv --brain out/brain.json
#include "PlanetCore/DataFiles.h"
#include "PlanetCore/Ecosystem.h"
#include "PlanetCore/People.h"

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
	std::string BrainPath;
	bool WithPeople = true;
	int FastDays = 1;

	for (int i = 1; i < Argc; ++i)
	{
		const bool HasValue = i + 1 < Argc;
		if (!std::strcmp(Argv[i], "--years") && HasValue) Years = std::atof(Argv[++i]);
		else if (!std::strcmp(Argv[i], "--seed") && HasValue) Seed = std::strtoull(Argv[++i], nullptr, 10);
		else if (!std::strcmp(Argv[i], "--data") && HasValue) DataDir = Argv[++i];
		else if (!std::strcmp(Argv[i], "--csv") && HasValue) CsvPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--sea-level") && HasValue) SeaLevel = std::atof(Argv[++i]);
		else if (!std::strcmp(Argv[i], "--brain") && HasValue) BrainPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--no-people")) WithPeople = false;
		else if (!std::strcmp(Argv[i], "--fast") && HasValue) FastDays = std::atoi(Argv[++i]);
		else
		{
			std::printf("Usage: earth315k_sim [--years N] [--seed S] [--data folder] [--csv file] [--sea-level metres] [--brain file.json] [--no-people] [--fast days]\n");
			std::printf("  --fast N  move plants and animals N days at a time (1..10): coarser, much faster\n");
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
	earth::People Folk(&Eco, Seed);
	size_t FirstsShown = 0;

	std::ofstream Csv;
	if (!CsvPath.empty())
	{
		Csv.open(CsvPath);
		Csv << "year,plant_cover,people,bands";
		for (const auto& S : Species) Csv << ',' << '"' << S.Name << '"';
		Csv << '\n';
	}

	const auto Start = std::chrono::steady_clock::now();
	size_t EventsShown = 0;
	const int TotalYears = static_cast<int>(Years);
	for (int Y = 0; Y <= TotalYears; ++Y)
	{
		for (int D = 0; Y > 0 && D < 365;)
		{
			const int Days = std::min(std::clamp(FastDays, 1, 10), 365 - D);
			Eco.Step(Days);
			for (int k = 0; WithPeople && k < Days; ++k)
			{
				Folk.StepDay();
			}
			D += Days;
		}
		if (Csv)
		{
			Csv << Y << ',' << Eco.AverageLandCover() << ',' << Folk.Alive() << ',' << Folk.BandCount();
			for (size_t s = 0; s < Species.size(); ++s) Csv << ',' << static_cast<long long>(Eco.CountOf(static_cast<int>(s)));
			Csv << '\n';
		}
		const int Every = TotalYears >= 100 ? TotalYears / 10 : 10;
		if (Y % Every == 0 || Y == TotalYears)
		{
			std::printf("Year %6d (%d years ago): %8.0f animals in %5zu groups, %2d species, plants %3.0f%%, %d people in %d bands\n", Y,
				315000 - Y, Eco.TotalAnimals(), Eco.Groups().size(), Eco.SpeciesAliveCount(), Eco.AverageLandCover() * 100.0,
				Folk.Alive(), Folk.BandCount());
		}
		for (; EventsShown < Eco.Events().size(); ++EventsShown)
		{
			std::printf("  Year %.0f: %s\n", Eco.Events()[EventsShown].Year, Eco.Events()[EventsShown].Text.c_str());
		}
		for (; FirstsShown < Folk.Firsts().size(); ++FirstsShown)
		{
			std::printf("  Year %.1f: %s\n", Folk.Firsts()[FirstsShown].Year, Folk.Firsts()[FirstsShown].Text.c_str());
		}
	}

	if (WithPeople)
	{
		const auto& Causes = Folk.DeathCauses();
		std::printf("People: %d alive in %d bands, generation %d, %lld born, %lld died (age %lld, predators %lld, hunger %lld, thirst %lld, injuries %lld), %lld animals hunted\n",
			Folk.Alive(), Folk.BandCount(), Folk.MaxGeneration(), Folk.TotalBirths(), Folk.TotalDeaths(), Causes[0], Causes[1], Causes[2],
			Causes[3], Causes[4], Folk.HuntsWon());
	}
	if (!BrainPath.empty() && Folk.Alive() > 0)
	{
		// The oldest adult has had the longest to learn.
		const earth::Npc* Oldest = &Folk.All()[0];
		for (const earth::Npc& P : Folk.All())
		{
			if (P.AgeDays > Oldest->AgeDays) Oldest = &P;
		}
		std::ofstream(BrainPath) << Folk.BrainJson(*Oldest) << '\n';
		std::printf("Wrote the brain of NPC %u (age %.0f) to %s\n", Oldest->Id, Oldest->AgeYears(), BrainPath.c_str());
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

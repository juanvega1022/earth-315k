// Runs the simulation with no graphics, as fast as the CPU allows, and prints what happened.
//
//   earth315k_sim --years 100 --seed 315000 --data Content/Data --csv out/populations.csv --brain out/brain.json
#include "PlanetCore/DataFiles.h"
#include "PlanetCore/Ecosystem.h"
#include "PlanetCore/People.h"
#include "PlanetCore/WorldSave.h"
#include "Atlas.h"
#include "RunReport.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

// Saves the brain of the oldest NPC, who has had the longest to learn, and the biggest brain.
static const earth::Npc& SaveBrain(const earth::People& Folk, const std::string& Path)
{
	const earth::Npc* Oldest = &Folk.All()[0];
	for (const earth::Npc& P : Folk.All())
	{
		if (P.AgeDays > Oldest->AgeDays) Oldest = &P;
	}
	std::ofstream(Path) << Folk.BrainJson(*Oldest) << '\n';
	// And the biggest brain alive, next to it as name_biggest.json.
	const earth::Npc* Biggest = &Folk.All()[0];
	for (const earth::Npc& P : Folk.All())
	{
		if (P.BrainSize() > Biggest->BrainSize()) Biggest = &P;
	}
	const size_t Dot = Path.find_last_of('.');
	const std::string Big = Dot == std::string::npos ? Path + "_biggest" : Path.substr(0, Dot) + "_biggest" + Path.substr(Dot);
	std::ofstream(Big) << Folk.BrainJson(*Biggest) << '\n';
	return *Oldest;
}

// Ctrl+C (or closing the window) asks the run to save and stop at the end of the current year.
static volatile std::sig_atomic_t StopAsked = 0;
static void AskToStop(int) { StopAsked = 1; }

static bool FileExists(const std::string& Path)
{
	if (std::FILE* F = std::fopen(Path.c_str(), "rb"))
	{
		std::fclose(F);
		return true;
	}
	return false;
}

int main(int Argc, char** Argv)
{
	double Years = 100.0;
	unsigned long long Seed = 315000;
	std::string DataDir = "Content/Data";
	std::string CsvPath;
	double SeaLevel = -30.0;
	std::string BrainPath;
	std::string LanguagesPath;
	std::string StoriesPath;
	std::string InventionsPath;
	std::string ReportPath;
	std::string BeliefsPath;
	std::string AtlasPath;
	std::string SavePath;
	std::string ResumePath;
	int SaveEvery = 1000;
	bool WithPeople = true;
	int FastDays = 1;
	earth::PeopleSettings PeopleCfg;
	earth::EcosystemSettings Settings;

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
		else if (!std::strcmp(Argv[i], "--max-people") && HasValue) PeopleCfg.MaxPeople = std::atoi(Argv[++i]);
		else if (!std::strcmp(Argv[i], "--density") && HasValue) PeopleCfg.PeoplePerKm2 = std::atof(Argv[++i]);
		else if (!std::strcmp(Argv[i], "--start") && HasValue) Settings.StartYearsAgo = std::atof(Argv[++i]);
		else if (!std::strcmp(Argv[i], "--no-ice-ages")) Settings.IceAges = false;
		else if (!std::strcmp(Argv[i], "--languages") && HasValue) LanguagesPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--stories") && HasValue) StoriesPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--inventions") && HasValue) InventionsPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--report") && HasValue) ReportPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--beliefs") && HasValue) BeliefsPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--atlas") && HasValue) AtlasPath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--save") && HasValue) SavePath = Argv[++i];
		else if (!std::strcmp(Argv[i], "--save-every") && HasValue) SaveEvery = std::max(1, std::atoi(Argv[++i]));
		else if (!std::strcmp(Argv[i], "--resume") && HasValue) ResumePath = Argv[++i];
		else
		{
			std::printf("Usage: earth315k_sim [--years N] [--seed S] [--data folder] [--csv file] [--sea-level metres] [--brain file.json] [--no-people] [--fast days] [--max-people N] [--density D] [--start years-ago] [--no-ice-ages] [--languages file.txt] [--stories file.txt] [--inventions file.txt] [--report file.html] [--beliefs file.txt] [--atlas file.html] [--save file.bin] [--save-every years] [--resume file.bin]\n");
			std::printf("  --fast N        move everything N days at a time (1..30): coarser, much faster\n");
			std::printf("  --max-people N  never more than N people (default: no limit but the land)\n");
			std::printf("  --density D     people a square km of the greenest land can feed (default %g)\n", PeopleCfg.PeoplePerKm2);
			std::printf("  --start N       begin N years ago (default 315000); sea level and ice follow Earth's history\n");
			std::printf("  --save F        save the whole world to F every --save-every years (default 1000), and when stopped\n");
			std::printf("  --resume F      carry on from a save instead of starting over; --years is then the year to run to\n");
			std::printf("  --report F      write an HTML report with charts and a scorecard against real history (needs --csv)\n");
			std::printf("  --atlas F       keep a map of languages, beliefs and people over time in F (an HTML page) and F.js\n");
			std::printf("  To stop and save: press Ctrl+C, or create a file named F.stop next to the save\n");
			return 1;
		}
	}

	earth::ElevationGrid Grid;
	if (!earth::LoadElevationGrid(DataDir, "global", Grid))
	{
		std::printf("Could not load %s/global.json and global.bin. Run tools/prepare_elevation.py first.\n", DataDir.c_str());
		return 1;
	}

	// Real climate, if tools/prepare_climate.py has been run; otherwise a rough one from latitude.
	earth::ElevationGrid Temperature, Rain;
	if (earth::LoadElevationGrid(DataDir, "temperature", Temperature) && earth::LoadElevationGrid(DataDir, "rain", Rain))
	{
		Settings.Temperature = &Temperature;
		Settings.Rain = &Rain;
		std::printf("Using the real climate in %s (temperature and rain)\n", DataDir.c_str());
	}
	else
	{
		std::printf("No climate data in %s: using a rough climate from latitude. Run tools/prepare_climate.py for the real one.\n", DataDir.c_str());
	}
	Settings.SeaLevelMeters = SeaLevel;
	earth::Ecosystem Eco(&Grid, earth::DefaultSpecies(), Seed, Settings);
	const auto& Species = Eco.GetSpecies();
	earth::People Folk(&Eco, Seed, PeopleCfg);
	size_t FirstsShown = 0;
	size_t EventsShown = 0;
	int FirstYear = 0;
	if (!ResumePath.empty())
	{
		if (!earth::LoadWorld(ResumePath, Eco, Folk))
		{
			std::printf("Could not read the save %s. It may be missing, damaged or from a newer version.\n", ResumePath.c_str());
			return 1;
		}
		FirstYear = static_cast<int>(std::lround(Eco.Years()));
		EventsShown = Eco.Events().size();
		FirstsShown = Folk.Firsts().size() - 1;
		std::printf("Carrying on from %s at year %d (%d years ago), with %d people.\n", ResumePath.c_str(), FirstYear, static_cast<int>(Eco.YearsAgo()), Folk.Alive());
	}
	if (WithPeople)
	{
		std::printf("The land can feed about %.0f people.\n", Folk.LandCapacity());
	}
	std::ofstream Csv;
	// Every session of a run is logged next to its outputs: which build, seed and settings made it.
	const std::string FirstOutput = !SavePath.empty() ? SavePath : !CsvPath.empty() ? CsvPath : ReportPath;
	const std::string RunLog = FirstOutput.empty() ? "" : FolderOf(FirstOutput) + "runs.txt";
	std::string Args;
	for (int i = 1; i < Argc; ++i) Args += std::string(i > 1 ? " " : "") + Argv[i];
	const std::string Label = std::string("Build ") + kSimVersion + ", seed " + std::to_string(Seed);
	AppendRunLog(RunLog, (ResumePath.empty() ? "Started    " : "Resumed    ") + Label + " (save format " + std::to_string(earth::kSaveVersion) +
		") at year " + std::to_string(FirstYear) + ". Arguments: " + Args);
	// The atlas starts afresh with a new run and grows with each session of a resumed one.
	const std::string AtlasJs = AtlasPath + ".js";
	size_t AtlasFirsts = ResumePath.empty() ? 0 : Folk.Firsts().size();
	if (!AtlasPath.empty() && ResumePath.empty()) std::remove(AtlasJs.c_str());
	auto Report = [&](int Y)
	{
		if (!ReportPath.empty() && !CsvPath.empty())
		{
			Csv.flush();
			WriteReport(ReportPath, CsvPath, Eco.YearsAgo() + Eco.Years(), Y, Folk.Firsts(), Label);
		}
	};
	if (!SavePath.empty())
	{
		std::signal(SIGINT, AskToStop);
		std::signal(SIGTERM, AskToStop);
#ifdef SIGBREAK
		std::signal(SIGBREAK, AskToStop); // Closing the console window on Windows.
#endif
	}
	const std::string StopFile = SavePath + ".stop";
	auto Save = [&](int Y)
	{
		if (SavePath.empty()) return;
		if (earth::SaveWorld(SavePath, Eco, Folk))
		{
			std::printf("  Saved the world at year %d to %s\n", Y, SavePath.c_str());
		}
		else
		{
			std::printf("  Could not save the world to %s; the last good save is kept\n", SavePath.c_str());
		}
	};

	const bool Appending = FirstYear > 0 && FileExists(CsvPath);
	if (!CsvPath.empty() && Appending)
	{
		Csv.open(CsvPath, std::ios::app);
	}
	else if (!CsvPath.empty())
	{
		Csv.open(CsvPath);
		Csv << "year,plant_cover,people,bands,skills_known,average_brain,biggest_brain,sea_level_m,languages";
		for (const auto& S : Species) Csv << ',' << '"' << S.Name << '"';
		Csv << ",inventions,beliefs,religions,kindness,curiosity\n";
	}

	const auto Start = std::chrono::steady_clock::now();
	const int TotalYears = static_cast<int>(Years);
	for (int Y = FirstYear > 0 ? FirstYear + 1 : 0; Y <= TotalYears; ++Y)
	{
		for (int D = 0; Y > 0 && D < 365;)
		{
			const int Days = std::min(std::clamp(FastDays, 1, 30), 365 - D);
			Eco.Step(Days);
			if (WithPeople)
			{
				Folk.Step(Days);
			}
			D += Days;
		}
		if (Csv)
		{
			Csv << Y << ',' << Eco.AverageLandCover() << ',' << Folk.Alive() << ',' << Folk.BandCount() << ',' << Folk.SkillsKnown() << ','
				<< Folk.AverageBrain() << ',' << Folk.BiggestBrain() << ',' << Eco.SeaLevelChange() << ',' << Folk.Languages();
			for (size_t s = 0; s < Species.size(); ++s) Csv << ',' << static_cast<long long>(Eco.CountOf(static_cast<int>(s)));
			Csv << ',' << Folk.InventionsKnown() << ',' << Folk.BeliefsHeld() << ',' << Folk.Religions() << ',' << Folk.AverageKindness() << ','
				<< Folk.AverageCuriosity() << '\n';
			Csv.flush();
		}
		// At least every 1,000 years, so long runs can be followed while they go.
		const int Every = std::min(1000, TotalYears >= 100 ? TotalYears / 10 : 10);
		if (Y % Every == 0 || Y == TotalYears)
		{
			std::printf("Year %6d (%d years ago): %8.0f animals in %5zu groups, %2d species, plants %3.0f%%, %d people in %d bands\n", Y,
				static_cast<int>(Eco.YearsAgo()), Eco.TotalAnimals(), Eco.Groups().size(), Eco.SpeciesAliveCount(), Eco.AverageLandCover() * 100.0,
				Folk.Alive(), Folk.BandCount());
			std::printf("            Sea level %+.0f m from today, ice age strength %.0f%%\n", Eco.SeaLevelChange(), Eco.IceCover() * 100.0);
			if (WithPeople)
			{
				std::printf("            %d skills and %d inventions known, brains of %.1f hidden neurons on average (biggest %d), %d languages, %d beliefs (%d religions)\n",
					Folk.SkillsKnown(), Folk.InventionsKnown(), Folk.AverageBrain(), Folk.BiggestBrain(), Folk.Languages(), Folk.BeliefsHeld(), Folk.Religions());
			}
			if (!LanguagesPath.empty() && Folk.Alive() > 0)
			{
				std::ofstream(LanguagesPath) << "Year " << Y << ", " << static_cast<int>(Eco.YearsAgo()) << " years ago\n\n" << Folk.LanguagesText();
			}
			Report(Y);
			if (!AtlasPath.empty() && WithPeople)
			{
				AppendAtlasFrame(AtlasJs, Eco, Folk, Y, AtlasFirsts);
				const size_t Slash = AtlasJs.find_last_of("/\\");
				WriteAtlasPage(AtlasPath, Slash == std::string::npos ? AtlasJs : AtlasJs.substr(Slash + 1));
			}
			if (!BeliefsPath.empty())
			{
				std::ofstream(BeliefsPath) << "Year " << Y << ", " << static_cast<int>(Eco.YearsAgo()) << " years ago: what the people believe\n\n" << Folk.BeliefsText();
			}
			if (!InventionsPath.empty())
			{
				std::ofstream(InventionsPath) << "Year " << Y << ", " << static_cast<int>(Eco.YearsAgo()) << " years ago: things made from materials, oldest first\n\n" << Folk.InventionsText();
			}
			if (!StoriesPath.empty())
			{
				std::ofstream(StoriesPath) << "Year " << Y << ", " << static_cast<int>(Eco.YearsAgo()) << " years ago: the people who made a difference\n\n" << Folk.StoriesText();
			}
			if (!BrainPath.empty() && Folk.Alive() > 0)
			{
				SaveBrain(Folk, BrainPath);
			}
		}
		for (; EventsShown < Eco.Events().size(); ++EventsShown)
		{
			std::printf("  Year %.0f: %s\n", Eco.Events()[EventsShown].Year, Eco.Events()[EventsShown].Text.c_str());
		}
		for (; FirstsShown < Folk.Firsts().size(); ++FirstsShown)
		{
			std::printf("  Year %.1f: %s\n", Folk.Firsts()[FirstsShown].Year, Folk.Firsts()[FirstsShown].Text.c_str());
		}
		const bool Stopping = StopAsked || (!SavePath.empty() && FileExists(StopFile));
		if (Y > FirstYear && (Y % SaveEvery == 0 || Y == TotalYears || Stopping))
		{
			Save(Y);
		}
		std::fflush(stdout);
		if (Stopping)
		{
			std::remove(StopFile.c_str());
			std::printf("Stopped at year %d. Carry on later with --resume %s\n", Y, SavePath.c_str());
			Report(Y);
			AppendRunLog(RunLog, "Stopped    at year " + std::to_string(Y) + ", saved to " + SavePath);
			return 0;
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
		const earth::Npc& Oldest = SaveBrain(Folk, BrainPath);
		std::printf("Wrote the brain of NPC %u (age %.0f) to %s\n", Oldest.Id, Oldest.AgeYears(), BrainPath.c_str());
	}

	const double Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Start).count();
	std::printf("\nFinal populations after %d years:\n", TotalYears);
	for (size_t s = 0; s < Species.size(); ++s)
	{
		std::printf("  %-26s %10.0f\n", Species[s].Name.c_str(), Eco.CountOf(static_cast<int>(s)));
	}
	std::printf("Simulated %d years in %.1f seconds.\n", TotalYears, Seconds);
	AppendRunLog(RunLog, "Finished   at year " + std::to_string(TotalYears) + " after " + std::to_string(static_cast<long long>(Seconds)) + " seconds");
	return 0;
}

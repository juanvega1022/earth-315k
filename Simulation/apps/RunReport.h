// What a headless run leaves behind besides its data: a log of every session (which build, seed and
// settings, when it started and stopped), and an HTML report with charts and a scorecard against
// real history, rebuilt from the CSV so it covers resumed runs too.
#pragma once

#include "PlanetCore/Ecosystem.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Changes whenever the simulation changes in a way that affects results.
inline const char* kSimVersion = "2026.11.03 milestone 3 (pace tuned)";

inline std::string NowText()
{
	const std::time_t T = std::time(nullptr);
	char Buf[32];
	std::strftime(Buf, sizeof(Buf), "%Y-%m-%d %H:%M:%S", std::localtime(&T));
	return Buf;
}

// The folder a file is in, with its separator, or "" for the current folder.
inline std::string FolderOf(const std::string& Path)
{
	const size_t At = Path.find_last_of("/\\");
	return At == std::string::npos ? "" : Path.substr(0, At + 1);
}

inline void AppendRunLog(const std::string& Path, const std::string& Line)
{
	if (Path.empty()) return;
	std::ofstream(Path, std::ios::app) << NowText() << "  " << Line << '\n';
}

// A milestone of real human history, as years before today.
struct Milestone
{
	const char* What;
	const char* EventStart; // The start of the event text that marks it in the sim.
	double RealYearsAgo;
	const char* RealRange;
};

inline const std::vector<Milestone>& RealMilestones()
{
	static const std::vector<Milestone> List = {
		{"Leave Africa for Asia", "First NPC sets foot in Asia", 65000, "about 70,000 to 50,000 years ago"},
		{"Reach Europe", "First NPC sets foot in Europe", 45000, "about 47,000 to 43,000 years ago"},
		{"Reach Australia", "First NPC sets foot in Australia", 55000, "about 65,000 to 50,000 years ago"},
		{"Reach the Americas", "First NPC sets foot in the Americas", 16000, "about 20,000 to 15,000 years ago"},
		{"Bows and arrows", "First discovery of bows and arrows", 64000, "about 70,000 to 60,000 years ago"},
		{"Pottery", "First discovery of pottery", 20000, "about 20,000 years ago"},
		{"Farming", "First discovery of farming", 11000, "about 12,000 to 10,000 years ago"},
		{"Writing", "First discovery of writing", 5200, "about 5,200 years ago"},
	};
	return List;
}

inline std::string Thousands(double V)
{
	char Buf[32];
	std::snprintf(Buf, sizeof(Buf), "%.0f", V);
	std::string S = Buf;
	for (int i = static_cast<int>(S.size()) - 3; i > (S[0] == '-' ? 1 : 0); i -= 3) S.insert(static_cast<size_t>(i), ",");
	return S;
}

// One line chart of a few CSV columns against years ago.
inline std::string Chart(const std::string& Title, const std::vector<double>& YearsAgo, const std::vector<std::vector<double>>& Series,
	const std::vector<std::string>& Names)
{
	const double W = 640, H = 200, L = 56, R = 12, T = 12, B = 28;
	double Lo = 1e300, Hi = -1e300;
	for (const auto& S : Series)
	{
		for (double V : S) Lo = std::min(Lo, V), Hi = std::max(Hi, V);
	}
	if (YearsAgo.size() < 2 || Lo > Hi) return "";
	if (Lo > 0 && Lo < Hi * 0.5) Lo = 0;
	if (Hi - Lo < 1e-9) Hi = Lo + 1;
	const double X0 = YearsAgo.front(), X1 = YearsAgo.back();
	auto X = [&](double V) { return L + (X0 - V) / std::max(1.0, X0 - X1) * (W - L - R); };
	auto Y = [&](double V) { return T + (Hi - V) / (Hi - Lo) * (H - T - B); };
	std::ostringstream O;
	O << "<figure><figcaption>" << Title << "</figcaption><svg viewBox=\"0 0 " << W << ' ' << H << "\" role=\"img\" aria-label=\"" << Title << "\">";
	O << "<line class=\"axis\" x1=\"" << L << "\" y1=\"" << H - B << "\" x2=\"" << W - R << "\" y2=\"" << H - B << "\"/>";
	// Small ranges, like plant cover from 0 to 1, need decimals.
	auto Tick = [&](double V)
	{
		if (Hi - Lo >= 10.0) return Thousands(V);
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), Hi - Lo >= 1.0 ? "%.1f" : "%.2f", V);
		return std::string(Buf);
	};
	O << "<text class=\"tick\" x=\"" << L - 6 << "\" y=\"" << Y(Hi) + 4 << "\" text-anchor=\"end\">" << Tick(Hi) << "</text>";
	O << "<text class=\"tick\" x=\"" << L - 6 << "\" y=\"" << Y(Lo) + 4 << "\" text-anchor=\"end\">" << Tick(Lo) << "</text>";
	O << "<text class=\"tick\" x=\"" << L << "\" y=\"" << H - 8 << "\">" << Thousands(X0) << " years ago</text>";
	O << "<text class=\"tick\" x=\"" << W - R << "\" y=\"" << H - 8 << "\" text-anchor=\"end\">" << Thousands(X1) << " years ago</text>";
	for (size_t s = 0; s < Series.size(); ++s)
	{
		O << "<polyline class=\"s" << s << "\" points=\"";
		for (size_t i = 0; i < Series[s].size(); ++i) O << static_cast<int>(X(YearsAgo[i])) << ',' << static_cast<int>(Y(Series[s][i])) << ' ';
		O << "\"/>";
	}
	O << "</svg>";
	if (Series.size() > 1)
	{
		O << "<div class=\"legend\">";
		for (size_t s = 0; s < Names.size(); ++s) O << "<span><i class=\"k" << s << "\"></i>" << Names[s] << "</span>";
		O << "</div>";
	}
	O << "</figure>";
	return O.str();
}

// Rebuilds the report from the CSV the run writes. Returns false if the CSV can't be read.
inline bool WriteReport(const std::string& HtmlPath, const std::string& CsvPath, double StartYearsAgo, int YearNow,
	const std::vector<earth::EcoEvent>& Firsts, const std::string& Label)
{
	std::ifstream In(CsvPath);
	std::string Line;
	if (!In || !std::getline(In, Line)) return false;
	std::vector<std::string> Head;
	{
		std::stringstream SS(Line);
		std::string Cell;
		while (std::getline(SS, Cell, ',')) Head.push_back(Cell);
	}
	std::map<std::string, std::vector<double>> Cols;
	std::vector<std::vector<double>> Rows;
	while (std::getline(In, Line))
	{
		std::vector<double> Row;
		std::stringstream SS(Line);
		std::string Cell;
		while (std::getline(SS, Cell, ',')) Row.push_back(std::atof(Cell.c_str()));
		if (!Row.empty()) Rows.push_back(std::move(Row));
	}
	// At most about 600 points per line, so the page stays light for a 315,000-year run.
	const size_t Step = std::max<size_t>(1, Rows.size() / 600);
	std::vector<double> YearsAgo;
	for (size_t r = 0; r < Rows.size(); r += Step)
	{
		YearsAgo.push_back(StartYearsAgo - Rows[r][0]);
		for (size_t c = 0; c < Head.size(); ++c) Cols[Head[c]].push_back(c < Rows[r].size() ? Rows[r][c] : 0.0);
	}
	auto Col = [&](const char* Name) { return Cols.count(Name) ? Cols[Name] : std::vector<double>(YearsAgo.size(), 0.0); };

	std::ostringstream O;
	O << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
	  << "<title>Earth 315k run report</title><style>"
	  << ":root{--bg:#fbfaf7;--fg:#1d1c1a;--muted:#6b6862;--line:#d9d5cc;--a:#2f6fbf;--b:#c0602a;--c:#3e8e5a}"
	  << "@media (prefers-color-scheme:dark){:root{--bg:#171614;--fg:#ecebe7;--muted:#a19d95;--line:#3a3833;--a:#7aaef0;--b:#f0a070;--c:#7cc896}}"
	  << "body{background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif;margin:0;padding:24px 16px;max-width:760px;margin:auto}"
	  << "h1{font-size:22px;margin:0 0 4px}p.sub{color:var(--muted);margin:0 0 24px}h2{font-size:17px;margin:28px 0 8px}"
	  << "figure{margin:0 0 20px}figcaption{font-weight:600;margin-bottom:4px}svg{width:100%;height:auto;display:block}"
	  << ".axis{stroke:var(--line)}.tick{fill:var(--muted);font-size:11px}polyline{fill:none;stroke-width:2}"
	  << ".s0{stroke:var(--a)}.s1{stroke:var(--b)}.s2{stroke:var(--c)}.legend{color:var(--muted);font-size:13px;display:flex;gap:16px}"
	  << ".legend i{display:inline-block;width:12px;height:3px;margin-right:6px;vertical-align:middle}.k0{background:var(--a)}.k1{background:var(--b)}.k2{background:var(--c)}"
	  << "table{border-collapse:collapse;width:100%;font-size:14px}td,th{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line);vertical-align:top}"
	  << "th{color:var(--muted);font-weight:500}.early{color:var(--b)}.close{color:var(--c)}"
	  << "</style></head><body>";
	O << "<h1>Earth 315k run report</h1><p class=\"sub\">" << Label << ". Year " << Thousands(YearNow) << ", which is "
	  << Thousands(StartYearsAgo - YearNow) << " years ago. Updated " << NowText() << ".</p>";

	// How the run's firsts compare with real history.
	O << "<h2>Scorecard against real history</h2><table><tr><th>Milestone</th><th>Real</th><th>This run</th></tr>";
	for (const Milestone& M : RealMilestones())
	{
		const earth::EcoEvent* Hit = nullptr;
		for (const auto& E : Firsts)
		{
			if (E.Text.rfind(M.EventStart, 0) == 0)
			{
				Hit = &E;
				break;
			}
		}
		O << "<tr><td>" << M.What << "</td><td>" << M.RealRange << "</td><td>";
		if (!Hit)
		{
			O << "not yet";
		}
		else
		{
			const double Ago = StartYearsAgo - Hit->Year;
			const double Off = Ago - M.RealYearsAgo;
			const bool Close = std::abs(Off) <= std::max(5000.0, M.RealYearsAgo * 0.25);
			O << Thousands(Ago) << " years ago <span class=\"" << (Close ? "close" : "early") << "\">("
			  << (Close ? "close" : Thousands(std::abs(Off)) + (Off > 0 ? " years too early" : " years too late")) << ")</span>";
		}
		O << "</td></tr>";
	}
	O << "</table>";

	O << "<h2>How the run went</h2>";
	O << Chart("People", YearsAgo, {Col("people")}, {"people"});
	O << Chart("Bands and languages", YearsAgo, {Col("bands"), Col("languages")}, {"bands", "languages"});
	O << Chart("Skills and inventions known", YearsAgo, {Col("skills_known"), Col("inventions")}, {"skills", "inventions"});
	O << Chart("Beliefs and religions (beliefs held by 10+ bands)", YearsAgo, {Col("beliefs"), Col("religions")}, {"beliefs", "religions"});
	O << Chart("Personality (average): kindness when sharing and curiosity", YearsAgo, {Col("kindness"), Col("curiosity")}, {"kindness", "curiosity"});
	O << Chart("Brain size (hidden neurons)", YearsAgo, {Col("average_brain"), Col("biggest_brain")}, {"average", "biggest"});
	O << Chart("Sea level compared with today (m)", YearsAgo, {Col("sea_level_m")}, {"sea level"});
	O << Chart("Plant cover (share of best)", YearsAgo, {Col("plant_cover")}, {"plants"});
	O << "</body></html>\n";
	std::ofstream Out(HtmlPath + ".tmp");
	Out << O.str();
	Out.close();
	std::remove(HtmlPath.c_str());
	return std::rename((HtmlPath + ".tmp").c_str(), HtmlPath.c_str()) == 0;
}

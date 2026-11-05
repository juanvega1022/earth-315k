// What a headless run leaves behind besides its data: a log of every session (which build, seed and
// settings, when it started and stopped), and an HTML report with charts and a scorecard against
// real history, rebuilt from the CSV so it covers resumed runs too.
#pragma once

#include "PlanetCore/Ecosystem.h"
#include "PlanetCore/Hominins.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// Changes whenever the simulation changes in a way that affects results.
inline const char* kSimVersion = "2026.11.04 history pace";

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
		// The first footsteps, not the lasting spread: Misliya Cave (Israel) holds a jaw ~185,000 years
		// old, though people only spread across Asia for good 70,000 to 50,000 years ago.
		{"Leave Africa for Asia", "First NPC sets foot in Asia", 185000, "about 194,000 to 177,000 years ago (Misliya Cave, Israel); for good 70,000 to 50,000"},
		{"Reach Europe", "First NPC sets foot in Europe", 45000, "about 47,000 to 43,000 years ago (a skull in Greece may be 210,000)"},
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
	const std::vector<earth::EcoEvent>& Firsts, const std::string& Label, const std::vector<earth::EcoEvent>& WorldEvents = {})
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
	  << "<meta http-equiv=\"refresh\" content=\"120\">"
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
	  << ".now{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}.now div{border:1px solid var(--line);border-radius:8px;padding:8px 10px}"
	  << ".now b{display:block;font-size:20px;font-weight:600}.now span{color:var(--muted);font-size:13px}td.y{white-space:nowrap;color:var(--muted)}"
	  << "</style></head><body>";
	O << "<h1>Earth 315k run report</h1><p class=\"sub\">" << Label << ". Year " << Thousands(YearNow) << ", which is "
	  << Thousands(StartYearsAgo - YearNow) << " years ago. Updated " << NowText() << "; the page reloads itself every 2 minutes.</p>";

	// Right now, from the run's latest line.
	if (!Rows.empty())
	{
		const std::vector<double>& Last = Rows.back();
		auto At = [&](const std::string& Name)
		{
			for (size_t c = 0; c < Head.size() && c < Last.size(); ++c)
				if (Head[c] == Name || Head[c] == "\"" + Name + "\"") return Last[c];
			return 0.0;
		};
		double Others = 0.0;
		for (size_t k = 1; k < earth::OtherKinds().size(); ++k) Others += At(earth::OtherKinds()[k].Plural);
		int Species = 0;
		for (const earth::Species& S : earth::DefaultSpecies())
			if (S.TameSkill < 0 && At(S.Name) >= 1.0) ++Species;
		const double Done = std::clamp(YearNow / std::max(1.0, StartYearsAgo), 0.0, 9.99);
		auto Box = [&](const std::string& Value, const std::string& What) { O << "<div><b>" << Value << "</b><span>" << What << "</span></div>"; };
		O << "<h2>Right now</h2><div class=\"now\">";
		Box(Thousands(YearNow), "year (" + std::to_string(static_cast<int>(Done * 100.0)) + "% of the way to today)");
		Box(Thousands(At("people") - Others), "of our people");
		for (size_t k = 1; k < earth::OtherKinds().size(); ++k)
			if (At(earth::OtherKinds()[k].Plural) >= 1.0) Box(Thousands(At(earth::OtherKinds()[k].Plural)), earth::OtherKinds()[k].Plural);
		Box(Thousands(At("bands")), "bands");
		Box(Thousands(At("languages")), "languages");
		Box(Thousands(At("skills_known")), "skills known");
		Box(Thousands(At("inventions")), "inventions known");
		Box(Thousands(At("beliefs")), "beliefs");
		Box(Thousands(At("religions")), "religions");
		Box(Thousands(At("average_brain")), "neurons in an average brain");
		Box(std::to_string(Species), "wild animal species alive");
		Box(Thousands(At("sea_level_m")) + " m", "sea level against today");
		O << "</div>";
	}

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
	{
		// Our people, and the two biggest other kinds.
		std::vector<double> Ours = Col("people");
		for (size_t k = 1; k < earth::OtherKinds().size(); ++k)
		{
			const std::vector<double> K = Col((std::string("\"") + earth::OtherKinds()[k].Plural + "\"").c_str());
			for (size_t i = 0; i < Ours.size() && i < K.size(); ++i) Ours[i] -= K[i];
		}
		O << Chart("People", YearsAgo, {Ours, Col("\"Neanderthals\""), Col("\"Denisovans\"")}, {"our people", "Neanderthals", "Denisovans"});
	}
	O << Chart("Bands and languages", YearsAgo, {Col("bands"), Col("languages")}, {"bands", "languages"});
	O << Chart("Skills and inventions known", YearsAgo, {Col("skills_known"), Col("inventions")}, {"skills", "inventions"});
	O << Chart("Beliefs and religions (beliefs held by 50+ bands)", YearsAgo, {Col("beliefs"), Col("religions")}, {"beliefs", "religions"});
	O << Chart("Personality (average): kindness when sharing and curiosity", YearsAgo, {Col("kindness"), Col("curiosity")}, {"kindness", "curiosity"});
	O << Chart("Brain size (hidden neurons)", YearsAgo, {Col("average_brain"), Col("biggest_brain")}, {"average", "biggest"});
	O << Chart("Sea level compared with today (m)", YearsAgo, {Col("sea_level_m")}, {"sea level"});
	O << Chart("Plant cover (share of best)", YearsAgo, {Col("plant_cover")}, {"plants"});
	// The key moments, newest first: the world's events and the people's firsts, leaving out the
	// many small ones (each new invention and each first hunt of a kind).
	{
		std::vector<const earth::EcoEvent*> Key;
		for (const auto& E : WorldEvents) Key.push_back(&E);
		for (const auto& E : Firsts)
			if (E.Text.rfind("First invention of", 0) != 0 && E.Text.find(" hunted (party of") == std::string::npos &&
				E.Text.rfind("A hidden group of", 0) != 0)
				Key.push_back(&E);
		std::stable_sort(Key.begin(), Key.end(), [](const earth::EcoEvent* A, const earth::EcoEvent* B) { return A->Year > B->Year; });
		O << "<h2>What happened, newest first</h2><table>";
		for (size_t i = 0; i < Key.size() && i < 400; ++i)
		{
			O << "<tr><td class=\"y\">" << Thousands(StartYearsAgo - Key[i]->Year) << " years ago</td><td>";
			for (char Ch : Key[i]->Text) O << (Ch == '<' ? std::string("&lt;") : Ch == '&' ? std::string("&amp;") : std::string(1, Ch));
			O << "</td></tr>";
		}
		O << "</table>";
	}
	O << "</body></html>\n";
	std::ofstream Out(HtmlPath + ".tmp");
	Out << O.str();
	Out.close();
	std::remove(HtmlPath.c_str());
	return std::rename((HtmlPath + ".tmp").c_str(), HtmlPath.c_str()) == 0;
}

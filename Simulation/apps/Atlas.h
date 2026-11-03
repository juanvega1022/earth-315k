// A history atlas: at every progress line the run adds a snapshot of the world (land at that
// sea level, where people live, which language and belief each place holds, and what happened
// since the last snapshot) to atlas.js, and writes atlas.html next to it, which shows the
// snapshots on a map with a time slider. Open atlas.html in any browser.
#pragma once

#include "PlanetCore/Ecosystem.h"
#include "PlanetCore/People.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

inline std::string JsonText(const std::string& S)
{
	std::string Out = "\"";
	for (char C : S)
	{
		if (C == '"' || C == '\\') Out += '\\';
		if (static_cast<unsigned char>(C) < 0x20) continue;
		Out += C;
	}
	return Out + "\"";
}

// Appends one snapshot. FirstsShown is how many of the people's firsts earlier snapshots covered.
inline void AppendAtlasFrame(const std::string& JsPath, const earth::Ecosystem& Eco, const earth::People& Folk, int Year, size_t& FirstsDone)
{
	constexpr int Deg = 2; // Map squares of 2 degrees.
	constexpr int Rows = 180 / Deg, Cols = 360 / Deg;
	std::ostringstream O;
	O << "ATLAS.push({y:" << Year << ",ago:" << static_cast<long long>(Eco.YearsAgo()) << ",sea:" << static_cast<int>(Eco.SeaLevelChange()) << ",land:\"";
	// Land as run lengths along rows, starting with sea: "12,5,30,..." alternating sea and land.
	bool Land = false;
	int Run = 0;
	bool First = true;
	for (int r = 0; r < Rows; ++r)
	{
		for (int c = 0; c < Cols; ++c)
		{
			const bool L = Eco.IsLand(90.0 - (r + 0.5) * Deg, -180.0 + (c + 0.5) * Deg);
			if (L != Land)
			{
				O << (First ? "" : ",") << Run;
				First = false;
				Run = 0;
				Land = L;
			}
			++Run;
		}
	}
	O << (First ? "" : ",") << Run << "\"";

	// Each language is named by the camp that first speaks it; each place by its biggest band.
	const std::vector<const earth::Camp*> Langs = Folk.LanguageCamps();
	struct Square
	{
		int People = 0, Biggest = 0, Lang = 0;
		uint32_t Belief = 0;
		unsigned Crops = 0, Herds = 0; // Bits of AllCrops() and of the kept kinds, in species order.
	};
	std::map<int, Square> Squares;
	std::map<int, int> LangSpeakers;
	std::map<uint32_t, int> BeliefBands;
	for (const earth::Camp& C : Folk.AllCamps())
	{
		if (C.Members.empty()) continue;
		int Lang = 0;
		for (const earth::Camp* L : Langs)
		{
			if (earth::People::Understand(C, *L))
			{
				Lang = L->Id;
				break;
			}
		}
		const int Size = static_cast<int>(C.Members.size());
		LangSpeakers[Lang] += Size;
		const uint32_t Belief = C.Faith.empty() ? 0 : C.Faith.front();
		if (Belief) BeliefBands[Belief] += 1;
		const int R = std::clamp(static_cast<int>((90.0 - C.Lat) / Deg), 0, Rows - 1);
		const int K = std::clamp(static_cast<int>((C.Lon + 180.0) / Deg), 0, Cols - 1);
		Square& S = Squares[R * Cols + K];
		S.People += Size;
		S.Crops |= C.Crops;
		if (Size > S.Biggest)
		{
			S.Biggest = Size;
			S.Lang = Lang;
			S.Belief = Belief;
		}
	}
	std::vector<std::string> KeptNames;
	std::map<int, unsigned> KeptBit;
	for (size_t k = 0; k < Eco.GetSpecies().size(); ++k)
	{
		if (Eco.GetSpecies()[k].TameSkill < 0) continue;
		KeptBit[static_cast<int>(k)] = 1u << KeptNames.size();
		KeptNames.push_back(Eco.GetSpecies()[k].Name);
	}
	for (const earth::AnimalGroup& G : Eco.Groups())
	{
		if (G.Herder == 0 || G.Count < 1.0) continue;
		const int R = std::clamp(static_cast<int>((90.0 - G.Lat) / Deg), 0, Rows - 1);
		const int K = std::clamp(static_cast<int>((G.Lon + 180.0) / Deg), 0, Cols - 1);
		auto It = Squares.find(R * Cols + K);
		if (It != Squares.end()) It->second.Herds |= KeptBit[G.SpeciesIndex];
	}
	O << ",sq:[";
	First = true;
	for (const auto& [Key, S] : Squares)
	{
		O << (First ? "" : ",") << '[' << Key << ',' << S.People << ',' << S.Lang << ',' << S.Belief << ',' << S.Crops << ',' << S.Herds << ']';
		First = false;
	}
	O << "],crops:[";
	for (size_t k = 0; k < earth::AllCrops().size(); ++k) O << (k ? "," : "") << JsonText(earth::AllCrops()[k].Name);
	O << "],kept:[";
	for (size_t k = 0; k < KeptNames.size(); ++k) O << (k ? "," : "") << JsonText(KeptNames[k]);
	O << "],langs:{";
	First = true;
	for (const auto& [Id, Speakers] : LangSpeakers)
	{
		std::string Water = "?";
		for (const earth::Camp* L : Langs)
		{
			if (L->Id == Id) Water = earth::SayWord(L->Words[0]);
		}
		O << (First ? "" : ",") << Id << ":[" << JsonText(Water) << ',' << Speakers << ']';
		First = false;
	}
	O << "},beliefs:{";
	First = true;
	for (const auto& [Id, Bands] : BeliefBands)
	{
		const earth::BeliefRecord& B = Folk.AllBeliefs()[Id - 1];
		O << (First ? "" : ",") << Id << ":[" << JsonText(earth::SayWord(B.Word)) << ',' << JsonText(earth::BeliefGloss(B.Subject, B.Claim, B.Practice)) << ',' << Bands << ']';
		First = false;
	}
	O << "},ev:[";
	First = true;
	for (; FirstsDone < Folk.Firsts().size(); ++FirstsDone)
	{
		const earth::EcoEvent& E = Folk.Firsts()[FirstsDone];
		O << (First ? "" : ",") << '[' << static_cast<long long>(E.Year) << ',' << JsonText(E.Text) << ']';
		First = false;
	}
	O << "]});\n";
	std::ofstream(JsPath, std::ios::app) << O.str();
}

// The page that shows atlas.js; rewritten each time so it always matches the build.
inline void WriteAtlasPage(const std::string& HtmlPath, const std::string& JsName)
{
	std::ofstream(HtmlPath) << R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Earth 315k atlas</title><style>
:root{--bg:#fbfaf7;--fg:#1d1c1a;--muted:#6b6862;--line:#d9d5cc;--sea:#dfe8ee;--land:#e9e4d8;--panel:#fff}
@media (prefers-color-scheme:dark){:root{--bg:#171614;--fg:#ecebe7;--muted:#a19d95;--line:#3a3833;--sea:#1d2a33;--land:#34312b;--panel:#1f1e1b}}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:0;padding:16px;max-width:1100px;margin:auto}
h1{font-size:20px;margin:0 0 2px}.sub{color:var(--muted);margin:0 0 12px}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:8px 0}
button,select{font:inherit;color:var(--fg);background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:4px 10px;min-height:32px}
button[aria-pressed=true]{border-color:var(--fg)}input[type=range]{flex:1;min-width:160px}
canvas{width:100%;height:auto;display:block;border:1px solid var(--line);border-radius:6px;background:var(--sea)}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:12px}@media (max-width:700px){.cols{grid-template-columns:1fr}}
h2{font-size:15px;margin:0 0 6px}ul{list-style:none;padding:0;margin:0;max-height:320px;overflow:auto}li{padding:3px 0;border-bottom:1px solid var(--line)}
.sw{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:middle}.m{color:var(--muted)}
</style></head><body><h1>Earth 315k atlas</h1><p class="sub" id="when">Loading...</p>
<div class="bar"><button id="play">Play</button><input type="range" id="t" min="0" max="0" value="0" aria-label="Time">
<select id="mode" aria-label="Colour by"><option value="lang">Languages</option><option value="belief">Beliefs</option><option value="people">People</option><option value="farm">Crops and herds</option></select></div>
<canvas id="map" width="1080" height="540"></canvas>
<div class="cols"><div><h2 id="keyhead">Languages</h2><ul id="key"></ul></div><div><h2>What happened (up to this moment)</h2><ul id="ev"></ul></div></div>
<script>window.ATLAS=[];</script><script src=")HTML" << JsName << R"HTML("></script><script>
const F=[],seen={};for(const f of ATLAS){seen[f.y]=f}for(const y of Object.keys(seen).map(Number).sort((a,b)=>a-b))F.push(seen[y]);
const cv=document.getElementById('map'),g=cv.getContext('2d'),t=document.getElementById('t'),mode=document.getElementById('mode');
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const hue=id=>(id*137.508)%360;const col=id=>id?`hsl(${hue(id)} 65% 50%)`:css('--muted');
const fmt=n=>Math.round(n).toLocaleString('en-US');
function draw(i){const f=F[i];if(!f)return;const W=cv.width,H=cv.height,cols=180,rows=90,cw=W/cols,ch=H/rows;
g.fillStyle=css('--sea');g.fillRect(0,0,W,H);g.fillStyle=css('--land');let p=0,land=false;
for(const n of f.land.split(',').map(Number)){if(land)for(let k=p;k<p+n;k++){g.fillRect((k%cols)*cw,Math.floor(k/cols)*ch,cw+.5,ch+.5)}p+=n;land=!land}
const m=mode.value,max=Math.max(1,...f.sq.map(s=>s[1]));
const low=v=>v?Math.log2(v&-v):-1;
for(const [k,ppl,l,b,cr=0,hd=0] of f.sq){const x=(k%cols)*cw,y=Math.floor(k/cols)*ch;
g.fillStyle=m==='lang'?col(l):m==='belief'?(b?col(b):css('--muted')):m==='farm'?(cr?col(1+low(cr)):hd?col(50+low(hd)):css('--muted')):`hsl(25 80% ${70-45*Math.sqrt(ppl/max)}%)`;g.fillRect(x,y,cw+.5,ch+.5);
if(m==='farm'&&cr&&hd){g.fillStyle=col(50+low(hd));g.beginPath();g.arc(x+cw/2,y+ch/2,Math.min(cw,ch)/3,0,7);g.fill()}}
const people=f.sq.reduce((a,s)=>a+s[1],0);
document.getElementById('when').textContent=`Year ${fmt(f.y)}, ${fmt(f.ago)} years ago. ${fmt(people)} people, sea level ${f.sea} m.`;
const key=document.getElementById('key');key.innerHTML='';
if(m==='belief'){document.getElementById('keyhead').textContent='Beliefs (each band\'s oldest)';
for(const [id,[w,gl,n]] of Object.entries(f.beliefs).sort((a,b)=>b[1][2]-a[1][2]).slice(0,40)){const li=document.createElement('li');
li.innerHTML=`<span class="sw" style="background:${col(+id)}"></span><b></b> <span class="m"></span><br><span></span>`;li.querySelector('b').textContent='"'+w+'"';
li.querySelector('.m').textContent=n+' bands';li.lastChild.textContent=gl;key.appendChild(li)}}
else if(m==='farm'){document.getElementById('keyhead').textContent='Crops sown and animals kept (places)';
const n=(bit,idx)=>f.sq.filter(s=>(s[idx]||0)>>bit&1).length,rows=[];
(f.crops||[]).forEach((c,i)=>{const k=n(i,4);if(k)rows.push([col(1+i),c,k])});(f.kept||[]).forEach((c,i)=>{const k=n(i,5);if(k)rows.push([col(50+i),c,k])});
if(!rows.length){const li=document.createElement('li');li.className='m';li.textContent='Nobody farms or keeps animals yet.';key.appendChild(li)}
for(const [c,name,k] of rows){const li=document.createElement('li');li.innerHTML=`<span class="sw" style="background:${c}"></span><b></b> <span class="m"></span>`;
li.querySelector('b').textContent=name;li.querySelector('.m').textContent=k+(k===1?' place':' places');key.appendChild(li)}}
else{document.getElementById('keyhead').textContent='Languages (their word for water)';
for(const [id,[w,n]] of Object.entries(f.langs).sort((a,b)=>b[1][1]-a[1][1]).slice(0,40)){const li=document.createElement('li');
li.innerHTML=`<span class="sw" style="background:${col(+id)}"></span><b></b> <span class="m"></span>`;li.querySelector('b').textContent='"'+w+'"';
li.querySelector('.m').textContent=fmt(n)+' speakers';key.appendChild(li)}}
const ev=document.getElementById('ev');ev.innerHTML='';const all=[];for(let j=0;j<=i;j++)for(const e of F[j].ev)all.push(e);
for(const [y,txt] of all.reverse().slice(0,200)){const li=document.createElement('li');li.innerHTML='<span class="m"></span> ';li.firstChild.textContent='Year '+fmt(y)+':';li.append(txt);ev.appendChild(li)}}
t.max=Math.max(0,F.length-1);t.value=t.max;t.oninput=()=>draw(+t.value);mode.onchange=()=>draw(+t.value);
let timer=null;document.getElementById('play').onclick=e=>{if(timer){clearInterval(timer);timer=null;e.target.textContent='Play';return}
if(+t.value>=+t.max)t.value=0;e.target.textContent='Pause';timer=setInterval(()=>{if(+t.value>=+t.max){clearInterval(timer);timer=null;e.target.textContent='Play';return}t.value=+t.value+1;draw(+t.value)},400)};
if(!F.length)document.getElementById('when').textContent='No snapshots yet: the run adds one at every progress line.';else draw(F.length-1);
</script></body></html>
)HTML";
}

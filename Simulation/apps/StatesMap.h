// A history of the states: every few years the run adds a snapshot of who rules where, who is at war
// with whom, and what happened to the states since the last snapshot to states_history.js, and
// writes states.html next to it: a map with a time slider. Open states.html in any browser.
#pragma once

#include "Atlas.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

inline std::string Capital(std::string S)
{
	if (!S.empty()) S[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(S[0])));
	return S;
}

// Events about states, rulers and wars, out of all the people's firsts.
inline bool IsStateEvent(const std::string& T)
{
	for (const char* K : {"chiefdom", "kingdom", "empire", "nation of", " state", "war ", "war with", "Peace between", "allies", "law",
			 "city of", "throne", "revolt", "rises up", "prophet", " falls"})
		if (T.find(K) != std::string::npos) return true;
	return false;
}

// Appends one snapshot. EventsDone is how many of the people's firsts earlier snapshots looked through.
inline void AppendStatesFrame(const std::string& JsPath, const earth::Ecosystem& Eco, const earth::People& Folk, int Year, size_t& EventsDone)
{
	constexpr int Deg = 2;
	constexpr int Rows = 180 / Deg, Cols = 360 / Deg;
	std::ostringstream O;
	O << "STATES.push({y:" << Year << ",ago:" << static_cast<long long>(Eco.YearsAgo()) << ",land:\"";
	bool Land = false, First = true;
	int Run = 0;
	for (int r = 0; r < Rows; ++r)
		for (int c = 0; c < Cols; ++c)
		{
			const bool L = Eco.IsLand(90.0 - (r + 0.5) * Deg, -180.0 + (c + 0.5) * Deg);
			if (L != Land)
			{
				O << (First ? "" : ",") << Run;
				First = false, Run = 0, Land = L;
			}
			++Run;
		}
	O << (First ? "" : ",") << Run << "\",sq:[";
	// Each square goes to the state of its biggest band; squares with people and no state are 0.
	std::map<int, std::pair<int, uint32_t>> Squares;
	for (const earth::Camp& C : Folk.AllCamps())
	{
		if (C.Members.empty()) continue;
		const int R = std::clamp(static_cast<int>((90.0 - C.Lat) / Deg), 0, Rows - 1);
		const int K = std::clamp(static_cast<int>((C.Lon + 180.0) / Deg), 0, Cols - 1);
		auto& S = Squares[R * Cols + K];
		if (static_cast<int>(C.Members.size()) > S.first) S = {static_cast<int>(C.Members.size()), C.State};
	}
	First = true;
	for (const auto& [Key, S] : Squares)
	{
		O << (First ? "" : ",") << '[' << Key << ',' << S.second << ']';
		First = false;
	}
	O << "],s:{";
	First = true;
	for (const earth::Polity& P : Folk.AllStates())
	{
		O << (First ? "" : ",") << P.Id << ":[" << JsonText(earth::People::StateName(P)) << ',' << static_cast<int>(P.Tier) << ',' << P.Bands << ','
		  << P.Members << ',' << static_cast<int>(P.Lat * 10.0) / 10.0 << ',' << static_cast<int>(P.Lon * 10.0) / 10.0 << ',' << P.Enemy << ','
		  << JsonText(P.Ruler ? Capital(earth::SayWord(P.RulerName)) : "") << ']';
		First = false;
	}
	O << "},ev:[";
	First = true;
	for (; EventsDone < Folk.Firsts().size(); ++EventsDone)
	{
		const earth::EcoEvent& E = Folk.Firsts()[EventsDone];
		if (!IsStateEvent(E.Text)) continue;
		O << (First ? "" : ",") << '[' << static_cast<long long>(E.Year) << ',' << JsonText(E.Text) << ']';
		First = false;
	}
	O << "]});\n";
	std::ofstream(JsPath, std::ios::app) << O.str();
}

inline void WriteStatesPage(const std::string& HtmlPath, const std::string& JsName)
{
	std::ofstream(HtmlPath) << R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Earth 315k: states and wars</title><style>
:root{--bg:#fbfaf7;--fg:#1d1c1a;--muted:#6b6862;--line:#d9d5cc;--sea:#dfe8ee;--land:#e9e4d8;--panel:#fff;--war:#b3261e}
@media (prefers-color-scheme:dark){:root{--bg:#171614;--fg:#ecebe7;--muted:#a19d95;--line:#3a3833;--sea:#1d2a33;--land:#34312b;--panel:#1f1e1b;--war:#ef7b72}}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:0;padding:16px;max-width:1100px;margin:auto}
h1{font-size:20px;margin:0 0 2px}.sub{color:var(--muted);margin:0 0 12px}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:8px 0}
button{font:inherit;color:var(--fg);background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:4px 10px;min-height:32px;cursor:pointer}
input[type=range]{flex:1;min-width:160px}label{color:var(--muted)}
canvas{width:100%;height:auto;display:block;border:1px solid var(--line);border-radius:6px;background:var(--sea);touch-action:none}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin-top:12px}@media (max-width:700px){.cols{grid-template-columns:1fr}}
h2{font-size:15px;margin:0 0 6px}ul{list-style:none;padding:0;margin:0;max-height:360px;overflow:auto}li{padding:3px 0;border-bottom:1px solid var(--line)}
.sw{display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:6px;vertical-align:middle}.m{color:var(--muted)}.w{color:var(--war)}
</style></head><body><h1>States and wars</h1><p class="sub" id="when">Loading...</p>
<div class="bar"><button id="play">Play</button><input type="range" id="t" min="0" max="0" value="0" aria-label="Year">
<label><input type="checkbox" id="zoom"> Zoom to the states</label></div>
<canvas id="map" width="1080" height="540" aria-label="Map of the states"></canvas>
<p class="m">Each colour is a state; grey is people with no state. Red lines join the capitals of states at war.</p>
<div class="cols"><div><h2 id="keyhead">Biggest states</h2><ul id="key"></ul></div><div><h2>History of the states (up to this year)</h2><ul id="ev"></ul></div></div>
<script>window.STATES=[];</script><script src=")HTML" << JsName << R"HTML("></script><script>
const F=[],seen={};for(const f of STATES){seen[f.y]=f}for(const y of Object.keys(seen).map(Number).sort((a,b)=>a-b))F.push(seen[y]);
const cv=document.getElementById('map'),g=cv.getContext('2d'),t=document.getElementById('t'),zoom=document.getElementById('zoom');
const css=n=>getComputedStyle(document.documentElement).getPropertyValue(n).trim();
const col=id=>id?`hsl(${(id*137.508)%360} 62% 50%)`:css('--muted');const fmt=n=>Math.round(n).toLocaleString('en-US');
const tiers=['chiefdom','kingdom','empire','nation'];
function view(f){if(!zoom.checked)return[0,0,180,90];let r0=90,r1=0,c0=180,c1=0;for(const[k,s]of f.sq){if(!s)continue;const r=Math.floor(k/180),c=k%180;r0=Math.min(r0,r);r1=Math.max(r1,r);c0=Math.min(c0,c);c1=Math.max(c1,c)}
if(r1<r0)return[0,0,180,90];const pad=3;r0=Math.max(0,r0-pad);c0=Math.max(0,c0-pad);r1=Math.min(89,r1+pad);c1=Math.min(179,c1+pad);let w=c1-c0+1,h=r1-r0+1;
if(w<2*h){const e=2*h-w;c0=Math.max(0,c0-Math.floor(e/2));w=Math.min(180-c0,2*h)}else{const e=Math.ceil(w/2)-h;r0=Math.max(0,r0-Math.floor(e/2));h=Math.min(90-r0,Math.ceil(w/2))}return[c0,r0,w,h]}
function draw(i){const f=F[i];if(!f)return;const W=cv.width,H=cv.height,[c0,r0,vw,vh]=view(f),cw=W/vw,ch=H/vh;
const X=c=>(c-c0)*cw,Y=r=>(r-r0)*ch;g.fillStyle=css('--sea');g.fillRect(0,0,W,H);g.fillStyle=css('--land');let p=0,land=false;
for(const n of f.land.split(',').map(Number)){if(land)for(let k=p;k<p+n;k++)g.fillRect(X(k%180),Y(Math.floor(k/180)),cw+.5,ch+.5);p+=n;land=!land}
for(const[k,s]of f.sq){g.fillStyle=col(s);g.globalAlpha=s?1:.35;g.fillRect(X(k%180),Y(Math.floor(k/180)),cw+.5,ch+.5)}g.globalAlpha=1;
const at=(la,lo)=>[X((lo+180)/2),Y((90-la)/2)];g.strokeStyle=css('--war');g.lineWidth=2.5;
for(const[id,s]of Object.entries(f.s)){const e=f.s[s[6]];if(!e||+id>s[6])continue;const[a,b]=at(s[4],s[5]),[c,d]=at(e[4],e[5]);g.beginPath();g.moveTo(a,b);g.lineTo(c,d);g.stroke()}
const list=Object.entries(f.s).sort((a,b)=>b[1][3]-a[1][3]);g.fillStyle=css('--fg');g.font='bold 13px system-ui';
for(const[id,s]of list.slice(0,12)){if(s[1]<1)continue;const[a,b]=at(s[4],s[5]);g.beginPath();g.arc(a,b,3.5,0,7);g.fill();g.fillText(s[0].replace(/^the /,''),a+6,b-4)}
const wars=list.filter(([id,s])=>f.s[s[6]]&&+id<s[6]).length;
document.getElementById('when').textContent=`Year ${fmt(f.y)}, ${f.ago>0?fmt(f.ago)+' years ago':fmt(-f.ago)+' years from now'}. ${list.length} states, ${wars} wars.`;
const key=document.getElementById('key');key.innerHTML='';
for(const[id,s]of list.slice(0,40)){const li=document.createElement('li');li.innerHTML='<span class="sw"></span><b></b> <span class="m"></span>';li.firstChild.style.background=col(+id);
li.querySelector('b').textContent=s[0];li.querySelector('.m').textContent=`${tiers[s[1]]}, ${fmt(s[3])} people in ${s[2]} bands`+(s[7]?`, ruled by ${s[7]}`:'');
const e=f.s[s[6]];if(e){const w=document.createElement('div');w.className='w';w.textContent='At war with '+e[0];li.appendChild(w)}key.appendChild(li)}
if(!list.length){const li=document.createElement('li');li.className='m';li.textContent='No states yet.';key.appendChild(li)}
const ev=document.getElementById('ev');ev.innerHTML='';const all=[];for(let j=0;j<=i;j++)for(const e of F[j].ev)all.push(e);
for(const[y,txt]of all.reverse().slice(0,300)){const li=document.createElement('li');li.innerHTML='<span class="m"></span> ';li.firstChild.textContent='Year '+fmt(y)+':';li.append(txt);if(/war|conquers/.test(txt))li.className='w';ev.appendChild(li)}}
t.max=Math.max(0,F.length-1);t.value=t.max;t.oninput=()=>draw(+t.value);zoom.onchange=()=>draw(+t.value);
let timer=null;document.getElementById('play').onclick=e=>{if(timer){clearInterval(timer);timer=null;e.target.textContent='Play';return}
if(+t.value>=+t.max)t.value=0;e.target.textContent='Pause';timer=setInterval(()=>{if(+t.value>=+t.max){clearInterval(timer);timer=null;e.target.textContent='Play';return}t.value=+t.value+1;draw(+t.value)},300)};
if(!F.length)document.getElementById('when').textContent='No states yet: the run adds a snapshot every 10 years once there are any.';else draw(F.length-1);
</script></body></html>
)HTML";
}

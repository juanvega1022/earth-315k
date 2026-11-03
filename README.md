# Earth 315k

A simulation of Earth starting 315,000 years ago, around the time of the oldest known *Homo sapiens* fossils. The planet is built from real elevation data, with seeded procedural generation (the No Man's Sky idea) adding detail finer than the data. AI NPCs with early-human traits come later.

The goal is to watch AI NPCs grow and develop from early-human starting traits. The work goes in steps:

1. **The planet and its life.** A 3D globe of the whole planet from real elevation data, with a higher-detail patch over Africa, Arabia and the Levant. Plants grow and die back with the seasons, and 49 wild animal species of the time (elephants, mammoths, cave lions, Smilodon, Diprotodon, red deer, reindeer, aurochs, wild boar, wild goats and sheep, junglefowl, turkeys, leopards and more), each in its real range, graze, hunt, breed and die out. Bands that learn dogs or herding tame the wild animals around them, and bands that learn planting sow the crops whose wild plants grow where they live.
2. **The first NPC band (now)** in East Africa, with needs, a small brain that learns, and children who inherit from parents.
3. **Watching tools:** time speed, following one NPC, and a timeline of firsts.

## What you need (Windows)

- **Unreal Engine 5.8** (5.4 or newer should also work) from the Epic Games Launcher
- **Visual Studio 2022** with the "Game development with C++" workload (the Epic installer guide lists the exact options)
- **Python 3.10 or newer** from python.org (tick "Add Python to PATH" when installing)
- **Git**, or GitHub Desktop, to download this repository
- About 2 GB of free disk space for the elevation download

An RTX 4060 is plenty for this.

## First run

1. **Download this repository** to your PC (GitHub Desktop: File > Clone repository > `earth-315k`).

2. **Fetch the real elevation data.** Open a terminal in the repository folder and run:

   ```
   pip install -r tools/requirements.txt
   python tools/prepare_elevation.py
   ```

   This downloads NOAA's ETOPO 2022 global elevation model (about 450 MB, once) and writes `global` and `region` files into `Content/Data/`. If the download fails, download the 60 arc-second "surface" GeoTIFF by hand from <https://www.ncei.noaa.gov/products/etopo-global-relief-model> and run `python tools/prepare_elevation.py --source path\to\file.tif`.

   For the real climate (optional, but much closer to Earth), also run `python tools/prepare_climate.py`. It downloads WorldClim 2.1's present-day temperature and rain (10 arc-minutes, once) and writes `temperature` and `rain` files next to the elevation. Without them the headless simulation uses a rough climate from latitude. The Unreal viewer still uses the rough climate.

3. **Open the project.** Right-click `Earth315k.uproject` and pick "Generate Visual Studio project files", then double-click `Earth315k.uproject`. Say yes when Unreal asks to build the `Earth315k` module. The first build takes a few minutes.

4. **Press Play.** In the editor, File > New Level > Empty Level, then press Play. The planet, its plants and animals, a sun and a flying camera appear automatically. Fly with W A S D, Q and E for down and up, and the mouse to look.

## Watching life

Land is coloured by plant cover: deep green where plants are thick, brown where they are thin. Each coloured ball is a group of animals of one species, bigger for bigger animals and bigger herds. The top left of the screen shows the year, the season, how many animals are alive and which species have died out.

| Key | What it does |
| --- | --- |
| `]` | Twice as fast (up to 1,000 years per second, if your PC keeps up) |
| `[` | Half as fast |
| `P` | Pause or carry on |
| `1` `2` `3` `4` | Jump ahead 10, 100, 1,000 or 10,000 years, then sum up what happened (jumps of 1,000 years or more move everyone a month at a time, see below) |
| `C` | Stop a jump |
| `F` | Follow an NPC with the camera; press again for the next one |
| `H` | Follow a group of animals (the biggest of a species); press again for the next species |
| `G` | Stop following |
| `B` | Show or hide the brain of whatever you follow |
| `V` | Show or hide the plants below you |
| Mouse wheel | Zoom in or out while following |
| `R` | Start a new world with the next seed |

The world runs on its own CPU thread and moves the animals on all cores, so it is not held back by the frame rate. The top left shows the speed you asked for and the speed your PC actually reaches. Above about 60 days per second, plants and animals move several days at a time (up to 10), which is coarser but much faster; NPCs still live every day.

Drag a `LifeSimulation` into the level to change its `Seed`, starting speed or marker size.

## The NPCs

Thirty people start in a camp at Omo Kibish in southern Ethiopia, knowing nothing about the world. They are the bright pink balls (children are smaller). Each adult decides for itself every day whether to forage, hunt, drink, rest, share food, scout, hide or spend time with others.

- **Brain.** Each NPC has a small neural network: 16 senses (hunger, thirst, tiredness, fear, loneliness, the urge to mate, plants, game and water nearby, the season, age, health, band size, food carried, hunting skill), 12 hidden neurons and 8 actions.
- **Instincts.** The first band is born with simple drives: eat when hungry, drink when thirsty, rest when tired, hide from danger, seek company, and a weak urge to chase game. Everything else has to be learned or evolved.
- **Learning.** After each day, choices that made an NPC feel better than usual get stronger and ones that made it feel worse get weaker. Lessons slowly fade unless they keep being confirmed.
- **Culture.** Children copy their mothers, and adults sometimes copy an older camp mate who is doing better.
- **Children.** A baby's inborn wiring is a mix of both parents' with a few random changes, so useful instincts spread over generations.
- **Growing brains.** Each child can be born with one more hidden neuron (2% chance) or one fewer (1%), with no upper limit. A bigger brain costs more food each day but makes its owner better at hitting on new ideas and at learning from others, so brains grow only where cleverness pays.
- **Skills.** There are 40 skills to find, from sharp stone flakes, fire and warning calls through spoken language, boats and bows to farming, writing, iron and mathematics. Each needs the skills before it (no farming before planting), and deeper skills are much rarer finds. Anyone curious and clever enough can discover one; camp mates and nearby bands learn it from them, and a skill nobody still knows is lost and must be found again. Skills change the world: more food, better hunting, fewer deaths from predators and illness, warmth in the cold, travel over water, and more people the land can feed.
- **Languages.** Each band has its own calls and words, which slowly change over the centuries. Bands that live near each other borrow words and stay understandable; bands cut off from each other drift apart until they can't understand each other, and then they no longer learn skills from each other either. Each of the 16 words stands for something (water, food, danger, lion, fire, mother, child, come, go, hunt, meat, plant, sun, night, friend, no); the sounds are the people's own and change over time. The progress lines and the CSV count how many languages there are, and `run_sim --languages out\languages.txt` saves each language's words with their English meanings.
- **Inventions.** The world has 25 materials, each with properties (hard, sharp, flexible, fibrous, sticky, burns, can be moulded, colourful, heavy, floats, warm, watertight, long), found where they really are: obsidian only in volcanic regions, copper where its ores occur, bamboo in southern Asia, fur only from animals of cold lands, flint and ochre in some regions and not others. Bands notice what is around them, and now and then someone curious tries a way of working one or two of them (striking, tying to a shaft, twisting into cord, weaving, firing, scraping, stacking, shaping a float, marking...). If the materials' properties suit it, the band has invented something, such as a flint point tied to a shaft of wood, names it in its own words and passes it to neighbours, along with the word. Nothing is listed in advance: what gets made depends on where people live. Every material and invention a band knows becomes a word in its language. `run_sim --inventions out\inventions.txt` lists every invention with who made it first, where and when, how many bands hit on it independently, and how many still make it.
- **Beliefs.** Once a band has spoken language, it now and then explains what happens to it, more often after deaths or when predators press close: a belief made of a subject from its world (the dead, the sun, water, the beasts that hunt them, or one of its materials), a claim about it (it watches us, it must be fed, it is where we came from, it is forbidden to touch...) and a practice (burying the dead with gifts, offerings, night dances, ochre on the body, a sacred fire, carving sacred signs...). Practices cost a little food or freedom and hold the band together, so it grows bigger before splitting. Beliefs spread to neighbours, slowest into bands already held together by their own; bands that split off don't always carry every belief, and sometimes change one a little, starting a new sect; a band now and then lets a belief go; a belief held by 50 bands is noted as a religion. A band that believes a material is forbidden refuses inventions made with it, and the inventor is shunned, which goes in their life story. `run_sim --beliefs out\beliefs.txt` lists the beliefs held now with their name, founder, origin and sects.
- **Names, trust and leaders.** Everyone has a name in their band's speech. Each person remembers the few people who matter most to them and how much they trust each: a baby and its mother start out close, time together makes friends, being fed when hungry builds trust, and going hungry while a camp mate hoards plenty is not forgotten. People share first with those they trust and pass over anyone who cheated them, and women lean towards men they trust and others think well of. A person's reputation is how much those who know them trust them, and each band's most trusted adult becomes its leader, keeping the role until someone is clearly more trusted. Life stories now give names for people and their parents, partners and children.
- **Remembered places.** Everyone remembers the best-watered place they have lived, and the memory fades slowly. Children take their mother's memory when they grow up, so knowledge of a good place passes down. In a drought or famine a band follows its oldest adult (25 or over) back to such a place if it is no more than 1,500 km away and still better than where they are.
- **Families.** Couples tend to stay together: a woman favours the father of her last child while he is in her band. Young children are fed first by their mother, then by their father and their mother's mother, and only then by the rest of the camp. They come to love the kin who feed them, and children who play together grow up as friends.
- **Sentences and growing vocabularies.** When something worth telling happens (a first invention, a first belief, a first leader, an elder leading the band to water), the life story records it as a sentence in the band's own speech with a word-by-word gloss, for example *"Hu ri nee" (make Ri stone axe)*. A band coins a new word the first time it needs one (make, lead, teach, us, the dead and so on), so vocabularies grow with what each band has lived through, and the words drift and get borrowed like the rest. Each language puts its words in its own order (doer-deed-thing, doer-thing-deed or deed-doer-thing), which changes as the language drifts. `--languages` lists the coined words too.
- **Grief and personality.** When someone dies, those who loved them grieve. Grief weighs on how they feel and fades over a few months. Each person also inherits a kindness trait from their parents: how much of what they carry they give away when they share. Kindness and curiosity evolve, and the CSV and report track their averages.
- **Planning ahead and the first specialists.** Bands that know how to dry and smoke food put by what they can't carry, about half of which survives drying, and the store slowly spoils. Hungry members draw on it, so a good season tides the band over a lean one. A band of 20 or more with food put by can feed its most inventive member to make tools full time, which makes inventions in that band more likely. Leaders must now be at least 18.
- **Pace tuning.** Inventions follow the skill tree: a hafted point needs wooden spears, a fired pot needs pottery, a wall needs shelters, and so on. Each tier deeper in the skill tree is 20 times rarer to discover (it was 10, and run 3 reached farming about 270,000 years too early). Planting can only be discovered when the climate is warm and steady (ice cover under 15%), as in our own history, where farming began once the last ice age ended. Beliefs come about five times more slowly than before, and a religion needs 50 bands, not 10 (run 4 had 50 religions within 3,000 years of its first belief).
- **Real climate and rivers.** With the climate files, how much a place can grow comes from its real yearly temperature and rain (the Miami model), moved back to the time of the run: colder in an ice age, most of all near the poles, and with a third less rain. Rain runs downhill on the quarter-degree height map and gathers into rivers, so the Nile, Indus and Euphrates carry water through the deserts, and people can follow them. A band treats a river within about 30 km as water.
- **Life stories.** Anyone who makes a difference gets a story: the first to discover or rediscover a skill, anyone born with the biggest brain so far, and the first to reach each land, who also names it in their band's own speech. Each story has their parents, any ancestors with stories of their own, partners, children, their band's word for water, what they did and when, and how and where they died. `run_sim --stories out\stories.txt` saves them all at every progress line.

## Ice ages

The climate follows Earth's own history from 315,000 years ago. Every century the sea level moves to where it was then: near today's level in warm times and down to about 125 m lower at the depth of an ice age, when land bridges open to the Americas, across to Indonesia and New Guinea, and between Britain and Europe. Ice sheets cover northern North America and northern Europe, and the cold reaches further from the poles, so plants, animals and people have to move. Run `run_sim` with `--start 25000` to begin near the last glacial maximum instead, or `--no-ice-ages` to keep today's climate.
- **Bands.** People live in camps that move on when food or water runs low, towards the best place their scouts have found. Camps split when they grow past about 40, and small camps that meet join up.
- **How many.** Fewer children are born where the land around a camp is already full, so numbers level off where food runs short instead of at a fixed limit. Each square km of the greenest land feeds 0.0003 people, about 1 in 200 of the real figure for hunter-gatherers, so the whole world holds about 30,000 NPCs and stays affordable to simulate.

Press `F` to follow someone. The panel on the right shows their brain live: senses on the left, hidden neurons in the middle and the 8 actions on the right with the chance of each. Green lines push towards an action and red lines push away, brighter for stronger. Below are their needs, skills and how they have spent their days. The top left lists "firsts" as they happen: the first hunt, the first child, the first band split, the first person to set foot outside Africa.

If everyone dies, a new band of 30 arrives at Omo Kibish, born with the instincts of the most successful NPCs so far, so the run goes on and keeps what evolved.

## Animal brains and plants

Each group of animals has a tiny brain too: 6 senses (hunger, food here, better food nearby, danger, season, crowding), 5 hidden neurons and 4 drives (the pull of food, caution, the urge to roam, the urge to stay). The drives decide where the group moves each day. When a group grows big enough it splits, and the new group inherits the brain with small random changes, so groups that move well grow, split more and pass their wiring on.

Press `H` to follow a group and `B` to see its brain. Press `V` to see the patch of plants below you (or below what you follow): how much plant food is there, the most it can hold, the growing season, how much animals eat each day, and a graph over recent days. Plants have no brain.

Under each brain panel, "brain waves" show the hidden neurons over the last few hundred days, one coloured line each.

`earth315k_sim --brain brain.json` saves the brain of the oldest NPC at the end of a run, for charts and other tools.

## Running life without graphics

The simulation itself is plain C++ in `Simulation/` and can run on its own, much faster than with graphics. With CMake installed (Visual Studio includes it):

```
cmake -S Simulation -B Simulation/build
cmake --build Simulation/build --config Release
Simulation/build/Release/earth315k_sim --years 1000 --csv populations.csv
```

(On Linux or macOS the program is at `Simulation/build/earth315k_sim`.) It prints the animals and people at least every 1,000 years, the people's firsts and any species that died out as they happen, and `--csv` saves the populations each year for charts. Both are written as the run goes, so you can watch a long run. `--brain brain.json` saves the oldest NPC's brain each time it prints and at the end. Run it with no data and it tells you to fetch the elevation first.

### Long runs

Add `--fast 30` to move everything a month at a time, people included. Each NPC's brain then decides once a month: half the month goes to what it picks, the rest is spread the way it would choose on a typical day, and it fetches water, rests and gathers extra food as needed along the way. It still learns from how the month went, has children and passes its wiring on. This is coarser than living every day, but the brains run on all cores and it is many times faster, which is what makes 315,000 years possible:

```
Simulation/build/Release/earth315k_sim --years 315000 --fast 30 --csv populations.csv --brain brain.json > run.txt
```

`--density D` changes how many people a square km of the greenest land can feed (default 0.0003); more people means a slower run. `--max-people N` adds a hard limit if you want one.

### Save points

Add `--save out\world.bin` and the whole world is saved every 1,000 years (`--save-every N` to change that). To stop cleanly, press Ctrl+C or create an empty file `out\world.bin.stop`; it saves first, then exits. To carry on, after installing a newer build or not, run with `--resume out\world.bin` and the same other flags; `--years` is then the year to stop at. The CSV is appended to. A resumed run gives exactly the same results as one that never stopped. Saves from older builds keep loading after new features are added.

To change the planet while editing, drag `PlanetGlobe` from the Place Actors panel into a level, then change its settings in the Details panel. The planet rebuilds as you change them.

### Run log and report

Every session of a run adds a line to `runs.txt` next to its outputs: when it started or resumed, the build and save format, the seed and all the arguments, and when it finished or was stopped. Add `--report out\report.html` (with `--csv`) and the run keeps an HTML page up to date at every progress line, with charts of people, bands, languages, skills, inventions, brains, sea level and plants, and a scorecard comparing when the run left Africa, reached each land and invented bows, pottery, farming and writing with when people really did. Open it in any browser while the run goes.

### History atlas

Add `--atlas out\atlas.html` and at every progress line the run adds a snapshot of the world to `out\atlas.html.js`: the coastline at that time's sea level, where people live, which language and belief each place holds, and what happened since the last snapshot. Open `out\atlas.html` in a browser and drag the slider or press Play to watch people spread, languages split and religions form, colouring the map by languages, beliefs or people. A resumed run keeps adding to the same atlas.

### Kept animals and crops

The world starts with only wild animals. When a band knows **dogs**, it can raise wolf pups if gray wolves live near its camp. When it knows **herding**, it can tame whatever wild herds live around it, as people really did: goats from wild goats and sheep from mouflon (the Zagros and Anatolia), cattle from aurochs, pigs from wild boar, horses from wild horses, donkeys from African wild asses, water buffalo, chickens from red junglefowl (South and Southeast Asia), turkeys (North America), llamas from guanacos (the Andes) and reindeer. Kept herds walk with their band, are drawn on the globe like any other herd, and grow only as big as the band can tend. When a band merges into another its herds go with it, and when a band dies out its herds go wild.

When a band knows **planting**, it sows the crops whose wild plants grow where it lives: wheat, barley and lentils in the Fertile Crescent, rice on the Yangtze, millet on the Yellow River, maize, beans and squash in Mexico, potatoes in the Andes, sorghum in the Sahel, yams in West Africa, taro and bananas in New Guinea, and sunflowers in eastern North America. Neighbouring bands that plant pick up each other's crops where their climate lets them grow. The first of each kept animal and each crop is in the history and the stories, and the atlas has a **Crops and herds** view.

Saves from before this update load as normal: the new wild animals appear in their ranges when the save is loaded.

### Other kinds of human (experimental, `--other-humans`)

With `--other-humans`, Neanderthals (Atapuerca, the Rhine, the Caucasus, the Zagros), Denisovans (the Altai and the Tibetan Plateau), Homo erectus (Java), Homo naledi (South Africa) and Homo floresiensis (Flores) start in their real homes if the run begins before they were last seen. They run on the same minds as the NPCs and start with the skills they had long had there (fire, hand axes, spears, clothing and so on). Neanderthals are built for the cold but need more food, and erectus, naledi and floresiensis have smaller brains. They speak their own languages, which NPCs can't understand. Bands of different kinds mostly keep apart, but now and then join, and NPCs and others can have children together. Every person carries the share of their ancestry from the other kinds. Whether the others last, mix in or die out is left to the world. The progress line, CSV and report count each kind, and the scorecard only counts NPCs reaching new lands. It is off by default for now: in tests Neanderthals die out within a few centuries, mostly to predators, and Homo naledi spreads across Africa, neither like the real record.

## Settings worth playing with

| Setting | What it does |
| --- | --- |
| `SeaLevelMeters` | Sea level compared with today. Try `-120` to see ice-age coastlines and land bridges. |
| `ElevationExaggeration` | How much mountains are stretched so they show at globe size. |
| `Seed` | Changes the procedural detail. The same seed always gives the same planet. |
| `FaceResolution` / `RegionResolution` | Mesh detail for the globe and the region. Higher is slower to build. |
| `DetailAmplitudeMeters` | Height of procedural detail in the region. `0` shows real data only. |

## How it works

- `tools/prepare_elevation.py` turns the ETOPO model into a coarse global grid and a detailed regional grid.
- `Simulation/include/PlanetCore/` is the simulation in plain C++ with no Unreal code, so it can be tested and run on its own: elevation sampling, seeded noise, cube-sphere geometry and terrain colouring, plus `Ecosystem.h` (plants and animals) and `Species.h` (the species list, with sizes, diets, ranges and speeds). The same seed always gives the same history.
- `ALifeSimulation` runs the ecosystem inside Unreal and draws it on the globe.
- `APlanetGlobe` builds the mesh: six cube faces for the globe, plus a latitude/longitude patch for the region where the noise adds detail. Real data decides where land and sea are, and the noise is never allowed to move a coastline.
- `AEarthGameMode` sets up the planet, sun and camera when you press Play.

## Known limits of this first version

- Plant growth comes from latitude, height and a desert belt, not a real climate model, and it is worked out in 2° squares.
- Water comes from how green a place can be, not from real rivers and lakes.
- The NPCs have been tuned on a made-up test continent; on the real map some balancing may be needed.
- Animals move as herds and packs, not as individuals. Some species don't balance yet: wild horses multiply until they fill the group limit, and some hunters die out early.
- Sea level is the only change from today's map. Ice sheets, the Green Sahara and lakes of 315,000 years ago are not modelled yet.
- The planet is not to real scale; it's a model globe you fly around.
- The engine's built-in vertex colour material is used, so lighting is basic.

## Running the tests

```
cmake -S Simulation -B Simulation/build
cmake --build Simulation/build --config Release
ctest --test-dir Simulation/build -C Release
python tests/test_prepare_elevation.py
```

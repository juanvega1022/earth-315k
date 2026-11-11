// Saving a running world to a file and carrying on from it later, even with newer code.
#pragma once

#include "Archive.h"
#include "Ecosystem.h"
#include "People.h"
#include "Space.h"

#include <cstdio>
#include <string>

namespace earth
{

// Writes to Path + ".tmp" first and then swaps it in, so a crash mid-save never spoils the last good save.
inline bool SaveWorld(const std::string& Path, Ecosystem& Eco, People& Folk, space::Colonies* Off = nullptr)
{
	const std::string Tmp = Path + ".tmp";
	{
		Archive Ar(Tmp, true);
		Eco.Serialize(Ar);
		Folk.Serialize(Ar);
		space::Colonies None;
		(Off ? *Off : None).Serialize(Ar);
		if (!Ar.Ok()) return false;
	}
	std::remove(Path.c_str());
	return std::rename(Tmp.c_str(), Path.c_str()) == 0;
}

// Eco and Folk must be built with the same elevation data and species first; this replaces their state.
inline bool LoadWorld(const std::string& Path, Ecosystem& Eco, People& Folk, space::Colonies* Off = nullptr)
{
	Archive Ar(Path, false);
	if (!Ar.Ok()) return false;
	Eco.Serialize(Ar);
	Folk.Serialize(Ar);
	if (Ar.Version >= 13 && Off) Off->Serialize(Ar);
	if (!Ar.Ok()) return false;
	Folk.NoteResumed(Ar.Version);
	return true;
}

} // namespace earth

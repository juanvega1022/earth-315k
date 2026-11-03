#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "PlanetCore/Ecosystem.h"
#include "PlanetCore/People.h"

#include "EarthHUD.generated.h"

class ALifeSimulation;
class UFont;

/**
 * Draws what the camera is following:
 *  - an NPC's brain: 16 senses on the left, 12 hidden neurons in the middle and 8 possible
 *    actions on the right, with the connections between them, then its needs and life so far;
 *  - an animal group's brain: 6 senses, 5 hidden neurons and 4 drives that decide where it moves;
 *  - the patch of plants below, with how it grows and how much is eaten.
 * Green connections push an output up, red ones push it down; brighter means stronger.
 * "Brain waves" are the hidden neurons over the last days, one line each.
 * While jumping ahead in time, a progress bar.
 */
UCLASS()
class EARTH315K_API AEarthHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawNpcPanel(const earth::Npc& P, const ALifeSimulation& Sim);
	void DrawAnimalPanel(const earth::AnimalGroup& G, const ALifeSimulation& Sim);
	void DrawPlantPanel(const earth::PlantPatch& Patch, const ALifeSimulation& Sim);
	void DrawJumpBar(const ALifeSimulation& Sim);
	void DrawWaves(const TArray<TArray<float>>& Waves, float Left, float Top, float Width, float Height);
	void DrawBar(const TCHAR* Label, float Value, const FLinearColor& Color, float Left, float& Y, float LabelWidth = 110.f);

	UFont* Font = nullptr;
	float Scale = 1.f;
};

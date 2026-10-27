#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "EarthHUD.generated.h"

/**
 * Draws the brain of the NPC being followed: its 16 senses on the left, 12 hidden neurons in
 * the middle and 8 possible actions on the right, with the connections between them. Green
 * connections push towards an action, red ones push away; brighter means stronger. Below it,
 * the NPC's needs and life so far.
 */
UCLASS()
class EARTH315K_API AEarthHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};

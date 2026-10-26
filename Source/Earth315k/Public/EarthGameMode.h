#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "EarthGameMode.generated.h"

class APlanetGlobe;

/**
 * Default game mode. Pressing Play in any level spawns the planet (if the level
 * doesn't already have one), a sun, and a free-flying camera facing Africa.
 */
UCLASS()
class EARTH315K_API AEarthGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEarthGameMode();

	virtual void StartPlay() override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

	/** How far from the planet centre the camera starts, in globe radii. */
	UPROPERTY(EditAnywhere, Category = "Planet")
	float StartDistanceInRadii = 3.f;

private:
	APlanetGlobe* EnsureWorldSetUp();
};

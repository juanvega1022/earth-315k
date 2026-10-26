#include "EarthGameMode.h"

#include "PlanetGlobe.h"

#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"

AEarthGameMode::AEarthGameMode()
{
	DefaultPawnClass = ADefaultPawn::StaticClass();
}

APlanetGlobe* AEarthGameMode::EnsureWorldSetUp()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	APlanetGlobe* Globe = nullptr;
	for (TActorIterator<APlanetGlobe> It(World); It; ++It)
	{
		Globe = *It;
		break;
	}
	if (!Globe)
	{
		// Turned half a turn so longitude 0 (Greenwich, Africa) faces the starting camera.
		Globe = World->SpawnActor<APlanetGlobe>(FVector::ZeroVector, FRotator(0.f, 180.f, 0.f));
	}

	bool bHasSun = false;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		bHasSun = true;
		break;
	}
	if (!bHasSun)
	{
		// Light coming from over the camera's shoulder.
		World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-20.f, 30.f, 0.f));
	}
	return Globe;
}

void AEarthGameMode::StartPlay()
{
	EnsureWorldSetUp();
	Super::StartPlay();
}

APawn* AEarthGameMode::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	const APlanetGlobe* Globe = EnsureWorldSetUp();
	const float Radius = Globe ? Globe->GlobeRadius : 20000.f;
	const FVector Centre = Globe ? Globe->GetActorLocation() : FVector::ZeroVector;

	// Start out in space on the -X side, looking along +X at the planet.
	FTransform Start(FRotator::ZeroRotator, Centre - FVector(Radius * StartDistanceInRadii, 0.f, 0.f));
	return Super::SpawnDefaultPawnAtTransform_Implementation(NewPlayer, Start);
}

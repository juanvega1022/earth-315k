#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PlanetCore/Ecosystem.h"

#include "LifeSimulation.generated.h"

class APlanetGlobe;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Runs the plants-and-animals simulation on the planet and draws it: land turns green or
 * brown with plant cover, and each animal group is a coloured ball sized by how many it has.
 *
 * Keys while playing: [ slower, ] faster, P pause.
 */
UCLASS()
class EARTH315K_API ALifeSimulation : public AActor
{
	GENERATED_BODY()

public:
	ALifeSimulation();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Same seed, same history. */
	UPROPERTY(EditAnywhere, Category = "Life")
	int64 Seed = 315000;

	/** Simulated days per real second. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1", ClampMax = "36500"))
	float DaysPerSecond = 30.f;

	UPROPERTY(EditAnywhere, Category = "Life")
	bool bPaused = false;

	/** Size of the animal markers. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1"))
	float MarkerScale = 1.f;

	/** How often the plant colours on the globe are refreshed, in real seconds. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1"))
	float PlantRepaintSeconds = 0.5f;

private:
	void HandleKeys();
	void StepSimulation(float DeltaSeconds);
	void DrawAnimals();
	void ShowStatus() const;

	UPROPERTY()
	TObjectPtr<APlanetGlobe> Globe;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> Markers;

	UPROPERTY()
	TObjectPtr<UStaticMesh> MarkerMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	TUniquePtr<earth::Ecosystem> Life;
	double PendingDays = 0.0;
	float SinceRepaint = 0.f;
	float SinceDraw = 0.f;
};

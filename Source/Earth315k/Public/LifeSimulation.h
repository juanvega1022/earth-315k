#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PlanetCore/Ecosystem.h"
#include "PlanetCore/People.h"

#include "LifeSimulation.generated.h"

class APlanetGlobe;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/**
 * Runs the plants, animals and NPCs on the planet and draws them: land turns green or brown
 * with plant cover, each animal group is a ball in its species' colour, and each NPC is a
 * bright pink ball. Markers grow with distance so they stay visible from space.
 *
 * Keys while playing: [ slower, ] faster, P pause, F follow an NPC (again for the next one),
 * G stop following, B show or hide the followed NPC's brain, mouse wheel zoom while following,
 * R start a new world with the next seed.
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
	float DaysPerSecond = 10.f;

	UPROPERTY(EditAnywhere, Category = "Life")
	bool bPaused = false;

	/** Size of the markers. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1"))
	float MarkerScale = 1.f;

	/** How often the plant colours on the globe are refreshed, in real seconds. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1"))
	float PlantRepaintSeconds = 0.5f;

	const earth::People* GetPeople() const { return Folk.Get(); }

	/** The NPC the camera is following, or null. */
	const earth::Npc* GetFollowed() const;

	bool IsBrainShown() const { return bShowBrain; }

private:
	void StartWorld();
	void HandleKeys();
	void StepSimulation(float DeltaSeconds);
	void FollowNext();
	void MoveCamera(float DeltaSeconds);
	void DrawMarkers();
	void ShowStatus();
	UInstancedStaticMeshComponent* MakeMarkers(const FLinearColor& Color);

	UPROPERTY()
	TObjectPtr<APlanetGlobe> Globe;

	UPROPERTY()
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> AnimalMarkers;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> NpcMarkers;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> FollowMarker;

	UPROPERTY()
	TObjectPtr<UStaticMesh> MarkerMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MarkerMaterial;

	TUniquePtr<earth::Ecosystem> Life;
	TUniquePtr<earth::People> Folk;
	double PendingDays = 0.0;
	double DaysDoneThisSecond = 0.0;
	double MeasuredDaysPerSecond = 0.0;
	float SinceMeasure = 0.f;
	float SinceRepaint = 0.f;
	float SinceDraw = 0.f;

	uint32 FollowedId = 0;
	FString FollowNote;
	float FollowHeight = 400.f;
	bool bShowBrain = true;
};

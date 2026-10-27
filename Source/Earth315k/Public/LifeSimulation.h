#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"

#include "PlanetCore/Ecosystem.h"
#include "PlanetCore/People.h"

#include <atomic>

#include "LifeSimulation.generated.h"

class APlanetGlobe;
class FLifeRunner;
class FRunnableThread;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

/** What the camera is following. */
enum class EFollowKind : uint8
{
	None,
	Npc,
	Animal
};

/** How the world stood when a time jump began, to sum up what changed. */
struct FLifeJumpStart
{
	double Day = 0.0;
	double Years = 0.0;
	double RealSeconds = 0.0;
	long long Births = 0;
	long long Deaths = 0;
	int32 People = 0;
	int32 Bands = 0;
	int32 Generation = 0;
	int32 BandsStarted = 0;
	size_t Firsts = 0;
	size_t Events = 0;
	double Animals = 0.0;
};

/**
 * Runs the plants, animals and NPCs on the planet and draws them: land turns green or brown
 * with plant cover, each animal group is a ball in its species' colour, and each NPC is a
 * bright pink ball. Markers grow with distance so they stay visible from space.
 *
 * The world runs on its own CPU thread (and moves the animals on all cores), so the speed is
 * not limited by the frame rate. At high speeds plants and animals move several days at a time.
 *
 * Keys while playing: [ slower, ] faster, P pause, 1 2 3 4 jump ahead 10, 100, 1,000 or 10,000
 * years, C cancel a jump, F follow an NPC (again for the next one), H follow an animal herd
 * (again for the next species), G stop following, B show or hide the brain, V show or hide the
 * plants, mouse wheel zoom while following, R start a new world with the next seed.
 */
UCLASS()
class EARTH315K_API ALifeSimulation : public AActor
{
	GENERATED_BODY()

public:
	ALifeSimulation();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Same seed, same history. */
	UPROPERTY(EditAnywhere, Category = "Life")
	int64 Seed = 315000;

	/** Simulated days per real second. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1", ClampMax = "365000"))
	float DaysPerSecond = 10.f;

	UPROPERTY(EditAnywhere, Category = "Life")
	bool bPaused = false;

	/** Size of the markers. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1"))
	float MarkerScale = 1.f;

	/** How often the plant colours on the globe are refreshed, in real seconds. */
	UPROPERTY(EditAnywhere, Category = "Life", meta = (ClampMin = "0.1"))
	float PlantRepaintSeconds = 0.5f;

	// Everything below reads the running world. Hold an FLifeReadScope while using it and
	// while using anything it returns; the world keeps running on its own thread otherwise.

	const earth::People* GetPeople() const { return Folk.Get(); }
	const earth::Ecosystem* GetLife() const { return Life.Get(); }

	EFollowKind GetFollowKind() const { return FollowKind; }

	/** The NPC the camera is following, or null. */
	const earth::Npc* GetFollowed() const;

	/** The animal group the camera is following, or null. */
	const earth::AnimalGroup* GetFollowedGroup() const;

	/** The patch of plants under whatever the camera is following, or under the camera. */
	earth::PlantPatch GetPlantPatch() const;

	bool IsBrainShown() const { return bShowBrain; }
	bool ArePlantsShown() const { return bShowPlants; }

	/** Recent samples of the followed brain's hidden neurons, oldest first, one array per sample. */
	const TArray<TArray<float>>& GetBrainWaves() const { return BrainWaves; }

	/** Recent samples of the plant patch: plants as a share of the best possible here, oldest first. */
	const TArray<float>& GetPlantHistory() const { return PlantHistory; }

	/** 0..1 while jumping ahead, negative otherwise. */
	float GetJumpProgress() const;
	FString GetJumpText() const;

private:
	friend class FLifeRunner;
	friend class FLifeReadScope;

	void StartWorld();
	void StopRunner();
	void HandleKeys();
	void StartJump(double Years);
	void FinishJump(bool bCancelled);
	void FollowNextNpc();
	void FollowNextHerd(bool bSameSpecies);
	void CheckFollowed();
	void SampleHistory();
	bool FollowedLatLon(double& Lat, double& Lon) const;
	void MoveCamera(float DeltaSeconds);
	void DrawMarkers();
	void ShowStatus();
	UInstancedStaticMeshComponent* MakeMarkers(const FLinearColor& Color);

	/** Days the world is moved in one go at the current speed (more when fast). */
	int32 StepDaysForSpeed() const;

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

	// The world, shared with the runner thread. Guarded by WorldLock.
	TUniquePtr<earth::Ecosystem> Life;
	TUniquePtr<earth::People> Folk;
	double PendingDays = 0.0;
	double JumpTargetDay = -1.0;
	int32 StepDays = 1;
	long long DaysDone = 0;

	mutable FCriticalSection WorldLock;
	// Readers waiting for the lock; the runner steps aside for them between steps.
	mutable std::atomic<int32> ReadersWaiting{0};
	// Raw pointer: the runner class is only defined in the .cpp. Created in BeginPlay, deleted in StopRunner.
	FLifeRunner* Runner = nullptr;
	FRunnableThread* RunnerThread = nullptr;

	// Game thread only.
	long long DaysDoneAtMeasure = 0;
	double MeasuredDaysPerSecond = 0.0;
	float SinceMeasure = 0.f;
	float SinceRepaint = 0.f;
	float SinceDraw = 0.f;

	FLifeJumpStart Jump;
	bool bJumping = false;
	FString JumpSummary;
	double JumpSummaryUntil = 0.0;

	EFollowKind FollowKind = EFollowKind::None;
	uint32 FollowedId = 0;
	uint32 FollowedGroupId = 0;
	int32 FollowedSpecies = -1;
	FString FollowNote;
	float FollowHeight = 400.f;
	bool bShowBrain = true;
	bool bShowPlants = false;

	double LastSampledDay = -1.0;
	TArray<TArray<float>> BrainWaves;
	TArray<float> PlantHistory;
};

/** Holds the world still while the game reads it. */
class EARTH315K_API FLifeReadScope
{
public:
	explicit FLifeReadScope(const ALifeSimulation* InSim);
	~FLifeReadScope();

private:
	const ALifeSimulation* Sim;
};

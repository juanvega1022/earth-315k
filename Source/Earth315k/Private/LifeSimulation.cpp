#include "LifeSimulation.h"

#include "PlanetGlobe.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr double kStartYearsAgo = 315000.0;
	constexpr float kDrawSeconds = 0.1f;
	constexpr int32 kHistorySamples = 240;
	constexpr double kSummarySeconds = 40.0;
	const FLinearColor kNpcColor(1.0f, 0.15f, 0.75f);
	const FLinearColor kFollowColor(1.0f, 0.95f, 0.2f);

	// Fixed keys so the on-screen lines replace themselves instead of scrolling.
	constexpr int32 kStatusKey = 315000;

	// Markers grow with distance from the camera so they stay visible from far away.
	double Visibility(const FVector& Camera, const FVector& Where)
	{
		return FMath::Clamp(FVector::Dist(Camera, Where) / 400.0, 1.0, 200.0);
	}

	FString Commas(double Value)
	{
		return FText::AsNumber(FMath::RoundToInt64(Value)).ToString();
	}

	FString Duration(double Seconds)
	{
		const int64 S = FMath::Max<int64>(0, FMath::RoundToInt64(Seconds));
		if (S < 60)
		{
			return FString::Printf(TEXT("%llds"), S);
		}
		if (S < 3600)
		{
			return FString::Printf(TEXT("%lldm %02llds"), S / 60, S % 60);
		}
		return FString::Printf(TEXT("%lldh %02lldm"), S / 3600, (S % 3600) / 60);
	}
}

/** Steps the world on its own thread, as fast as asked (or as fast as it can while jumping). */
class FLifeRunner : public FRunnable
{
public:
	explicit FLifeRunner(ALifeSimulation* InOwner) : Owner(InOwner) {}

	virtual uint32 Run() override
	{
		while (!bStop)
		{
			int32 Days = 0;
			{
				FScopeLock Lock(&Owner->WorldLock);
				if (Owner->Life && Owner->Folk)
				{
					const double Today = Owner->Life->Day();
					// Jumps of 1,000 years or more move everyone a month at a time, people included.
					bool bMonths = false;
					if (Owner->JumpTargetDay > Today)
					{
						bMonths = Owner->JumpTargetDay - Owner->Jump.Day >= 1000.0 * earth::Ecosystem::kDaysPerYear;
						Days = FMath::Clamp(static_cast<int32>(std::ceil(Owner->JumpTargetDay - Today)), 1, bMonths ? 30 : 10);
					}
					else if (Owner->PendingDays >= 1.0)
					{
						Days = FMath::Clamp(static_cast<int32>(Owner->PendingDays), 1, Owner->StepDays);
						Owner->PendingDays -= Days;
					}
					if (Days > 0)
					{
						Owner->Life->Step(Days);
						if (bMonths)
						{
							Owner->Folk->Step(Days);
						}
						else
						{
							for (int32 k = 0; k < Days; ++k)
							{
								Owner->Folk->StepDay();
							}
						}
						Owner->DaysDone += Days;
					}
				}
			}
			if (Days == 0)
			{
				FPlatformProcess::Sleep(0.002f);
			}
			// Let the game in to draw before taking the next step.
			while (Owner->ReadersWaiting.load() > 0 && !bStop)
			{
				FPlatformProcess::YieldThread();
			}
		}
		return 0;
	}

	virtual void Stop() override { bStop = true; }

private:
	ALifeSimulation* Owner;
	std::atomic<bool> bStop{false};
};

FLifeReadScope::FLifeReadScope(const ALifeSimulation* InSim) : Sim(InSim)
{
	if (Sim)
	{
		++Sim->ReadersWaiting;
		Sim->WorldLock.Lock();
		--Sim->ReadersWaiting;
	}
}

FLifeReadScope::~FLifeReadScope()
{
	if (Sim)
	{
		Sim->WorldLock.Unlock();
	}
}

ALifeSimulation::ALifeSimulation()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	MarkerMesh = Sphere.Object;
	MarkerMaterial = ShapeMaterial.Object;
}

UInstancedStaticMeshComponent* ALifeSimulation::MakeMarkers(const FLinearColor& Color)
{
	UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this);
	Ism->SetStaticMesh(MarkerMesh);
	Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ism->SetCastShadow(false);
	Ism->SetAbsolute(true, true, true);
	if (MarkerMaterial)
	{
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
		Mat->SetVectorParameterValue(TEXT("Color"), Color);
		Ism->SetMaterial(0, Mat);
	}
	Ism->SetupAttachment(RootComponent);
	Ism->RegisterComponent();
	return Ism;
}

void ALifeSimulation::BeginPlay()
{
	Super::BeginPlay();

	for (TActorIterator<APlanetGlobe> It(GetWorld()); It; ++It)
	{
		Globe = *It;
		break;
	}
	if (Globe)
	{
		Globe->EnsureBuilt();
	}
	if (!Globe || !Globe->GetGlobalGrid().IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("LifeSimulation: no planet with elevation data, so there is no life to simulate."));
		SetActorTickEnabled(false);
		return;
	}

	for (const earth::Species& S : earth::DefaultSpecies())
	{
		AnimalMarkers.Add(MakeMarkers(FLinearColor(S.Color[0], S.Color[1], S.Color[2])));
	}
	NpcMarkers = MakeMarkers(kNpcColor);
	FollowMarker = MakeMarkers(kFollowColor);
	StartWorld();

	Runner = new FLifeRunner(this);
	RunnerThread = FRunnableThread::Create(Runner, TEXT("Earth315kLife"), 0, TPri_Normal);
}

void ALifeSimulation::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopRunner();
	Super::EndPlay(EndPlayReason);
}

void ALifeSimulation::StopRunner()
{
	if (RunnerThread)
	{
		RunnerThread->Kill(true); // Calls Stop() and waits for the thread to finish.
		delete RunnerThread;
		RunnerThread = nullptr;
	}
	delete Runner;
	Runner = nullptr;
}

void ALifeSimulation::StartWorld()
{
	FLifeReadScope Scope(this);
	earth::EcosystemSettings Settings;
	Settings.SeaLevelMeters = Globe->SeaLevelMeters;
	Folk.Reset();
	Life = MakeUnique<earth::Ecosystem>(&Globe->GetGlobalGrid(), earth::DefaultSpecies(), static_cast<uint64_t>(Seed), Settings);
	Folk = MakeUnique<earth::People>(Life.Get(), static_cast<uint64_t>(Seed));
	FollowKind = EFollowKind::None;
	FollowedId = 0;
	FollowedGroupId = 0;
	FollowNote.Reset();
	PendingDays = 0.0;
	JumpTargetDay = -1.0;
	bJumping = false;
	JumpSummary.Reset();
	BrainWaves.Reset();
	PlantHistory.Reset();
	LastSampledDay = -1.0;
	Globe->TintPlants([this](double Lat, double Lon) { return static_cast<float>(Life->PlantCover(Lat, Lon)); });
	DrawMarkers();
}

int32 ALifeSimulation::StepDaysForSpeed() const
{
	// About 60 steps a second at most; beyond that plants and animals move several days at once.
	return FMath::Clamp(static_cast<int32>(DaysPerSecond / 60.f), 1, 10);
}

void ALifeSimulation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Life)
	{
		return;
	}

	HandleKeys();

	FLifeReadScope Scope(this);
	StepDays = StepDaysForSpeed();
	if (!bPaused && !bJumping)
	{
		// Don't let a backlog build up if the computer can't keep up: just run as fast as it can.
		PendingDays = FMath::Min(PendingDays + DeltaSeconds * DaysPerSecond, DaysPerSecond * 0.25 + 10.0);
	}
	if (bJumping && JumpTargetDay <= Life->Day())
	{
		FinishJump(false);
	}

	CheckFollowed();
	SampleHistory();
	MoveCamera(DeltaSeconds);

	SinceDraw += DeltaSeconds;
	if (SinceDraw >= kDrawSeconds)
	{
		SinceDraw = 0.f;
		DrawMarkers();
	}

	SinceRepaint += DeltaSeconds;
	if (SinceRepaint >= PlantRepaintSeconds * (bJumping ? 4.f : 1.f))
	{
		SinceRepaint = 0.f;
		Globe->TintPlants([this](double Lat, double Lon) { return static_cast<float>(Life->PlantCover(Lat, Lon)); });
	}

	SinceMeasure += DeltaSeconds;
	if (SinceMeasure >= 1.f)
	{
		MeasuredDaysPerSecond = (DaysDone - DaysDoneAtMeasure) / SinceMeasure;
		DaysDoneAtMeasure = DaysDone;
		SinceMeasure = 0.f;
	}

	ShowStatus();
}

void ALifeSimulation::HandleKeys()
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (!Player)
	{
		return;
	}
	if (Player->WasInputKeyJustPressed(EKeys::RightBracket))
	{
		DaysPerSecond = FMath::Min(DaysPerSecond * 2.f, 365000.f);
	}
	if (Player->WasInputKeyJustPressed(EKeys::LeftBracket))
	{
		DaysPerSecond = FMath::Max(DaysPerSecond * 0.5f, 0.25f);
	}
	if (Player->WasInputKeyJustPressed(EKeys::P))
	{
		bPaused = !bPaused;
	}
	if (Player->WasInputKeyJustPressed(EKeys::One))
	{
		StartJump(10.0);
	}
	if (Player->WasInputKeyJustPressed(EKeys::Two))
	{
		StartJump(100.0);
	}
	if (Player->WasInputKeyJustPressed(EKeys::Three))
	{
		StartJump(1000.0);
	}
	if (Player->WasInputKeyJustPressed(EKeys::Four))
	{
		StartJump(10000.0);
	}
	if (Player->WasInputKeyJustPressed(EKeys::C) && bJumping)
	{
		FLifeReadScope Scope(this);
		FinishJump(true);
	}
	if (Player->WasInputKeyJustPressed(EKeys::F))
	{
		FLifeReadScope Scope(this);
		FollowNextNpc();
	}
	if (Player->WasInputKeyJustPressed(EKeys::H))
	{
		FLifeReadScope Scope(this);
		FollowNextHerd(false);
	}
	if (Player->WasInputKeyJustPressed(EKeys::G))
	{
		FollowKind = EFollowKind::None;
		FollowNote.Reset();
		BrainWaves.Reset();
	}
	if (Player->WasInputKeyJustPressed(EKeys::B))
	{
		bShowBrain = !bShowBrain;
	}
	if (Player->WasInputKeyJustPressed(EKeys::V))
	{
		bShowPlants = !bShowPlants;
		PlantHistory.Reset();
	}
	if (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		FollowHeight = FMath::Max(FollowHeight * 0.8f, 60.f);
	}
	if (Player->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		FollowHeight = FMath::Min(FollowHeight * 1.25f, 20000.f);
	}
	if (Player->WasInputKeyJustPressed(EKeys::R))
	{
		Seed += 1;
		StartWorld();
	}
}

void ALifeSimulation::StartJump(double Years)
{
	FLifeReadScope Scope(this);
	if (bJumping)
	{
		// Pressing a jump key while jumping adds to the jump.
		JumpTargetDay += Years * earth::Ecosystem::kDaysPerYear;
		return;
	}
	Jump.Day = Life->Day();
	Jump.Years = Years;
	Jump.RealSeconds = FPlatformTime::Seconds();
	Jump.Births = Folk->TotalBirths();
	Jump.Deaths = Folk->TotalDeaths();
	Jump.People = Folk->Alive();
	Jump.Bands = Folk->BandCount();
	Jump.Generation = Folk->MaxGeneration();
	Jump.BandsStarted = Folk->BandsStarted();
	Jump.Firsts = Folk->Firsts().size();
	Jump.Events = Life->Events().size();
	Jump.Animals = Life->TotalAnimals();
	JumpTargetDay = Life->Day() + Years * earth::Ecosystem::kDaysPerYear;
	PendingDays = 0.0;
	bJumping = true;
	JumpSummary.Reset();
}

void ALifeSimulation::FinishJump(bool bCancelled)
{
	JumpTargetDay = -1.0;
	bJumping = false;
	const double YearsDone = (Life->Day() - Jump.Day) / earth::Ecosystem::kDaysPerYear;
	const double Took = FPlatformTime::Seconds() - Jump.RealSeconds;

	FString S = FString::Printf(TEXT("%s %s years in %s."), bCancelled ? TEXT("Jump stopped after") : TEXT("Jumped ahead"),
		*Commas(YearsDone), *Duration(Took));
	S += FString::Printf(TEXT("\n  People: %d -> %d in %d -> %d bands, %s born, %s died, generation %d -> %d"), Jump.People, Folk->Alive(),
		Jump.Bands, Folk->BandCount(), *Commas(static_cast<double>(Folk->TotalBirths() - Jump.Births)),
		*Commas(static_cast<double>(Folk->TotalDeaths() - Jump.Deaths)), Jump.Generation, Folk->MaxGeneration());
	if (Folk->BandsStarted() > Jump.BandsStarted)
	{
		S += FString::Printf(TEXT("\n  The people died out %d time(s); a new band arrived each time."), Folk->BandsStarted() - Jump.BandsStarted);
	}
	S += FString::Printf(TEXT("\n  Animals: %s -> %s"), *Commas(Jump.Animals), *Commas(Life->TotalAnimals()));

	// What happened along the way: the newest few of each.
	auto AddNews = [&S](const std::vector<earth::EcoEvent>& List, size_t From, int32 Max)
	{
		const size_t Limit = static_cast<size_t>(Max);
		const size_t Start = FMath::Max(From, List.size() > Limit ? List.size() - Limit : size_t(0));
		if (Start > From)
		{
			S += FString::Printf(TEXT("\n  ... %d earlier"), static_cast<int32>(Start - From));
		}
		for (size_t i = Start; i < List.size(); ++i)
		{
			S += FString::Printf(TEXT("\n  %s years ago: %s"), *Commas(kStartYearsAgo - List[i].Year), UTF8_TO_TCHAR(List[i].Text.c_str()));
		}
	};
	AddNews(Folk->Firsts(), Jump.Firsts, 8);
	AddNews(Life->Events(), Jump.Events, 4);

	JumpSummary = S;
	JumpSummaryUntil = FPlatformTime::Seconds() + kSummarySeconds;
	DrawMarkers();
}

float ALifeSimulation::GetJumpProgress() const
{
	if (!bJumping || !Life)
	{
		return -1.f;
	}
	const double Total = JumpTargetDay - Jump.Day;
	return Total > 0.0 ? static_cast<float>(FMath::Clamp((Life->Day() - Jump.Day) / Total, 0.0, 1.0)) : 1.f;
}

FString ALifeSimulation::GetJumpText() const
{
	if (!bJumping || !Life)
	{
		return FString();
	}
	const double DaysLeft = JumpTargetDay - Life->Day();
	const FString Left = MeasuredDaysPerSecond > 1.0 ? FString::Printf(TEXT(", about %s left"), *Duration(DaysLeft / MeasuredDaysPerSecond)) : FString();
	return FString::Printf(TEXT("Jumping ahead %s years: %d%%, %s years per second%s.   C to stop"),
		*Commas((JumpTargetDay - Jump.Day) / earth::Ecosystem::kDaysPerYear), FMath::RoundToInt(GetJumpProgress() * 100.f),
		*FString::Printf(TEXT("%.1f"), MeasuredDaysPerSecond / earth::Ecosystem::kDaysPerYear), *Left);
}

const earth::Npc* ALifeSimulation::GetFollowed() const
{
	return (Folk && FollowKind == EFollowKind::Npc && FollowedId != 0) ? Folk->Find(FollowedId) : nullptr;
}

const earth::AnimalGroup* ALifeSimulation::GetFollowedGroup() const
{
	return (Life && FollowKind == EFollowKind::Animal && FollowedGroupId != 0) ? Life->FindGroup(FollowedGroupId) : nullptr;
}

bool ALifeSimulation::FollowedLatLon(double& Lat, double& Lon) const
{
	if (const earth::Npc* P = GetFollowed())
	{
		Lat = P->Lat;
		Lon = P->Lon;
		return true;
	}
	if (const earth::AnimalGroup* G = GetFollowedGroup())
	{
		Lat = G->Lat;
		Lon = G->Lon;
		return true;
	}
	return false;
}

earth::PlantPatch ALifeSimulation::GetPlantPatch() const
{
	double Lat = 0.0;
	double Lon = 0.0;
	if (!FollowedLatLon(Lat, Lon))
	{
		// The land right below the camera.
		const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (Player && Player->PlayerCameraManager && Globe)
		{
			const FVector2D Below = Globe->WorldToLatLon(Player->PlayerCameraManager->GetCameraLocation());
			Lat = Below.X;
			Lon = Below.Y;
		}
	}
	return Life ? Life->PlantsAt(Lat, Lon) : earth::PlantPatch();
}

void ALifeSimulation::FollowNextNpc()
{
	// Adults only, in order of id, wrapping around: the oldest lineages first.
	const std::vector<earth::Npc>& All = Folk->All();
	const uint32 After = FollowKind == EFollowKind::Npc ? FollowedId : 0;
	uint32 Next = 0;
	uint32 Lowest = 0;
	for (const earth::Npc& P : All)
	{
		if (P.IsChild())
		{
			continue;
		}
		if (P.Id > After && (Next == 0 || P.Id < Next))
		{
			Next = P.Id;
		}
		if (Lowest == 0 || P.Id < Lowest)
		{
			Lowest = P.Id;
		}
	}
	FollowedId = Next != 0 ? Next : Lowest;
	BrainWaves.Reset();
	if (FollowedId == 0)
	{
		FollowKind = EFollowKind::None;
		FollowNote = TEXT("There is no one left to follow.");
	}
	else
	{
		FollowKind = EFollowKind::Npc;
	}
}

void ALifeSimulation::FollowNextHerd(bool bSameSpecies)
{
	// The biggest group of the next species that is still alive (or of the same species).
	const std::vector<earth::Species>& SpeciesList = Life->GetSpecies();
	const int32 NumSpecies = static_cast<int32>(SpeciesList.size());
	const int32 From = FollowKind == EFollowKind::Animal ? FollowedSpecies : -1;
	for (int32 Step = bSameSpecies ? 0 : 1; Step <= NumSpecies; ++Step)
	{
		const int32 Species = ((From < 0 ? Step - 1 : From + Step) % NumSpecies + NumSpecies) % NumSpecies;
		const earth::AnimalGroup* Biggest = nullptr;
		for (const earth::AnimalGroup& G : Life->Groups())
		{
			if (G.SpeciesIndex == Species && G.Count >= 1.0 && (!Biggest || G.Count > Biggest->Count))
			{
				Biggest = &G;
			}
		}
		if (Biggest)
		{
			FollowKind = EFollowKind::Animal;
			FollowedGroupId = Biggest->Id;
			FollowedSpecies = Species;
			BrainWaves.Reset();
			return;
		}
	}
	FollowKind = EFollowKind::None;
	FollowNote = TEXT("There are no animals left to follow.");
}

void ALifeSimulation::CheckFollowed()
{
	if (FollowKind == EFollowKind::Npc && !GetFollowed())
	{
		FollowNote = FString::Printf(TEXT("NPC %u has died. Following the next person."), FollowedId);
		FollowNextNpc();
	}
	else if (FollowKind == EFollowKind::Animal && !GetFollowedGroup())
	{
		FString Name = TEXT("animal");
		if (FollowedSpecies >= 0 && FollowedSpecies < static_cast<int32>(Life->GetSpecies().size()))
		{
			Name = UTF8_TO_TCHAR(Life->GetSpecies()[FollowedSpecies].Name.c_str());
		}
		FollowNote = FString::Printf(TEXT("The %s group you were following has died out. Following the biggest one left."), *Name);
		FollowNextHerd(true);
	}
}

void ALifeSimulation::SampleHistory()
{
	if (Life->Day() == LastSampledDay)
	{
		return;
	}
	LastSampledDay = Life->Day();

	TArray<float> Wave;
	if (const earth::Npc* P = GetFollowed())
	{
		Wave.Append(P->LastHidden.data(), static_cast<int32>(P->LastHidden.size()));
	}
	else if (const earth::AnimalGroup* G = GetFollowedGroup())
	{
		Wave.Append(G->LastHidden.data(), earth::kAnimalHidden);
	}
	if (Wave.Num() > 0)
	{
		if (BrainWaves.Num() >= kHistorySamples)
		{
			BrainWaves.RemoveAt(0);
		}
		BrainWaves.Add(MoveTemp(Wave));
	}

	if (bShowPlants)
	{
		const earth::PlantPatch Patch = GetPlantPatch();
		if (PlantHistory.Num() >= kHistorySamples)
		{
			PlantHistory.RemoveAt(0);
		}
		PlantHistory.Add(static_cast<float>(Patch.Plants / FMath::Max(1.0, Patch.Peak)));
	}
}

void ALifeSimulation::MoveCamera(float DeltaSeconds)
{
	double Lat = 0.0;
	double Lon = 0.0;
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	if (!Pawn || !FollowedLatLon(Lat, Lon))
	{
		return;
	}
	// Hover above and a little behind, looking down.
	const FVector Target = Globe->SurfacePoint(Lat, Lon);
	const FVector Up = (Target - Globe->GetActorLocation()).GetSafeNormal();
	FVector Side = FVector::CrossProduct(Up, FVector::UpVector).GetSafeNormal();
	if (Side.IsNearlyZero())
	{
		Side = FVector::ForwardVector;
	}
	const FVector Wanted = Target + Up * FollowHeight + Side * FollowHeight * 0.6f;
	const FVector Now = Pawn->GetActorLocation();
	const FVector Next = FVector::Dist(Now, Wanted) > FollowHeight * 20.f ? Wanted : FMath::VInterpTo(Now, Wanted, DeltaSeconds, 3.f);
	Pawn->SetActorLocation(Next);
	Player->SetControlRotation((Target - Next).Rotation());
}

void ALifeSimulation::DrawMarkers()
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const FVector Camera = (Player && Player->PlayerCameraManager) ? Player->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;

	// Animals: bigger animals get bigger balls, and a big herd is drawn up to three times larger.
	// The followed group is drawn in yellow with the followed NPC.
	const std::vector<earth::Species>& SpeciesList = Life->GetSpecies();
	TArray<TArray<FTransform>> PerSpecies;
	PerSpecies.SetNum(AnimalMarkers.Num());
	TArray<FTransform> Followed;
	const uint32 GroupId = FollowKind == EFollowKind::Animal ? FollowedGroupId : 0;
	for (const earth::AnimalGroup& Group : Life->Groups())
	{
		const earth::Species& S = SpeciesList[Group.SpeciesIndex];
		const double Herd = FMath::Clamp(std::cbrt(Group.Count / std::max(1.0, S.GroupSize)), 0.5, 3.0);
		const FVector Ground = Globe->SurfacePoint(Group.Lat, Group.Lon);
		const bool bFollowed = Group.Id == GroupId;
		const double Size = (4.0 + 2.5 * std::log10(std::max(1.0, S.BodyMassKg))) * Herd * (bFollowed ? 1.5 : 1.0) * MarkerScale * Visibility(Camera, Ground) / 100.0;
		const FTransform T(FRotator::ZeroRotator, Globe->SurfacePoint(Group.Lat, Group.Lon, Size * 50.0), FVector(Size));
		(bFollowed ? Followed : PerSpecies[Group.SpeciesIndex]).Add(T);
	}
	for (int32 s = 0; s < AnimalMarkers.Num(); ++s)
	{
		AnimalMarkers[s]->ClearInstances();
		AnimalMarkers[s]->AddInstances(PerSpecies[s], false, true);
	}

	// People: small pink balls, children smaller; the one being followed is yellow and larger.
	TArray<FTransform> Everyone;
	const uint32 NpcId = FollowKind == EFollowKind::Npc ? FollowedId : 0;
	for (const earth::Npc& P : Folk->All())
	{
		const FVector Ground = Globe->SurfacePoint(P.Lat, P.Lon);
		const bool bFollowed = P.Id == NpcId;
		const double Size = (P.IsChild() ? 3.0 : 5.0) * (bFollowed ? 1.8 : 1.0) * MarkerScale * Visibility(Camera, Ground) / 100.0;
		const FTransform T(FRotator::ZeroRotator, Globe->SurfacePoint(P.Lat, P.Lon, Size * 50.0), FVector(Size));
		(bFollowed ? Followed : Everyone).Add(T);
	}
	NpcMarkers->ClearInstances();
	NpcMarkers->AddInstances(Everyone, false, true);
	FollowMarker->ClearInstances();
	FollowMarker->AddInstances(Followed, false, true);
}

void ALifeSimulation::ShowStatus()
{
	if (!GEngine)
	{
		return;
	}

	const double YearsAgo = kStartYearsAgo - Life->Years();
	static const TCHAR* Seasons[] = {TEXT("winter"), TEXT("spring"), TEXT("summer"), TEXT("autumn")};
	// Northern seasons, starting from early January.
	const TCHAR* Season = Seasons[((Life->DayOfYear() + 10) / 91) % 4];
	const double YearsPerSecond = MeasuredDaysPerSecond / earth::Ecosystem::kDaysPerYear;
	const FString Running = YearsPerSecond >= 1.0 ? FString::Printf(TEXT("%.1f years"), YearsPerSecond) : Commas(MeasuredDaysPerSecond) + TEXT(" days");

	FString Text = FString::Printf(
		TEXT("%s years ago, northern %s%s\nSpeed: %s days per second, running at %s per second   ([ slower, ] faster, P pause, 1-4 jump 10/100/1000/10000 years, R new world)\n")
		TEXT("Plants: %.0f%% of their peak on land.   Animals: %s in %d groups, %d of %d species alive"),
		*Commas(YearsAgo), Season, bPaused ? TEXT(" (paused)") : TEXT(""), *Commas(DaysPerSecond), *Running,
		Life->AverageLandCover() * 100.0, *Commas(Life->TotalAnimals()), static_cast<int32>(Life->Groups().size()), Life->SpeciesAliveCount(),
		static_cast<int32>(Life->GetSpecies().size()));

	Text += FString::Printf(TEXT("\nPeople: %d in %d bands, generation %d, %lld born, %lld died, %lld animals hunted   (F follow a person, H follow animals, G stop, B brain, V plants)"),
		Folk->Alive(), Folk->BandCount(), Folk->MaxGeneration(), Folk->TotalBirths(), Folk->TotalDeaths(), Folk->HuntsWon());

	if (!JumpSummary.IsEmpty() && FPlatformTime::Seconds() < JumpSummaryUntil)
	{
		Text += TEXT("\n") + JumpSummary;
	}
	else
	{
		// The latest firsts for the people, then the latest animal news.
		const std::vector<earth::EcoEvent>& Firsts = Folk->Firsts();
		for (size_t i = Firsts.size() > 5 ? Firsts.size() - 5 : 0; i < Firsts.size(); ++i)
		{
			Text += FString::Printf(TEXT("\n  %s years ago: %s"), *Commas(kStartYearsAgo - Firsts[i].Year), UTF8_TO_TCHAR(Firsts[i].Text.c_str()));
		}
		const std::vector<earth::EcoEvent>& Events = Life->Events();
		for (size_t i = Events.size() > 2 ? Events.size() - 2 : 0; i < Events.size(); ++i)
		{
			Text += FString::Printf(TEXT("\n  %s years ago: %s"), *Commas(kStartYearsAgo - Events[i].Year), UTF8_TO_TCHAR(Events[i].Text.c_str()));
		}
	}
	if (!FollowNote.IsEmpty())
	{
		Text += TEXT("\n") + FollowNote;
	}

	GEngine->AddOnScreenDebugMessage(kStatusKey, 1.f, FColor::White, Text);
}

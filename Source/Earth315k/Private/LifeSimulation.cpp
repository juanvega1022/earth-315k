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
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr double kStartYearsAgo = 315000.0;
	constexpr double kStepBudgetSeconds = 0.012;
	constexpr float kDrawSeconds = 0.1f;
	const FLinearColor kNpcColor(1.0f, 0.15f, 0.75f);
	const FLinearColor kFollowColor(1.0f, 0.95f, 0.2f);

	// Fixed keys so the on-screen lines replace themselves instead of scrolling.
	constexpr int32 kStatusKey = 315000;

	// Markers grow with distance from the camera so they stay visible from far away.
	double Visibility(const FVector& Camera, const FVector& Where)
	{
		return FMath::Clamp(FVector::Dist(Camera, Where) / 400.0, 1.0, 200.0);
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
}

void ALifeSimulation::StartWorld()
{
	earth::EcosystemSettings Settings;
	Settings.SeaLevelMeters = Globe->SeaLevelMeters;
	Folk.Reset();
	Life = MakeUnique<earth::Ecosystem>(&Globe->GetGlobalGrid(), earth::DefaultSpecies(), static_cast<uint64_t>(Seed), Settings);
	Folk = MakeUnique<earth::People>(Life.Get(), static_cast<uint64_t>(Seed));
	FollowedId = 0;
	FollowNote.Reset();
	PendingDays = 0.0;
	Globe->TintPlants([this](double Lat, double Lon) { return static_cast<float>(Life->PlantCover(Lat, Lon)); });
	DrawMarkers();
}

void ALifeSimulation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Life)
	{
		return;
	}

	HandleKeys();
	if (!bPaused)
	{
		StepSimulation(DeltaSeconds);
	}
	MoveCamera(DeltaSeconds);

	SinceDraw += DeltaSeconds;
	if (SinceDraw >= kDrawSeconds)
	{
		SinceDraw = 0.f;
		DrawMarkers();
	}

	SinceRepaint += DeltaSeconds;
	if (SinceRepaint >= PlantRepaintSeconds)
	{
		SinceRepaint = 0.f;
		Globe->TintPlants([this](double Lat, double Lon) { return static_cast<float>(Life->PlantCover(Lat, Lon)); });
	}

	SinceMeasure += DeltaSeconds;
	if (SinceMeasure >= 1.f)
	{
		MeasuredDaysPerSecond = DaysDoneThisSecond / SinceMeasure;
		DaysDoneThisSecond = 0.0;
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
		DaysPerSecond = FMath::Min(DaysPerSecond * 2.f, 36500.f);
	}
	if (Player->WasInputKeyJustPressed(EKeys::LeftBracket))
	{
		DaysPerSecond = FMath::Max(DaysPerSecond * 0.5f, 0.25f);
	}
	if (Player->WasInputKeyJustPressed(EKeys::P))
	{
		bPaused = !bPaused;
	}
	if (Player->WasInputKeyJustPressed(EKeys::F))
	{
		FollowNext();
	}
	if (Player->WasInputKeyJustPressed(EKeys::G))
	{
		FollowedId = 0;
		FollowNote.Reset();
	}
	if (Player->WasInputKeyJustPressed(EKeys::B))
	{
		bShowBrain = !bShowBrain;
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

void ALifeSimulation::StepSimulation(float DeltaSeconds)
{
	PendingDays += DeltaSeconds * DaysPerSecond;

	// Never stall the frame: whatever doesn't fit in the budget is dropped, so very high
	// speeds simply run as fast as the computer allows.
	const double Start = FPlatformTime::Seconds();
	while (PendingDays >= 1.0 && FPlatformTime::Seconds() - Start < kStepBudgetSeconds)
	{
		Life->StepDay();
		Folk->StepDay();
		PendingDays -= 1.0;
		DaysDoneThisSecond += 1.0;
	}
	PendingDays = FMath::Min(PendingDays, 1.0);

	if (FollowedId != 0 && !Folk->Find(FollowedId))
	{
		FollowNote = FString::Printf(TEXT("NPC %u has died."), FollowedId);
		FollowNext();
	}
}

const earth::Npc* ALifeSimulation::GetFollowed() const
{
	return (Folk && FollowedId != 0) ? Folk->Find(FollowedId) : nullptr;
}

void ALifeSimulation::FollowNext()
{
	// Adults only, in order of id, wrapping around: the oldest lineages first.
	const std::vector<earth::Npc>& All = Folk->All();
	uint32 Next = 0;
	uint32 Lowest = 0;
	for (const earth::Npc& P : All)
	{
		if (P.IsChild())
		{
			continue;
		}
		if (P.Id > FollowedId && (Next == 0 || P.Id < Next))
		{
			Next = P.Id;
		}
		if (Lowest == 0 || P.Id < Lowest)
		{
			Lowest = P.Id;
		}
	}
	FollowedId = Next != 0 ? Next : Lowest;
	if (FollowedId == 0)
	{
		FollowNote = TEXT("There is no one left to follow.");
	}
}

void ALifeSimulation::MoveCamera(float DeltaSeconds)
{
	const earth::Npc* P = GetFollowed();
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	if (!P || !Pawn)
	{
		return;
	}
	// Hover above and a little behind the NPC, looking down at it.
	const FVector Target = Globe->SurfacePoint(P->Lat, P->Lon);
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
	const std::vector<earth::Species>& SpeciesList = Life->GetSpecies();
	TArray<TArray<FTransform>> PerSpecies;
	PerSpecies.SetNum(AnimalMarkers.Num());
	for (const earth::AnimalGroup& Group : Life->Groups())
	{
		const earth::Species& S = SpeciesList[Group.SpeciesIndex];
		const double Herd = FMath::Clamp(std::cbrt(Group.Count / std::max(1.0, S.GroupSize)), 0.5, 3.0);
		const FVector Ground = Globe->SurfacePoint(Group.Lat, Group.Lon);
		const double Size = (4.0 + 2.5 * std::log10(std::max(1.0, S.BodyMassKg))) * Herd * MarkerScale * Visibility(Camera, Ground) / 100.0;
		const FVector Where = Globe->SurfacePoint(Group.Lat, Group.Lon, Size * 50.0);
		PerSpecies[Group.SpeciesIndex].Add(FTransform(FRotator::ZeroRotator, Where, FVector(Size)));
	}
	for (int32 s = 0; s < AnimalMarkers.Num(); ++s)
	{
		AnimalMarkers[s]->ClearInstances();
		AnimalMarkers[s]->AddInstances(PerSpecies[s], false, true);
	}

	// People: small pink balls, children smaller; the one being followed is yellow and larger.
	TArray<FTransform> Everyone;
	TArray<FTransform> Followed;
	for (const earth::Npc& P : Folk->All())
	{
		const FVector Ground = Globe->SurfacePoint(P.Lat, P.Lon);
		const bool bFollowed = P.Id == FollowedId;
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

	FString Text = FString::Printf(
		TEXT("%s years ago, northern %s%s\nSpeed: %s days per second, running at %s   ([ slower, ] faster, P pause, R new world)\n")
		TEXT("Plants: %.0f%% of their peak on land.   Animals: %s in %d groups, %d of %d species alive"),
		*FText::AsNumber(FMath::RoundToInt64(YearsAgo)).ToString(), Season, bPaused ? TEXT(" (paused)") : TEXT(""),
		*FText::AsNumber(DaysPerSecond).ToString(), *FText::AsNumber(FMath::RoundToInt64(MeasuredDaysPerSecond)).ToString(),
		Life->AverageLandCover() * 100.0, *FText::AsNumber(FMath::RoundToInt64(Life->TotalAnimals())).ToString(),
		static_cast<int32>(Life->Groups().size()), Life->SpeciesAliveCount(), static_cast<int32>(Life->GetSpecies().size()));

	Text += FString::Printf(TEXT("\nPeople: %d in %d bands, generation %d, %lld born, %lld died, %lld animals hunted   (F follow, G stop, B brain)"),
		Folk->Alive(), Folk->BandCount(), Folk->MaxGeneration(), Folk->TotalBirths(), Folk->TotalDeaths(), Folk->HuntsWon());

	// The latest firsts for the people, then the latest animal news.
	const std::vector<earth::EcoEvent>& Firsts = Folk->Firsts();
	for (size_t i = Firsts.size() > 5 ? Firsts.size() - 5 : 0; i < Firsts.size(); ++i)
	{
		Text += FString::Printf(TEXT("\n  %s years ago: %s"),
			*FText::AsNumber(FMath::RoundToInt64(kStartYearsAgo - Firsts[i].Year)).ToString(), UTF8_TO_TCHAR(Firsts[i].Text.c_str()));
	}
	const std::vector<earth::EcoEvent>& Events = Life->Events();
	for (size_t i = Events.size() > 2 ? Events.size() - 2 : 0; i < Events.size(); ++i)
	{
		Text += FString::Printf(TEXT("\n  %s years ago: %s"),
			*FText::AsNumber(FMath::RoundToInt64(kStartYearsAgo - Events[i].Year)).ToString(), UTF8_TO_TCHAR(Events[i].Text.c_str()));
	}
	if (!FollowNote.IsEmpty())
	{
		Text += TEXT("\n") + FollowNote;
	}

	GEngine->AddOnScreenDebugMessage(kStatusKey, 1.f, FColor::White, Text);
}

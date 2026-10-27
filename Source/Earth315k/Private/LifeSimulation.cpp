#include "LifeSimulation.h"

#include "PlanetGlobe.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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

	// Fixed keys so the on-screen lines replace themselves instead of scrolling.
	constexpr int32 kStatusKey = 315000;
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

	earth::EcosystemSettings Settings;
	Settings.SeaLevelMeters = Globe->SeaLevelMeters;
	Life = MakeUnique<earth::Ecosystem>(&Globe->GetGlobalGrid(), earth::DefaultSpecies(), static_cast<uint64_t>(Seed), Settings);

	for (const earth::Species& S : Life->GetSpecies())
	{
		UInstancedStaticMeshComponent* Ism = NewObject<UInstancedStaticMeshComponent>(this);
		Ism->SetStaticMesh(MarkerMesh);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCastShadow(false);
		Ism->SetAbsolute(true, true, true);
		if (MarkerMaterial)
		{
			UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(MarkerMaterial, this);
			Mat->SetVectorParameterValue(TEXT("Color"), FLinearColor(S.Color[0], S.Color[1], S.Color[2]));
			Ism->SetMaterial(0, Mat);
		}
		Ism->SetupAttachment(RootComponent);
		Ism->RegisterComponent();
		Markers.Add(Ism);
	}

	Globe->TintPlants([this](double Lat, double Lon) { return static_cast<float>(Life->PlantCover(Lat, Lon)); });
	DrawAnimals();
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

	SinceDraw += DeltaSeconds;
	if (SinceDraw >= kDrawSeconds)
	{
		SinceDraw = 0.f;
		DrawAnimals();
	}

	SinceRepaint += DeltaSeconds;
	if (SinceRepaint >= PlantRepaintSeconds)
	{
		SinceRepaint = 0.f;
		Globe->TintPlants([this](double Lat, double Lon) { return static_cast<float>(Life->PlantCover(Lat, Lon)); });
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
		PendingDays -= 1.0;
	}
	PendingDays = FMath::Min(PendingDays, 1.0);
}

void ALifeSimulation::DrawAnimals()
{
	for (UInstancedStaticMeshComponent* Ism : Markers)
	{
		Ism->ClearInstances();
	}

	const std::vector<earth::Species>& SpeciesList = Life->GetSpecies();
	TArray<TArray<FTransform>> PerSpecies;
	PerSpecies.SetNum(Markers.Num());
	for (const earth::AnimalGroup& Group : Life->Groups())
	{
		const earth::Species& S = SpeciesList[Group.SpeciesIndex];
		// Bigger animals get bigger balls; a big herd is drawn up to three times larger.
		const double Herd = FMath::Clamp(std::cbrt(Group.Count / std::max(1.0, S.GroupSize)), 0.5, 3.0);
		const double Size = (40.0 + 25.0 * std::log10(std::max(1.0, S.BodyMassKg))) * Herd * MarkerScale / 100.0;
		const FVector Where = Globe->SurfacePoint(Group.Lat, Group.Lon, Size * 50.0);
		PerSpecies[Group.SpeciesIndex].Add(FTransform(FRotator::ZeroRotator, Where, FVector(Size)));
	}
	for (int32 s = 0; s < Markers.Num(); ++s)
	{
		Markers[s]->AddInstances(PerSpecies[s], false, true);
	}
}

void ALifeSimulation::ShowStatus() const
{
	if (!GEngine)
	{
		return;
	}

	const double YearsAgo = kStartYearsAgo - Life->Years();
	static const TCHAR* Seasons[] = {TEXT("winter"), TEXT("spring"), TEXT("summer"), TEXT("autumn")};
	// Northern seasons, starting from early January.
	const TCHAR* Season = Seasons[((Life->DayOfYear() + 10) / 91) % 4];

	const std::vector<earth::Species>& SpeciesList = Life->GetSpecies();
	int32 Top = -1;
	double TopCount = 0.0;
	for (int32 s = 0; s < static_cast<int32>(SpeciesList.size()); ++s)
	{
		const double Count = Life->CountOf(s);
		if (Count > TopCount)
		{
			TopCount = Count;
			Top = s;
		}
	}

	FString Text = FString::Printf(
		TEXT("%s years ago, northern %s%s\nSpeed: %s days per second   ([ slower, ] faster, P pause)\n")
		TEXT("Plants: %.0f%% of their peak on land\nAnimals: %s in %d groups, %d of %d species alive"),
		*FText::AsNumber(FMath::RoundToInt64(YearsAgo)).ToString(), Season, bPaused ? TEXT(" (paused)") : TEXT(""),
		*FText::AsNumber(DaysPerSecond).ToString(),
		Life->AverageLandCover() * 100.0,
		*FText::AsNumber(FMath::RoundToInt64(Life->TotalAnimals())).ToString(),
		static_cast<int32>(Life->Groups().size()), Life->SpeciesAliveCount(), static_cast<int32>(SpeciesList.size()));
	if (Top >= 0)
	{
		Text += FString::Printf(TEXT("\nMost numerous: %s (%s)"), UTF8_TO_TCHAR(SpeciesList[Top].Name.c_str()),
			*FText::AsNumber(FMath::RoundToInt64(TopCount)).ToString());
	}

	const std::vector<earth::EcoEvent>& Events = Life->Events();
	const size_t First = Events.size() > 3 ? Events.size() - 3 : 0;
	for (size_t i = First; i < Events.size(); ++i)
	{
		Text += FString::Printf(TEXT("\n  %s years ago: %s"),
			*FText::AsNumber(FMath::RoundToInt64(kStartYearsAgo - Events[i].Year)).ToString(),
			UTF8_TO_TCHAR(Events[i].Text.c_str()));
	}

	GEngine->AddOnScreenDebugMessage(kStatusKey, 1.f, FColor::White, Text);
}

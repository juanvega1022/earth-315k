#include "PlanetGlobe.h"

#include "PlanetCore/Sphere.h"
#include "PlanetCore/Terrain.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanet, Log, All);

namespace
{
	// Core maths is right-handed (Y = east); Unreal is left-handed, so flip Y.
	FVector ToUnreal(const earth::Vec3& V)
	{
		return FVector(V.X, -V.Y, V.Z);
	}

	earth::Vec3 FromUnreal(const FVector& V)
	{
		return earth::Vec3{V.X, -V.Y, V.Z};
	}

	struct FMeshData
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
		TArray<bool> InRegion;

		// Adds the triangle facing both ways so it renders regardless of winding convention.
		void AddTriangle(int32 A, int32 B, int32 C)
		{
			Triangles.Append({A, B, C, A, C, B});
		}

		void ComputeNormals()
		{
			Normals.Init(FVector::ZeroVector, Vertices.Num());
			for (int32 i = 0; i + 2 < Triangles.Num(); i += 6)
			{
				const int32 A = Triangles[i];
				const int32 B = Triangles[i + 1];
				const int32 C = Triangles[i + 2];
				FVector N = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]);
				// Point away from the planet centre.
				if (FVector::DotProduct(N, Vertices[A]) < 0.0)
				{
					N = -N;
				}
				Normals[A] += N;
				Normals[B] += N;
				Normals[C] += N;
			}
			for (int32 i = 0; i < Normals.Num(); ++i)
			{
				Normals[i] = Normals[i].IsNearlyZero() ? Vertices[i].GetSafeNormal() : Normals[i].GetSafeNormal();
			}
		}
	};
}

APlanetGlobe::APlanetGlobe()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PlanetMesh"));
	Mesh->bUseAsyncCooking = true;
	RootComponent = Mesh;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
		TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
	if (VertexColorMaterial.Succeeded())
	{
		Material = VertexColorMaterial.Object;
	}
}

void APlanetGlobe::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const UWorld* World = GetWorld();
	if (bRebuildInEditor || (World && World->IsGameWorld()))
	{
		BuildPlanet();
	}
}

bool APlanetGlobe::LoadGrid(const FString& Name, earth::ElevationGrid& OutGrid) const
{
	OutGrid = earth::ElevationGrid();

	const FString Dir = FPaths::ProjectContentDir() / TEXT("Data");
	const FString JsonPath = Dir / (Name + TEXT(".json"));
	const FString BinPath = Dir / (Name + TEXT(".bin"));

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Meta;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Meta) || !Meta.IsValid())
	{
		UE_LOG(LogPlanet, Warning, TEXT("Could not parse %s"), *JsonPath);
		return false;
	}

	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *BinPath))
	{
		UE_LOG(LogPlanet, Warning, TEXT("Missing %s"), *BinPath);
		return false;
	}

	OutGrid.Width = Meta->GetIntegerField(TEXT("width"));
	OutGrid.Height = Meta->GetIntegerField(TEXT("height"));
	OutGrid.North = Meta->GetNumberField(TEXT("north"));
	OutGrid.South = Meta->GetNumberField(TEXT("south"));
	OutGrid.West = Meta->GetNumberField(TEXT("west"));
	OutGrid.East = Meta->GetNumberField(TEXT("east"));

	const int64 Expected = static_cast<int64>(OutGrid.Width) * OutGrid.Height * sizeof(int16_t);
	if (OutGrid.Width <= 1 || OutGrid.Height <= 1 || Bytes.Num() != Expected)
	{
		UE_LOG(LogPlanet, Warning, TEXT("%s has %d bytes, expected %lld"), *BinPath, Bytes.Num(), Expected);
		OutGrid = earth::ElevationGrid();
		return false;
	}

	OutGrid.Heights.resize(static_cast<size_t>(OutGrid.Width) * OutGrid.Height);
	FMemory::Memcpy(OutGrid.Heights.data(), Bytes.GetData(), Bytes.Num());
	return true;
}

void APlanetGlobe::BuildPlanet()
{
	if (!Mesh)
	{
		return;
	}
	Mesh->ClearAllMeshSections();

	const bool bHaveGlobal = LoadGrid(TEXT("global"), GlobalGrid);
	const bool bHaveRegion = bBuildRegion && LoadGrid(TEXT("region"), RegionGrid);
	if (!bHaveGlobal)
	{
		const FString Msg = TEXT("Planet: no elevation data found in Content/Data. Run tools/prepare_elevation.py (see README). Showing a smooth sphere.");
		UE_LOG(LogPlanet, Warning, TEXT("%s"), *Msg);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Yellow, Msg);
		}
	}

	earth::TerrainSettings Settings;
	Settings.SeaLevelMeters = SeaLevelMeters;
	Settings.DetailAmplitudeMeters = DetailAmplitudeMeters;
	Settings.DetailScaleKm = DetailScaleKm;
	const earth::Terrain PlanetTerrain(bHaveGlobal ? &GlobalGrid : nullptr, bHaveRegion ? &RegionGrid : nullptr,
		static_cast<uint64>(Seed), Settings);

	// Height in metres -> distance from the centre in Unreal units. The sea surface sits at GlobeRadius.
	const double MetresToUnits = GlobeRadius * ElevationExaggeration / earth::kEarthRadiusMeters;
	auto AddVertex = [&](FMeshData& Data, const earth::Vec3& Dir, bool bDetail, double Lift)
	{
		const earth::LatLon P = earth::DirectionToLatLon(Dir);
		const double H = PlanetTerrain.Height(Dir, bDetail);
		const double Above = FMath::Max(H, static_cast<double>(SeaLevelMeters)) - SeaLevelMeters;
		const double Radius = GlobeRadius + Above * MetresToUnits + Lift;
		const earth::Color C = PlanetTerrain.ColorFor(H, P.Lat);

		Data.Vertices.Add(ToUnreal(Dir) * Radius);
		Data.UVs.Add(FVector2D((P.Lon + 180.0) / 360.0, (90.0 - P.Lat) / 180.0));
		Data.Colors.Add(FLinearColor(static_cast<float>(C.R), static_cast<float>(C.G), static_cast<float>(C.B)));
		Data.InRegion.Add(bHaveRegion && RegionGrid.Contains(P.Lat, P.Lon));
	};

	// Six cube faces at globe resolution, skipping triangles the region patch covers.
	const int32 N = FaceResolution;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		FMeshData Data;
		for (int32 j = 0; j <= N; ++j)
		{
			for (int32 i = 0; i <= N; ++i)
			{
				const double U = -1.0 + 2.0 * i / N;
				const double V = -1.0 + 2.0 * j / N;
				AddVertex(Data, earth::CubeFaceToDirection(Face, U, V), false, 0.0);
			}
		}
		for (int32 j = 0; j < N; ++j)
		{
			for (int32 i = 0; i < N; ++i)
			{
				const int32 A = j * (N + 1) + i;
				const int32 B = A + 1;
				const int32 C = A + (N + 1);
				const int32 D = C + 1;
				if (!(Data.InRegion[A] && Data.InRegion[B] && Data.InRegion[D]))
				{
					Data.AddTriangle(A, B, D);
				}
				if (!(Data.InRegion[A] && Data.InRegion[D] && Data.InRegion[C]))
				{
					Data.AddTriangle(A, D, C);
				}
			}
		}
		Data.ComputeNormals();
		Mesh->CreateMeshSection_LinearColor(Face, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, Data.Colors,
			TArray<FProcMeshTangent>(), false);
		Mesh->SetMaterial(Face, Material);
	}

	// Detailed region patch on a latitude/longitude grid, with seeded procedural detail.
	if (bHaveRegion)
	{
		const double LatSpan = RegionGrid.North - RegionGrid.South;
		const double LonSpan = RegionGrid.East - RegionGrid.West;
		const double MaxSpan = FMath::Max(LatSpan, LonSpan);
		const int32 Cols = FMath::Max(2, FMath::RoundToInt(RegionResolution * LonSpan / MaxSpan));
		const int32 Rows = FMath::Max(2, FMath::RoundToInt(RegionResolution * LatSpan / MaxSpan));
		// Sit a hair above the globe so the overlap at the patch edges doesn't flicker.
		const double Lift = GlobeRadius * 0.0003;

		FMeshData Data;
		for (int32 r = 0; r <= Rows; ++r)
		{
			for (int32 c = 0; c <= Cols; ++c)
			{
				const double Lat = RegionGrid.North - LatSpan * r / Rows;
				const double Lon = RegionGrid.West + LonSpan * c / Cols;
				AddVertex(Data, earth::LatLonToDirection(Lat, Lon), true, Lift);
			}
		}
		for (int32 r = 0; r < Rows; ++r)
		{
			for (int32 c = 0; c < Cols; ++c)
			{
				const int32 A = r * (Cols + 1) + c;
				const int32 B = A + 1;
				const int32 C = A + (Cols + 1);
				const int32 D = C + 1;
				Data.AddTriangle(A, B, D);
				Data.AddTriangle(A, D, C);
			}
		}
		Data.ComputeNormals();
		Mesh->CreateMeshSection_LinearColor(6, Data.Vertices, Data.Triangles, Data.Normals, Data.UVs, Data.Colors,
			TArray<FProcMeshTangent>(), false);
		Mesh->SetMaterial(6, Material);
	}

	UE_LOG(LogPlanet, Log, TEXT("Planet built: global data %s, region data %s, seed %lld"),
		bHaveGlobal ? TEXT("yes") : TEXT("no"), bHaveRegion ? TEXT("yes") : TEXT("no"), Seed);
}

FVector2D APlanetGlobe::WorldToLatLon(const FVector& WorldLocation) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const earth::LatLon P = earth::DirectionToLatLon(FromUnreal(Local));
	return FVector2D(P.Lat, P.Lon);
}

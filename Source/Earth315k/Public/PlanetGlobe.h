#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "PlanetCore/ElevationGrid.h"

#include "PlanetGlobe.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;

/**
 * The planet. Builds a 3D globe from real elevation data (Content/Data/global.*),
 * plus a higher-detail patch for one region (Content/Data/region.*) where seeded
 * procedural detail fills in what the data is too coarse to show.
 */
UCLASS()
class EARTH315K_API APlanetGlobe : public AActor
{
	GENERATED_BODY()

public:
	APlanetGlobe();

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Rebuild the planet mesh from the data files and current settings. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Planet")
	void BuildPlanet();

	/** Seed for procedural detail. The same seed always produces the same planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	int64 Seed = 315000;

	/** Radius of the globe in Unreal units (centimetres). Not real scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "100"))
	float GlobeRadius = 20000.f;

	/** Vertical exaggeration so mountains are visible at globe scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "0"))
	float ElevationExaggeration = 25.f;

	/** Sea level relative to today, in metres. Change it to see ice-age coastlines (around -120). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float SeaLevelMeters = -30.f;

	/** Grid cells along each edge of the six cube faces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "8", ClampMax = "512"))
	int32 FaceResolution = 128;

	/** Build the detailed region patch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Region")
	bool bBuildRegion = true;

	/** Grid cells across the region's longest side. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Region", meta = (ClampMin = "16", ClampMax = "2048"))
	int32 RegionResolution = 512;

	/** Largest procedural detail height in metres, added only inside the region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Region", meta = (ClampMin = "0"))
	float DetailAmplitudeMeters = 120.f;

	/** Size of the largest procedural detail features in kilometres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Region", meta = (ClampMin = "1"))
	float DetailScaleKm = 40.f;

	/** Rebuild automatically when a setting changes in the editor. */
	UPROPERTY(EditAnywhere, Category = "Planet")
	bool bRebuildInEditor = true;

	/** Material that shows vertex colours. Defaults to the engine's vertex colour material. */
	UPROPERTY(EditAnywhere, Category = "Planet")
	TObjectPtr<UMaterialInterface> Material;

	/** Latitude/longitude of the point under a world location, for debugging and future tools. */
	UFUNCTION(BlueprintCallable, Category = "Planet")
	FVector2D WorldToLatLon(const FVector& WorldLocation) const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Planet")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	earth::ElevationGrid GlobalGrid;
	earth::ElevationGrid RegionGrid;

	bool LoadGrid(const FString& Name, earth::ElevationGrid& OutGrid) const;
};

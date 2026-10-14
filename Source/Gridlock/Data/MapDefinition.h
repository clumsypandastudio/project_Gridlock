#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Model/GridlockTypes.h"
#include "MapDefinition.generated.h"

USTRUCT(BlueprintType)
struct FCityDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FText Name;

	/** Board position in map units (the v0.1 values match the Map tab drawing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	bool bIsStart = false;

	/** Visual only: river crossings are drawn differently. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	bool bIsBridgeTown = false;
};

USTRUCT(BlueprintType)
struct FRouteDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FName CityA;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FName CityB;

	/** Length in pins, 1 to 9: the tickets it takes to claim (v0.3), or its cash cost (v0.1/v0.2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (ClampMin = "1", ClampMax = "9"))
	int32 Length = 1;

	/** Tickets of this color pay for it (Rules v0.3). Gray = any one color. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	EGridlockColor Color = EGridlockColor::Gray;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	bool bCurved = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route", meta = (EditCondition = "bCurved"))
	FVector2D CurveControl = FVector2D::ZeroVector;

	/** Double routes (map v0.9): the other track between the same two cities. One player can't claim both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	FName TwinId;

	/** How far (map units) this track is drawn to the side of the straight line between its cities, for double routes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route")
	double SideOffset = 0.0;

	bool Touches(FName City) const { return CityA == City || CityB == City; }
	FName OtherEnd(FName City) const { return CityA == City ? CityB : CityA; }
};

/** The board: cities and the routes between them. One asset per map version. */
UCLASS(BlueprintType)
class GRIDLOCK_API UMapDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FCityDef> Cities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	TArray<FRouteDef> Routes;

	const FCityDef* FindCity(FName Id) const;
	const FRouteDef* FindRoute(FName Id) const;

	/** Every problem found in the data; empty when the map is valid. */
	TArray<FString> GetProblems() const;

	/** Replaces the contents with map v0.1 (15 cities, 30 routes of 1-3 pins). */
	UFUNCTION(CallInEditor, Category = "Map")
	void FillWithMapV01();

	/** Map v0.3: the same cities, routes and colors, with lengths of 1-9 pins (136 in all). */
	UFUNCTION(CallInEditor, Category = "Map")
	void FillWithMapV03();

	/** Map v0.5: map v0.3 recolored for the 8 Ticket to Ride colors, about 15 pins of each (west and east halves differ). */
	UFUNCTION(CallInEditor, Category = "Map")
	void FillWithMapV05();

	/** Map v0.9: map v0.5 with 8 double routes (out of each start city, and into each bridge town), each second track in its own color. */
	UFUNCTION(CallInEditor, Category = "Map")
	void FillWithMapV09();

	/** Logs every problem from GetProblems. */
	UFUNCTION(CallInEditor, Category = "Map")
	void ValidateMap();
};

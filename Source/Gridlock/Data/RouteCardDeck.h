#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Model/GridlockTypes.h"
#include "RouteCardDeck.generated.h"

USTRUCT(BlueprintType)
struct FRouteCardDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Card")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Card")
	FName CityA;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Card")
	FName CityB;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Card")
	int32 Points = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Card")
	ERouteCardType Type = ERouteCardType::Short;
};

/** The Route card (objective) deck. */
UCLASS(BlueprintType)
class GRIDLOCK_API URouteCardDeck : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Deck")
	TArray<FRouteCardDef> Cards;

	const FRouteCardDef* FindCard(FName Id) const;

	int32 GetTotalPoints() const;

	/** Replaces the contents with deck v0.1 (20 cards, 132 points). */
	UFUNCTION(CallInEditor, Category = "Deck")
	void FillWithDeckV01();

	/** Deck v0.3: the same 20 city pairs, each worth the fewest pins that join its cities on map v0.3. */
	UFUNCTION(CallInEditor, Category = "Deck")
	void FillWithDeckV03();

	/** The v0.1 city pairs, scored for a given map: points = fewest pins between the two cities. */
	void FillWithDeckForMap(const class UMapDefinition& Map);
};

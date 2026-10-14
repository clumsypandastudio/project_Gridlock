#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Model/GridlockTypes.h"
#include "ScenarioDefinition.generated.h"

class UAIPersonality;
class UMapDefinition;
class URouteCardDeck;
class UTuningData;

USTRUCT(BlueprintType)
struct FPresetRoute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FName RouteId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	int32 Owner = 0;
};

USTRUCT(BlueprintType)
struct FPresetProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	FName RouteId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	int32 Owner = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Preset")
	EPropertyTier Tier = EPropertyTier::Lot;
};

/** One "level": the same map with its own starting position, AI and goal. Player 0 is the human. */
UCLASS(BlueprintType)
class GRIDLOCK_API UScenarioDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario", meta = (MultiLine = "true"))
	FText GoalText;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TObjectPtr<UMapDefinition> Map;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TObjectPtr<URouteCardDeck> Deck;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TObjectPtr<UTuningData> Tuning;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scenario")
	TObjectPtr<UAIPersonality> AIPersonality;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starting State")
	TArray<FPresetRoute> PresetRoutes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starting State")
	TArray<FPresetProperty> PresetProperties;

	/** Empty = deal from the shuffled deck as usual. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starting State")
	TArray<FName> HumanStartingHand;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Starting State")
	TArray<FName> AIStartingHand;

	/** 0 = use the tuning asset's CardsToWin. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Goal")
	int32 OverrideCardsToWin = 0;
};

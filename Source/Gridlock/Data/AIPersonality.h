#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AIPersonality.generated.h"

UENUM(BlueprintType)
enum class EAIStyle : uint8
{
	/** Races its own Route cards. */
	Builder,
	/** Blocks and severs your routes. */
	Saboteur,
	/** Collects tolls and builds Landmarks. */
	Tycoon
};

UENUM(BlueprintType)
enum class EAIDifficulty : uint8
{
	/** Misjudges every choice by a random amount, then picks among its 3 best. */
	Easy,
	/** Always plays its best move. */
	Normal,
	/** Best move, after simulating your best reply to each of its top choices. */
	Hard
};

/** Weights for the AI's utility scoring (see the AI section of the UE Project tab). */
UCLASS(BlueprintType)
class GRIDLOCK_API UAIPersonality : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** How close the AI's unfinished Route cards are to done. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weights")
	float ProgressWeight = 1.0f;

	/** How close the opponent's likely cards are to done (subtracted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weights")
	float ThreatWeight = 0.4f;

	/** Cash, property value, and tolls earned minus tolls paid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weights")
	float EconomyWeight = 0.4f;

	/** The AI's own routes inside the opponent's range (subtracted). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weights")
	float ExposureWeight = 0.2f;

	/** Points and completed cards, against the opponent's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weights")
	float PointsWeight = 1.0f;

	/** 1 = always the best action. Easy uses 3. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "1"))
	int32 TopN = 1;

	/** Random error added to every score, including card keeps, so the AI misjudges. Easy uses 1.5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty", meta = (ClampMin = "0"))
	float ScoreNoise = 0.0f;

	/** Hard: simulate the opponent's best reply before choosing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Difficulty")
	bool bLookahead = false;

	/** Sets the weights and difficulty fields to a preset. */
	void ApplyPreset(EAIStyle Style, EAIDifficulty Difficulty);

	UFUNCTION(CallInEditor, Category = "Presets")
	void MakeBuilder() { ApplyPreset(EAIStyle::Builder, EAIDifficulty::Normal); }

	UFUNCTION(CallInEditor, Category = "Presets")
	void MakeSaboteur() { ApplyPreset(EAIStyle::Saboteur, EAIDifficulty::Normal); }

	UFUNCTION(CallInEditor, Category = "Presets")
	void MakeTycoon() { ApplyPreset(EAIStyle::Tycoon, EAIDifficulty::Normal); }
};

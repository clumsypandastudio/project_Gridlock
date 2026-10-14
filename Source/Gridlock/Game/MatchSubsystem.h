#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Model/GridlockTypes.h"
#include "Model/RulesLibrary.h"
#include "MatchSubsystem.generated.h"

class UScenarioDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGridlockEvent, const FGameEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGridlockStateChanged);

/** Owns the live game. The only thing that changes FBoardState; everything else asks or listens. */
UCLASS()
class GRIDLOCK_API UMatchSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Gridlock")
	void StartMatch(UScenarioDefinition* InScenario, int32 Seed);

	/** Validates and applies one action. False (with a reason) leaves the game unchanged. */
	UFUNCTION(BlueprintCallable, Category = "Gridlock")
	bool Submit(const FGameAction& Action, FText& OutReason);

	/** Steps back one action. For debugging and hot-seat only. */
	UFUNCTION(BlueprintCallable, Category = "Gridlock")
	bool Undo();

	UFUNCTION(BlueprintPure, Category = "Gridlock")
	FBoardState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Gridlock")
	UScenarioDefinition* GetScenario() const { return Scenario; }

	FRulesContext GetContext() const;

	/** One broadcast per event from Apply, in order. Actors and widgets animate from these. */
	UPROPERTY(BlueprintAssignable, Category = "Gridlock")
	FOnGridlockEvent OnGameEvent;

	/** After every successful Submit, Undo or StartMatch. */
	UPROPERTY(BlueprintAssignable, Category = "Gridlock")
	FOnGridlockStateChanged OnStateChanged;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void ApplyScenarioPresets();

	UPROPERTY()
	TObjectPtr<UScenarioDefinition> Scenario;

	UPROPERTY()
	FBoardState State;

	UPROPERTY()
	TArray<FBoardState> History;
};

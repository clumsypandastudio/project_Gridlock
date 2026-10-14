#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Model/GridlockTypes.h"
#include "GridlockGameMode.generated.h"

struct FRulesContext;

class ACameraActor;
class ACityActor;
class APropertyActor;
class ARouteActor;
class UAIPlayer;
class UMapDefinition;
class UMaterialInterface;
class UScenarioDefinition;
class UStaticMesh;

/** Loads the scenario, spawns the board and starts the match. */
UCLASS()
class GRIDLOCK_API AGridlockGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGridlockGameMode();

	/** Used when no scenario was chosen from the menu. Empty = a built-in v0.1 scenario. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gridlock")
	TObjectPtr<UScenarioDefinition> DefaultScenario;

	/** True: one person plays both sides. False: you play Blue against the AI as Orange. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gridlock")
	bool bHotSeat = false;

	/** Seconds between AI moves, so you can follow them. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gridlock", meta = (ClampMin = "0"))
	float AIMoveDelay = 1.0f;

	UFUNCTION(BlueprintCallable, Category = "Gridlock")
	void SetHotSeat(bool bEnabled);

	/** Starts a fresh game with the same scenario. */
	UFUNCTION(BlueprintCallable, Category = "Gridlock")
	void NewGame();

	/** The player the person at this screen controls right now (whoever's turn it is in hot-seat). */
	UFUNCTION(BlueprintPure, Category = "Gridlock")
	int32 GetHumanPlayer() const;

	/** A line for the HUD's log that isn't a game event (e.g. "Build skipped"). */
	void AddNotice(const FString& Text);

	/** Notices since the last call; the HUD takes them each frame. */
	TArray<FString> TakeNotices();

	UFUNCTION(BlueprintPure, Category = "Gridlock")
	UAIPlayer* GetAIPlayer() const { return AIPlayer; }

	/** Override these with BP children (BP_City, BP_Route, BP_Property) for real art. */
	UPROPERTY(EditDefaultsOnly, Category = "Gridlock|Board")
	TSubclassOf<ACityActor> CityClass;

	UPROPERTY(EditDefaultsOnly, Category = "Gridlock|Board")
	TSubclassOf<ARouteActor> RouteClass;

	UPROPERTY(EditDefaultsOnly, Category = "Gridlock|Board")
	TSubclassOf<APropertyActor> PropertyClass;

protected:
	virtual void BeginPlay() override;

	/** Spawns one actor per city, route and property slot from the scenario's map. */
	UFUNCTION(BlueprintNativeEvent, Category = "Gridlock")
	void SpawnBoard();

	/** Pushes the new state to every board actor. */
	UFUNCTION()
	void HandleStateChanged();

private:
	/** Starts the AI's next move after AIMoveDelay, if it's the AI's turn. */
	void ScheduleAITurn();
	void RunAIStep();

	/** Lights up what the human can click this step, and schedules a skip when there's nothing to do. */
	void UpdateHighlights(const FBoardState& State, const FRulesContext& Context);
	void RunAutoSkip();

	UScenarioDefinition* MakeFallbackScenario();
	void SpawnBackdrop(const UMapDefinition& Map);
	void SpawnLightingIfMissing();
	void SpawnCamera();

#if !UE_BUILD_SHIPPING
	/** Dev only: -GridlockDemoSteps=N plays N random moves, -GridlockShot=Path saves a screenshot and quits. */
	void RunCommandLineDevTools();
	static int32 GetRemainingCompileJobs();
	FTimerHandle ShotTimer;
	FTimerHandle QuitTimer;
#endif

	UPROPERTY()
	TObjectPtr<UScenarioDefinition> FallbackScenario;

	UPROPERTY()
	TObjectPtr<UAIPlayer> AIPlayer;

	UPROPERTY()
	TObjectPtr<UScenarioDefinition> ActiveScenario;

	FTimerHandle AITimer;
	FTimerHandle AutoSkipTimer;
	bool bAIStepPending = false;
	bool bAutoSkipPending = false;
	TArray<FString> Notices;

	UPROPERTY()
	TArray<TObjectPtr<ACityActor>> CityActors;

	UPROPERTY()
	TArray<TObjectPtr<ARouteActor>> RouteActors;

	UPROPERTY()
	TArray<TObjectPtr<APropertyActor>> PropertyActors;

	UPROPERTY()
	TObjectPtr<ACameraActor> BoardCamera;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlaneMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BasicShapeMaterial;
};

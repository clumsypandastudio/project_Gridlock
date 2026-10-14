#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Model/GridlockTypes.h"
#include "AIPlayer.generated.h"

class UAIPersonality;
struct FRulesContext;

/** Picks the AI's actions. The game mode submits them through UMatchSubsystem like the human's. */
UCLASS(BlueprintType)
class GRIDLOCK_API UAIPlayer : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TObjectPtr<UAIPersonality> Personality;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	int32 PlayerIndex = 1;

	/** The AI's next action, and an updated intent for the HUD. */
	FGameAction ChooseAction(const FBoardState& State, const FRulesContext& Context);

	/** The route the AI is building toward, for the HUD to highlight. */
	UFUNCTION(BlueprintPure, Category = "AI")
	FName GetIntent() const { return Intent; }

	/**
	 * The choice for any player, as a pure function of state (tests use it for both sides).
	 * Never reads the opponent's hand, including when simulating their reply.
	 */
	static FGameAction ChooseFor(const FBoardState& State, const FRulesContext& Context,
		const UAIPersonality& Personality, int32 Player, bool bAllowLookahead = true);

private:
	/** Ticket to Ride turn: claim a needed route, draw Route cards once all are done, or take the tickets it needs. */
	static FGameAction ChooseTicketToRideTurn(const FBoardState& State, const FRulesContext& Context,
		const UAIPersonality& Personality, int32 Player, const TArray<FGameAction>& Legal, FRandomStream& Random);

	FName Intent;
};

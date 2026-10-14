#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Model/GridlockTypes.h"
#include "RulesLibrary.generated.h"

class UMapDefinition;
class URouteCardDeck;
class UTuningData;

/** The data every rules call needs. Not owned; the scenario's assets outlive it. */
struct GRIDLOCK_API FRulesContext
{
	const UMapDefinition* Map = nullptr;
	const URouteCardDeck* Deck = nullptr;
	const UTuningData* Tuning = nullptr;

	/** A scenario's OverrideCardsToWin; 0 = use Tuning->CardsToWin. */
	int32 CardsToWinOverride = 0;

	bool IsValid() const { return Map && Deck && Tuning; }
	int32 GetCardsToWin() const;
};

/**
 * The rules, as pure functions of state: nothing here touches the world or keeps state.
 * Only UMatchSubsystem calls Apply on the live game; the AI calls it on copies.
 *
 * Phase flow: Setup (each player keeps cards) -> Start (automatic upkeep, then an optional draw)
 * -> Resolve -> Build -> BuyUpgrade -> Score (automatic) -> the next player's Start.
 * Resolve, Build and BuyUpgrade each allow one action, which ends the phase; EndPhase skips it.
 *
 * Rules v0.4 (bOneMainActionPerTurn): one main action a turn: buy cards in Start (2 tickets or 1
 * property card), remove in Resolve, or claim in Build. Any of them moves straight on to BuyUpgrade,
 * where placing or upgrading ends the turn. Selling (Start or BuyUpgrade) is an extra. New Route
 * cards are dealt at the start of a turn once the hand is empty (bRouteCardsWhenHandEmpty).
 */
UCLASS()
class GRIDLOCK_API URulesLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** A fresh game in the Setup phase: unclaimed board, shuffled deck, starting cash, dealt cards. */
	static FBoardState MakeInitialState(const FRulesContext& Context, int32 Seed);

	/** Moves past Setup for players with nothing to choose, e.g. after a scenario preset their hand. */
	static void AdvanceSetup(FBoardState& State, const FRulesContext& Context, TArray<FGameEvent>& OutEvents);

	/** Every action the active player may take right now. Each one passes IsLegal. */
	static TArray<FGameAction> GetLegalActions(const FBoardState& State, const FRulesContext& Context);

	/** False with a reason the HUD can show when the action isn't allowed. */
	static bool IsLegal(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context, FText& OutReason);

	/** The state after the action, plus what happened for the view to animate. Assumes IsLegal passed. */
	static FBoardState Apply(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context, TArray<FGameEvent>& OutEvents);

	/** Exact cash cost of an action, shown before the player commits. Negative when it pays (a sale). */
	static int32 PreviewCost(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context);

	/** Before the Build step: whether the active player could claim this route once they get there, paying as given. */
	static bool CanClaimLater(const FBoardState& State, const FRulesContext& Context, FName RouteId, FText& OutReason,
		EGridlockColor Color = EGridlockColor::Gray, int32 Wilds = -1);

	/** The winner, or INDEX_NONE while the game goes on (or after a turn-limit tie). */
	static int32 CheckWin(const FBoardState& State, const FRulesContext& Context);

	/** Cities the player's routes touch, or their start city before their first route. */
	static TSet<FName> GetNetworkCities(const FBoardState& State, const FRulesContext& Context, int32 Player);

	/** Whether the route's property slot is within the player's range, including Landmark reach. */
	static bool IsRouteInRange(const FBoardState& State, const FRulesContext& Context, int32 Player, FName RouteId);

	/** True when an opponent's working Complex or Landmark sits on the route. */
	static bool IsRouteSevered(const FBoardState& State, FName RouteId, int32 ForPlayer);

	/** Whether the card's two cities are joined by the player's own, unsevered routes. */
	static bool IsCardComplete(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId);

	/** v0.7: the player who owns every lot slot on the route (it's blocked for the other), or INDEX_NONE. */
	static int32 GetRouteBlocker(const FBoardState& State, const FRulesContext& Context, FName RouteId);

	/** The player's longest continuous route in pins: one path through their routes, each route used once. */
	static int32 GetLongestRoute(const FBoardState& State, const FRulesContext& Context, int32 Player);

	/** Total value of the player's properties: the tiebreaker. */
	static int32 GetPropertyValue(const FBoardState& State, const FRulesContext& Context, int32 Player);

	/** True when the player trails by CatchUpGap or more completed cards, halving removal costs. */
	static bool IsCatchUpActive(const FBoardState& State, const FRulesContext& Context, int32 Player);
};

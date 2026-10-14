#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Model/GridlockTypes.h"
#include "TuningData.generated.h"

/**
 * Every number from the Rules tabs. Field defaults are Rules v0.4: the card economy (colored tickets and
 * property cards you buy blind), one main action a turn, $35 to start, and cash from tolls, sales and
 * finished Route cards. The game itself plays Rules v0.5, plain Ticket to Ride on our map: see
 * ApplyRulesV05 in TuningData.cpp for its numbers. ApplyRulesV01..V05 set every field to one version.
 */
UCLASS(BlueprintType)
class GRIDLOCK_API UTuningData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// Setup
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup")
	int32 StartCashFirstPlayer = 35;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup")
	int32 StartCashSecondPlayer = 35;

	/** Route cards dealt at the start, and how many you must keep. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup")
	int32 InitialDeal = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Setup")
	int32 InitialKeep = 3;

	// Economy
	/** Cash at the start of each of your turns, on top of tolls. v0.4: none; money comes from tolls and sales. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	int32 BaseIncome = 0;

	/** v0.4: cash paid for each point of a Route card you finish (a 10-point card pays $10 at 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	int32 CashPerCardPoint = 1;

	/** Off (v0.4, like Ticket to Ride): build any open route. On: a route must touch your network or start city. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Routes")
	bool bRoutesMustConnect = false;

	/** Rules v0.6: money never runs out. Prices are still paid and added up per player (Spent), to tune real amounts from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	bool bUnlimitedCash = false;

	/** Cash per pin to claim a route. 0 in v0.3: the tickets pay for it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy")
	int32 CostPerLengthDot = 0;

	// Card economy (Rules v0.3)

	/** Off = Rules v0.2: routes and properties need only cash, no cards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards")
	bool bCardEconomy = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 TicketsPerColor = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 WildTickets = 14;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 LotCards = 16;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 BuildingCards = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 ComplexCards = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 LandmarkCards = 4;

	/** Cash to buy (blind draw) one ticket, or one property card. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 TicketCost = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 PropertyCardCost = 2;

	/** Tickets you may buy in one turn; 0 = as many as you can afford. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 MaxTicketsPerTurn = 2;

	/** Property cards you may buy in one turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 MaxPropertyCardsPerTurn = 1;

	/** Most tickets and property cards you may hold together; 0 = no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 CardHandLimit = 0;

	/** Dealt free at the start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 StartingTickets = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	int32 StartingLotCards = 1;

	/** A turn's buy is tickets or a property card, not both. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards", meta = (EditCondition = "bCardEconomy"))
	bool bTicketsOrPropertyNotBoth = true;

	/**
	 * Rules v0.4: one main action a turn (build a route, buy cards, or remove a property). Placing or
	 * upgrading properties and selling them are extras you can still do.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards|Options")
	bool bOneMainActionPerTurn = true;

	/** Stricter test option: one action a turn, with placing and upgrading counted as actions too. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Cards|Options")
	bool bOneActionPerTurn = false;

	/** Route cards come only when you've finished all yours: 3 are dealt at the start of your turn (keep 1+). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards|Options")
	bool bRouteCardsWhenHandEmpty = true;

	/** Sell one of your properties back to the bank (an extra, not your main action). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties|Selling")
	bool bCanSellProperties = true;

	/** A sale pays this share of what was invested in the property, rounded down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties|Selling", meta = (EditCondition = "bCanSellProperties"))
	float SellFraction = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties|Selling", meta = (EditCondition = "bCanSellProperties"))
	int32 MaxSalesPerTurn = 1;

	// Ticket to Ride (Rules v0.5). Off by default; ApplyRulesV05 switches them on.

	/**
	 * A Ticket to Ride turn: exactly one of take 2 tickets (face-up or from the deck; a face-up Wild is
	 * the whole draw), claim a route, or draw Route cards. No money and no properties.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride")
	bool bClassicTurn = false;

	/** The ticket deck has all 8 colors; off = the 5 colors of the earlier maps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride")
	bool bEightColors = false;

	/** Trains each player has to place, one per pin; 0 = no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride", meta = (ClampMin = "0"))
	int32 TrainsPerPlayer = 0;

	/** When a player ends a turn with this many trains or fewer, everyone gets one last turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride", meta = (ClampMin = "0"))
	int32 FinalRoundTrains = 2;

	/** Face-up tickets on the table; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride", meta = (ClampMin = "0", ClampMax = "8"))
	int32 FaceUpCount = 0;

	/** This many face-up Wilds and Lots together, and the row is shuffled back into the deck and 5 new are dealt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride", meta = (ClampMin = "2"))
	int32 FaceUpWildReset = 3;

	/** Points for claiming a route, by length: entry 0 is a 1-pin route. Empty = routes score nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride")
	TArray<int32> RoutePoints;

	/** End of the game: points for the longest continuous route (both get it on a tie). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride", meta = (ClampMin = "0"))
	int32 LongestRouteBonus = 0;

	/** v0.7: Lot cards shuffled into the ticket deck; 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride|Lots", meta = (ClampMin = "0"))
	int32 RouteLotCards = 0;

	/** v0.7: a Lot's full price. Face-up it costs this; from the deck, the ticket price now and the rest to keep it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride|Lots", meta = (ClampMin = "0"))
	int32 RouteLotPrice = 4;

	/** v0.7: lot slots on a route, by length (entry 0 is a 1-pin route). All of them owned by one player blocks the route for the other. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride|Lots")
	TArray<int32> RouteLotSlots;

	/** v0.8: a city property's price; 0 = no city properties. You need a route reaching the city, and buying ends your turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride|City Properties", meta = (ClampMin = "0"))
	int32 CityPropertyPrice = 0;

	/** v0.8: what the opponent pays the owner each time they build a route into the city. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride|City Properties", meta = (ClampMin = "0"))
	int32 CityToll = 3;

	/** v0.8: at the end, each finished Route card also pays the toll for every opponent city on its path (both ends included). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride|City Properties")
	bool bCityTollOnCardPaths = false;

	/** v0.8: new Route cards only once all of yours are finished. Stuck on one? Find another way round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride")
	bool bDrawOnlyWhenCardsDone = false;

	/** A face-up Wild's price; 0 = the same as any ticket. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride", meta = (ClampMin = "0"))
	int32 FaceUpWildPrice = 0;

	/** Route cards score (or, with bUnfinishedCardsCountAgainst, cost) only when the game ends, kept secret until then. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ticket to Ride")
	bool bScoreCardsAtEnd = false;

	// Properties
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 LotCost = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 UpgradeToBuildingCost = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 UpgradeToComplexCost = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 UpgradeToLandmarkCost = 6;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 TollLot = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 TollBuilding = 3;

	/** Also used for Landmarks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 TollSevered = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 LandmarkPointsPerTurn = 1;

	/** Routes within this many cities of a Landmark's route are in range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Properties")
	int32 LandmarkRange = 2;

	// Removal
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Removal")
	float BuyoutMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Removal")
	float DemolishMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Removal")
	int32 RubbleTicks = 2;

	/** Removing a property uses up your Build: you can't claim a route on a turn you removed one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Removal|Options")
	bool bRemovalUsesBuild = true;

	/** A property mid-upgrade costs its upgraded tier's value to remove, and the owner gets no refund. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Removal|Options")
	bool bUpgradeWindowFullPrice = true;

	// Route cards
	/** Route cards drawn at once during a game, how many you must keep, and the most unfinished you may hold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards")
	int32 DrawCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards")
	int32 MinKeep = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards")
	int32 HandLimit = 6;

	/** Drawing Route cards uses up your Build: you can't claim a route on a turn you drew. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards|Options")
	bool bDrawReplacesBuild = true;

	/** Cash paid to draw Route cards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards|Options", meta = (ClampMin = "0"))
	int32 DrawCost = 0;

	/** Cards kept this turn can't score until your next turn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards|Options")
	bool bHoldBeforeScoring = false;

	/** At the end of the game, each unfinished card in hand subtracts its points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Route Cards|Options")
	bool bUnfinishedCardsCountAgainst = false;

	// Winning
	/** v0.4: 5 cards in up to 40 rounds (2 tickets a buy makes building slow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Winning")
	int32 CardsToWin = 5;

	/** v0.3 cards are worth their distance (236 points in the deck), so the backup target is 100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Winning")
	int32 PointsToWin = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Winning")
	int32 TurnLimit = 40;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Winning")
	int32 CatchUpGap = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Winning")
	float CatchUpDiscount = 0.5f;

	/** Sets every field to one rules version (v0.1 original; v0.2 AI-playtest fixes; v0.3 card economy; v0.4 one main action). */
	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV01();

	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV02();

	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV03();

	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV04();

	/** Plain Ticket to Ride on our map. */
	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV05();

	/** Ticket to Ride where every ticket costs money (unlimited funds to start with). */
	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV06();

	/** v0.6 plus Lot cards that block routes. */
	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV07();

	/** v0.7 plus city properties that collect tolls: what the game plays now. */
	UFUNCTION(CallInEditor, Category = "Presets")
	void ApplyRulesV08();

	/** Lot slots on a route of this length (0 when RouteLotSlots doesn't cover it). */
	int32 GetLotSlots(int32 Length) const;

	/** What a face-up card of this kind costs: a ticket, a Wild, or a Lot. */
	int32 GetFaceUpPrice(EGridlockColor Color) const;

	/** Points for claiming a route of this length (0 when RoutePoints doesn't cover it). */
	int32 GetRoutePoints(int32 Length) const;

	/** The colors in the ticket deck: all 8, or the earlier rules' 5. */
	TArray<EGridlockColor> GetDeckColors() const;

	/** Cost to place (Lot) or to upgrade into the given tier. */
	int32 GetCostToReach(EPropertyTier Tier) const;

	/** Total spent to build a property up to the given tier. */
	int32 GetTierValue(EPropertyTier Tier) const;

	/** Toll an opponent pays each turn for their route carrying this tier. */
	int32 GetToll(EPropertyTier Tier) const;

	/** Removal costs, rounded up, with the catch-up discount when it applies. */
	int32 GetBuyoutCost(int32 Value, bool bCatchUp) const;
	int32 GetDemolishCost(int32 Value, bool bCatchUp) const;

	/** How many property cards of a tier are in the full deck. */
	int32 GetPropertyCardsInDeck(EPropertyTier Tier) const;
};

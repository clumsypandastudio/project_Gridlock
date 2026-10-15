#pragma once

#include "CoreMinimal.h"
#include "GridlockTypes.generated.h"

UENUM(BlueprintType)
enum class EGridlockPhase : uint8
{
	/** Both players choose which of their dealt Route cards to keep. */
	Setup,
	Start,
	Resolve,
	Build,
	BuyUpgrade,
	Score,
	GameOver
};

UENUM(BlueprintType)
enum class EGridlockActionType : uint8
{
	DrawCards,
	KeepCards,
	Buyout,
	Demolish,
	ClaimRoute,
	PlaceLot,
	Upgrade,
	EndPhase,
	/** Pay to draw the top ticket (Rules v0.3 card economy). */
	BuyTicket,
	/** Pay to draw the top property card. */
	BuyPropertyCard,
	/** Sell one of your properties back to the bank (Rules v0.4). */
	SellProperty,
	/** Ticket to Ride turn (v0.5): take the face-up ticket at Slot (0-4). Taking a blind one is BuyTicket. */
	TakeFaceUp,
	/** v0.7: a Lot drawn from the deck is kept (paying the rest of its price) or discarded. */
	KeepLot,
	DiscardLot,
	/** v0.7: put one of your Lot cards on a free lot slot of an open route. It ends your turn: one Lot a turn. */
	PlaceRouteLot,
	/** v0.8: buy the property in a city (RouteId = the city) one of your routes reaches. It ends your turn. */
	BuyCityProperty
};

/**
 * Route colors, and the ticket colors that pay for them. A Gray route takes any one color; a Wild
 * ticket is any color. The 8 card colors run from Red to White, as in Ticket to Ride.
 */
UENUM(BlueprintType)
enum class EGridlockColor : uint8
{
	Gray,
	Red,
	Orange,
	Yellow,
	Green,
	Blue,
	Purple,
	Black,
	White,
	Wild,
	/** Not a color: a Lot card in the ticket deck (Rules v0.7). Placed on a route's lot slot; all of a route's lots block it for the other player. */
	Lot
};

/** Ticket colors are indexed by EGridlockColor, so per-player counts are arrays of this size. */
constexpr int32 GridlockColorCount = 11;

/** The 8 card colors (no Gray, no Wild), in order. */
inline TArray<EGridlockColor> GridlockCardColors()
{
	return { EGridlockColor::Red, EGridlockColor::Orange, EGridlockColor::Yellow, EGridlockColor::Green,
		EGridlockColor::Blue, EGridlockColor::Purple, EGridlockColor::Black, EGridlockColor::White };
}

/** The 5 colors of the earlier rules (v0.1-v0.4), whose maps use only these. */
inline TArray<EGridlockColor> GridlockClassicFiveColors()
{
	return { EGridlockColor::Red, EGridlockColor::Yellow, EGridlockColor::Green, EGridlockColor::Purple, EGridlockColor::Black };
}

/** Property card counts are indexed by EPropertyTier (index 0, None, is unused). */
constexpr int32 GridlockTierCount = 5;

UENUM(BlueprintType)
enum class EPropertyTier : uint8
{
	None = 0,
	Lot = 1,
	Building = 2,
	Complex = 3,
	Landmark = 4
};

UENUM(BlueprintType)
enum class ERouteCardType : uint8
{
	Short,
	Medium,
	Long
};

UENUM(BlueprintType)
enum class EGridlockEventType : uint8
{
	PhaseChanged,
	IncomePaid,
	TollPaid,
	CardsDrawn,
	RouteClaimed,
	PropertyPlaced,
	UpgradeStarted,
	UpgradeFinished,
	PropertyRemoved,
	RubbleCleared,
	CardScored,
	GameOver,
	TurnStarted,
	LandmarkPointsScored,
	CardsKept,
	/** Like PropertyRemoved, but paid to the bank (demolish) rather than the owner (buyout). */
	PropertyDemolished,
	/** Amount = the ticket's EGridlockColor (hidden from the opponent in the UI). */
	TicketBought,
	/** Amount = the card's EPropertyTier (hidden from the opponent in the UI). */
	PropertyCardBought,
	/** A deck ran out and its discard pile was shuffled in. Amount: 0 tickets, 1 properties. */
	DeckReshuffled,
	/** Amount = the cash the bank paid. */
	PropertySold,
	/** A finished Route card paid out. Amount = cash, CardId = the card. */
	CardPaidOut,
	/** A face-up ticket was taken (public). Amount = its EGridlockColor. */
	FaceUpTaken,
	/** Three face-up Wilds: the whole face-up row was discarded and dealt again. */
	FaceUpReset,
	/** End of the game: an unfinished Route card. Amount = the points lost (negative), CardId = the card. */
	CardFailed,
	/** End of the game: the longest continuous route bonus. Amount = points, RouteId unused. */
	LongestRouteBonus,
	/** A player is down to their last trains: everyone gets one more turn. Amount = trains left. */
	FinalRound,
	/** v0.7: a Lot came off the deck; its drawer chooses to keep or discard it. */
	LotDrawn,
	/** Amount = the extra paid. */
	LotKept,
	LotDiscarded,
	/** RouteId = the route. Amount = lots on it now. */
	RouteLotPlaced,
	/** A player now owns every lot on RouteId: blocked for the other player. */
	RouteBlocked,
	/** v0.8: RouteId = the city, Amount = the price. */
	CityPropertyBought,
	/** v0.8: Player paid the city's owner. RouteId = the city, CardId = the Route card when paid at the end (else none), Amount = cash. */
	CityTollPaid
};

/** One move by a human or the AI. Every change to the game goes through one of these. */
USTRUCT(BlueprintType)
struct FGameAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	EGridlockActionType Type = EGridlockActionType::EndPhase;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Player = 0;

	/** A property slot is identified by its route. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	FName RouteId;

	/** KeepCards only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<FName> CardIds;

	/** ClaimRoute on a Gray route: which ticket color to pay with. Gray = pick the one you hold most of. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	EGridlockColor Color = EGridlockColor::Gray;

	/** ClaimRoute: how many Wild tickets to pay with (the rest in the color). -1 = only the Wilds needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Wilds = -1;

	/** TakeFaceUp: which face-up ticket (0-4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Slot = INDEX_NONE;
};

/** What Apply reports back, so the view can animate without running rules. */
USTRUCT(BlueprintType)
struct FGameEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	EGridlockEventType Type = EGridlockEventType::PhaseChanged;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Player = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	FName RouteId;

	/** CardScored, CardsDrawn and CardsKept. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	FName CardId;

	/** Cash, points or tier, depending on Type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Amount = 0;
};

USTRUCT(BlueprintType)
struct FSlotState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Owner = INDEX_NONE;

	/** Current, working tier. An upgrade in progress does not count until it finishes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	EPropertyTier Tier = EPropertyTier::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	bool bUpgrading = false;

	/** Total cash spent on this property, including an upgrade in progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Invested = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 RubbleTicks = 0;

	/** Rubble ticks down at the start of this player's turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 RubbleBy = INDEX_NONE;
};

/** The lots on one route (Rules v0.7), in the order they were placed. */
USTRUCT(BlueprintType)
struct FRouteLots
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<int32> Owners;
};

USTRUCT(BlueprintType)
struct FPlayerData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	FName StartCity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Cash = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Points = 0;

	/** Unfinished Route cards. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<FName> Hand;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<FName> Completed;

	/** Drawn cards waiting for a KeepCards action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<FName> PendingDraw;

	/** Tickets held, indexed by EGridlockColor. Hidden from the opponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<int32> Tickets;

	/** Property cards held, indexed by EPropertyTier. Hidden from the opponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<int32> PropertyCards;

	/** Everything this player has paid so far (with unlimited cash, the only record of spending). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Spent = 0;

	/** What Spent went on (v0.8, for the results window): tickets, Lots, city properties, and tolls paid to the opponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 SpentOnTickets = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 SpentOnLots = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 SpentOnProperties = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 TollsPaid = 0;

	/** Everything this player has been paid (tolls); Earned - Spent is their net. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Earned = 0;

	/** v0.8: tolls this player earned, by city (for the results window). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TMap<FName, int32> TollsByCity;

	/** v0.8: tolls this player paid at the end for a finished Route card's path, by card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TMap<FName, int32> TollsByCard;

	/** Trains left to place (Ticket to Ride rules); a route uses one per pin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Trains = 0;

	/** v0.7: a Lot just came off the deck and waits for KeepLot or DiscardLot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	bool bLotPending = false;

	/** Tickets held, Lot cards not included. */
	int32 TicketCount() const { int32 Total = 0; for (const int32 Count : Tickets) { Total += Count; } return Total - LotCardCount(); }
	int32 LotCardCount() const { return Tickets.IsValidIndex(static_cast<int32>(EGridlockColor::Lot)) ? Tickets[static_cast<int32>(EGridlockColor::Lot)] : 0; }
	int32 PropertyCardCount() const { int32 Total = 0; for (const int32 Count : PropertyCards) { Total += Count; } return Total; }
	int32 TicketsOf(EGridlockColor Color) const { return Tickets.IsValidIndex(static_cast<int32>(Color)) ? Tickets[static_cast<int32>(Color)] : 0; }
	int32 PropertyCardsOf(EPropertyTier Tier) const { return PropertyCards.IsValidIndex(static_cast<int32>(Tier)) ? PropertyCards[static_cast<int32>(Tier)] : 0; }
};

/** The whole game as plain data. Cheap to copy for AI simulation, undo and saves. */
USTRUCT(BlueprintType)
struct FBoardState
{
	GENERATED_BODY()

	/** Route id -> owning player, INDEX_NONE when unclaimed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TMap<FName, int32> RouteOwner;

	/** Route id -> the property slot on that route. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TMap<FName, FSlotState> Slots;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<FPlayerData> Players;

	/** Remaining Route cards, top of the deck last. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<FName> Deck;

	/** Ticket deck (top last) and its discard pile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<EGridlockColor> TicketDeck;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<EGridlockColor> TicketDiscard;

	/** The face-up tickets anyone can take (Ticket to Ride rules), up to 5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<EGridlockColor> FaceUp;

	/** v0.8: city id -> the player who owns its property. Unowned cities aren't listed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TMap<FName, int32> CityOwner;

	/** v0.7: route id -> the lots placed on it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TMap<FName, FRouteLots> RouteLots;

	/** Turns left once someone is down to their last trains; -1 = not yet. The game ends at 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 FinalTurnsLeft = -1;

	/** Property deck (top last) and its discard pile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<EPropertyTier> PropertyDeck;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<EPropertyTier> PropertyDiscard;

	/** How many times a discard pile has been shuffled back in (seeds the next shuffle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 ShuffleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	EGridlockPhase Phase = EGridlockPhase::Setup;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 ActivePlayer = 0;

	/** A round ends when play comes back to this player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 FirstPlayer = 0;

	/** Route cards may be drawn once per turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	bool bDrewThisTurn = false;

	/** The active player removed a property this turn (for the removal-uses-build option). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	bool bRemovedThisTurn = false;

	/** Cards bought this turn: all together, and tickets and property cards separately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 BuysThisTurn = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 TicketBuysThisTurn = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 PropertyBuysThisTurn = 0;

	/** Main actions taken this turn (for the one-action options). A run of buys counts as one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 ActionsThisTurn = 0;

	/** Properties the active player sold this turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 SalesThisTurn = 0;

	/** Cards the active player kept this turn (for the hold-before-scoring option). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	TArray<FName> CardsKeptThisTurn;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Round = 1;

	/** Seed for every random draw, so a game replays exactly from its action list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 RandomSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gridlock")
	int32 Winner = INDEX_NONE;
};

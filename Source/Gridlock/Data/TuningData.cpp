#include "Data/TuningData.h"
#include "UObject/UnrealType.h"

void UTuningData::ApplyRulesV01()
{
	Modify();
	ApplyRulesV03();
	bCardEconomy = false;
	CostPerLengthDot = 1;
	InitialDeal = 3;
	InitialKeep = 2;
	TollBuilding = 2;
	TollSevered = 2;
	DemolishMultiplier = 2.0f;
	bRemovalUsesBuild = false;
	bUpgradeWindowFullPrice = false;
	DrawCount = 2;
	HandLimit = 4;
	bDrawReplacesBuild = false;
	CardsToWin = 5;
	PointsToWin = 40;
}

void UTuningData::ApplyRulesV02()
{
	ApplyRulesV01();
	bDrawReplacesBuild = true;
	CardsToWin = 8;
	PointsToWin = 60;
}

void UTuningData::ApplyRulesV03()
{
	ApplyRulesV04();
	StartCashFirstPlayer = 6;
	StartCashSecondPlayer = 8;
	BaseIncome = 4;
	MaxTicketsPerTurn = 0;
	bTicketsOrPropertyNotBoth = false;
	bOneMainActionPerTurn = false;
	bRouteCardsWhenHandEmpty = false;
	bCanSellProperties = false;
	CardsToWin = 8;
	TurnLimit = 25;
	bRoutesMustConnect = true;
	CashPerCardPoint = 0;
}

void UTuningData::ApplyRulesV04()
{
	// Every field back to its declared default, which is v0.4.
	Modify();
	const UTuningData* Defaults = GetDefault<UTuningData>();
	for (TFieldIterator<FProperty> It(UTuningData::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		It->CopyCompleteValue_InContainer(this, Defaults);
	}
	MarkPackageDirty();
}

void UTuningData::ApplyRulesV05()
{
	// Ticket to Ride on our map: the numbers to change to retune the game.
	ApplyRulesV04();
	bClassicTurn = true;
	bEightColors = true;
	TicketsPerColor = 12;          // 8 colors x 12 + 14 Wild = 110 tickets, as in Ticket to Ride
	WildTickets = 14;
	StartingTickets = 4;           // dealt to each player at the start
	TrainsPerPlayer = 45;
	FinalRoundTrains = 2;
	FaceUpCount = 5;
	FaceUpWildReset = 3;
	MaxTicketsPerTurn = 2;         // take 2 tickets a turn (a face-up Wild counts as both)
	RoutePoints = { 1, 2, 4, 7, 10, 15, 18, 21, 27 }; // route length 1..9
	LongestRouteBonus = 10;
	InitialDeal = 3;               // Route cards dealt at the start, keep at least 2
	InitialKeep = 2;
	DrawCount = 3;                 // Route cards drawn during the game, keep at least 1
	MinKeep = 1;
	HandLimit = 30;
	bScoreCardsAtEnd = true;
	bUnfinishedCardsCountAgainst = true;
	bRoutesMustConnect = false;

	// No money, no properties, no other way to win than the most points at the end.
	StartCashFirstPlayer = 0;
	StartCashSecondPlayer = 0;
	BaseIncome = 0;
	CashPerCardPoint = 0;
	TicketCost = 0;
	PropertyCardCost = 0;
	StartingLotCards = 0;
	bCanSellProperties = false;
	bOneMainActionPerTurn = false;
	bTicketsOrPropertyNotBoth = false;
	bRouteCardsWhenHandEmpty = false;
	DrawCost = 0;
	CardsToWin = 0;
	PointsToWin = 0;
	TurnLimit = 100;               // a safety net only: the trains end the game long before
}

void UTuningData::ApplyRulesV06()
{
	// Ticket to Ride, plus buying tickets. To make money matter: bUnlimitedCash = false and give a starting amount.
	ApplyRulesV05();
	TicketCost = 1;                // per ticket, face-up or from the deck
	bUnlimitedCash = true;
	StartCashFirstPlayer = 0;      // used once bUnlimitedCash is off
	StartCashSecondPlayer = 0;
}

void UTuningData::ApplyRulesV07()
{
	// v0.6 plus Lots: cards in the ticket deck that you place on routes to block them.
	ApplyRulesV06();
	RouteLotCards = 12;
	RouteLotPrice = 4;             // face-up $4 and the turn ends; from the deck $1, then $3 more to keep it
	RouteLotSlots = { 1, 1, 1, 2, 2, 2, 3, 3, 3 }; // route length 1..9
	FaceUpWildPrice = 2;           // a face-up Wild is both of your tickets, so it costs 2
}

void UTuningData::ApplyRulesV08()
{
	// v0.7 plus city properties: buy one in a city your route reaches; the opponent pays you a toll.
	ApplyRulesV07();
	CityPropertyPrice = 10;
	CityToll = 3;                  // each time they build a route into your city
	bCityTollOnCardPaths = true;   // and at the end, for each finished Route card whose path visits it
}

int32 UTuningData::GetLotSlots(int32 Length) const
{
	return RouteLotSlots.IsValidIndex(Length - 1) ? RouteLotSlots[Length - 1] : 0;
}

int32 UTuningData::GetFaceUpPrice(EGridlockColor Color) const
{
	if (Color == EGridlockColor::Lot)
	{
		return RouteLotPrice;
	}
	return Color == EGridlockColor::Wild && FaceUpWildPrice > 0 ? FaceUpWildPrice : TicketCost;
}

int32 UTuningData::GetRoutePoints(int32 Length) const
{
	return RoutePoints.IsValidIndex(Length - 1) ? RoutePoints[Length - 1] : 0;
}

TArray<EGridlockColor> UTuningData::GetDeckColors() const
{
	return bEightColors ? GridlockCardColors() : GridlockClassicFiveColors();
}

int32 UTuningData::GetPropertyCardsInDeck(EPropertyTier Tier) const
{
	switch (Tier)
	{
	case EPropertyTier::Lot:      return LotCards;
	case EPropertyTier::Building: return BuildingCards;
	case EPropertyTier::Complex:  return ComplexCards;
	case EPropertyTier::Landmark: return LandmarkCards;
	default:                      return 0;
	}
}

int32 UTuningData::GetCostToReach(EPropertyTier Tier) const
{
	switch (Tier)
	{
	case EPropertyTier::Lot:      return LotCost;
	case EPropertyTier::Building: return UpgradeToBuildingCost;
	case EPropertyTier::Complex:  return UpgradeToComplexCost;
	case EPropertyTier::Landmark: return UpgradeToLandmarkCost;
	default:                      return 0;
	}
}

int32 UTuningData::GetTierValue(EPropertyTier Tier) const
{
	int32 Value = 0;
	for (uint8 Step = 1; Step <= static_cast<uint8>(Tier); ++Step)
	{
		Value += GetCostToReach(static_cast<EPropertyTier>(Step));
	}
	return Value;
}

int32 UTuningData::GetToll(EPropertyTier Tier) const
{
	switch (Tier)
	{
	case EPropertyTier::Lot:      return TollLot;
	case EPropertyTier::Building: return TollBuilding;
	case EPropertyTier::Complex:
	case EPropertyTier::Landmark: return TollSevered;
	default:                      return 0;
	}
}

int32 UTuningData::GetBuyoutCost(int32 Value, bool bCatchUp) const
{
	const float Discount = bCatchUp ? CatchUpDiscount : 1.0f;
	return FMath::CeilToInt(Value * BuyoutMultiplier * Discount);
}

int32 UTuningData::GetDemolishCost(int32 Value, bool bCatchUp) const
{
	const float Discount = bCatchUp ? CatchUpDiscount : 1.0f;
	return FMath::CeilToInt(Value * DemolishMultiplier * Discount);
}

#include "Misc/AutomationTest.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "Model/RulesLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GridlockCardEconomyTests
{
	/** Map v0.1, deck v0.1 and Rules v0.3 (or v0.4, the default tuning). Player 0 starts at Westport. */
	struct FFixture
	{
		FRulesContext Context;
		UTuningData* Tuning = nullptr;

		explicit FFixture(bool bRulesV04 = false)
		{
			UMapDefinition* Map = NewObject<UMapDefinition>();
			Map->FillWithMapV01();
			URouteCardDeck* Deck = NewObject<URouteCardDeck>();
			Deck->FillWithDeckV01();
			Tuning = NewObject<UTuningData>();
			if (!bRulesV04)
			{
				Tuning->ApplyRulesV03();
			}
			Context.Map = Map;
			Context.Deck = Deck;
			Context.Tuning = Tuning;
		}

		/** Past Setup with no cards in hand, player 0 first and to move in the given phase. */
		FBoardState MakeTurnState(EGridlockPhase Phase, int32 Cash = 20) const
		{
			FBoardState State = URulesLibrary::MakeInitialState(Context, 3);
			for (FPlayerData& Player : State.Players)
			{
				State.Deck.Append(Player.PendingDraw);
				Player.PendingDraw.Reset();
				for (int32 Color = 0; Color < GridlockColorCount; ++Color)
				{
					for (int32 Count = 0; Count < Player.Tickets[Color]; ++Count)
					{
						State.TicketDeck.Add(static_cast<EGridlockColor>(Color));
					}
				}
				for (int32 Tier = 0; Tier < GridlockTierCount; ++Tier)
				{
					for (int32 Count = 0; Count < Player.PropertyCards[Tier]; ++Count)
					{
						State.PropertyDeck.Add(static_cast<EPropertyTier>(Tier));
					}
				}
				Player.Tickets.Init(0, GridlockColorCount);
				Player.PropertyCards.Init(0, GridlockTierCount);
				Player.Cash = Cash;
			}
			State.FirstPlayer = 0;
			State.ActivePlayer = 0;
			State.Phase = Phase;
			return State;
		}

		static FGameAction Make(EGridlockActionType Type, int32 Player, FName RouteId = NAME_None)
		{
			FGameAction Action;
			Action.Type = Type;
			Action.Player = Player;
			Action.RouteId = RouteId;
			return Action;
		}

		bool IsLegal(const FBoardState& State, const FGameAction& Action) const
		{
			FText Reason;
			return URulesLibrary::IsLegal(State, Action, Context, Reason);
		}

		bool Do(FBoardState& State, EGridlockActionType Type, FName RouteId = NAME_None) const
		{
			const FGameAction Action = Make(Type, State.ActivePlayer, RouteId);
			FText Reason;
			if (!URulesLibrary::IsLegal(State, Action, Context, Reason))
			{
				return false;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Context, Events);
			return true;
		}

		static void Give(FBoardState& State, int32 Player, EGridlockColor Color, int32 Count)
		{
			State.Players[Player].Tickets[static_cast<int32>(Color)] += Count;
		}

		static void GiveProperty(FBoardState& State, int32 Player, EPropertyTier Tier, int32 Count)
		{
			State.Players[Player].PropertyCards[static_cast<int32>(Tier)] += Count;
		}

		int32 TicketsInGame() const { return Tuning->TicketsPerColor * 5 + Tuning->WildTickets; }
		int32 PropertyCardsInGame() const { return Tuning->LotCards + Tuning->BuildingCards + Tuning->ComplexCards + Tuning->LandmarkCards; }

		int32 TotalTickets(const FBoardState& State) const
		{
			int32 Total = State.TicketDeck.Num() + State.TicketDiscard.Num();
			for (const FPlayerData& Player : State.Players)
			{
				Total += Player.TicketCount();
			}
			return Total;
		}

		int32 TotalPropertyCards(const FBoardState& State) const
		{
			int32 Total = State.PropertyDeck.Num() + State.PropertyDiscard.Num();
			for (const FPlayerData& Player : State.Players)
			{
				Total += Player.PropertyCardCount();
			}
			return Total;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockCardSetupTest, "Gridlock.Rules.CardEconomy.Setup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockCardSetupTest::RunTest(const FString& Parameters)
{
	const GridlockCardEconomyTests::FFixture Fixture;
	FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, 11);
	const int32 First = State.FirstPlayer;

	TestEqual(TEXT("Five Route cards dealt"), State.Players[First].PendingDraw.Num(), 5);
	TestEqual(TEXT("Six starting tickets"), State.Players[First].TicketCount(), 6);
	TestEqual(TEXT("One starting Lot card"), State.Players[First].PropertyCardsOf(EPropertyTier::Lot), 1);
	TestEqual(TEXT("All 114 tickets accounted for"), Fixture.TotalTickets(State), 114);
	TestEqual(TEXT("All 36 property cards accounted for"), Fixture.TotalPropertyCards(State), 36);

	FGameAction KeepTwo = GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::KeepCards, First);
	KeepTwo.CardIds = { State.Players[First].PendingDraw[0], State.Players[First].PendingDraw[1] };
	TestFalse(TEXT("Keeping 2 of 5 is too few"), Fixture.IsLegal(State, KeepTwo));
	FGameAction KeepAll = KeepTwo;
	KeepAll.CardIds = State.Players[First].PendingDraw;
	TestTrue(TEXT("Keeping all 5 is allowed"), Fixture.IsLegal(State, KeepAll));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockCardBuyTest, "Gridlock.Rules.CardEconomy.Buying",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockCardBuyTest::RunTest(const FString& Parameters)
{
	const GridlockCardEconomyTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::Start, 10);

	TestEqual(TEXT("Ticket price"), URulesLibrary::PreviewCost(State, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::BuyTicket, 0), Fixture.Context), 1);
	TestTrue(TEXT("Buy a ticket"), Fixture.Do(State, EGridlockActionType::BuyTicket));
	TestEqual(TEXT("Paid $1"), State.Players[0].Cash, 9);
	TestEqual(TEXT("Holding 1 ticket"), State.Players[0].TicketCount(), 1);
	TestTrue(TEXT("Still the Start step"), State.Phase == EGridlockPhase::Start);
	TestFalse(TEXT("No Route-card draw after buying"), Fixture.IsLegal(State, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::DrawCards, 0)));

	TestTrue(TEXT("Buy a property card"), Fixture.Do(State, EGridlockActionType::BuyPropertyCard));
	TestEqual(TEXT("Paid $2"), State.Players[0].Cash, 7);
	TestFalse(TEXT("Only one property card a turn"), Fixture.IsLegal(State, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::BuyPropertyCard, 0)));
	for (int32 Count = 0; Count < 7; ++Count)
	{
		TestTrue(FString::Printf(TEXT("Tickets as long as the cash lasts (%d)"), Count + 2), Fixture.Do(State, EGridlockActionType::BuyTicket));
	}
	TestEqual(TEXT("Cash spent"), State.Players[0].Cash, 0);
	TestFalse(TEXT("No cash, no ticket"), Fixture.IsLegal(State, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::BuyTicket, 0)));
	TestEqual(TEXT("Cards conserved"), Fixture.TotalTickets(State), Fixture.TicketsInGame());

	FBoardState Drew = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	Fixture.Do(Drew, EGridlockActionType::DrawCards);
	TestFalse(TEXT("No buying after drawing Route cards"), Fixture.IsLegal(Drew, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::BuyTicket, 0)));

	FBoardState Broke = Fixture.MakeTurnState(EGridlockPhase::Start, 1);
	TestFalse(TEXT("A property card costs $2"), Fixture.IsLegal(Broke, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::BuyPropertyCard, 0)));

	FBoardState Full = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	GridlockCardEconomyTests::FFixture::Give(Full, 0, EGridlockColor::Red, 30);
	TestTrue(TEXT("No hand limit by default"), Fixture.IsLegal(Full, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::BuyTicket, 0)));
	Fixture.Tuning->CardHandLimit = 10;
	TestFalse(TEXT("An optional hand limit stops buying"), Fixture.IsLegal(Full, GridlockCardEconomyTests::FFixture::Make(EGridlockActionType::BuyTicket, 0)));
	Fixture.Tuning->CardHandLimit = 0;

	// An empty deck reshuffles its discard pile.
	FBoardState Empty = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	Empty.TicketDiscard = Empty.TicketDeck;
	Empty.TicketDeck.Reset();
	const int32 Discarded = Empty.TicketDiscard.Num();
	TestTrue(TEXT("Buy from a reshuffled deck"), Fixture.Do(Empty, EGridlockActionType::BuyTicket));
	TestTrue(TEXT("Discard pile went into the deck"), Empty.TicketDiscard.IsEmpty() && Empty.TicketDeck.Num() == Discarded - 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockCardRouteTest, "Gridlock.Rules.CardEconomy.ClaimWithTickets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockCardRouteTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockCardEconomyTests::FFixture;
	const FFixture Fixture;

	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::Build);
	FFixture::Give(State, 0, EGridlockColor::Yellow, 1);
	TestFalse(TEXT("Westport - Ironford (Yellow, 2) needs 2 tickets"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));
	FFixture::Give(State, 0, EGridlockColor::Wild, 1);
	TestTrue(TEXT("A Wild covers the second dot"), Fixture.Do(State, EGridlockActionType::ClaimRoute, TEXT("R03")));
	TestEqual(TEXT("Both tickets spent"), State.Players[0].TicketCount(), 0);
	TestEqual(TEXT("Into the discard pile"), State.TicketDiscard.Num(), 2);
	TestEqual(TEXT("No cash to claim: the tickets pay"), State.Players[0].Cash, 20);

	FBoardState Wrong = Fixture.MakeTurnState(EGridlockPhase::Build);
	FFixture::Give(Wrong, 0, EGridlockColor::Red, 2);
	TestFalse(TEXT("Red tickets don't pay for a Yellow route"), Fixture.IsLegal(Wrong, FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));
	TestTrue(TEXT("...but do for Westport - Pinecrest (Red)"), Fixture.IsLegal(Wrong, FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R01"))));

	FBoardState Gray = Fixture.MakeTurnState(EGridlockPhase::Build);
	Gray.RouteOwner[TEXT("R03")] = 0;
	FFixture::Give(Gray, 0, EGridlockColor::Purple, 1);
	TestTrue(TEXT("Ironford - Highcliff (Gray, 1) takes any color"), Fixture.Do(Gray, EGridlockActionType::ClaimRoute, TEXT("R05")));
	TestEqual(TEXT("Purple spent"), Gray.Players[0].TicketsOf(EGridlockColor::Purple), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockCardPropertyTest, "Gridlock.Rules.CardEconomy.PropertyCards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockCardPropertyTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockCardEconomyTests::FFixture;
	const FFixture Fixture;

	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	TestFalse(TEXT("No Lot without a Lot card"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::PlaceLot, 0, TEXT("R01"))));
	FFixture::GiveProperty(State, 0, EPropertyTier::Lot, 1);
	TestTrue(TEXT("Place a Lot with the card"), Fixture.Do(State, EGridlockActionType::PlaceLot, TEXT("R01")));
	TestEqual(TEXT("Card spent"), State.Players[0].PropertyCardsOf(EPropertyTier::Lot), 0);
	TestEqual(TEXT("Card discarded"), State.PropertyDiscard.Num(), 1);

	FBoardState Upgrade = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	Upgrade.Slots[TEXT("R01")].Owner = 0;
	Upgrade.Slots[TEXT("R01")].Tier = EPropertyTier::Lot;
	FFixture::GiveProperty(Upgrade, 0, EPropertyTier::Complex, 1);
	TestFalse(TEXT("A Complex card can't turn a Lot into a Building"), Fixture.IsLegal(Upgrade, FFixture::Make(EGridlockActionType::Upgrade, 0, TEXT("R01"))));
	FFixture::GiveProperty(Upgrade, 0, EPropertyTier::Building, 1);
	TestTrue(TEXT("A Building card can"), Fixture.Do(Upgrade, EGridlockActionType::Upgrade, TEXT("R01")));
	TestEqual(TEXT("Building card spent, Complex kept"), Upgrade.Players[0].PropertyCardsOf(EPropertyTier::Complex), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockCardOneActionTest, "Gridlock.Rules.CardEconomy.OneActionOption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockCardOneActionTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockCardEconomyTests::FFixture;
	FFixture Fixture;
	Fixture.Tuning->bOneActionPerTurn = true;

	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::Start);
	FFixture::Give(State, 0, EGridlockColor::Yellow, 2);
	TestTrue(TEXT("Buy (the turn's action)"), Fixture.Do(State, EGridlockActionType::BuyTicket));
	TestTrue(TEXT("More buys are part of the same action"), Fixture.Do(State, EGridlockActionType::BuyTicket));
	Fixture.Do(State, EGridlockActionType::EndPhase); // Start -> Resolve
	Fixture.Do(State, EGridlockActionType::EndPhase); // Resolve -> Build
	TestFalse(TEXT("No claim after buying"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));

	FBoardState Build = Fixture.MakeTurnState(EGridlockPhase::Build);
	FFixture::Give(Build, 0, EGridlockColor::Yellow, 2);
	FFixture::GiveProperty(Build, 0, EPropertyTier::Lot, 1);
	TestTrue(TEXT("Claim (the turn's action)"), Fixture.Do(Build, EGridlockActionType::ClaimRoute, TEXT("R03")));
	TestFalse(TEXT("No Lot after claiming"), Fixture.IsLegal(Build, FFixture::Make(EGridlockActionType::PlaceLot, 0, TEXT("R03"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockOneMainActionTest, "Gridlock.Rules.CardEconomy.OneMainAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockOneMainActionTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockCardEconomyTests::FFixture;
	const FFixture Fixture(true);

	const FBoardState Fresh = URulesLibrary::MakeInitialState(Fixture.Context, 5);
	TestTrue(TEXT("Both players start with $35"), Fresh.Players[0].Cash == 35 && Fresh.Players[1].Cash == 35);

	// Tickets: 2 at most, then straight on to placing and upgrading.
	FBoardState Tickets = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	TestTrue(TEXT("Buy a ticket"), Fixture.Do(Tickets, EGridlockActionType::BuyTicket));
	TestTrue(TEXT("Still buying after 1"), Tickets.Phase == EGridlockPhase::Start);
	TestFalse(TEXT("Tickets or a property card, not both"), Fixture.IsLegal(Tickets, FFixture::Make(EGridlockActionType::BuyPropertyCard, 0)));
	TestTrue(TEXT("Buy a second ticket"), Fixture.Do(Tickets, EGridlockActionType::BuyTicket));
	TestTrue(TEXT("2 tickets end the main action"), Tickets.Phase == EGridlockPhase::BuyUpgrade);
	TestEqual(TEXT("Paid $2"), Tickets.Players[0].Cash, 8);
	FFixture::GiveProperty(Tickets, 0, EPropertyTier::Lot, 1);
	TestTrue(TEXT("A Lot is still allowed after buying"), Fixture.IsLegal(Tickets, FFixture::Make(EGridlockActionType::PlaceLot, 0, TEXT("R01"))));

	// One ticket, then Done: on to placing and upgrading, not to building.
	FBoardState OneTicket = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	Fixture.Do(OneTicket, EGridlockActionType::BuyTicket);
	TestTrue(TEXT("Done after 1 ticket"), Fixture.Do(OneTicket, EGridlockActionType::EndPhase));
	TestTrue(TEXT("...skips building"), OneTicket.Phase == EGridlockPhase::BuyUpgrade);

	// A property card is the whole buy.
	FBoardState Property = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	TestTrue(TEXT("Buy a property card"), Fixture.Do(Property, EGridlockActionType::BuyPropertyCard));
	TestTrue(TEXT("1 property card ends the main action"), Property.Phase == EGridlockPhase::BuyUpgrade);

	// Building instead of buying.
	FBoardState Build = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	FFixture::Give(Build, 0, EGridlockColor::Yellow, 2);
	FFixture::GiveProperty(Build, 0, EPropertyTier::Lot, 1);
	FText Reason;
	TestTrue(TEXT("From Start, Westport - Ironford can be built"), URulesLibrary::CanClaimLater(Build, Fixture.Context, TEXT("R03"), Reason));
	Fixture.Do(Build, EGridlockActionType::EndPhase); // Start -> Resolve
	Fixture.Do(Build, EGridlockActionType::EndPhase); // Resolve -> Build
	TestTrue(TEXT("Claim (the main action)"), Fixture.Do(Build, EGridlockActionType::ClaimRoute, TEXT("R03")));
	TestTrue(TEXT("...then on to placing"), Build.Phase == EGridlockPhase::BuyUpgrade);
	TestTrue(TEXT("Place a Lot after building"), Fixture.Do(Build, EGridlockActionType::PlaceLot, TEXT("R03")));
	TestTrue(TEXT("Placing ends the turn"), Build.ActivePlayer == 1);

	FBoardState Bought = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	FFixture::Give(Bought, 0, EGridlockColor::Yellow, 2);
	Fixture.Do(Bought, EGridlockActionType::BuyTicket);
	TestFalse(TEXT("No building after buying"), URulesLibrary::CanClaimLater(Bought, Fixture.Context, TEXT("R03"), Reason));

	// Selling: half of what was spent, once a turn, at the start or after the main action.
	FBoardState Sell = Fixture.MakeTurnState(EGridlockPhase::Start, 10);
	Sell.Slots[TEXT("R01")].Owner = 0;
	Sell.Slots[TEXT("R01")].Tier = EPropertyTier::Building;
	Sell.Slots[TEXT("R01")].Invested = 5;
	Sell.Slots[TEXT("R02")].Owner = 0;
	Sell.Slots[TEXT("R02")].Tier = EPropertyTier::Lot;
	Sell.Slots[TEXT("R02")].Invested = 2;
	TestEqual(TEXT("A $5 Building sells for $2"), -URulesLibrary::PreviewCost(Sell, FFixture::Make(EGridlockActionType::SellProperty, 0, TEXT("R01")), Fixture.Context), 2);
	TestTrue(TEXT("Sell it"), Fixture.Do(Sell, EGridlockActionType::SellProperty, TEXT("R01")));
	TestEqual(TEXT("Paid $2"), Sell.Players[0].Cash, 12);
	TestTrue(TEXT("The slot is empty again"), Sell.Slots[TEXT("R01")].Owner == INDEX_NONE && Sell.Slots[TEXT("R01")].RubbleTicks == 0);
	TestTrue(TEXT("Selling isn't the main action"), Sell.Phase == EGridlockPhase::Start && Fixture.IsLegal(Sell, FFixture::Make(EGridlockActionType::BuyTicket, 0)));
	TestFalse(TEXT("One sale a turn"), Fixture.IsLegal(Sell, FFixture::Make(EGridlockActionType::SellProperty, 0, TEXT("R02"))));
	Sell.Slots[TEXT("R03")].Owner = 1;
	Sell.Slots[TEXT("R03")].Tier = EPropertyTier::Lot;
	Sell.SalesThisTurn = 0;
	TestFalse(TEXT("Only your own properties"), Fixture.IsLegal(Sell, FFixture::Make(EGridlockActionType::SellProperty, 0, TEXT("R03"))));

	// Route cards: no drawing; once all are finished, 3 are dealt at the start of the turn.
	FBoardState Cards = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade, 10);
	TestFalse(TEXT("No drawing Route cards"), Fixture.IsLegal(Cards, FFixture::Make(EGridlockActionType::DrawCards, 0)));
	Cards.Players[0].Hand = { Cards.Deck.Last() };
	Cards.Deck.Pop();
	const int32 OpponentCash = Cards.Players[1].Cash;
	Fixture.Do(Cards, EGridlockActionType::EndPhase); // player 0 ends the turn; player 1, with no cards, is dealt 3
	TestEqual(TEXT("No income in v0.4: cash only comes from tolls and sales"), Cards.Players[1].Cash, OpponentCash);
	TestEqual(TEXT("3 Route cards dealt"), Cards.Players[1].PendingDraw.Num(), 3);
	FGameAction KeepOne = FFixture::Make(EGridlockActionType::KeepCards, 1);
	KeepOne.CardIds = { Cards.Players[1].PendingDraw[0] };
	TestTrue(TEXT("Keep 1"), Fixture.IsLegal(Cards, KeepOne));
	TArray<FGameEvent> Events;
	Cards = URulesLibrary::Apply(Cards, KeepOne, Fixture.Context, Events);
	TestTrue(TEXT("...and the turn goes on from the start"), Cards.Phase == EGridlockPhase::Start && Cards.Players[1].Hand.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockChoosePaymentTest, "Gridlock.Rules.CardEconomy.ChoosePayment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockChoosePaymentTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockCardEconomyTests::FFixture;
	const FFixture Fixture(true);
	auto Claim = [](FName RouteId, EGridlockColor Color, int32 Wilds)
	{
		FGameAction Action = FFixture::Make(EGridlockActionType::ClaimRoute, 0, RouteId);
		Action.Color = Color;
		Action.Wilds = Wilds;
		return Action;
	};

	// Any open route, not only ones next to your network (player 0 starts at Westport).
	FBoardState Anywhere = Fixture.MakeTurnState(EGridlockPhase::Build);
	FFixture::Give(Anywhere, 0, EGridlockColor::Purple, 1);
	TestTrue(TEXT("Ironford - Highcliff (gray, 1) is far from Westport but can be built"), Fixture.IsLegal(Anywhere, Claim(TEXT("R05"), EGridlockColor::Purple, 0)));
	TestFalse(TEXT("...not with a color you don't hold"), Fixture.IsLegal(Anywhere, Claim(TEXT("R05"), EGridlockColor::Green, 0)));

	// Westport - Ironford: Yellow, 2 pins. Holding 2 Yellow and 1 Wild.
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::Build);
	FFixture::Give(State, 0, EGridlockColor::Yellow, 2);
	FFixture::Give(State, 0, EGridlockColor::Wild, 1);
	TestFalse(TEXT("2 Wilds chosen but only 1 held"), Fixture.IsLegal(State, Claim(TEXT("R03"), EGridlockColor::Yellow, 2)));
	TestFalse(TEXT("More Wilds than the route takes"), Fixture.IsLegal(State, Claim(TEXT("R03"), EGridlockColor::Yellow, 3)));
	FBoardState WithWild = State;
	TArray<FGameEvent> Events;
	TestTrue(TEXT("1 Yellow + 1 Wild, by choice"), Fixture.IsLegal(WithWild, Claim(TEXT("R03"), EGridlockColor::Yellow, 1)));
	WithWild = URulesLibrary::Apply(WithWild, Claim(TEXT("R03"), EGridlockColor::Yellow, 1), Fixture.Context, Events);
	TestTrue(TEXT("Paid 1 Yellow and 1 Wild"), WithWild.Players[0].TicketsOf(EGridlockColor::Yellow) == 1 && WithWild.Players[0].TicketsOf(EGridlockColor::Wild) == 0);
	FBoardState NoWild = URulesLibrary::Apply(State, Claim(TEXT("R03"), EGridlockColor::Yellow, 0), Fixture.Context, Events);
	TestTrue(TEXT("2 Yellow, Wild kept"), NoWild.Players[0].TicketsOf(EGridlockColor::Yellow) == 0 && NoWild.Players[0].TicketsOf(EGridlockColor::Wild) == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockCardPayoutTest, "Gridlock.Rules.CardEconomy.CardPayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockCardPayoutTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockCardEconomyTests::FFixture;
	const FFixture Fixture(true);

	// Player 0 owns every route, so the one card in hand is finished and scores when the turn ends.
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade, 10);
	const FRouteCardDef& Card = Fixture.Context.Deck->Cards[0];
	State.Deck.Remove(Card.Id);
	State.Players[0].Hand = { Card.Id };
	for (TPair<FName, int32>& Owner : State.RouteOwner)
	{
		Owner.Value = 0;
	}
	TestTrue(TEXT("End the turn"), Fixture.Do(State, EGridlockActionType::EndPhase));
	TestEqual(TEXT("The card scored its points"), State.Players[0].Points, Card.Points);
	TestEqual(TEXT("...and paid $1 per point"), State.Players[0].Cash, 10 + Card.Points * Fixture.Tuning->CashPerCardPoint);
	TestTrue(TEXT("$1 per point by default"), Fixture.Tuning->CashPerCardPoint == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockCardPlayoutTest, "Gridlock.Rules.CardEconomy.RandomPlayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockCardPlayoutTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockCardEconomyTests::FFixture;
	FFixture Fixture;

	for (const int32 Rules : { 0, 1, 2 }) // v0.3, v0.3 with one action, v0.4
	{
		if (Rules == 2)
		{
			Fixture.Tuning->ApplyRulesV04();
		}
		const bool bOneAction = Rules == 1;
		Fixture.Tuning->bOneActionPerTurn = bOneAction;
		for (int32 Seed = 1; Seed <= 20; ++Seed)
		{
			FRandomStream Random(Seed * 6151);
			FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, Seed);
			for (int32 Step = 0; Step < 4000 && State.Phase != EGridlockPhase::GameOver; ++Step)
			{
				const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Fixture.Context);
				if (!TestTrue(FString::Printf(TEXT("Seed %d: always a legal action"), Seed), Legal.Num() > 0))
				{
					return false;
				}
				const FGameAction& Action = Legal.Num() > 1 && Random.FRand() < 0.85f ? Legal[Random.RandRange(0, Legal.Num() - 2)] : Legal.Last();
				TArray<FGameEvent> Events;
				State = URulesLibrary::Apply(State, Action, Fixture.Context, Events);

				for (const FPlayerData& Player : State.Players)
				{
					bool bNegative = Player.Cash < 0;
					for (const int32 Count : Player.Tickets)
					{
						bNegative |= Count < 0;
					}
					for (const int32 Count : Player.PropertyCards)
					{
						bNegative |= Count < 0;
					}
					const bool bOverLimit = Fixture.Tuning->CardHandLimit > 0 && Player.TicketCount() + Player.PropertyCardCount() > Fixture.Tuning->CardHandLimit;
					if (bNegative || bOverLimit)
					{
						AddError(FString::Printf(TEXT("Seed %d step %d: negative cash or cards, or over the card limit"), Seed, Step));
						return false;
					}
				}
				if (Fixture.TotalTickets(State) != Fixture.TicketsInGame() || Fixture.TotalPropertyCards(State) != Fixture.PropertyCardsInGame())
				{
					AddError(FString::Printf(TEXT("Seed %d step %d: %d tickets and %d property cards (expected %d and %d)"),
						Seed, Step, Fixture.TotalTickets(State), Fixture.TotalPropertyCards(State), Fixture.TicketsInGame(), Fixture.PropertyCardsInGame()));
					return false;
				}
			}
			TestTrue(FString::Printf(TEXT("Seed %d (rules %d): game ends"), Seed, Rules), State.Phase == EGridlockPhase::GameOver);
		}
	}
	return true;
}

#endif

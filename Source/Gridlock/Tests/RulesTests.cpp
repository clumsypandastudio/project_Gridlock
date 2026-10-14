#include "Misc/AutomationTest.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "Model/RulesLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GridlockRulesTests
{
	/** Map v0.1, deck v0.1 and default tuning. Player 0 starts at Westport, player 1 at Easton. */
	struct FFixture
	{
		FRulesContext Context;

		FFixture()
		{
			UMapDefinition* Map = NewObject<UMapDefinition>();
			Map->FillWithMapV01();
			URouteCardDeck* Deck = NewObject<URouteCardDeck>();
			Deck->FillWithDeckV01();
			Context.Map = Map;
			Context.Deck = Deck;

			// These tests pin down the v0.2 mechanics; Gridlock.Rules.CardEconomy covers v0.3.
			UTuningData* Tuning = NewObject<UTuningData>();
			Tuning->ApplyRulesV02();
			Context.Tuning = Tuning;
		}

		/** Past Setup with empty hands, player 0 first and to move in the given phase. */
		FBoardState MakeTurnState(EGridlockPhase Phase, int32 Cash = 20) const
		{
			FBoardState State = URulesLibrary::MakeInitialState(Context, 1);
			for (FPlayerData& Player : State.Players)
			{
				State.Deck.Append(Player.PendingDraw);
				Player.PendingDraw.Reset();
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

		/** Applies the action if it's legal; returns whether it was. */
		bool Do(FBoardState& State, const FGameAction& Action) const
		{
			FText Reason;
			if (!URulesLibrary::IsLegal(State, Action, Context, Reason))
			{
				return false;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Context, Events);
			return true;
		}

		bool Do(FBoardState& State, EGridlockActionType Type, FName RouteId = NAME_None) const
		{
			return Do(State, Make(Type, State.ActivePlayer, RouteId));
		}

		/** Ends phases until the active player changes or the game ends. */
		void EndTurn(FBoardState& State) const
		{
			const int32 Player = State.ActivePlayer;
			while (State.ActivePlayer == Player && State.Phase != EGridlockPhase::GameOver)
			{
				Do(State, EGridlockActionType::EndPhase);
			}
		}

		static void SetProperty(FBoardState& State, FName RouteId, int32 Owner, EPropertyTier Tier)
		{
			FSlotState& Slot = State.Slots.FindChecked(RouteId);
			Slot.Owner = Owner;
			Slot.Tier = Tier;
		}
	};
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesSetupTest, "Gridlock.Rules.Setup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesSetupTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, 7);
	const int32 First = State.FirstPlayer;
	const int32 Second = 1 - First;

	TestTrue(TEXT("Phase"), State.Phase == EGridlockPhase::Setup);
	TestEqual(TEXT("Slots"), State.Slots.Num(), 30);
	TestEqual(TEXT("Deck after dealing"), State.Deck.Num(), 14);
	TestEqual(TEXT("First player cash"), State.Players[First].Cash, 6);
	TestEqual(TEXT("Second player cash"), State.Players[Second].Cash, 8);
	TestEqual(TEXT("First player to choose"), State.ActivePlayer, First);

	const TArray<FName> Dealt = State.Players[First].PendingDraw;
	TestEqual(TEXT("Dealt cards"), Dealt.Num(), 3);

	FGameAction KeepOne = GridlockRulesTests::FFixture::Make(EGridlockActionType::KeepCards, First);
	KeepOne.CardIds = { Dealt[0] };
	TestFalse(TEXT("Keeping 1 of 3 at setup is illegal"), Fixture.IsLegal(State, KeepOne));
	TestFalse(TEXT("EndPhase is illegal during setup"), Fixture.IsLegal(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::EndPhase, First)));

	FGameAction KeepTwo = KeepOne;
	KeepTwo.CardIds.Add(Dealt[1]);
	TestTrue(TEXT("Keeping 2 is legal"), Fixture.Do(State, KeepTwo));
	TestEqual(TEXT("Hand after keeping"), State.Players[First].Hand.Num(), 2);
	TestTrue(TEXT("Discarded card goes to the bottom"), State.Deck[0] == Dealt[2]);
	TestEqual(TEXT("Second player chooses next"), State.ActivePlayer, Second);
	TestTrue(TEXT("Still in setup"), State.Phase == EGridlockPhase::Setup);

	FGameAction KeepAll = GridlockRulesTests::FFixture::Make(EGridlockActionType::KeepCards, Second);
	KeepAll.CardIds = State.Players[Second].PendingDraw;
	TestTrue(TEXT("Second player keeps all 3"), Fixture.Do(State, KeepAll));
	TestTrue(TEXT("Game starts"), State.Phase == EGridlockPhase::Start);
	TestEqual(TEXT("First player opens"), State.ActivePlayer, First);
	TestEqual(TEXT("Round 1"), State.Round, 1);
	TestEqual(TEXT("First player collected income"), State.Players[First].Cash, 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesDrawTest, "Gridlock.Rules.DrawCards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesDrawTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::Start);
	const int32 DeckBefore = State.Deck.Num();

	TestTrue(TEXT("Draw"), Fixture.Do(State, EGridlockActionType::DrawCards));
	TestEqual(TEXT("Two cards drawn"), State.Players[0].PendingDraw.Num(), 2);
	TestFalse(TEXT("Must keep before ending the phase"), Fixture.IsLegal(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::EndPhase, 0)));

	FGameAction KeepNone = GridlockRulesTests::FFixture::Make(EGridlockActionType::KeepCards, 0);
	TestFalse(TEXT("Must keep at least 1"), Fixture.IsLegal(State, KeepNone));

	const TArray<FName> Drawn = State.Players[0].PendingDraw;
	FGameAction KeepOne = KeepNone;
	KeepOne.CardIds = { Drawn[0] };
	TestTrue(TEXT("Keep 1"), Fixture.Do(State, KeepOne));
	TestTrue(TEXT("Moves on to Resolve"), State.Phase == EGridlockPhase::Resolve);
	TestEqual(TEXT("Deck lost one card"), State.Deck.Num(), DeckBefore - 1);
	TestTrue(TEXT("Unkept card at the bottom"), State.Deck[0] == Drawn[1]);

	FBoardState Full = Fixture.MakeTurnState(EGridlockPhase::Start);
	Full.Players[0].Hand = { TEXT("C01"), TEXT("C03"), TEXT("C05"), TEXT("C07") };
	TestFalse(TEXT("No draw with a full hand"), Fixture.IsLegal(Full, GridlockRulesTests::FFixture::Make(EGridlockActionType::DrawCards, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesClaimTest, "Gridlock.Rules.ClaimRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesClaimTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::Build);

	TestFalse(TEXT("Ironford-Midway doesn't touch Westport"), Fixture.IsLegal(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R11"))));

	FBoardState Blocked = State;
	GridlockRulesTests::FFixture::SetProperty(Blocked, TEXT("R03"), 1, EPropertyTier::Building);
	TestFalse(TEXT("An opponent Building blocks the claim"), Fixture.IsLegal(Blocked, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));
	GridlockRulesTests::FFixture::SetProperty(Blocked, TEXT("R03"), 1, EPropertyTier::Lot);
	TestTrue(TEXT("An opponent Lot doesn't block"), Fixture.IsLegal(Blocked, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));
	GridlockRulesTests::FFixture::SetProperty(Blocked, TEXT("R03"), 0, EPropertyTier::Building);
	TestTrue(TEXT("Your own Building doesn't block you"), Fixture.IsLegal(Blocked, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));

	TestEqual(TEXT("Preview cost of a length-2 route"), URulesLibrary::PreviewCost(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03")), Fixture.Context), 2);
	TestTrue(TEXT("Claim Westport-Ironford"), Fixture.Do(State, EGridlockActionType::ClaimRoute, TEXT("R03")));
	TestEqual(TEXT("Owner"), State.RouteOwner[TEXT("R03")], 0);
	TestEqual(TEXT("Cash"), State.Players[0].Cash, 18);
	TestTrue(TEXT("One claim per turn: on to Buy/Upgrade"), State.Phase == EGridlockPhase::BuyUpgrade);
	TestFalse(TEXT("No second claim"), Fixture.IsLegal(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R01"))));

	FBoardState Broke = Fixture.MakeTurnState(EGridlockPhase::Build, 1);
	TestFalse(TEXT("Can't claim without the cash"), Fixture.IsLegal(Broke, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesPropertyTest, "Gridlock.Rules.LotsAndUpgrades",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesPropertyTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	State.RouteOwner[TEXT("R03")] = 0; // Westport-Ironford

	TestFalse(TEXT("Stonewatch-Northbridge is out of range"), Fixture.IsLegal(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::PlaceLot, 0, TEXT("R26"))));
	TestTrue(TEXT("Lot on Ironford-Midway"), Fixture.Do(State, EGridlockActionType::PlaceLot, TEXT("R11")));
	TestTrue(TEXT("Lot placed at once"), State.Slots[TEXT("R11")].Tier == EPropertyTier::Lot);
	TestEqual(TEXT("Lot cost"), State.Players[0].Cash, 18);
	TestEqual(TEXT("Turn passed"), State.ActivePlayer, 1);

	Fixture.EndTurn(State); // player 1 passes
	Fixture.Do(State, EGridlockActionType::EndPhase); // Start -> Resolve
	Fixture.Do(State, EGridlockActionType::EndPhase); // Resolve -> Build
	Fixture.Do(State, EGridlockActionType::EndPhase); // Build -> BuyUpgrade
	TestTrue(TEXT("Upgrade to Building"), Fixture.Do(State, EGridlockActionType::Upgrade, TEXT("R11")));
	TestTrue(TEXT("Upgrade in progress"), State.Slots[TEXT("R11")].bUpgrading);
	TestTrue(TEXT("Still a Lot while building"), State.Slots[TEXT("R11")].Tier == EPropertyTier::Lot);

	Fixture.EndTurn(State); // player 1 passes; player 0's turn starts
	TestTrue(TEXT("Building finished at the start of my turn"), State.Slots[TEXT("R11")].Tier == EPropertyTier::Building);
	TestFalse(TEXT("Upgrade done"), State.Slots[TEXT("R11")].bUpgrading);
	TestEqual(TEXT("Invested"), State.Slots[TEXT("R11")].Invested, 5);

	FBoardState Landmark = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	GridlockRulesTests::FFixture::SetProperty(Landmark, TEXT("R01"), 0, EPropertyTier::Landmark);
	TestFalse(TEXT("A Landmark can't be upgraded"), Fixture.IsLegal(Landmark, GridlockRulesTests::FFixture::Make(EGridlockActionType::Upgrade, 0, TEXT("R01"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesRemovalTest, "Gridlock.Rules.Removal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesRemovalTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::Resolve, 30);
	GridlockRulesTests::FFixture::SetProperty(State, TEXT("R01"), 1, EPropertyTier::Complex); // Westport-Pinecrest, value 9

	TestEqual(TEXT("Buyout is 1x value"), URulesLibrary::PreviewCost(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::Buyout, 0, TEXT("R01")), Fixture.Context), 9);
	TestEqual(TEXT("Demolish is 2x value"), URulesLibrary::PreviewCost(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::Demolish, 0, TEXT("R01")), Fixture.Context), 18);

	FBoardState Bought = State;
	TestTrue(TEXT("Buyout"), Fixture.Do(Bought, EGridlockActionType::Buyout, TEXT("R01")));
	TestEqual(TEXT("Buyer paid"), Bought.Players[0].Cash, 21);
	TestEqual(TEXT("Owner was paid"), Bought.Players[1].Cash, 39);

	TestTrue(TEXT("Demolish"), Fixture.Do(State, EGridlockActionType::Demolish, TEXT("R01")));
	TestEqual(TEXT("Demolisher paid"), State.Players[0].Cash, 12);
	TestEqual(TEXT("Owner got nothing"), State.Players[1].Cash, 30);
	TestEqual(TEXT("Property gone"), State.Slots[TEXT("R01")].Owner, INDEX_NONE);
	TestEqual(TEXT("Rubble"), State.Slots[TEXT("R01")].RubbleTicks, 2);
	TestTrue(TEXT("One removal per turn: on to Build"), State.Phase == EGridlockPhase::Build);

	Fixture.Do(State, EGridlockActionType::EndPhase); // Build -> BuyUpgrade
	TestFalse(TEXT("No Lot on rubble"), Fixture.IsLegal(State, GridlockRulesTests::FFixture::Make(EGridlockActionType::PlaceLot, 0, TEXT("R01"))));
	Fixture.EndTurn(State);
	Fixture.EndTurn(State);
	TestEqual(TEXT("Rubble ticks at the remover's turn"), State.Slots[TEXT("R01")].RubbleTicks, 1);
	Fixture.EndTurn(State);
	Fixture.EndTurn(State);
	TestEqual(TEXT("Rubble cleared after 2 of the remover's turns"), State.Slots[TEXT("R01")].RubbleTicks, 0);

	FBoardState Far = Fixture.MakeTurnState(EGridlockPhase::Resolve);
	GridlockRulesTests::FFixture::SetProperty(Far, TEXT("R27"), 1, EPropertyTier::Lot);
	TestFalse(TEXT("Can't remove out of range"), Fixture.IsLegal(Far, GridlockRulesTests::FFixture::Make(EGridlockActionType::Buyout, 0, TEXT("R27"))));

	FBoardState CatchUp = Fixture.MakeTurnState(EGridlockPhase::Resolve);
	GridlockRulesTests::FFixture::SetProperty(CatchUp, TEXT("R01"), 1, EPropertyTier::Complex);
	CatchUp.Players[1].Completed = { TEXT("C14"), TEXT("C16") };
	TestEqual(TEXT("Catch-up halves demolish"), URulesLibrary::PreviewCost(CatchUp, GridlockRulesTests::FFixture::Make(EGridlockActionType::Demolish, 0, TEXT("R01")), Fixture.Context), 9);
	TestEqual(TEXT("Catch-up halves buyout, rounded up"), URulesLibrary::PreviewCost(CatchUp, GridlockRulesTests::FFixture::Make(EGridlockActionType::Buyout, 0, TEXT("R01")), Fixture.Context), 5);

	FBoardState Upgrading = Fixture.MakeTurnState(EGridlockPhase::Resolve);
	GridlockRulesTests::FFixture::SetProperty(Upgrading, TEXT("R01"), 1, EPropertyTier::Lot);
	Upgrading.Slots[TEXT("R01")].bUpgrading = true;
	TestTrue(TEXT("Buy out mid-upgrade"), Fixture.Do(Upgrading, EGridlockActionType::Buyout, TEXT("R01")));
	TestEqual(TEXT("Paid the Lot value only"), Upgrading.Players[0].Cash, 18);
	TestEqual(TEXT("Owner gets the Lot value plus the upgrade refund"), Upgrading.Players[1].Cash, 25);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesTollTest, "Gridlock.Rules.Tolls",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesTollTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade, 0);
	State.ActivePlayer = 1;
	State.RouteOwner[TEXT("R03")] = 0;
	GridlockRulesTests::FFixture::SetProperty(State, TEXT("R03"), 1, EPropertyTier::Lot);

	Fixture.EndTurn(State);
	TestEqual(TEXT("Income 4, Lot toll 1"), State.Players[0].Cash, 3);
	TestEqual(TEXT("Toll paid to the owner"), State.Players[1].Cash, 1);

	FBoardState Heavy = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade, 0);
	Heavy.ActivePlayer = 1;
	for (const TCHAR* Route : { TEXT("R01"), TEXT("R02"), TEXT("R03") })
	{
		Heavy.RouteOwner[Route] = 0;
		GridlockRulesTests::FFixture::SetProperty(Heavy, Route, 1, EPropertyTier::Complex);
	}
	Fixture.EndTurn(Heavy);
	TestEqual(TEXT("Tolls stop at what you have"), Heavy.Players[0].Cash, 0);
	TestEqual(TEXT("Owner receives only what was paid"), Heavy.Players[1].Cash, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesScoringTest, "Gridlock.Rules.ScoringAndWinning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesScoringTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	State.RouteOwner[TEXT("R03")] = 0; // Westport-Ironford
	State.RouteOwner[TEXT("R05")] = 0; // Ironford-Highcliff
	State.Players[0].Hand = { TEXT("C13") }; // Westport-Highcliff, 4 points

	FBoardState Severed = State;
	GridlockRulesTests::FFixture::SetProperty(Severed, TEXT("R05"), 1, EPropertyTier::Complex);
	Fixture.Do(Severed, EGridlockActionType::EndPhase);
	TestEqual(TEXT("A severed path doesn't score"), Severed.Players[0].Completed.Num(), 0);

	Fixture.Do(State, EGridlockActionType::EndPhase);
	TestEqual(TEXT("Card scored"), State.Players[0].Completed.Num(), 1);
	TestEqual(TEXT("Points"), State.Players[0].Points, 4);
	TestEqual(TEXT("Card left the hand"), State.Players[0].Hand.Num(), 0);

	FBoardState CardsWin = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	CardsWin.RouteOwner[TEXT("R03")] = 0;
	CardsWin.RouteOwner[TEXT("R05")] = 0;
	CardsWin.Players[0].Hand = { TEXT("C13") };
	CardsWin.Players[0].Completed = { TEXT("C01"), TEXT("C03"), TEXT("C05"), TEXT("C07"), TEXT("C09"), TEXT("C11"), TEXT("C12") };
	Fixture.Do(CardsWin, EGridlockActionType::EndPhase);
	TestTrue(TEXT("Eighth card ends the game"), CardsWin.Phase == EGridlockPhase::GameOver);
	TestEqual(TEXT("Winner"), CardsWin.Winner, 0);

	FBoardState PointsWin = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	PointsWin.Players[0].Points = 60;
	Fixture.Do(PointsWin, EGridlockActionType::EndPhase);
	TestEqual(TEXT("60 points wins"), PointsWin.Winner, 0);

	FBoardState Limit = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	Limit.Round = 25;
	Limit.ActivePlayer = 1;
	Limit.Players[1].Points = 12;
	Limit.Players[0].Points = 12;
	GridlockRulesTests::FFixture::SetProperty(Limit, TEXT("R01"), 0, EPropertyTier::Lot);
	Fixture.Do(Limit, EGridlockActionType::EndPhase);
	TestTrue(TEXT("Round 25 ends the game"), Limit.Phase == EGridlockPhase::GameOver);
	TestEqual(TEXT("Tied points go to property value"), Limit.Winner, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesLandmarkTest, "Gridlock.Rules.Landmarks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesLandmarkTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	FBoardState State = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	GridlockRulesTests::FFixture::SetProperty(State, TEXT("R01"), 0, EPropertyTier::Complex); // Westport-Pinecrest

	TestFalse(TEXT("Highcliff-Northbridge out of range without a Landmark"), URulesLibrary::IsRouteInRange(State, Fixture.Context, 0, TEXT("R10")));
	GridlockRulesTests::FFixture::SetProperty(State, TEXT("R01"), 0, EPropertyTier::Landmark);
	TestTrue(TEXT("Landmark reaches Highcliff-Northbridge"), URulesLibrary::IsRouteInRange(State, Fixture.Context, 0, TEXT("R10")));
	TestTrue(TEXT("...and Stonewatch-Northbridge"), URulesLibrary::IsRouteInRange(State, Fixture.Context, 0, TEXT("R26")));
	TestFalse(TEXT("...but not Copperton-Midway"), URulesLibrary::IsRouteInRange(State, Fixture.Context, 0, TEXT("R27")));

	State.ActivePlayer = 1;
	Fixture.EndTurn(State);
	TestEqual(TEXT("Landmark scores 1 point at the start of my turn"), State.Players[0].Points, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesCardOptionsTest, "Gridlock.Rules.CardOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesCardOptionsTest::RunTest(const FString& Parameters)
{
	GridlockRulesTests::FFixture Fixture;
	UTuningData* Tuning = NewObject<UTuningData>();
	Tuning->ApplyRulesV02();
	Fixture.Context.Tuning = Tuning;

	// Draw replaces Build.
	Tuning->bDrawReplacesBuild = true;
	FBoardState Drew = Fixture.MakeTurnState(EGridlockPhase::Start);
	Fixture.Do(Drew, EGridlockActionType::DrawCards);
	FGameAction KeepAll = GridlockRulesTests::FFixture::Make(EGridlockActionType::KeepCards, 0);
	KeepAll.CardIds = Drew.Players[0].PendingDraw;
	Fixture.Do(Drew, KeepAll);
	Fixture.Do(Drew, EGridlockActionType::EndPhase); // Resolve -> Build
	TestFalse(TEXT("No claim on a turn you drew"), Fixture.IsLegal(Drew, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));
	FBoardState NoDraw = Fixture.MakeTurnState(EGridlockPhase::Build);
	TestTrue(TEXT("Claiming is fine without a draw"), Fixture.IsLegal(NoDraw, GridlockRulesTests::FFixture::Make(EGridlockActionType::ClaimRoute, 0, TEXT("R03"))));
	Tuning->bDrawReplacesBuild = false;

	// Draw cost.
	Tuning->DrawCost = 2;
	FBoardState Paid = Fixture.MakeTurnState(EGridlockPhase::Start, 5);
	TestEqual(TEXT("Preview shows the draw cost"), URulesLibrary::PreviewCost(Paid, GridlockRulesTests::FFixture::Make(EGridlockActionType::DrawCards, 0), Fixture.Context), 2);
	TestTrue(TEXT("Draw"), Fixture.Do(Paid, EGridlockActionType::DrawCards));
	TestEqual(TEXT("Paid $2 to draw"), Paid.Players[0].Cash, 3);
	FBoardState Broke = Fixture.MakeTurnState(EGridlockPhase::Start, 1);
	TestFalse(TEXT("Can't draw without the cash"), Fixture.IsLegal(Broke, GridlockRulesTests::FFixture::Make(EGridlockActionType::DrawCards, 0)));
	Tuning->DrawCost = 0;

	// Hold before scoring: a card kept this turn doesn't score until the next turn.
	Tuning->bHoldBeforeScoring = true;
	FBoardState Held = Fixture.MakeTurnState(EGridlockPhase::Start);
	Held.RouteOwner[TEXT("R03")] = 0; // Westport-Ironford
	Held.RouteOwner[TEXT("R05")] = 0; // Ironford-Highcliff
	Held.Deck.Remove(TEXT("C13"));
	Held.Deck.Add(TEXT("C13")); // Westport-Highcliff on top: already complete when drawn
	Fixture.Do(Held, EGridlockActionType::DrawCards);
	FGameAction KeepC13 = GridlockRulesTests::FFixture::Make(EGridlockActionType::KeepCards, 0);
	KeepC13.CardIds = { TEXT("C13") };
	TestTrue(TEXT("Keep the complete card"), Fixture.Do(Held, KeepC13));
	Fixture.EndTurn(Held);
	TestEqual(TEXT("Not scored on the turn it was kept"), Held.Players[0].Completed.Num(), 0);
	Fixture.EndTurn(Held); // the opponent's turn
	Fixture.EndTurn(Held);
	TestEqual(TEXT("Scored a turn later"), Held.Players[0].Completed.Num(), 1);
	Tuning->bHoldBeforeScoring = false;

	// Unfinished cards count against you at the end.
	Tuning->bUnfinishedCardsCountAgainst = true;
	FBoardState Limit = Fixture.MakeTurnState(EGridlockPhase::BuyUpgrade);
	Limit.Round = 25;
	Limit.ActivePlayer = 1;
	Limit.Players[0].Points = 10;
	Limit.Players[0].Hand = { TEXT("C01") }; // 11 points, unfinished
	Limit.Players[1].Points = 5;
	Fixture.Do(Limit, EGridlockActionType::EndPhase);
	TestEqual(TEXT("Unfinished card subtracted"), Limit.Players[0].Points, -1);
	TestEqual(TEXT("So the other player wins on points"), Limit.Winner, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockRulesPlayoutTest, "Gridlock.Rules.RandomPlayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockRulesPlayoutTest::RunTest(const FString& Parameters)
{
	const GridlockRulesTests::FFixture Fixture;
	const int32 TotalCards = Fixture.Context.Deck->Cards.Num();
	int32 GamesWithAWinner = 0;

	for (int32 Seed = 1; Seed <= 30; ++Seed)
	{
		FRandomStream Random(Seed * 7919);
		FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, Seed);

		int32 Steps = 0;
		while (State.Phase != EGridlockPhase::GameOver && Steps++ < 3000)
		{
			const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Fixture.Context);
			if (!TestTrue(FString::Printf(TEXT("Seed %d: always a legal action"), Seed), Legal.Num() > 0))
			{
				return false;
			}
			// Prefer real moves over EndPhase so games actually develop.
			const FGameAction& Action = Legal.Num() > 1 && Random.FRand() < 0.85f
				? Legal[Random.RandRange(0, Legal.Num() - 2)]
				: Legal.Last();

			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Fixture.Context, Events);

			int32 Cards = State.Deck.Num();
			for (const FPlayerData& Player : State.Players)
			{
				Cards += Player.Hand.Num() + Player.Completed.Num() + Player.PendingDraw.Num();
				if (Player.Cash < 0 || Player.Hand.Num() > Fixture.Context.Tuning->HandLimit)
				{
					AddError(FString::Printf(TEXT("Seed %d step %d: cash %d, hand %d"), Seed, Steps, Player.Cash, Player.Hand.Num()));
					return false;
				}
			}
			if (Cards != TotalCards)
			{
				AddError(FString::Printf(TEXT("Seed %d step %d: %d cards instead of %d"), Seed, Steps, Cards, TotalCards));
				return false;
			}
		}

		TestTrue(FString::Printf(TEXT("Seed %d: game ends"), Seed), State.Phase == EGridlockPhase::GameOver);
		GamesWithAWinner += State.Winner != INDEX_NONE ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("%d of 30 random games had a winner"), GamesWithAWinner));
	return true;
}

#endif

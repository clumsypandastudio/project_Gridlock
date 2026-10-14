#include "Misc/AutomationTest.h"
#include "Algo/Count.h"
#include "AI/AIPlayer.h"
#include "Data/AIPersonality.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "Model/RulesLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GridlockTicketToRideTests
{
	/** Rules v0.5 (Ticket to Ride) on map v0.5 and its deck. */
	struct FFixture
	{
		FRulesContext Context;
		UTuningData* Tuning = nullptr;

		/** Rules v0.5, v0.6 (tickets cost money), v0.7 (and Lots) or v0.8 (and city properties). */
		explicit FFixture(int32 Version = 5)
		{
			UMapDefinition* Map = NewObject<UMapDefinition>();
			Map->FillWithMapV05();
			URouteCardDeck* Deck = NewObject<URouteCardDeck>();
			Deck->FillWithDeckForMap(*Map);
			Tuning = NewObject<UTuningData>();
			if (Version >= 8)
			{
				Tuning->ApplyRulesV08();
			}
			else if (Version == 7)
			{
				Tuning->ApplyRulesV07();
			}
			else if (Version == 6)
			{
				Tuning->ApplyRulesV06();
			}
			else
			{
				Tuning->ApplyRulesV05();
			}
			Context.Map = Map;
			Context.Deck = Deck;
			Context.Tuning = Tuning;
		}

		/** Past Setup: each player keeps the first 2 of their 3 dealt Route cards. */
		FBoardState StartGame(int32 Seed) const
		{
			FBoardState State = URulesLibrary::MakeInitialState(Context, Seed);
			for (int32 Guard = 0; Guard < 4 && State.Phase == EGridlockPhase::Setup; ++Guard)
			{
				FGameAction Keep = Make(EGridlockActionType::KeepCards, State.ActivePlayer);
				const TArray<FName>& Pending = State.Players[State.ActivePlayer].PendingDraw;
				Keep.CardIds = { Pending[0], Pending[1] };
				TArray<FGameEvent> Events;
				State = URulesLibrary::Apply(State, Keep, Context, Events);
			}
			return State;
		}

		static FGameAction Make(EGridlockActionType Type, int32 Player, FName RouteId = NAME_None, int32 Slot = INDEX_NONE)
		{
			FGameAction Action;
			Action.Type = Type;
			Action.Player = Player;
			Action.RouteId = RouteId;
			Action.Slot = Slot;
			return Action;
		}

		bool IsLegal(const FBoardState& State, const FGameAction& Action) const
		{
			FText Reason;
			return URulesLibrary::IsLegal(State, Action, Context, Reason);
		}

		/** Applies the action for the active player if it's legal; false (and no change) otherwise. */
		bool Do(FBoardState& State, EGridlockActionType Type, FName RouteId = NAME_None, int32 Slot = INDEX_NONE, TArray<FGameEvent>* OutEvents = nullptr) const
		{
			const FGameAction Action = Make(Type, State.ActivePlayer, RouteId, Slot);
			if (!IsLegal(State, Action))
			{
				return false;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Context, Events);
			if (OutEvents)
			{
				OutEvents->Append(Events);
			}
			return true;
		}

		int32 TicketsInGame() const { return Tuning->TicketsPerColor * 8 + Tuning->WildTickets; }

		/** Every card of the ticket deck, Lots included, wherever it is: deck, discard, face-up, hands, or placed on routes. */
		static int32 AllCards(const FBoardState& State)
		{
			int32 Total = State.TicketDeck.Num() + State.TicketDiscard.Num() + State.FaceUp.Num();
			for (const FPlayerData& Player : State.Players)
			{
				for (const int32 Count : Player.Tickets)
				{
					Total += Count;
				}
			}
			for (const TPair<FName, FRouteLots>& Entry : State.RouteLots)
			{
				Total += Entry.Value.Owners.Num();
			}
			return Total;
		}

		static int32 TotalTickets(const FBoardState& State)
		{
			int32 Total = State.TicketDeck.Num() + State.TicketDiscard.Num() + State.FaceUp.Num();
			for (const FPlayerData& Player : State.Players)
			{
				Total += Player.TicketCount();
			}
			return Total;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRSetupTest, "Gridlock.TicketToRide.Setup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRSetupTest::RunTest(const FString& Parameters)
{
	const GridlockTicketToRideTests::FFixture Fixture;
	const FBoardState Dealt = URulesLibrary::MakeInitialState(Fixture.Context, 4);
	TestEqual(TEXT("3 Route cards dealt"), Dealt.Players[Dealt.FirstPlayer].PendingDraw.Num(), 3);
	FGameAction KeepOne = GridlockTicketToRideTests::FFixture::Make(EGridlockActionType::KeepCards, Dealt.FirstPlayer);
	KeepOne.CardIds = { Dealt.Players[Dealt.FirstPlayer].PendingDraw[0] };
	TestFalse(TEXT("Keeping 1 of 3 at the start is too few"), Fixture.IsLegal(Dealt, KeepOne));

	const FBoardState State = Fixture.StartGame(4);
	TestTrue(TEXT("Into the first turn"), State.Phase == EGridlockPhase::Start);
	TestEqual(TEXT("110 tickets in the game"), GridlockTicketToRideTests::FFixture::TotalTickets(State), 110);
	TestEqual(TEXT("5 face-up tickets"), State.FaceUp.Num(), 5);
	for (const FPlayerData& Player : State.Players)
	{
		TestEqual(TEXT("4 tickets each"), Player.TicketCount(), 4);
		TestEqual(TEXT("45 trains each"), Player.Trains, 45);
		TestEqual(TEXT("No money"), Player.Cash, 0);
		TestEqual(TEXT("No property cards"), Player.PropertyCardCount(), 0);
	}

	// Map v0.5: 8 colors, each with about 15 pins, and the same lengths as v0.3.
	TMap<EGridlockColor, int32> Pins;
	for (const FRouteDef& Route : Fixture.Context.Map->Routes)
	{
		Pins.FindOrAdd(Route.Color) += Route.Length;
	}
	for (const EGridlockColor Color : GridlockCardColors())
	{
		const int32 Count = Pins.FindRef(Color);
		TestTrue(FString::Printf(TEXT("Color %d has 14-16 pins (has %d)"), static_cast<int32>(Color), Count), Count >= 14 && Count <= 16);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRDrawTest, "Gridlock.TicketToRide.TakingTickets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRDrawTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture;

	// Two tickets: a face-up one, then one from the deck, and the turn passes.
	FBoardState State = Fixture.StartGame(7);
	const int32 Me = State.ActivePlayer;
	const int32 Plain = State.FaceUp.IndexOfByPredicate([](EGridlockColor Color) { return Color != EGridlockColor::Wild; });
	TestTrue(TEXT("Take a face-up ticket"), Fixture.Do(State, EGridlockActionType::TakeFaceUp, NAME_None, Plain));
	TestEqual(TEXT("Still my turn after 1"), State.ActivePlayer, Me);
	TestEqual(TEXT("The face-up row refilled"), State.FaceUp.Num(), 5);
	TestFalse(TEXT("No Route cards after taking a ticket"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::DrawCards, Me)));
	State.Players[Me].Tickets[static_cast<int32>(EGridlockColor::Red)] += 9;
	TestFalse(TEXT("No claiming after taking a ticket"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::ClaimRoute, Me, TEXT("R01"))));
	State.Players[Me].Tickets[static_cast<int32>(EGridlockColor::Red)] -= 9;
	const int32 Wild = State.FaceUp.IndexOfByKey(EGridlockColor::Wild);
	if (Wild != INDEX_NONE)
	{
		TestFalse(TEXT("A face-up Wild can't be the second ticket"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::TakeFaceUp, Me, NAME_None, Wild)));
	}
	TestTrue(TEXT("Take one from the deck"), Fixture.Do(State, EGridlockActionType::BuyTicket));
	TestEqual(TEXT("2 tickets end the turn"), State.ActivePlayer, 1 - Me);
	TestEqual(TEXT("Now 6 tickets"), State.Players[Me].TicketCount(), 6);
	TestEqual(TEXT("Tickets conserved"), FFixture::TotalTickets(State), 110);

	// A face-up Wild taken first is the whole draw.
	FBoardState WildFirst = Fixture.StartGame(8);
	const int32 DeckWild = WildFirst.TicketDeck.IndexOfByKey(EGridlockColor::Wild);
	Swap(WildFirst.FaceUp[0], WildFirst.TicketDeck[DeckWild]);
	const int32 First = WildFirst.ActivePlayer;
	TestTrue(TEXT("Take a face-up Wild"), Fixture.Do(WildFirst, EGridlockActionType::TakeFaceUp, NAME_None, 0));
	TestEqual(TEXT("...and the turn passes"), WildFirst.ActivePlayer, 1 - First);
	TestTrue(TEXT("Holding a Wild"), WildFirst.Players[First].TicketsOf(EGridlockColor::Wild) >= 1);

	// Three face-up Wilds: the row is discarded and dealt again.
	FBoardState Reset = Fixture.StartGame(9);
	Reset.FaceUp = { EGridlockColor::Wild, EGridlockColor::Wild, EGridlockColor::Red, EGridlockColor::Green, EGridlockColor::Blue };
	Reset.TicketDeck.Init(EGridlockColor::Black, 20);
	Reset.TicketDeck.Add(EGridlockColor::Wild); // the next card dealt, into the taken slot
	TArray<FGameEvent> Events;
	TestTrue(TEXT("Take the face-up Red"), Fixture.Do(Reset, EGridlockActionType::TakeFaceUp, NAME_None, 2, &Events));
	TestTrue(TEXT("Three Wilds reset the row"), Events.ContainsByPredicate([](const FGameEvent& Event) { return Event.Type == EGridlockEventType::FaceUpReset; }));
	TestTrue(TEXT("A fresh row of 5 with fewer than 3 Wilds"), Reset.FaceUp.Num() == 5 && Algo::Count(Reset.FaceUp, EGridlockColor::Wild) < 3);
	TestEqual(TEXT("The row went back into the deck, not the discard pile"), Reset.TicketDiscard.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRClaimAndEndTest, "Gridlock.TicketToRide.ClaimingAndTheEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRClaimAndEndTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture;
	FBoardState State = Fixture.StartGame(11);
	const int32 Me = State.ActivePlayer;
	const int32 Them = 1 - Me;
	State.Players[Me].Hand = { TEXT("C13"), TEXT("C01") };   // Westport - Highcliff (will be done), Westport - Frostpeak (won't)
	State.Players[Them].Hand.Reset();
	const int32 C13 = Fixture.Context.Deck->FindCard(TEXT("C13"))->Points;
	const int32 C01 = Fixture.Context.Deck->FindCard(TEXT("C01"))->Points;

	// Westport - Pinecrest: Red, 3 pins = 4 points and 3 trains; claiming is the whole turn.
	State.Players[Me].Tickets[static_cast<int32>(EGridlockColor::Red)] = 3;
	FGameAction Claim = FFixture::Make(EGridlockActionType::ClaimRoute, Me, TEXT("R01"));
	TestTrue(TEXT("Claim Westport - Pinecrest"), Fixture.IsLegal(State, Claim));
	TArray<FGameEvent> Events;
	State = URulesLibrary::Apply(State, Claim, Fixture.Context, Events);
	TestEqual(TEXT("4 points"), State.Players[Me].Points, 4);
	TestEqual(TEXT("42 trains left"), State.Players[Me].Trains, 42);
	TestEqual(TEXT("The turn passes"), State.ActivePlayer, Them);

	Fixture.Do(State, EGridlockActionType::BuyTicket);
	Fixture.Do(State, EGridlockActionType::BuyTicket);

	// Pinecrest - Highcliff: Blue, 2 pins. Down to 2 trains: one last turn each.
	State.Players[Me].Trains = 4;
	State.Players[Me].Tickets[static_cast<int32>(EGridlockColor::Blue)] += 2;
	TestFalse(TEXT("Too few trains for a 9-pin route"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::ClaimRoute, Me, TEXT("R09"))));
	TestTrue(TEXT("Claim Pinecrest - Highcliff"), Fixture.Do(State, EGridlockActionType::ClaimRoute, TEXT("R04")));
	TestEqual(TEXT("Last round: 2 turns left"), State.FinalTurnsLeft, 2);
	Fixture.Do(State, EGridlockActionType::BuyTicket);
	Fixture.Do(State, EGridlockActionType::BuyTicket);
	TestTrue(TEXT("Still going: my last turn"), State.Phase != EGridlockPhase::GameOver && State.ActivePlayer == Me);
	Fixture.Do(State, EGridlockActionType::BuyTicket);
	Fixture.Do(State, EGridlockActionType::BuyTicket);
	TestTrue(TEXT("Game over"), State.Phase == EGridlockPhase::GameOver);

	// 4 + 2 for routes, + Westport - Highcliff, - Westport - Frostpeak, + 10 for the longest route (5 pins against none).
	TestEqual(TEXT("Final score"), State.Players[Me].Points, 4 + 2 + C13 - C01 + 10);
	TestEqual(TEXT("Longest route"), URulesLibrary::GetLongestRoute(State, Fixture.Context, Me), 5);
	TestEqual(TEXT("Opponent scored nothing"), State.Players[Them].Points, 0);
	TestEqual(TEXT("Winner"), State.Winner, Me);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRRouteCardsTest, "Gridlock.TicketToRide.DrawingRouteCards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRRouteCardsTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture;
	FBoardState State = Fixture.StartGame(12);
	const int32 Me = State.ActivePlayer;
	const int32 HandBefore = State.Players[Me].Hand.Num();
	TestTrue(TEXT("Draw Route cards"), Fixture.Do(State, EGridlockActionType::DrawCards));
	TestEqual(TEXT("3 to choose from"), State.Players[Me].PendingDraw.Num(), 3);
	FGameAction KeepOne = FFixture::Make(EGridlockActionType::KeepCards, Me);
	KeepOne.CardIds = { State.Players[Me].PendingDraw[0] };
	TestTrue(TEXT("Keeping 1 is enough"), Fixture.IsLegal(State, KeepOne));
	TArray<FGameEvent> Events;
	State = URulesLibrary::Apply(State, KeepOne, Fixture.Context, Events);
	TestEqual(TEXT("One more Route card"), State.Players[Me].Hand.Num(), HandBefore + 1);
	TestEqual(TEXT("...and that was the turn"), State.ActivePlayer, 1 - Me);
	TestFalse(TEXT("No passing when there's something to do"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::EndPhase, State.ActivePlayer)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRPlayoutTest, "Gridlock.TicketToRide.RandomPlayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRPlayoutTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture;
	for (int32 Seed = 1; Seed <= 20; ++Seed)
	{
		FRandomStream Random(Seed * 7919);
		FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, Seed);
		for (int32 Step = 0; Step < 4000 && State.Phase != EGridlockPhase::GameOver; ++Step)
		{
			const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Fixture.Context);
			if (!TestTrue(FString::Printf(TEXT("Seed %d: always a legal action"), Seed), Legal.Num() > 0))
			{
				return false;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Legal[Random.RandRange(0, Legal.Num() - 1)], Fixture.Context, Events);
			bool bBad = FFixture::TotalTickets(State) != Fixture.TicketsInGame() || State.FaceUp.Num() > 5;
			for (const FPlayerData& Player : State.Players)
			{
				bBad |= Player.Trains < 0 || Player.Cash != 0;
				for (const int32 Count : Player.Tickets)
				{
					bBad |= Count < 0;
				}
			}
			if (bBad)
			{
				FString Details;
				for (const FPlayerData& Player : State.Players)
				{
					Details += FString::Printf(TEXT(" [trains %d, cash %d, tickets"), Player.Trains, Player.Cash);
					for (const int32 Count : Player.Tickets)
					{
						Details += FString::Printf(TEXT(" %d"), Count);
					}
					Details += TEXT("]");
				}
				AddError(FString::Printf(TEXT("Seed %d step %d: %d tickets (expected %d), %d face-up;%s"), Seed, Step,
					FFixture::TotalTickets(State), Fixture.TicketsInGame(), State.FaceUp.Num(), *Details));
				return false;
			}
		}
		TestTrue(FString::Printf(TEXT("Seed %d: the game ends"), Seed), State.Phase == EGridlockPhase::GameOver);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRAITest, "Gridlock.TicketToRide.AI",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRAITest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture;
	UAIPersonality* Builder = NewObject<UAIPersonality>();
	Builder->ApplyPreset(EAIStyle::Builder, EAIDifficulty::Normal);
	UAIPersonality* Easy = NewObject<UAIPersonality>();
	Easy->ApplyPreset(EAIStyle::Builder, EAIDifficulty::Easy);

	// Seat 0 plays A (nullptr = random moves), seat 1 plays B; seats swap every game.
	auto Play = [&Fixture](int32 Seed, const UAIPersonality* Seat0, const UAIPersonality* Seat1, int32& OutRounds, bool& bOutByTrains, bool& bOutLegal,
		int32& OutCompleted, int32& OutFailed, int32 OutPoints[2])
	{
		FRandomStream Random(Seed * 31);
		FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, Seed);
		bOutLegal = true;
		for (int32 Step = 0; Step < 4000 && State.Phase != EGridlockPhase::GameOver; ++Step)
		{
			const UAIPersonality* Personality = State.ActivePlayer == 0 ? Seat0 : Seat1;
			FGameAction Action;
			if (Personality)
			{
				Action = UAIPlayer::ChooseFor(State, Fixture.Context, *Personality, State.ActivePlayer);
			}
			else
			{
				const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Fixture.Context);
				Action = Legal[Random.RandRange(0, Legal.Num() - 1)];
			}
			FText Reason;
			if (!URulesLibrary::IsLegal(State, Action, Fixture.Context, Reason))
			{
				bOutLegal = false;
				break;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Fixture.Context, Events);
			for (const FGameEvent& Event : Events)
			{
				bOutByTrains |= Event.Type == EGridlockEventType::FinalRound;
				OutCompleted += Event.Type == EGridlockEventType::CardScored ? 1 : 0;
				OutFailed += Event.Type == EGridlockEventType::CardFailed ? 1 : 0;
			}
		}
		OutRounds = State.Round;
		OutPoints[0] = State.Players[0].Points;
		OutPoints[1] = State.Players[1].Points;
		return State.Winner;
	};

	struct FMatch
	{
		const TCHAR* Name;
		const UAIPersonality* A;
		const UAIPersonality* B;
	};
	for (const FMatch& Match : { FMatch{ TEXT("Normal vs random"), Builder, nullptr }, FMatch{ TEXT("Normal vs Easy"), Builder, Easy },
		FMatch{ TEXT("Normal vs Normal"), Builder, Builder } })
	{
		const int32 Games = 20;
		int32 WinsA = 0;
		int32 Rounds = 0;
		int32 ByTrains = 0;
		int32 Completed = 0;
		int32 Failed = 0;
		int32 Points = 0;
		for (int32 Game = 0; Game < Games; ++Game)
		{
			const bool bASeat0 = Game % 2 == 0;
			int32 GameRounds = 0;
			bool bByTrains = false;
			bool bLegal = true;
			int32 GamePoints[2] = { 0, 0 };
			const int32 Winner = Play(500 + Game, bASeat0 ? Match.A : Match.B, bASeat0 ? Match.B : Match.A, GameRounds, bByTrains, bLegal, Completed, Failed, GamePoints);
			TestTrue(FString::Printf(TEXT("%s game %d: every move legal"), Match.Name, Game), bLegal);
			WinsA += Winner == (bASeat0 ? 0 : 1) ? 1 : 0;
			Rounds += GameRounds;
			ByTrains += bByTrains ? 1 : 0;
			Points += GamePoints[0] + GamePoints[1];
		}
		AddInfo(FString::Printf(TEXT("TTR %s: A won %d of %d | %.1f rounds | ended by trains %d | Route cards finished %.1f, unfinished %.1f per game | %.0f points per player"),
			Match.Name, WinsA, Games, static_cast<float>(Rounds) / Games, ByTrains, static_cast<float>(Completed) / Games,
			static_cast<float>(Failed) / Games, static_cast<float>(Points) / (2 * Games)));
		if (Match.B == nullptr)
		{
			TestTrue(FString::Printf(TEXT("Normal beats random at least 80%% (won %d of %d)"), WinsA, Games), WinsA >= Games * 8 / 10);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRBuyingTest, "Gridlock.TicketToRide.BuyingTickets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRBuyingTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	FFixture Fixture(6);

	// Unlimited funds: every ticket costs $1, which only adds up in Spent.
	FBoardState State = Fixture.StartGame(21);
	const int32 Me = State.ActivePlayer;
	TestEqual(TEXT("Price of a face-up ticket"), URulesLibrary::PreviewCost(State, FFixture::Make(EGridlockActionType::TakeFaceUp, Me, NAME_None, 0), Fixture.Context), 1);
	const int32 Plain = State.FaceUp.IndexOfByPredicate([](EGridlockColor Color) { return Color != EGridlockColor::Wild; });
	TestTrue(TEXT("Buy a face-up ticket with $0 (unlimited)"), Fixture.Do(State, EGridlockActionType::TakeFaceUp, NAME_None, Plain));
	TestTrue(TEXT("Buy one from the deck"), Fixture.Do(State, EGridlockActionType::BuyTicket));
	TestEqual(TEXT("Spent $2"), State.Players[Me].Spent, 2);
	TestEqual(TEXT("Cash untouched"), State.Players[Me].Cash, 0);
	TestEqual(TEXT("Turn passed"), State.ActivePlayer, 1 - Me);

	// Limited funds: $1 buys one ticket, and the turn ends there.
	Fixture.Tuning->bUnlimitedCash = false;
	FBoardState Tight = Fixture.StartGame(22);
	const int32 Buyer = Tight.ActivePlayer;
	Tight.Players[Buyer].Cash = 1;
	TestTrue(TEXT("Buy 1 ticket with $1"), Fixture.Do(Tight, EGridlockActionType::BuyTicket));
	TestEqual(TEXT("$0 left"), Tight.Players[Buyer].Cash, 0);
	TestEqual(TEXT("Out of money: the turn ends after 1"), Tight.ActivePlayer, 1 - Buyer);
	Tight.ActivePlayer = Buyer;
	Tight.BuysThisTurn = 0;
	Tight.TicketBuysThisTurn = 0;
	FText Reason;
	TestFalse(TEXT("No money, no ticket"), URulesLibrary::IsLegal(Tight, FFixture::Make(EGridlockActionType::BuyTicket, Buyer), Fixture.Context, Reason));
	TestFalse(TEXT("...face-up either"), Fixture.IsLegal(Tight, FFixture::Make(EGridlockActionType::TakeFaceUp, Buyer, NAME_None, 0)));
	TestTrue(TEXT("Route cards are still free"), Fixture.IsLegal(Tight, FFixture::Make(EGridlockActionType::DrawCards, Buyer)));
	return true;
}

// Not a pass/fail check beyond legal moves: how much AI players spend, to pick real starting money from.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRSpendingReportTest, "Gridlock.TicketToRide.SpendingReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRSpendingReportTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture(6);
	UAIPersonality* Builder = NewObject<UAIPersonality>();
	Builder->ApplyPreset(EAIStyle::Builder, EAIDifficulty::Normal);

	const int32 Games = 20;
	TArray<int32> Spending;
	int32 Rounds = 0;
	for (int32 Game = 0; Game < Games; ++Game)
	{
		FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, 700 + Game);
		TArray<int32> SpentByRound;
		for (int32 Step = 0; Step < 4000 && State.Phase != EGridlockPhase::GameOver; ++Step)
		{
			const FGameAction Action = UAIPlayer::ChooseFor(State, Fixture.Context, *Builder, State.ActivePlayer);
			FText Reason;
			if (!TestTrue(TEXT("Every AI move legal"), URulesLibrary::IsLegal(State, Action, Fixture.Context, Reason)))
			{
				return false;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Fixture.Context, Events);
		}
		Rounds += State.Round;
		Spending.Add(State.Players[0].Spent);
		Spending.Add(State.Players[1].Spent);
	}
	Spending.Sort();
	int32 Total = 0;
	for (const int32 Spent : Spending)
	{
		Total += Spent;
	}
	AddInfo(FString::Printf(TEXT("TTR SPENDING per player over a whole game (%d players, %.1f rounds on average): average $%.1f, lowest $%d, middle $%d, highest $%d"),
		Spending.Num(), static_cast<float>(Rounds) / Games, static_cast<float>(Total) / Spending.Num(), Spending[0], Spending[Spending.Num() / 2], Spending.Last()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRLotsTest, "Gridlock.TicketToRide.Lots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRLotsTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture(7);
	auto MoveToFaceUp = [](FBoardState& State, EGridlockColor Color, int32 Slot)
	{
		const int32 InDeck = State.TicketDeck.IndexOfByKey(Color);
		Swap(State.FaceUp[Slot], State.TicketDeck[InDeck]);
	};

	FBoardState Start = Fixture.StartGame(31);
	TestEqual(TEXT("110 tickets + 12 Lots"), FFixture::AllCards(Start), 122);

	// A face-up Lot: $4, and that's the turn.
	FBoardState FaceUp = Start;
	const int32 Me = FaceUp.ActivePlayer;
	MoveToFaceUp(FaceUp, EGridlockColor::Lot, 0);
	TestEqual(TEXT("A face-up Lot costs $4"), URulesLibrary::PreviewCost(FaceUp, FFixture::Make(EGridlockActionType::TakeFaceUp, Me, NAME_None, 0), Fixture.Context), 4);
	TestTrue(TEXT("Buy the face-up Lot"), Fixture.Do(FaceUp, EGridlockActionType::TakeFaceUp, NAME_None, 0));
	TestEqual(TEXT("Spent $4"), FaceUp.Players[Me].Spent, 4);
	TestEqual(TEXT("Holding a Lot card"), FaceUp.Players[Me].LotCardCount(), 1);
	TestEqual(TEXT("...and the turn passed"), FaceUp.ActivePlayer, 1 - Me);

	// A face-up Wild costs $2; neither a Wild nor a Lot can be the second pick.
	FBoardState Second = Start;
	MoveToFaceUp(Second, EGridlockColor::Wild, 0);
	MoveToFaceUp(Second, EGridlockColor::Lot, 1);
	TestEqual(TEXT("A face-up Wild costs $2"), URulesLibrary::PreviewCost(Second, FFixture::Make(EGridlockActionType::TakeFaceUp, Me, NAME_None, 0), Fixture.Context), 2);
	TestTrue(TEXT("A ticket from the deck first"), Fixture.Do(Second, EGridlockActionType::BuyTicket) || Second.Players[Me].bLotPending);
	if (!Second.Players[Me].bLotPending)
	{
		TestFalse(TEXT("No face-up Lot as the second pick"), Fixture.IsLegal(Second, FFixture::Make(EGridlockActionType::TakeFaceUp, Me, NAME_None, 1)));
	}

	// Wilds and Lots together reset the face-up row: 2 Wilds + 1 Lot, 2 Lots + 1 Wild, 3 Lots.
	const TArray<TArray<EGridlockColor>> Mixes = {
		{ EGridlockColor::Wild, EGridlockColor::Wild, EGridlockColor::Lot },
		{ EGridlockColor::Lot, EGridlockColor::Lot, EGridlockColor::Wild },
		{ EGridlockColor::Lot, EGridlockColor::Lot, EGridlockColor::Lot } };
	for (const TArray<EGridlockColor>& Mix : Mixes)
	{
		FBoardState Row = Start;
		Row.FaceUp = { Mix[0], Mix[1], EGridlockColor::Red, EGridlockColor::Green, EGridlockColor::Blue };
		Row.TicketDeck.Init(EGridlockColor::Black, 20);
		Row.TicketDeck.Add(Mix[2]); // dealt into the taken slot: the third special card
		TArray<FGameEvent> MixEvents;
		TestTrue(TEXT("Take the face-up Red"), Fixture.Do(Row, EGridlockActionType::TakeFaceUp, NAME_None, 2, &MixEvents));
		const int32 Specials = Algo::Count(Row.FaceUp, EGridlockColor::Wild) + Algo::Count(Row.FaceUp, EGridlockColor::Lot);
		TestTrue(TEXT("3 Wilds/Lots in any mix reset the row"), MixEvents.ContainsByPredicate([](const FGameEvent& Event) { return Event.Type == EGridlockEventType::FaceUpReset; }));
		TestTrue(TEXT("...into a row of 5 with fewer than 3 of them"), Row.FaceUp.Num() == 5 && Specials < 3);
	}
	FBoardState TwoOnly = Start;
	TwoOnly.FaceUp = { EGridlockColor::Wild, EGridlockColor::Lot, EGridlockColor::Red, EGridlockColor::Green, EGridlockColor::Blue };
	TwoOnly.TicketDeck.Init(EGridlockColor::Black, 21);
	TArray<FGameEvent> TwoEvents;
	Fixture.Do(TwoOnly, EGridlockActionType::TakeFaceUp, NAME_None, 2, &TwoEvents);
	TestFalse(TEXT("1 Wild + 1 Lot is fine"), TwoEvents.ContainsByPredicate([](const FGameEvent& Event) { return Event.Type == EGridlockEventType::FaceUpReset; }));

	// A Lot off the deck: $1, then keep it for $3 more or discard it; either way it's one of the 2 tickets.
	FBoardState Blind = Start;
	Blind.TicketDeck.Swap(Blind.TicketDeck.IndexOfByKey(EGridlockColor::Lot), Blind.TicketDeck.Num() - 1);
	FBoardState Discard = Blind;
	TestTrue(TEXT("Draw from the deck"), Fixture.Do(Blind, EGridlockActionType::BuyTicket));
	TestTrue(TEXT("It's a Lot: keep or discard"), Blind.Players[Me].bLotPending);
	TestFalse(TEXT("Nothing else until then"), Fixture.IsLegal(Blind, FFixture::Make(EGridlockActionType::BuyTicket, Me)));
	TestTrue(TEXT("Keep it"), Fixture.Do(Blind, EGridlockActionType::KeepLot));
	TestEqual(TEXT("$1 + $3"), Blind.Players[Me].Spent, 4);
	TestTrue(TEXT("Still my turn, one ticket to go"), Blind.ActivePlayer == Me && Blind.Players[Me].LotCardCount() == 1);
	Fixture.Do(Discard, EGridlockActionType::BuyTicket);
	TestTrue(TEXT("Discard it"), Fixture.Do(Discard, EGridlockActionType::DiscardLot));
	TestTrue(TEXT("Discarded: no Lot, $1 spent, still my turn"), Discard.Players[Me].LotCardCount() == 0 && Discard.Players[Me].Spent == 1 && Discard.ActivePlayer == Me);
	TestEqual(TEXT("Cards conserved"), FFixture::AllCards(Discard), 122);

	// Placing a Lot ends the turn: one a turn. Pinecrest - Highcliff (2 pins) has 1 slot; Westport - Ironford (4 pins) has 2.
	auto BackToMe = [Me](FBoardState& State)
	{
		State.ActivePlayer = Me;
		State.BuysThisTurn = 0;
		State.TicketBuysThisTurn = 0;
	};
	FBoardState Place = Blind;
	Place.Players[Me].Tickets[static_cast<int32>(EGridlockColor::Lot)] = 3;
	TestTrue(TEXT("Place a Lot on Pinecrest - Highcliff"), Fixture.Do(Place, EGridlockActionType::PlaceRouteLot, TEXT("R04")));
	TestEqual(TEXT("Placing ends the turn"), Place.ActivePlayer, 1 - Me);
	TestEqual(TEXT("1 of 1: blocked"), URulesLibrary::GetRouteBlocker(Place, Fixture.Context, TEXT("R04")), Me);
	BackToMe(Place);
	TestFalse(TEXT("No room for another"), Fixture.IsLegal(Place, FFixture::Make(EGridlockActionType::PlaceRouteLot, Me, TEXT("R04"))));
	TestTrue(TEXT("One Lot on Westport - Ironford"), Fixture.Do(Place, EGridlockActionType::PlaceRouteLot, TEXT("R03")));
	TestEqual(TEXT("1 of 2: not blocked yet"), URulesLibrary::GetRouteBlocker(Place, Fixture.Context, TEXT("R03")), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Another turn gone"), Place.ActivePlayer, 1 - Me);

	// The opponent can't build the blocked route; its owner can.
	FBoardState Theirs = Place;
	Theirs.ActivePlayer = 1 - Me;
	Theirs.BuysThisTurn = 0;
	Theirs.TicketBuysThisTurn = 0;
	Theirs.Players[1 - Me].Tickets[static_cast<int32>(EGridlockColor::Blue)] = 5;
	TestFalse(TEXT("Blocked for the opponent"), Fixture.IsLegal(Theirs, FFixture::Make(EGridlockActionType::ClaimRoute, 1 - Me, TEXT("R04"))));
	Theirs.Players[1 - Me].Tickets[static_cast<int32>(EGridlockColor::Lot)] = 1;
	TestTrue(TEXT("The opponent takes the other slot on Westport - Ironford"), Fixture.Do(Theirs, EGridlockActionType::PlaceRouteLot, TEXT("R03")));
	TestEqual(TEXT("Mixed owners: nobody blocks it"), URulesLibrary::GetRouteBlocker(Theirs, Fixture.Context, TEXT("R03")), static_cast<int32>(INDEX_NONE));
	FBoardState Mine = Place;
	BackToMe(Mine);
	Mine.Players[Me].Tickets[static_cast<int32>(EGridlockColor::Blue)] = 5;
	TestTrue(TEXT("The owner can build it"), Fixture.IsLegal(Mine, FFixture::Make(EGridlockActionType::ClaimRoute, Me, TEXT("R04"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRLotsPlayoutTest, "Gridlock.TicketToRide.LotsPlayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRLotsPlayoutTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture(7);
	UAIPersonality* Builder = NewObject<UAIPersonality>();
	Builder->ApplyPreset(EAIStyle::Builder, EAIDifficulty::Normal);

	// Random moves (seeds 1-15), then AI against AI (seeds 101-115): always legal, cards conserved, the game ends.
	int32 Placed = 0;
	int32 Blocks = 0;
	int32 Rounds = 0;
	int32 Spent = 0;
	for (int32 Seed = 1; Seed <= 115; Seed = Seed == 15 ? 101 : Seed + 1)
	{
		const bool bAI = Seed > 100;
		FRandomStream Random(Seed * 92821);
		FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, Seed);
		for (int32 Step = 0; Step < 5000 && State.Phase != EGridlockPhase::GameOver; ++Step)
		{
			const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Fixture.Context);
			if (!TestTrue(FString::Printf(TEXT("Seed %d: always a legal action"), Seed), Legal.Num() > 0))
			{
				return false;
			}
			const FGameAction Action = bAI ? UAIPlayer::ChooseFor(State, Fixture.Context, *Builder, State.ActivePlayer) : Legal[Random.RandRange(0, Legal.Num() - 1)];
			FText Reason;
			if (!TestTrue(FString::Printf(TEXT("Seed %d step %d: the move is legal (%s)"), Seed, Step, *Reason.ToString()), URulesLibrary::IsLegal(State, Action, Fixture.Context, Reason)))
			{
				return false;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Fixture.Context, Events);
			for (const FGameEvent& Event : Events)
			{
				Placed += bAI && Event.Type == EGridlockEventType::RouteLotPlaced ? 1 : 0;
				Blocks += bAI && Event.Type == EGridlockEventType::RouteBlocked ? 1 : 0;
			}
			if (FFixture::AllCards(State) != 122)
			{
				AddError(FString::Printf(TEXT("Seed %d step %d: %d cards (expected 122)"), Seed, Step, FFixture::AllCards(State)));
				return false;
			}
		}
		TestTrue(FString::Printf(TEXT("Seed %d: the game ends"), Seed), State.Phase == EGridlockPhase::GameOver);
		if (bAI)
		{
			Rounds += State.Round;
			Spent += State.Players[0].Spent + State.Players[1].Spent;
		}
	}
	AddInfo(FString::Printf(TEXT("TTR LOTS (AI vs AI, 15 games): %.1f Lots placed and %.1f routes blocked per game, %.1f rounds, $%.1f spent per player"),
		Placed / 15.0f, Blocks / 15.0f, Rounds / 15.0f, Spent / 30.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRCityPropertiesTest, "Gridlock.TicketToRide.CityProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRCityPropertiesTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture(8);
	FBoardState State = Fixture.StartGame(41);
	const int32 Me = State.ActivePlayer;
	const int32 Them = 1 - Me;
	State.Players[Me].Hand.Reset();
	State.Players[Them].Hand = { TEXT("C13") }; // Westport - Highcliff

	// Only where your routes reach: nothing before Pinecrest - Highcliff is mine.
	TestFalse(TEXT("No route into Highcliff yet"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::BuyCityProperty, Me, TEXT("Highcliff"))));
	State.RouteOwner[TEXT("R04")] = Me;
	TestTrue(TEXT("Highcliff now"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::BuyCityProperty, Me, TEXT("Highcliff"))));
	TestTrue(TEXT("Pinecrest too"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::BuyCityProperty, Me, TEXT("Pinecrest"))));
	TestEqual(TEXT("A property costs $10"), URulesLibrary::PreviewCost(State, FFixture::Make(EGridlockActionType::BuyCityProperty, Me, TEXT("Pinecrest")), Fixture.Context), 10);
	TestTrue(TEXT("Buy Pinecrest"), Fixture.Do(State, EGridlockActionType::BuyCityProperty, TEXT("Pinecrest")));
	TestTrue(TEXT("Mine, $10 spent, and that was the turn"), State.CityOwner.FindRef(TEXT("Pinecrest")) == Me && State.Players[Me].Spent == 10 && State.ActivePlayer == Them);

	// The opponent builds Westport - Pinecrest into my city: a $3 toll.
	State.Players[Them].Tickets[static_cast<int32>(EGridlockColor::Red)] = 3;
	TestTrue(TEXT("They build Westport - Pinecrest"), Fixture.Do(State, EGridlockActionType::ClaimRoute, TEXT("R01")));
	TestTrue(TEXT("...and pay a $3 toll"), State.Players[Them].Spent == 3 && State.Players[Me].Earned == 3);
	TestFalse(TEXT("Pinecrest is taken"), Fixture.IsLegal(State, FFixture::Make(EGridlockActionType::BuyCityProperty, State.ActivePlayer, TEXT("Pinecrest"))));

	// At the end, their finished Westport - Highcliff card runs through Pinecrest: $3 again.
	State.RouteOwner[TEXT("R04")] = Them; // their Pinecrest - Highcliff, so the card is done through Pinecrest
	State.FinalTurnsLeft = 1;
	TArray<FGameEvent> Events;
	Fixture.Do(State, EGridlockActionType::BuyTicket, NAME_None, INDEX_NONE, &Events);
	Fixture.Do(State, EGridlockActionType::BuyTicket, NAME_None, INDEX_NONE, &Events);
	TestTrue(TEXT("Game over"), State.Phase == EGridlockPhase::GameOver);
	TestTrue(TEXT("The card's path paid the toll"), Events.ContainsByPredicate([Them](const FGameEvent& Event)
	{
		return Event.Type == EGridlockEventType::CityTollPaid && Event.Player == Them && Event.CardId == TEXT("C13") && Event.RouteId == TEXT("Pinecrest");
	}));
	TestEqual(TEXT("$6 earned in all"), State.Players[Me].Earned, 6);

	// A property is a whole turn: not after taking a ticket.
	FBoardState Ticket = Fixture.StartGame(42);
	const int32 Buyer = Ticket.ActivePlayer;
	Ticket.RouteOwner[TEXT("R04")] = Buyer;
	Fixture.Do(Ticket, EGridlockActionType::BuyTicket);
	if (!Ticket.Players[Buyer].bLotPending && Ticket.ActivePlayer == Buyer)
	{
		TestFalse(TEXT("No property after a ticket"), Fixture.IsLegal(Ticket, FFixture::Make(EGridlockActionType::BuyCityProperty, Buyer, TEXT("Highcliff"))));
	}

	// New Route cards only once all yours are finished, or can't be finished any more.
	FBoardState Cards = Fixture.StartGame(43);
	const int32 Holder = Cards.ActivePlayer;
	Cards.Players[Holder].Hand = { TEXT("C13") }; // Westport - Highcliff
	TestFalse(TEXT("No drawing with an unfinished card"), Fixture.IsLegal(Cards, FFixture::Make(EGridlockActionType::DrawCards, Holder)));
	FBoardState Done = Cards;
	Done.RouteOwner[TEXT("R01")] = Holder;
	Done.RouteOwner[TEXT("R04")] = Holder;
	TestTrue(TEXT("Drawing once it's finished"), Fixture.IsLegal(Done, FFixture::Make(EGridlockActionType::DrawCards, Holder)));
	FBoardState Stuck = Cards;
	for (const FName RouteId : { FName(TEXT("R01")), FName(TEXT("R02")), FName(TEXT("R03")) }) // every way out of Westport
	{
		Stuck.RouteOwner[RouteId] = 1 - Holder;
	}
	TestFalse(TEXT("The card can't be finished any more"), URulesLibrary::CanStillComplete(Stuck, Fixture.Context, Holder, TEXT("C13")));
	TestFalse(TEXT("...and still no drawing: no way out"), Fixture.IsLegal(Stuck, FFixture::Make(EGridlockActionType::DrawCards, Holder)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockTtRCityPlayoutTest, "Gridlock.TicketToRide.CityPropertiesPlayouts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockTtRCityPlayoutTest::RunTest(const FString& Parameters)
{
	using FFixture = GridlockTicketToRideTests::FFixture;
	const FFixture Fixture(8);
	UAIPersonality* Builder = NewObject<UAIPersonality>();
	Builder->ApplyPreset(EAIStyle::Builder, EAIDifficulty::Normal);

	// Random moves (seeds 1-15), then AI against AI (seeds 101-115): always legal, cards conserved, the game ends.
	int32 Bought = 0;
	int32 Tolls = 0;
	int32 TollCash = 0;
	int32 Rounds = 0;
	int32 Profit = 0;
	int32 CardsFinished = 0;
	int32 CardsFailed = 0;
	int32 LeastFinished = MAX_int32;
	for (int32 Seed = 1; Seed <= 115; Seed = Seed == 15 ? 101 : Seed + 1)
	{
		const bool bAI = Seed > 100;
		FRandomStream Random(Seed * 15485863);
		FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, Seed);
		for (int32 Step = 0; Step < 5000 && State.Phase != EGridlockPhase::GameOver; ++Step)
		{
			const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Fixture.Context);
			if (!TestTrue(FString::Printf(TEXT("Seed %d: always a legal action"), Seed), Legal.Num() > 0))
			{
				return false;
			}
			const FGameAction Action = bAI ? UAIPlayer::ChooseFor(State, Fixture.Context, *Builder, State.ActivePlayer) : Legal[Random.RandRange(0, Legal.Num() - 1)];
			FText Reason;
			if (!TestTrue(FString::Printf(TEXT("Seed %d step %d: the move is legal"), Seed, Step), URulesLibrary::IsLegal(State, Action, Fixture.Context, Reason)))
			{
				return false;
			}
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Fixture.Context, Events);
			for (const FGameEvent& Event : Events)
			{
				Bought += bAI && Event.Type == EGridlockEventType::CityPropertyBought ? 1 : 0;
				Tolls += bAI && Event.Type == EGridlockEventType::CityTollPaid ? 1 : 0;
				TollCash += bAI && Event.Type == EGridlockEventType::CityTollPaid ? Event.Amount : 0;
			}
			if (FFixture::AllCards(State) != 122)
			{
				AddError(FString::Printf(TEXT("Seed %d step %d: %d cards (expected 122)"), Seed, Step, FFixture::AllCards(State)));
				return false;
			}
		}
		TestTrue(FString::Printf(TEXT("Seed %d: the game ends"), Seed), State.Phase == EGridlockPhase::GameOver);
		if (bAI)
		{
			Rounds += State.Round;
			Profit += State.Players[0].Earned - State.Players[0].Spent + State.Players[1].Earned - State.Players[1].Spent;
			for (const FPlayerData& Player : State.Players)
			{
				CardsFinished += Player.Completed.Num();
				CardsFailed += Player.Hand.Num();
				LeastFinished = FMath::Min(LeastFinished, Player.Completed.Num());
			}
		}
	}
	AddInfo(FString::Printf(TEXT("TTR CITIES (AI vs AI, 15 games): %.1f properties bought, %.1f tolls ($%.1f) per game, %.1f rounds, profit $%.1f per player"),
		Bought / 15.0f, Tolls / 15.0f, TollCash / 15.0f, Rounds / 15.0f, Profit / 30.0f));
	AddInfo(FString::Printf(TEXT("TTR CITIES Route cards per player: %.1f finished, %.1f unfinished (fewest finished in a game: %d)"),
		CardsFinished / 30.0f, CardsFailed / 30.0f, LeastFinished));
	return true;
}

#endif
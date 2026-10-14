#include "Misc/AutomationTest.h"
#include "AI/AIPlayer.h"
#include "AI/AIScoring.h"
#include "Data/AIPersonality.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "HAL/PlatformTime.h"
#include "Model/RulesLibrary.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GridlockAITests
{
	/** Rules v0.4 on map and deck v0.3 (the game's default), plus the v0.1 map and deck for older rules. */
	struct FFixture
	{
		FRulesContext Context;
		const UMapDefinition* MapV01 = nullptr;
		const URouteCardDeck* DeckV01 = nullptr;

		FFixture()
		{
			UMapDefinition* Map = NewObject<UMapDefinition>();
			Map->FillWithMapV03();
			URouteCardDeck* Deck = NewObject<URouteCardDeck>();
			Deck->FillWithDeckForMap(*Map);
			Context.Map = Map;
			Context.Deck = Deck;
			Context.Tuning = NewObject<UTuningData>();

			UMapDefinition* OldMap = NewObject<UMapDefinition>();
			OldMap->FillWithMapV01();
			URouteCardDeck* OldDeck = NewObject<URouteCardDeck>();
			OldDeck->FillWithDeckV01();
			MapV01 = OldMap;
			DeckV01 = OldDeck;
		}

		/** The context for a tuning: the v0.3 map and deck for the card economy, v0.1 otherwise. */
		FRulesContext For(const UTuningData* Tuning) const
		{
			FRulesContext Result = Context;
			Result.Tuning = Tuning;
			if (!Tuning->bCardEconomy)
			{
				Result.Map = MapV01;
				Result.Deck = DeckV01;
			}
			return Result;
		}
	};

	UAIPersonality* MakePersonality(EAIStyle Style, EAIDifficulty Difficulty)
	{
		UAIPersonality* Personality = NewObject<UAIPersonality>();
		Personality->ApplyPreset(Style, Difficulty);
		return Personality;
	}

	/** A seat: an AI personality, or nullptr for a random player. */
	struct FSeat
	{
		const UAIPersonality* Personality = nullptr;
	};

	struct FGameResult
	{
		int32 Winner = INDEX_NONE;
		int32 Rounds = 0;
		bool bAllLegal = true;
		int32 Decisions = 0;
		double DecisionSeconds = 0.0;
		double SlowestDecision = 0.0;

		/** How the game ended: 5 cards, 40 points, or the turn limit. */
		bool bEndedByCards = false;
		bool bEndedByPoints = false;

		/** Completed cards by type, both players together: Short, Medium, Long. */
		int32 CompletedByType[3] = { 0, 0, 0 };

		/** Upgrades that finished at Complex or Landmark, and properties removed. */
		int32 BigProperties = 0;
		int32 Removals = 0;

		/** Property activity: Lots placed, upgrades started (to Building, Complex, Landmark), removals by tier (Lot..Landmark). */
		int32 LotsPlaced = 0;
		int32 UpgradesTo[3] = { 0, 0, 0 };
		int32 RemovedAtTier[4] = { 0, 0, 0, 0 };
		int32 RemovedMidUpgrade = 0;
		int32 Demolishes = 0;

		/** Lots placed on a route the opponent owns, the placer owns, or nobody owns. */
		int32 LotsOnOpponentRoute = 0;
		int32 LotsOnOwnRoute = 0;
		int32 LotsOnOpenRoute = 0;

		/** Money: both players' cash at the end (summed), tolls paid and properties sold during the game. */
		int32 FinalCash = 0;
		int32 TollsPaid = 0;
		int32 Sales = 0;
	};

	FGameResult PlayGame(const FRulesContext& Context, int32 Seed, const FSeat Seats[2])
	{
		FGameResult Result;
		FRandomStream Random(Seed * 104729);
		FBoardState State = URulesLibrary::MakeInitialState(Context, Seed);

		for (int32 Step = 0; Step < 4000 && State.Phase != EGridlockPhase::GameOver; ++Step)
		{
			const int32 Player = State.ActivePlayer;
			FGameAction Action;
			if (const UAIPersonality* Personality = Seats[Player].Personality)
			{
				const double Start = FPlatformTime::Seconds();
				Action = UAIPlayer::ChooseFor(State, Context, *Personality, Player);
				const double Took = FPlatformTime::Seconds() - Start;
				Result.DecisionSeconds += Took;
				Result.SlowestDecision = FMath::Max(Result.SlowestDecision, Took);
				++Result.Decisions;
			}
			else
			{
				const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Context);
				Action = Legal.Num() > 1 && Random.FRand() < 0.85f ? Legal[Random.RandRange(0, Legal.Num() - 2)] : Legal.Last();
			}

			FText Reason;
			if (!URulesLibrary::IsLegal(State, Action, Context, Reason))
			{
				Result.bAllLegal = false;
				break;
			}

			if (const FSlotState* Slot = State.Slots.Find(Action.RouteId))
			{
				const int32 RouteOwner = State.RouteOwner.FindRef(Action.RouteId);
				switch (Action.Type)
				{
				case EGridlockActionType::PlaceLot:
					++Result.LotsPlaced;
					Result.LotsOnOpponentRoute += RouteOwner != INDEX_NONE && RouteOwner != Player ? 1 : 0;
					Result.LotsOnOwnRoute += RouteOwner == Player ? 1 : 0;
					Result.LotsOnOpenRoute += RouteOwner == INDEX_NONE ? 1 : 0;
					break;
				case EGridlockActionType::Upgrade:
					++Result.UpgradesTo[FMath::Clamp(static_cast<int32>(Slot->Tier) - 1, 0, 2)];
					break;
				case EGridlockActionType::Buyout:
				case EGridlockActionType::Demolish:
					++Result.RemovedAtTier[FMath::Clamp(static_cast<int32>(Slot->Tier) - 1, 0, 3)];
					Result.RemovedMidUpgrade += Slot->bUpgrading ? 1 : 0;
					Result.Demolishes += Action.Type == EGridlockActionType::Demolish ? 1 : 0;
					break;
				default:
					break;
				}
			}

			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Context, Events);
			for (const FGameEvent& Event : Events)
			{
				Result.BigProperties += Event.Type == EGridlockEventType::UpgradeFinished && Event.Amount >= static_cast<int32>(EPropertyTier::Complex) ? 1 : 0;
				Result.Removals += Event.Type == EGridlockEventType::PropertyRemoved || Event.Type == EGridlockEventType::PropertyDemolished ? 1 : 0;
				Result.TollsPaid += Event.Type == EGridlockEventType::TollPaid ? Event.Amount : 0;
				Result.Sales += Event.Type == EGridlockEventType::PropertySold ? 1 : 0;
			}
		}

		Result.Winner = State.Winner;
		Result.Rounds = State.Round;
		if (State.Players.IsValidIndex(State.Winner))
		{
			const FPlayerData& Winner = State.Players[State.Winner];
			// Points can drop after the win (unfinished-card option), so "by points" = not by cards or the turn limit.
			Result.bEndedByCards = Winner.Completed.Num() >= Context.GetCardsToWin();
			Result.bEndedByPoints = !Result.bEndedByCards && State.Round <= Context.Tuning->TurnLimit;
		}
		for (const FPlayerData& Player : State.Players)
		{
			Result.FinalCash += Player.Cash;
			for (const FName CardId : Player.Completed)
			{
				if (const FRouteCardDef* Card = Context.Deck->FindCard(CardId))
				{
					++Result.CompletedByType[static_cast<int32>(Card->Type)];
				}
			}
		}
		return Result;
	}

	/** Averages over a match, for the balance report. */
	struct FMatchSummary
	{
		int32 Games = 0;
		int32 WinsA = 0;
		int32 ByCards = 0;
		int32 ByPoints = 0;
		float Rounds = 0.0f;
		float Cards = 0.0f;
		float BigProperties = 0.0f;
		float Removals = 0.0f;
		float FinalCash = 0.0f; // per player
		float TollsPaid = 0.0f;
		float Sales = 0.0f;
	};

	/** Plays A against B, swapping seats every game. Returns A's wins. */
	int32 PlayMatch(FAutomationTestBase& Test, const FRulesContext& Context, const TCHAR* Name, const UAIPersonality* A, const UAIPersonality* B,
		int32 Games, double& OutSlowest, int32* OutDraws = nullptr, FMatchSummary* OutSummary = nullptr)
	{
		int32 WinsA = 0;
		int32 Draws = 0;
		int32 Rounds = 0;
		double Seconds = 0.0;
		int32 Decisions = 0;
		int32 ByCards = 0;
		int32 ByPoints = 0;
		int32 BigProperties = 0;
		int32 Removals = 0;
		int32 FinalCash = 0;
		int32 TollsPaid = 0;
		int32 Sales = 0;
		int32 CompletedByType[3] = { 0, 0, 0 };
		for (int32 Game = 0; Game < Games; ++Game)
		{
			const bool bASeat0 = Game % 2 == 0;
			const FSeat Seats[2] = { { bASeat0 ? A : B }, { bASeat0 ? B : A } };
			const FGameResult Result = PlayGame(Context, 1000 + Game, Seats);
			Test.TestTrue(FString::Printf(TEXT("%s game %d: every move legal"), Name, Game), Result.bAllLegal);

			const int32 SeatA = bASeat0 ? 0 : 1;
			WinsA += Result.Winner == SeatA ? 1 : 0;
			Draws += Result.Winner == INDEX_NONE ? 1 : 0;
			Rounds += Result.Rounds;
			Seconds += Result.DecisionSeconds;
			Decisions += Result.Decisions;
			OutSlowest = FMath::Max(OutSlowest, Result.SlowestDecision);
			ByCards += Result.bEndedByCards ? 1 : 0;
			ByPoints += Result.bEndedByPoints ? 1 : 0;
			BigProperties += Result.BigProperties;
			Removals += Result.Removals;
			FinalCash += Result.FinalCash;
			TollsPaid += Result.TollsPaid;
			Sales += Result.Sales;
			for (int32 Type = 0; Type < 3; ++Type)
			{
				CompletedByType[Type] += Result.CompletedByType[Type];
			}
		}
		if (OutSummary)
		{
			OutSummary->Games = Games;
			OutSummary->WinsA = WinsA;
			OutSummary->ByCards = ByCards;
			OutSummary->ByPoints = ByPoints;
			OutSummary->Rounds = static_cast<float>(Rounds) / Games;
			OutSummary->Cards = static_cast<float>(CompletedByType[0] + CompletedByType[1] + CompletedByType[2]) / Games;
			OutSummary->BigProperties = static_cast<float>(BigProperties) / Games;
			OutSummary->Removals = static_cast<float>(Removals) / Games;
			OutSummary->FinalCash = static_cast<float>(FinalCash) / (2 * Games);
			OutSummary->TollsPaid = static_cast<float>(TollsPaid) / Games;
			OutSummary->Sales = static_cast<float>(Sales) / Games;
		}
		Test.AddInfo(FString::Printf(TEXT("%s: %d-%d (%d draws) over %d games, %.1f rounds on average, %.1f ms per AI decision"),
			Name, WinsA, Games - WinsA - Draws, Draws, Games, static_cast<float>(Rounds) / Games, Decisions > 0 ? 1000.0 * Seconds / Decisions : 0.0));
		Test.AddInfo(FString::Printf(TEXT("%s: ended by card target %d, by points target %d, by turn limit %d; cards completed per game: %.1f short, %.1f medium, %.1f long"),
			Name, ByCards, ByPoints, Games - ByCards - ByPoints, static_cast<float>(CompletedByType[0]) / Games,
			static_cast<float>(CompletedByType[1]) / Games, static_cast<float>(CompletedByType[2]) / Games));
		if (OutDraws)
		{
			*OutDraws = Draws;
		}
		return WinsA;
	}
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockAIBeatsRandomTest, "Gridlock.AI.BeatsRandom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockAIBeatsRandomTest::RunTest(const FString& Parameters)
{
	const GridlockAITests::FFixture Fixture;
	const UAIPersonality* Builder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Normal);

	double Slowest = 0.0;
	const int32 Games = 20;
	const int32 Wins = GridlockAITests::PlayMatch(*this, Fixture.Context, TEXT("Builder (Normal) vs random"), Builder, nullptr, Games, Slowest);
	TestTrue(FString::Printf(TEXT("Builder wins at least 80%% against random play (won %d of %d)"), Wins, Games), Wins >= Games * 8 / 10);
	TestTrue(FString::Printf(TEXT("Every decision under 1 second (slowest %.0f ms)"), Slowest * 1000.0), Slowest < 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockAIMatchupsTest, "Gridlock.AI.Matchups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockAIMatchupsTest::RunTest(const FString& Parameters)
{
	const GridlockAITests::FFixture Fixture;
	const UAIPersonality* Builder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Normal);
	const UAIPersonality* Saboteur = GridlockAITests::MakePersonality(EAIStyle::Saboteur, EAIDifficulty::Normal);
	const UAIPersonality* Tycoon = GridlockAITests::MakePersonality(EAIStyle::Tycoon, EAIDifficulty::Normal);
	const UAIPersonality* EasyBuilder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Easy);
	const UAIPersonality* HardBuilder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Hard);

	double Slowest = 0.0;
	GridlockAITests::PlayMatch(*this, Fixture.Context, TEXT("Builder vs Saboteur"), Builder, Saboteur, 40, Slowest);
	GridlockAITests::PlayMatch(*this, Fixture.Context, TEXT("Builder vs Tycoon"), Builder, Tycoon, 40, Slowest);
	GridlockAITests::PlayMatch(*this, Fixture.Context, TEXT("Saboteur vs Tycoon"), Saboteur, Tycoon, 40, Slowest);

	const int32 NormalWins = GridlockAITests::PlayMatch(*this, Fixture.Context, TEXT("Normal vs Easy (Builder)"), Builder, EasyBuilder, 40, Slowest);
	TestTrue(FString::Printf(TEXT("Normal beats Easy at least 70%% of the time (won %d of 40)"), NormalWins), NormalWins >= 28);

	double HardSlowest = 0.0;
	const int32 HardWins = GridlockAITests::PlayMatch(*this, Fixture.Context, TEXT("Hard vs Normal (Builder)"), HardBuilder, Builder, 40, HardSlowest);
	AddInfo(FString::Printf(TEXT("Slowest Hard decision: %.0f ms"), HardSlowest * 1000.0));
	TestTrue(FString::Printf(TEXT("Hard decisions under 2 seconds (slowest %.0f ms)"), HardSlowest * 1000.0), HardSlowest < 2.0);
	AddInfo(FString::Printf(TEXT("Hard won %d of 40 against Normal"), HardWins));
	return true;
}

// Not a pass/fail check: AI-vs-AI game length and endings under different tunings, for balancing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockAIBalanceReportTest, "Gridlock.AI.BalanceReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockAIBalanceReportTest::RunTest(const FString& Parameters)
{
	const GridlockAITests::FFixture Fixture;
	const UAIPersonality* Builder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Normal);
	const UAIPersonality* Saboteur = GridlockAITests::MakePersonality(EAIStyle::Saboteur, EAIDifficulty::Normal);
	const UAIPersonality* EasyBuilder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Easy);

	using FConfigure = TFunction<void(UTuningData&)>;
	const TArray<TPair<FString, FConfigure>> Variants = {
		{ TEXT("Rules v0.2"),                    [](UTuningData& T) { T.ApplyRulesV02(); } },
		{ TEXT("Rules v0.3 (card economy)"),     [](UTuningData& T) { T.ApplyRulesV03(); } },
		{ TEXT("Rules v0.4 (5 cards, 40 rounds)"), [](UTuningData& T) { T.ApplyRulesV04(); } },
		{ TEXT("v0.4, 8 cards, 25 rounds"),      [](UTuningData& T) { T.ApplyRulesV04(); T.CardsToWin = 8; T.TurnLimit = 25; } },
		{ TEXT("v0.4, 4 cards"),                 [](UTuningData& T) { T.ApplyRulesV04(); T.CardsToWin = 4; } },
		{ TEXT("v0.4, 6 cards"),                 [](UTuningData& T) { T.ApplyRulesV04(); T.CardsToWin = 6; } },
		{ TEXT("v0.4, 50 rounds"),               [](UTuningData& T) { T.ApplyRulesV04(); T.TurnLimit = 50; } },
		{ TEXT("v0.4, routes must connect"),     [](UTuningData& T) { T.ApplyRulesV04(); T.bRoutesMustConnect = true; } },
		{ TEXT("v0.4, no card payout"),          [](UTuningData& T) { T.ApplyRulesV04(); T.CashPerCardPoint = 0; } },
		{ TEXT("v0.4, payout $2 a point"),       [](UTuningData& T) { T.ApplyRulesV04(); T.CashPerCardPoint = 2; } },
		{ TEXT("v0.4, start $25"),               [](UTuningData& T) { T.ApplyRulesV04(); T.StartCashFirstPlayer = 25; T.StartCashSecondPlayer = 25; } },
		{ TEXT("v0.4, no payout, income $1"),    [](UTuningData& T) { T.ApplyRulesV04(); T.CashPerCardPoint = 0; T.BaseIncome = 1; } },
	};

	const int32 Games = 30;
	TArray<FString> Table;
	for (const TPair<FString, FConfigure>& Variant : Variants)
	{
		// Every variant starts from Rules v0.1, then applies its own changes.
		UTuningData* Tuning = NewObject<UTuningData>();
		Tuning->ApplyRulesV01();
		Variant.Value(*Tuning);
		const FRulesContext Context = Fixture.For(Tuning);

		double Slowest = 0.0;
		GridlockAITests::FMatchSummary Mixed;
		GridlockAITests::PlayMatch(*this, Context, *FString::Printf(TEXT("%s, Builder vs Saboteur"), *Variant.Key), Builder, Saboteur, Games, Slowest, nullptr, &Mixed);
		GridlockAITests::FMatchSummary Skill;
		GridlockAITests::PlayMatch(*this, Context, *FString::Printf(TEXT("%s, Normal vs Easy"), *Variant.Key), Builder, EasyBuilder, Games, Slowest, nullptr, &Skill);

		Table.Add(FString::Printf(TEXT("%-30s | %5.1f rounds | ends: cards %2d, points %2d, limit %2d | %4.1f cards | %4.1f Complex+ | %4.1f removals | end cash $%4.1f, tolls $%4.1f, %3.1f sales | Normal beats Easy %3.0f%%"),
			*Variant.Key, Mixed.Rounds, Mixed.ByCards, Mixed.ByPoints, Mixed.Games - Mixed.ByCards - Mixed.ByPoints,
			Mixed.Cards, Mixed.BigProperties, Mixed.Removals, Mixed.FinalCash, Mixed.TollsPaid, Mixed.Sales, 100.0f * Skill.WinsA / Skill.Games));
	}

	AddInfo(FString::Printf(TEXT("BALANCE SUMMARY (Builder vs Saboteur, %d games each; skill column from %d Normal vs Easy games)"), Games, Games));
	for (const FString& Row : Table)
	{
		AddInfo(TEXT("BALANCE ") + Row);
	}
	return true;
}

// Not a pass/fail check: what happens to properties (placed, upgraded, removed) in AI-vs-AI games.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockAIPropertyReportTest, "Gridlock.AI.PropertyReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockAIPropertyReportTest::RunTest(const FString& Parameters)
{
	const GridlockAITests::FFixture Fixture;
	const UAIPersonality* Builder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Normal);
	const UAIPersonality* Saboteur = GridlockAITests::MakePersonality(EAIStyle::Saboteur, EAIDifficulty::Normal);
	const UAIPersonality* Tycoon = GridlockAITests::MakePersonality(EAIStyle::Tycoon, EAIDifficulty::Normal);

	using FConfigure = TFunction<void(UTuningData&)>;
	struct FCase
	{
		FString Name;
		const UAIPersonality* A;
		const UAIPersonality* B;
		FConfigure Configure;
	};
	const TArray<FCase> Cases = {
		{ TEXT("v0.2, Builder vs Saboteur"),               Builder, Saboteur, [](UTuningData&) {} },
		{ TEXT("v0.2, Tycoon vs Tycoon"),                  Tycoon, Tycoon, [](UTuningData&) {} },
		{ TEXT("v0.3, Builder vs Saboteur"),             Builder, Saboteur, [](UTuningData& T) { T.ApplyRulesV03(); } },
		{ TEXT("v0.3, Tycoon vs Tycoon"),                Tycoon, Tycoon, [](UTuningData& T) { T.ApplyRulesV03(); } },
		{ TEXT("v0.4, Builder vs Saboteur"),             Builder, Saboteur, [](UTuningData& T) { T.ApplyRulesV04(); } },
		{ TEXT("v0.4, Tycoon vs Tycoon"),                Tycoon, Tycoon, [](UTuningData& T) { T.ApplyRulesV04(); } },
	};

	const int32 Games = 30;
	for (const FCase& Case : Cases)
	{
		// Cases start from Rules v0.2 (the v0.3 ones switch over themselves).
		UTuningData* Tuning = NewObject<UTuningData>();
		Tuning->ApplyRulesV02();
		Case.Configure(*Tuning);
		const FRulesContext Context = Fixture.For(Tuning);

		GridlockAITests::FGameResult Total;
		int32 Rounds = 0;
		for (int32 Game = 0; Game < Games; ++Game)
		{
			const bool bASeat0 = Game % 2 == 0;
			const GridlockAITests::FSeat Seats[2] = { { bASeat0 ? Case.A : Case.B }, { bASeat0 ? Case.B : Case.A } };
			const GridlockAITests::FGameResult Result = GridlockAITests::PlayGame(Context, 2000 + Game, Seats);
			Rounds += Result.Rounds;
			Total.LotsPlaced += Result.LotsPlaced;
			Total.LotsOnOpponentRoute += Result.LotsOnOpponentRoute;
			Total.LotsOnOwnRoute += Result.LotsOnOwnRoute;
			Total.LotsOnOpenRoute += Result.LotsOnOpenRoute;
			Total.RemovedMidUpgrade += Result.RemovedMidUpgrade;
			Total.Demolishes += Result.Demolishes;
			Total.BigProperties += Result.BigProperties;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Total.UpgradesTo[Index] += Result.UpgradesTo[Index];
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Total.RemovedAtTier[Index] += Result.RemovedAtTier[Index];
			}
		}

		const float G = static_cast<float>(Games);
		AddInfo(FString::Printf(TEXT("PROPERTIES %s: %.1f rounds | Lots placed %.1f (on opponent route %.1f, own %.1f, open %.1f) | upgrades started to Building %.1f, Complex %.1f, Landmark %.1f | finished Complex+ %.1f"),
			*Case.Name, Rounds / G, Total.LotsPlaced / G, Total.LotsOnOpponentRoute / G, Total.LotsOnOwnRoute / G, Total.LotsOnOpenRoute / G,
			Total.UpgradesTo[0] / G, Total.UpgradesTo[1] / G, Total.UpgradesTo[2] / G, Total.BigProperties / G));
		AddInfo(FString::Printf(TEXT("PROPERTIES %s: removed Lots %.1f, Buildings %.1f, Complexes %.1f, Landmarks %.1f | mid-upgrade %.1f | demolished %.1f"),
			*Case.Name, Total.RemovedAtTier[0] / G, Total.RemovedAtTier[1] / G, Total.RemovedAtTier[2] / G, Total.RemovedAtTier[3] / G,
			Total.RemovedMidUpgrade / G, Total.Demolishes / G));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockAINoPeekingTest, "Gridlock.AI.NoPeeking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockAINoPeekingTest::RunTest(const FString& Parameters)
{
	const GridlockAITests::FFixture Fixture;
	const UAIPersonality* Hard = GridlockAITests::MakePersonality(EAIStyle::Saboteur, EAIDifficulty::Hard);

	// Play a game and, at every AI decision, swap the opponent's hand for different unseen cards.
	FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, 77);
	int32 Checked = 0;
	for (int32 Step = 0; Step < 600 && State.Phase != EGridlockPhase::GameOver; ++Step)
	{
		const int32 Player = State.ActivePlayer;
		const FGameAction Action = UAIPlayer::ChooseFor(State, Fixture.Context, *Hard, Player);

		FBoardState Swapped = State;
		FPlayerData& Them = Swapped.Players[1 - Player];
		const int32 HandSize = Them.Hand.Num();
		if (HandSize > 0 && Them.PendingDraw.IsEmpty())
		{
			TArray<FName> Others;
			for (const FRouteCardDef& Card : Fixture.Context.Deck->Cards)
			{
				const FPlayerData& Me = State.Players[Player];
				if (!Them.Hand.Contains(Card.Id) && !Me.Hand.Contains(Card.Id) && !Me.PendingDraw.Contains(Card.Id)
					&& !Me.Completed.Contains(Card.Id) && !Them.Completed.Contains(Card.Id))
				{
					Others.Add(Card.Id);
				}
			}
			if (Others.Num() >= HandSize)
			{
				Them.Hand = TArray<FName>(Others.GetData(), HandSize);

				// Same counts, different hidden colors and tiers.
				const int32 TicketTotal = Them.TicketCount();
				Them.Tickets.Init(0, GridlockColorCount);
				Them.Tickets[static_cast<int32>(EGridlockColor::Black)] = TicketTotal;
				const int32 PropertyTotal = Them.PropertyCardCount();
				Them.PropertyCards.Init(0, GridlockTierCount);
				Them.PropertyCards[static_cast<int32>(EPropertyTier::Building)] = PropertyTotal;

				const FGameAction Blind = UAIPlayer::ChooseFor(Swapped, Fixture.Context, *Hard, Player);
				const bool bSame = Blind.Type == Action.Type && Blind.RouteId == Action.RouteId && Blind.CardIds == Action.CardIds;
				if (!TestTrue(FString::Printf(TEXT("Step %d: same choice whatever the opponent holds"), Step), bSame))
				{
					return false;
				}
				++Checked;
			}
		}

		TArray<FGameEvent> Events;
		State = URulesLibrary::Apply(State, Action, Fixture.Context, Events);
	}
	AddInfo(FString::Printf(TEXT("Checked %d decisions"), Checked));
	TestTrue(TEXT("Checked a meaningful number of decisions"), Checked > 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockAIIntentTest, "Gridlock.AI.Intent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockAIIntentTest::RunTest(const FString& Parameters)
{
	const GridlockAITests::FFixture Fixture;
	FBoardState State = URulesLibrary::MakeInitialState(Fixture.Context, 1);
	for (FPlayerData& Player : State.Players)
	{
		State.Deck.Append(Player.PendingDraw);
		Player.PendingDraw.Reset();
	}
	State.Phase = EGridlockPhase::Build;
	State.ActivePlayer = 0;
	State.Players[0].Hand = { TEXT("C13") }; // Westport - Highcliff: via Pinecrest (R01, R04) or Ironford (R03, R05)
	State.Players[0].Tickets.Init(0, GridlockColorCount);
	State.Players[0].Tickets[static_cast<int32>(EGridlockColor::Wild)] = 6; // enough for any route, so only distance matters

	const FName Intent = FAIScoring::FindIntent(State, Fixture.Context, 0);
	TestTrue(FString::Printf(TEXT("Heads out of Westport toward Highcliff (got %s)"), *Intent.ToString()),
		Intent == TEXT("R01") || Intent == TEXT("R03"));
	TestEqual(TEXT("5 pins to go (4 + 1 or 3 + 2)"), FAIScoring::CardRemainingCost(State, Fixture.Context, 0, TEXT("C13")), 5);

	State.RouteOwner[TEXT("R03")] = 0;
	TestEqual(TEXT("1 dot to go after claiming Westport - Ironford"), FAIScoring::CardRemainingCost(State, Fixture.Context, 0, TEXT("C13")), 1);
	TestTrue(TEXT("Then Ironford - Highcliff"), FAIScoring::FindIntent(State, Fixture.Context, 0) == TEXT("R05"));

	const UAIPersonality* Builder = GridlockAITests::MakePersonality(EAIStyle::Builder, EAIDifficulty::Normal);
	const FGameAction Choice = UAIPlayer::ChooseFor(State, Fixture.Context, *Builder, 0);
	TestTrue(FString::Printf(TEXT("Builder claims Ironford - Highcliff (chose %d on %s)"), static_cast<int32>(Choice.Type), *Choice.RouteId.ToString()),
		Choice.Type == EGridlockActionType::ClaimRoute && Choice.RouteId == TEXT("R05"));
	return true;
}

#endif

#include "AI/AIPlayer.h"
#include "Algo/Count.h"
#include "AI/AIScoring.h"
#include "Data/AIPersonality.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "Gridlock.h"
#include "Model/RulesLibrary.h"

namespace GridlockAIPlayerPrivate
{
	/** A kept card has to be worth more than this to earn its hand slot. */
	constexpr float KeepThreshold = 0.6f;

	/** Below this, nothing in hand is worth chasing and it's time to draw. */
	constexpr float DrawThreshold = 0.5f;

	/** Hard looks ahead from this many of its best candidates. */
	constexpr int32 LookaheadCandidates = 5;

	const FGameAction* FindType(const TArray<FGameAction>& Actions, EGridlockActionType Type)
	{
		return Actions.FindByPredicate([Type](const FGameAction& Action) { return Action.Type == Type; });
	}

	/**
	 * The board as Player knows it: the opponent's Route cards are replaced by the unseen cards that
	 * look best for them, and their tickets and property cards by a neutral guess with the same
	 * counts, so a simulated reply can't use anything Player has never seen.
	 */
	FBoardState WithGuessedOpponentHand(const FBoardState& State, const FRulesContext& Context, int32 Player)
	{
		FBoardState Guess = State;
		const int32 Other = 1 - Player;
		const FPlayerData& Me = State.Players[Player];
		FPlayerData& Them = Guess.Players[Other];

		// Tickets: the same total, spread evenly over the colors. Property cards: the same total, all Lots.
		const int32 TicketTotal = Them.TicketCount();
		const int32 PropertyTotal = Them.PropertyCardCount();
		Them.Tickets.Init(0, GridlockColorCount);
		for (int32 Index = 0; Index < TicketTotal; ++Index)
		{
			++Them.Tickets[static_cast<int32>(EGridlockColor::Red) + Index % 8];
		}
		Them.PropertyCards.Init(0, GridlockTierCount);
		Them.PropertyCards[static_cast<int32>(EPropertyTier::Lot)] = PropertyTotal;

		TArray<TPair<float, FName>> Unseen;
		for (const FRouteCardDef& Card : Context.Deck->Cards)
		{
			if (!Me.Hand.Contains(Card.Id) && !Me.PendingDraw.Contains(Card.Id) && !Me.Completed.Contains(Card.Id) && !Them.Completed.Contains(Card.Id))
			{
				Unseen.Emplace(FAIScoring::CardValue(State, Context, Other, Card.Id, false), Card.Id);
			}
		}
		Unseen.Sort([](const TPair<float, FName>& A, const TPair<float, FName>& B) { return A.Key > B.Key; });

		const int32 HandSize = Them.Hand.Num();
		Them.Hand.Reset();
		for (int32 Index = 0; Index < HandSize && Index < Unseen.Num(); ++Index)
		{
			Them.Hand.Add(Unseen[Index].Value);
		}
		return Guess;
	}

	/** Plays the active player's whole turn greedily (no lookahead). */
	FBoardState PlayGreedyTurn(FBoardState State, const FRulesContext& Context, const UAIPersonality& Personality)
	{
		const int32 Player = State.ActivePlayer;
		for (int32 Guard = 0; Guard < 12 && State.ActivePlayer == Player && State.Phase != EGridlockPhase::GameOver; ++Guard)
		{
			const FGameAction Action = UAIPlayer::ChooseFor(State, Context, Personality, Player, false);
			TArray<FGameEvent> Events;
			State = URulesLibrary::Apply(State, Action, Context, Events);
		}
		return State;
	}
}

FGameAction UAIPlayer::ChooseTicketToRideTurn(const FBoardState& State, const FRulesContext& Context,
	const UAIPersonality& Personality, int32 Player, const TArray<FGameAction>& Legal, FRandomStream& Random)
{
	const UTuningData& Tuning = *Context.Tuning;
	const FPlayerData& Me = State.Players[Player];
	auto Noise = [&Random, &Personality](float Scale)
	{
		return Personality.ScoreNoise > 0.0f ? Personality.ScoreNoise * Scale * Random.FRandRange(-1.0f, 1.0f) : 0.0f;
	};

	// The routes my unfinished Route cards still need (by the cheapest way round what the opponent holds).
	TSet<FName> Needed;
	for (const FName CardId : Me.Hand)
	{
		if (!URulesLibrary::IsCardComplete(State, Context, Player, CardId) && URulesLibrary::CanStillComplete(State, Context, Player, CardId))
		{
			Needed.Append(FAIScoring::NeededRoutesForCard(State, Context, Player, CardId));
		}
	}
	const bool bAllDone = URulesLibrary::AreCardsDone(State, Context, Player);
	// Stuck: unfinished cards with no way left to finish them, so no new cards either. Score routes instead.
	const bool bStuck = !bAllDone && Needed.IsEmpty();
	const bool bLastTurns = State.FinalTurnsLeft >= 0 || Me.Trains <= Tuning.FinalRoundTrains + 4;

	// Lots (v0.7). A Lot off the deck: keep it when a needed route still has room for it.
	auto FreeSlots = [&State, &Context, &Tuning](FName RouteId)
	{
		const FRouteDef* Route = Context.Map->FindRoute(RouteId);
		const FRouteLots* Lots = State.RouteLots.Find(RouteId);
		return Route ? Tuning.GetLotSlots(Route->Length) - (Lots ? Lots->Owners.Num() : 0) : 0;
	};
	if (Me.bLotPending)
	{
		const FGameAction* Keep = Legal.FindByPredicate([](const FGameAction& Action) { return Action.Type == EGridlockActionType::KeepLot; });
		const bool bUseful = Needed.Array().ContainsByPredicate([&FreeSlots](FName RouteId) { return FreeSlots(RouteId) > 0; });
		const FGameAction* Discard = Legal.FindByPredicate([](const FGameAction& Action) { return Action.Type == EGridlockActionType::DiscardLot; });
		return Keep && bUseful ? *Keep : Discard ? *Discard : Legal[0];
	}
	// Holding a Lot: placing one takes the whole turn, so only when it matters on a needed route:
	// it completes my block there, or it stops the opponent from completing theirs.
	if (Me.LotCardCount() > 0 && State.BuysThisTurn == 0 && !bLastTurns)
	{
		for (const FGameAction& Action : Legal)
		{
			if (Action.Type != EGridlockActionType::PlaceRouteLot || !Needed.Contains(Action.RouteId))
			{
				continue;
			}
			const FRouteLots* Lots = State.RouteLots.Find(Action.RouteId);
			const int32 Theirs = Lots ? Algo::Count(Lots->Owners, 1 - Player) : 0;
			const int32 Mine = Lots ? Lots->Owners.Num() - Theirs : 0;
			const int32 Free = FreeSlots(Action.RouteId);
			const bool bCompletesMyBlock = Free == 1 && Theirs == 0;
			const bool bStopsTheirBlock = Free == 1 && Mine == 0 && Theirs > 0;
			if (bCompletesMyBlock || bStopsTheirBlock)
			{
				return Action;
			}
		}
	}

	// 1. Claim: a needed route (the best one), or at the very end the most points a claim can still bring.
	if (State.BuysThisTurn == 0)
	{
		const float Base = FAIScoring::Evaluate(State, Context, Personality, Player);
		const FGameAction* Best = nullptr;
		float BestScore = -MAX_flt;
		for (const FGameAction& Action : Legal)
		{
			if (Action.Type != EGridlockActionType::ClaimRoute || (!Needed.Contains(Action.RouteId) && !bLastTurns && !bStuck))
			{
				continue;
			}
			const float Score = FAIScoring::ScoreAction(State, Action, Context, Personality, Player) + Noise(1.0f);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = &Action;
			}
		}
		if (Best && (BestScore > Base || bLastTurns || bStuck))
		{
			return *Best;
		}

		// 2. Route cards: all mine are done (the only time new ones can be drawn) and some trains are left to use.
		const FGameAction* DrawCards = Legal.FindByPredicate([](const FGameAction& Action) { return Action.Type == EGridlockActionType::DrawCards; });
		if (DrawCards && bAllDone && !bLastTurns && Me.Trains >= 7)
		{
			return *DrawCards;
		}

		// City properties (v0.8): with no claim worth making now, sometimes buy a city with many open routes
		// into it (future tolls), up to 4 of them.
		int32 Owned = 0;
		for (const TPair<FName, int32>& Entry : State.CityOwner)
		{
			Owned += Entry.Value == Player ? 1 : 0;
		}
		if (Tuning.CityPropertyPrice > 0 && !bLastTurns && Owned < 4)
		{
			const FGameAction* BestBuy = nullptr;
			int32 BestOpen = 0;
			for (const FGameAction& Action : Legal)
			{
				if (Action.Type != EGridlockActionType::BuyCityProperty)
				{
					continue;
				}
				int32 Open = 0;
				for (const FRouteDef& Route : Context.Map->Routes)
				{
					Open += Route.Touches(Action.RouteId) && State.RouteOwner.FindRef(Route.Id) == INDEX_NONE ? 1 : 0;
				}
				if (Open > BestOpen)
				{
					BestOpen = Open;
					BestBuy = &Action;
				}
			}
			if (BestBuy && BestOpen >= 3 && Random.FRand() < 0.4f)
			{
				return *BestBuy;
			}
		}
	}

	// 3. Tickets: the face-up color my needed routes lack most, a face-up Wild as a first pick, else from the deck.
	TArray<int32> Want;
	Want.Init(0, GridlockColorCount);
	for (const FName RouteId : Needed)
	{
		const FRouteDef* Route = Context.Map->FindRoute(RouteId);
		if (Route && Route->Color != EGridlockColor::Gray)
		{
			Want[static_cast<int32>(Route->Color)] += Route->Length;
		}
	}
	for (int32 Color = 0; Color < GridlockColorCount; ++Color)
	{
		Want[Color] -= Me.Tickets.IsValidIndex(Color) ? Me.Tickets[Color] : 0;
	}
	const FGameAction* Pick = nullptr;
	int32 PickWant = 0;
	const FGameAction* FaceUpWild = nullptr;
	const FGameAction* Blind = nullptr;
	for (const FGameAction& Action : Legal)
	{
		if (Action.Type == EGridlockActionType::BuyTicket)
		{
			Blind = &Action;
		}
		if (Action.Type != EGridlockActionType::TakeFaceUp)
		{
			continue;
		}
		const EGridlockColor Color = State.FaceUp[Action.Slot];
		if (Color == EGridlockColor::Wild)
		{
			FaceUpWild = &Action;
			continue;
		}
		const int32 Score = Want[static_cast<int32>(Color)] + FMath::RoundToInt(Noise(2.0f));
		if (Score > PickWant)
		{
			PickWant = Score;
			Pick = &Action;
		}
	}
	if (Pick)
	{
		return *Pick;
	}
	if (FaceUpWild && !Needed.IsEmpty() && Noise(1.0f) > -1.0f)
	{
		return *FaceUpWild;
	}
	if (Blind)
	{
		return *Blind;
	}
	for (const FGameAction& Action : Legal)
	{
		if (Action.Type != EGridlockActionType::KeepCards)
		{
			return Action; // anything legal (a stray claim, or ending a stuck turn)
		}
	}
	return Legal[0];
}

FGameAction UAIPlayer::ChooseFor(const FBoardState& State, const FRulesContext& Context,
	const UAIPersonality& Personality, int32 Player, bool bAllowLookahead)
{
	using namespace GridlockAIPlayerPrivate;

	const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Context);
	if (Legal.IsEmpty() || !State.Players.IsValidIndex(Player))
	{
		FGameAction End;
		End.Type = EGridlockActionType::EndPhase;
		End.Player = Player;
		return End;
	}
	if (Legal.Num() == 1)
	{
		return Legal[0];
	}
	const FPlayerData& Me = State.Players[Player];

	// Easy's misjudgement. The seed comes from the state, so replays match.
	FRandomStream Random(State.RandomSeed + State.Round * 7919 + static_cast<int32>(State.Phase) * 131 + Player * 17 + Me.Cash
		+ State.BuysThisTurn * 31 + Me.Trains * 3);
	auto Noise = [&Random, &Personality](float Scale)
	{
		return Personality.ScoreNoise > 0.0f ? Personality.ScoreNoise * Scale * Random.FRandRange(-1.0f, 1.0f) : 0.0f;
	};

	// Keeping cards: each kept card has to be worth its hand slot (more so when unfinished cards cost points).
	if (!Me.PendingDraw.IsEmpty())
	{
		const float Threshold = Context.Tuning->bUnfinishedCardsCountAgainst ? 2.0f * KeepThreshold : KeepThreshold;
		TMap<FName, float> Judged;
		for (const FName CardId : Me.PendingDraw)
		{
			Judged.Add(CardId, FAIScoring::CardValue(State, Context, Player, CardId) + Noise(0.5f));
		}
		const FGameAction* Best = nullptr;
		float BestScore = -MAX_flt;
		for (const FGameAction& Action : Legal)
		{
			float Score = 0.0f;
			for (const FName CardId : Action.CardIds)
			{
				Score += Judged.FindRef(CardId) - Threshold;
			}
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = &Action;
			}
		}
		return *Best;
	}

	// Ticket to Ride (v0.5): one of claim a route, draw Route cards, or take 2 tickets.
	if (State.Phase == EGridlockPhase::Start && Context.Tuning->bClassicTurn)
	{
		return ChooseTicketToRideTurn(State, Context, Personality, Player, Legal, Random);
	}

	// Start: draw Route cards when the hand is thin or nothing in it is worth chasing; otherwise
	// buy the tickets the next routes need and a Lot card to invest with, keeping cash to build.
	if (State.Phase == EGridlockPhase::Start)
	{
		const UTuningData& Tuning = *Context.Tuning;
		float BestHeld = 0.0f;
		for (const FName CardId : Me.Hand)
		{
			BestHeld = FMath::Max(BestHeld, FAIScoring::CardValue(State, Context, Player, CardId));
		}
		const FGameAction* Draw = FindType(Legal, EGridlockActionType::DrawCards);
		const FGameAction* End = FindType(Legal, EGridlockActionType::EndPhase);
		const FGameAction* BuyTicket = FindType(Legal, EGridlockActionType::BuyTicket);
		const FGameAction* BuyCard = FindType(Legal, EGridlockActionType::BuyPropertyCard);

		// When drawing costs the turn's Build, only draw with nothing worth building toward.
		const bool bThinHand = Tuning.bDrawReplacesBuild ? Me.Hand.IsEmpty() : Me.Hand.Num() < 2;
		const bool bWantRouteCards = bThinHand || BestHeld + Noise(0.3f) < DrawThreshold;
		if (Draw && bWantRouteCards)
		{
			return *Draw;
		}

		// One main action (v0.4): build or remove when a claim or removal is worth it; otherwise buy.
		if (Tuning.bOneMainActionPerTurn && State.BuysThisTurn == 0 && End)
		{
			const float Base = FAIScoring::Evaluate(State, Context, Personality, Player);
			float BestMove = -MAX_flt;
			for (const EGridlockPhase Phase : { EGridlockPhase::Resolve, EGridlockPhase::Build })
			{
				FBoardState Later = State;
				Later.Phase = Phase;
				for (const FGameAction& Action : URulesLibrary::GetLegalActions(Later, Context))
				{
					if (Action.Type != EGridlockActionType::EndPhase && Action.Type != EGridlockActionType::SellProperty)
					{
						BestMove = FMath::Max(BestMove, FAIScoring::ScoreAction(Later, Action, Context, Personality, Player));
					}
				}
			}
			if (BestMove > Base + 0.05f + Noise(0.3f))
			{
				return *End;
			}
		}

		if (Tuning.bCardEconomy && State.BuysThisTurn < 12) // a safety cap on buys in one turn
		{
			const FRouteDef* NextRoute = Context.Map->FindRoute(FAIScoring::FindIntent(State, Context, Player));
			const int32 ClaimCash = NextRoute ? NextRoute->Length * Tuning.CostPerLengthDot : 0;

			// Property cards: a Lot card to get into properties, or the card that upgrades one it owns.
			// Money-minded personalities hold more of them. This comes first: only one a turn, and
			// otherwise tickets would take all the cash.
			int32 UpgradesWaitingForCards = 0;
			for (const TPair<FName, FSlotState>& Entry : State.Slots)
			{
				const FSlotState& Slot = Entry.Value;
				if (Slot.Owner == Player && !Slot.bUpgrading && Slot.Tier < EPropertyTier::Landmark
					&& Me.PropertyCardsOf(static_cast<EPropertyTier>(static_cast<uint8>(Slot.Tier) + 1)) == 0)
				{
					++UpgradesWaitingForCards;
				}
			}
			const int32 MaxPropertyCards = Personality.EconomyWeight >= 0.8f ? 4 : 3;
			// When a turn's buy is tickets or a property card, take the card only when no tickets are
			// missing, or now and then (more often for money-minded personalities).
			const bool bPropertyTurn = !Tuning.bTicketsOrPropertyNotBoth || FAIScoring::TicketShortfall(State, Context, Player) == 0
				|| Random.FRand() < (Personality.EconomyWeight >= 0.8f ? 0.35f : 0.15f);
			const bool bWantPropertyCard = bPropertyTurn && Me.PropertyCardCount() < MaxPropertyCards
				&& (Me.PropertyCardsOf(EPropertyTier::Lot) == 0 || UpgradesWaitingForCards > 0);
			if (BuyCard && bWantPropertyCard && Me.Cash - Tuning.PropertyCardCost >= ClaimCash + Tuning.LotCost && Noise(1.0f) > -1.2f)
			{
				return *BuyCard;
			}

			// Tickets for the next routes, keeping enough cash to play a property card it holds.
			const int32 PlayReserve = Me.PropertyCardCount() > 0 ? Tuning.LotCost : 0;
			const bool bNeedTickets = FAIScoring::TicketShortfall(State, Context, Player) > 0 || Me.TicketCount() < 3;
			if (BuyTicket && bNeedTickets && Me.Cash - Tuning.TicketCost >= ClaimCash + PlayReserve && Noise(1.0f) > -1.2f)
			{
				return *BuyTicket;
			}
		}
		return End ? *End : Legal[0];
	}

	// Everything else: score the position each action leads to. A Resolve choice is judged together
	// with the best claim that follows it, so a removal that costs the turn's Build is weighed fairly.
	TArray<TPair<float, int32>> Scored;
	for (int32 Index = 0; Index < Legal.Num(); ++Index)
	{
		float Score = 0.0f;
		if (State.Phase == EGridlockPhase::Resolve)
		{
			TArray<FGameEvent> Events;
			const FBoardState After = URulesLibrary::Apply(State, Legal[Index], Context, Events);
			Score = FAIScoring::Evaluate(After, Context, Personality, Player);
			if (After.Phase == EGridlockPhase::Build && After.ActivePlayer == Player)
			{
				for (const FGameAction& Claim : URulesLibrary::GetLegalActions(After, Context))
				{
					Score = FMath::Max(Score, FAIScoring::ScoreAction(After, Claim, Context, Personality, Player));
				}
			}
		}
		else
		{
			Score = FAIScoring::ScoreAction(State, Legal[Index], Context, Personality, Player);
		}
		Scored.Emplace(Score + Noise(1.0f), Index);
	}
	Scored.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });

	// Hard: for turn-ending choices, judge the best few by the position after the opponent's reply.
	if (bAllowLookahead && Personality.bLookahead && State.Phase == EGridlockPhase::BuyUpgrade)
	{
		Scored.SetNum(FMath::Min(LookaheadCandidates, Scored.Num()));
		for (TPair<float, int32>& Candidate : Scored)
		{
			TArray<FGameEvent> Events;
			const FBoardState After = URulesLibrary::Apply(State, Legal[Candidate.Value], Context, Events);
			if (After.Phase != EGridlockPhase::GameOver && After.ActivePlayer != Player)
			{
				const FBoardState Reply = PlayGreedyTurn(WithGuessedOpponentHand(After, Context, Player), Context, Personality);
				Candidate.Key = FAIScoring::Evaluate(Reply, Context, Personality, Player);
			}
		}
		Scored.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });
	}

	// Easy: a weighted pick among the top few.
	const int32 Pool = FMath::Clamp(Personality.TopN, 1, Scored.Num());
	if (Pool == 1)
	{
		return Legal[Scored[0].Value];
	}
	const float Floor = Scored[Pool - 1].Key;
	float Total = 0.0f;
	for (int32 Index = 0; Index < Pool; ++Index)
	{
		Total += Scored[Index].Key - Floor + 0.1f;
	}
	float Roll = Random.FRand() * Total;
	for (int32 Index = 0; Index < Pool; ++Index)
	{
		Roll -= Scored[Index].Key - Floor + 0.1f;
		if (Roll <= 0.0f)
		{
			return Legal[Scored[Index].Value];
		}
	}
	return Legal[Scored[0].Value];
}

FGameAction UAIPlayer::ChooseAction(const FBoardState& State, const FRulesContext& Context)
{
	if (!Personality)
	{
		FGameAction End;
		End.Type = EGridlockActionType::EndPhase;
		End.Player = PlayerIndex;
		return End;
	}

	const FGameAction Action = ChooseFor(State, Context, *Personality, PlayerIndex);
	Intent = FAIScoring::FindIntent(State, Context, PlayerIndex);

	const FAIScoreTerms Terms = FAIScoring::ComputeTerms(State, Context, PlayerIndex);
	UE_LOG(LogGridlock, Verbose, TEXT("AI chose action %d on %s (progress %.2f, threat %.2f, economy %.2f, exposure %.2f, points %.1f; intent %s)"),
		static_cast<int32>(Action.Type), *Action.RouteId.ToString(), Terms.Progress, Terms.Threat, Terms.Economy,
		Terms.Exposure, Terms.Points, *Intent.ToString());
	return Action;
}

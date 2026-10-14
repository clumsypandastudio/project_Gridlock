#include "Model/RulesLibrary.h"
#include "Algo/Count.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "Model/Pathfinding.h"

#define LOCTEXT_NAMESPACE "GridlockRules"

int32 FRulesContext::GetCardsToWin() const
{
	return CardsToWinOverride > 0 ? CardsToWinOverride : (Tuning ? Tuning->CardsToWin : 0);
}

namespace GridlockRulesPrivate
{
	int32 Opponent(int32 Player)
	{
		return 1 - Player;
	}

	EPropertyTier NextTier(EPropertyTier Tier)
	{
		return static_cast<EPropertyTier>(static_cast<uint8>(Tier) + 1);
	}

	FText TierName(EPropertyTier Tier)
	{
		switch (Tier)
		{
		case EPropertyTier::Lot:      return LOCTEXT("Lot", "Lot");
		case EPropertyTier::Building: return LOCTEXT("Building", "Building");
		case EPropertyTier::Complex:  return LOCTEXT("Complex", "Complex");
		case EPropertyTier::Landmark: return LOCTEXT("Landmark", "Landmark");
		default:                      return FText::GetEmpty();
		}
	}

	void AddEvent(TArray<FGameEvent>& Events, EGridlockEventType Type, int32 Player,
		FName RouteId = NAME_None, int32 Amount = 0, FName CardId = NAME_None)
	{
		FGameEvent& Event = Events.AddDefaulted_GetRef();
		Event.Type = Type;
		Event.Player = Player;
		Event.RouteId = RouteId;
		Event.Amount = Amount;
		Event.CardId = CardId;
	}

	void SetPhase(FBoardState& State, EGridlockPhase Phase, TArray<FGameEvent>& Events)
	{
		State.Phase = Phase;
		AddEvent(Events, EGridlockEventType::PhaseChanged, State.ActivePlayer, NAME_None, static_cast<int32>(Phase));
	}

	FText ColorName(EGridlockColor Color)
	{
		switch (Color)
		{
		case EGridlockColor::Red:    return LOCTEXT("Red", "Red");
		case EGridlockColor::Orange: return LOCTEXT("Orange", "Orange");
		case EGridlockColor::Yellow: return LOCTEXT("Yellow", "Yellow");
		case EGridlockColor::Green:  return LOCTEXT("Green", "Green");
		case EGridlockColor::Blue:   return LOCTEXT("Blue", "Blue");
		case EGridlockColor::Purple: return LOCTEXT("Purple", "Purple");
		case EGridlockColor::Black:  return LOCTEXT("Black", "Black");
		case EGridlockColor::White:  return LOCTEXT("White", "White");
		case EGridlockColor::Wild:   return LOCTEXT("Wild", "Wild");
		case EGridlockColor::Lot:    return LOCTEXT("LotCard", "Lot");
		default:                     return LOCTEXT("Gray", "Gray");
		}
	}

	template <typename CardType>
	void Shuffle(TArray<CardType>& Cards, FRandomStream& Random)
	{
		for (int32 Index = Cards.Num() - 1; Index > 0; --Index)
		{
			Cards.Swap(Index, Random.RandRange(0, Index));
		}
	}

	/** Takes the top card; an empty deck first gets its discard pile shuffled in. False if both are empty. */
	template <typename CardType>
	bool DrawFrom(FBoardState& State, TArray<CardType>& Deck, TArray<CardType>& Discard, int32 DeckIndex, TArray<FGameEvent>& Events, CardType& OutCard)
	{
		if (Deck.IsEmpty())
		{
			if (Discard.IsEmpty())
			{
				return false;
			}
			Deck = MoveTemp(Discard);
			Discard.Reset();
			FRandomStream Random(State.RandomSeed * 31 + (++State.ShuffleCount) * 7919);
			Shuffle(Deck, Random);
			AddEvent(Events, EGridlockEventType::DeckReshuffled, State.ActivePlayer, NAME_None, DeckIndex);
		}
		OutCard = Deck.Last();
		Deck.RemoveAt(Deck.Num() - 1);
		return true;
	}

	/**
	 * How Player would pay for a route with tickets: the color used, how many of that color, and how
	 * many Wilds. A colored route takes its own color; a Gray route one color (Preferred, or else the
	 * color held most). Wilds >= 0 is the player's own choice of how many Wilds to use; otherwise only
	 * the Wilds needed after the color run out. False when the tickets aren't there.
	 */
	bool PlanRoutePayment(const FPlayerData& Player, const FRouteDef& Route, EGridlockColor Preferred, int32 Wilds,
		EGridlockColor& OutColor, int32& OutColored, int32& OutWild)
	{
		OutColor = Route.Color;
		if (Route.Color == EGridlockColor::Gray)
		{
			if (Preferred != EGridlockColor::Gray && Preferred != EGridlockColor::Wild)
			{
				OutColor = Preferred;
			}
			else
			{
				OutColor = EGridlockColor::Red;
				for (uint8 Color = static_cast<uint8>(EGridlockColor::Red); Color <= static_cast<uint8>(EGridlockColor::White); ++Color)
				{
					if (Player.TicketsOf(static_cast<EGridlockColor>(Color)) > Player.TicketsOf(OutColor))
					{
						OutColor = static_cast<EGridlockColor>(Color);
					}
				}
			}
		}
		if (Wilds >= 0)
		{
			OutWild = FMath::Min(Wilds, Route.Length);
			OutColored = Route.Length - OutWild;
			return Player.TicketsOf(OutColor) >= OutColored && Player.TicketsOf(EGridlockColor::Wild) >= OutWild;
		}
		OutColored = FMath::Min(Player.TicketsOf(OutColor), Route.Length);
		OutWild = Route.Length - OutColored;
		return OutWild <= Player.TicketsOf(EGridlockColor::Wild);
	}

	/**
	 * The one-action options: whether this action is still allowed this turn. A main action (buy cards,
	 * remove, claim) comes once a turn; an extra (place or upgrade) is free under bOneMainActionPerTurn.
	 */
	bool MainActionAllowed(const FBoardState& State, const UTuningData& Tuning, bool bIsBuy, FText& OutReason, bool bIsExtra = false)
	{
		const bool bContinuingBuys = bIsBuy && State.BuysThisTurn > 0;
		if (Tuning.bOneActionPerTurn && State.ActionsThisTurn > 0 && !bContinuingBuys)
		{
			OutReason = LOCTEXT("OneAction", "You've already taken your one action this turn.");
			return false;
		}
		if (Tuning.bOneMainActionPerTurn && !bIsExtra && State.ActionsThisTurn > 0 && !bContinuingBuys)
		{
			OutReason = LOCTEXT("OneMainAction", "You've had your main action this turn: you can still place, upgrade or sell a property, or press Done.");
			return false;
		}
		return true;
	}

	/** Rules v0.4: after the main action, go straight to placing, upgrading and selling. */
	void FinishMainAction(FBoardState& State, const UTuningData& Tuning, TArray<FGameEvent>& Events)
	{
		if (Tuning.bOneMainActionPerTurn && State.Phase != EGridlockPhase::BuyUpgrade)
		{
			SetPhase(State, EGridlockPhase::BuyUpgrade, Events);
		}
	}

	/** Pays a price: it always counts toward Spent, and comes out of Cash unless cash is unlimited. */
	void Pay(FPlayerData& Player, const UTuningData& Tuning, int32 Amount)
	{
		Player.Spent += Amount;
		if (!Tuning.bUnlimitedCash)
		{
			Player.Cash -= Amount;
		}
	}

	/**
	 * v0.8: Payer owes the toll for City if the opponent owns its property. Unlimited cash: the full toll,
	 * counted in the payer's Spent and the owner's Earned; otherwise what the payer has, up to the toll.
	 */
	void PayToll(FBoardState& State, const UTuningData& Tuning, int32 Payer, FName City, FName CardId, TArray<FGameEvent>& Events)
	{
		const int32* Owner = State.CityOwner.Find(City);
		if (!Owner || *Owner == Payer || Tuning.CityToll <= 0)
		{
			return;
		}
		const int32 Receiver = *Owner;
		FPlayerData& From = State.Players[Payer];
		const int32 Amount = Tuning.bUnlimitedCash ? Tuning.CityToll : FMath::Min(Tuning.CityToll, From.Cash);
		if (Amount <= 0)
		{
			return;
		}
		Pay(From, Tuning, Amount);
		if (!CardId.IsNone())
		{
			From.TollsByCard.FindOrAdd(CardId) += Amount;
		}
		FPlayerData& To = State.Players[Receiver];
		To.Earned += Amount;
		To.TollsByCity.FindOrAdd(City) += Amount;
		To.Cash += Tuning.bUnlimitedCash ? 0 : Amount;
		AddEvent(Events, EGridlockEventType::CityTollPaid, Payer, City, Amount, CardId);
	}

	/** v0.8: whether one of Player's routes reaches City (a start city alone doesn't count). */
	bool HasRouteInto(const FBoardState& State, const FRulesContext& Context, int32 Player, FName City)
	{
		for (const TPair<FName, int32>& Entry : State.RouteOwner)
		{
			const FRouteDef* Route = Entry.Value == Player ? Context.Map->FindRoute(Entry.Key) : nullptr;
			if (Route && Route->Touches(City))
			{
				return true;
			}
		}
		return false;
	}

	/** What the bank pays for a property: a share of everything spent on it. */
	int32 SellPrice(const UTuningData& Tuning, const FSlotState& Slot)
	{
		return FMath::FloorToInt(Slot.Invested * Tuning.SellFraction);
	}

	int32 RemovalCost(const FBoardState& State, const FRulesContext& Context, const FSlotState& Slot, bool bDemolish, int32 Player)
	{
		// An upgrade still under construction is removed at its current, working tier's value,
		// unless the full-price option charges its upgraded tier's value.
		const bool bFullPrice = Slot.bUpgrading && Context.Tuning->bUpgradeWindowFullPrice;
		const int32 Value = Context.Tuning->GetTierValue(bFullPrice ? NextTier(Slot.Tier) : Slot.Tier);
		const bool bCatchUp = URulesLibrary::IsCatchUpActive(State, Context, Player);
		return bDemolish ? Context.Tuning->GetDemolishCost(Value, bCatchUp) : Context.Tuning->GetBuyoutCost(Value, bCatchUp);
	}

	int32 KeepMinimum(const FBoardState& State, const FRulesContext& Context, const FPlayerData& Player)
	{
		const int32 Minimum = State.Phase == EGridlockPhase::Setup ? Context.Tuning->InitialKeep : Context.Tuning->MinKeep;
		return FMath::Min(Minimum, Player.PendingDraw.Num());
	}

	int32 ResolveTurnLimit(const FBoardState& State, const FRulesContext& Context)
	{
		// Most points, then most Route cards completed, then highest property value. A full tie has no winner.
		const int32 PointsDiff = State.Players[0].Points - State.Players[1].Points;
		if (PointsDiff != 0)
		{
			return PointsDiff > 0 ? 0 : 1;
		}
		const int32 CardsDiff = State.Players[0].Completed.Num() - State.Players[1].Completed.Num();
		if (CardsDiff != 0)
		{
			return CardsDiff > 0 ? 0 : 1;
		}
		const int32 ValueDiff = URulesLibrary::GetPropertyValue(State, Context, 0) - URulesLibrary::GetPropertyValue(State, Context, 1);
		if (ValueDiff != 0)
		{
			return ValueDiff > 0 ? 0 : 1;
		}
		return INDEX_NONE;
	}

	/** A card or points target reached (a target of 0 is off, as in Ticket to Ride). */
	bool HasImmediateWin(const FPlayerData& Player, const FRulesContext& Context)
	{
		const int32 CardsToWin = Context.GetCardsToWin();
		const int32 PointsToWin = Context.Tuning->PointsToWin;
		return (CardsToWin > 0 && Player.Completed.Num() >= CardsToWin) || (PointsToWin > 0 && Player.Points >= PointsToWin);
	}

	/** Option: each unfinished card in hand subtracts its points when the game ends. */
	void ApplyUnfinishedCards(FBoardState& State, const FRulesContext& Context)
	{
		if (!Context.Tuning->bUnfinishedCardsCountAgainst)
		{
			return;
		}
		for (FPlayerData& Player : State.Players)
		{
			for (const FName CardId : Player.Hand)
			{
				const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
				Player.Points -= Card ? Card->Points : 0;
			}
		}
	}

	void EndGame(FBoardState& State, int32 Winner, TArray<FGameEvent>& Events)
	{
		State.Phase = EGridlockPhase::GameOver;
		State.Winner = Winner;
		AddEvent(Events, EGridlockEventType::GameOver, Winner);
	}

	/** The automatic part of the Start phase, in the order the Rules tab lists it. */
	void BeginTurn(FBoardState& State, const FRulesContext& Context, TArray<FGameEvent>& Events)
	{
		const int32 Player = State.ActivePlayer;
		const int32 Other = Opponent(Player);
		const UTuningData& Tuning = *Context.Tuning;

		State.bDrewThisTurn = false;
		State.bRemovedThisTurn = false;
		State.BuysThisTurn = 0;
		State.TicketBuysThisTurn = 0;
		State.PropertyBuysThisTurn = 0;
		State.ActionsThisTurn = 0;
		State.SalesThisTurn = 0;
		State.CardsKeptThisTurn.Reset();
		AddEvent(Events, EGridlockEventType::TurnStarted, Player, NAME_None, State.Round);
		SetPhase(State, EGridlockPhase::Start, Events);

		for (TPair<FName, FSlotState>& Entry : State.Slots)
		{
			FSlotState& Slot = Entry.Value;
			if (Slot.Owner == Player && Slot.bUpgrading)
			{
				Slot.Tier = NextTier(Slot.Tier);
				Slot.bUpgrading = false;
				AddEvent(Events, EGridlockEventType::UpgradeFinished, Player, Entry.Key, static_cast<int32>(Slot.Tier));
			}
		}

		FPlayerData& Me = State.Players[Player];
		Me.Cash += Tuning.BaseIncome;
		AddEvent(Events, EGridlockEventType::IncomePaid, Player, NAME_None, Tuning.BaseIncome);

		// Tolls on my routes carrying an opponent property. Pay what you have; no debt.
		for (const TPair<FName, int32>& Owner : State.RouteOwner)
		{
			const FSlotState* Slot = State.Slots.Find(Owner.Key);
			if (Owner.Value != Player || !Slot || Slot->Owner != Other)
			{
				continue;
			}
			const int32 Toll = FMath::Min(Tuning.GetToll(Slot->Tier), Me.Cash);
			if (Toll > 0)
			{
				Me.Cash -= Toll;
				State.Players[Other].Cash += Toll;
				AddEvent(Events, EGridlockEventType::TollPaid, Player, Owner.Key, Toll);
			}
		}

		int32 Landmarks = 0;
		for (TPair<FName, FSlotState>& Entry : State.Slots)
		{
			FSlotState& Slot = Entry.Value;
			if (Slot.Owner == Player && Slot.Tier == EPropertyTier::Landmark)
			{
				++Landmarks;
			}
			if (Slot.RubbleBy == Player && Slot.RubbleTicks > 0 && --Slot.RubbleTicks == 0)
			{
				Slot.RubbleBy = INDEX_NONE;
				AddEvent(Events, EGridlockEventType::RubbleCleared, Player, Entry.Key);
			}
		}
		if (Landmarks > 0)
		{
			Me.Points += Landmarks * Tuning.LandmarkPointsPerTurn;
			AddEvent(Events, EGridlockEventType::LandmarkPointsScored, Player, NAME_None, Landmarks * Tuning.LandmarkPointsPerTurn);
		}

		// Rules v0.4: with every Route card finished, new ones are dealt (keep at least MinKeep). Free.
		if (Tuning.bRouteCardsWhenHandEmpty && Me.Hand.IsEmpty() && Me.PendingDraw.IsEmpty())
		{
			for (int32 Count = 0; Count < Tuning.DrawCount && !State.Deck.IsEmpty(); ++Count)
			{
				Me.PendingDraw.Add(State.Deck.Last());
				State.Deck.RemoveAt(State.Deck.Num() - 1);
				AddEvent(Events, EGridlockEventType::CardsDrawn, Player, NAME_None, 0, Me.PendingDraw.Last());
			}
		}
	}

	/** Ticket to Ride's end: Route cards score or cost their points, then the longest-route bonus, then the winner. */
	void FinishGame(FBoardState& State, const FRulesContext& Context, TArray<FGameEvent>& Events)
	{
		const UTuningData& Tuning = *Context.Tuning;
		if (!Tuning.bScoreCardsAtEnd)
		{
			ApplyUnfinishedCards(State, Context);
		}
		for (int32 Player = 0; Player < State.Players.Num() && Tuning.bScoreCardsAtEnd; ++Player)
		{
			const TArray<FName> Hand = State.Players[Player].Hand;
			for (const FName CardId : Hand)
			{
				const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
				FPlayerData& Who = State.Players[Player];
				if (URulesLibrary::IsCardComplete(State, Context, Player, CardId))
				{
					Who.Hand.Remove(CardId);
					Who.Completed.Add(CardId);
					Who.Points += Card->Points;
					AddEvent(Events, EGridlockEventType::CardScored, Player, NAME_None, Card->Points, CardId);

					// v0.8: the card's path pays the toll in every opponent city it visits.
					if (Tuning.bCityTollOnCardPaths && Tuning.CityPropertyPrice > 0)
					{
						TArray<FName> PathRoutes;
						FGridlockPathfinding::ShortestDistance(*Context.Map, Card->CityA, Card->CityB, [&State, Player](const FRouteDef& Route)
						{
							return State.RouteOwner.FindRef(Route.Id) == Player && !URulesLibrary::IsRouteSevered(State, Route.Id, Player);
						}, &PathRoutes);
						TSet<FName> Visited = { Card->CityA, Card->CityB };
						for (const FName RouteId : PathRoutes)
						{
							if (const FRouteDef* Step = Context.Map->FindRoute(RouteId))
							{
								Visited.Add(Step->CityA);
								Visited.Add(Step->CityB);
							}
						}
						for (const FName City : Visited)
						{
							PayToll(State, Tuning, Player, City, CardId, Events);
						}
					}
				}
				else if (Tuning.bUnfinishedCardsCountAgainst)
				{
					Who.Points -= Card->Points;
					AddEvent(Events, EGridlockEventType::CardFailed, Player, NAME_None, -Card->Points, CardId);
				}
			}
		}
		if (Tuning.LongestRouteBonus > 0 && State.Players.Num() == 2)
		{
			const int32 Longest[2] = { URulesLibrary::GetLongestRoute(State, Context, 0), URulesLibrary::GetLongestRoute(State, Context, 1) };
			const int32 Best = FMath::Max(Longest[0], Longest[1]);
			for (int32 Player = 0; Player < 2 && Best > 0; ++Player)
			{
				if (Longest[Player] == Best)
				{
					State.Players[Player].Points += Tuning.LongestRouteBonus;
					AddEvent(Events, EGridlockEventType::LongestRouteBonus, Player, NAME_None, Tuning.LongestRouteBonus);
				}
			}
		}
		EndGame(State, ResolveTurnLimit(State, Context), Events);
	}

	/** The face-up ticket at Slot is replaced from the deck (or removed when none is left). */
	void RefillFaceUpSlot(FBoardState& State, int32 Slot, TArray<FGameEvent>& Events)
	{
		EGridlockColor Color = EGridlockColor::Gray;
		if (DrawFrom(State, State.TicketDeck, State.TicketDiscard, 0, Events, Color))
		{
			State.FaceUp[Slot] = Color;
		}
		else
		{
			State.FaceUp.RemoveAt(Slot);
		}
	}

	/** Fills the face-up row; with too many Wilds and Lots in it, the row is shuffled back into the deck and dealt again. */
	void RefillFaceUp(FBoardState& State, const UTuningData& Tuning, TArray<FGameEvent>& Events)
	{
		for (int32 Attempt = 0; Attempt < 4; ++Attempt) // a few redeals at most, in case the deck is mostly Wild
		{
			EGridlockColor Color = EGridlockColor::Gray;
			while (State.FaceUp.Num() < Tuning.FaceUpCount && DrawFrom(State, State.TicketDeck, State.TicketDiscard, 0, Events, Color))
			{
				State.FaceUp.Add(Color);
			}
			// Wilds and Lots together: 3 Wilds, 3 Lots, 2 + 1 either way.
			const int32 Specials = Algo::Count(State.FaceUp, EGridlockColor::Wild) + Algo::Count(State.FaceUp, EGridlockColor::Lot);
			if (Specials < Tuning.FaceUpWildReset || State.TicketDeck.Num() + State.TicketDiscard.Num() < Tuning.FaceUpCount)
			{
				return;
			}
			// The row goes back into the deck, the deck is shuffled, and 5 new cards are dealt.
			State.TicketDeck.Append(State.FaceUp);
			State.FaceUp.Reset();
			FRandomStream Random(State.RandomSeed * 31 + (++State.ShuffleCount) * 7919);
			Shuffle(State.TicketDeck, Random);
			AddEvent(Events, EGridlockEventType::FaceUpReset, State.ActivePlayer, NAME_None, Specials);
		}
	}

	/** The automatic Score phase, then the next player's turn (or the end of the game). */
	void ScoreAndAdvance(FBoardState& State, const FRulesContext& Context, TArray<FGameEvent>& Events)
	{
		const int32 Player = State.ActivePlayer;
		SetPhase(State, EGridlockPhase::Score, Events);

		const TArray<FName> Hand = Context.Tuning->bScoreCardsAtEnd ? TArray<FName>() : State.Players[Player].Hand;
		for (const FName CardId : Hand)
		{
			const bool bHeldLongEnough = !Context.Tuning->bHoldBeforeScoring || !State.CardsKeptThisTurn.Contains(CardId);
			if (!bHeldLongEnough || !URulesLibrary::IsCardComplete(State, Context, Player, CardId))
			{
				continue;
			}
			const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
			FPlayerData& Me = State.Players[Player];
			Me.Hand.Remove(CardId);
			Me.Completed.Add(CardId);
			Me.Points += Card->Points;
			AddEvent(Events, EGridlockEventType::CardScored, Player, NAME_None, Card->Points, CardId);
			const int32 Payout = Card->Points * Context.Tuning->CashPerCardPoint;
			if (Payout > 0)
			{
				Me.Cash += Payout;
				AddEvent(Events, EGridlockEventType::CardPaidOut, Player, NAME_None, Payout, CardId);
			}
		}

		if (HasImmediateWin(State.Players[Player], Context))
		{
			ApplyUnfinishedCards(State, Context);
			EndGame(State, Player, Events);
			return;
		}

		// Trains: once a player ends a turn with FinalRoundTrains or fewer, everyone gets one more turn.
		if (Context.Tuning->TrainsPerPlayer > 0)
		{
			if (State.FinalTurnsLeft > 0 && --State.FinalTurnsLeft == 0)
			{
				FinishGame(State, Context, Events);
				return;
			}
			if (State.FinalTurnsLeft < 0 && State.Players[Player].Trains <= Context.Tuning->FinalRoundTrains)
			{
				State.FinalTurnsLeft = State.Players.Num();
				AddEvent(Events, EGridlockEventType::FinalRound, Player, NAME_None, State.Players[Player].Trains);
			}
		}

		State.ActivePlayer = Opponent(Player);
		if (State.ActivePlayer == State.FirstPlayer)
		{
			++State.Round;
			if (State.Round > Context.Tuning->TurnLimit)
			{
				FinishGame(State, Context, Events);
				return;
			}
		}
		BeginTurn(State, Context, Events);
	}

	/** Cities a Landmark reaches: its route's ends plus LandmarkRange - 1 hops out. */
	TSet<FName> LandmarkReach(const FRulesContext& Context, const FRouteDef& LandmarkRoute)
	{
		TSet<FName> Reach = { LandmarkRoute.CityA, LandmarkRoute.CityB };
		TArray<FName> Frontier = { LandmarkRoute.CityA, LandmarkRoute.CityB };
		for (int32 Hop = 1; Hop < Context.Tuning->LandmarkRange; ++Hop)
		{
			TArray<FName> NextFrontier;
			for (const FName City : Frontier)
			{
				for (const FRouteDef& Route : Context.Map->Routes)
				{
					if (Route.Touches(City) && !Reach.Contains(Route.OtherEnd(City)))
					{
						Reach.Add(Route.OtherEnd(City));
						NextFrontier.Add(Route.OtherEnd(City));
					}
				}
			}
			Frontier = MoveTemp(NextFrontier);
		}
		return Reach;
	}
}


FBoardState URulesLibrary::MakeInitialState(const FRulesContext& Context, int32 Seed)
{
	FBoardState State;
	State.RandomSeed = Seed;
	if (!Context.IsValid())
	{
		return State;
	}

	for (const FRouteDef& Route : Context.Map->Routes)
	{
		State.RouteOwner.Add(Route.Id, INDEX_NONE);
		State.Slots.Add(Route.Id, FSlotState());
	}

	FRandomStream Random(Seed);
	State.FirstPlayer = Random.RandRange(0, 1);
	State.ActivePlayer = State.FirstPlayer;

	TArray<FName> StartCities;
	for (const FCityDef& City : Context.Map->Cities)
	{
		if (City.bIsStart)
		{
			StartCities.Add(City.Id);
		}
	}

	State.Players.SetNum(2);
	for (int32 Index = 0; Index < State.Players.Num(); ++Index)
	{
		FPlayerData& Player = State.Players[Index];
		Player.StartCity = StartCities.IsValidIndex(Index) ? StartCities[Index] : NAME_None;
		Player.Cash = Index == State.FirstPlayer ? Context.Tuning->StartCashFirstPlayer : Context.Tuning->StartCashSecondPlayer;
		Player.Trains = Context.Tuning->TrainsPerPlayer;
	}

	for (const FRouteCardDef& Card : Context.Deck->Cards)
	{
		State.Deck.Add(Card.Id);
	}
	for (int32 Index = State.Deck.Num() - 1; Index > 0; --Index)
	{
		State.Deck.Swap(Index, Random.RandRange(0, Index));
	}

	// Each player is dealt InitialDeal cards and keeps at least InitialKeep in the Setup phase.
	for (FPlayerData& Player : State.Players)
	{
		for (int32 Count = 0; Count < Context.Tuning->InitialDeal && !State.Deck.IsEmpty(); ++Count)
		{
			Player.PendingDraw.Add(State.Deck.Last());
			State.Deck.RemoveAt(State.Deck.Num() - 1);
		}
		Player.Tickets.Init(0, GridlockColorCount);
		Player.PropertyCards.Init(0, GridlockTierCount);
	}

	// Card economy: shuffle the ticket and property decks, then deal the free starting tickets and Lots.
	const UTuningData& Tuning = *Context.Tuning;
	if (Tuning.bCardEconomy)
	{
		for (const EGridlockColor Color : Tuning.GetDeckColors())
		{
			for (int32 Count = 0; Count < Tuning.TicketsPerColor; ++Count)
			{
				State.TicketDeck.Add(Color);
			}
		}
		for (int32 Count = 0; Count < Tuning.RouteLotCards; ++Count)
		{
			State.TicketDeck.Add(EGridlockColor::Lot);
		}
		for (int32 Count = 0; Count < Tuning.WildTickets; ++Count)
		{
			State.TicketDeck.Add(EGridlockColor::Wild);
		}
		for (uint8 Tier = static_cast<uint8>(EPropertyTier::Lot); Tier <= static_cast<uint8>(EPropertyTier::Landmark); ++Tier)
		{
			for (int32 Count = 0; Count < Tuning.GetPropertyCardsInDeck(static_cast<EPropertyTier>(Tier)); ++Count)
			{
				State.PropertyDeck.Add(static_cast<EPropertyTier>(Tier));
			}
		}
		GridlockRulesPrivate::Shuffle(State.TicketDeck, Random);
		GridlockRulesPrivate::Shuffle(State.PropertyDeck, Random);

		for (FPlayerData& Player : State.Players)
		{
			for (int32 Count = 0; Count < Tuning.StartingTickets && !State.TicketDeck.IsEmpty(); ++Count)
			{
				++Player.Tickets[static_cast<int32>(State.TicketDeck.Last())];
				State.TicketDeck.RemoveAt(State.TicketDeck.Num() - 1);
			}
			for (int32 Count = 0; Count < Tuning.StartingLotCards; ++Count)
			{
				const int32 Lot = State.PropertyDeck.FindLast(EPropertyTier::Lot);
				if (Lot != INDEX_NONE)
				{
					State.PropertyDeck.RemoveAt(Lot);
					++Player.PropertyCards[static_cast<int32>(EPropertyTier::Lot)];
				}
			}
		}
	}

	// Ticket to Ride: the face-up row.
	if (Context.Tuning->bCardEconomy && Context.Tuning->FaceUpCount > 0)
	{
		TArray<FGameEvent> Ignored;
		GridlockRulesPrivate::RefillFaceUp(State, *Context.Tuning, Ignored);
	}

	State.Phase = EGridlockPhase::Setup;
	State.Round = 1;
	return State;
}

void URulesLibrary::AdvanceSetup(FBoardState& State, const FRulesContext& Context, TArray<FGameEvent>& OutEvents)
{
	if (State.Phase != EGridlockPhase::Setup || !State.Players[State.ActivePlayer].PendingDraw.IsEmpty())
	{
		return;
	}
	const int32 Second = GridlockRulesPrivate::Opponent(State.FirstPlayer);
	if (State.ActivePlayer == State.FirstPlayer && !State.Players[Second].PendingDraw.IsEmpty())
	{
		State.ActivePlayer = Second;
		return;
	}
	State.ActivePlayer = State.FirstPlayer;
	GridlockRulesPrivate::BeginTurn(State, Context, OutEvents);
}

TArray<FGameAction> URulesLibrary::GetLegalActions(const FBoardState& State, const FRulesContext& Context)
{
	TArray<FGameAction> Legal;
	if (!Context.IsValid() || State.Phase == EGridlockPhase::GameOver || !State.Players.IsValidIndex(State.ActivePlayer))
	{
		return Legal;
	}

	const int32 Player = State.ActivePlayer;
	TArray<FGameAction> Candidates;
	auto AddCandidate = [&Candidates, Player](EGridlockActionType Type, FName RouteId = NAME_None)
	{
		FGameAction& Action = Candidates.AddDefaulted_GetRef();
		Action.Type = Type;
		Action.Player = Player;
		Action.RouteId = RouteId;
	};

	const TArray<FName>& Pending = State.Players[Player].PendingDraw;
	if (!Pending.IsEmpty())
	{
		// Every subset of the drawn cards; IsLegal drops the ones that keep too few or too many.
		for (int32 Mask = 0; Mask < (1 << Pending.Num()); ++Mask)
		{
			AddCandidate(EGridlockActionType::KeepCards);
			for (int32 Index = 0; Index < Pending.Num(); ++Index)
			{
				if (Mask & (1 << Index))
				{
					Candidates.Last().CardIds.Add(Pending[Index]);
				}
			}
		}
	}
	else
	{
		if (State.Phase == EGridlockPhase::Start || State.Phase == EGridlockPhase::BuyUpgrade)
		{
			for (const TPair<FName, FSlotState>& Entry : State.Slots)
			{
				if (Entry.Value.Owner == Player)
				{
					AddCandidate(EGridlockActionType::SellProperty, Entry.Key);
				}
			}
		}
		switch (State.Phase)
		{
		case EGridlockPhase::Start:
			AddCandidate(EGridlockActionType::DrawCards);
			AddCandidate(EGridlockActionType::BuyTicket);
			AddCandidate(EGridlockActionType::BuyPropertyCard);
			for (int32 Slot = 0; Slot < State.FaceUp.Num(); ++Slot)
			{
				AddCandidate(EGridlockActionType::TakeFaceUp);
				Candidates.Last().Slot = Slot;
			}
			if (Context.Tuning->bClassicTurn)
			{
				for (const FRouteDef& Route : Context.Map->Routes)
				{
					AddCandidate(EGridlockActionType::ClaimRoute, Route.Id);
					if (State.Players[Player].LotCardCount() > 0)
					{
						AddCandidate(EGridlockActionType::PlaceRouteLot, Route.Id);
					}
				}
			}
			AddCandidate(EGridlockActionType::KeepLot);
			AddCandidate(EGridlockActionType::DiscardLot);
			if (Context.Tuning->bClassicTurn && Context.Tuning->CityPropertyPrice > 0)
			{
				for (const FCityDef& City : Context.Map->Cities)
				{
					AddCandidate(EGridlockActionType::BuyCityProperty, City.Id);
				}
			}
			break;
		case EGridlockPhase::Resolve:
			for (const TPair<FName, FSlotState>& Entry : State.Slots)
			{
				AddCandidate(EGridlockActionType::Buyout, Entry.Key);
				AddCandidate(EGridlockActionType::Demolish, Entry.Key);
			}
			break;
		case EGridlockPhase::Build:
			for (const FRouteDef& Route : Context.Map->Routes)
			{
				AddCandidate(EGridlockActionType::ClaimRoute, Route.Id);
			}
			break;
		case EGridlockPhase::BuyUpgrade:
			for (const TPair<FName, FSlotState>& Entry : State.Slots)
			{
				AddCandidate(EGridlockActionType::PlaceLot, Entry.Key);
				AddCandidate(EGridlockActionType::Upgrade, Entry.Key);
			}
			break;
		default:
			break;
		}
		AddCandidate(EGridlockActionType::EndPhase);
	}

	FText Reason;
	for (const FGameAction& Action : Candidates)
	{
		if (IsLegal(State, Action, Context, Reason))
		{
			Legal.Add(Action);
		}
	}
	return Legal;
}

bool URulesLibrary::IsLegal(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context, FText& OutReason)
{
	if (!Context.IsValid())
	{
		OutReason = LOCTEXT("NoContext", "The scenario is missing its map, deck or tuning asset.");
		return false;
	}
	if (State.Phase == EGridlockPhase::GameOver)
	{
		OutReason = LOCTEXT("GameOver", "The game is over.");
		return false;
	}
	if (!State.Players.IsValidIndex(Action.Player) || Action.Player != State.ActivePlayer)
	{
		OutReason = LOCTEXT("NotYourTurn", "It's not your turn.");
		return false;
	}

	const UTuningData& Tuning = *Context.Tuning;
	const FPlayerData& Me = State.Players[Action.Player];
	const bool bChoosingCards = !Me.PendingDraw.IsEmpty();
	if (bChoosingCards && Action.Type != EGridlockActionType::KeepCards)
	{
		OutReason = LOCTEXT("ChooseCardsFirst", "Choose which Route cards to keep first.");
		return false;
	}
	const bool bLotChoice = Action.Type == EGridlockActionType::KeepLot || Action.Type == EGridlockActionType::DiscardLot;
	if (Me.bLotPending != bLotChoice)
	{
		OutReason = Me.bLotPending ? LOCTEXT("LotChoiceFirst", "Keep or discard the Lot you drew first.") : LOCTEXT("NoLotDrawn", "You haven't drawn a Lot.");
		return false;
	}

	auto NeedCash = [&OutReason, &Me, &Tuning](int32 Cost)
	{
		if (Me.Cash >= Cost || Tuning.bUnlimitedCash)
		{
			return true;
		}
		OutReason = FText::Format(LOCTEXT("NeedCash", "You need ${0} for that, and you have ${1}."), FText::AsNumber(Cost), FText::AsNumber(Me.Cash));
		return false;
	};

	const FSlotState* Slot = State.Slots.Find(Action.RouteId);

	switch (Action.Type)
	{
	case EGridlockActionType::DrawCards:
		if (Tuning.bRouteCardsWhenHandEmpty)
		{
			OutReason = LOCTEXT("DrawWhenDone", "New Route cards come at the start of your turn once you've finished all of yours.");
			return false;
		}
		if (State.Phase != EGridlockPhase::Start)
		{
			OutReason = LOCTEXT("DrawOnlyAtStart", "You can only draw Route cards at the start of your turn.");
			return false;
		}
		if (State.bDrewThisTurn)
		{
			OutReason = LOCTEXT("AlreadyDrew", "You've already drawn Route cards this turn.");
			return false;
		}
		if (State.Deck.IsEmpty())
		{
			OutReason = LOCTEXT("DeckEmpty", "The Route card deck is empty.");
			return false;
		}
		if (Me.Hand.Num() + Tuning.MinKeep > Tuning.HandLimit)
		{
			OutReason = FText::Format(LOCTEXT("HandFull", "Your hand is full: you can hold at most {0} unfinished cards."), FText::AsNumber(Tuning.HandLimit));
			return false;
		}
		if (Tuning.bDrawOnlyWhenCardsDone && !AreCardsDone(State, Context, Action.Player))
		{
			OutReason = LOCTEXT("FinishCardsFirst", "Finish all your Route cards first: new ones come only once every card you hold is done.");
			return false;
		}
		if (State.BuysThisTurn > 0)
		{
			OutReason = Tuning.bClassicTurn ? LOCTEXT("TookInsteadOfDraw", "You've taken a ticket: take your second one.")
				: LOCTEXT("BoughtInsteadOfDraw", "You bought cards this turn, so you can't also draw Route cards.");
			return false;
		}
		if (!GridlockRulesPrivate::MainActionAllowed(State, Tuning, false, OutReason))
		{
			return false;
		}
		return NeedCash(Tuning.DrawCost);

	case EGridlockActionType::BuyTicket:
	case EGridlockActionType::BuyPropertyCard:
	{
		const bool bTicket = Action.Type == EGridlockActionType::BuyTicket;
		if (!Tuning.bCardEconomy || (Tuning.bClassicTurn && !bTicket))
		{
			OutReason = LOCTEXT("NoCards", "These rules don't use tickets or property cards.");
			return false;
		}
		if (State.Phase != EGridlockPhase::Start)
		{
			OutReason = LOCTEXT("BuyOnlyAtStart", "You can only buy cards at the start of your turn.");
			return false;
		}
		if (State.bDrewThisTurn)
		{
			OutReason = LOCTEXT("DrewInsteadOfBuy", "You drew Route cards this turn, so you can't also buy cards.");
			return false;
		}
		const int32 Limit = bTicket ? Tuning.MaxTicketsPerTurn : Tuning.MaxPropertyCardsPerTurn;
		const int32 Bought = bTicket ? State.TicketBuysThisTurn : State.PropertyBuysThisTurn;
		if (Limit > 0 && Bought >= Limit)
		{
			OutReason = bTicket
				? FText::Format(LOCTEXT("TicketLimit", "You can buy at most {0} tickets a turn."), FText::AsNumber(Limit))
				: FText::Format(LOCTEXT("PropertyLimit", "You can buy only {0} property card a turn."), FText::AsNumber(Limit));
			return false;
		}
		if (Tuning.bTicketsOrPropertyNotBoth && (bTicket ? State.PropertyBuysThisTurn : State.TicketBuysThisTurn) > 0)
		{
			OutReason = LOCTEXT("TicketsOrProperty", "A turn's buy is tickets or a property card, not both.");
			return false;
		}
		if (Tuning.CardHandLimit > 0 && Me.TicketCount() + Me.PropertyCardCount() >= Tuning.CardHandLimit)
		{
			OutReason = FText::Format(LOCTEXT("CardHandFull", "You can hold at most {0} tickets and property cards."), FText::AsNumber(Tuning.CardHandLimit));
			return false;
		}
		const bool bDeckEmpty = bTicket ? State.TicketDeck.IsEmpty() && State.TicketDiscard.IsEmpty()
			: State.PropertyDeck.IsEmpty() && State.PropertyDiscard.IsEmpty();
		if (bDeckEmpty)
		{
			OutReason = bTicket ? LOCTEXT("TicketsGone", "There are no tickets left to buy.") : LOCTEXT("PropertiesGone", "There are no property cards left to buy.");
			return false;
		}
		if (!GridlockRulesPrivate::MainActionAllowed(State, Tuning, true, OutReason))
		{
			return false;
		}
		return NeedCash(bTicket ? Tuning.TicketCost : Tuning.PropertyCardCost);
	}

	case EGridlockActionType::TakeFaceUp:
	{
		if (!Tuning.bClassicTurn || State.Phase != EGridlockPhase::Start)
		{
			OutReason = LOCTEXT("FaceUpWhen", "You can take a face-up ticket at the start of your turn.");
			return false;
		}
		if (!State.FaceUp.IsValidIndex(Action.Slot))
		{
			OutReason = LOCTEXT("NoFaceUp", "There's no face-up ticket there.");
			return false;
		}
		if (State.bDrewThisTurn)
		{
			OutReason = LOCTEXT("DrewInsteadOfTake", "You drew Route cards this turn.");
			return false;
		}
		if (State.TicketBuysThisTurn >= Tuning.MaxTicketsPerTurn)
		{
			OutReason = FText::Format(LOCTEXT("TakeLimit", "You take {0} tickets a turn."), FText::AsNumber(Tuning.MaxTicketsPerTurn));
			return false;
		}
		if (State.TicketBuysThisTurn > 0 && State.FaceUp[Action.Slot] == EGridlockColor::Wild)
		{
			OutReason = LOCTEXT("WildSecond", "A face-up Wild counts as both of your tickets: you can only take it first.");
			return false;
		}
		if (State.TicketBuysThisTurn > 0 && State.FaceUp[Action.Slot] == EGridlockColor::Lot)
		{
			OutReason = LOCTEXT("LotSecond", "A face-up Lot is your whole turn: you can only take it first.");
			return false;
		}
		return NeedCash(Tuning.GetFaceUpPrice(State.FaceUp[Action.Slot]));
	}

	case EGridlockActionType::KeepLot:
		return NeedCash(FMath::Max(0, Tuning.RouteLotPrice - Tuning.TicketCost));

	case EGridlockActionType::BuyCityProperty:
	{
		const FCityDef* City = Context.Map->FindCity(Action.RouteId);
		if (Tuning.CityPropertyPrice <= 0 || !Tuning.bClassicTurn || State.Phase != EGridlockPhase::Start || !City)
		{
			OutReason = LOCTEXT("CityPropertyWhen", "You can buy a city property on your turn.");
			return false;
		}
		if (const int32* CityOwner = State.CityOwner.Find(Action.RouteId))
		{
			OutReason = *CityOwner == Action.Player ? LOCTEXT("CityYours", "You already own this city.") : LOCTEXT("CityTheirs", "Your opponent already owns this city.");
			return false;
		}
		if (State.BuysThisTurn > 0)
		{
			OutReason = LOCTEXT("PropertyWholeTurn", "Buying a property is a whole turn: you've already taken a ticket this turn.");
			return false;
		}
		if (!GridlockRulesPrivate::HasRouteInto(State, Context, Action.Player, Action.RouteId))
		{
			OutReason = LOCTEXT("CityNeedsRoute", "Build a route into this city first: you can only buy where your routes reach.");
			return false;
		}
		return NeedCash(Tuning.CityPropertyPrice);
	}

	case EGridlockActionType::DiscardLot:
		return true;

	case EGridlockActionType::PlaceRouteLot:
	{
		const FRouteDef* Route = Context.Map->FindRoute(Action.RouteId);
		if (!Tuning.bClassicTurn || State.Phase != EGridlockPhase::Start || !Route)
		{
			OutReason = LOCTEXT("PlaceLotWhen", "You can place a Lot on your turn.");
			return false;
		}
		if (Me.LotCardCount() == 0)
		{
			OutReason = LOCTEXT("NoLotCards", "You have no Lot cards: they come from the ticket deck or the face-up row.");
			return false;
		}
		if (State.RouteOwner.FindRef(Action.RouteId) != INDEX_NONE)
		{
			OutReason = LOCTEXT("LotOnClaimed", "Lots only go on open routes.");
			return false;
		}
		const int32 Slots = Tuning.GetLotSlots(Route->Length);
		const FRouteLots* Lots = State.RouteLots.Find(Action.RouteId);
		if (Slots == 0 || (Lots && Lots->Owners.Num() >= Slots))
		{
			OutReason = FText::Format(LOCTEXT("LotsFull", "All {0} lot slots on this route are taken."), FText::AsNumber(Slots));
			return false;
		}
		return true;
	}

	case EGridlockActionType::KeepCards:
	{
		if (!bChoosingCards)
		{
			OutReason = LOCTEXT("NothingToKeep", "There are no drawn cards to keep.");
			return false;
		}
		TSet<FName> Seen;
		for (const FName CardId : Action.CardIds)
		{
			if (!Me.PendingDraw.Contains(CardId) || Seen.Contains(CardId))
			{
				OutReason = LOCTEXT("KeepOnlyDrawn", "You can only keep cards you just drew, each once.");
				return false;
			}
			Seen.Add(CardId);
		}
		const int32 Minimum = GridlockRulesPrivate::KeepMinimum(State, Context, Me);
		if (Action.CardIds.Num() < Minimum)
		{
			OutReason = FText::Format(LOCTEXT("KeepAtLeast", "Keep at least {0} of these cards."), FText::AsNumber(Minimum));
			return false;
		}
		if (Me.Hand.Num() + Action.CardIds.Num() > Tuning.HandLimit)
		{
			OutReason = FText::Format(LOCTEXT("KeepTooMany", "You can hold at most {0} unfinished cards."), FText::AsNumber(Tuning.HandLimit));
			return false;
		}
		return true;
	}

	case EGridlockActionType::Buyout:
	case EGridlockActionType::Demolish:
		if (State.Phase != EGridlockPhase::Resolve)
		{
			OutReason = LOCTEXT("RemoveOnlyInResolve", "You can only remove properties in the Resolve step.");
			return false;
		}
		if (!Slot || Slot->Owner == INDEX_NONE || Slot->Owner == Action.Player)
		{
			OutReason = LOCTEXT("NoOpponentProperty", "There's no opponent property there.");
			return false;
		}
		if (!IsRouteInRange(State, Context, Action.Player, Action.RouteId))
		{
			OutReason = LOCTEXT("PropertyOutOfRange", "That property is out of your range.");
			return false;
		}
		if (!GridlockRulesPrivate::MainActionAllowed(State, Tuning, false, OutReason))
		{
			return false;
		}
		return NeedCash(GridlockRulesPrivate::RemovalCost(State, Context, *Slot, Action.Type == EGridlockActionType::Demolish, Action.Player));

	case EGridlockActionType::ClaimRoute:
	{
		if (State.Phase != (Tuning.bClassicTurn ? EGridlockPhase::Start : EGridlockPhase::Build))
		{
			OutReason = LOCTEXT("ClaimOnlyInBuild", "You can only claim routes in the Build step.");
			return false;
		}
		if (Tuning.bClassicTurn && State.BuysThisTurn > 0)
		{
			OutReason = LOCTEXT("TookInsteadOfClaim", "You've taken a ticket this turn: take your second one.");
			return false;
		}
		const FRouteDef* Route = Context.Map->FindRoute(Action.RouteId);
		const int32* Owner = State.RouteOwner.Find(Action.RouteId);
		if (!Route || !Owner)
		{
			OutReason = LOCTEXT("UnknownRoute", "That route isn't on the map.");
			return false;
		}
		if (*Owner != INDEX_NONE)
		{
			OutReason = LOCTEXT("AlreadyClaimed", "That route is already claimed.");
			return false;
		}
		const TSet<FName> Network = Tuning.bRoutesMustConnect ? GetNetworkCities(State, Context, Action.Player) : TSet<FName>();
		if (Tuning.bRoutesMustConnect && !Network.Contains(Route->CityA) && !Network.Contains(Route->CityB))
		{
			OutReason = LOCTEXT("NotConnected", "That route doesn't connect to your network.");
			return false;
		}
		if (Slot && Slot->Owner != INDEX_NONE && Slot->Owner != Action.Player && Slot->Tier >= EPropertyTier::Building)
		{
			OutReason = FText::Format(LOCTEXT("RouteBlocked", "Your opponent's {0} blocks that route."), GridlockRulesPrivate::TierName(Slot->Tier));
			return false;
		}
		if (Tuning.bDrawReplacesBuild && State.bDrewThisTurn)
		{
			OutReason = LOCTEXT("DrewInsteadOfBuild", "You drew Route cards this turn, so you can't claim a route.");
			return false;
		}
		if (Tuning.bRemovalUsesBuild && State.bRemovedThisTurn)
		{
			OutReason = LOCTEXT("RemovedInsteadOfBuild", "You removed a property this turn, so you can't claim a route.");
			return false;
		}
		if (!GridlockRulesPrivate::MainActionAllowed(State, Tuning, false, OutReason))
		{
			return false;
		}
		if (GetRouteBlocker(State, Context, Action.RouteId) == GridlockRulesPrivate::Opponent(Action.Player))
		{
			OutReason = LOCTEXT("RouteBlockedByLots", "Your opponent owns every lot on this route: only they can build it.");
			return false;
		}
		if (Tuning.TrainsPerPlayer > 0 && Me.Trains < Route->Length)
		{
			OutReason = FText::Format(LOCTEXT("NeedTrains", "This route takes {0} trains and you have {1} left."), FText::AsNumber(Route->Length), FText::AsNumber(Me.Trains));
			return false;
		}
		EGridlockColor PayColor;
		int32 Colored = 0;
		int32 Wild = 0;
		if (Action.Wilds > Route->Length)
		{
			OutReason = FText::Format(LOCTEXT("TooManyWilds", "This route takes only {0} tickets."), FText::AsNumber(Route->Length));
			return false;
		}
		if (Tuning.bCardEconomy && !GridlockRulesPrivate::PlanRoutePayment(Me, *Route, Action.Color, Action.Wilds, PayColor, Colored, Wild))
		{
			OutReason = Action.Wilds >= 0
				? FText::Format(LOCTEXT("ChosenPayment", "Paying {0} {1} + {2} Wild needs that many: you hold {3} {1} and {4} Wild."),
					FText::AsNumber(Colored), GridlockRulesPrivate::ColorName(PayColor), FText::AsNumber(Wild),
					FText::AsNumber(Me.TicketsOf(PayColor)), FText::AsNumber(Me.TicketsOf(EGridlockColor::Wild)))
				: Route->Color == EGridlockColor::Gray
				? FText::Format(LOCTEXT("NeedGrayTickets", "You need {0} tickets of one color for this gray route (Wild tickets count)."), FText::AsNumber(Route->Length))
				: FText::Format(LOCTEXT("NeedTickets", "You need {0} {1} tickets for this route (Wild tickets count)."),
					FText::AsNumber(Route->Length), GridlockRulesPrivate::ColorName(Route->Color));
			return false;
		}
		return NeedCash(Route->Length * Tuning.CostPerLengthDot);
	}

	case EGridlockActionType::PlaceLot:
		if (State.Phase != EGridlockPhase::BuyUpgrade)
		{
			OutReason = LOCTEXT("BuyOnlyInBuyUpgrade", "You can only buy or upgrade in the Buy/Upgrade step.");
			return false;
		}
		if (!Slot)
		{
			OutReason = LOCTEXT("UnknownSlot", "That slot isn't on the map.");
			return false;
		}
		if (Slot->RubbleTicks > 0)
		{
			OutReason = LOCTEXT("Rubble", "That slot is still rubble.");
			return false;
		}
		if (Slot->Owner != INDEX_NONE)
		{
			OutReason = LOCTEXT("SlotTaken", "That slot already has a property.");
			return false;
		}
		if (!IsRouteInRange(State, Context, Action.Player, Action.RouteId))
		{
			OutReason = LOCTEXT("SlotOutOfRange", "That slot is out of your range.");
			return false;
		}
		if (Tuning.bCardEconomy && Me.PropertyCardsOf(EPropertyTier::Lot) == 0)
		{
			OutReason = LOCTEXT("NeedLotCard", "You need a Lot card: buy property cards at the start of your turn.");
			return false;
		}
		if (!GridlockRulesPrivate::MainActionAllowed(State, Tuning, false, OutReason, true))
		{
			return false;
		}
		return NeedCash(Tuning.LotCost);

	case EGridlockActionType::Upgrade:
		if (State.Phase != EGridlockPhase::BuyUpgrade)
		{
			OutReason = LOCTEXT("UpgradeOnlyInBuyUpgrade", "You can only buy or upgrade in the Buy/Upgrade step.");
			return false;
		}
		if (!Slot || Slot->Owner != Action.Player)
		{
			OutReason = LOCTEXT("NotYourProperty", "You can only upgrade your own properties.");
			return false;
		}
		if (Slot->bUpgrading)
		{
			OutReason = LOCTEXT("AlreadyUpgrading", "That property is already being upgraded.");
			return false;
		}
		if (Slot->Tier >= EPropertyTier::Landmark)
		{
			OutReason = LOCTEXT("MaxTier", "A Landmark can't be upgraded further.");
			return false;
		}
		if (!IsRouteInRange(State, Context, Action.Player, Action.RouteId))
		{
			OutReason = LOCTEXT("UpgradeOutOfRange", "That property is out of your range.");
			return false;
		}
		if (Tuning.bCardEconomy && Me.PropertyCardsOf(GridlockRulesPrivate::NextTier(Slot->Tier)) == 0)
		{
			OutReason = FText::Format(LOCTEXT("NeedUpgradeCard", "You need a {0} card to upgrade this: buy property cards at the start of your turn."),
				GridlockRulesPrivate::TierName(GridlockRulesPrivate::NextTier(Slot->Tier)));
			return false;
		}
		if (!GridlockRulesPrivate::MainActionAllowed(State, Tuning, false, OutReason, true))
		{
			return false;
		}
		return NeedCash(Tuning.GetCostToReach(GridlockRulesPrivate::NextTier(Slot->Tier)));

	case EGridlockActionType::SellProperty:
		if (!Tuning.bCanSellProperties)
		{
			OutReason = LOCTEXT("NoSelling", "These rules don't allow selling properties.");
			return false;
		}
		if (State.Phase != EGridlockPhase::Start && State.Phase != EGridlockPhase::BuyUpgrade)
		{
			OutReason = LOCTEXT("SellWhen", "You can sell a property at the start of your turn or after your main action.");
			return false;
		}
		if (!Slot || Slot->Owner != Action.Player)
		{
			OutReason = LOCTEXT("SellOwnOnly", "You can only sell your own properties.");
			return false;
		}
		if (State.SalesThisTurn >= Tuning.MaxSalesPerTurn)
		{
			OutReason = FText::Format(LOCTEXT("SaleLimit", "You can sell {0} property a turn."), FText::AsNumber(Tuning.MaxSalesPerTurn));
			return false;
		}
		return true;

	case EGridlockActionType::EndPhase:
		if (State.Phase == EGridlockPhase::Setup)
		{
			OutReason = LOCTEXT("SetupKeep", "Choose which Route cards to keep first.");
			return false;
		}
		if (Tuning.bClassicTurn)
		{
			// Ticket to Ride has no passing: only when nothing at all can be done does the turn just end.
			FGameAction Other = Action;
			FText Ignored;
			for (const EGridlockActionType Type : { EGridlockActionType::BuyTicket, EGridlockActionType::DrawCards })
			{
				Other.Type = Type;
				if (IsLegal(State, Other, Context, Ignored))
				{
					OutReason = LOCTEXT("NoPassing", "Take tickets, claim a route, or draw Route cards.");
					return false;
				}
			}
			Other.Type = EGridlockActionType::TakeFaceUp;
			for (Other.Slot = 0; Other.Slot < State.FaceUp.Num(); ++Other.Slot)
			{
				if (IsLegal(State, Other, Context, Ignored))
				{
					OutReason = LOCTEXT("NoPassing", "Take tickets, claim a route, or draw Route cards.");
					return false;
				}
			}
			for (const EGridlockActionType Type : { EGridlockActionType::ClaimRoute, EGridlockActionType::PlaceRouteLot })
			{
				Other.Type = Type;
				for (const FRouteDef& Route : Context.Map->Routes)
				{
					Other.RouteId = Route.Id;
					if (IsLegal(State, Other, Context, Ignored))
					{
						OutReason = LOCTEXT("NoPassing", "Take tickets, claim a route, or draw Route cards.");
						return false;
					}
				}
			}
			Other.Type = EGridlockActionType::BuyCityProperty;
			for (const FCityDef& City : Context.Map->Cities)
			{
				Other.RouteId = City.Id;
				if (IsLegal(State, Other, Context, Ignored))
				{
					OutReason = LOCTEXT("NoPassing", "Take tickets, claim a route, or draw Route cards.");
					return false;
				}
			}
		}
		return true;
	}

	OutReason = LOCTEXT("UnknownAction", "Unknown action.");
	return false;
}

FBoardState URulesLibrary::Apply(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context, TArray<FGameEvent>& OutEvents)
{
	FBoardState Next = State;
	const UTuningData& Tuning = *Context.Tuning;
	const int32 Player = Action.Player;

	// Ticket to Ride: after a ticket, the turn ends once 2 are taken (or no second one can be).
	auto EndTurnIfTicketsDone = [&Next, &Context, &Tuning, &OutEvents, Player]()
	{
		FGameAction More;
		More.Player = Player;
		FText Reason;
		bool bCanTakeMore = false;
		if (Next.TicketBuysThisTurn < Tuning.MaxTicketsPerTurn)
		{
			More.Type = EGridlockActionType::BuyTicket;
			bCanTakeMore = IsLegal(Next, More, Context, Reason);
			More.Type = EGridlockActionType::TakeFaceUp;
			for (More.Slot = 0; More.Slot < Next.FaceUp.Num() && !bCanTakeMore; ++More.Slot)
			{
				bCanTakeMore = IsLegal(Next, More, Context, Reason);
			}
		}
		if (!bCanTakeMore)
		{
			GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents);
		}
	};

	switch (Action.Type)
	{
	case EGridlockActionType::DrawCards:
	{
		FPlayerData& Me = Next.Players[Player];
		Me.Cash -= Tuning.DrawCost;
		for (int32 Count = 0; Count < Tuning.DrawCount && !Next.Deck.IsEmpty(); ++Count)
		{
			Me.PendingDraw.Add(Next.Deck.Last());
			Next.Deck.RemoveAt(Next.Deck.Num() - 1);
			GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::CardsDrawn, Player, NAME_None, 0, Me.PendingDraw.Last());
		}
		Next.bDrewThisTurn = true;
		++Next.ActionsThisTurn;
		break;
	}

	case EGridlockActionType::BuyTicket:
	{
		EGridlockColor Color = EGridlockColor::Gray;
		if (GridlockRulesPrivate::DrawFrom(Next, Next.TicketDeck, Next.TicketDiscard, 0, OutEvents, Color))
		{
			FPlayerData& Me = Next.Players[Player];
			GridlockRulesPrivate::Pay(Me, Tuning, Tuning.TicketCost);
			++Me.Tickets[static_cast<int32>(Color)];
			Next.ActionsThisTurn += Next.BuysThisTurn == 0 ? 1 : 0;
			++Next.BuysThisTurn;
			++Next.TicketBuysThisTurn;
			if (Color == EGridlockColor::Lot)
			{
				// A Lot off the deck: keep it (paying the rest of its price) or discard it, before anything else.
				Me.bLotPending = true;
				GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::LotDrawn, Player);
				break;
			}
			GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::TicketBought, Player, NAME_None, static_cast<int32>(Color));
		}
		if (Tuning.bClassicTurn)
		{
			EndTurnIfTicketsDone();
			break;
		}
		FText Reason;
		if (!IsLegal(Next, Action, Context, Reason))
		{
			GridlockRulesPrivate::FinishMainAction(Next, Tuning, OutEvents); // the buy limit (or the cash) is reached
		}
		break;
	}

	case EGridlockActionType::TakeFaceUp:
	{
		const EGridlockColor Color = Next.FaceUp[Action.Slot];
		GridlockRulesPrivate::Pay(Next.Players[Player], Tuning, Tuning.GetFaceUpPrice(Color));
		++Next.Players[Player].Tickets[static_cast<int32>(Color)];
		Next.ActionsThisTurn += Next.BuysThisTurn == 0 ? 1 : 0;
		++Next.BuysThisTurn;
		const bool bWholeTurn = Color == EGridlockColor::Wild || Color == EGridlockColor::Lot; // a face-up Wild or Lot is the whole draw
		Next.TicketBuysThisTurn += bWholeTurn ? Tuning.MaxTicketsPerTurn : 1;
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::FaceUpTaken, Player, NAME_None, static_cast<int32>(Color));
		GridlockRulesPrivate::RefillFaceUpSlot(Next, Action.Slot, OutEvents);
		GridlockRulesPrivate::RefillFaceUp(Next, Tuning, OutEvents);
		EndTurnIfTicketsDone();
		break;
	}

	case EGridlockActionType::BuyPropertyCard:
	{
		EPropertyTier Tier = EPropertyTier::None;
		if (GridlockRulesPrivate::DrawFrom(Next, Next.PropertyDeck, Next.PropertyDiscard, 1, OutEvents, Tier))
		{
			FPlayerData& Me = Next.Players[Player];
			Me.Cash -= Tuning.PropertyCardCost;
			++Me.PropertyCards[static_cast<int32>(Tier)];
			Next.ActionsThisTurn += Next.BuysThisTurn == 0 ? 1 : 0;
			++Next.BuysThisTurn;
			++Next.PropertyBuysThisTurn;
			GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::PropertyCardBought, Player, NAME_None, static_cast<int32>(Tier));
		}
		FText Reason;
		if (!IsLegal(Next, Action, Context, Reason))
		{
			GridlockRulesPrivate::FinishMainAction(Next, Tuning, OutEvents);
		}
		break;
	}

	case EGridlockActionType::BuyCityProperty:
	{
		GridlockRulesPrivate::Pay(Next.Players[Player], Tuning, Tuning.CityPropertyPrice);
		Next.CityOwner.Add(Action.RouteId, Player);
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::CityPropertyBought, Player, Action.RouteId, Tuning.CityPropertyPrice);
		GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents); // buying is the turn
		break;
	}

	case EGridlockActionType::KeepLot:
	{
		FPlayerData& Me = Next.Players[Player];
		const int32 Rest = FMath::Max(0, Tuning.RouteLotPrice - Tuning.TicketCost);
		GridlockRulesPrivate::Pay(Me, Tuning, Rest);
		Me.bLotPending = false;
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::LotKept, Player, NAME_None, Rest);
		EndTurnIfTicketsDone();
		break;
	}

	case EGridlockActionType::DiscardLot:
	{
		FPlayerData& Me = Next.Players[Player];
		--Me.Tickets[static_cast<int32>(EGridlockColor::Lot)];
		Next.TicketDiscard.Add(EGridlockColor::Lot);
		Me.bLotPending = false;
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::LotDiscarded, Player);
		EndTurnIfTicketsDone();
		break;
	}

	case EGridlockActionType::PlaceRouteLot:
	{
		--Next.Players[Player].Tickets[static_cast<int32>(EGridlockColor::Lot)];
		FRouteLots& Lots = Next.RouteLots.FindOrAdd(Action.RouteId);
		Lots.Owners.Add(Player);
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::RouteLotPlaced, Player, Action.RouteId, Lots.Owners.Num());
		if (GetRouteBlocker(Next, Context, Action.RouteId) == Player)
		{
			GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::RouteBlocked, Player, Action.RouteId);
		}
		GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents); // one Lot a turn: placing ends it, so the opponent can answer
		break;
	}

	case EGridlockActionType::KeepCards:
	{
		FPlayerData& Me = Next.Players[Player];
		for (const FName CardId : Me.PendingDraw)
		{
			if (Action.CardIds.Contains(CardId))
			{
				Me.Hand.Add(CardId);
				Next.CardsKeptThisTurn.Add(CardId);
			}
			else
			{
				Next.Deck.Insert(CardId, 0); // back to the bottom of the deck
			}
		}
		Me.PendingDraw.Reset();
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::CardsKept, Player, NAME_None, Action.CardIds.Num());

		if (Next.Phase == EGridlockPhase::Setup)
		{
			AdvanceSetup(Next, Context, OutEvents);
		}
		else if (Next.bDrewThisTurn && Tuning.bClassicTurn)
		{
			GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents); // drawing Route cards was the turn
		}
		else if (Next.bDrewThisTurn)
		{
			GridlockRulesPrivate::SetPhase(Next, EGridlockPhase::Resolve, OutEvents);
		}
		// Otherwise the cards were dealt free at the start of the turn (v0.4), and the turn goes on.
		break;
	}

	case EGridlockActionType::Buyout:
	case EGridlockActionType::Demolish:
	{
		FSlotState& Slot = Next.Slots.FindChecked(Action.RouteId);
		const bool bDemolish = Action.Type == EGridlockActionType::Demolish;
		const int32 Cost = GridlockRulesPrivate::RemovalCost(State, Context, Slot, bDemolish, Player);

		Next.Players[Player].Cash -= Cost;
		if (!bDemolish)
		{
			Next.Players[Slot.Owner].Cash += Cost;
		}
		if (Slot.bUpgrading && !Tuning.bUpgradeWindowFullPrice)
		{
			Next.Players[Slot.Owner].Cash += Tuning.GetCostToReach(GridlockRulesPrivate::NextTier(Slot.Tier)); // the bank refunds the upgrade
		}
		Next.bRemovedThisTurn = true;
		++Next.ActionsThisTurn;
		GridlockRulesPrivate::AddEvent(OutEvents, bDemolish ? EGridlockEventType::PropertyDemolished : EGridlockEventType::PropertyRemoved,
			Player, Action.RouteId, Cost);

		Slot = FSlotState();
		Slot.RubbleTicks = Tuning.RubbleTicks;
		Slot.RubbleBy = Tuning.RubbleTicks > 0 ? Player : INDEX_NONE;
		GridlockRulesPrivate::SetPhase(Next, Tuning.bOneMainActionPerTurn ? EGridlockPhase::BuyUpgrade : EGridlockPhase::Build, OutEvents);
		break;
	}

	case EGridlockActionType::SellProperty:
	{
		FSlotState& Slot = Next.Slots.FindChecked(Action.RouteId);
		const int32 Price = GridlockRulesPrivate::SellPrice(Tuning, Slot);
		Next.Players[Player].Cash += Price;
		++Next.SalesThisTurn;
		Slot = FSlotState(); // an empty slot, free for anyone in range
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::PropertySold, Player, Action.RouteId, Price);
		break;
	}

	case EGridlockActionType::ClaimRoute:
	{
		const FRouteDef* Route = Context.Map->FindRoute(Action.RouteId);
		const int32 Cost = Route->Length * Tuning.CostPerLengthDot;
		FPlayerData& Me = Next.Players[Player];
		Me.Cash -= Cost;
		Next.RouteOwner[Action.RouteId] = Player;
		++Next.ActionsThisTurn;

		// Pay the tickets: the route's color (or the chosen one, for gray) first, then Wilds.
		EGridlockColor PayColor;
		int32 Colored = 0;
		int32 Wild = 0;
		if (Tuning.bCardEconomy && GridlockRulesPrivate::PlanRoutePayment(Me, *Route, Action.Color, Action.Wilds, PayColor, Colored, Wild))
		{
			Me.Tickets[static_cast<int32>(PayColor)] -= Colored;
			Me.Tickets[static_cast<int32>(EGridlockColor::Wild)] -= Wild;
			for (int32 Count = 0; Count < Colored; ++Count)
			{
				Next.TicketDiscard.Add(PayColor);
			}
			for (int32 Count = 0; Count < Wild; ++Count)
			{
				Next.TicketDiscard.Add(EGridlockColor::Wild);
			}
		}
		Me.Trains -= Tuning.TrainsPerPlayer > 0 ? Route->Length : 0;
		Me.Points += Tuning.GetRoutePoints(Route->Length);
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::RouteClaimed, Player, Action.RouteId, Cost);
		GridlockRulesPrivate::PayToll(Next, Tuning, Player, Route->CityA, NAME_None, OutEvents); // v0.8: building into an opponent's city
		GridlockRulesPrivate::PayToll(Next, Tuning, Player, Route->CityB, NAME_None, OutEvents);
		if (Tuning.bClassicTurn)
		{
			GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents); // claiming was the turn
			break;
		}
		GridlockRulesPrivate::SetPhase(Next, EGridlockPhase::BuyUpgrade, OutEvents);
		break;
	}

	case EGridlockActionType::PlaceLot:
	{
		FSlotState& Slot = Next.Slots.FindChecked(Action.RouteId);
		Next.Players[Player].Cash -= Tuning.LotCost;
		if (Tuning.bCardEconomy)
		{
			--Next.Players[Player].PropertyCards[static_cast<int32>(EPropertyTier::Lot)];
			Next.PropertyDiscard.Add(EPropertyTier::Lot);
		}
		++Next.ActionsThisTurn;
		Slot.Owner = Player;
		Slot.Tier = EPropertyTier::Lot;
		Slot.Invested = Tuning.LotCost;
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::PropertyPlaced, Player, Action.RouteId, Tuning.LotCost);
		GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents);
		break;
	}

	case EGridlockActionType::Upgrade:
	{
		FSlotState& Slot = Next.Slots.FindChecked(Action.RouteId);
		const EPropertyTier NewTier = GridlockRulesPrivate::NextTier(Slot.Tier);
		const int32 Cost = Tuning.GetCostToReach(NewTier);
		Next.Players[Player].Cash -= Cost;
		if (Tuning.bCardEconomy)
		{
			--Next.Players[Player].PropertyCards[static_cast<int32>(NewTier)];
			Next.PropertyDiscard.Add(NewTier);
		}
		++Next.ActionsThisTurn;
		Slot.bUpgrading = true;
		Slot.Invested += Cost;
		GridlockRulesPrivate::AddEvent(OutEvents, EGridlockEventType::UpgradeStarted, Player, Action.RouteId, static_cast<int32>(GridlockRulesPrivate::NextTier(Slot.Tier)));
		GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents);
		break;
	}

	case EGridlockActionType::EndPhase:
		switch (Next.Phase)
		{
		case EGridlockPhase::Start:
			if (Tuning.bClassicTurn)
			{
				GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents); // nothing could be done: the turn ends
				break;
			}
			// v0.4: after buying, Done goes on to placing and upgrading; before it, on to building.
			GridlockRulesPrivate::SetPhase(Next, Tuning.bOneMainActionPerTurn && Next.ActionsThisTurn > 0
				? EGridlockPhase::BuyUpgrade : EGridlockPhase::Resolve, OutEvents);
			break;
		case EGridlockPhase::Resolve:    GridlockRulesPrivate::SetPhase(Next, EGridlockPhase::Build, OutEvents); break;
		case EGridlockPhase::Build:      GridlockRulesPrivate::SetPhase(Next, EGridlockPhase::BuyUpgrade, OutEvents); break;
		case EGridlockPhase::BuyUpgrade: GridlockRulesPrivate::ScoreAndAdvance(Next, Context, OutEvents); break;
		default: break;
		}
		break;
	}

	return Next;
}

int32 URulesLibrary::PreviewCost(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context)
{
	if (!Context.IsValid())
	{
		return 0;
	}
	const FSlotState* Slot = State.Slots.Find(Action.RouteId);

	switch (Action.Type)
	{
	case EGridlockActionType::ClaimRoute:
	{
		const FRouteDef* Route = Context.Map->FindRoute(Action.RouteId);
		return Route ? Route->Length * Context.Tuning->CostPerLengthDot : 0;
	}
	case EGridlockActionType::DrawCards:
		return Context.Tuning->DrawCost;
	case EGridlockActionType::BuyTicket:
		return Context.Tuning->TicketCost;
	case EGridlockActionType::TakeFaceUp:
		return State.FaceUp.IsValidIndex(Action.Slot) ? Context.Tuning->GetFaceUpPrice(State.FaceUp[Action.Slot]) : Context.Tuning->TicketCost;
	case EGridlockActionType::KeepLot:
		return FMath::Max(0, Context.Tuning->RouteLotPrice - Context.Tuning->TicketCost);
	case EGridlockActionType::BuyCityProperty:
		return Context.Tuning->CityPropertyPrice;
	case EGridlockActionType::BuyPropertyCard:
		return Context.Tuning->PropertyCardCost;
	case EGridlockActionType::PlaceLot:
		return Context.Tuning->LotCost;
	case EGridlockActionType::Upgrade:
		return Slot && Slot->Tier < EPropertyTier::Landmark ? Context.Tuning->GetCostToReach(GridlockRulesPrivate::NextTier(Slot->Tier)) : 0;
	case EGridlockActionType::Buyout:
	case EGridlockActionType::Demolish:
		return Slot ? GridlockRulesPrivate::RemovalCost(State, Context, *Slot, Action.Type == EGridlockActionType::Demolish, Action.Player) : 0;
	case EGridlockActionType::SellProperty:
		return Slot ? -GridlockRulesPrivate::SellPrice(*Context.Tuning, *Slot) : 0;
	default:
		return 0;
	}
}

bool URulesLibrary::CanClaimLater(const FBoardState& State, const FRulesContext& Context, FName RouteId, FText& OutReason,
	EGridlockColor Color, int32 Wilds)
{
	FBoardState AtBuild = State;
	const bool bClassic = Context.Tuning && Context.Tuning->bClassicTurn; // Ticket to Ride claims right from the start of the turn
	if (!bClassic && (State.Phase == EGridlockPhase::Start || State.Phase == EGridlockPhase::Resolve))
	{
		AtBuild.Phase = EGridlockPhase::Build;
	}
	FGameAction Claim;
	Claim.Type = EGridlockActionType::ClaimRoute;
	Claim.Player = State.ActivePlayer;
	Claim.RouteId = RouteId;
	Claim.Color = Color;
	Claim.Wilds = Wilds;
	return IsLegal(AtBuild, Claim, Context, OutReason);
}

int32 URulesLibrary::CheckWin(const FBoardState& State, const FRulesContext& Context)
{
	if (State.Phase == EGridlockPhase::GameOver)
	{
		return State.Winner;
	}
	if (!Context.Tuning || State.Players.Num() != 2)
	{
		return INDEX_NONE;
	}
	if (GridlockRulesPrivate::HasImmediateWin(State.Players[State.ActivePlayer], Context))
	{
		return State.ActivePlayer;
	}
	if (GridlockRulesPrivate::HasImmediateWin(State.Players[GridlockRulesPrivate::Opponent(State.ActivePlayer)], Context))
	{
		return GridlockRulesPrivate::Opponent(State.ActivePlayer);
	}
	return State.Round > Context.Tuning->TurnLimit ? GridlockRulesPrivate::ResolveTurnLimit(State, Context) : INDEX_NONE;
}

TSet<FName> URulesLibrary::GetNetworkCities(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	// Severed routes still count here: severing only stops a route counting toward Route cards.
	TSet<FName> Cities;
	if (!Context.Map || !State.Players.IsValidIndex(Player))
	{
		return Cities;
	}
	for (const TPair<FName, int32>& Entry : State.RouteOwner)
	{
		if (Entry.Value != Player)
		{
			continue;
		}
		if (const FRouteDef* Route = Context.Map->FindRoute(Entry.Key))
		{
			Cities.Add(Route->CityA);
			Cities.Add(Route->CityB);
		}
	}
	if (Cities.IsEmpty())
	{
		Cities.Add(State.Players[Player].StartCity);
	}
	return Cities;
}

bool URulesLibrary::IsRouteInRange(const FBoardState& State, const FRulesContext& Context, int32 Player, FName RouteId)
{
	const FRouteDef* Route = Context.Map ? Context.Map->FindRoute(RouteId) : nullptr;
	if (!Route)
	{
		return false;
	}
	const TSet<FName> Network = GetNetworkCities(State, Context, Player);
	if (Network.Contains(Route->CityA) || Network.Contains(Route->CityB))
	{
		return true;
	}

	// A working Landmark also reaches every route within LandmarkRange cities of its own route.
	if (!Context.Tuning)
	{
		return false;
	}
	for (const TPair<FName, FSlotState>& Entry : State.Slots)
	{
		if (Entry.Value.Owner != Player || Entry.Value.Tier != EPropertyTier::Landmark)
		{
			continue;
		}
		if (const FRouteDef* LandmarkRoute = Context.Map->FindRoute(Entry.Key))
		{
			const TSet<FName> Reach = GridlockRulesPrivate::LandmarkReach(Context, *LandmarkRoute);
			if (Reach.Contains(Route->CityA) || Reach.Contains(Route->CityB))
			{
				return true;
			}
		}
	}
	return false;
}

bool URulesLibrary::IsRouteSevered(const FBoardState& State, FName RouteId, int32 ForPlayer)
{
	const FSlotState* Slot = State.Slots.Find(RouteId);
	return Slot && Slot->Owner != INDEX_NONE && Slot->Owner != ForPlayer && Slot->Tier >= EPropertyTier::Complex;
}

bool URulesLibrary::CanStillComplete(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId)
{
	const FRouteCardDef* Card = Context.Deck ? Context.Deck->FindCard(CardId) : nullptr;
	if (!Card || !Context.Map)
	{
		return false;
	}
	auto CanUse = [&State, &Context, Player](const FRouteDef& Route)
	{
		const int32 RouteOwner = State.RouteOwner.FindRef(Route.Id);
		if (RouteOwner == Player)
		{
			return !IsRouteSevered(State, Route.Id, Player);
		}
		return RouteOwner == INDEX_NONE && GetRouteBlocker(State, Context, Route.Id) != GridlockRulesPrivate::Opponent(Player);
	};
	return FGridlockPathfinding::ShortestDistance(*Context.Map, Card->CityA, Card->CityB, CanUse) != INDEX_NONE;
}

bool URulesLibrary::AreCardsDone(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	if (!State.Players.IsValidIndex(Player))
	{
		return false;
	}
	// No way out: a card that can't be finished any more still has to be (by another way round), or no new cards.
	for (const FName CardId : State.Players[Player].Hand)
	{
		if (!IsCardComplete(State, Context, Player, CardId))
		{
			return false;
		}
	}
	return true;
}

bool URulesLibrary::IsCardComplete(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId)
{
	const FRouteCardDef* Card = Context.Deck ? Context.Deck->FindCard(CardId) : nullptr;
	if (!Card || !Context.Map)
	{
		return false;
	}
	auto CanUse = [&State, Player](const FRouteDef& Route)
	{
		const int32* Owner = State.RouteOwner.Find(Route.Id);
		return Owner && *Owner == Player && !IsRouteSevered(State, Route.Id, Player);
	};
	return FGridlockPathfinding::ShortestDistance(*Context.Map, Card->CityA, Card->CityB, CanUse) != INDEX_NONE;
}

int32 URulesLibrary::GetRouteBlocker(const FBoardState& State, const FRulesContext& Context, FName RouteId)
{
	const FRouteDef* Route = Context.Map ? Context.Map->FindRoute(RouteId) : nullptr;
	const FRouteLots* Lots = State.RouteLots.Find(RouteId);
	const int32 Slots = Route && Context.Tuning ? Context.Tuning->GetLotSlots(Route->Length) : 0;
	if (!Lots || Slots == 0 || Lots->Owners.Num() < Slots)
	{
		return INDEX_NONE;
	}
	const int32 First = Lots->Owners[0];
	return Lots->Owners.ContainsByPredicate([First](int32 Owner) { return Owner != First; }) ? INDEX_NONE : First;
}

int32 URulesLibrary::GetLongestRoute(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	TArray<const FRouteDef*> Owned;
	for (const TPair<FName, int32>& Entry : State.RouteOwner)
	{
		const FRouteDef* Route = Entry.Value == Player && Context.Map ? Context.Map->FindRoute(Entry.Key) : nullptr;
		if (Route && Owned.Num() < 64)
		{
			Owned.Add(Route);
		}
	}

	// Depth-first over trails: a city may be passed more than once, a route only once.
	TFunction<int32(FName, uint64)> Walk = [&Owned, &Walk](FName City, uint64 Used)
	{
		int32 Best = 0;
		for (int32 Index = 0; Index < Owned.Num(); ++Index)
		{
			const uint64 Bit = uint64(1) << Index;
			if (!(Used & Bit) && Owned[Index]->Touches(City))
			{
				Best = FMath::Max(Best, Owned[Index]->Length + Walk(Owned[Index]->OtherEnd(City), Used | Bit));
			}
		}
		return Best;
	};

	int32 Longest = 0;
	for (const FRouteDef* Route : Owned)
	{
		Longest = FMath::Max(Longest, FMath::Max(Walk(Route->CityA, 0), Walk(Route->CityB, 0)));
	}
	return Longest;
}

int32 URulesLibrary::GetPropertyValue(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	int32 Value = 0;
	for (const TPair<FName, FSlotState>& Entry : State.Slots)
	{
		if (Entry.Value.Owner == Player && Context.Tuning)
		{
			Value += Context.Tuning->GetTierValue(Entry.Value.Tier);
		}
	}
	return Value;
}

bool URulesLibrary::IsCatchUpActive(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	if (!Context.Tuning || !State.Players.IsValidIndex(Player) || !State.Players.IsValidIndex(GridlockRulesPrivate::Opponent(Player)))
	{
		return false;
	}
	const int32 Behind = State.Players[GridlockRulesPrivate::Opponent(Player)].Completed.Num() - State.Players[Player].Completed.Num();
	return Behind >= Context.Tuning->CatchUpGap;
}

#undef LOCTEXT_NAMESPACE

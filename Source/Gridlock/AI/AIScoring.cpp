#include "AI/AIScoring.h"
#include "Algo/Reverse.h"
#include "Data/AIPersonality.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "Model/Pathfinding.h"
#include "Model/RulesLibrary.h"

namespace GridlockAIPrivate
{
	/** An upgrade in progress counts as done: the AI plans a turn ahead. */
	EPropertyTier EffectiveTier(const FSlotState& Slot)
	{
		if (Slot.Owner == INDEX_NONE)
		{
			return EPropertyTier::None;
		}
		return Slot.bUpgrading ? static_cast<EPropertyTier>(static_cast<uint8>(Slot.Tier) + 1) : Slot.Tier;
	}

	/** Tickets the player lacks for this route alone (its color, or its best color if gray, plus Wilds). */
	int32 TicketDeficit(const FPlayerData& Player, const FRouteDef& Route)
	{
		int32 Matching = Player.TicketsOf(Route.Color);
		if (Route.Color == EGridlockColor::Gray)
		{
			for (uint8 Color = static_cast<uint8>(EGridlockColor::Red); Color <= static_cast<uint8>(EGridlockColor::White); ++Color)
			{
				Matching = FMath::Max(Matching, Player.TicketsOf(static_cast<EGridlockColor>(Color)));
			}
		}
		return FMath::Max(0, Route.Length - Matching - Player.TicketsOf(EGridlockColor::Wild));
	}

	/**
	 * What a route costs Player on the way to a card, in cash-like units. INDEX_NONE = can't use it.
	 * bKnowsTickets: the player's own view, where a missing ticket costs extra (it has to be bought blind).
	 */
	int32 RouteCost(const FBoardState& State, const FRulesContext& Context, int32 Player, const FRouteDef& Route, bool bKnowsTickets)
	{
		const int32* Owner = State.RouteOwner.Find(Route.Id);
		const FSlotState* Slot = State.Slots.Find(Route.Id);
		const bool bOpponentProperty = Slot && Slot->Owner != INDEX_NONE && Slot->Owner != Player;
		const EPropertyTier Tier = bOpponentProperty ? EffectiveTier(*Slot) : EPropertyTier::None;

		if (Owner && *Owner == Player)
		{
			return Tier >= EPropertyTier::Complex ? 5 : 0; // severed: the property has to go first
		}
		if (Owner && *Owner != INDEX_NONE)
		{
			return INDEX_NONE; // the opponent's route
		}
		if (URulesLibrary::GetRouteBlocker(State, Context, Route.Id) == 1 - Player)
		{
			return INDEX_NONE; // every lot on it is the opponent's
		}

		int32 Cost = Route.Length;
		for (const FName End : { Route.CityA, Route.CityB })
		{
			const int32* CityOwner = State.CityOwner.Find(End);
			Cost += CityOwner && *CityOwner == 1 - Player ? 1 : 0; // v0.8: a toll to pay
		}
		if (Tier >= EPropertyTier::Building)
		{
			Cost += 5; // blocked until removed
		}
		else if (Tier == EPropertyTier::Lot)
		{
			Cost += 1; // a Lot means a toll
		}
		if (bKnowsTickets && Context.Tuning->bCardEconomy && State.Players.IsValidIndex(Player))
		{
			Cost += 2 * TicketDeficit(State.Players[Player], Route);
		}
		return Cost;
	}

	TMap<FName, int32> CostsFromNetwork(const FBoardState& State, const FRulesContext& Context, int32 Player, bool bKnowsTickets,
		TMap<FName, FName>* OutArrivedBy = nullptr)
	{
		const TArray<FName> Sources = URulesLibrary::GetNetworkCities(State, Context, Player).Array();
		return FGridlockPathfinding::CostsFrom(*Context.Map, Sources,
			[&State, &Context, Player, bKnowsTickets](const FRouteDef& Route) { return RouteCost(State, Context, Player, Route, bKnowsTickets); },
			OutArrivedBy);
	}

	int32 RemainingCost(const FBoardState& State, const FRulesContext& Context, int32 Player,
		const FRouteCardDef& Card, const TMap<FName, int32>& FromNetwork, bool bKnowsTickets)
	{
		const TMap<FName, int32> FromA = FGridlockPathfinding::CostsFrom(*Context.Map, { Card.CityA },
			[&State, &Context, Player, bKnowsTickets](const FRouteDef& Route) { return RouteCost(State, Context, Player, Route, bKnowsTickets); });
		const int32* Between = FromA.Find(Card.CityB);
		if (Between && !Context.Tuning->bRoutesMustConnect)
		{
			return *Between; // routes can be built anywhere: only the path between the two cities counts
		}
		const int32* ReachA = FromNetwork.Find(Card.CityA);
		const int32* ReachB = FromNetwork.Find(Card.CityB);
		if (!Between || (!ReachA && !ReachB))
		{
			return INDEX_NONE;
		}
		// Joining the card to the network is counted from its nearer end (an overestimate when
		// the network already sits in the middle of the path, which is fine for ranking moves).
		return *Between + FMath::Min(ReachA ? *ReachA : MAX_int32, ReachB ? *ReachB : MAX_int32);
	}

	float ValueFromCost(int32 Points, int32 Cost)
	{
		return Cost == INDEX_NONE ? 0.0f : Points / (1.0f + Cost);
	}

	/** A held card's worth; when unfinished cards cost points, far-off cards are a liability. */
	float HeldValue(const FRulesContext& Context, int32 Points, int32 Cost)
	{
		if (!Context.Tuning->bUnfinishedCardsCountAgainst || Cost == 0)
		{
			return ValueFromCost(Points, Cost);
		}
		const float Risk = Cost == INDEX_NONE ? 1.0f : FMath::Min(1.0f, Cost / 6.0f);
		return ValueFromCost(Points, Cost) - 0.25f * Points * Risk;
	}

	/** Routes from a Dijkstra source to City, by walking back through ArrivedBy. */
	void CollectPath(const FRulesContext& Context, const TMap<FName, FName>& ArrivedBy, FName City, TArray<FName>& OutRoutes)
	{
		for (int32 Guard = 0; Guard < 64; ++Guard)
		{
			const FName* RouteId = ArrivedBy.Find(City);
			const FRouteDef* Route = RouteId ? Context.Map->FindRoute(*RouteId) : nullptr;
			if (!Route)
			{
				return;
			}
			OutRoutes.Add(Route->Id);
			City = Route->OtherEnd(City);
		}
	}
}

FAIScoreTerms FAIScoring::ComputeTerms(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	using namespace GridlockAIPrivate;

	FAIScoreTerms Terms;
	const int32 Other = 1 - Player;
	if (!Context.IsValid() || !State.Players.IsValidIndex(Player) || !State.Players.IsValidIndex(Other))
	{
		return Terms;
	}
	const FPlayerData& Me = State.Players[Player];
	const FPlayerData& Them = State.Players[Other];
	const UTuningData& Tuning = *Context.Tuning;

	// Progress: my unfinished cards, each worth its points discounted by what's left to build.
	const TMap<FName, int32> MyReach = CostsFromNetwork(State, Context, Player, true);
	for (const FName CardId : Me.Hand)
	{
		if (const FRouteCardDef* Card = Context.Deck->FindCard(CardId))
		{
			Terms.Progress += HeldValue(Context, Card->Points, RemainingCost(State, Context, Player, *Card, MyReach, true));
		}
	}

	// Threat: the opponent's progress on the cards I haven't seen, scaled to their hand size.
	// Their tickets are hidden, so their routes are costed without them.
	const TMap<FName, int32> TheirReach = CostsFromNetwork(State, Context, Other, false);
	float UnseenProgress = 0.0f;
	int32 Unseen = 0;
	for (const FRouteCardDef& Card : Context.Deck->Cards)
	{
		if (Me.Hand.Contains(Card.Id) || Me.PendingDraw.Contains(Card.Id) || Me.Completed.Contains(Card.Id) || Them.Completed.Contains(Card.Id))
		{
			continue;
		}
		UnseenProgress += ValueFromCost(Card.Points, RemainingCost(State, Context, Other, Card, TheirReach, false));
		++Unseen;
	}
	Terms.Threat = Unseen > 0 ? UnseenProgress * Them.Hand.Num() / Unseen : 0.0f;

	// Economy: cash (theirs counts against me), property value, cards in hand, and tolls per turn.
	int32 TollsIn = 0;
	int32 TollsOut = 0;
	int32 MyPropertyValue = 0;
	int32 MyLandmarks = 0;
	int32 TheirLandmarks = 0;
	for (const TPair<FName, FSlotState>& Entry : State.Slots)
	{
		const FSlotState& Slot = Entry.Value;
		const EPropertyTier Tier = EffectiveTier(Slot);
		MyPropertyValue += Slot.Owner == Player ? Slot.Invested : 0;
		MyLandmarks += Slot.Owner == Player && Tier == EPropertyTier::Landmark ? 1 : 0;
		TheirLandmarks += Slot.Owner == Other && Tier == EPropertyTier::Landmark ? 1 : 0;

		const int32* RouteOwner = State.RouteOwner.Find(Entry.Key);
		if (!RouteOwner || *RouteOwner == INDEX_NONE || Slot.Owner == INDEX_NONE || Slot.Owner == *RouteOwner)
		{
			continue;
		}
		const int32 Toll = Tuning.GetToll(Tier);
		TollsIn += Slot.Owner == Player ? Toll : 0;
		TollsOut += *RouteOwner == Player ? Toll : 0;
	}
	const float CardsHeld = Tuning.bCardEconomy
		? 0.8f * (Tuning.TicketCost * Me.TicketCount() + Tuning.PropertyCardCost * Me.PropertyCardCount())
		: 0.0f;
	Terms.Economy = 0.1f * (Me.Cash - 0.5f * Them.Cash + 0.6f * MyPropertyValue + CardsHeld + 3.0f * (TollsIn - TollsOut));

	// Exposure: my routes the opponent can reach with properties.
	const TSet<FName> TheirNetwork = URulesLibrary::GetNetworkCities(State, Context, Other);
	int32 Exposed = 0;
	for (const TPair<FName, int32>& Entry : State.RouteOwner)
	{
		const FRouteDef* Route = Entry.Value == Player ? Context.Map->FindRoute(Entry.Key) : nullptr;
		Exposed += Route && (TheirNetwork.Contains(Route->CityA) || TheirNetwork.Contains(Route->CityB)) ? 1 : 0;
	}
	Terms.Exposure = 0.1f * Exposed;

	// Points: score and completed cards against theirs, plus what Landmarks will still earn.
	const int32 RoundsLeft = FMath::Max(0, Tuning.TurnLimit - State.Round + 1);
	Terms.Points = (Me.Points - Them.Points)
		+ 3.0f * (Me.Completed.Num() - Them.Completed.Num())
		+ 0.5f * RoundsLeft * Tuning.LandmarkPointsPerTurn * (MyLandmarks - TheirLandmarks);

	return Terms;
}

float FAIScoring::Evaluate(const FBoardState& State, const FRulesContext& Context, const UAIPersonality& Personality, int32 Player)
{
	if (State.Phase == EGridlockPhase::GameOver)
	{
		return State.Winner == Player ? 10000.0f : State.Winner == INDEX_NONE ? 0.0f : -10000.0f;
	}
	const FAIScoreTerms Terms = ComputeTerms(State, Context, Player);
	return Personality.ProgressWeight * Terms.Progress
		- Personality.ThreatWeight * Terms.Threat
		+ Personality.EconomyWeight * Terms.Economy
		- Personality.ExposureWeight * Terms.Exposure
		+ Personality.PointsWeight * Terms.Points;
}

float FAIScoring::ScoreAction(const FBoardState& State, const FGameAction& Action, const FRulesContext& Context,
	const UAIPersonality& Personality, int32 Player)
{
	TArray<FGameEvent> Events;
	return Evaluate(URulesLibrary::Apply(State, Action, Context, Events), Context, Personality, Player);
}

int32 FAIScoring::CardRemainingCost(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId, bool bKnowsTickets)
{
	const FRouteCardDef* Card = Context.IsValid() ? Context.Deck->FindCard(CardId) : nullptr;
	if (!Card)
	{
		return INDEX_NONE;
	}
	const TMap<FName, int32> Reach = GridlockAIPrivate::CostsFromNetwork(State, Context, Player, bKnowsTickets);
	return GridlockAIPrivate::RemainingCost(State, Context, Player, *Card, Reach, bKnowsTickets);
}

float FAIScoring::CardValue(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId, bool bKnowsTickets)
{
	const FRouteCardDef* Card = Context.IsValid() ? Context.Deck->FindCard(CardId) : nullptr;
	return Card ? GridlockAIPrivate::ValueFromCost(Card->Points, CardRemainingCost(State, Context, Player, CardId, bKnowsTickets)) : 0.0f;
}

TArray<FName> FAIScoring::NeededRoutes(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	using namespace GridlockAIPrivate;

	TArray<FName> Unclaimed;
	if (!Context.IsValid() || !State.Players.IsValidIndex(Player))
	{
		return Unclaimed;
	}

	// The most valuable unfinished card.
	FName Best = NAME_None;
	float BestValue = 0.0f;
	for (const FName CardId : State.Players[Player].Hand)
	{
		const float Value = CardValue(State, Context, Player, CardId);
		if (Value > BestValue)
		{
			BestValue = Value;
			Best = CardId;
		}
	}
	return Best.IsNone() ? Unclaimed : NeededRoutesForCard(State, Context, Player, Best);
}

TArray<FName> FAIScoring::NeededRoutesForCard(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId)
{
	using namespace GridlockAIPrivate;

	TArray<FName> Unclaimed;
	const FRouteCardDef* Target = Context.IsValid() && State.Players.IsValidIndex(Player) ? Context.Deck->FindCard(CardId) : nullptr;
	if (!Target)
	{
		return Unclaimed;
	}

	// The routes still needed: from the network to the card's nearer end, then across the card.
	TMap<FName, FName> FromNetworkArrivedBy;
	const TMap<FName, int32> Reach = CostsFromNetwork(State, Context, Player, true, &FromNetworkArrivedBy);
	const int32* ReachA = Reach.Find(Target->CityA);
	const int32* ReachB = Reach.Find(Target->CityB);
	const FName Near = (ReachA && (!ReachB || *ReachA <= *ReachB)) ? Target->CityA : Target->CityB;
	const FName Far = Near == Target->CityA ? Target->CityB : Target->CityA;

	// When routes must connect, first the way from the network to the nearer end; otherwise just the card's own path.
	TArray<FName> Path;
	if (Context.Tuning->bRoutesMustConnect)
	{
		CollectPath(Context, FromNetworkArrivedBy, Near, Path);
		Algo::Reverse(Path); // network end first
	}
	TArray<FName> Across;
	TMap<FName, FName> AcrossArrivedBy;
	FGridlockPathfinding::CostsFrom(*Context.Map, { Near },
		[&State, &Context, Player](const FRouteDef& Route) { return RouteCost(State, Context, Player, Route, true); }, &AcrossArrivedBy);
	CollectPath(Context, AcrossArrivedBy, Far, Across);
	Algo::Reverse(Across);
	Path.Append(Across);

	for (const FName RouteId : Path)
	{
		const int32* Owner = State.RouteOwner.Find(RouteId);
		if (Owner && *Owner == INDEX_NONE)
		{
			Unclaimed.AddUnique(RouteId);
		}
	}
	return Unclaimed;
}

FName FAIScoring::FindIntent(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	const TArray<FName> Needed = NeededRoutes(State, Context, Player);

	// Prefer the needed route the AI can claim right now (it touches the network).
	const TSet<FName> Network = URulesLibrary::GetNetworkCities(State, Context, Player);
	for (const FName RouteId : Needed)
	{
		const FRouteDef* Route = Context.Map->FindRoute(RouteId);
		if (Route && (Network.Contains(Route->CityA) || Network.Contains(Route->CityB)))
		{
			return RouteId;
		}
	}
	return Needed.IsEmpty() ? NAME_None : Needed[0];
}

int32 FAIScoring::TicketShortfall(const FBoardState& State, const FRulesContext& Context, int32 Player)
{
	if (!Context.IsValid() || !Context.Tuning->bCardEconomy || !State.Players.IsValidIndex(Player))
	{
		return 0;
	}
	TArray<int32> Left = State.Players[Player].Tickets;
	Left.SetNumZeroed(GridlockColorCount);
	const int32 WildIndex = static_cast<int32>(EGridlockColor::Wild);

	int32 Shortfall = 0;
	for (const FName RouteId : NeededRoutes(State, Context, Player))
	{
		const FRouteDef* Route = Context.Map->FindRoute(RouteId);
		if (!Route)
		{
			continue;
		}
		int32 ColorIndex = static_cast<int32>(Route->Color);
		if (Route->Color == EGridlockColor::Gray)
		{
			ColorIndex = static_cast<int32>(EGridlockColor::Red);
			for (int32 Color = static_cast<int32>(EGridlockColor::Red); Color <= static_cast<int32>(EGridlockColor::White); ++Color)
			{
				ColorIndex = Left[Color] > Left[ColorIndex] ? Color : ColorIndex;
			}
		}
		const int32 Colored = FMath::Min(Left[ColorIndex], Route->Length);
		const int32 Wild = FMath::Min(Left[WildIndex], Route->Length - Colored);
		Left[ColorIndex] -= Colored;
		Left[WildIndex] -= Wild;
		Shortfall += Route->Length - Colored - Wild;
	}
	return Shortfall;
}

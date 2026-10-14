#include "Game/MatchSubsystem.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/ScenarioDefinition.h"
#include "Data/TuningData.h"
#include "Gridlock.h"

void UMatchSubsystem::StartMatch(UScenarioDefinition* InScenario, int32 Seed)
{
	Scenario = InScenario;
	History.Reset();

	const FRulesContext Context = GetContext();
	if (!Context.IsValid())
	{
		UE_LOG(LogGridlock, Error, TEXT("StartMatch: scenario %s is missing its map, deck or tuning asset"), *GetNameSafe(Scenario));
		return;
	}

	State = URulesLibrary::MakeInitialState(Context, Seed);
	ApplyScenarioPresets();

	// A preset hand means that player has nothing to choose in Setup.
	TArray<FGameEvent> Events;
	URulesLibrary::AdvanceSetup(State, Context, Events);
	for (const FGameEvent& Event : Events)
	{
		OnGameEvent.Broadcast(Event);
	}
	OnStateChanged.Broadcast();
}

bool UMatchSubsystem::Submit(const FGameAction& Action, FText& OutReason)
{
	const FRulesContext Context = GetContext();
	if (!URulesLibrary::IsLegal(State, Action, Context, OutReason))
	{
		return false;
	}

	History.Push(State);
	TArray<FGameEvent> Events;
	State = URulesLibrary::Apply(State, Action, Context, Events);

	for (const FGameEvent& Event : Events)
	{
		OnGameEvent.Broadcast(Event);
	}
	OnStateChanged.Broadcast();
	return true;
}

bool UMatchSubsystem::Undo()
{
	if (History.IsEmpty())
	{
		return false;
	}
	State = History.Pop();
	OnStateChanged.Broadcast();
	return true;
}

FRulesContext UMatchSubsystem::GetContext() const
{
	FRulesContext Context;
	if (Scenario)
	{
		Context.Map = Scenario->Map;
		Context.Deck = Scenario->Deck;
		Context.Tuning = Scenario->Tuning;
		Context.CardsToWinOverride = Scenario->OverrideCardsToWin;
	}
	return Context;
}

bool UMatchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMatchSubsystem::ApplyScenarioPresets()
{
	const FRulesContext Context = GetContext();

	for (const FPresetRoute& Preset : Scenario->PresetRoutes)
	{
		if (int32* Owner = State.RouteOwner.Find(Preset.RouteId))
		{
			*Owner = Preset.Owner;
		}
	}

	for (const FPresetProperty& Preset : Scenario->PresetProperties)
	{
		if (FSlotState* Slot = State.Slots.Find(Preset.RouteId))
		{
			Slot->Owner = Preset.Owner;
			Slot->Tier = Preset.Tier;
			Slot->Invested = Context.Tuning->GetTierValue(Preset.Tier);
		}
	}

	// A preset hand replaces the dealt cards; they go back on the bottom of the deck.
	auto ReplaceHand = [this](int32 PlayerIndex, const TArray<FName>& Hand)
	{
		if (Hand.IsEmpty() || !State.Players.IsValidIndex(PlayerIndex))
		{
			return;
		}
		FPlayerData& Player = State.Players[PlayerIndex];
		State.Deck.Insert(Player.PendingDraw, 0);
		Player.PendingDraw.Reset();
		Player.Hand = Hand;
		State.Deck.RemoveAll([&Hand](FName Card) { return Hand.Contains(Card); });
	};
	ReplaceHand(0, Scenario->HumanStartingHand);
	ReplaceHand(1, Scenario->AIStartingHand);
}

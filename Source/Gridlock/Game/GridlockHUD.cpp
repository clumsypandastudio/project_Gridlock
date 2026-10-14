#include "Game/GridlockHUD.h"
#include "Algo/Count.h"
#include "AI/AIPlayer.h"
#include "AI/AIScoring.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/TuningData.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Misc/CommandLine.h"
#include "Game/GridlockGameMode.h"
#include "Game/GridlockPlayerController.h"
#include "Game/MatchSubsystem.h"
#include "Model/RulesLibrary.h"
#include "View/BoardLayout.h"

namespace GridlockHUDPrivate
{
	const FLinearColor PanelColor(0.02f, 0.02f, 0.03f, 0.94f);
	const FLinearColor Plain(0.93f, 0.93f, 0.93f);
	const FLinearColor Quiet(0.62f, 0.62f, 0.62f);
	const FLinearColor NoticeColor(1.0f, 0.85f, 0.3f);
	constexpr float BannerHeight = 50.0f;
	constexpr float PanelHeight = 176.0f;
	constexpr int32 LogLinesShown = 7;
	constexpr double MarkSeconds = 3.0;

	const TCHAR* StepName(EGridlockPhase Phase)
	{
		switch (Phase)
		{
		case EGridlockPhase::Setup:      return TEXT("Setup");
		case EGridlockPhase::Start:      return TEXT("Step 1 of 5: Start");
		case EGridlockPhase::Resolve:    return TEXT("Step 2 of 5: Resolve");
		case EGridlockPhase::Build:      return TEXT("Step 3 of 5: Build");
		case EGridlockPhase::BuyUpgrade: return TEXT("Step 4 of 5: Buy/Upgrade");
		case EGridlockPhase::Score:      return TEXT("Step 5 of 5: Score");
		default:                         return TEXT("Game over");
		}
	}

	const TCHAR* TierName(int32 Tier)
	{
		switch (static_cast<EPropertyTier>(Tier))
		{
		case EPropertyTier::Lot:      return TEXT("Lot");
		case EPropertyTier::Building: return TEXT("Building");
		case EPropertyTier::Complex:  return TEXT("Complex");
		case EPropertyTier::Landmark: return TEXT("Landmark");
		default:                      return TEXT("property");
		}
	}

	FString LabelFor(const UAIPlayer* AI, int32 Player)
	{
		const FString Name = GridlockBoard::PlayerName(Player);
		return AI ? Name + (Player == AI->PlayerIndex ? TEXT(" (AI)") : TEXT(" (you)")) : Name;
	}

	FString CardName(const FRulesContext& Context, FName CardId)
	{
		const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
		return Card ? FString::Printf(TEXT("%s - %s"), *Card->CityA.ToString(), *Card->CityB.ToString()) : CardId.ToString();
	}

	/** "10 pts + $10, about 3 pins to go" and friends: what a card is worth and how far it is from done. */
	FString CardProgress(const FBoardState& State, const FRulesContext& Context, int32 Player, FName CardId)
	{
		const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
		const int32 Points = Card ? Card->Points : 0;
		const int32 Payout = Points * Context.Tuning->CashPerCardPoint;
		const FString Worth = Payout > 0 ? FString::Printf(TEXT("%d pts + $%d"), Points, Payout) : FString::Printf(TEXT("%d pts"), Points);
		if (URulesLibrary::IsCardComplete(State, Context, Player, CardId))
		{
			return Context.Tuning->bScoreCardsAtEnd ? FString::Printf(TEXT("%s, done: scores at the end of the game"), *Worth)
				: FString::Printf(TEXT("%s, done: pays at the end of your turn"), *Worth);
		}
		// Pins still to claim along the cheapest path (not counting which tickets you hold).
		const int32 Remaining = FAIScoring::CardRemainingCost(State, Context, Player, CardId, false);
		return Remaining == INDEX_NONE
			? FString::Printf(TEXT("%s, blocked for now"), *Worth)
			: FString::Printf(TEXT("%s, about %d pins to go"), *Worth, Remaining);
	}
}

void AGridlockHUD::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Listen from spawn, before the game mode starts the match, so the log has the whole game.
	UMatchSubsystem* Match = GetWorld() ? GetWorld()->GetSubsystem<UMatchSubsystem>() : nullptr;
	if (Match && !Match->OnGameEvent.IsAlreadyBound(this, &AGridlockHUD::HandleGameEvent))
	{
		Match->OnGameEvent.AddDynamic(this, &AGridlockHUD::HandleGameEvent);
	}
}

void AGridlockHUD::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("GridlockNoHelp")))
	{
		bHelpOpen = false;
	}
#endif
}

void AGridlockHUD::HandleGameEvent(const FGameEvent& Event)
{
	const UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	const FRulesContext Context = Match ? Match->GetContext() : FRulesContext();
	if (!Context.IsValid())
	{
		return;
	}
	const AGridlockGameMode* GameMode = GetWorld()->GetAuthGameMode<AGridlockGameMode>();
	const UAIPlayer* AI = GameMode && !GameMode->bHotSeat ? GameMode->GetAIPlayer() : nullptr;

	const FString Text = FormatEvent(Event, Context, AI);
	if (!Text.IsEmpty())
	{
		const bool bQuiet = Event.Type == EGridlockEventType::TurnStarted;
		Log.Add({ Text, bQuiet ? GridlockHUDPrivate::Quiet : GridlockBoard::PlayerColor(Event.Player) });
		if (Log.Num() > 50)
		{
			Log.RemoveAt(0);
		}
	}

	// Cards you buy go to the "You drew" strip (only yours: the opponent's stay hidden).
	const int32 Human = GameMode ? GameMode->GetHumanPlayer() : 0;
	if (Event.Player == Human && (Event.Type == EGridlockEventType::TicketBought || Event.Type == EGridlockEventType::PropertyCardBought
		|| Event.Type == EGridlockEventType::FaceUpTaken))
	{
		const bool bTicket = Event.Type != EGridlockEventType::PropertyCardBought;
		RecentDraws.Add({ bTicket ? GridlockBoard::TicketColorName(static_cast<EGridlockColor>(Event.Amount)) : FString(GridlockHUDPrivate::TierName(Event.Amount)),
			bTicket ? GridlockBoard::TicketColor(static_cast<EGridlockColor>(Event.Amount)) : GridlockBoard::Clickable,
			GetWorld()->GetRealTimeSeconds() + 3.5 });
		if (RecentDraws.Num() > 6)
		{
			RecentDraws.RemoveAt(0);
		}
	}

	// A brief marker on the board where the change happened.
	const bool bBoardChange = Event.Type == EGridlockEventType::RouteClaimed || Event.Type == EGridlockEventType::PropertyPlaced
		|| Event.Type == EGridlockEventType::UpgradeStarted || Event.Type == EGridlockEventType::UpgradeFinished
		|| Event.Type == EGridlockEventType::PropertyRemoved || Event.Type == EGridlockEventType::PropertyDemolished
		|| Event.Type == EGridlockEventType::PropertySold || Event.Type == EGridlockEventType::RouteLotPlaced;
	const FRouteDef* Route = bBoardChange ? Context.Map->FindRoute(Event.RouteId) : nullptr;
	if (Route)
	{
		Marks.Add({ GridlockBoard::RouteMidpoint(*Route, *Context.Map, 10.0), GridlockBoard::PlayerColor(Event.Player),
			GetWorld()->GetRealTimeSeconds() + GridlockHUDPrivate::MarkSeconds });
	}
}

FName AGridlockHUD::GetButtonAt(const FVector2D& ScreenPosition) const
{
	for (const FButtonRect& Button : Buttons)
	{
		if (ScreenPosition.X >= Button.Min.X && ScreenPosition.X <= Button.Max.X && ScreenPosition.Y >= Button.Min.Y && ScreenPosition.Y <= Button.Max.Y)
		{
			return Button.Id;
		}
	}
	return NAME_None;
}

void AGridlockHUD::DrawHUD()
{
	Super::DrawHUD();
	Buttons.Reset();

	const UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	const AGridlockPlayerController* Controller = Cast<AGridlockPlayerController>(GetOwningPlayerController());
	if (!Match || !Controller || !Canvas)
	{
		return;
	}
	const FRulesContext Context = Match->GetContext();
	if (!Context.IsValid())
	{
		return;
	}
	const FBoardState State = Match->GetState();

	AGridlockGameMode* GameMode = GetWorld()->GetAuthGameMode<AGridlockGameMode>();
	if (GameMode)
	{
		for (const FString& Notice : GameMode->TakeNotices())
		{
			Log.Add({ Notice, GridlockHUDPrivate::NoticeColor });
		}
	}

	FDrawFrame Frame;
	Frame.State = &State;
	Frame.Context = &Context;
	Frame.Controller = Controller;
	Frame.AI = GameMode && !GameMode->bHotSeat ? GameMode->GetAIPlayer() : nullptr;
	Frame.Human = Controller->GetActingPlayer();
	Frame.bHumanTurn = State.Phase != EGridlockPhase::GameOver && State.ActivePlayer == Frame.Human;
	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (Controller->GetMousePosition(MouseX, MouseY))
	{
		Frame.Mouse = FVector2D(MouseX, MouseY);
	}

	DrawCityNames(Frame);
	DrawGoalRings(Frame);
	DrawMarks(Frame);
	DrawRouteLots(Frame);
	DrawBanner(Frame);
	DrawRouteTiles(Frame);
	DrawPanel(Frame);

	const bool bChoosingCards = Frame.bHumanTurn && State.Players.IsValidIndex(Frame.Human) && !State.Players[Frame.Human].PendingDraw.IsEmpty();
	const bool bLotChoice = Frame.bHumanTurn && State.Players.IsValidIndex(Frame.Human) && State.Players[Frame.Human].bLotPending;
	if (bChoosingCards)
	{
		DrawKeepPopup(Frame);
	}
	else if (bLotChoice)
	{
		DrawLotPopup(Frame);
	}
	else if (Frame.bHumanTurn && !Controller->GetCityChoice().IsNone())
	{
		DrawCityPopup(Frame);
	}
	else if (Frame.bHumanTurn && !Controller->GetBuildRoute().IsNone())
	{
		DrawBuildPopup(Frame);
	}
	else
	{
		DrawTooltip(Frame);
	}

	// The results window opens by itself once per game, when it ends.
	if (State.Phase == EGridlockPhase::GameOver && !bResultsShown)
	{
		bResultsShown = true;
		bResultsOpen = true;
	}
	else if (State.Phase != EGridlockPhase::GameOver)
	{
		bResultsShown = false;
		bResultsOpen = false;
	}
	if (bResultsOpen && State.Phase == EGridlockPhase::GameOver)
	{
		Buttons.Reset(); // the window's own buttons only
		DrawResults(Frame);
	}

	if (bHelpOpen)
	{
		Buttons.Reset(); // while help is up, a click only closes it
		DrawHelp(Frame);
	}
}

void AGridlockHUD::DrawCityNames(const FDrawFrame& Frame)
{
	UFont* Font = GEngine->GetSmallFont();
	const UTuningData& Tuning = *Frame.Context->Tuning;
	CityScreen.Reset();
	for (const FCityDef& City : Frame.Context->Map->Cities)
	{
		const FVector Screen = Project(GridlockBoard::ToWorld(City.Position, 20.0));
		CityScreen.Add(City.Id, FVector2D(Screen.X, Screen.Y));
		const int32* CityOwner = Frame.State->CityOwner.Find(City.Id);
		FString Name = City.Name.ToString();
		if (CityOwner)
		{
			// v0.8: an owned city: a thick ring in the owner's color, and its toll by the name.
			DrawRing(FVector2D(Screen.X, Screen.Y - 6.0f), 20.0f, GridlockBoard::PlayerColor(*CityOwner), 5.0f);
			Name += FString::Printf(TEXT("  $%d"), Tuning.CityToll);
		}
		float Width = 0.0f;
		float Height = 0.0f;
		GetTextSize(Name, Width, Height, Font);
		DrawTextShadowed(Name, CityOwner ? GridlockBoard::PlayerColor(*CityOwner) : FLinearColor::White, Screen.X - Width * 0.5f, Screen.Y + 14.0f, Font);
	}
}

FName AGridlockHUD::GetCityAt(const FVector2D& ScreenPosition) const
{
	FName Closest;
	double Best = 22.0 * 22.0; // within about a city's radius
	for (const TPair<FName, FVector2D>& Entry : CityScreen)
	{
		const double Distance = FVector2D::DistSquared(Entry.Value, ScreenPosition);
		if (Distance < Best)
		{
			Best = Distance;
			Closest = Entry.Key;
		}
	}
	return Closest;
}

TArray<FString> AGridlockHUD::DescribeCity(const FDrawFrame& Frame, FName CityId) const
{
	TArray<FString> Lines;
	const UTuningData& Tuning = *Frame.Context->Tuning;
	if (Tuning.CityPropertyPrice <= 0)
	{
		return Lines;
	}
	Lines.Add(CityId.ToString());
	if (const int32* CityOwner = Frame.State->CityOwner.Find(CityId))
	{
		Lines.Add(FString::Printf(TEXT("%s owns it. Building a route into it costs the other player $%d,"), *PlayerLabel(Frame, *CityOwner), Tuning.CityToll));
		Lines.Add(TEXT("and so does each of their finished Route cards whose path visits it (at the end)."));
		return Lines;
	}
	FGameAction Buy;
	Buy.Type = EGridlockActionType::BuyCityProperty;
	Buy.Player = Frame.Human;
	Buy.RouteId = CityId;
	FText Reason;
	Lines.Add(TEXT("Nobody owns it yet."));
	Lines.Add(URulesLibrary::IsLegal(*Frame.State, Buy, *Frame.Context, Reason)
		? FString::Printf(TEXT("Click: buy its property for $%d (ends your turn)"), Tuning.CityPropertyPrice)
		: FString::Printf(TEXT("Can't buy it: %s"), *Reason.ToString()));
	return Lines;
}

void AGridlockHUD::DrawResults(const FDrawFrame& Frame)
{
	const FBoardState& State = *Frame.State;
	const float BoxWidth = FMath::Min(1040.0f, Canvas->ClipX - 40.0f);
	const float BoxHeight = Canvas->ClipY - 80.0f;
	const float X = (Canvas->ClipX - BoxWidth) * 0.5f;
	const float Y = 40.0f;
	DrawRect(FLinearColor(0.01f, 0.01f, 0.03f, 0.97f), X, Y, BoxWidth, BoxHeight);
	DrawOutline(X, Y, BoxWidth, BoxHeight, GridlockBoard::Clickable, 2.0f);

	// Who won, by how much, and how long it took.
	const FString Winner = State.Winner == INDEX_NONE ? FString(TEXT("A draw"))
		: Frame.AI && State.Winner == Frame.Human ? FString(TEXT("You win!")) : FString::Printf(TEXT("%s wins"), *PlayerLabel(Frame, State.Winner));
	const FString Score = State.Players.Num() == 2 ? FString::Printf(TEXT("  %d to %d"),
		FMath::Max(State.Players[0].Points, State.Players[1].Points), FMath::Min(State.Players[0].Points, State.Players[1].Points)) : FString();
	DrawTextShadowed(Winner + Score, State.Winner == INDEX_NONE ? GridlockBoard::Clickable : GridlockBoard::PlayerColor(State.Winner), X + 24.0f, Y + 16.0f, GEngine->GetLargeFont(), 1.2f);
	FString Time;
	if (const AGridlockGameMode* Mode = GetWorld()->GetAuthGameMode<AGridlockGameMode>())
	{
		const int32 Seconds = FMath::RoundToInt(Mode->GetGameSeconds());
		Time = FString::Printf(TEXT("Game time %d:%02d   %d rounds"), Seconds / 60, Seconds % 60, State.Round);
	}
	DrawTextShadowed(Time, GridlockHUDPrivate::Plain, X + 24.0f, Y + 50.0f, GEngine->GetMediumFont());

	// You on the left, the opponent on the right (in hot-seat: player 1 left, player 2 right).
	const int32 Left = Frame.AI ? Frame.Human : 0;
	const float ColumnWidth = (BoxWidth - 72.0f) * 0.5f;
	DrawResultsColumn(Frame, Left, X + 24.0f, Y + 86.0f, ColumnWidth);
	DrawLine(X + BoxWidth * 0.5f, Y + 86.0f, X + BoxWidth * 0.5f, Y + BoxHeight - 70.0f, FLinearColor(1.0f, 1.0f, 1.0f, 0.2f), 1.0f);
	DrawResultsColumn(Frame, 1 - Left, X + BoxWidth * 0.5f + 24.0f, Y + 86.0f, ColumnWidth);

	const float ButtonWidth = 220.0f;
	DrawButton(Frame, TEXT("NewGame"), TEXT("New game"), X + BoxWidth * 0.5f - ButtonWidth - 8.0f, Y + BoxHeight - 56.0f, ButtonWidth, 40.0f, true, true);
	DrawButton(Frame, TEXT("ResultsClose"), TEXT("Look at the board  [Esc]"), X + BoxWidth * 0.5f + 8.0f, Y + BoxHeight - 56.0f, ButtonWidth, 40.0f, true, false);
	Buttons.Add({ TEXT("ResultsWindow"), FVector2D(X, Y), FVector2D(X + BoxWidth, Y + BoxHeight) }); // clicks elsewhere on it do nothing
}

float AGridlockHUD::DrawResultsColumn(const FDrawFrame& Frame, int32 Player, float X, float Y, float Width)
{
	const FBoardState& State = *Frame.State;
	const FRulesContext& Context = *Frame.Context;
	const UTuningData& Tuning = *Context.Tuning;
	const FPlayerData& Data = State.Players[Player];
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();
	const FLinearColor Color = GridlockBoard::PlayerColor(Player);
	const float Line = 18.0f;
	float Row = Y;
	auto Text = [&](const FString& Value, const FLinearColor& TextColor, float Indent = 0.0f)
	{
		DrawTextShadowed(Value, TextColor, X + Indent, Row, Small);
		Row += Line;
	};
	auto Heading = [&](const FString& Value)
	{
		Row += 6.0f;
		DrawTextShadowed(Value, GridlockBoard::Clickable, X, Row, Small);
		Row += Line;
	};
	// A label on the left and its number lined up on the right.
	auto Pair = [&](const FString& Label, const FString& Value, const FLinearColor& TextColor)
	{
		DrawTextShadowed(Label, TextColor, X + 10.0f, Row, Small);
		float ValueWidth = 0.0f;
		float ValueHeight = 0.0f;
		GetTextSize(Value, ValueWidth, ValueHeight, Small);
		DrawTextShadowed(Value, TextColor, X + Width - 10.0f - ValueWidth, Row, Small);
		Row += Line;
	};

	DrawTextShadowed(FString::Printf(TEXT("%s   %d points"), *PlayerLabel(Frame, Player), Data.Points), Color, X, Row, Medium);
	Row += 28.0f;

	// Points, piece by piece (they add up to the total).
	int32 RoutePoints = 0;
	int32 Routes = 0;
	int32 TrainsUsed = 0;
	for (const TPair<FName, int32>& Entry : State.RouteOwner)
	{
		const FRouteDef* Route = Entry.Value == Player ? Context.Map->FindRoute(Entry.Key) : nullptr;
		if (Route)
		{
			RoutePoints += Tuning.GetRoutePoints(Route->Length);
			TrainsUsed += Route->Length;
			++Routes;
		}
	}
	int32 Finished = 0;
	for (const FName CardId : Data.Completed)
	{
		const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
		Finished += Card ? Card->Points : 0;
	}
	int32 Missed = 0;
	for (const FName CardId : Data.Hand)
	{
		const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
		Missed += Card ? Card->Points : 0;
	}
	const int32 Longest = URulesLibrary::GetLongestRoute(State, Context, Player);
	const int32 OtherLongest = URulesLibrary::GetLongestRoute(State, Context, 1 - Player);
	const bool bBonus = Tuning.LongestRouteBonus > 0 && Longest > 0 && Longest >= OtherLongest;
	Heading(TEXT("POINTS"));
	Pair(FString::Printf(TEXT("Routes: %d claimed, %d trains used"), Routes, TrainsUsed), FString::Printf(TEXT("+%d"), RoutePoints), GridlockHUDPrivate::Plain);
	Pair(FString::Printf(TEXT("Route cards finished: %d"), Data.Completed.Num()), FString::Printf(TEXT("+%d"), Finished), GridlockHUDPrivate::Plain);
	Pair(FString::Printf(TEXT("Route cards not finished: %d"), Data.Hand.Num()), FString::Printf(TEXT("-%d"), Missed),
		Data.Hand.IsEmpty() ? GridlockHUDPrivate::Plain : GridlockHUDPrivate::NoticeColor);
	Pair(FString::Printf(TEXT("Longest route: %d trains%s"), Longest, bBonus ? TEXT(" (the longest)") : TEXT("")),
		FString::Printf(TEXT("+%d"), bBonus ? Tuning.LongestRouteBonus : 0), GridlockHUDPrivate::Plain);
	Pair(TEXT("Total"), FString::Printf(TEXT("%d"), Data.Points), Color);

	// Every Route card, finished or not, and any toll its path paid at the end.
	Heading(FString::Printf(TEXT("ROUTE CARDS (%d)"), Data.Completed.Num() + Data.Hand.Num()));
	for (const FName CardId : Data.Completed)
	{
		const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
		const int32 Toll = Data.TollsByCard.FindRef(CardId);
		Pair(FString::Printf(TEXT("done      %s%s"), *GridlockHUDPrivate::CardName(Context, CardId),
			Toll > 0 ? *FString::Printf(TEXT("   (paid $%d in tolls on the way)"), Toll) : TEXT("")), FString::Printf(TEXT("+%d"), Card ? Card->Points : 0), GridlockHUDPrivate::Plain);
	}
	for (const FName CardId : Data.Hand)
	{
		const FRouteCardDef* Card = Context.Deck->FindCard(CardId);
		Pair(FString::Printf(TEXT("missed   %s"), *GridlockHUDPrivate::CardName(Context, CardId)), FString::Printf(TEXT("-%d"), Card ? Card->Points : 0), GridlockHUDPrivate::NoticeColor);
	}

	// Cities owned and what each earned.
	if (Tuning.CityPropertyPrice > 0)
	{
		TArray<FName> Owned;
		for (const TPair<FName, int32>& Entry : State.CityOwner)
		{
			if (Entry.Value == Player)
			{
				Owned.Add(Entry.Key);
			}
		}
		Heading(FString::Printf(TEXT("CITY PROPERTIES (%d)"), Owned.Num()));
		if (Owned.IsEmpty())
		{
			Text(TEXT("none"), GridlockHUDPrivate::Quiet, 10.0f);
		}
		for (const FName City : Owned)
		{
			Pair(FString::Printf(TEXT("%s (cost $%d)"), *City.ToString(), Tuning.CityPropertyPrice), FString::Printf(TEXT("earned $%d"), Data.TollsByCity.FindRef(City)), GridlockHUDPrivate::Plain);
		}
	}

	// Money.
	if (Tuning.TicketCost > 0 || Tuning.CityPropertyPrice > 0)
	{
		Heading(TEXT("MONEY"));
		const int32 Profit = Data.Earned - Data.Spent;
		Text(FString::Printf(TEXT("Spent $%d   Earned $%d   Profit %s$%d"), Data.Spent, Data.Earned, Profit < 0 ? TEXT("-") : TEXT(""), FMath::Abs(Profit)),
			GridlockHUDPrivate::Plain, 10.0f);
	}
	return Row - Y;
}

void AGridlockHUD::DrawCityPopup(const FDrawFrame& Frame)
{
	const UTuningData& Tuning = *Frame.Context->Tuning;
	const FName CityId = Frame.Controller->GetCityChoice();
	const float BoxWidth = 600.0f;
	const float BoxHeight = 190.0f;
	const float X = (Canvas->ClipX - BoxWidth) * 0.5f;
	const float Y = GridlockHUDPrivate::BannerHeight + 70.0f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.95f), X, Y, BoxWidth, BoxHeight);
	DrawOutline(X, Y, BoxWidth, BoxHeight, GridlockBoard::Clickable, 2.0f);
	DrawTextShadowed(FString::Printf(TEXT("Property in %s"), *CityId.ToString()), GridlockBoard::Clickable, X + 20.0f, Y + 14.0f, GEngine->GetLargeFont());
	DrawTextShadowed(FString::Printf(TEXT("$%d to buy. It's your turn's action. Your opponent then pays you $%d each time they build a route into %s,"),
		Tuning.CityPropertyPrice, Tuning.CityToll, *CityId.ToString()), GridlockHUDPrivate::Plain, X + 20.0f, Y + 48.0f, GEngine->GetSmallFont());
	DrawTextShadowed(TEXT("and again at the end for each of their finished Route cards whose path visits it."), GridlockHUDPrivate::Plain, X + 20.0f, Y + 66.0f, GEngine->GetSmallFont());

	FGameAction Buy;
	Buy.Type = EGridlockActionType::BuyCityProperty;
	Buy.Player = Frame.Human;
	Buy.RouteId = CityId;
	FText Reason;
	const bool bCanBuy = URulesLibrary::IsLegal(*Frame.State, Buy, *Frame.Context, Reason);
	if (!bCanBuy)
	{
		DrawTextShadowed(Reason.ToString(), GridlockHUDPrivate::NoticeColor, X + 20.0f, Y + 94.0f, GEngine->GetSmallFont());
	}
	const float ButtonWidth = 220.0f;
	DrawButton(Frame, TEXT("BuyCity"), FString::Printf(TEXT("Buy it, $%d  [Enter]"), Tuning.CityPropertyPrice), X + BoxWidth * 0.5f - ButtonWidth - 8.0f,
		Y + BoxHeight - 52.0f, ButtonWidth, 38.0f, bCanBuy, true);
	DrawButton(Frame, TEXT("CityCancel"), bCanBuy ? TEXT("Cancel  [Esc]") : TEXT("Close  [Esc]"), X + BoxWidth * 0.5f + 8.0f, Y + BoxHeight - 52.0f, ButtonWidth, 38.0f, true, false);
	Buttons.Add({ TEXT("CityWindow"), FVector2D(X, Y), FVector2D(X + BoxWidth, Y + BoxHeight) });
}

void AGridlockHUD::DrawGoalRings(const FDrawFrame& Frame)
{
	const FBoardState& State = *Frame.State;
	const FRulesContext& Context = *Frame.Context;

	// Gold rings around the cities on your unfinished Route cards.
	if (State.Players.IsValidIndex(Frame.Human))
	{
		TSet<FName> Goals;
		for (const FName CardId : State.Players[Frame.Human].Hand)
		{
			if (const FRouteCardDef* Card = Context.Deck->FindCard(CardId))
			{
				Goals.Add(Card->CityA);
				Goals.Add(Card->CityB);
			}
		}
		for (const FName CityId : Goals)
		{
			if (const FCityDef* City = Context.Map->FindCity(CityId))
			{
				const FVector Screen = Project(GridlockBoard::ToWorld(City->Position, 10.0));
				DrawRing(FVector2D(Screen.X, Screen.Y), 24.0f, GridlockBoard::Clickable, 2.5f);
			}
		}
	}

	// A small ring on the route the AI is heading for (not under Ticket to Ride rules, where its Route cards are secret).
	if (Frame.AI && State.Phase != EGridlockPhase::GameOver && !Context.Tuning->bScoreCardsAtEnd)
	{
		if (const FRouteDef* Route = Context.Map->FindRoute(Frame.AI->GetIntent()))
		{
			const FVector Screen = Project(GridlockBoard::RouteMidpoint(*Route, *Context.Map, 10.0));
			const FLinearColor Color = GridlockBoard::PlayerColor(Frame.AI->PlayerIndex);
			DrawRing(FVector2D(Screen.X, Screen.Y), 16.0f, Color, 2.0f);
			DrawTextShadowed(TEXT("AI target"), Color, Screen.X + 18.0f, Screen.Y - 8.0f, GEngine->GetSmallFont());
		}
	}
}

void AGridlockHUD::DrawMarks(const FDrawFrame& Frame)
{
	const double Now = GetWorld()->GetRealTimeSeconds();
	Marks.RemoveAll([Now](const FMark& Mark) { return Mark.Until < Now; });
	for (const FMark& Mark : Marks)
	{
		const FVector Screen = Project(Mark.World);
		const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(Now) * 8.0f);
		const float Size = 30.0f + 8.0f * Pulse;
		FLinearColor Color = Mark.Color;
		Color.A = FMath::Clamp(static_cast<float>((Mark.Until - Now) / GridlockHUDPrivate::MarkSeconds) + 0.2f, 0.0f, 1.0f);
		DrawOutline(Screen.X - Size * 0.5f, Screen.Y - Size * 0.5f, Size, Size, Color, 3.0f);
	}
}

void AGridlockHUD::DrawRouteLots(const FDrawFrame& Frame)
{
	const FRulesContext& Context = *Frame.Context;
	if (Context.Tuning->RouteLotCards <= 0)
	{
		return;
	}
	for (const FRouteDef& Route : Context.Map->Routes)
	{
		const int32 Slots = Context.Tuning->GetLotSlots(Route.Length);
		const FCityDef* A = Context.Map->FindCity(Route.CityA);
		const FCityDef* B = Context.Map->FindCity(Route.CityB);
		if (Slots <= 0 || !A || !B || Frame.State->RouteOwner.FindRef(Route.Id) != INDEX_NONE)
		{
			continue; // claimed routes keep their lots, but they no longer matter
		}
		const FRouteLots* Lots = Frame.State->RouteLots.Find(Route.Id);
		const int32 Blocker = URulesLibrary::GetRouteBlocker(*Frame.State, Context, Route.Id);
		for (int32 Index = 0; Index < Slots; ++Index)
		{
			// 1 slot in the middle; 2 or 3 spread around it.
			const double T = Slots == 1 ? 0.5 : 0.5 + (Index - (Slots - 1) * 0.5) * (Slots == 2 ? 0.3 : 0.2);
			const FVector2D At = Route.bCurved ? GridlockBoard::QuadraticPoint(A->Position, Route.CurveControl, B->Position, T)
				: FMath::Lerp(A->Position, B->Position, T);
			const FVector Screen = Project(GridlockBoard::ToWorld(At, 12.0));
			const float Size = 13.0f;
			const bool bOwned = Lots && Lots->Owners.IsValidIndex(Index);
			const FLinearColor Fill = bOwned ? GridlockBoard::PlayerColor(Lots->Owners[Index]) : FLinearColor(0.05f, 0.05f, 0.05f, 0.75f);
			DrawRect(Fill, Screen.X - Size * 0.5f, Screen.Y - Size * 0.5f, Size, Size);
			DrawOutline(Screen.X - Size * 0.5f, Screen.Y - Size * 0.5f, Size, Size, Blocker != INDEX_NONE ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.6f),
				Blocker != INDEX_NONE ? 2.5f : 1.0f);
		}
	}
}

void AGridlockHUD::DrawLotPopup(const FDrawFrame& Frame)
{
	const UTuningData& Tuning = *Frame.Context->Tuning;
	const int32 Rest = FMath::Max(0, Tuning.RouteLotPrice - Tuning.TicketCost);
	const float BoxWidth = 560.0f;
	const float BoxHeight = 170.0f;
	const float X = (Canvas->ClipX - BoxWidth) * 0.5f;
	const float Y = GridlockHUDPrivate::BannerHeight + 70.0f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.95f), X, Y, BoxWidth, BoxHeight);
	DrawOutline(X, Y, BoxWidth, BoxHeight, GridlockBoard::TicketColor(EGridlockColor::Lot), 2.0f);
	DrawTextShadowed(TEXT("You drew a Lot from the deck"), GridlockBoard::Clickable, X + 20.0f, Y + 14.0f, GEngine->GetLargeFont());
	DrawTextShadowed(FString::Printf(TEXT("Keep it for $%d more ($%d in all) to place on a route later (one a turn), or discard it."), Rest, Tuning.RouteLotPrice),
		GridlockHUDPrivate::Plain, X + 20.0f, Y + 48.0f, GEngine->GetSmallFont());
	DrawTextShadowed(Frame.State->TicketBuysThisTurn < Tuning.MaxTicketsPerTurn ? TEXT("Either way it counts as one of your 2 tickets; you still have the other.")
		: TEXT("Either way it was your second ticket, so your turn ends."), GridlockHUDPrivate::Quiet, X + 20.0f, Y + 66.0f, GEngine->GetSmallFont());
	FGameAction Keep;
	Keep.Type = EGridlockActionType::KeepLot;
	Keep.Player = Frame.Human;
	FText Reason;
	const bool bCanKeep = URulesLibrary::IsLegal(*Frame.State, Keep, *Frame.Context, Reason);
	if (!bCanKeep)
	{
		DrawTextShadowed(Reason.ToString(), GridlockHUDPrivate::NoticeColor, X + 20.0f, Y + 86.0f, GEngine->GetSmallFont());
	}
	const float ButtonWidth = 220.0f;
	DrawButton(Frame, TEXT("KeepLot"), FString::Printf(TEXT("Keep it, $%d  [Enter]"), Rest), X + BoxWidth * 0.5f - ButtonWidth - 8.0f, Y + BoxHeight - 52.0f, ButtonWidth, 38.0f, bCanKeep, true);
	DrawButton(Frame, TEXT("DiscardLot"), TEXT("Discard it  [Esc]"), X + BoxWidth * 0.5f + 8.0f, Y + BoxHeight - 52.0f, ButtonWidth, 38.0f, true, false);
	Buttons.Add({ TEXT("LotWindow"), FVector2D(X, Y), FVector2D(X + BoxWidth, Y + BoxHeight) });
}

void AGridlockHUD::DrawBanner(const FDrawFrame& Frame)
{
	const FBoardState& State = *Frame.State;
	const UTuningData& Tuning = *Frame.Context->Tuning;
	const float Width = Canvas->ClipX;
	DrawRect(GridlockHUDPrivate::PanelColor, 0.0f, 0.0f, Width, GridlockHUDPrivate::BannerHeight);

	FString Headline;
	FLinearColor Color = GridlockBoard::Clickable;
	if (State.Phase == EGridlockPhase::GameOver)
	{
		Headline = State.Winner == INDEX_NONE ? FString(TEXT("Game over: a draw."))
			: Frame.AI && State.Winner == Frame.Human ? FString(TEXT("You win!"))
			: FString::Printf(TEXT("%s wins."), *PlayerLabel(Frame, State.Winner));
		if (const AGridlockGameMode* Mode = GetWorld()->GetAuthGameMode<AGridlockGameMode>())
		{
			const int32 Seconds = FMath::RoundToInt(Mode->GetGameSeconds());
			Headline += FString::Printf(TEXT("  Game time %d:%02d."), Seconds / 60, Seconds % 60);
		}
		Headline += TEXT("  Click New game to play again.");
		Color = GridlockBoard::PlayerColor(State.Winner);
	}
	else if (!Frame.bHumanTurn)
	{
		Headline = FString::Printf(TEXT("%s is playing. Each move shows in the log below."), *PlayerLabel(Frame, State.ActivePlayer));
		Color = GridlockBoard::PlayerColor(State.ActivePlayer);
	}
	else
	{
		const bool bChoosing = State.Players.IsValidIndex(Frame.Human) && !State.Players[Frame.Human].PendingDraw.IsEmpty();
		const FString Who = Frame.AI ? FString() : PlayerLabel(Frame, Frame.Human) + TEXT(": ");
		switch (State.Phase)
		{
		case EGridlockPhase::Setup:
			Headline = TEXT("Choose your starting Route cards.");
			break;
		case EGridlockPhase::Start:
			Headline = bChoosing ? FString(TEXT("Choose which drawn cards to keep."))
				: Frame.Controller->IsSellMode() ? FString(TEXT("Sell: click one of your properties, or click Sell again to stop."))
				: State.Players[Frame.Human].bLotPending ? FString(TEXT("You drew a Lot: keep it or discard it."))
				: Tuning.bClassicTurn && !Frame.Controller->GetBuildRoute().IsNone() ? FString(TEXT("Choose which tickets to pay with, then Build. Or place a Lot on it."))
				: Tuning.bClassicTurn && State.BuysThisTurn > 0 ? FString(Tuning.TicketCost > 0 ? TEXT("Buy 1 more ticket: face-up (not a Wild) or from the deck.")
					: TEXT("Take 1 more ticket: face-up (not a Wild) or from the deck."))
				: Tuning.bClassicTurn && Tuning.TicketCost > 0 ? FString::Printf(TEXT("Your turn: buy 2 tickets ($%d each), OR click a route to claim it, OR draw 3 Route cards."), Tuning.TicketCost)
				: Tuning.bClassicTurn ? FString(TEXT("Your turn: take 2 tickets, OR click a route to claim it, OR draw 3 Route cards."))
				: Tuning.bOneMainActionPerTurn && State.BuysThisTurn > 0 ? FString(TEXT("You bought 1 ticket: buy 1 more, or press Done buying."))
				: !Frame.Controller->GetBuildRoute().IsNone() ? FString(TEXT("Choose which tickets to pay with, then Build."))
				: Tuning.bOneMainActionPerTurn ? FString(TEXT("Your turn: pick ONE. Buy 2 tickets or 1 property card, OR click a route to build it."))
				: Tuning.bCardEconomy ? FString(TEXT("Your turn. Buy tickets (as many as you can afford) and 1 property card, or draw Route cards. Brighter routes are ready to claim."))
				: Tuning.bDrawReplacesBuild ? FString(TEXT("Your turn. Draw Route cards (no building this turn), or Skip to build."))
				: FString(TEXT("Your turn. Draw Route cards, or Skip."));
			break;
		case EGridlockPhase::Resolve:
			Headline = Frame.Controller->IsDemolishMode()
				? TEXT("Click a glowing opponent property to demolish it, or Skip.")
				: TEXT("Click a glowing opponent property to buy it out, or Skip.");
			break;
		case EGridlockPhase::Build:
			Headline = !Frame.Controller->GetBuildRoute().IsNone() ? TEXT("Choose which tickets to pay with, then Build.")
				: Tuning.bCardEconomy ? TEXT("Click a route to build it and choose your tickets. Thick routes are ones your tickets pay for. Gold rings: your cards' cities.")
				: TEXT("Click a glowing route to claim it. Gold rings mark your cards' cities.");
			break;
		case EGridlockPhase::BuyUpgrade:
			Headline = Frame.Controller->IsSellMode() ? FString(TEXT("Sell: click one of your properties, or click Sell again to stop."))
				: Tuning.bOneMainActionPerTurn ? FString::Printf(TEXT("Main action done. Optional: click a glowing slot to play a Lot ($%d) or an upgrade card, or sell. Then End turn."), Tuning.LotCost)
				: Tuning.bCardEconomy ? FString::Printf(TEXT("Click a glowing slot: empty = play a Lot card ($%d), yours = play an upgrade card. Or End turn."), Tuning.LotCost)
				: FString::Printf(TEXT("Click a glowing slot: empty = Lot ($%d), yours = upgrade. Or End turn."), Tuning.LotCost);
			break;
		default:
			break;
		}
		Headline = Who + Headline;
		if (!Frame.AI)
		{
			Color = GridlockBoard::PlayerColor(Frame.Human);
		}
	}

	UFont* Font = GEngine->GetLargeFont();
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Headline, TextWidth, TextHeight, Font);
	const float Scale = TextWidth > Width - 32.0f ? (Width - 32.0f) / TextWidth : 1.0f;
	DrawTextShadowed(Headline, Color, (Width - TextWidth * Scale) * 0.5f, (GridlockHUDPrivate::BannerHeight - TextHeight * Scale) * 0.5f, Font, Scale);

	// Feedback or the reason a click was refused, just under the banner for a few seconds.
	const FString Message = Frame.Controller->GetMessage().ToString();
	if (!Message.IsEmpty() && Frame.Controller->GetMessageAge() < 6.0)
	{
		UFont* MessageFont = GEngine->GetMediumFont();
		GetTextSize(Message, TextWidth, TextHeight, MessageFont);
		const float X = (Width - TextWidth) * 0.5f;
		DrawRect(GridlockHUDPrivate::PanelColor, X - 12.0f, GridlockHUDPrivate::BannerHeight, TextWidth + 24.0f, TextHeight + 10.0f);
		DrawTextShadowed(Message, GridlockHUDPrivate::NoticeColor, X, GridlockHUDPrivate::BannerHeight + 5.0f, MessageFont);
	}
}

void AGridlockHUD::DrawRouteTiles(const FDrawFrame& Frame)
{
	const FBoardState& State = *Frame.State;
	const FRulesContext& Context = *Frame.Context;
	if (!State.Players.IsValidIndex(Frame.Human))
	{
		return;
	}
	const TArray<FName>& Hand = State.Players[Frame.Human].Hand;
	UFont* Small = GEngine->GetSmallFont();
	const float Top = GridlockHUDPrivate::BannerHeight + 4.0f;
	const float Height = 40.0f;
	const float Gap = 6.0f;

	if (Hand.IsEmpty())
	{
		if (State.Phase != EGridlockPhase::Setup && State.Phase != EGridlockPhase::GameOver)
		{
			DrawTextShadowed(Context.Tuning->bRouteCardsWhenHandEmpty
				? TEXT("All your Route cards are done: new ones are dealt at the start of your next turn.")
				: TEXT("No Route cards in front of you: draw some at the start of your turn."), GridlockHUDPrivate::Quiet, 14.0f, Top + 12.0f, Small);
		}
		return;
	}

	const float TileWidth = FMath::Min(210.0f, (Canvas->ClipX - 28.0f - Gap * (Hand.Num() - 1)) / Hand.Num());
	for (int32 Index = 0; Index < Hand.Num(); ++Index)
	{
		const FName CardId = Hand[Index];
		const float X = 14.0f + Index * (TileWidth + Gap);
		const bool bHover = Frame.Mouse.X >= X && Frame.Mouse.X <= X + TileWidth && Frame.Mouse.Y >= Top && Frame.Mouse.Y <= Top + Height;
		const bool bDone = URulesLibrary::IsCardComplete(State, Context, Frame.Human, CardId);

		DrawRect(FLinearColor(0.03f, 0.03f, 0.05f, bHover ? 0.95f : 0.82f), X, Top, TileWidth, Height);
		DrawOutline(X, Top, TileWidth, Height, bDone ? FLinearColor(0.3f, 0.9f, 0.3f) : GridlockBoard::Clickable, bHover ? 2.0f : 1.0f);
		DrawTextShadowed(GridlockHUDPrivate::CardName(Context, CardId), GridlockHUDPrivate::Plain, X + 8.0f, Top + 4.0f, Small);
		DrawTextShadowed(GridlockHUDPrivate::CardProgress(State, Context, Frame.Human, CardId), GridlockHUDPrivate::Quiet, X + 8.0f, Top + 21.0f, Small);

		// Hovering a card rings its two cities on the board, larger than the usual goal rings, and
		// marks a suggested path: the cheapest unclaimed routes that would complete it.
		const FRouteCardDef* Card = bHover ? Context.Deck->FindCard(CardId) : nullptr;
		if (Card)
		{
			for (const FName RouteId : FAIScoring::NeededRoutesForCard(State, Context, Frame.Human, CardId))
			{
				if (const FRouteDef* Route = Context.Map->FindRoute(RouteId))
				{
					const FVector Screen = Project(GridlockBoard::RouteMidpoint(*Route, *Context.Map, 10.0));
					DrawRing(FVector2D(Screen.X, Screen.Y), 13.0f, FLinearColor::White, 2.5f);
				}
			}
			for (const FName CityId : { Card->CityA, Card->CityB })
			{
				if (const FCityDef* City = Context.Map->FindCity(CityId))
				{
					const FVector Screen = Project(GridlockBoard::ToWorld(City->Position, 10.0));
					DrawRing(FVector2D(Screen.X, Screen.Y), 32.0f, FLinearColor::White, 3.0f);
				}
			}
		}
	}
}

void AGridlockHUD::DrawPanel(const FDrawFrame& Frame)
{
	const FBoardState& State = *Frame.State;
	const FRulesContext& Context = *Frame.Context;
	const float Width = Canvas->ClipX;
	const float Top = Canvas->ClipY - GridlockHUDPrivate::PanelHeight;
	UFont* Medium = GEngine->GetMediumFont();
	UFont* Small = GEngine->GetSmallFont();
	DrawRect(GridlockHUDPrivate::PanelColor, 0.0f, Top, Width, GridlockHUDPrivate::PanelHeight);

	// Column 1: the turn, both players, and your Route cards.
	const float Left = 14.0f;
	float Y = Top + 8.0f;
	DrawTextShadowed(Context.Tuning->bClassicTurn
		? FString::Printf(TEXT("Round %d%s"), State.Round, State.FinalTurnsLeft >= 0 ? TEXT("   LAST ROUND: one more turn each") : TEXT(""))
		: FString::Printf(TEXT("Round %d of %d   %s"), State.Round, Context.Tuning->TurnLimit, GridlockHUDPrivate::StepName(State.Phase)),
		State.FinalTurnsLeft >= 0 ? GridlockHUDPrivate::NoticeColor : GridlockHUDPrivate::Quiet, Left, Y, Small);
	Y += 18.0f;

	// Both players, with how many cards each holds (the counts are public, the cards aren't).
	for (int32 Player = 0; Player < State.Players.Num(); ++Player)
	{
		const FPlayerData& Data = State.Players[Player];
		if (Context.Tuning->bClassicTurn)
		{
			// Ticket to Ride: points, trains, and how many cards each holds (the cards themselves stay secret).
			const FString Money = Context.Tuning->TicketCost <= 0 ? FString()
				: Context.Tuning->bUnlimitedCash && Context.Tuning->CityPropertyPrice > 0
				? FString::Printf(TEXT("   spent $%d  earned $%d  profit %s$%d"), Data.Spent, Data.Earned, Data.Earned >= Data.Spent ? TEXT("") : TEXT("-"), FMath::Abs(Data.Earned - Data.Spent))
				: Context.Tuning->bUnlimitedCash ? FString::Printf(TEXT("   spent $%d"), Data.Spent)
				: FString::Printf(TEXT("   $%d"), Data.Cash);
			DrawTextShadowed(FString::Printf(TEXT("%s%s   %d pts%s   %d trains   %d tickets   %d Route cards%s"), State.ActivePlayer == Player ? TEXT("> ") : TEXT("   "),
				*PlayerLabel(Frame, Player), Data.Points, *Money, Data.Trains, Data.TicketCount(), Data.Hand.Num() + Data.Completed.Num(),
				State.Phase == EGridlockPhase::GameOver ? *FString::Printf(TEXT(" (%d done)"), Data.Completed.Num()) : TEXT("")),
				GridlockBoard::PlayerColor(Player), Left, Y, Small);
			Y += 19.0f;
			continue;
		}
		const bool bCatchUp = URulesLibrary::IsCatchUpActive(State, Context, Player);
		const FString Cards = Context.Tuning->bCardEconomy
			? FString::Printf(TEXT("   %d tickets, %d property cards"), Data.TicketCount(), Data.PropertyCardCount())
			: FString();
		DrawTextShadowed(FString::Printf(TEXT("%s%s   $%d   %d pts   %d of %d Route cards done%s%s"), State.ActivePlayer == Player ? TEXT("> ") : TEXT("   "),
			*PlayerLabel(Frame, Player), Data.Cash, Data.Points, Data.Completed.Num(), Context.GetCardsToWin(), *Cards,
			bCatchUp ? TEXT("   (catch-up)") : TEXT("")), GridlockBoard::PlayerColor(Player), Left, Y, Small);
		Y += 19.0f;
	}

	// Your hand: tickets as colored cards, one per color with its count, then property cards by tier.
	if (State.Players.IsValidIndex(Frame.Human) && Context.Tuning->bCardEconomy)
	{
		const FPlayerData& Me = State.Players[Frame.Human];
		Y += 8.0f;
		DrawTextShadowed(TEXT("Tickets"), GridlockHUDPrivate::Plain, Left, Y + 5.0f, Small);
		float X = Left + 66.0f;
		TArray<EGridlockColor> Shown = Context.Tuning->GetDeckColors();
		Shown.Add(EGridlockColor::Wild);
		if (Context.Tuning->RouteLotCards > 0)
		{
			Shown.Add(EGridlockColor::Lot);
		}
		for (int32 Index = 0; Index < Shown.Num(); ++Index)
		{
			// Two rows when there are many colors (the property cards' row is free under Ticket to Ride rules).
			if (Context.Tuning->bClassicTurn && Index == 5)
			{
				Y += 30.0f;
				X = Left + 66.0f;
			}
			const EGridlockColor TicketColor = Shown[Index];
			X += DrawCardChip(FString::Printf(TEXT("%s %d"), *GridlockBoard::TicketColorName(TicketColor), Me.TicketsOf(TicketColor)),
				GridlockBoard::TicketColor(TicketColor), X, Y, Me.TicketsOf(TicketColor) > 0) + 5.0f;
		}

		if (!Context.Tuning->bClassicTurn)
		{
			Y += 30.0f;
			DrawTextShadowed(TEXT("Property"), GridlockHUDPrivate::Plain, Left, Y + 5.0f, Small);
			X = Left + 66.0f;
			for (uint8 Tier = static_cast<uint8>(EPropertyTier::Lot); Tier <= static_cast<uint8>(EPropertyTier::Landmark); ++Tier)
			{
				const int32 Count = Me.PropertyCardsOf(static_cast<EPropertyTier>(Tier));
				X += DrawCardChip(FString::Printf(TEXT("%s %d"), GridlockHUDPrivate::TierName(Tier), Count), GridlockBoard::Clickable, X, Y, Count > 0) + 5.0f;
			}
		}

		// What you just drew, for a few seconds, just above the panel.
		const double Now = GetWorld()->GetRealTimeSeconds();
		RecentDraws.RemoveAll([Now](const FRecentDraw& Draw) { return Draw.Until < Now; });
		if (!RecentDraws.IsEmpty())
		{
			const float ToastTop = Top - 38.0f;
			float ToastX = Left;
			DrawRect(GridlockHUDPrivate::PanelColor, Left - 6.0f, ToastTop - 4.0f, 90.0f + RecentDraws.Num() * 96.0f, 34.0f);
			DrawTextShadowed(TEXT("You drew:"), GridlockHUDPrivate::NoticeColor, ToastX, ToastTop + 4.0f, Medium);
			ToastX += 84.0f;
			for (const FRecentDraw& Draw : RecentDraws)
			{
				ToastX += DrawCardChip(Draw.Label, Draw.Color, ToastX, ToastTop, true) + 6.0f;
			}
		}
	}

	// Column 2: what happened, newest last.
	const float LogLeft = Width * 0.46f;
	const float LogWidth = Width * 0.26f - 12.0f;
	DrawTextShadowed(TEXT("What happened"), GridlockHUDPrivate::Quiet, LogLeft, Top + 8.0f, Small);
	const int32 First = FMath::Max(0, Log.Num() - GridlockHUDPrivate::LogLinesShown);
	for (int32 Index = First; Index < Log.Num(); ++Index)
	{
		FString Text = Log[Index].Text;
		float TextWidth = 0.0f;
		float TextHeight = 0.0f;
		GetTextSize(Text, TextWidth, TextHeight, Small);
		while (TextWidth > LogWidth && Text.Len() > 8)
		{
			Text = Text.LeftChop(4) + TEXT("...");
			GetTextSize(Text, TextWidth, TextHeight, Small);
		}
		DrawTextShadowed(Text, Log[Index].Color, LogLeft, Top + 28.0f + (Index - First) * 19.0f, Small);
	}

	// Column 3: buttons.
	DrawButtons(Frame, Width * 0.73f, Top + 12.0f, Width * 0.27f - 14.0f);
}

void AGridlockHUD::DrawButtons(const FDrawFrame& Frame, float Left, float Top, float Width)
{
	static const FName Next(TEXT("Next")), Draw(TEXT("Draw")), Keep(TEXT("Keep")), Undo(TEXT("Undo")), Help(TEXT("Help")),
		Mode(TEXT("Mode")), HotSeat(TEXT("HotSeat")), NewGame(TEXT("NewGame")), Wait(TEXT("Wait")),
		BuyTicket(TEXT("BuyTicket")), BuyTwo(TEXT("BuyTwo")), BuyProperty(TEXT("BuyProperty")), Sell(TEXT("Sell"));
	const FBoardState& State = *Frame.State;
	const UTuningData& Tuning = *Frame.Context->Tuning;
	const float RowHeight = 42.0f;
	const float Gap = 8.0f;
	const float Half = (Width - Gap) * 0.5f;
	float RowTop = Top + RowHeight + Gap;

	auto CanDo = [&Frame, &State](EGridlockActionType Type)
	{
		FGameAction Action;
		Action.Type = Type;
		Action.Player = Frame.Human;
		FText Reason;
		return URulesLibrary::IsLegal(State, Action, *Frame.Context, Reason);
	};

	if (State.Phase == EGridlockPhase::GameOver)
	{
		DrawButton(Frame, NewGame, TEXT("New game"), Left, Top, Half, RowHeight, true, true);
		DrawButton(Frame, TEXT("ResultsOpen"), TEXT("Results"), Left + Half + Gap, Top, Half, RowHeight, true, false);
	}
	else if (!Frame.bHumanTurn)
	{
		DrawButton(Frame, Wait, FString::Printf(TEXT("%s is playing..."), *PlayerLabel(Frame, State.ActivePlayer)), Left, Top, Width, RowHeight, false, false);
	}
	else if (!State.Players[Frame.Human].PendingDraw.IsEmpty())
	{
		DrawButton(Frame, Keep, TEXT("Keep selected cards  [Enter]"), Left, Top, Width, RowHeight, true, true);
	}
	else
	{
		// Rules v0.4: selling is an extra, at the start of the turn or after the main action.
		bool bOwnsProperty = false;
		for (const TPair<FName, FSlotState>& Entry : State.Slots)
		{
			bOwnsProperty |= Entry.Value.Owner == Frame.Human;
		}
		const bool bCanSell = Tuning.bCanSellProperties && bOwnsProperty && State.SalesThisTurn < Tuning.MaxSalesPerTurn;
		const FString SellLabel = Frame.Controller->IsSellMode() ? FString(TEXT("Selling: click a property  [S]")) : FString(TEXT("Sell a property  [S]"));

		switch (State.Phase)
		{
		case EGridlockPhase::Start:
			if (Tuning.bClassicTurn)
			{
				// Ticket to Ride: the face-up row (click one, or 1-5), then the deck and Route cards.
				const float Short = 34.0f;
				const float Step = Short + 6.0f;
				const float CardWidth = (Width - 4.0f * 6.0f) / 5.0f;
				for (int32 Slot = 0; Slot < State.FaceUp.Num(); ++Slot)
				{
					FGameAction Take;
					Take.Type = EGridlockActionType::TakeFaceUp;
					Take.Player = Frame.Human;
					Take.Slot = Slot;
					FText Reason;
					const bool bCanTake = URulesLibrary::IsLegal(State, Take, *Frame.Context, Reason);
					DrawFaceUpCard(Frame, Slot, State.FaceUp[Slot], Left + Slot * (CardWidth + 6.0f), Top, CardWidth, Short, bCanTake);
				}
				const int32 DeckLeft = State.TicketDeck.Num() + State.TicketDiscard.Num();
				DrawButton(Frame, BuyTicket, Tuning.TicketCost > 0 ? FString::Printf(TEXT("Deck (%d) $%d  [T]"), DeckLeft, Tuning.TicketCost)
					: FString::Printf(TEXT("From the deck (%d)  [T]"), DeckLeft), Left, Top + Step, Half, Short,
					CanDo(EGridlockActionType::BuyTicket), true);
				DrawButton(Frame, Draw, FString::Printf(TEXT("%d Route cards  [D]"), Tuning.DrawCount), Left + Half + Gap, Top + Step, Half, Short,
					CanDo(EGridlockActionType::DrawCards), false);
				FText PassReason;
				FGameAction Pass;
				Pass.Type = EGridlockActionType::EndPhase;
				Pass.Player = Frame.Human;
				if (URulesLibrary::IsLegal(State, Pass, *Frame.Context, PassReason))
				{
					DrawButton(Frame, Next, TEXT("Nothing to do: pass  [Space]"), Left, Top + 2.0f * Step, Width, Short, true, false);
				}
				else
				{
					DrawTextShadowed(State.BuysThisTurn > 0 ? TEXT("Take your second ticket (a face-up Wild can't be second).")
						: Tuning.TicketCost > 0 ? *FString::Printf(TEXT("Face-up $%d each: click one or press 1-5. Or click a route."), Tuning.TicketCost)
						: TEXT("Face-up: click one or press 1-5. Or click a route to claim it."), GridlockHUDPrivate::Quiet, Left, Top + 2.0f * Step + 8.0f, GEngine->GetSmallFont());
				}
				RowTop = Top + 3.0f * Step;
			}
			else if (Tuning.bCardEconomy && Tuning.bOneMainActionPerTurn)
			{
				// One main action: buy (2 tickets or 1 property card), or move on to build or remove.
				const float Short = 34.0f;
				const float Step = Short + 6.0f;
				const bool bCanBuyTicket = CanDo(EGridlockActionType::BuyTicket);
				const int32 TicketsLeft = Tuning.MaxTicketsPerTurn > 0 ? Tuning.MaxTicketsPerTurn - State.TicketBuysThisTurn : 2;
				DrawButton(Frame, BuyTwo, FString::Printf(TEXT("Buy 2 tickets $%d  [Y]"), 2 * Tuning.TicketCost), Left, Top, Half, Short,
					bCanBuyTicket && TicketsLeft >= 2 && State.Players[Frame.Human].Cash >= 2 * Tuning.TicketCost, true);
				DrawButton(Frame, BuyTicket, FString::Printf(TEXT("Buy 1 ticket $%d  [T]"), Tuning.TicketCost), Left + Half + Gap, Top, Half, Short, bCanBuyTicket, true);
				DrawButton(Frame, BuyProperty, FString::Printf(TEXT("Buy 1 property card $%d  [P]"), Tuning.PropertyCardCost), Left, Top + Step, Half, Short,
					CanDo(EGridlockActionType::BuyPropertyCard), true);
				DrawButton(Frame, Sell, SellLabel, Left + Half + Gap, Top + Step, Half, Short, bCanSell, false);

				// Moving on needs something to claim or remove; otherwise the button would only throw the turn away.
				bool bCanBuild = false;
				FBoardState AtResolve = State;
				AtResolve.Phase = EGridlockPhase::Resolve;
				for (const FGameAction& Action : URulesLibrary::GetLegalActions(AtResolve, *Frame.Context))
				{
					bCanBuild |= Action.Type == EGridlockActionType::Buyout || Action.Type == EGridlockActionType::Demolish;
				}
				for (const FRouteDef& Route : Frame.Context->Map->Routes)
				{
					FText Reason;
					bCanBuild = bCanBuild || URulesLibrary::CanClaimLater(State, *Frame.Context, Route.Id, Reason);
				}
				if (State.BuysThisTurn > 0)
				{
					DrawButton(Frame, Next, TEXT("Done buying  [Space]"), Left, Top + 2.0f * Step, Width, Short, true, false);
				}
				else
				{
					DrawButton(Frame, Next, bCanBuild ? TEXT("Build a route or remove a property instead  [Space]") : TEXT("Nothing to build yet: buy tickets"),
						Left, Top + 2.0f * Step, Width, Short, bCanBuild, false);
				}
				RowTop = Top + 3.0f * Step;
			}
			else if (Tuning.bCardEconomy)
			{
				// Three short rows: tickets, then property card and Route cards, then Done.
				const float Short = 34.0f;
				const float Step = Short + 6.0f;
				const bool bCanBuyTicket = CanDo(EGridlockActionType::BuyTicket);
				DrawButton(Frame, BuyTicket, FString::Printf(TEXT("Buy 1 ticket $%d  [T]"), Tuning.TicketCost), Left, Top, Half, Short, bCanBuyTicket, true);
				DrawButton(Frame, BuyTwo, FString::Printf(TEXT("Buy 2 tickets $%d  [Y]"), 2 * Tuning.TicketCost), Left + Half + Gap, Top, Half, Short,
					bCanBuyTicket && State.Players[Frame.Human].Cash >= 2 * Tuning.TicketCost, true);
				DrawButton(Frame, BuyProperty, FString::Printf(TEXT("Buy property $%d (1/turn)  [P]"), Tuning.PropertyCardCost), Left, Top + Step, Half, Short,
					CanDo(EGridlockActionType::BuyPropertyCard), true);
				DrawButton(Frame, Draw, FString::Printf(TEXT("Draw %d Route cards  [D]"), Tuning.DrawCount), Left + Half + Gap, Top + Step, Half, Short,
					CanDo(EGridlockActionType::DrawCards), false);
				DrawButton(Frame, Next, State.BuysThisTurn > 0 ? FString::Printf(TEXT("Done buying (bought %d)  [Space]"), State.BuysThisTurn)
					: FString(TEXT("Done  [Space]")), Left, Top + 2.0f * Step, Width, Short, true, false);
				RowTop = Top + 3.0f * Step;
			}
			else
			{
				DrawButton(Frame, Draw, FString::Printf(TEXT("Draw %d cards  [D]"), Tuning.DrawCount), Left, Top, Half, RowHeight, CanDo(EGridlockActionType::DrawCards), true);
				DrawButton(Frame, Next, TEXT("Skip  [Space]"), Left + Half + Gap, Top, Half, RowHeight, true, false);
			}
			break;
		case EGridlockPhase::Resolve:
			DrawButton(Frame, Mode, Frame.Controller->IsDemolishMode() ? TEXT("Mode: Demolish") : TEXT("Mode: Buy out"), Left, Top, Half, RowHeight, true, false);
			DrawButton(Frame, Next, TEXT("Skip  [Space]"), Left + Half + Gap, Top, Half, RowHeight, true, false);
			break;
		case EGridlockPhase::Build:
			DrawButton(Frame, Next, TEXT("Skip building  [Space]"), Left, Top, Width, RowHeight, true, false);
			break;
		case EGridlockPhase::BuyUpgrade:
			if (Tuning.bCanSellProperties)
			{
				DrawButton(Frame, Next, TEXT("End turn  [Space]"), Left, Top, Half, RowHeight, true, true);
				DrawButton(Frame, Sell, SellLabel, Left + Half + Gap, Top, Half, RowHeight, bCanSell, false);
			}
			else
			{
				DrawButton(Frame, Next, TEXT("End turn  [Space]"), Left, Top, Width, RowHeight, true, true);
			}
			break;
		default:
			break;
		}
	}

	const float Third = (Width - 2.0f * Gap) / 3.0f;
	const AGridlockGameMode* GameMode = GetWorld()->GetAuthGameMode<AGridlockGameMode>();
	DrawButton(Frame, Undo, TEXT("Undo  [Z]"), Left, RowTop, Third, 32.0f, State.Phase != EGridlockPhase::Setup, false);
	DrawButton(Frame, Help, TEXT("Help  [F1]"), Left + Third + Gap, RowTop, Third, 32.0f, true, false);
	DrawButton(Frame, HotSeat, GameMode && GameMode->bHotSeat ? TEXT("Hot-seat: on") : TEXT("Hot-seat: off"),
		Left + 2.0f * (Third + Gap), RowTop, Third, 32.0f, true, false);

	if (Frame.bHumanTurn && State.Phase == EGridlockPhase::Resolve && RowTop + 60.0f < Canvas->ClipY)
	{
		DrawTextShadowed(TEXT("Tip: right-click a property to demolish it directly."), GridlockHUDPrivate::Quiet, Left, RowTop + 40.0f, GEngine->GetSmallFont());
	}
}

void AGridlockHUD::DrawKeepPopup(const FDrawFrame& Frame)
{
	const FBoardState& State = *Frame.State;
	const FRulesContext& Context = *Frame.Context;
	const FPlayerData& Me = State.Players[Frame.Human];
	const TArray<bool>& Keep = Frame.Controller->GetKeepSelection();
	const int32 Minimum = FMath::Min(State.Phase == EGridlockPhase::Setup ? Context.Tuning->InitialKeep : Context.Tuning->MinKeep, Me.PendingDraw.Num());

	int32 Kept = 0;
	for (int32 Index = 0; Index < Me.PendingDraw.Num(); ++Index)
	{
		Kept += Keep.IsValidIndex(Index) && Keep[Index] ? 1 : 0;
	}

	const float BoxWidth = 600.0f;
	const float CardHeight = 46.0f;
	const float BoxHeight = 92.0f + Me.PendingDraw.Num() * (CardHeight + 8.0f) + 58.0f;
	const float X = (Canvas->ClipX - BoxWidth) * 0.5f;
	const float Y = GridlockHUDPrivate::BannerHeight + 44.0f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.94f), X, Y, BoxWidth, BoxHeight);
	DrawOutline(X, Y, BoxWidth, BoxHeight, GridlockBoard::Clickable, 2.0f);

	DrawTextShadowed(State.Phase == EGridlockPhase::Setup ? TEXT("Choose your starting Route cards") : TEXT("Choose which drawn cards to keep"),
		GridlockBoard::Clickable, X + 20.0f, Y + 14.0f, GEngine->GetLargeFont());
	DrawTextShadowed(FString::Printf(TEXT("Click a card to keep it or put it back. Keep at least %d."), Minimum),
		GridlockHUDPrivate::Plain, X + 20.0f, Y + 46.0f, GEngine->GetSmallFont());
	DrawTextShadowed(TEXT("A card scores when your own routes join its two cities. Unkept cards go back under the deck."),
		GridlockHUDPrivate::Quiet, X + 20.0f, Y + 62.0f, GEngine->GetSmallFont());

	for (int32 Index = 0; Index < Me.PendingDraw.Num(); ++Index)
	{
		const bool bKeep = Keep.IsValidIndex(Index) && Keep[Index];
		const FName CardId = Me.PendingDraw[Index];
		const FString Label = FString::Printf(TEXT("%s  %d.  %s     %s"), bKeep ? TEXT("[keep]") : TEXT("[ -- ]"), Index + 1,
			*GridlockHUDPrivate::CardName(Context, CardId), *GridlockHUDPrivate::CardProgress(State, Context, Frame.Human, CardId));
		DrawButton(Frame, FName(*FString::Printf(TEXT("Card%d"), Index)), Label, X + 20.0f, Y + 86.0f + Index * (CardHeight + 8.0f),
			BoxWidth - 40.0f, CardHeight, true, bKeep);
	}

	const float ButtonWidth = 280.0f;
	DrawButton(Frame, TEXT("Keep"), FString::Printf(TEXT("Keep %d card%s  [Enter]"), Kept, Kept == 1 ? TEXT("") : TEXT("s")),
		X + (BoxWidth - ButtonWidth) * 0.5f, Y + BoxHeight - 52.0f, ButtonWidth, 40.0f, Kept >= Minimum, true);
}

void AGridlockHUD::DrawBuildPopup(const FDrawFrame& Frame)
{
	const FBoardState& State = *Frame.State;
	const FRulesContext& Context = *Frame.Context;
	const FRouteDef* Route = Context.Map->FindRoute(Frame.Controller->GetBuildRoute());
	if (!Route || !State.Players.IsValidIndex(Frame.Human))
	{
		return;
	}
	const FPlayerData& Me = State.Players[Frame.Human];
	const bool bGray = Route->Color == EGridlockColor::Gray;
	const EGridlockColor Color = Frame.Controller->GetBuildColor();
	const int32 Wilds = FMath::Min(Frame.Controller->GetBuildWilds(), Route->Length);
	const int32 Colored = Route->Length - Wilds;
	UFont* Small = GEngine->GetSmallFont();
	UFont* Medium = GEngine->GetMediumFont();

	const float BoxWidth = 620.0f;
	const float BoxHeight = Context.Tuning->RouteLotCards > 0 ? 300.0f : 262.0f;
	const float X = (Canvas->ClipX - BoxWidth) * 0.5f;
	const float Y = GridlockHUDPrivate::BannerHeight + 52.0f;
	DrawRect(FLinearColor(0.02f, 0.02f, 0.05f, 0.95f), X, Y, BoxWidth, BoxHeight);
	DrawOutline(X, Y, BoxWidth, BoxHeight, bGray ? GridlockHUDPrivate::Plain : GridlockBoard::TicketColor(Route->Color), 2.0f);

	DrawTextShadowed(FString::Printf(TEXT("Build %s"), *RouteName(Context, Route->Id)), GridlockBoard::Clickable, X + 20.0f, Y + 12.0f, GEngine->GetLargeFont());
	DrawTextShadowed(bGray
		? FString::Printf(TEXT("%d pins, gray route: pay %d tickets of any ONE color. Wild tickets can stand in for any of them."), Route->Length, Route->Length)
		: FString::Printf(TEXT("%d pins, %s route: pay %d %s tickets. Wild tickets can stand in for any of them."), Route->Length,
			*GridlockBoard::TicketColorName(Route->Color), Route->Length, *GridlockBoard::TicketColorName(Route->Color)),
		GridlockHUDPrivate::Plain, X + 20.0f, Y + 44.0f, Small);

	// Color: one choice on a colored route; any color you hold on a gray one (click to pick).
	float RowY = Y + 74.0f;
	DrawTextShadowed(TEXT("Color"), GridlockHUDPrivate::Quiet, X + 20.0f, RowY + 4.0f, Medium);
	float ChipX = X + 110.0f;
	for (uint8 Index = static_cast<uint8>(EGridlockColor::Red); Index <= static_cast<uint8>(EGridlockColor::White); ++Index)
	{
		const EGridlockColor Option = static_cast<EGridlockColor>(Index);
		if (!bGray && Option != Route->Color)
		{
			continue;
		}
		const float Width = DrawCardChip(FString::Printf(TEXT("%s: you have %d"), *GridlockBoard::TicketColorName(Option), Me.TicketsOf(Option)),
			GridlockBoard::TicketColor(Option), ChipX, RowY, Me.TicketsOf(Option) > 0 || Option == Color);
		if (Option == Color)
		{
			DrawOutline(ChipX - 3.0f, RowY - 3.0f, Width + 6.0f, 30.0f, FLinearColor::White, 2.0f);
		}
		if (bGray)
		{
			Buttons.Add({ FName(*FString::Printf(TEXT("PayColor%d"), Index)), FVector2D(ChipX, RowY), FVector2D(ChipX + Width, RowY + 24.0f) });
		}
		ChipX += Width + 10.0f;
	}

	// Wilds: as many as you like, up to the route's length.
	RowY += 40.0f;
	DrawTextShadowed(TEXT("Wild"), GridlockHUDPrivate::Quiet, X + 20.0f, RowY + 4.0f, Medium);
	DrawButton(Frame, TEXT("WildLess"), TEXT("-"), X + 110.0f, RowY - 2.0f, 34.0f, 28.0f, Wilds > 0, false);
	const float WildWidth = DrawCardChip(FString::Printf(TEXT("use %d Wild"), Wilds), GridlockBoard::TicketColor(EGridlockColor::Wild), X + 152.0f, RowY, true);
	DrawButton(Frame, TEXT("WildMore"), TEXT("+"), X + 160.0f + WildWidth, RowY - 2.0f, 34.0f, 28.0f, Wilds < Route->Length, false);
	DrawTextShadowed(FString::Printf(TEXT("you have %d Wild"), Me.TicketsOf(EGridlockColor::Wild)), GridlockHUDPrivate::Quiet, X + 206.0f + WildWidth, RowY + 4.0f, Small);

	// What you'd pay, and whether you can.
	RowY += 40.0f;
	DrawTextShadowed(TEXT("You pay"), GridlockHUDPrivate::Quiet, X + 20.0f, RowY + 4.0f, Medium);
	ChipX = X + 110.0f;
	if (Colored > 0)
	{
		ChipX += DrawCardChip(FString::Printf(TEXT("%d %s"), Colored, *GridlockBoard::TicketColorName(Color)), GridlockBoard::TicketColor(Color), ChipX, RowY, true) + 8.0f;
	}
	if (Wilds > 0)
	{
		ChipX += DrawCardChip(FString::Printf(TEXT("%d Wild"), Wilds), GridlockBoard::TicketColor(EGridlockColor::Wild), ChipX, RowY, true) + 8.0f;
	}
	FText Reason;
	const bool bCanBuild = URulesLibrary::CanClaimLater(State, Context, Route->Id, Reason, Color, Wilds);
	if (!bCanBuild)
	{
		DrawTextShadowed(Reason.ToString(), GridlockHUDPrivate::NoticeColor, X + 20.0f, RowY + 32.0f, Small);
	}

	// Lots (v0.7): the route's slots, and placing one of yours (free; the window stays open).
	const int32 Slots = Context.Tuning->GetLotSlots(Route->Length);
	if (Context.Tuning->RouteLotCards > 0 && Slots > 0)
	{
		const FRouteLots* Lots = State.RouteLots.Find(Route->Id);
		const int32 Placed = Lots ? Lots->Owners.Num() : 0;
		const int32 Mine = Lots ? Algo::Count(Lots->Owners, Frame.Human) : 0;
		const int32 Blocker = URulesLibrary::GetRouteBlocker(State, Context, Route->Id);
		const FString Text = Blocker == Frame.Human ? FString::Printf(TEXT("Lots: all %d yours, so only you can build it."), Slots)
			: Blocker != INDEX_NONE ? FString::Printf(TEXT("Lots: all %d are your opponent's, so you can't build it."), Slots)
			: FString::Printf(TEXT("Lots: %d of %d taken (%d yours). Own all %d and only you can build it."), Placed, Slots, Mine, Slots);
		const float LotsY = Y + 216.0f;
		DrawTextShadowed(Text, GridlockHUDPrivate::Plain, X + 20.0f, LotsY + 4.0f, Small);
		FGameAction Place;
		Place.Type = EGridlockActionType::PlaceRouteLot;
		Place.Player = Frame.Human;
		Place.RouteId = Route->Id;
		FText PlaceReason;
		DrawButton(Frame, TEXT("PlaceLotHere"), FString::Printf(TEXT("Place a Lot, end turn (%d)"), Me.LotCardCount()), X + BoxWidth - 220.0f, LotsY - 2.0f, 200.0f, 28.0f,
			URulesLibrary::IsLegal(State, Place, Context, PlaceReason), false);
	}

	const float ButtonWidth = 200.0f;
	const float ButtonY = Y + BoxHeight - 50.0f;
	DrawButton(Frame, TEXT("BuildConfirm"), TEXT("Build  [Enter]"), X + BoxWidth * 0.5f - ButtonWidth - 8.0f, ButtonY, ButtonWidth, 38.0f, bCanBuild, true);
	DrawButton(Frame, TEXT("BuildCancel"), TEXT("Cancel  [Esc]"), X + BoxWidth * 0.5f + 8.0f, ButtonY, ButtonWidth, 38.0f, true, false);

	// Clicks elsewhere on the window do nothing (rather than picking the route underneath).
	Buttons.Add({ TEXT("BuildWindow"), FVector2D(X, Y), FVector2D(X + BoxWidth, Y + BoxHeight) });
}

void AGridlockHUD::DrawTooltip(const FDrawFrame& Frame)
{
	const FName City = GetCityAt(Frame.Mouse);
	const FName RouteId = Frame.Controller->GetHoveredRouteId();
	const TArray<FString> Lines = !City.IsNone() ? DescribeCity(Frame, City) : DescribeHover(Frame, RouteId);
	if (Lines.IsEmpty())
	{
		return;
	}

	UFont* Font = GEngine->GetSmallFont();
	float BoxWidth = 0.0f;
	for (const FString& Line : Lines)
	{
		float LineWidth = 0.0f;
		float LineHeight = 0.0f;
		GetTextSize(Line, LineWidth, LineHeight, Font);
		BoxWidth = FMath::Max(BoxWidth, LineWidth);
	}
	BoxWidth += 20.0f;
	const float BoxHeight = Lines.Num() * 18.0f + 12.0f;
	const float X = FMath::Min(Frame.Mouse.X + 20.0f, Canvas->ClipX - BoxWidth - 8.0f);
	const float Y = FMath::Min(Frame.Mouse.Y + 20.0f, Canvas->ClipY - GridlockHUDPrivate::PanelHeight - BoxHeight - 4.0f);

	DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.88f), X, Y, BoxWidth, BoxHeight);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const bool bAction = Index == Lines.Num() - 1 && Frame.bHumanTurn;
		DrawTextShadowed(Lines[Index], bAction ? GridlockBoard::Clickable : GridlockHUDPrivate::Plain, X + 10.0f, Y + 6.0f + Index * 18.0f, Font);
	}
}

TArray<FString> AGridlockHUD::DescribeHover(const FDrawFrame& Frame, FName RouteId) const
{
	TArray<FString> Lines;
	const FBoardState& State = *Frame.State;
	const FRulesContext& Context = *Frame.Context;
	const FRouteDef* Route = Context.Map->FindRoute(RouteId);
	if (!Route)
	{
		return Lines;
	}

	Lines.Add(FString::Printf(TEXT("%s   (%s, %d dot%s)"), *RouteName(Context, RouteId), *GridlockBoard::TicketColorName(Route->Color),
		Route->Length, Route->Length == 1 ? TEXT("") : TEXT("s")));

	// What claiming it takes, against the tickets you hold.
	if (Context.Tuning->bCardEconomy && State.RouteOwner.FindRef(RouteId) == INDEX_NONE && State.Players.IsValidIndex(Frame.Human))
	{
		const FPlayerData& Me = State.Players[Frame.Human];
		Lines.Add(Route->Color == EGridlockColor::Gray
			? FString::Printf(TEXT("Needs %d tickets of any one color (Wild counts). You hold %d tickets, %d Wild."),
				Route->Length, Me.TicketCount(), Me.TicketsOf(EGridlockColor::Wild))
			: FString::Printf(TEXT("Needs %d %s tickets (Wild counts). You hold %d %s, %d Wild."),
				Route->Length, *GridlockBoard::TicketColorName(Route->Color),
				Me.TicketsOf(Route->Color), *GridlockBoard::TicketColorName(Route->Color), Me.TicketsOf(EGridlockColor::Wild)));
	}

	const int32 RouteOwner = State.RouteOwner.FindRef(RouteId);
	if (RouteOwner == INDEX_NONE)
	{
		Lines.Add(TEXT("Unclaimed"));
	}
	else
	{
		Lines.Add(FString::Printf(TEXT("%s's route%s"), *PlayerLabel(Frame, RouteOwner),
			URulesLibrary::IsRouteSevered(State, RouteId, RouteOwner) ? TEXT(", cut by a Complex: doesn't count for cards") : TEXT("")));
	}

	const FSlotState Slot = State.Slots.FindRef(RouteId);
	if (Context.Tuning->bClassicTurn)
	{
		// Ticket to Ride rules: no properties, but lots (v0.7).
		const int32 Slots = Context.Tuning->GetLotSlots(Route->Length);
		if (Context.Tuning->RouteLotCards > 0 && Slots > 0)
		{
			const FRouteLots* Lots = State.RouteLots.Find(RouteId);
			const int32 Blocker = URulesLibrary::GetRouteBlocker(State, Context, RouteId);
			Lines.Add(Blocker != INDEX_NONE ? FString::Printf(TEXT("Lots: all %d %s's, so only %s can build it"), Slots, *PlayerLabel(Frame, Blocker), *PlayerLabel(Frame, Blocker))
				: FString::Printf(TEXT("Lots: %d of %d slots taken"), Lots ? Lots->Owners.Num() : 0, Slots));
		}
	}
	else if (Slot.RubbleTicks > 0)
	{
		Lines.Add(TEXT("Slot: rubble, nothing can be built here yet"));
	}
	else if (Slot.Owner != INDEX_NONE)
	{
		Lines.Add(FString::Printf(TEXT("Slot: %s's %s%s, toll $%d a turn"), *PlayerLabel(Frame, Slot.Owner),
			GridlockHUDPrivate::TierName(static_cast<int32>(Slot.Tier)), Slot.bUpgrading ? TEXT(" (upgrading)") : TEXT(""), Context.Tuning->GetToll(Slot.Tier)));
	}
	else
	{
		Lines.Add(TEXT("Slot: empty"));
	}

	// What a click would do here, with its cost, or why it isn't possible.
	if (!Frame.bHumanTurn)
	{
		return Lines;
	}
	auto Try = [&](EGridlockActionType Type, const TCHAR* Verb)
	{
		FGameAction Action;
		Action.Type = Type;
		Action.Player = Frame.Human;
		Action.RouteId = RouteId;
		FText Reason;
		return URulesLibrary::IsLegal(State, Action, Context, Reason)
			? FString::Printf(TEXT("%s for $%d"), Verb, URulesLibrary::PreviewCost(State, Action, Context))
			: FString::Printf(TEXT("Can't %s: %s"), *FString(Verb).ToLower(), *Reason.ToString());
	};
	if (Frame.Controller->IsSellMode())
	{
		FGameAction Sale;
		Sale.Type = EGridlockActionType::SellProperty;
		Sale.Player = Frame.Human;
		Sale.RouteId = RouteId;
		FText Reason;
		Lines.Add(URulesLibrary::IsLegal(State, Sale, Context, Reason)
			? FString::Printf(TEXT("Click: Sell for $%d"), -URulesLibrary::PreviewCost(State, Sale, Context))
			: FString::Printf(TEXT("Can't sell: %s"), *Reason.ToString()));
		return Lines;
	}
	switch (State.Phase)
	{
	case EGridlockPhase::Start:
		if ((Context.Tuning->bOneMainActionPerTurn || Context.Tuning->bClassicTurn) && State.RouteOwner.FindRef(RouteId) == INDEX_NONE)
		{
			FText Reason;
			Lines.Add(URulesLibrary::CanClaimLater(State, Context, RouteId, Reason)
				? FString::Printf(TEXT("Click: claim it and choose your tickets%s"), Context.Tuning->GetRoutePoints(Route->Length) > 0
					? *FString::Printf(TEXT(" (+%d points)"), Context.Tuning->GetRoutePoints(Route->Length)) : TEXT(" (your main action this turn)"))
				: State.ActionsThisTurn > 0 ? FString::Printf(TEXT("Can't build: %s"), *Reason.ToString())
				: FString::Printf(TEXT("Click to choose tickets. Right now: %s"), *Reason.ToString()));
		}
		break;
	case EGridlockPhase::Resolve:
		Lines.Add(FString::Printf(TEXT("Click: %s   Right-click: %s"), *Try(EGridlockActionType::Buyout, TEXT("Buy out")), *Try(EGridlockActionType::Demolish, TEXT("Demolish"))));
		break;
	case EGridlockPhase::Build:
		if (State.RouteOwner.FindRef(RouteId) == INDEX_NONE)
		{
			FText Reason;
			Lines.Add(URulesLibrary::CanClaimLater(State, Context, RouteId, Reason)
				? FString(TEXT("Click: build it and choose your tickets"))
				: FString::Printf(TEXT("Click to choose tickets. Right now: %s"), *Reason.ToString()));
		}
		break;
	case EGridlockPhase::BuyUpgrade:
		Lines.Add(TEXT("Click: ") + (Slot.Owner == Frame.Human
			? Try(EGridlockActionType::Upgrade, *FString::Printf(TEXT("Upgrade to %s"), GridlockHUDPrivate::TierName(static_cast<int32>(Slot.Tier) + 1)))
			: Try(EGridlockActionType::PlaceLot, TEXT("Place a Lot"))));
		break;
	default:
		break;
	}
	return Lines;
}

void AGridlockHUD::DrawHelp(const FDrawFrame& Frame)
{
	const UTuningData& Tuning = *Frame.Context->Tuning;
	DrawRect(FLinearColor(0.01f, 0.01f, 0.02f, 0.97f), 0.0f, 0.0f, Canvas->ClipX, Canvas->ClipY);

	struct FHelpLine
	{
		FString Text;
		bool bHeading;
	};
	TArray<FHelpLine> Lines = {
		{ TEXT("GOAL"), true },
		{ FString::Printf(TEXT("Complete %d Route cards, or reach %d points. A Route card is complete when your own routes join its two cities."),
			Frame.Context->GetCardsToWin(), Tuning.PointsToWin), false },
		{ TEXT("Your cards' cities have gold rings on the board, and your cards are listed at the bottom left."), false },
	};
	const TArray<FHelpLine> OneActionTurn = {
		{ TEXT("EACH TURN (the banner at the top always says what to do)"), true },
		{ FString::Printf(TEXT("%s Finished all your Route cards? You're dealt %d new ones (keep at least %d)."),
			Tuning.BaseIncome > 0 ? *FString::Printf(TEXT("You get $%d plus any tolls."), Tuning.BaseIncome) : Tuning.CashPerCardPoint > 0 ? *FString::Printf(TEXT("No income: you earn from tolls, sales, and $%d per point for each Route card you finish."), Tuning.CashPerCardPoint) : TEXT("You collect tolls from your properties (there's no other income)."),
			Tuning.DrawCount, Tuning.MinKeep), false },
		{ FString::Printf(TEXT("Then ONE main action: buy %d tickets ($%d each) OR 1 property card ($%d), drawn blind,"),
			Tuning.MaxTicketsPerTurn, Tuning.TicketCost, Tuning.PropertyCardCost), false },
		{ Tuning.bRoutesMustConnect
			? FString(TEXT("   OR build a route next to yours (one ticket of its color per pin; gray: any one color; Wild: any), OR remove an opponent property."))
			: FString(TEXT("   OR build any open route: click it, then choose the tickets (its color, or any ONE color on gray, plus Wilds if you like). OR remove a property.")), false },
		{ FString::Printf(TEXT("Then, if you like: play a Lot card + $%d on an empty slot in range, or the next tier's card to upgrade (this ends the turn)."),
			Tuning.LotCost), false },
		{ FString::Printf(TEXT("You may also sell one property a turn, for %d%% of what you spent on it. Finished Route cards score at the end of your turn."),
			FMath::RoundToInt(100.0f * Tuning.SellFraction)), false },
	};
	const TArray<FHelpLine> PhaseTurn = {
		{ TEXT("EACH TURN (the banner at the top always says what to do)"), true },
		{ Tuning.bCardEconomy
			? FString::Printf(TEXT("1. Start: you get $%d. Buy tickets blind ($%d each, 1 or 2 at a time, as many as you like) and 1 property card ($%d). Or draw %d Route cards."),
				Tuning.BaseIncome, Tuning.TicketCost, Tuning.PropertyCardCost, Tuning.DrawCount)
			: FString::Printf(TEXT("1. Start: you get $%d. Then draw Route cards if you want%s."), Tuning.BaseIncome,
				Tuning.bDrawReplacesBuild ? TEXT(", but you can't claim a route on a turn you draw") : TEXT("")), false },
		{ TEXT("2. Resolve: remove one opponent property in your range. Buy out = pay its owner. Demolish = pay the bank double."), false },
		{ Tuning.bCardEconomy
			? FString(TEXT("3. Build: claim one route touching your routes by spending one ticket of its color per pin (gray: any one color; Wild: any)."))
			: FString::Printf(TEXT("3. Build: claim one route that touches your routes (or your start city). It costs $%d per dot."), Tuning.CostPerLengthDot), false },
		{ Tuning.bCardEconomy
			? FString::Printf(TEXT("4. Buy/Upgrade: play a Lot card + $%d on an empty slot in range, or the next tier's card to upgrade one of yours."), Tuning.LotCost)
			: FString::Printf(TEXT("4. Buy/Upgrade: place a Lot ($%d) on an empty slot in your range, or upgrade one of yours. Upgrades are ready next turn."), Tuning.LotCost), false },
		{ TEXT("5. Score: finished Route cards score by themselves at the end of your turn."), false },
	};
	if (Tuning.bClassicTurn)
	{
		Lines = {
			{ TEXT("GOAL"), true },
			{ TEXT("Score the most points. Claim routes for points, and join the two cities on each of your Route cards."), false },
			{ FString::Printf(TEXT("Route points by length: 1 2 4 7 10 15 18 21 27 (1 to 9 trains). Longest continuous route: +%d at the end."), Tuning.LongestRouteBonus), false },
			{ TEXT("At the end, each finished Route card adds its points and each unfinished one SUBTRACTS them. Your cards' cities have gold rings."), false },
			{ TEXT("EACH TURN: do exactly ONE of these"), true },
			{ Tuning.TicketCost > 0
				? FString::Printf(TEXT("1. Buy 2 tickets, $%d each: from the 5 face-up ones (click, or keys 1-5) or from the deck (T). A face-up Wild counts as both (and costs $%d)."),
					Tuning.TicketCost, Tuning.GetFaceUpPrice(EGridlockColor::Wild))
				: FString(TEXT("1. Take 2 tickets: from the 5 face-up ones (click, or keys 1-5) or from the deck (T). A face-up Wild counts as both.")), false },
			{ TEXT("2. Claim a route: click it, choose the tickets (its color, any ONE color on gray, Wilds stand in for any), then Build."), false },
			{ FString::Printf(TEXT("3. Draw %d Route cards (D) and keep at least %d%s."), Tuning.DrawCount, Tuning.MinKeep,
				Tuning.bDrawOnlyWhenCardsDone ? TEXT(": only once all of yours are finished") : TEXT("")), false },
			{ Tuning.TicketCost > 0 && Tuning.bUnlimitedCash ? FString(TEXT("   Money: unlimited for now. The panel shows how much each player has spent."))
				: Tuning.TicketCost > 0 ? FString::Printf(TEXT("   Money: you start with $%d, and there's no other income yet."), Tuning.StartCashFirstPlayer)
				: FString(), false },
			{ Tuning.RouteLotCards > 0 ? FString(TEXT("LOTS")) : FString(), true },
			{ Tuning.RouteLotCards > 0 ? FString::Printf(TEXT("Lot cards are in the ticket deck. Face-up a Lot costs $%d and is your whole turn; from the deck pay $%d, then keep it for $%d more or discard it."),
				Tuning.RouteLotPrice, Tuning.TicketCost, FMath::Max(0, Tuning.RouteLotPrice - Tuning.TicketCost)) : FString(), false },
			{ Tuning.RouteLotCards > 0 ? FString(TEXT("Placing a Lot ends your turn (one a turn): click a route, then Place a Lot. Routes have 1-3 lot slots by length; own them all and only you can build it."))
				: FString(), false },
			{ FString::Printf(TEXT("   If %d of the face-up cards are Wilds or Lots (any mix), all 5 are shuffled back into the deck and 5 new are dealt."), Tuning.FaceUpWildReset), false },
			{ Tuning.CityPropertyPrice > 0 ? FString(TEXT("CITY PROPERTIES")) : FString(), true },
			{ Tuning.CityPropertyPrice > 0 ? FString::Printf(TEXT("Click a city your routes reach to buy its property: $%d, and it's your turn. Each owned city has a ring in its owner's color."),
				Tuning.CityPropertyPrice) : FString(), false },
			{ Tuning.CityPropertyPrice > 0 ? FString::Printf(TEXT("The other player pays $%d each time they build a route into it, and again at the end for each finished Route card whose path visits it."),
				Tuning.CityToll) : FString(), false },
			{ TEXT("THE END"), true },
			{ FString::Printf(TEXT("Each player has %d trains, one per pin. When someone ends a turn with %d or fewer, everyone gets one last turn."),
				Tuning.TrainsPerPlayer, Tuning.FinalRoundTrains), false },
			{ TEXT("CONTROLS"), true },
			{ TEXT("Hover over a route to see its color, length and points. Thicker routes are ones your tickets can already pay for."), false },
			{ TEXT("Your Route cards are the tiles under the banner: hover one to see its two cities and a suggested path on the board."), false },
			{ TEXT("Your tickets are the colored cards at the bottom left. Keys: 1-5 face-up, T deck, D Route cards, Z undo, F1 help."), false },
			{ TEXT("The AI's moves are listed in the log and flash on the board."), false },
		};
	}
	else
	{
	Lines.Append(Tuning.bOneMainActionPerTurn ? OneActionTurn : PhaseTurn);
	Lines.Append({
		{ TEXT("PROPERTIES (each route has one slot, the square in its middle)"), true },
		{ FString::Printf(TEXT("Lot: toll $%d a turn from the route's owner.  Building: blocks open routes, toll $%d."), Tuning.TollLot, Tuning.TollBuilding), false },
		{ FString::Printf(TEXT("Complex: cuts an opponent's route, toll $%d.  Landmark: cuts it too and scores %d point a turn."), Tuning.TollSevered, Tuning.LandmarkPointsPerTurn), false },
		{ TEXT("Range: you can only use slots on routes that touch a city your routes touch."), false },
		{ TEXT("CONTROLS"), true },
		{ TEXT("Click anything glowing gold. Hover over a route to see what a click would do and what it costs."), false },
		{ TEXT("Buttons are at the bottom right. Keys: Space next step, T buy 1 ticket, Y buy 2, P buy a property card, S sell, Z undo, F1 help."), false },
		{ TEXT("Your Route cards are the tiles under the banner: hover one to see its two cities and a suggested path on the board."), false },
		{ TEXT("Your tickets are the colored cards at the bottom left. Routes they already pay for are drawn brighter before you build."), false },
		{ TEXT("Steps where you have nothing to do are skipped for you. The AI's moves are listed in the log and flash on the board."), false },
	});
	}

	UFont* Heading = GEngine->GetLargeFont();
	UFont* Body = GEngine->GetMediumFont();
	const float Left = FMath::Max(24.0f, Canvas->ClipX * 0.5f - 470.0f);
	float Y = 28.0f;
	DrawTextShadowed(TEXT("How to play Gridlock"), GridlockBoard::Clickable, Left, Y, Heading, 1.2f);
	Y += 46.0f;
	for (const FHelpLine& Line : Lines)
	{
		if (Line.bHeading)
		{
			Y += 8.0f;
			DrawTextShadowed(Line.Text, GridlockBoard::Clickable, Left, Y, Body);
			Y += 24.0f;
		}
		else
		{
			DrawTextShadowed(Line.Text, GridlockHUDPrivate::Plain, Left + 12.0f, Y, Body);
			Y += 22.0f;
		}
	}
	Y += 14.0f;
	DrawTextShadowed(TEXT("Click anywhere or press any key to start."), GridlockHUDPrivate::NoticeColor, Left, Y, Heading);
}

void AGridlockHUD::DrawButton(const FDrawFrame& Frame, FName Id, const FString& Label, float X, float Y, float W, float H, bool bEnabled, bool bPrimary)
{
	const bool bHover = bEnabled && Frame.Mouse.X >= X && Frame.Mouse.X <= X + W && Frame.Mouse.Y >= Y && Frame.Mouse.Y <= Y + H;
	FLinearColor Fill = !bEnabled ? FLinearColor(0.12f, 0.12f, 0.12f, 0.9f)
		: bPrimary ? FLinearColor(0.85f, 0.62f, 0.06f, 0.95f)
		: FLinearColor(0.22f, 0.24f, 0.3f, 0.95f);
	if (bHover)
	{
		Fill = FMath::Lerp(Fill, FLinearColor::White, 0.2f);
	}
	DrawRect(Fill, X, Y, W, H);
	DrawOutline(X, Y, W, H, bEnabled ? FLinearColor(1.0f, 1.0f, 1.0f, 0.35f) : FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));

	UFont* Font = GEngine->GetMediumFont();
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Label, TextWidth, TextHeight, Font);
	const float Scale = TextWidth > W - 12.0f ? (W - 12.0f) / TextWidth : 1.0f;
	const FLinearColor TextColor = !bEnabled ? GridlockHUDPrivate::Quiet : bPrimary ? FLinearColor::Black : FLinearColor::White;
	DrawText(Label, TextColor, X + (W - TextWidth * Scale) * 0.5f, Y + (H - TextHeight * Scale) * 0.5f, Font, Scale);

	if (bEnabled)
	{
		Buttons.Add({ Id, FVector2D(X, Y), FVector2D(X + W, Y + H) });
	}
}

void AGridlockHUD::DrawFaceUpCard(const FDrawFrame& Frame, int32 Slot, EGridlockColor Color, float X, float Y, float W, float H, bool bEnabled)
{
	const bool bHover = bEnabled && Frame.Mouse.X >= X && Frame.Mouse.X <= X + W && Frame.Mouse.Y >= Y && Frame.Mouse.Y <= Y + H;
	const FLinearColor Base = GridlockBoard::TicketColor(Color);
	FLinearColor Fill = Base;
	Fill.A = bEnabled ? 1.0f : 0.3f;
	DrawRect(Fill, X, Y, W, H);
	DrawOutline(X, Y, W, H, bHover ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.45f), bHover ? 3.0f : 1.0f);

	UFont* Font = GEngine->GetSmallFont();
	const int32 Price = Frame.Context->Tuning->GetFaceUpPrice(Color);
	const FString Label = Price != Frame.Context->Tuning->TicketCost
		? FString::Printf(TEXT("%s $%d"), *GridlockBoard::TicketColorName(Color), Price)
		: FString::Printf(TEXT("%s %d"), *GridlockBoard::TicketColorName(Color), Slot + 1);
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Label, TextWidth, TextHeight, Font);
	const bool bLight = 0.3f * Base.R + 0.6f * Base.G + 0.1f * Base.B > 0.45f;
	FLinearColor TextColor = bLight ? FLinearColor::Black : FLinearColor::White;
	TextColor.A = bEnabled ? 1.0f : 0.5f;
	DrawText(Label, TextColor, X + (W - TextWidth) * 0.5f, Y + (H - TextHeight) * 0.5f, Font);
	if (bEnabled)
	{
		Buttons.Add({ FName(*FString::Printf(TEXT("FaceUp%d"), Slot)), FVector2D(X, Y), FVector2D(X + W, Y + H) });
	}
}

float AGridlockHUD::DrawCardChip(const FString& Label, const FLinearColor& Color, float X, float Y, bool bHeld)
{
	UFont* Font = GEngine->GetSmallFont();
	float TextWidth = 0.0f;
	float TextHeight = 0.0f;
	GetTextSize(Label, TextWidth, TextHeight, Font);
	const float Width = TextWidth + 16.0f;
	const float Height = 24.0f;

	FLinearColor Fill = Color;
	Fill.A = bHeld ? 0.95f : 0.22f;
	DrawRect(Fill, X, Y, Width, Height);
	DrawOutline(X, Y, Width, Height, FLinearColor(1.0f, 1.0f, 1.0f, bHeld ? 0.5f : 0.15f));

	// Dark text on light cards, white on dark ones.
	const bool bLightCard = bHeld && 0.3f * Color.R + 0.6f * Color.G + 0.1f * Color.B > 0.45f;
	FLinearColor TextColor = bLightCard ? FLinearColor::Black : FLinearColor::White;
	TextColor.A = bHeld ? 1.0f : 0.45f;
	DrawText(Label, TextColor, X + 8.0f, Y + (Height - TextHeight) * 0.5f, Font);
	return Width;
}

void AGridlockHUD::DrawTextShadowed(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale)
{
	DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f * Color.A), X + 1.0f, Y + 1.0f, Font, Scale);
	DrawText(Text, Color, X, Y, Font, Scale);
}

void AGridlockHUD::DrawRing(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness)
{
	constexpr int32 Sides = 20;
	for (int32 Side = 0; Side < Sides; ++Side)
	{
		const float A = 2.0f * PI * Side / Sides;
		const float B = 2.0f * PI * (Side + 1) / Sides;
		DrawLine(Center.X + Radius * FMath::Cos(A), Center.Y + Radius * FMath::Sin(A),
			Center.X + Radius * FMath::Cos(B), Center.Y + Radius * FMath::Sin(B), Color, Thickness);
	}
}

void AGridlockHUD::DrawOutline(float X, float Y, float W, float H, const FLinearColor& Color, float Thickness)
{
	DrawLine(X, Y, X + W, Y, Color, Thickness);
	DrawLine(X + W, Y, X + W, Y + H, Color, Thickness);
	DrawLine(X + W, Y + H, X, Y + H, Color, Thickness);
	DrawLine(X, Y + H, X, Y, Color, Thickness);
}

FString AGridlockHUD::PlayerLabel(const FDrawFrame& Frame, int32 Player) const
{
	return GridlockHUDPrivate::LabelFor(Frame.AI, Player);
}

FString AGridlockHUD::RouteName(const FRulesContext& Context, FName RouteId) const
{
	const FRouteDef* Route = Context.Map->FindRoute(RouteId);
	return Route ? FString::Printf(TEXT("%s - %s"), *Route->CityA.ToString(), *Route->CityB.ToString()) : RouteId.ToString();
}

FString AGridlockHUD::FormatEvent(const FGameEvent& Event, const FRulesContext& Context, const UAIPlayer* AI) const
{
	const FString Who = GridlockHUDPrivate::LabelFor(AI, Event.Player);
	const FString Route = RouteName(Context, Event.RouteId);
	switch (Event.Type)
	{
	case EGridlockEventType::TurnStarted:          return FString::Printf(TEXT("-- Round %d: %s's turn --"), Event.Amount, *Who);
	case EGridlockEventType::RouteClaimed:
	{
		const FRouteDef* Claimed = Context.Map->FindRoute(Event.RouteId);
		return Context.Tuning->bCardEconomy && Claimed && Context.Tuning->GetRoutePoints(Claimed->Length) > 0
			? FString::Printf(TEXT("%s claimed %s (%d trains, +%d points)"), *Who, *Route, Claimed->Length, Context.Tuning->GetRoutePoints(Claimed->Length))
			: Context.Tuning->bCardEconomy && Claimed
			? FString::Printf(TEXT("%s claimed %s (%d tickets)"), *Who, *Route, Claimed->Length)
			: FString::Printf(TEXT("%s claimed %s ($%d)"), *Who, *Route, Event.Amount);
	}
	case EGridlockEventType::PropertyPlaced:       return FString::Printf(TEXT("%s placed a Lot on %s"), *Who, *Route);
	case EGridlockEventType::UpgradeStarted:       return FString::Printf(TEXT("%s started a %s on %s (ready next turn)"), *Who, GridlockHUDPrivate::TierName(Event.Amount), *Route);
	case EGridlockEventType::UpgradeFinished:      return FString::Printf(TEXT("%s's %s on %s is ready"), *Who, GridlockHUDPrivate::TierName(Event.Amount), *Route);
	case EGridlockEventType::PropertyRemoved:      return FString::Printf(TEXT("%s bought out the property on %s ($%d)"), *Who, *Route, Event.Amount);
	case EGridlockEventType::PropertyDemolished:   return FString::Printf(TEXT("%s demolished the property on %s ($%d)"), *Who, *Route, Event.Amount);
	case EGridlockEventType::TollPaid:             return FString::Printf(TEXT("%s paid a $%d toll on %s"), *Who, Event.Amount, *Route);
	case EGridlockEventType::CardsKept:            return FString::Printf(TEXT("%s kept %d Route card%s"), *Who, Event.Amount, Event.Amount == 1 ? TEXT("") : TEXT("s"));
	case EGridlockEventType::CardScored:           return FString::Printf(TEXT("%s completed %s (+%d points)"), *Who, *GridlockHUDPrivate::CardName(Context, Event.CardId), Event.Amount);
	case EGridlockEventType::LandmarkPointsScored: return FString::Printf(TEXT("%s earned %d point%s from Landmarks"), *Who, Event.Amount, Event.Amount == 1 ? TEXT("") : TEXT("s"));
	case EGridlockEventType::GameOver:             return Event.Player == INDEX_NONE ? FString(TEXT("Game over: a draw")) : FString::Printf(TEXT("Game over: %s wins"), *Who);
	case EGridlockEventType::TicketBought:
	{
		const TCHAR* Verb = Context.Tuning->TicketCost > 0 ? TEXT("bought") : TEXT("took");
		return AI && Event.Player == AI->PlayerIndex ? FString::Printf(TEXT("%s %s a ticket from the deck"), *Who, Verb)
			: FString::Printf(TEXT("%s %s a %s ticket from the deck"), *Who, Verb, *GridlockBoard::TicketColorName(static_cast<EGridlockColor>(Event.Amount)));
	}
	case EGridlockEventType::PropertyCardBought:   return AI && Event.Player == AI->PlayerIndex ? FString::Printf(TEXT("%s bought a property card"), *Who)
		: FString::Printf(TEXT("%s bought a %s card"), *Who, GridlockHUDPrivate::TierName(Event.Amount));
	case EGridlockEventType::FaceUpTaken:          return FString::Printf(TEXT("%s %s a face-up %s ticket"), *Who, Context.Tuning->TicketCost > 0 ? TEXT("bought") : TEXT("took"),
		*GridlockBoard::TicketColorName(static_cast<EGridlockColor>(Event.Amount)));
	case EGridlockEventType::FaceUpReset:          return FString::Printf(TEXT("%d Wilds/Lots face up: shuffled back into the deck, 5 new dealt"), Event.Amount);
	case EGridlockEventType::CardFailed:           return FString::Printf(TEXT("%s didn't finish %s (%d points)"), *Who, *GridlockHUDPrivate::CardName(Context, Event.CardId), Event.Amount);
	case EGridlockEventType::LongestRouteBonus:    return FString::Printf(TEXT("%s has the longest route (+%d points)"), *Who, Event.Amount);
	case EGridlockEventType::LotDrawn:             return FString::Printf(TEXT("%s drew a Lot from the deck"), *Who);
	case EGridlockEventType::LotKept:              return FString::Printf(TEXT("%s kept the Lot ($%d more)"), *Who, Event.Amount);
	case EGridlockEventType::LotDiscarded:         return FString::Printf(TEXT("%s discarded the Lot"), *Who);
	case EGridlockEventType::RouteLotPlaced:       return FString::Printf(TEXT("%s placed a Lot on %s"), *Who, *Route);
	case EGridlockEventType::RouteBlocked:         return FString::Printf(TEXT("%s owns every lot on %s: blocked for the other player"), *Who, *Route);
	case EGridlockEventType::CityPropertyBought:   return FString::Printf(TEXT("%s bought the property in %s ($%d)"), *Who, *Event.RouteId.ToString(), Event.Amount);
	case EGridlockEventType::CityTollPaid:
	{
		const FString Payee = GridlockHUDPrivate::LabelFor(AI, 1 - Event.Player);
		return Event.CardId.IsNone() ? FString::Printf(TEXT("%s paid %s a $%d toll for building into %s"), *Who, *Payee, Event.Amount, *Event.RouteId.ToString())
			: FString::Printf(TEXT("%s paid %s $%d: %s passes through %s"), *Who, *Payee, Event.Amount, *GridlockHUDPrivate::CardName(Context, Event.CardId), *Event.RouteId.ToString());
	}
	case EGridlockEventType::FinalRound:           return FString::Printf(TEXT("%s is down to %d trains: one last turn each!"), *Who, Event.Amount);
	case EGridlockEventType::CardPaidOut:          return FString::Printf(TEXT("%s was paid $%d for finishing it"), *Who, Event.Amount);
	case EGridlockEventType::PropertySold:         return FString::Printf(TEXT("%s sold the property on %s ($%d)"), *Who, *Route, Event.Amount);
	case EGridlockEventType::DeckReshuffled:       return Event.Amount == 0 ? FString(TEXT("The ticket discard pile was shuffled into a new deck"))
		: FString(TEXT("The property discard pile was shuffled into a new deck"));
	default:                                       return FString();
	}
}

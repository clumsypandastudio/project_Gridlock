#include "Game/GridlockPlayerController.h"
#include "Data/MapDefinition.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Game/GridlockGameMode.h"
#include "Game/GridlockHUD.h"
#include "Game/MatchSubsystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Model/RulesLibrary.h"
#include "View/PropertyActor.h"
#include "View/BoardLayout.h"
#include "View/RouteActor.h"

#define LOCTEXT_NAMESPACE "GridlockController"

AGridlockPlayerController::AGridlockPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	bAutoManageActiveCameraTarget = false; // the game mode points us at the board camera
}

void AGridlockPlayerController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
	AddMappingContext();
}

void AGridlockPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		return;
	}
	Input->BindAction(Actions[0], ETriggerEvent::Started, this, &ThisClass::HandleSelect);
	Input->BindAction(Actions[1], ETriggerEvent::Started, this, &ThisClass::HandleDemolish);
	Input->BindAction(Actions[2], ETriggerEvent::Started, this, &ThisClass::HandleEndPhase);
	Input->BindAction(Actions[3], ETriggerEvent::Started, this, &ThisClass::HandleDraw);
	Input->BindAction(Actions[4], ETriggerEvent::Started, this, &ThisClass::HandleEnter);
	Input->BindAction(Actions[5], ETriggerEvent::Started, this, &ThisClass::HandleUndo);
	Input->BindAction(Actions[6], ETriggerEvent::Started, this, &ThisClass::HandleToggle1);
	Input->BindAction(Actions[7], ETriggerEvent::Started, this, &ThisClass::HandleToggle2);
	Input->BindAction(Actions[8], ETriggerEvent::Started, this, &ThisClass::HandleToggle3);
	Input->BindAction(Actions[9], ETriggerEvent::Started, this, &ThisClass::HandleToggleHotSeat);
	Input->BindAction(Actions[10], ETriggerEvent::Started, this, &ThisClass::HandleHelp);
	Input->BindAction(Actions[11], ETriggerEvent::Started, this, &ThisClass::HandleToggle4);
	Input->BindAction(Actions[12], ETriggerEvent::Started, this, &ThisClass::HandleToggle5);
	Input->BindAction(Actions[13], ETriggerEvent::Started, this, &ThisClass::HandleBuyTicket);
	Input->BindAction(Actions[14], ETriggerEvent::Started, this, &ThisClass::HandleBuyPropertyCard);
	Input->BindAction(Actions[15], ETriggerEvent::Started, this, &ThisClass::HandleBuyTwoTickets);
	Input->BindAction(Actions[16], ETriggerEvent::Started, this, &ThisClass::HandleSellMode);
	Input->BindAction(Actions[17], ETriggerEvent::Started, this, &ThisClass::HandleCancel);
	AddMappingContext();
}

void AGridlockPlayerController::CreateInput()
{
	if (BoardContext)
	{
		return;
	}
	BoardContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Board_Runtime"));

	const TArray<FKey> Keys = {
		EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::SpaceBar, EKeys::D, EKeys::Enter,
		EKeys::Z, EKeys::One, EKeys::Two, EKeys::Three, EKeys::H, EKeys::F1, EKeys::Four, EKeys::Five, EKeys::T, EKeys::P, EKeys::Y, EKeys::S, EKeys::Escape };
	for (const FKey& Key : Keys)
	{
		UInputAction* Action = NewObject<UInputAction>(this);
		BoardContext->MapKey(Action, Key);
		Actions.Add(Action);
	}
}

void AGridlockPlayerController::AddMappingContext()
{
	ULocalPlayer* Local = GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Input = Local ? Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Input && BoardContext && !Input->HasMappingContext(BoardContext))
	{
		Input->AddMappingContext(BoardContext, 0);
	}
}

void AGridlockPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	const UMatchSubsystem* Match = GetMatch();
	if (!Match)
	{
		return;
	}
	const FBoardState State = Match->GetState();

	// New drawn cards start out all marked to keep.
	const int32 Acting = GetActingPlayer();
	const TArray<FName> Pending = State.Players.IsValidIndex(Acting) ? State.Players[Acting].PendingDraw : TArray<FName>();
	if (Pending != KeepSelectionFor)
	{
		KeepSelectionFor = Pending;
		KeepSelection.Init(true, Pending.Num());
	}

	// The buy out / demolish switch only means something in Resolve; it resets every turn. Sell mode
	// lasts while you can sell.
	if (State.Phase != EGridlockPhase::Resolve)
	{
		bDemolishMode = false;
	}
	const bool bCanSellNow = State.ActivePlayer == Acting && (State.Phase == EGridlockPhase::Start || State.Phase == EGridlockPhase::BuyUpgrade);
	if (!bCanSellNow)
	{
		bSellMode = false;
	}

	// The build window closes once building isn't an option any more (the turn moved on, an undo, the route went).
	const bool bLotPending = State.Players.IsValidIndex(Acting) && State.Players[Acting].bLotPending;
	const bool bCanBuildNow = State.ActivePlayer == Acting && Pending.IsEmpty() && !bLotPending && State.RouteOwner.FindRef(BuildRoute) == INDEX_NONE
		&& (State.Phase == EGridlockPhase::Build || (State.Phase == EGridlockPhase::Start
			&& (Match->GetContext().Tuning->bOneMainActionPerTurn || Match->GetContext().Tuning->bClassicTurn)));
	if (!bCanBuildNow)
	{
		BuildRoute = NAME_None;
	}
	if (State.ActivePlayer != Acting || State.Phase != EGridlockPhase::Start || !Pending.IsEmpty() || bLotPending)
	{
		CityChoice = NAME_None;
	}
}

UMatchSubsystem* AGridlockPlayerController::GetMatch() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UMatchSubsystem>() : nullptr;
}

AGridlockHUD* AGridlockPlayerController::GetGridlockHUD() const
{
	return Cast<AGridlockHUD>(GetHUD());
}

int32 AGridlockPlayerController::GetActingPlayer() const
{
	const AGridlockGameMode* GameMode = GetWorld()->GetAuthGameMode<AGridlockGameMode>();
	return GameMode ? GameMode->GetHumanPlayer() : 0;
}

double AGridlockPlayerController::GetMessageAge() const
{
	return GetWorld() ? GetWorld()->GetRealTimeSeconds() - MessageTime : 0.0;
}

void AGridlockPlayerController::SetMessage(const FText& InMessage)
{
	Message = InMessage;
	MessageTime = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
}

bool AGridlockPlayerController::SubmitAction(const FGameAction& Action, FText& OutReason)
{
	UMatchSubsystem* Match = GetMatch();
	if (!Match)
	{
		return false;
	}
	FGameAction Acting = Action;
	Acting.Player = GetActingPlayer();
	return Match->Submit(Acting, OutReason);
}

FName AGridlockPlayerController::GetHoveredRouteId() const
{
	FHitResult Hit;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, Hit))
	{
		return NAME_None;
	}
	if (const ARouteActor* Route = Cast<ARouteActor>(Hit.GetActor()))
	{
		return Route->RouteId;
	}
	if (const APropertyActor* Property = Cast<APropertyActor>(Hit.GetActor()))
	{
		return Property->RouteId;
	}
	return NAME_None;
}

void AGridlockPlayerController::HandleSelect()
{
	AGridlockHUD* HUD = GetGridlockHUD();
	if (HUD && HUD->IsHelpOpen())
	{
		HUD->SetHelpOpen(false); // any click closes the help screen
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (HUD && GetMousePosition(MouseX, MouseY))
	{
		const FName Button = HUD->GetButtonAt(FVector2D(MouseX, MouseY));
		if (!Button.IsNone())
		{
			PressButton(Button);
			return;
		}
		const FName City = HUD->GetCityAt(FVector2D(MouseX, MouseY));
		if (!City.IsNone())
		{
			ClickCity(City);
			return;
		}
	}
	ClickRoute(bDemolishMode);
}

void AGridlockPlayerController::HandleDemolish()
{
	ClickRoute(true);
}

void AGridlockPlayerController::PressButton(FName ButtonId)
{
	static const FName Next(TEXT("Next")), Draw(TEXT("Draw")), Keep(TEXT("Keep")), Undo(TEXT("Undo")), Help(TEXT("Help")),
		Mode(TEXT("Mode")), HotSeat(TEXT("HotSeat")), NewGame(TEXT("NewGame")), Card0(TEXT("Card0")), Card1(TEXT("Card1")), Card2(TEXT("Card2")),
		Card3(TEXT("Card3")), Card4(TEXT("Card4")), BuyTicket(TEXT("BuyTicket")), BuyTwo(TEXT("BuyTwo")), BuyProperty(TEXT("BuyProperty")),
		Sell(TEXT("Sell")), BuildConfirm(TEXT("BuildConfirm")), BuildCancel(TEXT("BuildCancel")),
		WildLess(TEXT("WildLess")), WildMore(TEXT("WildMore"));

	if (ButtonId == Next)          { HandleEndPhase(); }
	else if (ButtonId == Draw)     { HandleDraw(); }
	else if (ButtonId == Keep)     { HandleKeep(); }
	else if (ButtonId == Undo)     { HandleUndo(); }
	else if (ButtonId == Help)     { HandleHelp(); }
	else if (ButtonId == HotSeat)  { HandleToggleHotSeat(); }
	else if (ButtonId == Card0)    { ToggleKeep(0); }
	else if (ButtonId == Card1)    { ToggleKeep(1); }
	else if (ButtonId == Card2)    { ToggleKeep(2); }
	else if (ButtonId == Card3)    { ToggleKeep(3); }
	else if (ButtonId == Card4)    { ToggleKeep(4); }
	else if (ButtonId == BuyTicket)   { HandleBuyTicket(); }
	else if (ButtonId == BuyTwo)      { HandleBuyTwoTickets(); }
	else if (ButtonId == BuyProperty) { HandleBuyPropertyCard(); }
	else if (ButtonId == Sell)        { HandleSellMode(); }
	else if (ButtonId == BuildConfirm) { ConfirmBuild(); }
	else if (ButtonId == TEXT("KeepLot"))      { Send(EGridlockActionType::KeepLot); }
	else if (ButtonId == TEXT("DiscardLot"))   { Send(EGridlockActionType::DiscardLot); }
	else if (ButtonId == TEXT("PlaceLotHere")) { Send(EGridlockActionType::PlaceRouteLot, BuildRoute); }
	else if (ButtonId == TEXT("BuyCity"))      { const FName City = CityChoice; CityChoice = NAME_None; Send(EGridlockActionType::BuyCityProperty, City); }
	else if (ButtonId == TEXT("CityCancel"))   { CityChoice = NAME_None; }
	else if (ButtonId == TEXT("ResultsOpen") || ButtonId == TEXT("ResultsClose"))
	{
		if (AGridlockHUD* HUD = GetGridlockHUD())
		{
			HUD->SetResultsOpen(ButtonId == TEXT("ResultsOpen"));
		}
	}
	else if (ButtonId == BuildCancel)  { HandleCancel(); }
	else if (ButtonId == WildLess)     { ChangeBuildWilds(-1); }
	else if (ButtonId == WildMore)     { ChangeBuildWilds(1); }
	else if (ButtonId.ToString().StartsWith(TEXT("FaceUp")))
	{
		TakeFaceUp(FCString::Atoi(*ButtonId.ToString().RightChop(6)));
	}
	else if (ButtonId.ToString().StartsWith(TEXT("PayColor")))
	{
		SetBuildColor(static_cast<EGridlockColor>(FCString::Atoi(*ButtonId.ToString().RightChop(8))));
	}
	else if (ButtonId == Mode)
	{
		bDemolishMode = !bDemolishMode;
		SetMessage(bDemolishMode
			? LOCTEXT("DemolishMode", "Demolish: pay the bank double, and the owner gets nothing.")
			: LOCTEXT("BuyoutMode", "Buy out: pay the owner the property's value."));
	}
	else if (ButtonId == NewGame)
	{
		if (AGridlockGameMode* GameMode = GetWorld()->GetAuthGameMode<AGridlockGameMode>())
		{
			GameMode->NewGame();
			SetMessage(FText::GetEmpty());
		}
	}
}

void AGridlockPlayerController::ClickRoute(bool bDemolish)
{
	const UMatchSubsystem* Match = GetMatch();
	const FName RouteId = GetHoveredRouteId();
	if (!Match || RouteId.IsNone())
	{
		return;
	}

	const FBoardState State = Match->GetState();
	if (State.ActivePlayer != GetActingPlayer() && State.Phase != EGridlockPhase::GameOver)
	{
		SetMessage(LOCTEXT("WaitForAI", "It's the AI's turn: watch the log, and your turn comes next."));
		return;
	}

	if (bSellMode)
	{
		Send(EGridlockActionType::SellProperty, RouteId);
		bSellMode = false;
		return;
	}

	switch (State.Phase)
	{
	case EGridlockPhase::Resolve:
		Send(bDemolish ? EGridlockActionType::Demolish : EGridlockActionType::Buyout, RouteId);
		break;
	case EGridlockPhase::Build:
		OpenBuild(RouteId);
		break;
	case EGridlockPhase::BuyUpgrade:
	{
		const FSlotState* Slot = State.Slots.Find(RouteId);
		const bool bMine = Slot && Slot->Owner == GetActingPlayer();
		Send(bMine ? EGridlockActionType::Upgrade : EGridlockActionType::PlaceLot, RouteId);
		break;
	}
	case EGridlockPhase::Start:
		if (Match->GetContext().Tuning->bOneMainActionPerTurn || Match->GetContext().Tuning->bClassicTurn)
		{
			OpenBuild(RouteId);
		}
		else
		{
			SetMessage(LOCTEXT("StartHint", "Start of your turn: buy cards or draw Route cards with the buttons, or press Done."));
		}
		break;
	default:
		break;
	}
}

void AGridlockPlayerController::ClaimFromStart(const FGameAction& Claim)
{
	UMatchSubsystem* Match = GetMatch();
	FText Reason;
	if (!URulesLibrary::CanClaimLater(Match->GetState(), Match->GetContext(), Claim.RouteId, Reason, Claim.Color, Claim.Wilds))
	{
		SetMessage(Reason);
		return;
	}
	// Step through Start and Resolve, then claim. Undo steps back one at a time.
	for (int32 Guard = 0; Guard < 3 && Match->GetState().Phase != EGridlockPhase::Build; ++Guard)
	{
		FGameAction End;
		End.Type = EGridlockActionType::EndPhase;
		if (!SubmitAction(End, Reason))
		{
			SetMessage(Reason);
			return;
		}
	}
	const int32 Cost = URulesLibrary::PreviewCost(Match->GetState(), Claim, Match->GetContext());
	SetMessage(SubmitAction(Claim, Reason) ? Describe(Claim, Cost) : Reason);
}

FGameAction AGridlockPlayerController::MakeBuildAction() const
{
	FGameAction Claim;
	Claim.Type = EGridlockActionType::ClaimRoute;
	Claim.Player = GetActingPlayer();
	Claim.RouteId = BuildRoute;
	Claim.Color = BuildColor;
	Claim.Wilds = BuildWilds;
	return Claim;
}

void AGridlockPlayerController::OpenBuild(FName RouteId)
{
	const UMatchSubsystem* Match = GetMatch();
	const FRouteDef* Route = Match ? Match->GetContext().Map->FindRoute(RouteId) : nullptr;
	const FBoardState State = Match ? Match->GetState() : FBoardState();
	if (!Route || !State.Players.IsValidIndex(GetActingPlayer()))
	{
		return;
	}
	FText Reason;
	if (State.RouteOwner.FindRef(RouteId) != INDEX_NONE)
	{
		SetMessage(LOCTEXT("AlreadyClaimedClick", "That route is already claimed."));
		return;
	}
	if (State.Phase == EGridlockPhase::Start && State.ActionsThisTurn > 0 && !Match->GetContext().Tuning->bClassicTurn)
	{
		URulesLibrary::CanClaimLater(State, Match->GetContext(), RouteId, Reason);
		SetMessage(Reason);
		return;
	}

	// Start from the route's color (gray: the color you hold most of) and only the Wilds you need.
	const FPlayerData& Me = State.Players[GetActingPlayer()];
	BuildRoute = RouteId;
	CityChoice = NAME_None;
	BuildColor = Route->Color;
	if (BuildColor == EGridlockColor::Gray)
	{
		BuildColor = EGridlockColor::Red;
		for (uint8 Color = static_cast<uint8>(EGridlockColor::Red); Color <= static_cast<uint8>(EGridlockColor::White); ++Color)
		{
			BuildColor = Me.TicketsOf(static_cast<EGridlockColor>(Color)) > Me.TicketsOf(BuildColor) ? static_cast<EGridlockColor>(Color) : BuildColor;
		}
	}
	BuildWilds = FMath::Clamp(Route->Length - Me.TicketsOf(BuildColor), 0, Route->Length);
	SetMessage(FText::GetEmpty());
}

void AGridlockPlayerController::SetBuildColor(EGridlockColor Color)
{
	const UMatchSubsystem* Match = GetMatch();
	const FRouteDef* Route = Match ? Match->GetContext().Map->FindRoute(BuildRoute) : nullptr;
	if (!Route || Route->Color != EGridlockColor::Gray || Color == EGridlockColor::Gray || Color == EGridlockColor::Wild)
	{
		return; // only a gray route lets you pick the color
	}
	BuildColor = Color;
	const FBoardState State = Match->GetState();
	BuildWilds = FMath::Clamp(Route->Length - State.Players[GetActingPlayer()].TicketsOf(BuildColor), 0, Route->Length);
}

void AGridlockPlayerController::ChangeBuildWilds(int32 Delta)
{
	const UMatchSubsystem* Match = GetMatch();
	const FRouteDef* Route = Match ? Match->GetContext().Map->FindRoute(BuildRoute) : nullptr;
	if (Route)
	{
		BuildWilds = FMath::Clamp(BuildWilds + Delta, 0, Route->Length);
	}
}

void AGridlockPlayerController::ConfirmBuild()
{
	const UMatchSubsystem* Match = GetMatch();
	if (!Match || BuildRoute.IsNone())
	{
		return;
	}
	const FGameAction Claim = MakeBuildAction();
	BuildRoute = NAME_None;
	if (Match->GetState().Phase == EGridlockPhase::Start && !Match->GetContext().Tuning->bClassicTurn)
	{
		ClaimFromStart(Claim);
		return;
	}
	FText Reason;
	const int32 Cost = URulesLibrary::PreviewCost(Match->GetState(), Claim, Match->GetContext());
	SetMessage(SubmitAction(Claim, Reason) ? Describe(Claim, Cost) : Reason);
}

void AGridlockPlayerController::HandleEnter()
{
	const UMatchSubsystem* Match = GetMatch();
	if (Match && Match->GetState().Players.IsValidIndex(GetActingPlayer()) && Match->GetState().Players[GetActingPlayer()].bLotPending)
	{
		Send(EGridlockActionType::KeepLot);
	}
	else if (!CityChoice.IsNone())
	{
		const FName City = CityChoice;
		CityChoice = NAME_None;
		Send(EGridlockActionType::BuyCityProperty, City);
	}
	else if (!BuildRoute.IsNone())
	{
		ConfirmBuild();
	}
	else
	{
		HandleKeep();
	}
}

void AGridlockPlayerController::HandleCancel()
{
	AGridlockHUD* HUD = GetGridlockHUD();
	if (HUD && HUD->IsResultsOpen())
	{
		HUD->SetResultsOpen(false);
		return;
	}
	const UMatchSubsystem* Match = GetMatch();
	if (Match && Match->GetState().Players.IsValidIndex(GetActingPlayer()) && Match->GetState().Players[GetActingPlayer()].bLotPending)
	{
		Send(EGridlockActionType::DiscardLot);
		return;
	}
	BuildRoute = NAME_None;
	CityChoice = NAME_None;
	bSellMode = false;
}

void AGridlockPlayerController::ClickCity(FName CityId)
{
	const UMatchSubsystem* Match = GetMatch();
	if (!Match || Match->GetContext().Tuning->CityPropertyPrice <= 0)
	{
		return;
	}
	const FBoardState State = Match->GetState();
	if (State.ActivePlayer != GetActingPlayer() && State.Phase != EGridlockPhase::GameOver)
	{
		SetMessage(LOCTEXT("WaitForAICity", "It's the AI's turn: watch the log, and your turn comes next."));
		return;
	}
	BuildRoute = NAME_None;
	CityChoice = CityId;
}

void AGridlockPlayerController::HandleSellMode()
{
	const UMatchSubsystem* Match = GetMatch();
	if (!Match || !Match->GetContext().Tuning->bCanSellProperties)
	{
		return;
	}
	bSellMode = !bSellMode;
	SetMessage(bSellMode
		? FText::Format(LOCTEXT("SellModeOn", "Sell: click one of your properties. The bank pays {0}% of what you spent on it."),
			FMath::RoundToInt(100.0f * Match->GetContext().Tuning->SellFraction))
		: LOCTEXT("SellModeOff", "Sell mode off."));
}

void AGridlockPlayerController::HandleEndPhase()
{
	Send(EGridlockActionType::EndPhase);
}

void AGridlockPlayerController::HandleDraw()
{
	Send(EGridlockActionType::DrawCards);
}

void AGridlockPlayerController::HandleBuyTicket()
{
	Send(EGridlockActionType::BuyTicket);
}

void AGridlockPlayerController::HandleBuyTwoTickets()
{
	// Two single buys: the second is refused (with its reason) if the first used the last of the cash.
	Send(EGridlockActionType::BuyTicket);
	Send(EGridlockActionType::BuyTicket);
}

void AGridlockPlayerController::HandleBuyPropertyCard()
{
	Send(EGridlockActionType::BuyPropertyCard);
}

void AGridlockPlayerController::HandleKeep()
{
	FGameAction Action;
	Action.Type = EGridlockActionType::KeepCards;
	for (int32 Index = 0; Index < KeepSelectionFor.Num(); ++Index)
	{
		if (KeepSelection.IsValidIndex(Index) && KeepSelection[Index])
		{
			Action.CardIds.Add(KeepSelectionFor[Index]);
		}
	}
	FText Reason;
	SetMessage(SubmitAction(Action, Reason) ? FText::GetEmpty() : Reason);
}

void AGridlockPlayerController::HandleUndo()
{
	UMatchSubsystem* Match = GetMatch();
	if (!Match || !Match->Undo())
	{
		SetMessage(LOCTEXT("NothingToUndo", "Nothing to undo."));
		return;
	}

	// Against the AI, keep stepping back past its moves to your own last decision.
	const AGridlockGameMode* GameMode = GetWorld()->GetAuthGameMode<AGridlockGameMode>();
	if (GameMode && !GameMode->bHotSeat)
	{
		while (Match->GetState().ActivePlayer != GetActingPlayer() && Match->Undo())
		{
		}
	}
	SetMessage(LOCTEXT("Undone", "Undid your last action."));
}

void AGridlockPlayerController::HandleToggleHotSeat()
{
	AGridlockGameMode* GameMode = GetWorld()->GetAuthGameMode<AGridlockGameMode>();
	if (!GameMode)
	{
		return;
	}
	GameMode->SetHotSeat(!GameMode->bHotSeat);
	SetMessage(GameMode->bHotSeat
		? LOCTEXT("HotSeatOn", "Hot-seat: you play both sides.")
		: LOCTEXT("HotSeatOff", "You play Cyan; the AI plays Magenta."));
}

void AGridlockPlayerController::HandleHelp()
{
	if (AGridlockHUD* HUD = GetGridlockHUD())
	{
		HUD->SetHelpOpen(!HUD->IsHelpOpen());
	}
}

void AGridlockPlayerController::TakeFaceUp(int32 Slot)
{
	UMatchSubsystem* Match = GetMatch();
	AGridlockHUD* HUD = GetGridlockHUD();
	if (HUD && HUD->IsHelpOpen())
	{
		HUD->SetHelpOpen(false);
		return;
	}
	if (!Match || !Match->GetState().FaceUp.IsValidIndex(Slot))
	{
		return;
	}
	const EGridlockColor Color = Match->GetState().FaceUp[Slot];
	FGameAction Take;
	Take.Type = EGridlockActionType::TakeFaceUp;
	Take.Slot = Slot;
	FText Reason;
	const int32 Cost = URulesLibrary::PreviewCost(Match->GetState(), Take, Match->GetContext());
	SetMessage(SubmitAction(Take, Reason)
		? (Cost > 0 ? FText::Format(LOCTEXT("BoughtFaceUp", "Bought the face-up {0} ticket for ${1}."), FText::FromString(GridlockBoard::TicketColorName(Color)), Cost)
			: FText::Format(LOCTEXT("TookFaceUp", "Took the face-up {0} ticket."), FText::FromString(GridlockBoard::TicketColorName(Color))))
		: Reason);
}

void AGridlockPlayerController::ToggleKeep(int32 Index)
{
	// Number keys pick drawn Route cards while choosing them, and face-up tickets otherwise.
	if (KeepSelectionFor.IsEmpty())
	{
		const UMatchSubsystem* Match = GetMatch();
		if (Match && Match->GetContext().Tuning->bClassicTurn)
		{
			TakeFaceUp(Index);
		}
		return;
	}
	if (KeepSelection.IsValidIndex(Index))
	{
		KeepSelection[Index] = !KeepSelection[Index];
	}
}

void AGridlockPlayerController::Send(EGridlockActionType Type, FName RouteId)
{
	const UMatchSubsystem* Match = GetMatch();
	AGridlockHUD* HUD = GetGridlockHUD();
	if (HUD && HUD->IsHelpOpen())
	{
		HUD->SetHelpOpen(false); // the first key press just closes the help screen
		return;
	}
	if (!Match)
	{
		return;
	}
	FGameAction Action;
	Action.Type = Type;
	Action.Player = GetActingPlayer();
	Action.RouteId = RouteId;
	const int32 Cost = URulesLibrary::PreviewCost(Match->GetState(), Action, Match->GetContext());

	FText Reason;
	SetMessage(SubmitAction(Action, Reason) ? Describe(Action, Cost) : Reason);
}

FText AGridlockPlayerController::Describe(const FGameAction& Action, int32 Cost) const
{
	FText Route = FText::FromName(Action.RouteId);
	const UMatchSubsystem* Match = GetMatch();
	const UMapDefinition* Map = Match ? Match->GetContext().Map : nullptr;
	if (const FRouteDef* Def = Map ? Map->FindRoute(Action.RouteId) : nullptr)
	{
		Route = FText::FromString(FString::Printf(TEXT("%s - %s"), *Def->CityA.ToString(), *Def->CityB.ToString()));
	}

	switch (Action.Type)
	{
	case EGridlockActionType::ClaimRoute:
	{
		const FRouteDef* Def = Map ? Map->FindRoute(Action.RouteId) : nullptr;
		if (!Def || Action.Wilds < 0)
		{
			return FText::Format(LOCTEXT("Claimed", "Built {0}."), Route);
		}
		const int32 Wild = FMath::Min(Action.Wilds, Def->Length);
		return FText::Format(LOCTEXT("ClaimedWith", "Built {0} with {1} {2} + {3} Wild."), Route, Def->Length - Wild,
			FText::FromString(GridlockBoard::TicketColorName(Action.Color)), Wild);
	}
	case EGridlockActionType::PlaceLot:   return FText::Format(LOCTEXT("PlacedLot", "Placed a Lot on {0} for ${1}."), Route, Cost);
	case EGridlockActionType::Upgrade:    return FText::Format(LOCTEXT("Upgrading", "Upgrading the property on {0} for ${1}. Ready at the start of your next turn."), Route, Cost);
	case EGridlockActionType::Buyout:     return FText::Format(LOCTEXT("BoughtOut", "Bought out the property on {0} for ${1}."), Route, Cost);
	case EGridlockActionType::Demolish:   return FText::Format(LOCTEXT("Demolished", "Demolished the property on {0} for ${1}."), Route, Cost);
	case EGridlockActionType::BuyTicket:  return Cost > 0 ? FText::Format(LOCTEXT("BoughtTicket", "Bought a ticket for ${0}: see what you drew above your tickets."), Cost)
		: LOCTEXT("TookTicket", "Took a ticket from the deck: see what you drew above your tickets.");
	case EGridlockActionType::BuyPropertyCard: return FText::Format(LOCTEXT("BoughtPropertyCard", "Bought a property card for ${0}. It's in your property cards below."), Cost);
	case EGridlockActionType::KeepLot:       return FText::Format(LOCTEXT("KeptLot", "Kept the Lot for ${0} more. Place it on a route on a later turn (placing ends the turn)."), Cost);
	case EGridlockActionType::DiscardLot:    return LOCTEXT("DiscardedLot", "Discarded the Lot.");
	case EGridlockActionType::PlaceRouteLot: return FText::Format(LOCTEXT("PlacedRouteLot", "Placed a Lot on {0}. That was your turn."), Route);
	case EGridlockActionType::BuyCityProperty: return FText::Format(LOCTEXT("BoughtCity", "Bought the property in {0} for ${1}. That was your turn."), FText::FromName(Action.RouteId), Cost);
	case EGridlockActionType::SellProperty: return FText::Format(LOCTEXT("Sold", "Sold the property on {0} for ${1}."), Route, -Cost);
	default:                              return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE

#include "Game/GridlockGameMode.h"
#include "AI/AIPlayer.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AIPersonality.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Data/ScenarioDefinition.h"
#include "Data/TuningData.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Engine.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Game/GridlockHUD.h"
#include "Game/GridlockPlayerController.h"
#include "Game/MatchSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Gridlock.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Model/RulesLibrary.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"
#include "View/BoardLayout.h"
#include "View/CityActor.h"
#include "View/PropertyActor.h"
#include "View/RouteActor.h"

#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#endif

AGridlockGameMode::AGridlockGameMode()
{
	PlayerControllerClass = AGridlockPlayerController::StaticClass();
	HUDClass = AGridlockHUD::StaticClass();
	DefaultPawnClass = APawn::StaticClass(); // the board camera does the viewing

	CityClass = ACityActor::StaticClass();
	RouteClass = ARouteActor::StaticClass();
	PropertyClass = APropertyActor::StaticClass();

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	PlaneMesh = Plane.Object;
	BasicShapeMaterial = Material.Object;
}

void AGridlockGameMode::BeginPlay()
{
	Super::BeginPlay();

	UScenarioDefinition* Scenario = DefaultScenario ? DefaultScenario.Get() : MakeFallbackScenario();
	ActiveScenario = Scenario;
	UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	if (!Match)
	{
		return;
	}

	AIPlayer = NewObject<UAIPlayer>(this);
	AIPlayer->PlayerIndex = 1;
	AIPlayer->Personality = Scenario->AIPersonality;
	if (!AIPlayer->Personality)
	{
		UAIPersonality* Builder = NewObject<UAIPersonality>(this);
		Builder->ApplyPreset(EAIStyle::Builder, EAIDifficulty::Normal);
		AIPlayer->Personality = Builder;
	}

	GameStartTime = GetWorld()->GetRealTimeSeconds();
	GameEndTime = -1.0;
	Match->StartMatch(Scenario, FMath::Rand());
	SpawnLightingIfMissing();
	SpawnBoard();
	SpawnCamera();

	Match->OnStateChanged.AddDynamic(this, &AGridlockGameMode::HandleStateChanged);
	HandleStateChanged();

#if !UE_BUILD_SHIPPING
	RunCommandLineDevTools();
#endif
}

UScenarioDefinition* AGridlockGameMode::MakeFallbackScenario()
{
	UE_LOG(LogGridlock, Display, TEXT("No DefaultScenario set: using the built-in Ticket to Ride rules with ticket prices, Lots and city properties (v0.8) on map v0.5"));

	FallbackScenario = NewObject<UScenarioDefinition>(this);
	FallbackScenario->Title = FText::FromString(TEXT("Built-in v0.8 (Ticket to Ride, buying tickets, Lots, city properties)"));

	UMapDefinition* Map = NewObject<UMapDefinition>(FallbackScenario);
	Map->FillWithMapV05();
	URouteCardDeck* Deck = NewObject<URouteCardDeck>(FallbackScenario);
	Deck->FillWithDeckForMap(*Map);

	FallbackScenario->Map = Map;
	FallbackScenario->Deck = Deck;
	UTuningData* Tuning = NewObject<UTuningData>(FallbackScenario);
	Tuning->ApplyRulesV08();
	FallbackScenario->Tuning = Tuning;
	UAIPersonality* Builder = NewObject<UAIPersonality>(FallbackScenario);
	Builder->ApplyPreset(EAIStyle::Builder, EAIDifficulty::Normal);
	FallbackScenario->AIPersonality = Builder;
	return FallbackScenario;
}

void AGridlockGameMode::SetHotSeat(bool bEnabled)
{
	bHotSeat = bEnabled;
	HandleStateChanged();
}

void AGridlockGameMode::NewGame()
{
	if (UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>())
	{
		GetWorldTimerManager().ClearTimer(AITimer);
		GetWorldTimerManager().ClearTimer(AutoSkipTimer);
		bAIStepPending = false;
		bAutoSkipPending = false;
		AddNotice(TEXT("New game."));
		GameStartTime = GetWorld()->GetRealTimeSeconds();
		GameEndTime = -1.0;
		Match->StartMatch(ActiveScenario, FMath::Rand());
	}
}

int32 AGridlockGameMode::GetHumanPlayer() const
{
	const UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	return bHotSeat && Match ? Match->GetState().ActivePlayer : 0;
}

void AGridlockGameMode::AddNotice(const FString& Text)
{
	Notices.Add(Text);
}

TArray<FString> AGridlockGameMode::TakeNotices()
{
	return MoveTemp(Notices);
}

void AGridlockGameMode::UpdateHighlights(const FBoardState& State, const FRulesContext& Context)
{
	const int32 Human = GetHumanPlayer();
	const bool bHumanTurn = State.Phase != EGridlockPhase::GameOver && State.ActivePlayer == Human;
	const TArray<FGameAction> Legal = bHumanTurn ? URulesLibrary::GetLegalActions(State, Context) : TArray<FGameAction>();

	TSet<FName> Claimable;
	TSet<FName> ClickableSlots;
	for (const FGameAction& Action : Legal)
	{
		switch (Action.Type)
		{
		case EGridlockActionType::ClaimRoute:
			Claimable.Add(Action.RouteId);
			break;
		case EGridlockActionType::PlaceLot:
		case EGridlockActionType::Upgrade:
		case EGridlockActionType::Buyout:
		case EGridlockActionType::Demolish:
			ClickableSlots.Add(Action.RouteId);
			break;
		default:
			break;
		}
	}

	// Before the Build step, routes your tickets already pay for are shown as ready.
	TSet<FName> Ready;
	const bool bBeforeBuild = bHumanTurn && (State.Phase == EGridlockPhase::Start || State.Phase == EGridlockPhase::Resolve);
	if (bBeforeBuild && State.Players.IsValidIndex(Human) && State.Players[Human].PendingDraw.IsEmpty())
	{
		FBoardState AtBuild = State;
		AtBuild.Phase = EGridlockPhase::Build;
		for (const FGameAction& Action : URulesLibrary::GetLegalActions(AtBuild, Context))
		{
			if (Action.Type == EGridlockActionType::ClaimRoute)
			{
				Ready.Add(Action.RouteId);
			}
		}
	}

	// While you build, unclaimed routes you can't claim are dimmed so the clickable ones stand out.
	const bool bBuilding = bHumanTurn && State.Phase == EGridlockPhase::Build;
	for (ARouteActor* Route : RouteActors)
	{
		const bool bUnclaimed = State.RouteOwner.FindRef(Route->RouteId) == INDEX_NONE;
		Route->SetHighlight(Claimable.Contains(Route->RouteId) ? EBoardHighlight::Clickable
			: Ready.Contains(Route->RouteId) ? EBoardHighlight::Ready
			: bBuilding && bUnclaimed ? EBoardHighlight::Dimmed
			: EBoardHighlight::None);
	}
	for (APropertyActor* Property : PropertyActors)
	{
		Property->SetHighlight(ClickableSlots.Contains(Property->RouteId) ? EBoardHighlight::Clickable : EBoardHighlight::None);
	}

	// A step where the only option is to move on is skipped for you, with a note in the log.
	const bool bOnlyEndPhase = Legal.Num() == 1 && Legal[0].Type == EGridlockActionType::EndPhase;
	const bool bSkippableStep = State.Phase == EGridlockPhase::Start || State.Phase == EGridlockPhase::Resolve
		|| State.Phase == EGridlockPhase::Build || State.Phase == EGridlockPhase::BuyUpgrade;
	// Our own flag, not IsTimerActive: a timer still counts as active while its callback runs, so a
	// skip that leads straight into another empty step would never get scheduled.
	if (bOnlyEndPhase && bSkippableStep && !bAutoSkipPending)
	{
		bAutoSkipPending = true;
		GetWorldTimerManager().SetTimer(AutoSkipTimer, this, &AGridlockGameMode::RunAutoSkip, 0.6f, false);
	}
}

void AGridlockGameMode::RunAutoSkip()
{
	bAutoSkipPending = false;
	UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	if (!Match)
	{
		return;
	}
	const FBoardState State = Match->GetState();
	const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Match->GetContext());
	if (State.ActivePlayer != GetHumanPlayer() || Legal.Num() != 1 || Legal[0].Type != EGridlockActionType::EndPhase)
	{
		return; // something changed (an undo, a new game) since this was scheduled
	}

	switch (State.Phase)
	{
	case EGridlockPhase::Start:      AddNotice(Match->GetContext().Tuning->bClassicTurn ? TEXT("Turn passed: there was nothing you could take or claim.")
		: Match->GetContext().Tuning->bCardEconomy ? TEXT("Start skipped: you can't buy cards now.")
		: TEXT("Start skipped: you can't draw cards now.")); break;
	case EGridlockPhase::Resolve:    AddNotice(TEXT("Resolve skipped: nothing to remove.")); break;
	case EGridlockPhase::Build:      AddNotice(State.bDrewThisTurn ? TEXT("Build skipped: you drew cards this turn.")
		: TEXT("Build skipped: no route you can claim.")); break;
	case EGridlockPhase::BuyUpgrade: AddNotice(TEXT("Turn ended: nothing to buy or upgrade.")); break;
	default: break;
	}
	FText Reason;
	Match->Submit(Legal[0], Reason);
}

void AGridlockGameMode::ScheduleAITurn()
{
	const UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	// Our own flag, not IsTimerActive: while RunAIStep runs, its timer still counts as active, which
	// used to stop the AI after its first move of every turn.
	if (bHotSeat || !AIPlayer || !Match || bAIStepPending)
	{
		return;
	}
	const FBoardState State = Match->GetState();
	if (State.Phase != EGridlockPhase::GameOver && State.ActivePlayer == AIPlayer->PlayerIndex)
	{
		bAIStepPending = true;
		GetWorldTimerManager().SetTimer(AITimer, this, &AGridlockGameMode::RunAIStep, AIMoveDelay, false);
	}
}

void AGridlockGameMode::RunAIStep()
{
	bAIStepPending = false;
	UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	if (bHotSeat || !AIPlayer || !Match)
	{
		return;
	}
	const FBoardState State = Match->GetState();
	if (State.Phase == EGridlockPhase::GameOver || State.ActivePlayer != AIPlayer->PlayerIndex)
	{
		return; // e.g. an undo handed the turn back to the human
	}

	// Submitting fires OnStateChanged, which schedules the AI's next step if it's still its turn.
	const FGameAction Action = AIPlayer->ChooseAction(State, Match->GetContext());
	FText Reason;
	if (!Match->Submit(Action, Reason))
	{
		UE_LOG(LogGridlock, Error, TEXT("AI chose an illegal action (%s); ending its phase instead"), *Reason.ToString());
		FGameAction End;
		End.Type = EGridlockActionType::EndPhase;
		End.Player = AIPlayer->PlayerIndex;
		Match->Submit(End, Reason);
	}
}

void AGridlockGameMode::SpawnBoard_Implementation()
{
	const UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	const FRulesContext Context = Match ? Match->GetContext() : FRulesContext();
	if (!Context.Map)
	{
		UE_LOG(LogGridlock, Error, TEXT("SpawnBoard: no map"));
		return;
	}

	SpawnBackdrop(*Context.Map);

	for (const FCityDef& City : Context.Map->Cities)
	{
		ACityActor* Actor = GetWorld()->SpawnActor<ACityActor>(CityClass, FTransform::Identity);
		Actor->Setup(City);
		CityActors.Add(Actor);
	}

	for (const FRouteDef& Route : Context.Map->Routes)
	{
		ARouteActor* RouteActor = GetWorld()->SpawnActor<ARouteActor>(RouteClass, FTransform::Identity);
		RouteActor->Setup(Route, *Context.Map);
		RouteActors.Add(RouteActor);

		APropertyActor* Property = GetWorld()->SpawnActor<APropertyActor>(PropertyClass, FTransform::Identity);
		Property->Setup(Route.Id, RouteActor->GetSlotLocation());
		Property->SetActorHiddenInGame(Context.Tuning && Context.Tuning->bClassicTurn); // no properties under Ticket to Ride rules (lots are drawn by the HUD)
		PropertyActors.Add(Property);
	}
}

void AGridlockGameMode::SpawnBackdrop(const UMapDefinition& Map)
{
	// Map bounds, padded, as a ground plane under the board (the engine plane is 100 x 100).
	FBox2D Bounds(ForceInit);
	for (const FCityDef& City : Map.Cities)
	{
		Bounds += City.Position;
	}
	for (const FRouteDef& Route : Map.Routes)
	{
		if (Route.bCurved)
		{
			Bounds += Route.CurveControl;
		}
	}
	Bounds = Bounds.ExpandBy(FVector2D(400.0, 300.0)); // reaches past the screen edges under the tilted camera

	auto SpawnPlane = [this](const FVector& Center, const FVector2D& WorldSize, const FLinearColor& Color)
	{
		AStaticMeshActor* Plane = GetWorld()->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
		UStaticMeshComponent* Mesh = Plane->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(PlaneMesh);
		Mesh->SetWorldScale3D(FVector(WorldSize.X / 100.0, WorldSize.Y / 100.0, 1.0));
		Mesh->SetMaterial(0, GridlockBoard::MakeColorMaterial(Plane, Color));
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	};

	const FVector2D Size = Bounds.GetSize() * GridlockBoard::Scale;
	SpawnPlane(GridlockBoard::ToWorld(Bounds.GetCenter(), 0.0), FVector2D(Size.Y, Size.X), GridlockBoard::Ground);

	// The river runs the full height of the ground, under the bridge towns.
	const FVector2D RiverTop(GridlockBoard::CenterX, Bounds.Min.Y);
	const FVector2D RiverBottom(GridlockBoard::CenterX, Bounds.Max.Y);
	const double RiverLength = (RiverBottom.Y - RiverTop.Y) * GridlockBoard::Scale;
	SpawnPlane(GridlockBoard::ToWorld((RiverTop + RiverBottom) * 0.5, 0.5), FVector2D(RiverLength, 34.0 * GridlockBoard::Scale), GridlockBoard::River);
}

void AGridlockGameMode::SpawnLightingIfMissing()
{
	if (TActorIterator<ADirectionalLight>(GetWorld()))
	{
		return; // the level has its own lighting
	}

	ADirectionalLight* Sun = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-55.0, 35.0, 0.0));
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		SunLight->SetAtmosphereSunLight(true);
	}

	GetWorld()->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);

	ASkyLight* Sky = GetWorld()->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->bRealTimeCapture = true;
	Sky->FinishSpawning(FTransform::Identity);

	// Auto-exposure washes out the player colors on a plain board; pull it down a stop.
	APostProcessVolume* PostProcess = GetWorld()->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator);
	PostProcess->bUnbound = true;
	PostProcess->Settings.bOverride_AutoExposureBias = true;
	PostProcess->Settings.AutoExposureBias = -1.0f;
}

void AGridlockGameMode::SpawnCamera()
{
	// A tabletop view tilted 20 degrees from straight down, so property heights read. Screen up = world +X (map north).
	// It aims a little south of the board's center to leave room for the HUD panel at the bottom.
	const double Pitch = 70.0;
	const double Height = 1200.0;
	const double AimX = -110.0;
	const double CameraX = AimX - Height / FMath::Tan(FMath::DegreesToRadians(Pitch));
	BoardCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(CameraX, 0.0, Height), FRotator(-Pitch, 0.0, 0.0));
	BoardCamera->GetCameraComponent()->SetFieldOfView(70.0f);
	BoardCamera->GetCameraComponent()->bConstrainAspectRatio = false;

	if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
	{
		Controller->SetViewTarget(BoardCamera);
	}
}

void AGridlockGameMode::HandleStateChanged()
{
	const UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();
	if (!Match)
	{
		return;
	}
	const FBoardState State = Match->GetState();

	for (ACityActor* City : CityActors)
	{
		City->OnBoardStateChanged(State);
	}
	for (ARouteActor* Route : RouteActors)
	{
		Route->OnBoardStateChanged(State);
	}
	for (APropertyActor* Property : PropertyActors)
	{
		Property->OnSlotChanged(State.Slots.FindRef(Property->RouteId));
	}

	// The game clock stops at the end (and starts again if an undo takes the game back).
	if (State.Phase == EGridlockPhase::GameOver && GameEndTime < 0.0)
	{
		GameEndTime = GetWorld()->GetRealTimeSeconds();
		const int32 Seconds = FMath::RoundToInt(GameEndTime - GameStartTime);
		AddNotice(FString::Printf(TEXT("Game time: %d:%02d (%d rounds)"), Seconds / 60, Seconds % 60, State.Round));
	}
	else if (State.Phase != EGridlockPhase::GameOver)
	{
		GameEndTime = -1.0;
	}
	UpdateHighlights(State, Match->GetContext());
	ScheduleAITurn();
}

double AGridlockGameMode::GetGameSeconds() const
{
	return (GameEndTime >= 0.0 ? GameEndTime : GetWorld()->GetRealTimeSeconds()) - GameStartTime;
}

#if !UE_BUILD_SHIPPING
void AGridlockGameMode::RunCommandLineDevTools()
{
	UMatchSubsystem* Match = GetWorld()->GetSubsystem<UMatchSubsystem>();

	int32 DemoSteps = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("GridlockDemoSteps="), DemoSteps) && Match)
	{
		FRandomStream Random(DemoSteps);
		for (int32 Step = 0; Step < DemoSteps; ++Step)
		{
			const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(Match->GetState(), Match->GetContext());
			if (Legal.IsEmpty())
			{
				break;
			}
			const FGameAction& Action = Legal.Num() > 1 && Random.FRand() < 0.85f
				? Legal[Random.RandRange(0, Legal.Num() - 2)]
				: Legal.Last();
			FText Reason;
			Match->Submit(Action, Reason);
		}
	}

	// -GridlockDemoPhase=Start|Build|BuyUpgrade|Resolve|Keep|AITurn: random moves until the human's turn at that step (or the AI's turn).
	FString DemoPhase;
	if (FParse::Value(FCommandLine::Get(), TEXT("GridlockDemoPhase="), DemoPhase) && Match)
	{
		FRandomStream Random(7);
		for (int32 Step = 0; Step < 400; ++Step)
		{
			const FBoardState State = Match->GetState();
			const TArray<FGameAction> Legal = URulesLibrary::GetLegalActions(State, Match->GetContext());
			if (State.Phase == EGridlockPhase::GameOver || Legal.IsEmpty())
			{
				break;
			}
			const bool bHumanTurn = State.ActivePlayer == GetHumanPlayer();
			const bool bAtPhase = (DemoPhase == TEXT("Start") && State.Phase == EGridlockPhase::Start && State.Players[State.ActivePlayer].PendingDraw.IsEmpty())
				|| (DemoPhase == TEXT("Build") && State.Phase == EGridlockPhase::Build)
				|| (DemoPhase == TEXT("BuyUpgrade") && State.Phase == EGridlockPhase::BuyUpgrade)
				|| (DemoPhase == TEXT("Resolve") && State.Phase == EGridlockPhase::Resolve);
			const bool bLegalTarget = Legal.Num() > 1; // something to click, not just "next step"
			if ((bHumanTurn && Step > 20 && bAtPhase && bLegalTarget) || (DemoPhase == TEXT("AITurn") && Step > 20 && !bHumanTurn && State.Phase == EGridlockPhase::Start))
			{
				break;
			}
			FText Reason;
			if (bHumanTurn && Step > 20 && DemoPhase == TEXT("Keep") && State.Phase == EGridlockPhase::Start)
			{
				if (const FGameAction* Draw = Legal.FindByPredicate([](const FGameAction& A) { return A.Type == EGridlockActionType::DrawCards; }))
				{
					Match->Submit(*Draw, Reason);
					break;
				}
			}
			const FGameAction& Action = Legal.Num() > 1 && Random.FRand() < 0.85f ? Legal[Random.RandRange(0, Legal.Num() - 2)] : Legal.Last();
			Match->Submit(Action, Reason);
		}
	}

	// -GridlockDemoBuildWindow: open the build window on a route your tickets pay for (or any open one).
	AGridlockPlayerController* Controller = Cast<AGridlockPlayerController>(GetWorld()->GetFirstPlayerController());
	if (FParse::Param(FCommandLine::Get(), TEXT("GridlockDemoBuildWindow")) && Match && Controller)
	{
		const FBoardState State = Match->GetState();
		FName Pick;
		for (const TPair<FName, int32>& Entry : State.RouteOwner)
		{
			FText Reason;
			if (Entry.Value == INDEX_NONE && (Pick.IsNone() || URulesLibrary::CanClaimLater(State, Match->GetContext(), Entry.Key, Reason)))
			{
				Pick = Entry.Key;
			}
		}
		Controller->OpenBuild(Pick);
	}

	FString ShotPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("GridlockShot="), ShotPath))
	{
		// Wait (up to 3 minutes) for shaders and assets to finish compiling, so the shot shows real materials,
		// and at least -GridlockShotDelay=N seconds (to let the AI play live first).
		float MinDelay = 0.0f;
		FParse::Value(FCommandLine::Get(), TEXT("GridlockShotDelay="), MinDelay);
		GetWorldTimerManager().SetTimer(ShotTimer, FTimerDelegate::CreateWeakLambda(this, [this, ShotPath, MinDelay]()
		{
			const double Waited = GetWorld()->GetTimeSeconds();
			if ((GetRemainingCompileJobs() > 0 && Waited < 180.0) || Waited < MinDelay)
			{
				return;
			}
			const FString Path = ShotPath; // clearing the timer destroys this lambda and its captures
			GetWorldTimerManager().ClearTimer(ShotTimer);
			GetWorldTimerManager().SetTimer(QuitTimer, FTimerDelegate::CreateWeakLambda(this, [this, ShotPath = Path]()
			{
				UE_LOG(LogGridlock, Display, TEXT("Screenshot requested: %s"), *ShotPath);
				FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
				GetWorldTimerManager().SetTimer(QuitTimer, FTimerDelegate::CreateLambda([]()
				{
					FPlatformMisc::RequestExit(false);
				}), 4.0f, false);
			}), 2.0f, false);
		}), 1.0f, true);
	}
}

int32 AGridlockGameMode::GetRemainingCompileJobs()
{
	int32 Jobs = 0;
#if WITH_EDITOR
	Jobs += FAssetCompilingManager::Get().GetNumRemainingAssets();
	Jobs += GShaderCompilingManager ? GShaderCompilingManager->GetNumRemainingJobs() : 0;
#endif
	return Jobs;
}
#endif

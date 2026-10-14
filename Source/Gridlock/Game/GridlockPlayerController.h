#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Model/GridlockTypes.h"
#include "GridlockPlayerController.generated.h"

class AGridlockHUD;
class UInputAction;
class UInputMappingContext;
class UMatchSubsystem;

/**
 * Turns clicks and keys into FGameActions and sends them to UMatchSubsystem.
 * A click first checks the HUD's buttons, then the board: the step decides what a board click
 * means (buy out or demolish, claim, Lot or upgrade). Keys are shortcuts for the buttons:
 * Space next step, T buy a ticket, Y buy 2 tickets, P buy a property card, D draw Route cards, 1-5 + Enter keep cards,
 * S sell mode, Z undo, F1 help, H hot-seat, right-click demolish, Esc closes the build window.
 * Ticket to Ride rules: 1-5 take a face-up ticket, T takes one from the deck, D draws Route cards.
 * Clicking a route you could build opens a build window to choose the ticket color and how many Wilds to pay.
 */
UCLASS()
class GRIDLOCK_API AGridlockPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AGridlockPlayerController();

	/** Sends an action as the acting player (in hot-seat, whoever's turn it is). */
	UFUNCTION(BlueprintCallable, Category = "Gridlock")
	bool SubmitAction(const FGameAction& Action, FText& OutReason);

	/** Runs an on-screen button by its id (the HUD names them). */
	void PressButton(FName ButtonId);

	/** The route (or its slot) under the mouse, NAME_None if none. */
	FName GetHoveredRouteId() const;

	/** The player this controller acts for right now. */
	int32 GetActingPlayer() const;

	/** Feedback from the last action, or why it was refused, and how long ago it was set. */
	const FText& GetMessage() const { return Message; }
	double GetMessageAge() const;

	/** Which of the acting player's drawn cards are marked to keep. */
	const TArray<bool>& GetKeepSelection() const { return KeepSelection; }

	/** In Resolve, whether a board click demolishes (true) or buys out (false). */
	bool IsDemolishMode() const { return bDemolishMode; }

	/** Whether a board click sells your property (Rules v0.4). */
	bool IsSellMode() const { return bSellMode; }

	/** The route in the open build window (NAME_None when closed), and the payment chosen in it. */
	FName GetBuildRoute() const { return BuildRoute; }
	EGridlockColor GetBuildColor() const { return BuildColor; }
	int32 GetBuildWilds() const { return BuildWilds; }

	/** v0.8: the city in the open buy window (NAME_None when closed). */
	FName GetCityChoice() const { return CityChoice; }

	/** The claim the build window would send. */
	FGameAction MakeBuildAction() const;

	/** Opens the build window on a route, starting from its color (gray: the color you hold most) and only the Wilds needed. */
	void OpenBuild(FName RouteId);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

private:
	void CreateInput();
	void AddMappingContext();
	UMatchSubsystem* GetMatch() const;
	AGridlockHUD* GetGridlockHUD() const;

	void HandleSelect();
	void HandleDemolish();
	void HandleEndPhase();
	void HandleDraw();
	void HandleKeep();
	void HandleUndo();
	void HandleToggle1() { ToggleKeep(0); }
	void HandleToggle2() { ToggleKeep(1); }
	void HandleToggle3() { ToggleKeep(2); }
	void HandleToggle4() { ToggleKeep(3); }
	void HandleToggle5() { ToggleKeep(4); }
	void HandleBuyTicket();
	void HandleBuyTwoTickets();
	void HandleBuyPropertyCard();
	void HandleToggleHotSeat();
	void HandleHelp();
	void HandleSellMode();

	/** Rules v0.4: building from the start of your turn steps on to the Build step first. */
	void ClaimFromStart(const FGameAction& Claim);

	/** The build window: change the payment, build or close. */
	void SetBuildColor(EGridlockColor Color);
	void ChangeBuildWilds(int32 Delta);
	void ConfirmBuild();
	void HandleEnter();
	void HandleCancel();

	/** v0.8: a click on a city opens its buy window. */
	void ClickCity(FName CityId);

	void ClickRoute(bool bDemolish);
	void ToggleKeep(int32 Index);

	/** Ticket to Ride: take the face-up ticket in this slot (0-4). */
	void TakeFaceUp(int32 Slot);
	void Send(EGridlockActionType Type, FName RouteId = NAME_None);
	void SetMessage(const FText& InMessage);
	FText Describe(const FGameAction& Action, int32 Cost) const;

	// Placeholder input built in code; swap for IA_/IMC_ assets in Content/Gridlock/Input later.
	UPROPERTY()
	TObjectPtr<UInputMappingContext> BoardContext;

	UPROPERTY()
	TArray<TObjectPtr<UInputAction>> Actions;

	FText Message;
	double MessageTime = 0.0;
	TArray<bool> KeepSelection;
	TArray<FName> KeepSelectionFor;
	bool bDemolishMode = false;
	bool bSellMode = false;
	FName CityChoice;
	FName BuildRoute;
	EGridlockColor BuildColor = EGridlockColor::Gray;
	int32 BuildWilds = 0;
};

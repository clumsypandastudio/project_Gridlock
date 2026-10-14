#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Model/GridlockTypes.h"
#include "GridlockHUD.generated.h"

struct FRulesContext;
class AGridlockPlayerController;
class UAIPlayer;
class UFont;

/**
 * Placeholder HUD drawn with the canvas (WBP_HUD replaces it in milestone 5):
 * - a banner at the top that says what to do right now
 * - a bottom panel: both players, your Route cards, a log of what happened, and buttons
 * - a pop-up for choosing Route cards, a tooltip at the mouse, gold rings on your cards' cities,
 *   markers where the last moves happened, and a how-to-play screen (shown at the start).
 */
UCLASS()
class GRIDLOCK_API AGridlockHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	/** The on-screen button at this screen position, or NAME_None. */
	FName GetButtonAt(const FVector2D& ScreenPosition) const;

	/** The city drawn at (or very near) this screen position last frame, or NAME_None. */
	FName GetCityAt(const FVector2D& ScreenPosition) const;

	bool IsHelpOpen() const { return bHelpOpen; }
	void SetHelpOpen(bool bOpen) { bHelpOpen = bOpen; }

	/** The end-of-game results window (opens by itself when the game ends). */
	bool IsResultsOpen() const { return bResultsOpen; }
	void SetResultsOpen(bool bOpen) { bResultsOpen = bOpen; }

private:
	UFUNCTION()
	void HandleGameEvent(const FGameEvent& Event);

	/** What everything in one frame needs. */
	struct FDrawFrame
	{
		const FBoardState* State = nullptr;
		const FRulesContext* Context = nullptr;
		const AGridlockPlayerController* Controller = nullptr;
		const UAIPlayer* AI = nullptr; // set when you play against the AI
		int32 Human = 0;
		bool bHumanTurn = false;
		FVector2D Mouse = FVector2D::ZeroVector;
	};

	void DrawCityNames(const FDrawFrame& Frame);
	void DrawGoalRings(const FDrawFrame& Frame);
	void DrawMarks(const FDrawFrame& Frame);

	/** v0.7: each route's lot slots as small squares on the line, filled in the owner's color. */
	void DrawRouteLots(const FDrawFrame& Frame);

	/** v0.7: you drew a Lot from the deck: keep it (paying the rest) or discard it. */
	void DrawLotPopup(const FDrawFrame& Frame);

	/** End of the game: the time, and each player side by side: points, Route cards, cities and money. */
	void DrawResults(const FDrawFrame& Frame);

	/** One player's column in the results window; returns its height. */
	float DrawResultsColumn(const FDrawFrame& Frame, int32 Player, float X, float Y, float Width);

	/** v0.8: the clicked city: buy its property (ends your turn), or why you can't. */
	void DrawCityPopup(const FDrawFrame& Frame);

	/** v0.8: what hovering a city tells you: its owner and toll, or whether you can buy it. */
	TArray<FString> DescribeCity(const FDrawFrame& Frame, FName CityId) const;
	void DrawBanner(const FDrawFrame& Frame);

	/** Your unfinished Route cards laid out as tiles under the banner; hovering one rings its cities. */
	void DrawRouteTiles(const FDrawFrame& Frame);
	void DrawPanel(const FDrawFrame& Frame);
	void DrawButtons(const FDrawFrame& Frame, float Left, float Top, float Width);
	void DrawKeepPopup(const FDrawFrame& Frame);

	/** The build window: the route, the ticket color (pickable on a gray route), how many Wilds, and Build / Cancel. */
	void DrawBuildPopup(const FDrawFrame& Frame);
	void DrawTooltip(const FDrawFrame& Frame);
	void DrawHelp(const FDrawFrame& Frame);

	/** A clickable button; disabled buttons draw grey and can't be clicked. */
	void DrawButton(const FDrawFrame& Frame, FName Id, const FString& Label, float X, float Y, float W, float H, bool bEnabled, bool bPrimary);
	void DrawTextShadowed(const FString& Text, const FLinearColor& Color, float X, float Y, UFont* Font, float Scale = 1.0f);

	/** One face-up ticket (Ticket to Ride rules): a clickable card in its color, faint when it can't be taken. */
	void DrawFaceUpCard(const FDrawFrame& Frame, int32 Slot, EGridlockColor Color, float X, float Y, float W, float H, bool bEnabled);

	/** A small colored card with a label (a ticket or property card); faint when you hold none. Returns its width. */
	float DrawCardChip(const FString& Label, const FLinearColor& Color, float X, float Y, bool bHeld);
	void DrawRing(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness);
	void DrawOutline(float X, float Y, float W, float H, const FLinearColor& Color, float Thickness = 1.0f);

	FString PlayerLabel(const FDrawFrame& Frame, int32 Player) const;
	FString RouteName(const FRulesContext& Context, FName RouteId) const;
	FString FormatEvent(const FGameEvent& Event, const FRulesContext& Context, const UAIPlayer* AI) const;
	TArray<FString> DescribeHover(const FDrawFrame& Frame, FName RouteId) const;

	struct FLogLine
	{
		FString Text;
		FLinearColor Color;
	};

	struct FMark
	{
		FVector World;
		FLinearColor Color;
		double Until = 0.0;
	};

	struct FButtonRect
	{
		FName Id;
		FVector2D Min;
		FVector2D Max;
	};

	/** A card you just bought, shown in the "You drew" strip for a few seconds. */
	struct FRecentDraw
	{
		FString Label;
		FLinearColor Color;
		double Until = 0.0;
	};

	TArray<FLogLine> Log;
	TArray<FMark> Marks;
	TArray<FRecentDraw> RecentDraws;
	TArray<FButtonRect> Buttons;
	TMap<FName, FVector2D> CityScreen;
	bool bHelpOpen = true;
	bool bResultsOpen = false;
	bool bResultsShown = false; // opened once for this game already
};

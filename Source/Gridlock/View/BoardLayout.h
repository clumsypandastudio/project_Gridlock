#pragma once

#include "CoreMinimal.h"
#include "Model/GridlockTypes.h"

class UMapDefinition;
class UMaterialInstanceDynamic;
struct FRouteDef;

/** Map coordinates (as drawn on the Map tab) to world space, plus the placeholder look. */
namespace GridlockBoard
{
	/** World units per map unit. */
	constexpr double Scale = 2.0;

	/** The map point that sits at the world origin (Midway). */
	constexpr double CenterX = 380.0;
	constexpr double CenterY = 283.0;

	/** Routes stop this far (in map units) from a city's center. */
	constexpr double CityGap = 14.0;

	/** Map +x becomes world +Y (screen right) and map +y becomes world -X (screen down) under the top-down camera. */
	inline FVector ToWorld(const FVector2D& MapPos, double Z = 0.0)
	{
		return FVector(-(MapPos.Y - CenterY) * Scale, (MapPos.X - CenterX) * Scale, Z);
	}

	inline FVector2D QuadraticPoint(const FVector2D& A, const FVector2D& Control, const FVector2D& B, double T)
	{
		const double U = 1.0 - T;
		return A * (U * U) + Control * (2.0 * U * T) + B * (T * T);
	}

	GRIDLOCK_API FLinearColor PlayerColor(int32 Player);
	GRIDLOCK_API FString PlayerName(int32 Player);

	inline const FLinearColor UnclaimedRoute(0.22f, 0.22f, 0.22f);
	inline const FLinearColor EmptySlot(0.8f, 0.8f, 0.8f);
	inline const FLinearColor Rubble(0.2f, 0.12f, 0.06f);
	inline const FLinearColor City(0.9f, 0.9f, 0.9f);
	inline const FLinearColor BridgeTown(0.95f, 0.75f, 0.2f);
	inline const FLinearColor Ground(0.16f, 0.26f, 0.15f);
	inline const FLinearColor River(0.08f, 0.25f, 0.6f);

	/** Slots you can click right now, and your Route cards' cities. */
	inline const FLinearColor Clickable(1.0f, 0.78f, 0.08f);


	/** A route's (or ticket's) color on the board and in the HUD. */
	GRIDLOCK_API FLinearColor TicketColor(EGridlockColor Color);
	GRIDLOCK_API FString TicketColorName(EGridlockColor Color);

	/** A point along a route (T = 0 at CityA, 1 at CityB) in map space, shifted to its side for a double route's track. */
	GRIDLOCK_API FVector2D RoutePoint(const FRouteDef& Route, const UMapDefinition& Map, double T);

	/** A route's midpoint (where its property slot sits), in world space. */
	GRIDLOCK_API FVector RouteMidpoint(const FRouteDef& Route, const UMapDefinition& Map, double Z = 0.0);
}

/** How a board piece shows whether you can click it right now. */
enum class EBoardHighlight : uint8
{
	None,
	/** You can click it: thicker for routes (in their own color), gold for slots. */
	Clickable,
	/** Not an option this step: drawn darker so the clickable ones stand out. */
	Dimmed,
	/** Not clickable yet, but your tickets already pay for it (shown before the Build step). */
	Ready
};

namespace GridlockBoard
{

	/** A dynamic instance of the engine's BasicShapeMaterial with its Color parameter set. */
	GRIDLOCK_API UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FLinearColor& Color);
	GRIDLOCK_API void SetColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color);
}

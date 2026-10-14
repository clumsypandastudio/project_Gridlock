#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UMapDefinition;
struct FRouteDef;

/** Shortest paths over the map's routes, measured in length dots. */
class GRIDLOCK_API FGridlockPathfinding
{
public:
	/**
	 * Dijkstra from one city to another, using only routes CanUse accepts.
	 * Returns the distance in dots, or INDEX_NONE when To can't be reached.
	 * OutRouteIds, when given, receives the path's routes in order from From to To.
	 */
	static int32 ShortestDistance(const UMapDefinition& Map, FName From, FName To,
		TFunctionRef<bool(const FRouteDef&)> CanUse, TArray<FName>* OutRouteIds = nullptr);

	/** Same, over every route (an empty board). */
	static int32 ShortestDistance(const UMapDefinition& Map, FName From, FName To);

	/**
	 * Cheapest cost from any of the source cities to every city, where Cost gives each route's
	 * cost (0 is allowed, INDEX_NONE means impassable). Unreachable cities are left out.
	 * OutArrivedBy, when given, maps each reached city to the route it was reached by.
	 */
	static TMap<FName, int32> CostsFrom(const UMapDefinition& Map, const TArray<FName>& Sources,
		TFunctionRef<int32(const FRouteDef&)> Cost, TMap<FName, FName>* OutArrivedBy = nullptr);
};

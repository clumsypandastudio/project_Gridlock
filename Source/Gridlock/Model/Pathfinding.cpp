#include "Model/Pathfinding.h"
#include "Data/MapDefinition.h"

int32 FGridlockPathfinding::ShortestDistance(const UMapDefinition& Map, FName From, FName To,
	TFunctionRef<bool(const FRouteDef&)> CanUse, TArray<FName>* OutRouteIds)
{
	if (OutRouteIds)
	{
		OutRouteIds->Reset();
	}
	if (!Map.FindCity(From) || !Map.FindCity(To))
	{
		return INDEX_NONE;
	}

	// The map is small (15 cities), so a linear scan for the closest city is fine.
	TMap<FName, int32> Distance;
	TMap<FName, FName> ArrivedBy;
	TSet<FName> Settled;
	for (const FCityDef& City : Map.Cities)
	{
		Distance.Add(City.Id, MAX_int32);
	}
	Distance[From] = 0;

	while (true)
	{
		FName Closest;
		int32 ClosestDistance = MAX_int32;
		for (const TPair<FName, int32>& Entry : Distance)
		{
			if (!Settled.Contains(Entry.Key) && Entry.Value < ClosestDistance)
			{
				Closest = Entry.Key;
				ClosestDistance = Entry.Value;
			}
		}
		if (ClosestDistance == MAX_int32 || Closest == To)
		{
			break;
		}
		Settled.Add(Closest);

		for (const FRouteDef& Route : Map.Routes)
		{
			if (!Route.Touches(Closest) || !CanUse(Route))
			{
				continue;
			}
			const FName Next = Route.OtherEnd(Closest);
			int32* NextDistance = Distance.Find(Next);
			if (!NextDistance || Settled.Contains(Next))
			{
				continue;
			}
			const int32 Candidate = ClosestDistance + Route.Length;
			if (Candidate < *NextDistance)
			{
				*NextDistance = Candidate;
				ArrivedBy.Add(Next, Route.Id);
			}
		}
	}

	const int32 Result = Distance[To];
	if (Result == MAX_int32)
	{
		return INDEX_NONE;
	}

	if (OutRouteIds)
	{
		FName City = To;
		while (City != From)
		{
			const FName RouteId = ArrivedBy.FindChecked(City);
			OutRouteIds->Insert(RouteId, 0);
			City = Map.FindRoute(RouteId)->OtherEnd(City);
		}
	}
	return Result;
}

int32 FGridlockPathfinding::ShortestDistance(const UMapDefinition& Map, FName From, FName To)
{
	return ShortestDistance(Map, From, To, [](const FRouteDef&) { return true; });
}

TMap<FName, int32> FGridlockPathfinding::CostsFrom(const UMapDefinition& Map, const TArray<FName>& Sources,
	TFunctionRef<int32(const FRouteDef&)> Cost, TMap<FName, FName>* OutArrivedBy)
{
	TMap<FName, int32> Best;
	TSet<FName> Settled;
	for (const FName Source : Sources)
	{
		if (Map.FindCity(Source))
		{
			Best.Add(Source, 0);
		}
	}

	while (true)
	{
		FName Closest;
		int32 ClosestCost = MAX_int32;
		for (const TPair<FName, int32>& Entry : Best)
		{
			if (!Settled.Contains(Entry.Key) && Entry.Value < ClosestCost)
			{
				Closest = Entry.Key;
				ClosestCost = Entry.Value;
			}
		}
		if (ClosestCost == MAX_int32)
		{
			break;
		}
		Settled.Add(Closest);

		for (const FRouteDef& Route : Map.Routes)
		{
			if (!Route.Touches(Closest))
			{
				continue;
			}
			const int32 Step = Cost(Route);
			const FName Next = Route.OtherEnd(Closest);
			if (Step == INDEX_NONE || Settled.Contains(Next))
			{
				continue;
			}
			const int32 Candidate = ClosestCost + Step;
			const int32* Known = Best.Find(Next);
			if (!Known || Candidate < *Known)
			{
				Best.Add(Next, Candidate);
				if (OutArrivedBy)
				{
					OutArrivedBy->Add(Next, Route.Id);
				}
			}
		}
	}
	return Best;
}

#include "Data/MapDefinition.h"
#include "Gridlock.h"

const FCityDef* UMapDefinition::FindCity(FName Id) const
{
	return Cities.FindByPredicate([Id](const FCityDef& City) { return City.Id == Id; });
}

const FRouteDef* UMapDefinition::FindRoute(FName Id) const
{
	return Routes.FindByPredicate([Id](const FRouteDef& Route) { return Route.Id == Id; });
}

TArray<FString> UMapDefinition::GetProblems() const
{
	TArray<FString> Problems;

	TSet<FName> CityIds;
	int32 StartCount = 0;
	for (const FCityDef& City : Cities)
	{
		if (CityIds.Contains(City.Id))
		{
			Problems.Add(FString::Printf(TEXT("Duplicate city id %s"), *City.Id.ToString()));
		}
		CityIds.Add(City.Id);
		StartCount += City.bIsStart ? 1 : 0;
	}
	if (StartCount != 2)
	{
		Problems.Add(FString::Printf(TEXT("Expected 2 start cities, found %d"), StartCount));
	}

	TSet<FName> RouteIds;
	TSet<FString> CityPairs;
	TMap<FName, int32> Degree;
	for (const FRouteDef& Route : Routes)
	{
		if (RouteIds.Contains(Route.Id))
		{
			Problems.Add(FString::Printf(TEXT("Duplicate route id %s"), *Route.Id.ToString()));
		}
		RouteIds.Add(Route.Id);

		if (!CityIds.Contains(Route.CityA) || !CityIds.Contains(Route.CityB))
		{
			Problems.Add(FString::Printf(TEXT("Route %s uses an unknown city"), *Route.Id.ToString()));
		}
		if (Route.Length < 1 || Route.Length > 9)
		{
			Problems.Add(FString::Printf(TEXT("Route %s has length %d (must be 1-9)"), *Route.Id.ToString(), Route.Length));
		}

		const FString A = Route.CityA.ToString();
		const FString B = Route.CityB.ToString();
		const FString Pair = A < B ? A + TEXT("|") + B : B + TEXT("|") + A;
		if (CityPairs.Contains(Pair))
		{
			Problems.Add(FString::Printf(TEXT("Route %s duplicates another route between the same cities"), *Route.Id.ToString()));
		}
		CityPairs.Add(Pair);

		Degree.FindOrAdd(Route.CityA)++;
		Degree.FindOrAdd(Route.CityB)++;
	}

	// The "no single cut" rule from the Map tab: one blocker never isolates a city.
	for (const FCityDef& City : Cities)
	{
		const int32 Count = Degree.FindRef(City.Id);
		if (Count < 3)
		{
			Problems.Add(FString::Printf(TEXT("City %s has only %d routes (needs at least 3)"), *City.Id.ToString(), Count));
		}
	}

	return Problems;
}

void UMapDefinition::FillWithMapV01()
{
	Modify();
	Cities.Reset();
	Routes.Reset();

	auto AddCity = [this](const TCHAR* Id, double X, double Y, bool bIsStart = false)
	{
		FCityDef& City = Cities.AddDefaulted_GetRef();
		City.Id = FName(Id);
		City.Name = FText::FromString(Id);
		City.Position = FVector2D(X, Y);
		City.bIsStart = bIsStart;
	};

	auto AddRoute = [this](const TCHAR* Id, const TCHAR* A, const TCHAR* B, int32 Length, EGridlockColor Color)
	{
		FRouteDef& Route = Routes.AddDefaulted_GetRef();
		Route.Id = FName(Id);
		Route.CityA = FName(A);
		Route.CityB = FName(B);
		Route.Length = Length;
		Route.Color = Color;
	};

	auto AddCurvedRoute = [this, &AddRoute](const TCHAR* Id, const TCHAR* A, const TCHAR* B, int32 Length, double CX, double CY, EGridlockColor Color)
	{
		AddRoute(Id, A, B, Length, Color);
		Routes.Last().bCurved = true;
		Routes.Last().CurveControl = FVector2D(CX, CY);
	};

	// West
	AddCity(TEXT("Westport"), 74, 283, true);
	AddCity(TEXT("Pinecrest"), 166, 161);
	AddCity(TEXT("Saltmarsh"), 166, 406);
	AddCity(TEXT("Ironford"), 238, 283);
	AddCity(TEXT("Millbrook"), 280, 384);
	AddCity(TEXT("Highcliff"), 273, 182);
	// River (bridge towns)
	AddCity(TEXT("Northbridge"), 380, 139);
	AddCity(TEXT("Midway"), 380, 283);
	AddCity(TEXT("Southbridge"), 380, 427);
	for (int32 Index = Cities.Num() - 3; Index < Cities.Num(); ++Index)
	{
		Cities[Index].bIsBridgeTown = true;
	}
	// East
	AddCity(TEXT("Easton"), 686, 283, true);
	AddCity(TEXT("Frostpeak"), 594, 161);
	AddCity(TEXT("Coralbay"), 594, 406);
	AddCity(TEXT("Copperton"), 522, 283);
	AddCity(TEXT("Vinemoor"), 480, 384);
	AddCity(TEXT("Stonewatch"), 487, 182);

	AddRoute(TEXT("R01"), TEXT("Westport"), TEXT("Pinecrest"), 2, EGridlockColor::Red);
	AddRoute(TEXT("R02"), TEXT("Westport"), TEXT("Saltmarsh"), 2, EGridlockColor::Green);
	AddRoute(TEXT("R03"), TEXT("Westport"), TEXT("Ironford"), 2, EGridlockColor::Yellow);
	AddRoute(TEXT("R04"), TEXT("Pinecrest"), TEXT("Highcliff"), 1, EGridlockColor::Yellow);
	AddRoute(TEXT("R05"), TEXT("Ironford"), TEXT("Highcliff"), 1, EGridlockColor::Gray);
	AddRoute(TEXT("R06"), TEXT("Ironford"), TEXT("Saltmarsh"), 2, EGridlockColor::Red);
	AddRoute(TEXT("R07"), TEXT("Ironford"), TEXT("Millbrook"), 1, EGridlockColor::Green);
	AddRoute(TEXT("R08"), TEXT("Saltmarsh"), TEXT("Millbrook"), 2, EGridlockColor::Purple);
	AddCurvedRoute(TEXT("R09"), TEXT("Pinecrest"), TEXT("Northbridge"), 3, 266, 82, EGridlockColor::Purple);
	AddRoute(TEXT("R10"), TEXT("Highcliff"), TEXT("Northbridge"), 2, EGridlockColor::Green);
	AddRoute(TEXT("R11"), TEXT("Ironford"), TEXT("Midway"), 2, EGridlockColor::Black);
	AddRoute(TEXT("R12"), TEXT("Millbrook"), TEXT("Midway"), 2, EGridlockColor::Yellow);
	AddRoute(TEXT("R13"), TEXT("Millbrook"), TEXT("Southbridge"), 1, EGridlockColor::Red);
	AddCurvedRoute(TEXT("R14"), TEXT("Saltmarsh"), TEXT("Southbridge"), 3, 266, 492, EGridlockColor::Black);
	AddRoute(TEXT("R15"), TEXT("Northbridge"), TEXT("Midway"), 1, EGridlockColor::Gray);
	AddRoute(TEXT("R16"), TEXT("Midway"), TEXT("Southbridge"), 1, EGridlockColor::Gray);
	AddRoute(TEXT("R17"), TEXT("Easton"), TEXT("Frostpeak"), 2, EGridlockColor::Red);
	AddRoute(TEXT("R18"), TEXT("Easton"), TEXT("Coralbay"), 2, EGridlockColor::Green);
	AddRoute(TEXT("R19"), TEXT("Easton"), TEXT("Copperton"), 2, EGridlockColor::Yellow);
	AddRoute(TEXT("R20"), TEXT("Frostpeak"), TEXT("Stonewatch"), 1, EGridlockColor::Yellow);
	AddRoute(TEXT("R21"), TEXT("Copperton"), TEXT("Stonewatch"), 1, EGridlockColor::Gray);
	AddRoute(TEXT("R22"), TEXT("Copperton"), TEXT("Coralbay"), 2, EGridlockColor::Red);
	AddRoute(TEXT("R23"), TEXT("Copperton"), TEXT("Vinemoor"), 1, EGridlockColor::Green);
	AddRoute(TEXT("R24"), TEXT("Coralbay"), TEXT("Vinemoor"), 2, EGridlockColor::Purple);
	AddCurvedRoute(TEXT("R25"), TEXT("Frostpeak"), TEXT("Northbridge"), 3, 494, 82, EGridlockColor::Purple);
	AddRoute(TEXT("R26"), TEXT("Stonewatch"), TEXT("Northbridge"), 2, EGridlockColor::Green);
	AddRoute(TEXT("R27"), TEXT("Copperton"), TEXT("Midway"), 2, EGridlockColor::Black);
	AddRoute(TEXT("R28"), TEXT("Vinemoor"), TEXT("Midway"), 2, EGridlockColor::Yellow);
	AddRoute(TEXT("R29"), TEXT("Vinemoor"), TEXT("Southbridge"), 1, EGridlockColor::Red);
	AddCurvedRoute(TEXT("R30"), TEXT("Coralbay"), TEXT("Southbridge"), 3, 494, 492, EGridlockColor::Black);

	MarkPackageDirty();
}

void UMapDefinition::FillWithMapV03()
{
	FillWithMapV01();

	// Roughly by distance on the map, with the start cities' routes kept short (3-4) so nobody is
	// stuck in their corner. Each west route's east mirror (R17-R30) gets the same length.
	const TMap<FName, int32> WestLengths = {
		{ TEXT("R01"), 3 }, { TEXT("R02"), 3 }, { TEXT("R03"), 4 }, { TEXT("R04"), 2 }, { TEXT("R05"), 1 },
		{ TEXT("R06"), 4 }, { TEXT("R07"), 2 }, { TEXT("R08"), 3 }, { TEXT("R09"), 9 }, { TEXT("R10"), 5 },
		{ TEXT("R11"), 6 }, { TEXT("R12"), 6 }, { TEXT("R13"), 5 }, { TEXT("R14"), 8 }, { TEXT("R15"), 7 }, { TEXT("R16"), 7 } };
	for (FRouteDef& Route : Routes)
	{
		const int32 Number = FCString::Atoi(*Route.Id.ToString().RightChop(1));
		const FName West = Number > 16 ? FName(*FString::Printf(TEXT("R%02d"), Number - 16)) : Route.Id;
		if (const int32* Length = WestLengths.Find(West))
		{
			Route.Length = *Length;
		}
	}
	MarkPackageDirty();
}

void UMapDefinition::FillWithMapV05()
{
	FillWithMapV03();
	using C = EGridlockColor;
	const TMap<FName, EGridlockColor> Colors = {
		{ TEXT("R01"), C::Red },    { TEXT("R02"), C::Green },  { TEXT("R03"), C::Yellow }, { TEXT("R04"), C::Blue },
		{ TEXT("R05"), C::Gray },   { TEXT("R06"), C::Orange }, { TEXT("R07"), C::White },  { TEXT("R08"), C::Purple },
		{ TEXT("R09"), C::Black },  { TEXT("R10"), C::Blue },   { TEXT("R11"), C::White },  { TEXT("R12"), C::Purple },
		{ TEXT("R13"), C::Orange }, { TEXT("R14"), C::Red },    { TEXT("R15"), C::Gray },   { TEXT("R16"), C::Gray },
		{ TEXT("R17"), C::Yellow }, { TEXT("R18"), C::Purple }, { TEXT("R19"), C::Red },    { TEXT("R20"), C::Blue },
		{ TEXT("R21"), C::Gray },   { TEXT("R22"), C::Purple }, { TEXT("R23"), C::White },  { TEXT("R24"), C::Green },
		{ TEXT("R25"), C::Green },  { TEXT("R26"), C::Black },  { TEXT("R27"), C::Blue },   { TEXT("R28"), C::Orange },
		{ TEXT("R29"), C::White },  { TEXT("R30"), C::Yellow } };
	for (FRouteDef& Route : Routes)
	{
		if (const EGridlockColor* Color = Colors.Find(Route.Id))
		{
			Route.Color = *Color;
		}
	}
	MarkPackageDirty();
}

void UMapDefinition::ValidateMap()
{
	const TArray<FString> Problems = GetProblems();
	if (Problems.IsEmpty())
	{
		UE_LOG(LogGridlock, Display, TEXT("%s: no problems (%d cities, %d routes)"), *GetName(), Cities.Num(), Routes.Num());
		return;
	}
	for (const FString& Problem : Problems)
	{
		UE_LOG(LogGridlock, Warning, TEXT("%s: %s"), *GetName(), *Problem);
	}
}

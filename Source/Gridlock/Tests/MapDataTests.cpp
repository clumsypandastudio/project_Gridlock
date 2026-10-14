#include "Misc/AutomationTest.h"
#include "Data/MapDefinition.h"
#include "Data/RouteCardDeck.h"
#include "Model/Pathfinding.h"

#if WITH_DEV_AUTOMATION_TESTS

// Checks the v0.1 map against the numbers on the Map tab.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockMapV01Test, "Gridlock.Data.MapV01",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockMapV01Test::RunTest(const FString& Parameters)
{
	UMapDefinition* Map = NewObject<UMapDefinition>();
	Map->FillWithMapV01();

	TestEqual(TEXT("City count"), Map->Cities.Num(), 15);
	TestEqual(TEXT("Route count"), Map->Routes.Num(), 30);

	int32 TotalDots = 0;
	for (const FRouteDef& Route : Map->Routes)
	{
		TotalDots += Route.Length;
	}
	TestEqual(TEXT("Total length dots"), TotalDots, 54);

	for (const FString& Problem : Map->GetProblems())
	{
		AddError(Problem);
	}

	TestEqual(TEXT("Westport to Easton"), FGridlockPathfinding::ShortestDistance(*Map, TEXT("Westport"), TEXT("Easton")), 8);
	TestEqual(TEXT("Westport to Midway"), FGridlockPathfinding::ShortestDistance(*Map, TEXT("Westport"), TEXT("Midway")), 4);

	// With Midway closed off, the west still reaches the east through the other bridges.
	auto AvoidMidway = [](const FRouteDef& Route) { return !Route.Touches(TEXT("Midway")); };
	TestTrue(TEXT("Crossing without Midway"),
		FGridlockPathfinding::ShortestDistance(*Map, TEXT("Westport"), TEXT("Easton"), AvoidMidway) != INDEX_NONE);

	TArray<FName> Path;
	FGridlockPathfinding::ShortestDistance(*Map, TEXT("Westport"), TEXT("Highcliff"), [](const FRouteDef&) { return true; }, &Path);
	TestEqual(TEXT("Westport to Highcliff takes 2 routes"), Path.Num(), 2);

	return true;
}

// Checks every Route card's points and type against its shortest-path distance.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockDeckV01Test, "Gridlock.Data.DeckV01",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockDeckV01Test::RunTest(const FString& Parameters)
{
	UMapDefinition* Map = NewObject<UMapDefinition>();
	Map->FillWithMapV01();
	URouteCardDeck* Deck = NewObject<URouteCardDeck>();
	Deck->FillWithDeckV01();

	TestEqual(TEXT("Card count"), Deck->Cards.Num(), 20);
	TestEqual(TEXT("Total points"), Deck->GetTotalPoints(), 132);

	for (const FRouteCardDef& Card : Deck->Cards)
	{
		const int32 Dots = FGridlockPathfinding::ShortestDistance(*Map, Card.CityA, Card.CityB);
		const ERouteCardType Expected =
			Dots >= 7 ? ERouteCardType::Long :
			Dots >= 5 ? ERouteCardType::Medium :
			ERouteCardType::Short;

		TestTrue(FString::Printf(TEXT("%s is at least 3 dots"), *Card.Id.ToString()), Dots >= 3);
		TestEqual(FString::Printf(TEXT("%s type matches its %d dots"), *Card.Id.ToString(), Dots),
			static_cast<int32>(Card.Type), static_cast<int32>(Expected));
	}

	return true;
}

// Map v0.3: the same routes with 1-9 pins; deck v0.3 scores each card by the fewest pins it takes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridlockMapV03Test, "Gridlock.Data.MapAndDeckV03",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGridlockMapV03Test::RunTest(const FString& Parameters)
{
	UMapDefinition* Map = NewObject<UMapDefinition>();
	Map->FillWithMapV03();
	for (const FString& Problem : Map->GetProblems())
	{
		AddError(Problem);
	}

	int32 TotalPins = 0;
	TSet<int32> Lengths;
	for (const FRouteDef& Route : Map->Routes)
	{
		TotalPins += Route.Length;
		Lengths.Add(Route.Length);
		const FCityDef* A = Map->FindCity(Route.CityA);
		const FCityDef* B = Map->FindCity(Route.CityB);
		const bool bStartRoute = (A && A->bIsStart) || (B && B->bIsStart);
		if (bStartRoute)
		{
			TestTrue(FString::Printf(TEXT("%s out of a start city is 3-4 pins"), *Route.Id.ToString()), Route.Length >= 3 && Route.Length <= 4);
		}
	}
	TestEqual(TEXT("136 pins in all"), TotalPins, 136);
	TestEqual(TEXT("Every length from 1 to 9 is used"), Lengths.Num(), 9);

	// Mirror routes match: R(n) and R(n+16) for the two halves.
	for (int32 West = 1; West <= 14; ++West)
	{
		const FRouteDef* A = Map->FindRoute(*FString::Printf(TEXT("R%02d"), West));
		const FRouteDef* B = Map->FindRoute(*FString::Printf(TEXT("R%02d"), West + 16));
		TestTrue(FString::Printf(TEXT("R%02d mirrors R%02d"), West, West + 16), A && B && A->Length == B->Length && A->Color == B->Color);
	}

	URouteCardDeck* Deck = NewObject<URouteCardDeck>();
	Deck->FillWithDeckForMap(*Map);
	FString Table;
	for (const FRouteCardDef& Card : Deck->Cards)
	{
		TestEqual(FString::Printf(TEXT("%s is worth its distance"), *Card.Id.ToString()),
			Card.Points, FGridlockPathfinding::ShortestDistance(*Map, Card.CityA, Card.CityB));
		Table += FString::Printf(TEXT("%s %s-%s %d; "), *Card.Id.ToString(), *Card.CityA.ToString(), *Card.CityB.ToString(), Card.Points);
	}
	AddInfo(FString::Printf(TEXT("DECKV03 total %d points: %s"), Deck->GetTotalPoints(), *Table));
	return true;
}

#endif

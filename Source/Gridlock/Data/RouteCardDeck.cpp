#include "Data/RouteCardDeck.h"
#include "Data/MapDefinition.h"
#include "Model/Pathfinding.h"

const FRouteCardDef* URouteCardDeck::FindCard(FName Id) const
{
	return Cards.FindByPredicate([Id](const FRouteCardDef& Card) { return Card.Id == Id; });
}

int32 URouteCardDeck::GetTotalPoints() const
{
	int32 Total = 0;
	for (const FRouteCardDef& Card : Cards)
	{
		Total += Card.Points;
	}
	return Total;
}

void URouteCardDeck::FillWithDeckV01()
{
	Modify();
	Cards.Reset();

	auto AddCard = [this](const TCHAR* Id, const TCHAR* A, const TCHAR* B, ERouteCardType Type)
	{
		FRouteCardDef& Card = Cards.AddDefaulted_GetRef();
		Card.Id = FName(Id);
		Card.CityA = FName(A);
		Card.CityB = FName(B);
		Card.Type = Type;
		Card.Points = Type == ERouteCardType::Long ? 11 : Type == ERouteCardType::Medium ? 7 : 4;
	};

	AddCard(TEXT("C01"), TEXT("Westport"), TEXT("Frostpeak"), ERouteCardType::Long);
	AddCard(TEXT("C02"), TEXT("Easton"), TEXT("Pinecrest"), ERouteCardType::Long);
	AddCard(TEXT("C03"), TEXT("Saltmarsh"), TEXT("Frostpeak"), ERouteCardType::Long);
	AddCard(TEXT("C04"), TEXT("Coralbay"), TEXT("Pinecrest"), ERouteCardType::Long);
	AddCard(TEXT("C05"), TEXT("Westport"), TEXT("Northbridge"), ERouteCardType::Medium);
	AddCard(TEXT("C06"), TEXT("Easton"), TEXT("Northbridge"), ERouteCardType::Medium);
	AddCard(TEXT("C07"), TEXT("Westport"), TEXT("Vinemoor"), ERouteCardType::Medium);
	AddCard(TEXT("C08"), TEXT("Easton"), TEXT("Millbrook"), ERouteCardType::Medium);
	AddCard(TEXT("C09"), TEXT("Highcliff"), TEXT("Copperton"), ERouteCardType::Medium);
	AddCard(TEXT("C10"), TEXT("Stonewatch"), TEXT("Ironford"), ERouteCardType::Medium);
	AddCard(TEXT("C11"), TEXT("Saltmarsh"), TEXT("Coralbay"), ERouteCardType::Medium);
	AddCard(TEXT("C12"), TEXT("Pinecrest"), TEXT("Frostpeak"), ERouteCardType::Medium);
	AddCard(TEXT("C13"), TEXT("Westport"), TEXT("Highcliff"), ERouteCardType::Short);
	AddCard(TEXT("C14"), TEXT("Easton"), TEXT("Stonewatch"), ERouteCardType::Short);
	AddCard(TEXT("C15"), TEXT("Pinecrest"), TEXT("Millbrook"), ERouteCardType::Short);
	AddCard(TEXT("C16"), TEXT("Frostpeak"), TEXT("Vinemoor"), ERouteCardType::Short);
	AddCard(TEXT("C17"), TEXT("Westport"), TEXT("Midway"), ERouteCardType::Short);
	AddCard(TEXT("C18"), TEXT("Easton"), TEXT("Midway"), ERouteCardType::Short);
	AddCard(TEXT("C19"), TEXT("Ironford"), TEXT("Vinemoor"), ERouteCardType::Short);
	AddCard(TEXT("C20"), TEXT("Copperton"), TEXT("Millbrook"), ERouteCardType::Short);

	MarkPackageDirty();
}


void URouteCardDeck::FillWithDeckV03()
{
	UMapDefinition* Map = NewObject<UMapDefinition>(GetTransientPackage());
	Map->FillWithMapV03();
	FillWithDeckForMap(*Map);
}

void URouteCardDeck::FillWithDeckForMap(const UMapDefinition& Map)
{
	FillWithDeckV01();
	for (FRouteCardDef& Card : Cards)
	{
		// Like Ticket to Ride: a card is worth roughly the routes it takes. Types split by distance.
		const int32 Pins = FGridlockPathfinding::ShortestDistance(Map, Card.CityA, Card.CityB);
		Card.Points = FMath::Max(1, Pins);
		Card.Type = Pins >= 14 ? ERouteCardType::Long : Pins >= 9 ? ERouteCardType::Medium : ERouteCardType::Short;
	}
	MarkPackageDirty();
}

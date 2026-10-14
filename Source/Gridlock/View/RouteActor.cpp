#include "View/RouteActor.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/MapDefinition.h"
#include "Engine/StaticMesh.h"
#include "Model/RulesLibrary.h"
#include "UObject/ConstructorHelpers.h"
#include "View/BoardLayout.h"

ARouteActor::ARouteActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	CubeMesh = Cube.Object;
	SphereMesh = Sphere.Object;
}

void ARouteActor::Setup(const FRouteDef& Route, const UMapDefinition& Map)
{
	RouteId = Route.Id;
	RouteColor = GridlockBoard::TicketColor(Route.Color);
	const FCityDef* CityA = Map.FindCity(Route.CityA);
	const FCityDef* CityB = Map.FindCity(Route.CityB);
	if (!CityA || !CityB)
	{
		return;
	}

	auto PointAt = [&Route, &Map](double T)
	{
		return GridlockBoard::RoutePoint(Route, Map, T); // to one side for a double route's track
	};

	// The polyline, with its ends pulled back so the line stops at the city's edge.
	TArray<FVector2D> Points;
	const int32 Steps = Route.bCurved ? 12 : 1;
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		Points.Add(PointAt(static_cast<double>(Step) / Steps));
	}
	Points[0] += (Points[1] - Points[0]).GetSafeNormal() * GridlockBoard::CityGap;
	Points.Last() += (Points[Points.Num() - 2] - Points.Last()).GetSafeNormal() * GridlockBoard::CityGap;

	BaseColor = RouteColor;
	Material = GridlockBoard::MakeColorMaterial(this, RouteColor);
	for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
	{
		AddSegment(GridlockBoard::ToWorld(Points[Index], 2.0), GridlockBoard::ToWorld(Points[Index + 1], 2.0));
	}

	// One pin per ticket it takes, split around the slot at the midpoint (the extra one goes on the
	// first half) and spread evenly over each half.
	const int32 Left = (Route.Length + 1) / 2;
	const int32 Right = Route.Length / 2;
	auto Spread = [](int32 Index, int32 Count, double From, double To)
	{
		return Count == 1 ? (From + To) * 0.5 : From + (To - From) * Index / (Count - 1);
	};
	for (int32 Index = 0; Index < Left; ++Index)
	{
		AddDot(GridlockBoard::ToWorld(PointAt(Spread(Index, Left, 0.1, 0.4)), 4.0));
	}
	for (int32 Index = 0; Index < Right; ++Index)
	{
		AddDot(GridlockBoard::ToWorld(PointAt(Spread(Index, Right, 0.6, 0.9)), 4.0));
	}

	SlotLocation = GridlockBoard::ToWorld(PointAt(0.5), 0.0);
}

void ARouteActor::AddSegment(const FVector& From, const FVector& To)
{
	const FVector Middle = (From + To) * 0.5;
	const FVector Direction = To - From;
	const FRotator Rotation = Direction.Rotation();
	const double Length = Direction.Size();

	UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
	Piece->SetStaticMesh(CubeMesh);
	Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Piece->SetupAttachment(Root);
	Piece->RegisterComponent();
	Piece->SetWorldLocationAndRotation(Middle, Rotation);
	Piece->SetWorldScale3D(FVector(Length / 100.0, 0.07, 0.03));
	Piece->SetMaterial(0, Material);
	Pieces.Add(Piece);
	Segments.Add(Piece);
	SegmentLengths.Add(Length);

	// The line is only a few pixels wide on screen, so clicks hit a wider invisible box.
	UBoxComponent* Box = NewObject<UBoxComponent>(this);
	Box->SetupAttachment(Root);
	Box->RegisterComponent();
	Box->SetWorldLocationAndRotation(Middle, Rotation);
	Box->SetBoxExtent(FVector(Length * 0.5, 14.0, 8.0));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	ClickBoxes.Add(Box);
}

void ARouteActor::AddDot(const FVector& At)
{
	UStaticMeshComponent* Dot = NewObject<UStaticMeshComponent>(this);
	Dot->SetStaticMesh(SphereMesh);
	Dot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dot->SetupAttachment(Root);
	Dot->RegisterComponent();
	Dot->SetWorldLocation(At);
	Dot->SetWorldScale3D(FVector(0.1));
	Dot->SetMaterial(0, Material);
	Pieces.Add(Dot);
	Dots.Add(Dot);
}

void ARouteActor::OnBoardStateChanged_Implementation(const FBoardState& State)
{
	const int32* RouteOwner = State.RouteOwner.Find(RouteId);
	if (!RouteOwner || *RouteOwner == INDEX_NONE)
	{
		BaseColor = RouteColor; // unclaimed routes show the ticket color that pays for them
	}
	else
	{
		BaseColor = GridlockBoard::PlayerColor(*RouteOwner);
		if (URulesLibrary::IsRouteSevered(State, RouteId, *RouteOwner))
		{
			BaseColor *= 0.3f; // severed: still owned, but it doesn't count toward Route cards
		}
	}
	ApplyLook();
}

void ARouteActor::SetHighlight(EBoardHighlight InHighlight)
{
	if (Highlight != InHighlight)
	{
		Highlight = InHighlight;
		ApplyLook();
	}
}

void ARouteActor::ApplyLook()
{
	const bool bClickable = Highlight == EBoardHighlight::Clickable;
	const bool bReady = Highlight == EBoardHighlight::Ready;
	// A route keeps its own color (it's the ticket color that pays for it): being buildable shows as a
	// thicker line with bigger pins, never as a different color.
	GridlockBoard::SetColor(Material, Highlight == EBoardHighlight::Dimmed ? BaseColor * 0.4f : BaseColor);

	const double Thickness = bClickable ? 0.16 : bReady ? 0.13 : 0.07;
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		Segments[Index]->SetWorldScale3D(FVector(SegmentLengths[Index] / 100.0, Thickness, 0.03));
	}
	for (UStaticMeshComponent* Dot : Dots)
	{
		Dot->SetWorldScale3D(FVector(bClickable || bReady ? 0.16 : 0.1));
	}
}

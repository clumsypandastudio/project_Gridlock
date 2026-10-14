#include "View/CityActor.h"
#include "Components/StaticMeshComponent.h"
#include "Data/MapDefinition.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "View/BoardLayout.h"

ACityActor::ACityActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Mesh->SetStaticMesh(Cylinder.Object);
}

void ACityActor::Setup(const FCityDef& City)
{
	CityId = City.Id;
	bIsBridgeTown = City.bIsBridgeTown;

	// The engine cylinder is 100 units wide and tall, centered on its pivot.
	const double Diameter = City.bIsStart ? 0.5 : bIsBridgeTown ? 0.44 : 0.36;
	Mesh->SetWorldScale3D(FVector(Diameter, Diameter, 0.2));
	SetActorLocation(GridlockBoard::ToWorld(City.Position, 10.0));

	Material = GridlockBoard::MakeColorMaterial(this, bIsBridgeTown ? GridlockBoard::BridgeTown : GridlockBoard::City);
	Mesh->SetMaterial(0, Material);
}

void ACityActor::OnBoardStateChanged_Implementation(const FBoardState& State)
{
	FLinearColor Color = bIsBridgeTown ? GridlockBoard::BridgeTown : GridlockBoard::City;
	for (int32 Player = 0; Player < State.Players.Num(); ++Player)
	{
		if (State.Players[Player].StartCity == CityId)
		{
			Color = GridlockBoard::PlayerColor(Player);
		}
	}
	GridlockBoard::SetColor(Material, Color);
}

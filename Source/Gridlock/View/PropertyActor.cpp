#include "View/PropertyActor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "View/BoardLayout.h"

APropertyActor::APropertyActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Mesh->SetStaticMesh(Cube.Object);
}

void APropertyActor::Setup(FName InRouteId, const FVector& Location)
{
	RouteId = InRouteId;
	SetActorLocation(Location);
	Material = GridlockBoard::MakeColorMaterial(this, GridlockBoard::EmptySlot);
	Mesh->SetMaterial(0, Material);
	OnSlotChanged(FSlotState());
}

void APropertyActor::SetHighlight(EBoardHighlight InHighlight)
{
	if (Highlight != InHighlight)
	{
		Highlight = InHighlight;
		OnSlotChanged(LastSlot);
	}
}

void APropertyActor::OnSlotChanged_Implementation(const FSlotState& Slot)
{
	LastSlot = Slot;

	// Footprint and height as fractions of the 100-unit engine cube.
	FVector Size(0.22, 0.22, 0.03);
	FLinearColor Color = GridlockBoard::EmptySlot;

	if (Slot.RubbleTicks > 0)
	{
		Size = FVector(0.26, 0.26, 0.06);
		Color = GridlockBoard::Rubble;
	}
	else if (Slot.Owner != INDEX_NONE)
	{
		switch (Slot.Tier)
		{
		case EPropertyTier::Lot:      Size = FVector(0.24, 0.24, 0.12); break;
		case EPropertyTier::Building: Size = FVector(0.26, 0.26, 0.32); break;
		case EPropertyTier::Complex:  Size = FVector(0.3, 0.3, 0.55); break;
		case EPropertyTier::Landmark: Size = FVector(0.32, 0.32, 1.0); break;
		default: break;
		}
		Color = GridlockBoard::PlayerColor(Slot.Owner);
		if (Slot.bUpgrading)
		{
			Color = FMath::Lerp(Color, FLinearColor::White, 0.55f);
		}
	}

	if (Highlight == EBoardHighlight::Clickable)
	{
		Color = Slot.Owner == INDEX_NONE ? GridlockBoard::Clickable : FMath::Lerp(Color, GridlockBoard::Clickable, 0.75f);
		Size.X = FMath::Max(Size.X * 1.3, 0.32);
		Size.Y = FMath::Max(Size.Y * 1.3, 0.32);
		Size.Z = FMath::Max(Size.Z, 0.08);
	}

	Mesh->SetRelativeScale3D(Size);
	Mesh->SetRelativeLocation(FVector(0.0, 0.0, Size.Z * 50.0));
	GridlockBoard::SetColor(Material, Color);
}

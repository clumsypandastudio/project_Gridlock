#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Model/GridlockTypes.h"
#include "View/BoardLayout.h"
#include "PropertyActor.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

/** Shows the property slot at a route's midpoint. Visuals only: it never runs rules. */
UCLASS()
class GRIDLOCK_API APropertyActor : public AActor
{
	GENERATED_BODY()

public:
	APropertyActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gridlock")
	FName RouteId;

	/** Called once by the game mode after spawning. */
	virtual void Setup(FName InRouteId, const FVector& Location);

	/** The C++ version is a placeholder block: taller per tier, owner-colored, pale while upgrading. */
	UFUNCTION(BlueprintNativeEvent, Category = "Gridlock")
	void OnSlotChanged(const FSlotState& Slot);

	/** Gold and bigger when you can click it now (place, upgrade or remove). */
	void SetHighlight(EBoardHighlight InHighlight);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gridlock")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

private:
	FSlotState LastSlot;
	EBoardHighlight Highlight = EBoardHighlight::None;
};

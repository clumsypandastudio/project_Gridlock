#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Model/GridlockTypes.h"
#include "View/BoardLayout.h"
#include "RouteActor.generated.h"

class UBoxComponent;
class UMapDefinition;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FRouteDef;

/** Shows one route as a line with its length dots, plus a wide invisible box to click. Visuals only. */
UCLASS()
class GRIDLOCK_API ARouteActor : public AActor
{
	GENERATED_BODY()

public:
	ARouteActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gridlock")
	FName RouteId;

	/** Builds the line, dots and click boxes. Called once by the game mode after spawning. */
	virtual void Setup(const FRouteDef& Route, const UMapDefinition& Map);

	/** Where this route's property slot sits, at the route's midpoint. */
	FVector GetSlotLocation() const { return SlotLocation; }

	/** Called whenever the board state changes. The C++ version colors the route by its owner. */
	UFUNCTION(BlueprintNativeEvent, Category = "Gridlock")
	void OnBoardStateChanged(const FBoardState& State);

	/** Thicker, with bigger pins, when you can build it (always in its own color); darker when it isn't an option this step. */
	void SetHighlight(EBoardHighlight InHighlight);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gridlock")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Pieces;

	UPROPERTY()
	TArray<TObjectPtr<UBoxComponent>> ClickBoxes;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Segments;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Dots;

private:
	void AddSegment(const FVector& From, const FVector& To);
	void AddDot(const FVector& At);
	void ApplyLook();

	FVector SlotLocation = FVector::ZeroVector;
	TArray<double> SegmentLengths;
	FLinearColor BaseColor = FLinearColor::Black;
	FLinearColor RouteColor = FLinearColor::Gray;
	EBoardHighlight Highlight = EBoardHighlight::None;
};

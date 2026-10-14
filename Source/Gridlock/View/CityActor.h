#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Model/GridlockTypes.h"
#include "CityActor.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FCityDef;

/** Shows one city. Visuals only: it never runs rules. */
UCLASS()
class GRIDLOCK_API ACityActor : public AActor
{
	GENERATED_BODY()

public:
	ACityActor();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gridlock")
	FName CityId;

	/** Places and sizes the city. Called once by the game mode after spawning. */
	virtual void Setup(const FCityDef& City);

	/** Called whenever the board state changes. The C++ version colors start cities by player. */
	UFUNCTION(BlueprintNativeEvent, Category = "Gridlock")
	void OnBoardStateChanged(const FBoardState& State);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gridlock")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> Material;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gridlock")
	bool bIsBridgeTown = false;
};

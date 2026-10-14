#include "View/BoardLayout.h"
#include "Data/MapDefinition.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace GridlockBoard
{
	static const TCHAR* BasicShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	static const FName ColorParameter(TEXT("Color"));

	FLinearColor PlayerColor(int32 Player)
	{
		switch (Player)
		{
		// Cyan and Magenta: no route uses either, so a claimed route never looks like an open one.
		case 0:  return FLinearColor(0.0f, 0.85f, 0.95f);
		case 1:  return FLinearColor(1.0f, 0.1f, 0.7f);
		default: return FLinearColor(0.5f, 0.5f, 0.5f);
		}
	}

	FString PlayerName(int32 Player)
	{
		return Player == 0 ? TEXT("Cyan") : Player == 1 ? TEXT("Magenta") : TEXT("Nobody");
	}

	UMaterialInstanceDynamic* MakeColorMaterial(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BasicShapeMaterialPath);
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Outer);
		SetColor(Material, Color);
		return Material;
	}

	FLinearColor TicketColor(EGridlockColor Color)
	{
		switch (Color)
		{
		case EGridlockColor::Red:    return FLinearColor(0.8f, 0.06f, 0.06f);
		case EGridlockColor::Orange: return FLinearColor(1.0f, 0.42f, 0.0f);
		case EGridlockColor::Yellow: return FLinearColor(0.95f, 0.85f, 0.08f);
		case EGridlockColor::Green:  return FLinearColor(0.3f, 0.85f, 0.15f);
		case EGridlockColor::Blue:   return FLinearColor(0.08f, 0.25f, 0.95f);
		case EGridlockColor::Purple: return FLinearColor(0.45f, 0.12f, 0.75f);
		case EGridlockColor::Black:  return FLinearColor(0.02f, 0.02f, 0.02f);
		case EGridlockColor::White:  return FLinearColor(0.93f, 0.93f, 0.93f);
		case EGridlockColor::Wild:   return FLinearColor(0.95f, 0.72f, 0.25f); // gold, like a locomotive
		case EGridlockColor::Lot:    return FLinearColor(0.5f, 0.32f, 0.14f);  // brown, like land
		default:                     return FLinearColor(0.5f, 0.5f, 0.5f);
		}
	}

	FString TicketColorName(EGridlockColor Color)
	{
		switch (Color)
		{
		case EGridlockColor::Red:    return TEXT("Red");
		case EGridlockColor::Orange: return TEXT("Orange");
		case EGridlockColor::Yellow: return TEXT("Yellow");
		case EGridlockColor::Green:  return TEXT("Green");
		case EGridlockColor::Blue:   return TEXT("Blue");
		case EGridlockColor::Purple: return TEXT("Purple");
		case EGridlockColor::Black:  return TEXT("Black");
		case EGridlockColor::White:  return TEXT("White");
		case EGridlockColor::Wild:   return TEXT("Wild");
		case EGridlockColor::Lot:    return TEXT("Lot");
		default:                     return TEXT("Gray");
		}
	}

	FVector RouteMidpoint(const FRouteDef& Route, const UMapDefinition& Map, double Z)
	{
		const FCityDef* A = Map.FindCity(Route.CityA);
		const FCityDef* B = Map.FindCity(Route.CityB);
		if (!A || !B)
		{
			return FVector::ZeroVector;
		}
		const FVector2D Middle = Route.bCurved
			? QuadraticPoint(A->Position, Route.CurveControl, B->Position, 0.5)
			: (A->Position + B->Position) * 0.5;
		return ToWorld(Middle, Z);
	}

	void SetColor(UMaterialInstanceDynamic* Material, const FLinearColor& Color)
	{
		if (Material)
		{
			Material->SetVectorParameterValue(ColorParameter, Color);
		}
	}
}

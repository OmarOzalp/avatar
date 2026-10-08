#include "Sandbox/BendingSandboxLibrary.h"

#include "BendingLog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Data/BendingDiscipline.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sandbox/BendingTechniqueAbility.h"
#include "Sandbox/BendingTechniqueMove.h"
#include "Sandbox/BendingTechniqueTypes.h"

namespace
{
	const TCHAR* BasicShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	const FName ColorParameterName(TEXT("Color"));
}

TArray<UBendingDiscipline*> UBendingSandboxLibrary::CreateTechniqueDisciplines(UObject* Outer)
{
	TArray<UBendingDiscipline*> Disciplines;
	if (!Outer)
	{
		return Disciplines;
	}

	static constexpr EBendingElement Elements[] = { EBendingElement::Water, EBendingElement::Earth, EBendingElement::Fire, EBendingElement::Air };
	for (const EBendingElement Element : Elements)
	{
		UBendingDiscipline* Discipline = NewObject<UBendingDiscipline>(Outer);
		Discipline->Element = Element;

		for (int32 Index = 1; Index < static_cast<int32>(EBendingTechnique::Count); ++Index)
		{
			const EBendingTechnique Technique = static_cast<EBendingTechnique>(Index);
			const BendingSim::FTechniqueInfo& Info = BendingTechnique::GetInfo(Technique);
			if (BendingTechnique::ToElement(Info.Element) != Element)
			{
				continue;
			}

			UBendingTechniqueMove* Move = NewObject<UBendingTechniqueMove>(Outer);
			Move->Technique = Technique;
			Move->DisplayName = FText::FromString(BendingTechnique::GetDisplayName(Technique));
			Move->Element = Element;
			Move->InputTag = BendingTechnique::GetInputTag(Info.Slot);
			Move->AbilityClass = UBendingTechniqueAbility::StaticClass();
			Move->FrameData.StartupFrames = FMath::Max(Info.StartupFrames, 1);
			Move->FrameData.ActiveFrames = FMath::Max(Info.ActiveFrames, 1);
			Move->FrameData.RecoveryFrames = FMath::Max(Info.RecoveryFrames, 0);
			// Late recovery is cancellable, so techniques chain without waiting out every frame.
			Move->FrameData.RecoveryCancelFrame = FMath::RoundToInt(0.6f * static_cast<float>(Move->FrameData.RecoveryFrames));
			Move->FrameData.bCancelOnHit = true;
			Move->bUseMotionWarping = false;
			Move->ChiCost = static_cast<float>(Info.ChiCost);
			Move->StaminaCost = static_cast<float>(Info.StaminaCost);
			Discipline->Moves.Add(Move);
		}
		Disciplines.Add(Discipline);
	}
	return Disciplines;
}

FLinearColor UBendingSandboxLibrary::GetElementColor(EBendingElement Element)
{
	switch (Element)
	{
	case EBendingElement::Water: return FromSRGB(40, 110, 220);
	case EBendingElement::Earth: return FromSRGB(60, 140, 60);
	case EBendingElement::Fire:  return FromSRGB(210, 50, 30);
	case EBendingElement::Air:   return FromSRGB(235, 220, 140);
	default:                     return FromSRGB(150, 150, 150);
	}
}

UMaterialInstanceDynamic* UBendingSandboxLibrary::SetMeshColor(UPrimitiveComponent* Mesh, FLinearColor Color)
{
	if (!Mesh)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!Material)
	{
		UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, BasicShapeMaterialPath);
		if (!BaseMaterial)
		{
			UE_LOG(LogBending, Warning, TEXT("%s not found; %s keeps its default material."), BasicShapeMaterialPath, *Mesh->GetName());
		}
		// Without a base material this instances the mesh's current one, which may simply ignore "Color".
		Material = Mesh->CreateDynamicMaterialInstance(0, BaseMaterial);
	}
	if (Material)
	{
		Material->SetVectorParameterValue(ColorParameterName, Color);
	}
	return Material;
}

UStaticMesh* UBendingSandboxLibrary::LoadBasicShape(const TCHAR* ShapeName)
{
	const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), ShapeName, ShapeName);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Mesh)
	{
		UE_LOG(LogBending, Warning, TEXT("Engine basic shape %s not found."), *Path);
	}
	return Mesh;
}

void UBendingSandboxLibrary::SetInstanceTransforms(UInstancedStaticMeshComponent* Instances, const TArray<FTransform>& WorldTransforms)
{
	if (!Instances)
	{
		return;
	}
	if (Instances->GetInstanceCount() == WorldTransforms.Num())
	{
		if (WorldTransforms.Num() > 0)
		{
			Instances->BatchUpdateInstancesTransforms(0, WorldTransforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		}
		return;
	}
	Instances->ClearInstances();
	if (WorldTransforms.Num() > 0)
	{
		Instances->AddInstances(WorldTransforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
	}
}

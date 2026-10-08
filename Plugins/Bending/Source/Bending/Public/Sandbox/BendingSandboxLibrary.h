#pragma once

#include "CoreMinimal.h"
#include "BendingTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BendingSandboxLibrary.generated.h"

class UBendingDiscipline;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UStaticMesh;

/**
 * Helpers for the asset-free sandbox: moves built from the kernel's technique table, and the engine's basic shapes
 * tinted through BasicShapeMaterial, which is all the art the sandbox uses.
 */
UCLASS()
class BENDING_API UBendingSandboxLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * One discipline per element, holding one UBendingTechniqueMove (run by UBendingTechniqueAbility, no montage) per
	 * kernel technique with the kernel's frame data and costs. Created as runtime objects inside Outer: keep the
	 * returned array referenced for as long as the disciplines are granted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bending|Sandbox")
	static TArray<UBendingDiscipline*> CreateTechniqueDisciplines(UObject* Outer);

	/** Shirt colour of a stance (linear). */
	UFUNCTION(BlueprintPure, Category = "Bending|Sandbox")
	static FLinearColor GetElementColor(EBendingElement Element);

	/**
	 * Tints a mesh with a dynamic instance of /Engine/BasicShapes/BasicShapeMaterial (vector parameter "Color"),
	 * reusing the instance the mesh already has.
	 */
	UFUNCTION(BlueprintCallable, Category = "Bending|Sandbox")
	static UMaterialInstanceDynamic* SetMeshColor(UPrimitiveComponent* Mesh, FLinearColor Color);

	/** /Engine/BasicShapes mesh (Sphere, Cube, Cylinder): 100 cm across, pivot at the centre. Null with a warning if missing. */
	static UStaticMesh* LoadBasicShape(const TCHAR* ShapeName);

	/** Makes an instanced mesh show exactly these world-space transforms, reusing instances when the count is unchanged. */
	static void SetInstanceTransforms(UInstancedStaticMeshComponent* Instances, const TArray<FTransform>& WorldTransforms);

	/** Linear colour from an sRGB pick. */
	[[nodiscard]] static FLinearColor FromSRGB(uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); }

	[[nodiscard]] static FLinearColor LerpColor(const FLinearColor& A, const FLinearColor& B, float Alpha) { return A + (B - A) * Alpha; }
};

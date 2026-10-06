#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment_Data.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment_Data.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkGymStation_VisibleRuntime.generated.h"

class UWorld;
class UCk_EntityScript_UE;

UENUM(BlueprintType)
enum class ECkGym_StationVisibleRole : uint8
{
    BackWall,
    FloorStage,
    LeftTrim,
    RightTrim,
    TopTrim,
    BottomTrim,
    TitleText,
    DescriptionText,
    FloorDescriptionText,
    Spotlight,
};

// Value-only observation. Neither UObject pointers nor ECS handles cross the wire.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkGym_StationVisiblePiece
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) ECkGym_StationVisibleRole Role = ECkGym_StationVisibleRole::BackWall;
    UPROPERTY(BlueprintReadOnly) FTransform NodeWorldTransform = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly) FTransform ComponentWorldTransform = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly) bool Visible = false;
    UPROPERTY(BlueprintReadOnly) bool HiddenInGame = false;

    UPROPERTY(BlueprintReadOnly) FString MeshAssetPath;
    UPROPERTY(BlueprintReadOnly) FString ParentMaterialPath;
    UPROPERTY(BlueprintReadOnly) FString BaseMaterialPath;
    UPROPERTY(BlueprintReadOnly) FLinearColor MeshColor = FLinearColor::Transparent;
    UPROPERTY(BlueprintReadOnly) int32 CollisionEnabled = 0;

    UPROPERTY(BlueprintReadOnly) FString Text;
    UPROPERTY(BlueprintReadOnly) FString FontPath;
    UPROPERTY(BlueprintReadOnly) FString TextMaterialPath;
    UPROPERTY(BlueprintReadOnly) FVector ShownTextLocalSize = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) float TextWorldSize = 0.0f;
    UPROPERTY(BlueprintReadOnly) float TextXScale = 0.0f;
    UPROPERTY(BlueprintReadOnly) float TextYScale = 0.0f;
    UPROPERTY(BlueprintReadOnly) float TextHorizontalSpacing = 0.0f;
    UPROPERTY(BlueprintReadOnly) float TextVerticalSpacing = 0.0f;
    UPROPERTY(BlueprintReadOnly) FColor TextColor = FColor::Transparent;
    UPROPERTY(BlueprintReadOnly) int32 HorizontalAlignment = 0;
    UPROPERTY(BlueprintReadOnly) int32 VerticalAlignment = 0;

    UPROPERTY(BlueprintReadOnly) float SpotIntensity = 0.0f;
    UPROPERTY(BlueprintReadOnly) float SpotAttenuationRadius = 0.0f;
    UPROPERTY(BlueprintReadOnly) float SpotInnerConeAngle = 0.0f;
    UPROPERTY(BlueprintReadOnly) float SpotOuterConeAngle = 0.0f;
    UPROPERTY(BlueprintReadOnly) FColor SpotColor = FColor::Transparent;
    UPROPERTY(BlueprintReadOnly) int32 SpotIntensityUnits = 0;
};

UCLASS()
class CKTESTS_API UCk_GymStation_VisibleRuntime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Exact named role, ownership, bridge, world, type and applied component style.
    // Failure clears OutPiece and never publishes a partial row.
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gym|Visible")
    static bool TryCapture_VisiblePiece(
        const FCk_Handle& InStation,
        const FCk_Handle_SceneNode& InNode,
        const FCk_Handle_UnrealComponent& InComponentHandle,
        ECkGym_StationVisibleRole InRole,
        UWorld* InWorld,
        FCkGym_StationVisiblePiece& OutPiece);

    // Non-ensuring entity identity for an actual live EntityScript instance.
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Gym|Visible")
    static FCk_Handle TryGet_StationEntity(const UCk_EntityScript_UE* InScript);
};

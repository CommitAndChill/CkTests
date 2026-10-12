#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "CkUnrealComponentGym_Runtime.generated.h"

class AActor;
class UActorComponent;

// Value-only observation of one authored authority component. A false return
// clears the output. A true return can report bStyleValid=false without
// endorsing the component as an authored visual.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkUnrealComponentGym_SourceSample
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 Role = -1;
    UPROPERTY(BlueprintReadOnly) FName ActualComponentClass = NAME_None;
    UPROPERTY(BlueprintReadOnly) FTransform ComponentWorldPose = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly) bool bStyleValid = false;
    UPROPERTY(BlueprintReadOnly) bool bTransformQueueEmpty = false;
};

UCLASS()
class CKTESTS_API UCk_UnrealComponentGym_Runtime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // First OnAdded capture: validates the actual chain before styling has
    // been configured. The caller retains these exact returned identities.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|UnrealComponent")
    static bool TryCaptureSourceComponent(
        AActor* ExpectedSourceActor,
        const FCk_Handle& RawEntity,
        const FCk_Handle& RawComponent,
        int32 Role,
        UActorComponent*& OutComponent,
        AActor*& OutHostActor);

    // Role order matches CkUnrealComponentGym_Driver.as: mesh, point, spot,
    // text, arrow, box, sphere, capsule. No allocations, world writes, or
    // component-host creation. The caller supplies retained exact identities.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|UnrealComponent")
    static bool TryGetSourceSample(
        AActor* ExpectedSourceActor,
        AActor* ExpectedHostActor,
        UActorComponent* ExpectedComponent,
        const FCk_Handle& RawEntity,
        const FCk_Handle& RawComponent,
        int32 Role,
        FCkUnrealComponentGym_SourceSample& OutSample);
};

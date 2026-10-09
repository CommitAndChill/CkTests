#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkGym_AdmissionRuntime.generated.h"

// Stored in CkDynamic named-fragment storage on the exact transient world root.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkGym_StationAdmission
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) FCk_Handle ActiveOwner;
    UPROPERTY(BlueprintReadWrite) int32 ReservationTicket = 0;
    UPROPERTY(BlueprintReadWrite) int32 ActiveTicket = 0;
    UPROPERTY(BlueprintReadWrite) int32 NextTicket = 0;
    UPROPERTY(BlueprintReadWrite) bool ClosedUnproven = false;
};

UCLASS()
class CKTESTS_API UCkGym_AdmissionRuntime_UE : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|Admission")
    static bool Get_IsClosedUnproven(const FCk_Handle& InWorldRoot);

    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|Admission")
    static bool TryCloseWorldUnproven(const FCk_Handle& InWorldRoot);
};

#pragma once

#include "CkEcs/EntityScript/CkEntityScript.h"
#include "Test_EntityScript_CapturedAddProbe.generated.h"

USTRUCT()
struct FCk_Test_EntityScript_CapturedAddParams
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Marker = 0;
};

UCLASS()
class UCk_Test_EntityScript_CapturedAddProbe_UE final : public UCk_EntityScript_UE
{
    GENERATED_BODY()
public:
    UPROPERTY()
    int32 Marker = 0;

    static int32 ConstructCalls;
    static int32 LastConstructMarker;
    static int32 LastPayloadMarker;

    virtual auto Get_EffectiveReplication() const -> ECk_Replication override;
    virtual auto Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
        -> ECk_EntityScript_ConstructionFlow override;
};

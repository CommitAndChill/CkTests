#pragma once

#include "CkSceneNodeGym_ActorLedger.h"
#include "CkSceneNode_ActorLedgerProbe.generated.h"

class UStaticMeshComponent;

UCLASS()
class ACk_SceneNode_ActorLedgerProbe final : public ACk_SceneNodeGym_SourceActor_UE
{
    GENERATED_BODY()
public:
    ACk_SceneNode_ActorLedgerProbe();
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Cube;

    bool bObservedNativeClaimAtBeginPlay = false;
    int32 BeginPlayCount = 0;
    int32 CapturedCountAtBeginPlay = -1;
};

// A frozen visual class whose CDO violates the nonreplicated recipe.
UCLASS()
class ACk_SceneNode_ActorLedgerReplicatedProbe final : public ACk_SceneNodeGym_SourceActor_UE
{
    GENERATED_BODY()
public:
    ACk_SceneNode_ActorLedgerReplicatedProbe();
};

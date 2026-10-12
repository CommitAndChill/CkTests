#pragma once

#include "CkTests/Net/Probes/CkActorRelay_TestProbe.h"
#include "Test_PendingRendezvous_RelaySubject.generated.h"

// Exact-class test subject; warm relay channels use the base class.
// Inherits the real replicated ActorRelay/WithActor bridge unchanged.
UCLASS()
class ACk_Test_PendingRendezvous_RelaySubject_UE final : public ACk_ActorRelay_TestProbe_UE
{
    GENERATED_BODY()

public:
    ACk_Test_PendingRendezvous_RelaySubject_UE();
};

#pragma once

#include "CkCrowd/Debug/CkCrowd_RemoteDebugRegistry.h"

#include "Test_CrowdRemoteDebug_RegistryReentryProbe.generated.h"

// A world-owned native provider whose IsForWorld callback can change its own registration.
UCLASS()
class UCkTest_CrowdRemoteDebug_RegistryReentryProbe final : public UObject,
    public ICkCrowdRemoteDebugProvider
{
    GENERATED_BODY()

public:
    TWeakObjectPtr<UWorld> BoundWorld;
    mutable int32 Mode = 0; // 0 ordinary, 1 unregister, 2 replace, 3 mark owner garbage
    mutable bool bInCallback = false;
    mutable bool bReentrySucceeded = false;
    mutable FCkCrowdRemoteProviderToken ActiveToken;
    mutable FCkCrowdRemoteProviderToken ReplacementToken;
    mutable int32 RowCalls = 0;
    int32 SubmitCalls = 0;

    virtual auto GetWorld() const -> UWorld* override;
    virtual auto IsForWorld(const UWorld& World) const -> bool override;
    virtual auto CopyRows(const UWorld& World, const APlayerController& RequestingPC,
        const ULocalPlayer& RequestingLocalPlayer,
        TArray<FCkCrowdRemoteAgentRow>& Out) const -> bool override;
    virtual auto RequestSelectionFromOwningLocalController(const UWorld& World,
        APlayerController& RequestingPC, const ULocalPlayer& RequestingLocalPlayer,
        const FCkCrowdRemoteAgentId& Id, int64 SelectionRequestSerial, bool bClear) -> bool override;
    virtual auto CopySelectedDetail(const UWorld& World, const APlayerController& RequestingPC,
        const ULocalPlayer& RequestingLocalPlayer, const FCkCrowdRemoteAgentId& Id,
        int64 SelectionRequestSerial, FCkCrowdRemoteAgentDetail& Out) const -> bool override;
    virtual auto SubmitFromOwningLocalController(const UWorld& World,
        APlayerController& RequestingPC, const ULocalPlayer& RequestingLocalPlayer,
        const FCkCrowdRemoteCommandRequest& Request) -> bool override;
    virtual auto CopyCommandResult(const UWorld& World, const APlayerController& RequestingPC,
        const ULocalPlayer& RequestingLocalPlayer, int64 ClientSerial,
        FCkCrowdRemoteCommandResult& Out) const -> bool override;
};

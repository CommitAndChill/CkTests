#pragma once

#include "CkCrowd/Debug/CkCrowd_RemoteDebugRegistry.h"

#include "Test_CrowdRemoteDebug_ProviderProbe.generated.h"

// Native definitions remain linkable in non-editor targets; only the automation body is guarded.
UCLASS()
class UCkTest_CrowdRemoteDebug_ProviderProbe final : public UObject, public ICkCrowdRemoteDebugProvider
{
    GENERATED_BODY()

public:
    TWeakObjectPtr<UWorld> BoundWorld;
    int64 WorldGeneration = 0;
    int32 Mode = 0; // 0 valid, 1 default, 2 NaN, 3 enum, 4 mixed generation, 5 duplicate, 6 zero quat, 7 unnormalized, 8 normalized, 9 mixed sequence
    int32 DetailMode = 0; // 0 valid, 1 wrong request serial, 2 wrong ID, 3 wrong sample ID
    mutable int32 RowCalls = 0;
    mutable int32 DetailCalls = 0;
    mutable int32 ResultCalls = 0;
    int32 SubmitCalls = 0;
    int32 SelectionCalls = 0;
    int32 AcceptedSelections = 0;
    TMap<TWeakObjectPtr<const APlayerController>, int64> SelectionSerialByPC;
    TMap<TWeakObjectPtr<const APlayerController>, FCkCrowdRemoteAgentId> SelectedIdByPC;
    TWeakObjectPtr<APlayerController> LastSelectionPC;
    TWeakObjectPtr<ULocalPlayer> LastSelectionLocalPlayer;
    TWeakObjectPtr<APlayerController> LastSubmitPC;
    TWeakObjectPtr<ULocalPlayer> LastSubmitLocalPlayer;
    FCkCrowdRemoteCommandResult LastResult;

    virtual auto
        GetWorld()
        const -> UWorld* override;
    virtual auto
        IsForWorld(
            const UWorld& World
        )
        const -> bool override;
    virtual auto
        CopyRows(
            const UWorld& World, const APlayerController& RequestingPC,
            const ULocalPlayer& RequestingLocalPlayer, TArray<FCkCrowdRemoteAgentRow>& Out
        )
        const -> bool override;
    virtual auto
        RequestSelectionFromOwningLocalController(
            const UWorld& World, APlayerController& RequestingPC,
            const ULocalPlayer& RequestingLocalPlayer, const FCkCrowdRemoteAgentId& Id,
            int64 SelectionRequestSerial, bool bClear)
        -> bool override;
    virtual auto
        CopySelectedDetail(
            const UWorld& World, const APlayerController& RequestingPC,
            const ULocalPlayer& RequestingLocalPlayer, const FCkCrowdRemoteAgentId& Id,
            int64 SelectionRequestSerial, FCkCrowdRemoteAgentDetail& Out
        )
        const -> bool override;
    virtual auto
        SubmitFromOwningLocalController(
            const UWorld& World, APlayerController& RequestingPC,
            const ULocalPlayer& RequestingLocalPlayer,
            const FCkCrowdRemoteCommandRequest& Request
        )
        -> bool override;
    virtual auto
        CopyCommandResult(
            const UWorld& World, const APlayerController& RequestingPC,
            const ULocalPlayer& RequestingLocalPlayer, int64 ClientSerial,
            FCkCrowdRemoteCommandResult& Out
        )
        const -> bool override;
};

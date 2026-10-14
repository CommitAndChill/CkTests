#pragma once

#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"

#include "CkTest_ProceduralActionManagedListener.generated.h"

enum class ECk_Test_ProceduralActionManagedReentry : uint8
{
    None,
    Cancel,
    Release,
    DestroyScope,
    DestroyPlayer,
    Unbind,
    PlayAnother
};

// Recipient for managed delivery tests. Reentry actions run once, from the first records callback.
UCLASS()
class UCk_Test_ProceduralActionManagedListener_UE : public UObject
{
    GENERATED_BODY()

public:
    ECk_Test_ProceduralActionManagedReentry Reentry = ECk_Test_ProceduralActionManagedReentry::None;
    FCk_Handle_ProceduralActionPlayer Player;
    FCk_Handle_ProceduralAction Action;
    FCk_ProceduralAction_Identity Identity;
    FCk_Handle Scope;
    FCk_Handle PlayerRoot;
    FCk_Handle AnotherScope;
    TWeakObjectPtr<UCk_ProceduralAction_Data> AnotherClip;
    TSharedPtr<int32> ExternalCompletionCount;

    bool ReentryDone = false;
    int32 RecordsCalls = 0;
    int32 PoseCalls = 0;
    TArray<FCk_ProceduralAction_Record> Records;
    TArray<int32> PageSizes;
    int32 RecordsSeenAtCompletion = INDEX_NONE;
    int32 CompletionCount = 0;
    FCk_ProceduralAction_ManagedCompletion LastCompletion;
    uint64 CompletionFrame = 0;
    bool PlayerPendingDestroyAtCompletion = false;
    bool ManagedDuringCompletion = false;
    bool ActionPendingDestroyAtCompletion = false;

    FCk_ProceduralAction_ManagedStart AnotherStart;
    uint64 AnotherPlayFrame = 0;
    int32 AnotherCompletionsInsidePlay = INDEX_NONE;
    int32 AnotherCompletionCount = 0;
    uint64 AnotherCompletionFrame = 0;
    FCk_ProceduralAction_ManagedCompletion AnotherCompletion;

    auto
    MakeRecordsDelegate()
        -> FCk_Delegate_ProceduralAction_OnManagedRecords
    {
        auto Delegate = FCk_Delegate_ProceduralAction_OnManagedRecords{};
        Delegate.BindDynamic(this, &UCk_Test_ProceduralActionManagedListener_UE::OnRecords);
        return Delegate;
    }

    auto
    MakePoseDelegate()
        -> FCk_Delegate_ProceduralAction_OnManagedAcceptedPose
    {
        auto Delegate = FCk_Delegate_ProceduralAction_OnManagedAcceptedPose{};
        Delegate.BindDynamic(this, &UCk_Test_ProceduralActionManagedListener_UE::OnAcceptedPose);
        return Delegate;
    }

    auto
    MakeCompletionDelegate()
        -> FCk_Delegate_ProceduralAction_OnManagedCompleted
    {
        auto Delegate = FCk_Delegate_ProceduralAction_OnManagedCompleted{};
        Delegate.BindDynamic(this, &UCk_Test_ProceduralActionManagedListener_UE::OnCompleted);
        return Delegate;
    }

    UFUNCTION()
    void
    OnRecords(
        FCk_Handle_ProceduralActionPlayer InPlayer,
        FCk_ProceduralAction_ManagedPage InPage)
    {
        ++RecordsCalls;
        PageSizes.Add(InPage.Get_Records().Num());
        Records.Append(InPage.Get_Records());
        if (ReentryDone || Reentry == ECk_Test_ProceduralActionManagedReentry::None)
        { return; }
        ReentryDone = true;
        switch (Reentry)
        {
            case ECk_Test_ProceduralActionManagedReentry::Cancel:
            {
                UCk_Utils_ProceduralAction_UE::Request_Cancel(Action,
                    FCk_Request_ProceduralAction_Cancel{FName{TEXT("ListenerCancel")}}, {});
                break;
            }
            case ECk_Test_ProceduralActionManagedReentry::Release:
            {
                UCk_Utils_ProceduralAction_UE::Request_Release(Action,
                    FCk_Request_ProceduralAction_Release{}, {});
                break;
            }
            case ECk_Test_ProceduralActionManagedReentry::DestroyScope:
            {
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Scope);
                break;
            }
            case ECk_Test_ProceduralActionManagedReentry::DestroyPlayer:
            {
                UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(PlayerRoot);
                break;
            }
            case ECk_Test_ProceduralActionManagedReentry::Unbind:
            {
                UCk_Utils_ProceduralActionPlayer_UE::UnbindFrom_OnManagedRecords(Player, Identity,
                    MakeRecordsDelegate());
                break;
            }
            case ECk_Test_ProceduralActionManagedReentry::PlayAnother:
            {
                auto Spec = FCk_ProceduralAction_LoadedPlaySpec{AnotherClip.Get()};
                Spec.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                    FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, FName{TEXT("MissingLimb")}}});
                auto Completion = FCk_Delegate_ProceduralAction_OnManagedCompleted{};
                Completion.BindDynamic(this, &UCk_Test_ProceduralActionManagedListener_UE::OnAnotherCompleted);
                AnotherPlayFrame = GFrameCounter;
                AnotherStart = UCk_Utils_ProceduralAction_UE::Play_Loaded(AnotherScope, Player, Spec, Completion);
                AnotherCompletionsInsidePlay = AnotherCompletionCount;
                break;
            }
            default:
                break;
        }
    }

    UFUNCTION()
    void
    OnAcceptedPose(
        FCk_Handle_ProceduralActionPlayer InPlayer,
        FCk_ProceduralAction_Receipt InReceipt)
    {
        ++PoseCalls;
    }

    UFUNCTION()
    void
    OnCompleted(
        FCk_Handle_ProceduralActionPlayer InPlayer,
        FCk_ProceduralAction_ManagedCompletion InCompletion)
    {
        ++CompletionCount;
        if (ExternalCompletionCount.IsValid())
        { ++(*ExternalCompletionCount); }
        LastCompletion = InCompletion;
        CompletionFrame = GFrameCounter;
        RecordsSeenAtCompletion = Records.Num();
        PlayerPendingDestroyAtCompletion = ck::IsValid(InPlayer)
            && InPlayer.Has<ck::FTag_DestroyEntity_Initiate>();
        ManagedDuringCompletion = UCk_Utils_ProceduralActionPlayer_UE::Get_IsManagedDelivery(InPlayer,
            InCompletion.Get_Identity());
        ActionPendingDestroyAtCompletion = ck::IsValid(Action) && Action.Has<ck::FTag_DestroyEntity_Initiate>();
    }

    UFUNCTION()
    void
    OnAnotherCompleted(
        FCk_Handle_ProceduralActionPlayer InPlayer,
        FCk_ProceduralAction_ManagedCompletion InCompletion)
    {
        ++AnotherCompletionCount;
        AnotherCompletionFrame = GFrameCounter;
        AnotherCompletion = InCompletion;
    }
};

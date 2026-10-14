#include "CoreMinimal.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_ANGELSCRIPT_CK
#error This verification host requires AngelScript to be disabled.
#endif

DEFINE_LOG_CATEGORY_STATIC(LogProceduralActionsSmoke, Log, All);

class FProceduralActionsSmokeModule final : public FDefaultGameModuleImpl
{
public:
    auto StartupModule() -> void override
    {
#if !WITH_EDITOR
        _Started = FPlatformTime::Seconds();
        _Ticker = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateRaw(this, &FProceduralActionsSmokeModule::DoTick));
#endif
    }

    auto ShutdownModule() -> void override
    {
        FTSTicker::GetCoreTicker().RemoveTicker(_Ticker);
    }

private:
    auto DoFinish(bool InSucceeded, const TCHAR* InReason) -> bool
    {
        UE_LOG(LogProceduralActionsSmoke, Display,
            TEXT("PROCEDURAL_ACTIONS_NATIVE_SMOKE result=%s accepted=%d reason=%s WITH_EDITOR=%d"),
            InSucceeded ? TEXT("PASS") : TEXT("FAIL"), _Accepted, InReason, WITH_EDITOR);
        FPlatformMisc::RequestExitWithStatus(false, InSucceeded ? 0 : 1);
        return false;
    }

    auto DoTick(float) -> bool
    {
#if WITH_EDITOR
        return DoFinish(false, TEXT("EditorCannotCertifyNativeRuntime"));
#else
        if (FPlatformTime::Seconds() - _Started > 90.0)
        {
            return DoFinish(false, TEXT("WorldOrPlaybackTimeout"));
        }
        if (NOT _Created)
        {
            if (ck::Is_NOT_Valid(GEngine))
            {
                return true;
            }
            for (const auto& Context : GEngine->GetWorldContexts())
            {
                auto* World = Context.World();
                if (Context.WorldType != EWorldType::Game || ck::Is_NOT_Valid(World)
                    || NOT World->HasBegunPlay())
                {
                    continue;
                }
                auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
                _Root = UCk_Utils_Transform_UE::Add(RootEntity, FTransform::Identity,
                    ECk_Replication::DoesNotReplicate);
                auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(_Root);
                const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
                    FTransform::Identity, ECk_Replication::DoesNotReplicate);
                auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
                Layout.Set_BodyEnvelopeMin(FVector{-2.0});
                Layout.Set_BodyEnvelopeMax(FVector{2.0});
                auto Spec = FCk_ProceduralArticulation_Spec{Layout, TArray<FCk_ProceduralLimb_Binding>{}};
                Spec.Set_Presentation(Presentation);
                _Subject = UCk_Utils_ProceduralArticulation_UE::Add(_Root, Spec);
                _Created = true;
                if (ck::Is_NOT_Valid(_Subject))
                {
                    return DoFinish(false, TEXT("SubjectComposition"));
                }
                break;
            }
            return true;
        }
        if (NOT _Playing)
        {
            if (UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(_Subject) == 0)
            {
                return true;
            }
            _Player = UCk_Utils_ProceduralActionPlayer_UE::Add(_Subject, FCk_ProceduralActionPlayer_Spec{});
            _Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(_Root);
            auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{NewObject<UCk_ProceduralAction_Data>()};
            auto Body = FCk_ProceduralAction_BodyChannel{};
            Body.Set_Enabled(true);
            Clip->Set_BodyChannel(Body);
            auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Lean")}, FCk_Time{0.2}};
            Phase.Set_BodyKeys({
                FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0f}, FVector::ZeroVector, FQuat::Identity},
                FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0f}, FVector{1.0, 0.0, 0.0}, FQuat::Identity}});
            Clip->Set_Phases({Phase});
            const auto Action = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(
                _Scope, _Player, FCk_ProceduralAction_LoadedPlaySpec{Clip.Get()});
            if (ck::Is_NOT_Valid(Action))
            {
                return DoFinish(false, TEXT("ActionAdmission"));
            }
            _Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(Action);
            _Playing = true;
            return true;
        }
        const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(_Player, _Identity, _Cursor, 64);
        if (Page.Get_Result() != ECk_ProceduralAction_ReadResult::Succeeded
            && Page.Get_Result() != ECk_ProceduralAction_ReadResult::Pending)
        {
            return DoFinish(false, TEXT("ReceiptRead"));
        }
        for (const auto& Record : Page.Get_Records())
        {
            if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose)
            {
                ++_Accepted;
                const auto& Pose = Record.Get_Receipt().Get_CurrentPose();
                _ReachedLean |= Pose.Get_HasBodyPresentation()
                    && Pose.Get_BodyPresentation().GetTranslation().Equals(FVector{1.0, 0.0, 0.0}, 0.05);
            }
            if (Record.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal)
            {
                return DoFinish(_Accepted > 1 && _ReachedLean
                    && Record.Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed
                    && Record.Get_Result().Get_Recovery() == ECk_ProceduralAction_Recovery::Succeeded,
                    TEXT("ObservedTerminal"));
            }
        }
        _Cursor = Page.Get_LastStreamSequence();
        return true;
#endif
    }

private:
    FTSTicker::FDelegateHandle _Ticker;
    FCk_Handle_Transform _Root;
    FCk_Handle_ProceduralArticulation _Subject;
    FCk_Handle_ProceduralActionPlayer _Player;
    FCk_Handle _Scope;
    FCk_ProceduralAction_Identity _Identity;
    int64 _Cursor = 0;
    int32 _Accepted = 0;
    double _Started = 0.0;
    bool _Created = false;
    bool _Playing = false;
    bool _ReachedLean = false;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FProceduralActionsSmokeModule, ProceduralActionsSmoke, "ProceduralActionsSmoke");

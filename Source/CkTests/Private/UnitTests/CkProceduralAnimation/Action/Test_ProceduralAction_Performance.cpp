#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Fragment.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/Limb/CkProceduralLimb_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkTests/CkTests_Log.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace ck_test_procedural_action_performance
{
    // Opt in with CK_PROCEDURAL_ACTION_PERF=1. The default automation gate only checks registration.
    // The measured interval is PIE frame cadence including this driver's work, not player CPU self-time.
    // CkProcessors/Insights scopes provide processor attribution in a separate capture.
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 60.0;
    constexpr auto WarmupFrames = 8;
    constexpr auto MeasuredFrames = 24;
    constexpr auto WalkingMeasuredFrames = 60;
    constexpr auto SubmitPeriodFrames = 8;
    constexpr auto ClipDurationSeconds = 60.0;
    const auto LimbId = FName{TEXT("PerfArm")};
    const auto SecondLimbId = FName{TEXT("PerfArmB")};
    const auto BorrowedLimbId = FName{TEXT("Leg0")};
    constexpr auto WalkerY = 100000.0;
    constexpr auto WalkerBodyHeight = 60.0;

    enum class EScenario : uint8
    {
        Dedicated,
        WalkerIdle,
        WalkerWalking,
        WalkerBorrowed,
        Disjoint,
        LoadedClip
    };

    auto
    Get_MeasuredFrames(EScenario InScenario) -> int32
    { return InScenario == EScenario::WalkerWalking ? WalkingMeasuredFrames : MeasuredFrames; }

    auto
    Get_Name(EScenario InScenario) -> const TCHAR*
    {
        switch (InScenario)
        {
        case EScenario::Dedicated: return TEXT("Dedicated");
        case EScenario::WalkerIdle: return TEXT("WalkerIdle");
        case EScenario::WalkerWalking: return TEXT("WalkerWalking");
        case EScenario::WalkerBorrowed: return TEXT("WalkerBorrowed");
        case EScenario::Disjoint: return TEXT("Disjoint");
        case EScenario::LoadedClip: return TEXT("LoadedClip");
        }
        return TEXT("Unknown");
    }

    auto
    Get_HasActions(EScenario InScenario) -> bool
    {
        return InScenario == EScenario::Dedicated || InScenario == EScenario::WalkerBorrowed
            || InScenario == EScenario::Disjoint || InScenario == EScenario::LoadedClip;
    }

    auto
    Get_IsWalker(EScenario InScenario) -> bool
    {
        return InScenario == EScenario::WalkerIdle || InScenario == EScenario::WalkerWalking
            || InScenario == EScenario::WalkerBorrowed;
    }

    struct FInstance
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_ProceduralActionPlayer Player;
        TArray<FCk_Handle_ProceduralAction> Actions;
        TArray<FCk_ProceduralAction_Identity> Identities;
        TArray<FName> ActionLimbIds;
        TArray<ECk_ProceduralLeg_FootPhase> PreviousFootPhases;
        FVector InitialLocation = FVector::ZeroVector;
    };

    struct FCase
    {
        int32 Subjects = 0;
        int32 Links = 0;
        int32 Repetition = 0;
        EScenario Scenario = EScenario::Dedicated;
        TStrongObjectPtr<UCk_ProceduralAction_Data> ResidentClip;
        FCk_Handle Floor;
        FCk_Handle_JoltBody FloorBody;
        TArray<FInstance> Instances;
        double FrameSumMs = 0.0;
        double FrameMaxMs = 0.0;
        double DriverSubmitMs = 0.0;
        int32 FrameIntervals = 0;
        int64 MaxReservedRecordSlots = 0;
        int64 MaxRetainedTransforms = 0;
        SIZE_T MaxRetainedStoreArrayHeapBytes = 0;
        double MaxLastReceiptBacklogSeconds = 0.0;
        bool DriverValid = true;
        bool AcceptedWithinRegion = false;
        bool ObservedSwing = false;
        bool ObservedTouchdown = false;
        bool ObservedClipIntermediate = false;
        TArray<int64> ClipEntrySequences;
    };

    auto
    MakePoseBatch(
        FName InLimbId,
        double InDegrees)
        -> FCk_ProceduralAction_PoseBatch
    {
        const auto Radians = FMath::DegreesToRadians(InDegrees);
        auto Target = FCk_ProceduralAction_LimbTarget{InLimbId,
            FTransform{FQuat{FVector::UpVector, Radians},
                FVector{FMath::Cos(Radians) - 1.0, FMath::Sin(Radians), 0.0}}};
        Target.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        auto Batch = FCk_ProceduralAction_PoseBatch{};
        Batch.Set_Limbs(TArray<FCk_ProceduralAction_LimbTarget>{Target});
        return Batch;
    }

    auto
    MakeResidentClip() -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
        auto Track = FCk_ProceduralAction_LimbTrack{LimbId};
        Track.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        auto Keys = TArray<FCk_ProceduralAction_VectorKey>{};
        for (const auto& Key : TArray<TPair<double, double>>{
            {0.0, 0.0}, {0.02, 0.05}, {0.04, -0.05}, {1.0, 0.0}})
        {
            const auto Radians = FMath::DegreesToRadians(Key.Value);
            Keys.Emplace(FCk_FloatRange_0to1{Key.Key},
                FVector{FMath::Cos(Radians) - 1.0, FMath::Sin(Radians), 0.0});
        }
        Track.Set_PositionKeys(Keys);
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("ReachCurve")},
            FCk_Time{ClipDurationSeconds}};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
        Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{LimbId}});
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Clip;
    }

    auto
    MakeLengths(int32 InLinks) -> TArray<float>
    {
        auto Lengths = TArray<float>{};
        Lengths.Init(160.0f / static_cast<float>(InLinks), InLinks);
        return Lengths;
    }

    auto
    AddSegments(const FCk_Handle_Transform& InRoot, int32 InLinks, const FTransform& InStart)
        -> TArray<FCk_Handle_Transform>
    {
        auto Segments = TArray<FCk_Handle_Transform>{};
        Segments.Reserve(InLinks);
        for (auto LinkIndex = 0; LinkIndex < InLinks; ++LinkIndex)
        {
            auto Parent = InRoot.ConvertToHandle();
            auto SegmentEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Parent);
            Segments.Add(UCk_Utils_Transform_UE::Add(SegmentEntity, InStart,
                ECk_Replication::DoesNotReplicate));
        }
        return Segments;
    }

    auto
    AddFloor(UWorld* InWorld, FCase& InOutCase) -> void
    {
        InOutCase.Floor = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        UCk_Utils_Transform_UE::Add(InOutCase.Floor,
            FTransform{FVector{50000.0, WalkerY, -25.0}}, ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
        Shape.Set_HalfExtents(FVector{51000.0, 2000.0, 25.0});
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_CollisionProfileName(TEXT("BlockAll"));
        InOutCase.FloorBody = UCk_Utils_JoltBody_UE::Add(InOutCase.Floor, Spec);
    }

    auto
    AddDedicated(UWorld* InWorld, int32 InIndex, int32 InLinks, bool InDisjoint) -> FInstance
    {
        auto Instance = FInstance{};
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        Instance.InitialLocation = FVector{static_cast<double>(InIndex) * 1000.0, 0.0, 2000.0};
        Instance.Root = UCk_Utils_Transform_UE::Add(RootEntity,
            FTransform{Instance.InitialLocation}, ECk_Replication::DoesNotReplicate);
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>{};
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>{};
        for (const auto Id : InDisjoint ? TArray<FName>{LimbId, SecondLimbId}
                                        : TArray<FName>{LimbId})
        {
            const auto OffsetY = Id == SecondLimbId ? 80.0 : 0.0;
            Layouts.Emplace(Id, FTransform{FVector{0.0, OffsetY, 0.0}}, MakeLengths(InLinks),
                FTransform{FVector{160.0, 0.0, 0.0}}, FVector::UpVector);
            Bindings.Emplace(Id, AddSegments(Instance.Root, InLinks,
                FTransform{Instance.InitialLocation}));
            Instance.ActionLimbIds.Add(Id);
        }
        Instance.Subject = UCk_Utils_ProceduralArticulation_UE::Add(Instance.Root,
            FCk_ProceduralArticulation_Spec{FCk_ProceduralArticulation_Layout{Layouts}, Bindings});
        return Instance;
    }

    auto
    AddWalker(UWorld* InWorld, int32 InIndex, int32 InLinks) -> FInstance
    {
        auto Instance = FInstance{};
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        Instance.InitialLocation = FVector{static_cast<double>(InIndex) * 1000.0,
            WalkerY, WalkerBodyHeight};
        Instance.Root = UCk_Utils_Transform_UE::Add(RootEntity,
            FTransform{Instance.InitialLocation}, ECk_Replication::DoesNotReplicate);
        auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
        auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
        const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0},
            {-40.0, 30.0}, {-40.0, -30.0}};
        for (auto LegIndex = 0; LegIndex < Corners.Num(); ++LegIndex)
        {
            const auto Id = FName{*FString::Printf(TEXT("Leg%d"), LegIndex)};
            const auto& Corner = Corners[LegIndex];
            auto Placement = FCk_ProceduralLeg_Placement{
                FVector{Corner.X, Corner.Y, 0.0}, FVector{Corner.X, Corner.Y * 2.0, -60.0}};
            Placement.Set_PhaseOffset(LegIndex == 0 || LegIndex == 3 ? 0.0f : 0.5f);
            Legs.Emplace(Id, Placement, FCk_ProceduralLeg_ChainGeometry{MakeLengths(InLinks)});
            Chains.Emplace(Id, FCk_ProceduralRig_Spec{AddSegments(Instance.Root, InLinks,
                FTransform{Instance.InitialLocation})});
        }
        auto* Rig = NewObject<UCk_ProceduralRig_Data>();
        Rig->Set_Legs(Legs);
        const auto Walker = UCk_Utils_ProceduralAnimation_UE::Add_Walker(Instance.Root,
            Rig, NewObject<UCk_ProceduralGait_Data>(), Chains);
        Instance.Gait = Walker.Get_Gait();
        Instance.Subject = UCk_Utils_ProceduralArticulation_UE::Cast(Instance.Root);
        Instance.ActionLimbIds.Add(BorrowedLimbId);
        return Instance;
    }

    auto
    Get_AllSettled(
        const FCase& InCase)
        -> bool
    {
        if (InCase.Instances.Num() != InCase.Subjects) { return false; }
        for (const auto& Instance : InCase.Instances)
        {
            if (NOT ck::IsValid(Instance.Subject)
                || UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(Instance.Subject) <= 0)
            { return false; }
            if (Get_IsWalker(InCase.Scenario)
                && (NOT ck::IsValid(Instance.Gait)
                    || UCk_Utils_ProceduralGait_UE::Get_Status(Instance.Gait)
                        != ECk_ProceduralAnimation_Status::Ready
                    || UCk_Utils_ProceduralGait_UE::Get_PlantedCount(Instance.Gait) != 4))
            { return false; }
        }
        return true;
    }

    auto
    Get_AllReady(
        const FCase& InCase)
        -> bool
    {
        if (NOT Get_HasActions(InCase.Scenario))
        { return InCase.Instances.Num() == InCase.Subjects; }
        const auto ExpectedActions = InCase.Scenario == EScenario::Disjoint ? 2 : 1;
        for (const auto& Instance : InCase.Instances)
        {
            if (Instance.Actions.Num() != ExpectedActions) { return false; }
            for (const auto& Action : Instance.Actions)
            {
                if (NOT ck::IsValid(Action)
                    || UCk_Utils_ProceduralAction_UE::Get_State(Action)
                        != (InCase.Scenario == EScenario::LoadedClip
                            ? ECk_ProceduralAction_State::Playing
                            : ECk_ProceduralAction_State::Ready))
                { return false; }
            }
        }
        return InCase.Instances.Num() == InCase.Subjects;
    }

    auto
    Get_AllClipAdvanced(const FCase& InCase) -> bool
    {
        if (InCase.Scenario != EScenario::LoadedClip) { return false; }
        auto Index = 0;
        for (const auto& Instance : InCase.Instances)
        {
            for (const auto& Action : Instance.Actions)
            {
                if (NOT InCase.ClipEntrySequences.IsValidIndex(Index)) { return false; }
                const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(Action);
                if (NOT Receipt.Get_HasAcceptedPose()
                    || Receipt.Get_Stamp().Get_SampleSequence()
                        <= InCase.ClipEntrySequences[Index])
                { return false; }
                ++Index;
            }
        }
        return Index == InCase.Subjects;
    }

    auto
    Get_AllAccepted(
        const FCase& InCase,
        int64 InMinimumSampleSequence)
        -> bool
    {
        if (NOT Get_HasActions(InCase.Scenario))
        { return InCase.Instances.Num() == InCase.Subjects; }
        for (const auto& Instance : InCase.Instances)
        {
            if (Instance.Actions.Num() != Instance.ActionLimbIds.Num()) { return false; }
            for (const auto& Action : Instance.Actions)
            {
                const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(Action);
                if (NOT Receipt.Get_HasAcceptedPose()
                    || Receipt.Get_Stamp().Get_SampleSequence() < InMinimumSampleSequence)
                { return false; }
            }
        }
        return InCase.Instances.Num() == InCase.Subjects;
    }

    class FMeasureFrames final : public IAutomationLatentCommand
    {
    private:
        TSharedRef<FCase> _Case;
        int32 _Frame = 0;
        double _PreviousSeconds = 0.0;
        FString _RegionName;
        uint64 _RegionId = 0;
        int32 _MeasuredFrames = 0;

        auto CloseRegion() -> void
        {
            if (_RegionId != 0)
            {
                TRACE_END_REGION_WITH_ID(_RegionId);
                _RegionId = 0;
            }
        }

    public:
        explicit FMeasureFrames(const TSharedRef<FCase>& InCase)
            : _Case(InCase)
            , _RegionName(FString::Printf(TEXT("CkActionPERF_%s_s%d_l%d_r%d"),
                Get_Name(InCase->Scenario), InCase->Subjects, InCase->Links, InCase->Repetition))
            , _MeasuredFrames(Get_MeasuredFrames(InCase->Scenario)) {}

        virtual ~FMeasureFrames() override
        { CloseRegion(); }

        virtual auto Update() -> bool override
        {
            if (_Frame == 0)
            {
                if (_Case->Scenario == EScenario::LoadedClip)
                {
                    _Case->ClipEntrySequences.Reserve(_Case->Subjects);
                    for (const auto& Instance : _Case->Instances)
                    {
                        for (const auto& Action : Instance.Actions)
                        {
                            const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(Action);
                            if (NOT Receipt.Get_HasAcceptedPose())
                            { _Case->DriverValid = false; return true; }
                            _Case->ClipEntrySequences.Add(Receipt.Get_Stamp().Get_SampleSequence());
                        }
                    }
                }
                TRACE_BOOKMARK(TEXT("CkActionPERF begin scenario=%s subjects=%d links=%d repetition=%d"),
                    Get_Name(_Case->Scenario), _Case->Subjects, _Case->Links, _Case->Repetition);
                _RegionId = TRACE_BEGIN_REGION_WITH_ID(*_RegionName);
            }
            for (const auto& Instance : _Case->Instances)
            {
                if (NOT ck::IsValid(Instance.Subject)
                    || (Get_HasActions(_Case->Scenario)
                        && (NOT ck::IsValid(Instance.Player)
                            || Instance.Actions.IsEmpty())))
                { _Case->DriverValid = false; CloseRegion(); return true; }
            }
            if (NOT Get_AllReady(*_Case))
            { _Case->DriverValid = false; CloseRegion(); return true; }
            if (_Case->Scenario == EScenario::WalkerBorrowed)
            {
                for (const auto& Instance : _Case->Instances)
                {
                    const auto Control = UCk_Utils_ProceduralArticulation_UE::Get_LimbControl(
                        Instance.Subject, BorrowedLimbId);
                    if (Control.Get_State() != ECk_ProceduralLimbControl_State::Controlled
                        || Control.Get_Participating())
                    { _Case->DriverValid = false; CloseRegion(); return true; }
                }
            }
            if (_Case->Scenario == EScenario::WalkerWalking)
            {
                for (auto& Instance : _Case->Instances)
                {
                    const auto Legs = UCk_Utils_ProceduralGait_UE::Get_Legs(Instance.Gait);
                    if (Legs.Num() != 4)
                    { _Case->DriverValid = false; CloseRegion(); return true; }
                    for (auto LegIndex = 0; LegIndex < Legs.Num(); ++LegIndex)
                    {
                        const auto Phase = UCk_Utils_ProceduralLeg_UE::Get_Foot(Legs[LegIndex]).Get_Phase();
                        _Case->ObservedSwing |= Phase == ECk_ProceduralLeg_FootPhase::Swinging;
                        if (Instance.PreviousFootPhases.IsValidIndex(LegIndex))
                        {
                            _Case->ObservedTouchdown |= Instance.PreviousFootPhases[LegIndex]
                                == ECk_ProceduralLeg_FootPhase::Swinging
                                && Phase == ECk_ProceduralLeg_FootPhase::Planted;
                            Instance.PreviousFootPhases[LegIndex] = Phase;
                        }
                        else
                        { Instance.PreviousFootPhases.Add(Phase); }
                    }
                }
            }
            const auto Now = FPlatformTime::Seconds();
            if (_PreviousSeconds > 0.0)
            {
                const auto IntervalMs = (Now - _PreviousSeconds) * 1000.0;
                _Case->FrameSumMs += IntervalMs;
                _Case->FrameMaxMs = FMath::Max(_Case->FrameMaxMs, IntervalMs);
                ++_Case->FrameIntervals;
            }
            _PreviousSeconds = Now;

            auto ReservedRecordSlots = int64{0};
            auto RetainedTransforms = int64{0};
            auto RetainedStoreArrayHeapBytes = SIZE_T{0};
            auto ClipActionIndex = 0;
            for (const auto& Instance : _Case->Instances)
            {
                if (NOT ck::IsValid(Instance.Player)) { continue; }
                const auto& Store = Instance.Player.Get<ck::FFragment_ProceduralActionPlayer>()
                    .Get_Receipts();
                ReservedRecordSlots += Store.Get_RetainedRecordSlots();
                RetainedTransforms += Store.Get_RetainedPoseTransforms();
                RetainedStoreArrayHeapBytes += Store.Get_AllocatedSize();
                for (const auto& Action : Instance.Actions)
                {
                    const auto Receipt = UCk_Utils_ProceduralAction_UE::Get_LastReceipt(Action);
                    if (Receipt.Get_HasAcceptedPose())
                    {
                        if (_Case->Scenario == EScenario::LoadedClip)
                        {
                            _Case->ObservedClipIntermediate |=
                                _Case->ClipEntrySequences.IsValidIndex(ClipActionIndex)
                                && Receipt.Get_Stamp().Get_SampleSequence()
                                    > _Case->ClipEntrySequences[ClipActionIndex]
                                && NOT Receipt.Get_IntermediateSamples().IsEmpty()
                                && Receipt.Get_Sampling().Get_EvaluatedSamples() > 1;
                        }
                        _Case->MaxLastReceiptBacklogSeconds = FMath::Max(
                            _Case->MaxLastReceiptBacklogSeconds,
                            Receipt.Get_Sampling().Get_Backlog().Get_Seconds());
                    }
                    ++ClipActionIndex;
                }
            }
            _Case->MaxReservedRecordSlots = FMath::Max(
                _Case->MaxReservedRecordSlots, ReservedRecordSlots);
            _Case->MaxRetainedTransforms = FMath::Max(
                _Case->MaxRetainedTransforms, RetainedTransforms);
            _Case->MaxRetainedStoreArrayHeapBytes = FMath::Max(
                _Case->MaxRetainedStoreArrayHeapBytes, RetainedStoreArrayHeapBytes);

            if (_Case->Scenario == EScenario::WalkerWalking
                || _Case->Scenario == EScenario::WalkerBorrowed)
            {
                for (const auto& Instance : _Case->Instances)
                {
                    auto Root = Instance.Root;
                    UCk_Utils_Transform_UE::Request_SetLocation(Root,
                        FCk_Request_Transform_SetLocation{
                            Instance.InitialLocation + FVector{static_cast<double>(_Frame) * 1.5, 0.0, 0.0}},
                        {});
                }
            }

            if (Get_HasActions(_Case->Scenario) && _Case->Scenario != EScenario::LoadedClip
                && _Frame < _MeasuredFrames && _Frame % SubmitPeriodFrames == 0)
            {
                const auto SubmitStart = FPlatformTime::Seconds();
                for (auto& Instance : _Case->Instances)
                {
                    for (auto ActionIndex = 0; ActionIndex < Instance.Actions.Num(); ++ActionIndex)
                    {
                        const auto Batch = MakePoseBatch(Instance.ActionLimbIds[ActionIndex],
                            (_Frame / SubmitPeriodFrames) % 2 == 0 ? 0.05 : -0.05);
                        auto Action = Instance.Actions[ActionIndex];
                        UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(Action,
                            FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
                    }
                }
                _Case->DriverSubmitMs += (FPlatformTime::Seconds() - SubmitStart) * 1000.0;
            }
            ++_Frame;
            if (_Frame > _MeasuredFrames)
            {
                _Case->AcceptedWithinRegion = _Case->Scenario == EScenario::LoadedClip
                    ? Get_AllClipAdvanced(*_Case) : Get_AllAccepted(*_Case, 4);
                TRACE_BOOKMARK(TEXT("CkActionPERF end scenario=%s subjects=%d links=%d repetition=%d"),
                    Get_Name(_Case->Scenario), _Case->Subjects, _Case->Links, _Case->Repetition);
                CloseRegion();
            }
            return _Frame > _MeasuredFrames;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionProductionManualMatrixTest,
    "Ck.ProceduralAnimation.Action.Perf.ProductionManualMatrix",
    ck_test_procedural_action_performance::TestFlags)

auto
    FCkProceduralActionProductionManualMatrixTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_performance;
    if (FPlatformMisc::GetEnvironmentVariable(TEXT("CK_PROCEDURAL_ACTION_PERF")) != TEXT("1"))
    {
        AddInfo(TEXT("[CkAction PERF] skipped; set CK_PROCEDURAL_ACTION_PERF=1 for the opt-in matrix."));
        return true;
    }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    for (const auto Scenario : {EScenario::Dedicated, EScenario::WalkerIdle,
        EScenario::WalkerWalking, EScenario::WalkerBorrowed, EScenario::Disjoint,
        EScenario::LoadedClip})
    {
        for (const auto Subjects : {1, 10, 100})
        {
            for (const auto Links : {2, 4, 8})
            {
                for (auto Repetition = 1; Repetition <= 3; ++Repetition)
                {
                    const auto Case = MakeShared<FCase>();
                    Case->Scenario = Scenario;
                    Case->Subjects = Subjects;
                    Case->Links = Links;
                    Case->Repetition = Repetition;

                    if (Scenario == EScenario::LoadedClip)
                    { Case->ResidentClip = MakeResidentClip(); }

                    if (Get_IsWalker(Scenario))
                    {
                        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                            FCk_NetAutoTest_ServerAction::CreateLambda(
                                [Case](UWorld* InWorld) { AddFloor(InWorld, *Case); })));
                        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                            FCk_NetAutoTest_Condition::CreateLambda([Case]
                            {
                                return ck::IsValid(Case->FloorBody)
                                    && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Case->FloorBody);
                            }), WaitSeconds, TEXT("Performance walking floor is present in Jolt")));
                    }

                    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                        FCk_NetAutoTest_ServerAction::CreateLambda(
                            [this, Case](UWorld* InWorld)
                            {
                                Case->Instances.Reserve(Case->Subjects);
                                for (auto SubjectIndex = 0; SubjectIndex < Case->Subjects; ++SubjectIndex)
                                {
                                    auto Instance = Get_IsWalker(Case->Scenario)
                                        ? AddWalker(InWorld, SubjectIndex, Case->Links)
                                        : AddDedicated(InWorld, SubjectIndex, Case->Links,
                                            Case->Scenario == EScenario::Disjoint);
                                    if (NOT ck::IsValid(Instance.Subject)
                                        || (Get_IsWalker(Case->Scenario) && NOT ck::IsValid(Instance.Gait)))
                                    {
                                        AddError(FString::Printf(TEXT("[CkAction PERF] setup failed scenario=%s subjects=%d links=%d repetition=%d subject=%d"),
                                            Get_Name(Case->Scenario), Case->Subjects, Case->Links,
                                            Case->Repetition, SubjectIndex));
                                    }
                                    Case->Instances.Add(MoveTemp(Instance));
                                }
                            })));
                    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                        FCk_NetAutoTest_Condition::CreateLambda([Case] { return Get_AllSettled(*Case); }),
                        WaitSeconds, TEXT("Performance subjects have settled actual entry poses")));
                    if (Get_HasActions(Scenario))
                    {
                        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                            FCk_NetAutoTest_ServerAction::CreateLambda(
                                [this, Case](UWorld*)
                                {
                                    for (auto& Instance : Case->Instances)
                                    {
                                        if (NOT ck::IsValid(Instance.Subject)) { continue; }
                                        auto Spec = FCk_ProceduralActionPlayer_Spec{};
                                        if (Case->Scenario == EScenario::WalkerBorrowed)
                                        {
                                            auto Locomotion = FCk_ProceduralLimbControl_Locomotion{};
                                            auto Support = FCk_ProceduralLimbControl_Support{
                                                ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
                                            Support.Set_SupportHoldTimeout(FCk_Time{20.0});
                                            Locomotion.Set_Support(Support);
                                            Spec.Set_Locomotion(Locomotion);
                                        }
                                        Instance.Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                                            Instance.Subject, Spec);
                                        if (NOT ck::IsValid(Instance.Player))
                                        {
                                            AddError(FString::Printf(TEXT("[CkAction PERF] player admission failed scenario=%s subjects=%d links=%d repetition=%d"),
                                                Get_Name(Case->Scenario), Case->Subjects, Case->Links,
                                                Case->Repetition));
                                            continue;
                                        }
                                        for (const auto Limb : Instance.ActionLimbIds)
                                        {
                                            auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(
                                                Instance.Root);
                                            auto Action = FCk_Handle_ProceduralAction{};
                                            if (Case->Scenario == EScenario::LoadedClip)
                                            {
                                                auto Play = FCk_ProceduralAction_LoadedPlaySpec{
                                                    Case->ResidentClip.Get()};
                                                Play.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                                                    FCk_ProceduralAction_SlotBinding{LimbId, Limb}});
                                                Action = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(
                                                    Scope, Instance.Player, Play);
                                            }
                                            else
                                            {
                                                auto Control = FCk_ProceduralAction_ControlSpec{
                                                    TArray<FName>{Limb}};
                                                Control.Set_InputTimeout(FCk_Time{30.0});
                                                Action = UCk_Utils_ProceduralAction_UE::Create_Manual(
                                                    Scope, Instance.Player, Control);
                                            }
                                            if (NOT ck::IsValid(Action))
                                            {
                                                AddError(FString::Printf(TEXT("[CkAction PERF] action admission failed scenario=%s subjects=%d links=%d repetition=%d limb=%s"),
                                                    Get_Name(Case->Scenario), Case->Subjects, Case->Links,
                                                    Case->Repetition, *Limb.ToString()));
                                                continue;
                                            }
                                            Instance.Actions.Add(Action);
                                            Instance.Identities.Add(
                                                UCk_Utils_ProceduralAction_UE::Get_Identity(Action));
                                        }
                                    }
                                })));
                        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                            FCk_NetAutoTest_Condition::CreateLambda([Case] { return Get_AllReady(*Case); }),
                            WaitSeconds, TEXT("Performance actions acquire real claims")));
                        if (Scenario != EScenario::LoadedClip)
                        {
                            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                                FCk_NetAutoTest_ServerAction::CreateLambda(
                                    [Case](UWorld*)
                                    {
                                        for (auto& Instance : Case->Instances)
                                        {
                                            for (auto ActionIndex = 0; ActionIndex < Instance.Actions.Num(); ++ActionIndex)
                                            {
                                                auto Action = Instance.Actions[ActionIndex];
                                                const auto Batch = MakePoseBatch(Instance.ActionLimbIds[ActionIndex], 0.05);
                                                UCk_Utils_ProceduralAction_UE::Request_SubmitPoseBatch(Action,
                                                    FCk_Request_ProceduralAction_SubmitPoseBatch{Batch}, {});
                                            }
                                        }
                                    })));
                        }
                        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                            FCk_NetAutoTest_Condition::CreateLambda([Case]
                            { return Get_AllAccepted(*Case, 1); }),
                            WaitSeconds, TEXT("Performance warmup settles for every action")));
                    }
                    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(WarmupFrames));
                    ADD_LATENT_AUTOMATION_COMMAND(FMeasureFrames(Case));
                    if (Get_HasActions(Scenario) && Scenario != EScenario::LoadedClip)
                    {
                        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                            FCk_NetAutoTest_Condition::CreateLambda([Case]
                            { return Get_AllAccepted(*Case, 4); }),
                            WaitSeconds, TEXT("Every measured manual request has an accepted receipt")));
                    }
                    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                        FCk_NetAutoTest_ServerAction::CreateLambda(
                            [this, Case](UWorld*)
                            {
                                auto Accepted = int64{0};
                                for (const auto& Instance : Case->Instances)
                                {
                                    for (auto ActionIndex = 0; ActionIndex < Instance.Actions.Num(); ++ActionIndex)
                                    {
                                        const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                                            Instance.Player, Instance.Identities[ActionIndex], 0,
                                            Case->Scenario == EScenario::LoadedClip ? 64 : 16);
                                        auto ActionAccepted = 0;
                                        for (const auto& Record : Page.Get_Records())
                                        {
                                            if (Record.Get_Kind()
                                                == ECk_ProceduralAction_RecordKind::AcceptedPose)
                                            {
                                                ++Accepted;
                                                ++ActionAccepted;
                                            }
                                        }
                                        TestTrue(TEXT("Each action retains accepted workload poses"),
                                            (Page.Get_Result() == ECk_ProceduralAction_ReadResult::Pending
                                                || Page.Get_Result() == ECk_ProceduralAction_ReadResult::Succeeded)
                                                && NOT Page.Get_HasMore()
                                                && (Case->Scenario == EScenario::LoadedClip
                                                    ? ActionAccepted >= 2 : ActionAccepted == 4));
                                    }
                                }
                                const auto ActionsPerSubject = Case->Scenario == EScenario::Disjoint ? 2
                                    : Get_HasActions(Case->Scenario) ? 1 : 0;
                                if (Case->Scenario == EScenario::LoadedClip)
                                {
                                    TestTrue(TEXT("Loaded clips accept beyond warmup during measured frames"),
                                        Get_AllClipAdvanced(*Case));
                                    TestTrue(TEXT("Loaded clip receipts include intermediate curve evaluations"),
                                        Case->ObservedClipIntermediate);
                                    TestTrue(TEXT("Loaded clips remain in Playing throughout measurement"),
                                        Get_AllReady(*Case));
                                }
                                else
                                {
                                    TestEqual(TEXT("Every manual action accepted warmup and measured submissions"),
                                        Accepted, static_cast<int64>(Case->Subjects) * ActionsPerSubject * 4);
                                }
                                TestTrue(TEXT("Every performance driver retained live subjects"),
                                    Case->DriverValid);
                                TestTrue(TEXT("Measured accepted samples settled before the trace region closed"),
                                    Case->AcceptedWithinRegion);
                                if (Case->Scenario == EScenario::WalkerWalking)
                                {
                                    TestTrue(TEXT("Walking workload includes an actual gait swing"),
                                        Case->ObservedSwing);
                                    TestTrue(TEXT("Walking workload includes an actual gait touchdown"),
                                        Case->ObservedTouchdown);
                                }
                                TestEqual(TEXT("Measured frame intervals are complete"),
                                    Case->FrameIntervals, Get_MeasuredFrames(Case->Scenario));
                                if (Case->Scenario == EScenario::WalkerWalking
                                    || Case->Scenario == EScenario::WalkerBorrowed)
                                {
                                    for (const auto& Instance : Case->Instances)
                                    {
                                        TestTrue(TEXT("Walking workload moved each actual root"),
                                            UCk_Utils_Transform_UE::Get_EntityCurrentLocation(Instance.Root).X
                                                > Instance.InitialLocation.X + 20.0);
                                    }
                                }
                                const auto AverageMs = Case->FrameIntervals > 0
                                    ? Case->FrameSumMs / Case->FrameIntervals : 0.0;
                                const auto Metrics = FString::Printf(TEXT("[CkAction PERF] scenario=%s subjects=%d links=%d repetition=%d warmupFrames=%d measuredFrameIntervals=%d frameIntervalAvgMs=%.6f frameIntervalMaxMs=%.6f driverSubmitTotalMs=%.6f acceptedIncludingWarmup=%lld acceptedPerSubject=%.3f maxReservedRecordSlots=%lld reservedPerSubject=%.3f maxRetainedPoseTransforms=%lld transformsPerSubject=%.3f maxRetainedStoreArrayHeapBytes=%llu maxLastReceiptBacklogSeconds=%.6f processorCpu=Insights/CkProcessors wholeGameThreadAllocations=unavailable joltQueries=unavailable"),
                                    Get_Name(Case->Scenario), Case->Subjects, Case->Links, Case->Repetition,
                                     WarmupFrames, Case->FrameIntervals, AverageMs, Case->FrameMaxMs,
                                    Case->DriverSubmitMs, static_cast<long long>(Accepted),
                                    static_cast<double>(Accepted) / Case->Subjects,
                                    static_cast<long long>(Case->MaxReservedRecordSlots),
                                    static_cast<double>(Case->MaxReservedRecordSlots) / Case->Subjects,
                                    static_cast<long long>(Case->MaxRetainedTransforms),
                                    static_cast<double>(Case->MaxRetainedTransforms) / Case->Subjects,
                                    static_cast<unsigned long long>(Case->MaxRetainedStoreArrayHeapBytes),
                                    Case->MaxLastReceiptBacklogSeconds);
                                AddInfo(Metrics);
                                ck::tests::Log(TEXT("{}"), Metrics);
                                for (const auto& Instance : Case->Instances)
                                {
                                    auto Root = Instance.Root.ConvertToHandle();
                                    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
                                }
                                if (ck::IsValid(Case->Floor))
                                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Case->Floor); }
                            })));
                    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
                        FCk_NetAutoTest_Condition::CreateLambda([Case]
                        {
                            for (const auto& Instance : Case->Instances)
                            {
                                if (ck::IsValid(Instance.Root)) { return false; }
                            }
                            return NOT ck::IsValid(Case->Floor);
                        }), WaitSeconds, TEXT("Previous performance workload is removed before next repetition")));
                }
            }
        }
    }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

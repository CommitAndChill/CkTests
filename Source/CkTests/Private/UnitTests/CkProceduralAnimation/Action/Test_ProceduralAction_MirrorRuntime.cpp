#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Fragment.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Core/CkProceduralActionValidation.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_mirror_runtime
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto RootPose = FTransform{FQuat{FVector::UpVector, 0.4}, FVector{300.0, -200.0, 2000.0}};
    const auto LeftLimb = FName{TEXT("MirrorLeft")};
    const auto RightLimb = FName{TEXT("MirrorRight")};
    const auto SourceSlot = FName{TEXT("Swing")};
    const auto MirroredSlot = FName{TEXT("Mirrored")};
    const auto PhaseId = FName{TEXT("Swing")};
    const auto WindowId = FName{TEXT("Strike")};
    const auto LimbNodPath = FSoftObjectPath{TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA")};
    const auto MirrorCountEnsure = FString{TEXT("ProceduralAction Create_FromLoadedClip rejected clip or tuning [Play.Mirrors]")};

    struct FState
    {
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle Scope;
        FCk_Handle_ProceduralAction Action;
        FCk_ProceduralAction_Identity Identity;
        FCk_Handle SoftScope;
        FCk_Handle_ProceduralAction SoftAction;
        FCk_ProceduralAction_Identity SoftIdentity;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TArray<FCk_ProceduralAction_Phase> PhasesBefore;
        TArray<FCk_ProceduralAction_Slot> SlotsBefore;
    };

    // Left and right two-segment limbs attached symmetrically about the body XZ plane. With InAsymmetricRight the right
    // limb is shorter and its rest-endpoint frame is turned a quarter about up, so only a conversion through both rest
    // frames maps a rest-normalized offset to the mirror image of the left endpoint.
    auto
        AddSubject(
            UWorld* InWorld,
            bool InAsymmetricRight,
            FState& InOutState)
        -> void
    {
        auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        InOutState.Root = UCk_Utils_Transform_UE::Add(RootEntity, RootPose, ECk_Replication::DoesNotReplicate);
        const auto RightLength = InAsymmetricRight ? 40.0f : 50.0f;
        const auto RightRest = InAsymmetricRight
            ? FTransform{FQuat{FVector::UpVector, UE_HALF_PI}, FVector{60.0, 0.0, 0.0}}
            : FTransform{FVector{60.0, 0.0, 0.0}};
        auto Layouts = TArray<FCk_ProceduralLimb_Layout>{
            FCk_ProceduralLimb_Layout{LeftLimb, FTransform{FVector{0.0, 50.0, 0.0}}, TArray<float>{50.0f, 50.0f},
                FTransform{FVector{60.0, 0.0, 0.0}}, FVector::UpVector},
            FCk_ProceduralLimb_Layout{RightLimb, FTransform{FVector{0.0, -50.0, 0.0}},
                TArray<float>{RightLength, RightLength}, RightRest, FVector::UpVector}};
        auto Bindings = TArray<FCk_ProceduralLimb_Binding>{};
        for (const auto& Layout : Layouts)
        {
            auto Parts = TArray<FCk_Handle_Transform>{};
            for (auto Index = 0; Index < 2; ++Index)
            {
                auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOutState.Root);
                Parts.Add(UCk_Utils_Transform_UE::Add(PartEntity, FTransform::Identity,
                    ECk_Replication::DoesNotReplicate));
            }
            Bindings.Emplace(Layout.Get_Id(), Parts);
        }
        InOutState.Subject = UCk_Utils_ProceduralArticulation_UE::Add(InOutState.Root,
            FCk_ProceduralArticulation_Spec{FCk_ProceduralArticulation_Layout{Layouts}, Bindings});
    }

    auto
        Get_IsSettled(
            const FState& InState)
        -> bool
    {
        return ck::IsValid(InState.Subject)
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(InState.Subject) > 0;
    }

    auto
        AddPlayer(
            FState& InOutState)
        -> bool
    {
        auto Spec = FCk_ProceduralActionPlayer_Spec{};
        auto Capacity = Spec.Get_Capacity();
        Capacity.Set_RecordsPerInstance(256);
        Spec.Set_Capacity(Capacity);
        InOutState.Player = UCk_Utils_ProceduralActionPlayer_UE::Add(InOutState.Subject, Spec);
        return ck::IsValid(InOutState.Player);
    }

    auto
        MakeKey(
            double InTime,
            const FVector& InValue)
        -> FCk_ProceduralAction_VectorKey
    { return FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{InTime}, InValue}; }

    // One slot, one linear phase from InFrom to InTo in InSpace, and a window inside the phase.
    auto
        MakeClip(
            ECk_ProceduralAction_TrackSpace InSpace,
            const FVector& InFrom,
            const FVector& InTo)
        -> UCk_ProceduralAction_Data*
    {
        auto* Clip = NewObject<UCk_ProceduralAction_Data>(GetTransientPackage());
        Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{SourceSlot}});
        auto Track = FCk_ProceduralAction_LimbTrack{SourceSlot};
        Track.Set_Space(InSpace);
        if (InSpace == ECk_ProceduralAction_TrackSpace::AnchorAtStart)
        { Track.Set_AnchorId(FName{TEXT("Target")}); }
        Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{MakeKey(0.0, InFrom), MakeKey(1.0, InTo)});
        auto Phase = FCk_ProceduralAction_Phase{PhaseId, FCk_Time{0.4}};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        Clip->Set_Windows(TArray<FCk_ProceduralAction_Window>{FCk_ProceduralAction_Window{WindowId,
            FCk_ProceduralAction_TimeRef{PhaseId, FCk_FloatRange_0to1{0.25}},
            FCk_ProceduralAction_TimeRef{PhaseId, FCk_FloatRange_0to1{0.75}}}});
        return Clip;
    }

    auto
        MakeMirroredSpec(
            UCk_ProceduralAction_Data* InClip,
            FName InDestinationLimb)
        -> FCk_ProceduralAction_LoadedPlaySpec
    {
        auto Spec = FCk_ProceduralAction_LoadedPlaySpec{InClip};
        Spec.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
            FCk_ProceduralAction_SlotBinding{SourceSlot, LeftLimb},
            FCk_ProceduralAction_SlotBinding{MirroredSlot, InDestinationLimb}});
        Spec.Set_Mirrors(TArray<FCk_ProceduralAction_MirrorSpec>{
            FCk_ProceduralAction_MirrorSpec{SourceSlot, MirroredSlot, ECk_ProceduralAction_MirrorPlane::XZ}});
        return Spec;
    }

    auto
        PhasesEqual(
            const TArray<FCk_ProceduralAction_Phase>& InA,
            const TArray<FCk_ProceduralAction_Phase>& InB)
        -> bool
    {
        if (InA.Num() != InB.Num())
        { return false; }
        for (auto Index = 0; Index < InA.Num(); ++Index)
        {
            if (NOT FCk_ProceduralAction_Phase::StaticStruct()->CompareScriptStruct(&InA[Index], &InB[Index], PPF_None))
            { return false; }
        }
        return true;
    }

    auto
        ReadAll(
            const FCk_Handle_ProceduralActionPlayer& InPlayer,
            FCk_ProceduralAction_Identity InIdentity)
        -> TArray<FCk_ProceduralAction_Record>
    {
        auto Records = TArray<FCk_ProceduralAction_Record>{};
        auto Cursor = int64{0};
        for (auto PageIndex = 0; PageIndex < 64; ++PageIndex)
        {
            const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(InPlayer, InIdentity, Cursor, 64);
            Records.Append(Page.Get_Records());
            Cursor = Page.Get_LastStreamSequence();
            if (NOT Page.Get_HasMore())
            { break; }
        }
        return Records;
    }

    auto
        Get_Terminal(
            const TArray<FCk_ProceduralAction_Record>& InRecords)
        -> const FCk_ProceduralAction_Record*
    {
        return InRecords.FindByPredicate([](const FCk_ProceduralAction_Record& InRecord)
        { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::Terminal; });
    }

    auto
        Get_HasDiagnostic(
            const TArray<ck::procedural_action::FDiagnostic>& InDiagnostics,
            FName InCode)
        -> bool
    {
        return InDiagnostics.ContainsByPredicate([InCode](const ck::procedural_action::FDiagnostic& InDiagnostic)
        { return InDiagnostic.Get_Code() == InCode; });
    }

    auto
        DestroyRoot(
            const FState& InState)
        -> void
    {
        if (ck::Is_NOT_Valid(InState.Root))
        { return; }
        auto Root = InState.Root.ConvertToHandle();
        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMirrorRuntimeExpandsTest,
    "Ck.ProceduralAnimation.Action.Mirror.Runtime.ExpandsBeforeClaimAndKeepsSourceUnchanged",
    ck_test_procedural_action_mirror_runtime::TestFlags)

auto
    FCkProceduralActionMirrorRuntimeExpandsTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_mirror_runtime;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            AddSubject(InWorld, false, *State);
            TestTrue(TEXT("Symmetric two-limb subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(*State); }),
        WaitSeconds, TEXT("Subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player attaches"), AddPlayer(*State)))
            { return; }
            State->Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{MakeClip(ECk_ProceduralAction_TrackSpace::BodyLocal,
                FVector{60.0, 50.0, 0.0}, FVector{70.0, 60.0, 10.0})};
            State->PhasesBefore = State->Clip->Get_Phases();
            State->SlotsBefore = State->Clip->Get_Slots();
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(State->Scope, State->Player,
                MakeMirroredSpec(State->Clip.Get(), RightLimb));
            if (NOT TestTrue(TEXT("Mirrored loaded clip reserves an action"), ck::IsValid(State->Action)))
            { return; }
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            TestEqual(TEXT("The loaded path binds synchronously"), UCk_Utils_ProceduralAction_UE::Get_State(State->Action),
                ECk_ProceduralAction_State::PendingAdmission);
            if (NOT TestTrue(TEXT("The action carries its compiled clip"),
                State->Action.Has<ck::FFragment_ProceduralAction_Compiled>()))
            { return; }
            const auto& Compiled = State->Action.Get<ck::FFragment_ProceduralAction_Compiled>();
            const auto& Clip = *Compiled.Get_Clip();
            TestEqual(TEXT("The bound clip has the source and the mirrored slot"), Clip.Get_Slots().Num(), 2);
            if (Clip.Get_Slots().Num() == 2)
            {
                TestEqual(TEXT("The source slot keeps its place"), Clip.Get_Slots()[0].Get_Id(), SourceSlot);
                TestEqual(TEXT("The mirrored slot follows it"), Clip.Get_Slots()[1].Get_Id(), MirroredSlot);
            }
            TestEqual(TEXT("Both slots are claimed by limb ID"), Compiled.Get_BoundLimbIds(),
                TArray<FName>{LeftLimb, RightLimb});
            const auto& Tracks = Clip.Get_Phases()[0].Get_LimbTracks();
            if (TestEqual(TEXT("The phase gains exactly one destination track"), Tracks.Num(), 2))
            {
                const auto& Source = Tracks[0].Get_PositionKeys();
                const auto& Mirrored = Tracks[1].Get_PositionKeys();
                TestEqual(TEXT("The destination track names the mirrored slot"), Tracks[1].Get_Slot(), MirroredSlot);
                TestTrue(TEXT("Body-local keys reflect across XZ at the same times"), Source.Num() == Mirrored.Num()
                    && Source.Num() == 2
                    && Mirrored[0].Get_Value().Equals(FVector{60.0, -50.0, 0.0}, 1.0e-9)
                    && Mirrored[1].Get_Value().Equals(FVector{70.0, -60.0, 10.0}, 1.0e-9)
                    && Mirrored[1].Get_Time().Get_Value() == Source[1].Get_Time().Get_Value());
            }
            TestEqual(TEXT("Events are not duplicated"), Clip.Get_Events().Num(), 2);
            TestTrue(TEXT("The source asset's phases are unchanged"), PhasesEqual(State->PhasesBefore, State->Clip->Get_Phases()));
            TestEqual(TEXT("The source asset declares no extra slot"), State->Clip->Get_Slots().Num(), State->SlotsBefore.Num());

            auto Soft = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{LimbNodPath}};
            Soft.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                FCk_ProceduralAction_SlotBinding{FName{TEXT("Nod")}, LeftLimb},
                FCk_ProceduralAction_SlotBinding{FName{TEXT("NodMirrored")}, RightLimb}});
            Soft.Set_Mirrors(TArray<FCk_ProceduralAction_MirrorSpec>{FCk_ProceduralAction_MirrorSpec{
                FName{TEXT("Nod")}, FName{TEXT("NodMirrored")}, ECk_ProceduralAction_MirrorPlane::XZ}});
            State->SoftScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->SoftAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(State->SoftScope, State->Player, Soft);
            State->SoftIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->SoftAction);
            TestTrue(TEXT("Mirrored soft clip reserves an action"), ck::IsValid(State->SoftAction));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_Terminal(ReadAll(State->Player, State->Identity)) != nullptr;
        }), WaitSeconds, TEXT("Mirrored loaded clip finishes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Records = ReadAll(State->Player, State->Identity);
            auto Entered = 0;
            auto Exited = 0;
            auto Accepted = 0;
            auto BothLimbs = true;
            for (const auto& Record : Records)
            {
                if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose)
                { continue; }
                ++Accepted;
                BothLimbs &= Record.Get_Receipt().Get_CurrentPose().Get_Limbs().Num() == 2;
                for (const auto& Event : Record.Get_Receipt().Get_Interval().Get_Events())
                {
                    if (Event.Get_Id() != WindowId)
                    { continue; }
                    Entered += Event.Get_Kind() == ECk_ProceduralAction_EventKind::WindowEnter ? 1 : 0;
                    Exited += Event.Get_Kind() == ECk_ProceduralAction_EventKind::WindowExit ? 1 : 0;
                }
            }
            TestTrue(TEXT("The mirrored clip publishes accepted poses"), Accepted > 0);
            TestTrue(TEXT("Every accepted pose carries both mirrored limbs"), BothLimbs);
            TestEqual(TEXT("The window opens once"), Entered, 1);
            TestEqual(TEXT("The window closes once"), Exited, 1);
            const auto* Terminal = Get_Terminal(Records);
            TestTrue(TEXT("The mirrored clip completes"), Terminal != nullptr
                && Terminal->Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed);
            TestTrue(TEXT("The source asset's phases are still unchanged after playback"),
                PhasesEqual(State->PhasesBefore, State->Clip->Get_Phases()));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return ck::IsValid(State->SoftAction) && State->SoftAction.Has<ck::FFragment_ProceduralAction_Compiled>();
        }), WaitSeconds, TEXT("Mirrored soft clip loads, compiles and binds")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto& Compiled = State->SoftAction.Get<ck::FFragment_ProceduralAction_Compiled>();
            TestEqual(TEXT("The soft-load path also expands before binding"), Compiled.Get_Clip()->Get_Slots().Num(), 2);
            TestEqual(TEXT("The soft-load path binds both limbs"), Compiled.Get_BoundLimbIds(),
                TArray<FName>{LeftLimb, RightLimb});
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_Terminal(ReadAll(State->Player, State->SoftIdentity)) != nullptr;
        }), WaitSeconds, TEXT("Mirrored soft clip reaches a terminal")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [State](UWorld*) { DestroyRoot(*State); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMirrorRuntimeRejectsTest,
    "Ck.ProceduralAnimation.Action.Mirror.Runtime.RejectsOccupiedDestinationMissingSourceIncompatibleBinding",
    ck_test_procedural_action_mirror_runtime::TestFlags)

auto
    FCkProceduralActionMirrorRuntimeRejectsTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_mirror_runtime;
    const auto State = MakeShared<FState>();
    AddExpectedError(MirrorCountEnsure, EAutomationExpectedErrorFlags::Contains, 0, false);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            AddSubject(InWorld, false, *State);
            TestTrue(TEXT("Two-limb subject composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(*State); }),
        WaitSeconds, TEXT("Subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player attaches"), AddPlayer(*State)))
            { return; }
            const auto BodyLocalClip = TStrongObjectPtr<UCk_ProceduralAction_Data>{MakeClip(
                ECk_ProceduralAction_TrackSpace::BodyLocal, FVector{60.0, 50.0, 0.0}, FVector{70.0, 60.0, 10.0})};
            const auto TwoSlotClip = TStrongObjectPtr<UCk_ProceduralAction_Data>{MakeClip(
                ECk_ProceduralAction_TrackSpace::BodyLocal, FVector{60.0, 50.0, 0.0}, FVector{70.0, 60.0, 10.0})};
            TwoSlotClip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{
                FCk_ProceduralAction_Slot{SourceSlot}, FCk_ProceduralAction_Slot{MirroredSlot}});
            const auto AnchorClip = TStrongObjectPtr<UCk_ProceduralAction_Data>{MakeClip(
                ECk_ProceduralAction_TrackSpace::AnchorAtStart, FVector::ZeroVector, FVector{10.0, 0.0, 0.0})};

            struct FCase
            {
                const TCHAR* Name;
                UCk_ProceduralAction_Data* Clip;
                FCk_ProceduralAction_LoadedPlaySpec Spec;
                FName Code;
            };
            auto MissingSource = MakeMirroredSpec(BodyLocalClip.Get(), RightLimb);
            MissingSource.Set_Mirrors(TArray<FCk_ProceduralAction_MirrorSpec>{FCk_ProceduralAction_MirrorSpec{
                FName{TEXT("Ghost")}, MirroredSlot, ECk_ProceduralAction_MirrorPlane::XZ}});
            auto MissingBinding = MakeMirroredSpec(BodyLocalClip.Get(), RightLimb);
            MissingBinding.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                FCk_ProceduralAction_SlotBinding{SourceSlot, LeftLimb}});
            auto Anchored = MakeMirroredSpec(AnchorClip.Get(), RightLimb);
            Anchored.Set_AnchorBindings(TArray<FCk_ProceduralAction_AnchorBinding>{
                FCk_ProceduralAction_AnchorBinding{FName{TEXT("Target")}, RootPose}});
            const auto Cases = TArray<FCase>{
                FCase{TEXT("an occupied destination slot"), TwoSlotClip.Get(),
                    MakeMirroredSpec(TwoSlotClip.Get(), RightLimb), TEXT("MirrorDestination")},
                FCase{TEXT("a source slot the clip does not declare"), BodyLocalClip.Get(), MissingSource,
                    TEXT("MirrorSource")},
                FCase{TEXT("a destination without a slot binding"), BodyLocalClip.Get(), MissingBinding,
                    TEXT("MirrorBinding")},
                FCase{TEXT("a destination bound to a limb the subject lacks"), BodyLocalClip.Get(),
                    MakeMirroredSpec(BodyLocalClip.Get(), FName{TEXT("NoSuchLimb")}), TEXT("MirrorBinding")},
                FCase{TEXT("a destination bound to the source's own limb"), BodyLocalClip.Get(),
                    MakeMirroredSpec(BodyLocalClip.Get(), LeftLimb), TEXT("MirrorBinding")},
                FCase{TEXT("an anchor-relative source track"), AnchorClip.Get(), Anchored, TEXT("MirrorSpace")}};

            for (const auto& Case : Cases)
            {
                const auto PhasesBefore = Case.Clip->Get_Phases();
                const auto SlotsBefore = Case.Clip->Get_Slots();

                auto Compiled = ck::procedural_action::FCompiledClip{};
                auto Diagnostics = TArray<ck::procedural_action::FDiagnostic>{};
                TestTrue(FString::Printf(TEXT("%s: the source clip compiles"), Case.Name),
                    ck::procedural_action::Compile(*Case.Clip, Compiled, Diagnostics));
                auto Expanded = ck::procedural_action::FCompiledClip{};
                Diagnostics.Reset();
                TestFalse(FString::Printf(TEXT("%s: expansion rejects"), Case.Name),
                    ck::procedural_action::ExpandMirrors(Compiled, Case.Spec.Get_Mirrors(), Case.Spec.Get_SlotBindings(),
                        State->Subject, Expanded, Diagnostics));
                TestTrue(FString::Printf(TEXT("%s: the diagnostic names the cause (%s)"), Case.Name,
                    *Case.Code.ToString()), Get_HasDiagnostic(Diagnostics, Case.Code));
                TestTrue(FString::Printf(TEXT("%s: no partially expanded clip escapes"), Case.Name),
                    Expanded.Get_Slots().IsEmpty() && Expanded.Get_Phases().IsEmpty());

                auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
                const auto Action = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(Scope, State->Player, Case.Spec);
                if (NOT TestTrue(FString::Printf(TEXT("%s: the start still returns its action"), Case.Name),
                    ck::IsValid(Action)))
                { continue; }
                TestEqual(FString::Printf(TEXT("%s: the loaded clip finishes inside the call"), Case.Name),
                    UCk_Utils_ProceduralAction_UE::Get_State(Action), ECk_ProceduralAction_State::Finished);
                const auto Result = UCk_Utils_ProceduralAction_UE::Get_Result(Action);
                TestEqual(FString::Printf(TEXT("%s: the outcome is Rejected"), Case.Name),
                    Result.Get_Outcome(), ECk_ProceduralAction_Outcome::Rejected);
                TestEqual(FString::Printf(TEXT("%s: the reason is the mirror"), Case.Name),
                    Result.Get_Reason(), FName{TEXT("MirrorRejected")});
                TestFalse(FString::Printf(TEXT("%s: the action never compiled a bound clip"), Case.Name),
                    Action.Has<ck::FFragment_ProceduralAction_Compiled>());
                TestFalse(FString::Printf(TEXT("%s: no limb is claimed"), Case.Name),
                    UCk_Utils_ProceduralArticulation_UE::Get_IsAnyLimbControlled(State->Subject));
                const auto Records = ReadAll(State->Player, UCk_Utils_ProceduralAction_UE::Get_Identity(Action));
                TestTrue(FString::Printf(TEXT("%s: the stream ends without accepted motion"), Case.Name),
                    Get_Terminal(Records) != nullptr && NOT Records.ContainsByPredicate(
                        [](const FCk_ProceduralAction_Record& InRecord)
                        { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose; }));
                TestTrue(FString::Printf(TEXT("%s: the source asset is unchanged"), Case.Name),
                    PhasesEqual(PhasesBefore, Case.Clip->Get_Phases()) && SlotsBefore.Num() == Case.Clip->Get_Slots().Num());
            }

            auto Advisory = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{TwoSlotClip.Get()}};
            Advisory.Set_SlotBindings(TArray<FCk_ProceduralAction_SlotBinding>{
                FCk_ProceduralAction_SlotBinding{SourceSlot, LeftLimb},
                FCk_ProceduralAction_SlotBinding{MirroredSlot, RightLimb}});
            Advisory.Set_Mirrors(TArray<FCk_ProceduralAction_MirrorSpec>{
                FCk_ProceduralAction_MirrorSpec{SourceSlot, MirroredSlot, ECk_ProceduralAction_MirrorPlane::XZ}});
            const auto Report = UCk_Utils_ProceduralActionPlayer_UE::Get_AdmissionReport(State->Player, Advisory);
            TestEqual(TEXT("The advisory report applies the same expansion"), Report.Get_Status(),
                ECk_ProceduralAction_ReportStatus::Invalid);
            TestTrue(TEXT("The advisory report names the occupied destination"),
                Report.Get_Diagnostics().ContainsByPredicate([](const FCk_ProceduralAction_Diagnostic& InDiagnostic)
                { return InDiagnostic.Get_Code() == FName{TEXT("MirrorDestination")}; }));

            auto TooMany = MakeMirroredSpec(BodyLocalClip.Get(), RightLimb);
            auto Mirrors = TArray<FCk_ProceduralAction_MirrorSpec>{};
            for (auto Index = 0; Index <= ck::procedural_action::content_limits::MaxSlots; ++Index)
            {
                Mirrors.Emplace(SourceSlot, FName{*FString::Printf(TEXT("Mirror%d"), Index)},
                    ECk_ProceduralAction_MirrorPlane::XZ);
            }
            TooMany.Set_Mirrors(Mirrors);
            auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Retained = UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(State->Player).Num();
            TestFalse(TEXT("More mirrors than the slot ceiling reject before creating an action"),
                ck::IsValid(UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(Scope, State->Player, TooMany)));
            TestEqual(TEXT("A rejected start reserves no stream"),
                UCk_Utils_ProceduralActionPlayer_UE::Get_RetainedReceiptIdentities(State->Player).Num(), Retained);
            DestroyRoot(*State);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionMirrorRuntimeRestNormalizedTest,
    "Ck.ProceduralAnimation.Action.Mirror.Runtime.RestNormalizedConvertsThroughSubjectFrames",
    ck_test_procedural_action_mirror_runtime::TestFlags)

auto
    FCkProceduralActionMirrorRuntimeRestNormalizedTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_mirror_runtime;
    const auto State = MakeShared<FState>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            AddSubject(InWorld, true, *State);
            TestTrue(TEXT("Subject with an asymmetric right rest frame composes"), ck::IsValid(State->Subject));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State] { return Get_IsSettled(*State); }),
        WaitSeconds, TEXT("Subject settles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Player attaches"), AddPlayer(*State)))
            { return; }
            State->Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{MakeClip(
                ECk_ProceduralAction_TrackSpace::LimbRestNormalized, FVector::ZeroVector, FVector{0.1, 0.1, 0.1})};
            State->Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->Action = UCk_Utils_ProceduralAction_UE::Create_FromLoadedClip(State->Scope, State->Player,
                MakeMirroredSpec(State->Clip.Get(), RightLimb));
            if (NOT TestTrue(TEXT("Rest-normalized mirrored clip binds"), ck::IsValid(State->Action)
                && State->Action.Has<ck::FFragment_ProceduralAction_Compiled>()))
            { return; }
            State->Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->Action);
            const auto& Clip = *State->Action.Get<ck::FFragment_ProceduralAction_Compiled>().Get_Clip();
            TestEqual(TEXT("The destination slot stays rest-normalized in its own limb's frame"),
                Clip.Get_Slots().Num() == 2 ? Clip.Get_Slots()[1].Get_Space() : ECk_ProceduralAction_TrackSpace::BodyLocal,
                ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return Get_Terminal(ReadAll(State->Player, State->Identity)) != nullptr;
        }), WaitSeconds, TEXT("Mirrored rest-normalized clip finishes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto Records = ReadAll(State->Player, State->Identity);
            const auto LeftRest = FVector{60.0, 50.0, 0.0};
            auto Compared = 0;
            auto WorstErrorCm = 0.0;
            auto LeftTravelCm = 0.0;
            for (const auto& Record : Records)
            {
                if (Record.Get_Kind() != ECk_ProceduralAction_RecordKind::AcceptedPose
                    || Record.Get_Receipt().Get_Interval().Get_PhaseId() != PhaseId)
                { continue; }
                const auto& Pose = Record.Get_Receipt().Get_CurrentPose();
                const auto* Left = Pose.Get_Limbs().FindByPredicate([](const FCk_ProceduralArticulation_LimbPose& InLimb)
                { return InLimb.Get_Id() == LeftLimb; });
                const auto* Right = Pose.Get_Limbs().FindByPredicate([](const FCk_ProceduralArticulation_LimbPose& InLimb)
                { return InLimb.Get_Id() == RightLimb; });
                if (Left == nullptr || Right == nullptr)
                { continue; }
                const auto LeftLocal = Pose.Get_RootFrame().InverseTransformPositionNoScale(
                    Left->Get_AcceptedEndpoint().GetTranslation());
                const auto RightLocal = Pose.Get_RootFrame().InverseTransformPositionNoScale(
                    Right->Get_AcceptedEndpoint().GetTranslation());
                const auto Expected = ck::procedural_action::ReflectVector(LeftLocal, ECk_ProceduralAction_MirrorPlane::XZ);
                WorstErrorCm = FMath::Max(WorstErrorCm, (RightLocal - Expected).Size());
                LeftTravelCm = FMath::Max(LeftTravelCm, (LeftLocal - LeftRest).Size());
                ++Compared;
            }
            AddInfo(FString::Printf(TEXT("Compared %d accepted poses; worst mirror error %.4f cm; left travel %.2f cm"),
                Compared, WorstErrorCm, LeftTravelCm));
            TestTrue(TEXT("Accepted poses of the clip phase carry both limbs"), Compared > 0);
            TestTrue(TEXT("The source limb actually moved"), LeftTravelCm > 5.0);
            TestTrue(TEXT("Accepted endpoints are mirror images across the body XZ plane to 0.1 cm"), WorstErrorCm <= 0.1);
            const auto* Terminal = Get_Terminal(Records);
            TestTrue(TEXT("The mirrored rest-normalized clip completes"), Terminal != nullptr
                && Terminal->Get_Result().Get_Outcome() == ECk_ProceduralAction_Outcome::Completed);
            DestroyRoot(*State);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

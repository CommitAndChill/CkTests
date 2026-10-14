#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_ProceduralActionToolkitFixture.h"

#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringCommands.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModel.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModes.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSelectionProxy.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_CurveAdapter.h"

#include "Components/StaticMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace ck_test_procedural_action_editor_consolidation
{
    using namespace ck_test_procedural_action_toolkit;

    constexpr auto MountedFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    constexpr auto CommandFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FName Motion{TEXT("Motion")};

    auto
        Body()
        -> FName
    { return ck::procedural_action_editor::Get_BodyChannelName(); }

    auto
        CanMount(
            FAutomationTestBase& InTest)
        -> bool
    {
        if (FApp::CanEverRender() && FSlateApplication::IsInitialized() && GEditor != nullptr)
        { return true; }
        InTest.AddError(TEXT("The clip editor rows need a real RHI and an initialized Slate editor."));
        return false;
    }

    // Writes one Details field the way the Details panel does: the member, then one property-changed notification.
    auto
        NotifyProxyEdited(
            UCk_ProceduralActionSelectionProxy& InProxy,
            FName InProperty)
        -> void
    {
        auto Event = FPropertyChangedEvent{UCk_ProceduralActionSelectionProxy::StaticClass()->FindPropertyByName(InProperty)};
        InProxy.PostEditChangeProperty(Event);
    }

    auto
        ProxyShows(
            const FCk_ProceduralAction_EditorToolkit& InToolkit,
            ECk_ProceduralActionSelectionProxy_Shows InShows)
        -> bool
    {
        const auto* Proxy = InToolkit.Get_SelectionProxy();
        return Proxy != nullptr && Proxy->_Shows == InShows;
    }

    // The last phase holding a key on InChannel, and its last key's time: the clip's end, which no phase boundary pins.
    auto
        FindKey(
            const UCk_ProceduralAction_Data& InClip,
            FName InChannel,
            FName& OutPhase,
            double& OutTime)
        -> bool
    {
        for (auto Index = InClip.Get_Phases().Num() - 1; Index >= 0; --Index)
        {
            const auto& Phase = InClip.Get_Phases()[Index];
            if (InChannel == Body() && NOT Phase.Get_BodyKeys().IsEmpty())
            {
                OutPhase = Phase.Get_Id();
                OutTime = Phase.Get_BodyKeys().Last().Get_Time().Get_Value();
                return true;
            }
            for (const auto& Track : Phase.Get_LimbTracks())
            {
                if (Track.Get_Slot() == InChannel && NOT Track.Get_PositionKeys().IsEmpty())
                {
                    OutPhase = Phase.Get_Id();
                    OutTime = Track.Get_PositionKeys().Last().Get_Time().Get_Value();
                    return true;
                }
            }
        }
        return false;
    }

    auto
        FindPhase(
            const UCk_ProceduralAction_Data& InClip,
            FName InPhaseId)
        -> const FCk_ProceduralAction_Phase*
    {
        return InClip.Get_Phases().FindByPredicate([InPhaseId](const FCk_ProceduralAction_Phase& InPhase)
            { return InPhase.Get_Id() == InPhaseId; });
    }

    auto
        FindTrack(
            const UCk_ProceduralAction_Data& InClip,
            FName InPhaseId,
            FName InSlot)
        -> const FCk_ProceduralAction_LimbTrack*
    {
        const auto* Phase = FindPhase(InClip, InPhaseId);
        return Phase == nullptr ? nullptr : Phase->Get_LimbTracks().FindByPredicate([InSlot](const FCk_ProceduralAction_LimbTrack& InTrack)
            { return InTrack.Get_Slot() == InSlot; });
    }

    template <typename TKey>
    auto
        KeyAt(
            const TArray<TKey>& InKeys,
            double InTime)
        -> const TKey*
    {
        return InKeys.FindByPredicate([InTime](const TKey& InKey)
            { return ck::procedural_action_editor::IsSamePoseTime(InKey.Get_Time().Get_Value(), InTime); });
    }

    // The static meshes the preview viewport shows: what a refreshed appearance changes.
    auto
        ShownMeshes(
            FCk_ProceduralAction_EditorToolkit& InToolkit)
        -> TSet<const UStaticMesh*>
    {
        auto Meshes = TSet<const UStaticMesh*>{};
        const auto Client = FindViewportClient(InToolkit);
        const auto* World = Client.IsValid() ? Client->GetWorld() : nullptr;
        if (World == nullptr)
        { return Meshes; }
        for (const auto* Component : TObjectRange<UStaticMeshComponent>{})
        {
            if (Component->GetWorld() == World && Component->IsRegistered() && Component->GetStaticMesh() != nullptr)
            { Meshes.Add(Component->GetStaticMesh()); }
        }
        return Meshes;
    }

    auto
        MakeBodyClip()
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional)};
        auto Phase = FCk_ProceduralAction_Phase{Motion, FCk_Time{1.0}};
        Phase.Set_BodyKeys(TArray<FCk_ProceduralAction_TransformKey>{
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector, FQuat::Identity},
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.5}, FVector{10.0, 0.0, 0.0}, FQuat::Identity},
            FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{1.0}, FVector{20.0, 0.0, 0.0}, FQuat::Identity}});
        Clip->Set_BodyChannel(FCk_ProceduralAction_BodyChannel{}.Set_Enabled(true));
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Clip;
    }

    // ----------------------------------------------------------------------------------------------------------------

    struct FSolverBuildState : FDisposableClipToolkit
    {
        TSharedRef<int32> SolverWorlds = MakeShared<int32>(0);
        uint64 RevisionBefore = 0;
        FString KeyBefore;
        FDelegateHandle WorldHandle;

        auto StopCounting() -> void
        {
            if (WorldHandle.IsValid())
            { FWorldDelegates::OnPostWorldInitialization.Remove(WorldHandle); }
            WorldHandle.Reset();
        }

        ~FSolverBuildState() { StopCounting(); }
    };

    class FRunOneSolverBuildPerEdit final : public IAutomationLatentCommand
    {
    public:
        explicit FRunOneSolverBuildPerEdit(TSharedRef<FSolverBuildState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            { return State.Fail(TEXT("timed out")); }
            auto* Editor = State.Toolkit();
            if (Editor == nullptr)
            { return State.Fail(TEXT("the clip editor closed")); }
            const auto Model = Editor->Get_Model();
            const auto* Modes = Editor->Get_Modes();
            if (State.Stage == 0)
            {
                const auto Solving = Modes != nullptr && Modes->Get_Mode() == FCk_ProceduralActionEditorModes::EMode::Solver
                    && Modes->Get_Solver() != nullptr && Modes->Get_FrameCount() > 0;
                if (NOT State.Settle(Solving, TEXT("the Solver has not produced a frame"), 600))
                { return false; }
                if (NOT Solving)
                { return State.Fail(TEXT("the editor never solved the clip")); }
                auto Phase = FName{};
                auto Time = 0.0;
                if (NOT FindKey(*State.Clip, Body(), Phase, Time))
                { return State.Fail(TEXT("the fixture has no body key")); }
                Model->SelectKey(Phase, Body(), FCk_FloatRange_0to1{Time});
                State.Stage = 1;
                return false;
            }
            if (State.Stage == 1)
            {
                if (NOT State.Settle(ProxyShows(*Editor, ECk_ProceduralActionSelectionProxy_Shows::Key), TEXT("Details do not show the key")))
                { return false; }
                const auto Counter = State.SolverWorlds;
                State.WorldHandle = FWorldDelegates::OnPostWorldInitialization.AddLambda(
                    [Counter](UWorld* InWorld, const UWorld::InitializationValues)
                {
                    if (InWorld != nullptr && UCk_EcsWorld_Subsystem_UE::Is_ProductionPreviewWorld(*InWorld))
                    { ++(*Counter); }
                });
                auto* Proxy = Editor->Get_SelectionProxy();
                State.RevisionBefore = Editor->Get_Session()->Get_Revision();
                State.KeyBefore = Proxy->_Position.ToString();
                Proxy->_Position += FVector{5.0, 0.0, 0.0};
                NotifyProxyEdited(*Proxy, GET_MEMBER_NAME_CHECKED(UCk_ProceduralActionSelectionProxy, _Position));
                State.Test->AddInfo(FString::Printf(TEXT("edit: revision %llu -> %llu, key %s, message '%s', mode %d, solver %p"),
                    State.RevisionBefore, Editor->Get_Session()->Get_Revision(), *State.KeyBefore, *Editor->Get_Message(),
                    static_cast<int32>(Modes->Get_Mode()), Modes->Get_Solver()));
                State.Frames = 0;
                State.Stage = 2;
                return false;
            }
            if (++State.Frames < 60)
            { return false; }
            State.StopCounting();
            State.Test->AddInfo(FString::Printf(TEXT("after: revision %llu, status '%s', solver %p, frames %d"),
                Editor->Get_Session()->Get_Revision(), *Editor->Get_StatusText().ToString(), Modes->Get_Solver(), Modes->Get_FrameCount()));
            State.Test->TestEqual(TEXT("one Details edit builds the Solver world once"), *State.SolverWorlds, 1);
            State.Close();
            return true;
        }

    private:
        TSharedRef<FSolverBuildState> _State;
    };

    // ----------------------------------------------------------------------------------------------------------------

    class FRunOnePlayhead final : public IAutomationLatentCommand
    {
    public:
        explicit FRunOnePlayhead(TSharedRef<FDisposableClipToolkit> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            { return State.Fail(TEXT("timed out")); }
            auto* Editor = State.Toolkit();
            if (Editor == nullptr)
            { return State.Fail(TEXT("the clip editor closed")); }
            const auto Model = Editor->Get_Model();
            if (State.Stage == 0)
            {
                const auto Mounted = NOT TextOf(*Editor, FName{TEXT("CkActionEditor.TransportTime")}).IsEmpty();
                if (NOT State.Settle(Mounted, TEXT("the transport shows no time")))
                { return false; }
                Model->SetTime(FCk_Time{0.25});
                State.Stage = 1;
                return false;
            }
            const auto Transport = TextOf(*Editor, FName{TEXT("CkActionEditor.TransportTime")});
            const auto Hud = TextOf(*Editor, FName{TEXT("CkActionEditor.Status")});
            State.Test->TestTrue(FString::Printf(TEXT("the model holds the requested time (%f)"), Model->Get_Time().Get_Seconds()),
                FMath::IsNearlyEqual(Model->Get_Time().Get_Seconds(), 0.25, 1.0e-6));
            State.Test->TestTrue(FString::Printf(TEXT("the transport shows it (%s)"), *Transport),
                Transport.StartsWith(TEXT("0.25 s | frame 15")));
            State.Test->TestTrue(FString::Printf(TEXT("the viewport HUD shows it (%s)"), *Hud), Hud.Contains(TEXT("| 0.25 s |")));
            const auto Session = Editor->Get_Session();
            auto PhaseId = FName{};
            auto Fraction = 0.0;
            State.Test->TestTrue(TEXT("the session places the model's time in a section"),
                Session->ToRawFraction(Model->Get_Time(), PhaseId, Fraction));
            State.Test->TestTrue(FString::Printf(TEXT("the transport names that section and fraction (%s)"), *Transport),
                Transport.EndsWith(FString::Printf(TEXT("%s %.2f"), *PhaseId.ToString(), Fraction)));
            State.Close();
            return true;
        }

    private:
        TSharedRef<FDisposableClipToolkit> _State;
    };

    // ----------------------------------------------------------------------------------------------------------------

    struct FAppearanceState : FDisposableClipToolkit
    {
        TSet<const UStaticMesh*> Before;
    };

    class FRunSubjectUndoRefreshesAppearance final : public IAutomationLatentCommand
    {
    public:
        explicit FRunSubjectUndoRefreshesAppearance(TSharedRef<FAppearanceState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            { return State.Fail(TEXT("timed out")); }
            auto* Editor = State.Toolkit();
            if (Editor == nullptr)
            { return State.Fail(TEXT("the clip editor closed")); }
            if (State.Stage == 0)
            {
                State.Before = ShownMeshes(*Editor);
                if (NOT State.Settle(NOT State.Before.IsEmpty(), TEXT("the preview shows no mesh"), 300))
                { return false; }
                if (State.Before.IsEmpty())
                { return State.Fail(TEXT("the preview never showed its subject")); }
                Editor->SetScenario(nullptr);
                State.Stage = 1;
                return false;
            }
            if (State.Stage == 1)
            {
                const auto Changed = NOT ShownMeshes(*Editor).Includes(State.Before) || ShownMeshes(*Editor).Num() != State.Before.Num();
                if (NOT State.Settle(Changed, TEXT("clearing the subject did not change the preview")))
                { return false; }
                if (NOT State.Test->TestTrue(TEXT("clearing the subject changes the preview"), Changed))
                { State.Close(); return true; }
                if (NOT State.Test->TestTrue(TEXT("undo the subject change"), GEditor->UndoTransaction()))
                { State.Close(); return true; }
                State.Stage = 2;
                return false;
            }
            const auto Restored = ShownMeshes(*Editor);
            const auto Same = Restored.Num() == State.Before.Num() && Restored.Includes(State.Before);
            if (NOT State.Settle(Same, TEXT("undo did not restore the preview"), 60))
            { return false; }
            State.Test->TestTrue(TEXT("undoing a subject change refreshes the preview's meshes"), Same);
            State.Close();
            return true;
        }

    private:
        TSharedRef<FAppearanceState> _State;
    };

    // ----------------------------------------------------------------------------------------------------------------

    struct FRenameState : FDisposableClipToolkit
    {
        FName Original;
        FName Renamed{TEXT("RenamedSection")};
    };

    class FRunRenameUndoKeepsSelection final : public IAutomationLatentCommand
    {
    public:
        explicit FRunRenameUndoKeepsSelection(TSharedRef<FRenameState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            { return State.Fail(TEXT("timed out")); }
            auto* Editor = State.Toolkit();
            if (Editor == nullptr)
            { return State.Fail(TEXT("the clip editor closed")); }
            const auto Model = Editor->Get_Model();
            if (State.Stage == 0)
            {
                if (State.Clip->Get_Phases().Num() < 2 && NOT Editor->AddSection())
                { return State.Fail(TEXT("could not add a second section")); }
                State.Stage = 1;
                return false;
            }
            if (State.Stage == 1)
            {
                if (NOT State.Settle(State.Clip->Get_Phases().Num() >= 2, TEXT("the second section did not appear")))
                { return false; }
                State.Original = State.Clip->Get_Phases()[1].Get_Id();
                Model->SelectSection(State.Original);
                State.Stage = 2;
                return false;
            }
            if (State.Stage == 2)
            {
                if (NOT State.Settle(ProxyShows(*Editor, ECk_ProceduralActionSelectionProxy_Shows::Section), TEXT("Details do not show the section")))
                { return false; }
                auto* Proxy = Editor->Get_SelectionProxy();
                Proxy->_SectionName = State.Renamed;
                NotifyProxyEdited(*Proxy, GET_MEMBER_NAME_CHECKED(UCk_ProceduralActionSelectionProxy, _SectionName));
                State.Stage = 3;
                return false;
            }
            if (State.Stage == 3)
            {
                const auto Renamed = State.Clip->Get_Phases()[1].Get_Id() == State.Renamed && Model->Get_SelectedPhase() == State.Renamed;
                if (NOT State.Settle(Renamed, TEXT("the rename did not land")))
                { return false; }
                if (NOT State.Test->TestTrue(TEXT("the section and the selection are renamed"), Renamed)
                    || NOT State.Test->TestTrue(TEXT("undo the rename"), GEditor->UndoTransaction()))
                { State.Close(); return true; }
                State.Frames = 0;
                State.Stage = 4;
                return false;
            }
            if (++State.Frames < 10)
            { return false; }
            State.Test->TestEqual(TEXT("undo restores the section's name"), State.Clip->Get_Phases()[1].Get_Id(), State.Original);
            State.Test->TestEqual(TEXT("the selection follows the restored name"), Model->Get_SelectedPhase(), State.Original);
            State.Close();
            return true;
        }

    private:
        TSharedRef<FRenameState> _State;
    };

    // ----------------------------------------------------------------------------------------------------------------

    enum class EGizmoCase : uint8 { RotateBody, BendLimb, ReachPolicyProxy };

    struct FGizmoState : FDisposableClipToolkit
    {
        EGizmoCase Case = EGizmoCase::RotateBody;
        FName Channel;
        FName Phase;
        double Time = 0.0;
        FQuat RotationBefore = FQuat::Identity;
        TOptional<FVector> BendBefore;
        ECk_ProceduralAction_ReachPolicy PolicyBefore = ECk_ProceduralAction_ReachPolicy::RejectUnreachable;
    };

    class FRunGizmo final : public IAutomationLatentCommand
    {
    public:
        explicit FRunGizmo(TSharedRef<FGizmoState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            { return State.Fail(TEXT("timed out")); }
            auto* Editor = State.Toolkit();
            if (Editor == nullptr)
            { return State.Fail(TEXT("the clip editor closed")); }
            const auto Model = Editor->Get_Model();
            if (State.Stage == 0)
            {
                Model->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Authored);
                State.Channel = State.Case == EGizmoCase::RotateBody ? Body()
                    : State.Clip->Get_Slots().IsEmpty() ? NAME_None : State.Clip->Get_Slots()[0].Get_Id();
                if (NOT FindKey(*State.Clip, State.Channel, State.Phase, State.Time))
                { return State.Fail(TEXT("the fixture has no key on the channel")); }
                if (State.Case == EGizmoCase::RotateBody)
                {
                    State.RotationBefore = KeyAt(FindPhase(*State.Clip, State.Phase)->Get_BodyKeys(), State.Time)->Get_Rotation();
                }
                else if (const auto* Track = FindTrack(*State.Clip, State.Phase, State.Channel))
                {
                    if (const auto* Bend = KeyAt(Track->Get_BendKeys(), State.Time))
                    { State.BendBefore = Bend->Get_Value(); }
                    State.PolicyBefore = Track->Get_ReachPolicy();
                }
                Model->SelectKey(State.Phase, State.Channel, FCk_FloatRange_0to1{State.Time});
                State.Stage = 1;
                return false;
            }
            if (State.Stage == 1)
            {
                const auto Client = FindViewportClient(*Editor);
                const auto Ready = ProxyShows(*Editor, ECk_ProceduralActionSelectionProxy_Shows::Key) && Client.IsValid()
                    && Client->Viewport != nullptr && (Editor->Get_Modes() == nullptr || Editor->Get_Modes()->Get_DisplayedFrame() == nullptr);
                if (NOT State.Settle(Ready, TEXT("the key is not editable in the viewport"), 300))
                { return false; }
                if (NOT Ready)
                { return State.Fail(TEXT("the viewport never offered the key")); }
                if (State.Case == EGizmoCase::ReachPolicyProxy)
                {
                    auto* Proxy = Editor->Get_SelectionProxy();
                    Proxy->_ReachPolicy = State.PolicyBefore == ECk_ProceduralAction_ReachPolicy::RejectUnreachable
                        ? ECk_ProceduralAction_ReachPolicy::ProjectToReach : ECk_ProceduralAction_ReachPolicy::RejectUnreachable;
                    NotifyProxyEdited(*Proxy, GET_MEMBER_NAME_CHECKED(UCk_ProceduralActionSelectionProxy, _ReachPolicy));
                    State.Stage = 3;
                    return false;
                }
                Client->SetWidgetMode(State.Case == EGizmoCase::RotateBody ? UE::Widget::WM_Rotate : UE::Widget::WM_Scale);
                State.Stage = 2;
                return false;
            }
            if (State.Stage == 2)
            {
                const auto Client = FindViewportClient(*Editor);
                Client->TrackingStarted(FInputEventState{Client->Viewport, EKeys::LeftMouseButton, IE_Pressed}, true, false);
                auto Drag = State.Case == EGizmoCase::RotateBody ? FVector::ZeroVector : FVector{0.0, 25.0, 15.0};
                auto Rotation = State.Case == EGizmoCase::RotateBody ? FRotator{0.0, 30.0, 0.0} : FRotator::ZeroRotator;
                auto Scale = FVector::ZeroVector;
                Client->InputWidgetDelta(Client->Viewport, EAxisList::XYZ, Drag, Rotation, Scale);
                Client->TrackingStopped();
                State.Stage = 3;
                return false;
            }
            const auto* Phase = FindPhase(*State.Clip, State.Phase);
            const auto* Track = FindTrack(*State.Clip, State.Phase, State.Channel);
            auto Written = false;
            switch (State.Case)
            {
                case EGizmoCase::RotateBody:
                {
                    const auto* Key = Phase == nullptr ? nullptr : KeyAt(Phase->Get_BodyKeys(), State.Time);
                    Written = Key != nullptr && Key->Get_Rotation().AngularDistance(State.RotationBefore) > FMath::DegreesToRadians(10.0);
                    break;
                }
                case EGizmoCase::BendLimb:
                {
                    const auto* Key = Track == nullptr ? nullptr : KeyAt(Track->Get_BendKeys(), State.Time);
                    Written = Key != nullptr && (NOT State.BendBefore.IsSet() || NOT Key->Get_Value().Equals(State.BendBefore.GetValue(), 1.0e-3));
                    break;
                }
                case EGizmoCase::ReachPolicyProxy:
                    Written = Track != nullptr && Track->Get_ReachPolicy() != State.PolicyBefore;
                    break;
            }
            if (NOT State.Settle(Written, TEXT("the edit did not reach the clip"), 30))
            { return false; }
            State.Test->TestTrue(TEXT("the edit wrote the clip's key"), Written);
            State.Close();
            return true;
        }

    private:
        TSharedRef<FGizmoState> _State;
    };

    auto
        StartGizmoCase(
            FAutomationTestBase& InTest,
            EGizmoCase InCase,
            const TCHAR* InSource,
            const TCHAR* InPreview,
            const TCHAR* InName)
        -> bool
    {
        if (NOT CanMount(InTest))
        { return false; }
        auto State = MakeShared<FGizmoState>();
        State->Test = &InTest;
        State->Case = InCase;
        if (NOT InTest.TestTrue(TEXT("a disposable example clip opens in the clip editor"), State->Open(InSource, InPreview, InName)))
        { State->Close(); return false; }
        State->DeadlineSeconds = FPlatformTime::Seconds() + 30.0;
        ADD_LATENT_AUTOMATION_COMMAND(FRunGizmo(State));
        return true;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Refresh_OneSolverBuildPerEdit,
    "Ck.ProceduralAnimation.Action.Editor.Refresh.OneSolverBuildPerEdit",
    ck_test_procedural_action_editor_consolidation::MountedFlags)

auto
    FCkTest_ProceduralAction_Editor_Refresh_OneSolverBuildPerEdit::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    if (NOT CanMount(*this))
    { return false; }
    auto State = MakeShared<FSolverBuildState>();
    State->Test = this;
    if (NOT TestTrue(TEXT("a disposable BodyPulse opens in the clip editor"), State->Open(BodyPulsePath, BodyPulsePreviewPath,
        TEXT("PulseSolverBuild"))))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 60.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunOneSolverBuildPerEdit(State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Time_OnePlayhead,
    "Ck.ProceduralAnimation.Action.Editor.Time.OnePlayhead",
    ck_test_procedural_action_editor_consolidation::MountedFlags)

auto
    FCkTest_ProceduralAction_Editor_Time_OnePlayhead::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    if (NOT CanMount(*this))
    { return false; }
    auto State = MakeShared<FDisposableClipToolkit>();
    State->Test = this;
    if (NOT TestTrue(TEXT("a disposable BodyPulse opens in the clip editor"), State->Open(BodyPulsePath, BodyPulsePreviewPath,
        TEXT("PulsePlayhead"))))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 30.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunOnePlayhead(State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Undo_SubjectChangeRefreshesAppearance,
    "Ck.ProceduralAnimation.Action.Editor.Undo.SubjectChangeRefreshesAppearance",
    ck_test_procedural_action_editor_consolidation::MountedFlags)

auto
    FCkTest_ProceduralAction_Editor_Undo_SubjectChangeRefreshesAppearance::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    if (NOT CanMount(*this))
    { return false; }
    auto State = MakeShared<FAppearanceState>();
    State->Test = this;
    if (NOT TestTrue(TEXT("a disposable BodyPulse opens in the clip editor"), State->Open(BodyPulsePath, BodyPulsePreviewPath,
        TEXT("PulseSubjectUndo"))))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 30.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunSubjectUndoRefreshesAppearance(State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Undo_RenameSectionKeepsSelection,
    "Ck.ProceduralAnimation.Action.Editor.Undo.RenameSectionKeepsSelection",
    ck_test_procedural_action_editor_consolidation::MountedFlags)

auto
    FCkTest_ProceduralAction_Editor_Undo_RenameSectionKeepsSelection::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    if (NOT CanMount(*this))
    { return false; }
    auto State = MakeShared<FRenameState>();
    State->Test = this;
    if (NOT TestTrue(TEXT("a disposable BodyPulse opens in the clip editor"), State->Open(BodyPulsePath, BodyPulsePreviewPath,
        TEXT("PulseRenameUndo"))))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 30.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunRenameUndoKeepsSelection(State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Gizmo_RotateWritesOrientation,
    "Ck.ProceduralAnimation.Action.Editor.Gizmo.RotateWritesOrientation",
    ck_test_procedural_action_editor_consolidation::MountedFlags)

auto
    FCkTest_ProceduralAction_Editor_Gizmo_RotateWritesOrientation::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    return StartGizmoCase(*this, EGizmoCase::RotateBody, BodyPulsePath, BodyPulsePreviewPath, TEXT("PulseRotate"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Gizmo_BendWritesBend,
    "Ck.ProceduralAnimation.Action.Editor.Gizmo.BendWritesBend",
    ck_test_procedural_action_editor_consolidation::MountedFlags)

auto
    FCkTest_ProceduralAction_Editor_Gizmo_BendWritesBend::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    return StartGizmoCase(*this, EGizmoCase::BendLimb, LimbNodPath, LimbNodPreviewPath, TEXT("NodBend"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Details_ReachPolicyWritesTheTrack,
    "Ck.ProceduralAnimation.Action.Editor.Details.ReachPolicyWritesTheTrack",
    ck_test_procedural_action_editor_consolidation::MountedFlags)

auto
    FCkTest_ProceduralAction_Editor_Details_ReachPolicyWritesTheTrack::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    return StartGizmoCase(*this, EGizmoCase::ReachPolicyProxy, LimbNodPath, LimbNodPreviewPath, TEXT("NodReach"));
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Commands_DeleteKeyReportsAMissingKey,
    "Ck.ProceduralAnimation.Action.Editor.Commands.DeleteKeyReportsAMissingKey",
    ck_test_procedural_action_editor_consolidation::CommandFlags)

auto
    FCkTest_ProceduralAction_Editor_Commands_DeleteKeyReportsAMissingKey::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    auto Clip = MakeBodyClip();
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    TestFalse(TEXT("deleting where no key is refused"), Commands.DeleteKey(Motion, Body(), 0.25, Error));
    TestFalse(TEXT("the refusal says why"), Error.IsEmpty());
    if (NOT TestTrue(TEXT("deleting an existing key succeeds"), Commands.DeleteKey(Motion, Body(), 0.5, Error)))
    { AddInfo(Error); return false; }
    TestEqual(TEXT("the key is gone"), Clip->Get_Phases()[0].Get_BodyKeys().Num(), 2);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Commands_RemoveEventRemovesMarkersAndWindows,
    "Ck.ProceduralAnimation.Action.Editor.Commands.RemoveEventRemovesMarkersAndWindows",
    ck_test_procedural_action_editor_consolidation::CommandFlags)

auto
    FCkTest_ProceduralAction_Editor_Commands_RemoveEventRemovesMarkersAndWindows::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    auto Clip = MakeBodyClip();
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto Hit = FName{TEXT("Hit")};
    const auto Danger = FName{TEXT("Danger")};
    if (NOT TestTrue(TEXT("add a marker"), Commands.AddMarker(Hit, Motion, 0.5, Error))
        || NOT TestTrue(TEXT("add a window"), Commands.AddWindow(Danger, Motion, 0.25, Motion, 0.75, Error)))
    { AddInfo(Error); return false; }
    TestTrue(TEXT("remove the marker"), Commands.RemoveEvent(Hit, Error));
    TestTrue(TEXT("the marker is gone"), Clip->Get_Markers().IsEmpty());
    TestTrue(TEXT("remove the window"), Commands.RemoveEvent(Danger, Error));
    TestTrue(TEXT("the window is gone"), Clip->Get_Windows().IsEmpty());
    TestFalse(TEXT("removing an unknown event is refused"), Commands.RemoveEvent(FName{TEXT("Missing")}, Error));
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Commands_SetPhaseEasingAndUndo,
    "Ck.ProceduralAnimation.Action.Editor.Commands.SetPhaseEasingAndUndo",
    ck_test_procedural_action_editor_consolidation::CommandFlags)

auto
    FCkTest_ProceduralAction_Editor_Commands_SetPhaseEasingAndUndo::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = MakeBodyClip();
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    if (NOT TestTrue(TEXT("ease the phase"), Commands.SetPhaseEasing(Motion, ECk_ProceduralAction_Easing::InOutCubic, Error)))
    { AddInfo(Error); return false; }
    TestEqual(TEXT("the phase eases"), Clip->Get_Phases()[0].Get_Easing(), ECk_ProceduralAction_Easing::InOutCubic);
    TestFalse(TEXT("an unknown phase is refused"), Commands.SetPhaseEasing(FName{TEXT("Missing")}, ECk_ProceduralAction_Easing::InQuad, Error));
    if (NOT TestTrue(TEXT("undo the easing"), GEditor->UndoTransaction()))
    { return false; }
    TestEqual(TEXT("undo restores linear"), Clip->Get_Phases()[0].Get_Easing(), ECk_ProceduralAction_Easing::Linear);
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Commands_MoveWindowKeepsItsLength,
    "Ck.ProceduralAnimation.Action.Editor.Commands.MoveWindowKeepsItsLength",
    ck_test_procedural_action_editor_consolidation::CommandFlags)

auto
    FCkTest_ProceduralAction_Editor_Commands_MoveWindowKeepsItsLength::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    auto Clip = MakeBodyClip();
    auto Commands = FCk_ProceduralActionAuthoringCommands{Clip.Get()};
    auto Error = FString{};
    const auto Danger = FName{TEXT("Danger")};
    if (NOT TestTrue(TEXT("add a window"), Commands.AddWindow(Danger, Motion, 0.25, Motion, 0.5, Error)))
    { AddInfo(Error); return false; }
    if (NOT TestTrue(TEXT("drag the window by its body"), Commands.MoveWindow(Danger,
        FCk_ProceduralAction_TimeRef{Motion, FCk_FloatRange_0to1{0.5}}, FCk_ProceduralAction_TimeRef{Motion, FCk_FloatRange_0to1{0.75}}, Error)))
    { AddInfo(Error); return false; }
    const auto& Window = Clip->Get_Windows()[0];
    TestTrue(TEXT("the start moved"), FMath::IsNearlyEqual(Window.Get_Start().Get_Time().Get_Value(), 0.5));
    TestTrue(TEXT("the end moved with it"), FMath::IsNearlyEqual(Window.Get_End().Get_Time().Get_Value(), 0.75));
    TestFalse(TEXT("an end before the start is refused"), Commands.MoveWindow(Danger,
        FCk_ProceduralAction_TimeRef{Motion, FCk_FloatRange_0to1{0.75}}, FCk_ProceduralAction_TimeRef{Motion, FCk_FloatRange_0to1{0.5}}, Error));
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_Editor_Curves_RejectsDuplicateKeyTimes,
    "Ck.ProceduralAnimation.Action.Editor.Curves.RejectsDuplicateKeyTimes",
    ck_test_procedural_action_editor_consolidation::CommandFlags)

auto
    FCkTest_ProceduralAction_Editor_Curves_RejectsDuplicateKeyTimes::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_editor_consolidation;
    auto Clip = MakeBodyClip();
    auto EasedPhases = Clip->Get_Phases();
    EasedPhases[0].Set_Easing(ECk_ProceduralAction_Easing::InCubic);
    Clip->Set_Phases(EasedPhases);
    const auto PhasesBefore = Clip->Get_Phases();
    auto Session = MakeShared<FCk_ProceduralActionAuthoringSession>(Clip.Get());
    auto Model = MakeShared<FCk_ProceduralActionEditorModel>();
    Model->SelectSection(Motion);
    auto Adapter = MakeShared<FCk_ProceduralAction_CurveAdapter>();
    Adapter->Init(Session, Model);
    Adapter->Refresh();
    auto* Curve = Adapter->FindCurve(Body(), 0);
    if (NOT TestNotNull(TEXT("the body's X axis has a curve"), Curve) || NOT TestEqual(TEXT("three keys"), Curve->GetNumKeys(), 3))
    { return false; }
    auto Handles = TArray<FKeyHandle>{};
    for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
    { Handles.Add(*It); }
    // The middle key dragged onto the first: both now map to one key time.
    Curve->SetKeyTime(Handles[1], Curve->GetKeyTime(Handles[0]));
    Adapter->OnCurveModified(Body(), 0);
    TestTrue(FString::Printf(TEXT("the edit is refused with the compiler's KeyTime diagnostic (%s)"), *Adapter->GetStatusText().ToString()),
        Adapter->GetStatusText().ToString().Contains(TEXT("KeyTime")));
    auto Unchanged = Clip->Get_Phases().Num() == PhasesBefore.Num();
    for (auto Index = 0; Unchanged && Index < PhasesBefore.Num(); ++Index)
    {
        Unchanged = FCk_ProceduralAction_Phase::StaticStruct()->CompareScriptStruct(
            &Clip->Get_Phases()[Index], &PhasesBefore[Index], PPF_None);
    }
    TestTrue(TEXT("the asset is unchanged"), Unchanged);
    return NOT HasAnyErrors();
}

#endif

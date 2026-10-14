#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModel.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModes.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPoseGroups.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSelectionProxy.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_EditorToolkit.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"

namespace ck_test_procedural_action_pose_workspace
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    constexpr auto MaxSettleFrames = 60;
    constexpr auto FrameSeconds = 1.0 / 60.0;
    const TCHAR* BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* BodyPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    const FName BodyRowTag{TEXT("CkActionEditor.Channel.Body")};
    const FName BodyRecordTag{TEXT("CkActionEditor.Record.Body")};
    const FVector BodyEdit{5.0, 0.0, 0.0};

    auto
        FindTagged(
            const TSharedRef<SWidget>& InWidget,
            FName InTag,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetTag() == InTag)
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindTagged(Children->GetChildAt(Index), InTag, InDepth + 1))
            { return Found; }
        }
        return {};
    }

    auto
        FindToolkit(
            UAssetEditorSubsystem* InEditors,
            UCk_ProceduralAction_Data* InClip)
        -> FCk_ProceduralAction_EditorToolkit*
    {
        auto* Instance = InEditors != nullptr && InClip != nullptr ? InEditors->FindEditorForAsset(InClip, false) : nullptr;
        if (Instance == nullptr || Instance->GetEditorName() != FCk_ProceduralAction_EditorToolkit::ToolkitName)
        { return nullptr; }
        return static_cast<FCk_ProceduralAction_EditorToolkit*>(Instance);
    }

    auto
        FindTab(
            FCk_ProceduralAction_EditorToolkit& InToolkit,
            FName InTabId)
        -> TSharedPtr<SDockTab>
    {
        const auto TabManager = InToolkit.GetAssociatedTabManager();
        return TabManager.IsValid() ? TabManager->FindExistingLiveTab(FTabId{InTabId}) : TSharedPtr<SDockTab>{};
    }

    auto
        FindInToolkit(
            FCk_ProceduralAction_EditorToolkit& InToolkit,
            FName InTag)
        -> TSharedPtr<SWidget>
    {
        for (const auto TabId : {FCk_ProceduralAction_EditorToolkit::ViewportTabId, FCk_ProceduralAction_EditorToolkit::ChannelsTabId,
            FCk_ProceduralAction_EditorToolkit::DetailsTabId, FCk_ProceduralAction_EditorToolkit::TimelineTabId,
            FCk_ProceduralAction_EditorToolkit::CurvesTabId})
        {
            const auto Tab = FindTab(InToolkit, TabId);
            if (NOT Tab.IsValid())
            { continue; }
            if (const auto Found = FindTagged(Tab->GetContent(), InTag))
            { return Found; }
        }
        return {};
    }

    auto
        IsReachable(
            const TSharedRef<SWidget>& InWidget)
        -> bool
    {
        auto Path = FWidgetPath{};
        return FSlateApplication::Get().FindPathToWidget(InWidget, Path) && Path.IsValid();
    }

    struct FHitDiagnostic
    {
        FString Point;
        FString HitPath;
        bool HitTarget = false;
    };

    auto
        ClickWidget(
            const TSharedRef<SWidget>& InWidget,
            FHitDiagnostic* OutDiagnostic = nullptr)
        -> bool
    {
        if (NOT InWidget->IsEnabled())
        { return false; }
        auto& Slate = FSlateApplication::Get();
        auto Path = FWidgetPath{};
        if (NOT Slate.FindPathToWidget(InWidget, Path) || NOT Path.IsValid())
        { return false; }
        const auto Window = Slate.FindWidgetWindow(InWidget);
        if (NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid())
        { return false; }
        const auto Position = FVector2D{Path.Widgets.Last().Geometry.GetAbsolutePositionAtCoordinates(FVector2D{0.5, 0.5})};
        if (OutDiagnostic != nullptr)
        {
            const auto Hit = Slate.LocateWindowUnderMouse(Position, Slate.GetInteractiveTopLevelWindows());
            OutDiagnostic->Point = Position.ToString();
            OutDiagnostic->HitPath = Hit.IsValid() ? Hit.ToString() : TEXT("None");
            OutDiagnostic->HitTarget = Hit.IsValid() && Hit.ContainsWidget(&InWidget.Get());
        }
        const auto Released = TSet<FKey>{};
        const auto Pressed = TSet<FKey>{EKeys::LeftMouseButton};
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released, EKeys::Invalid, 0.0f, FModifierKeysState{}});
        const auto Down = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto Up = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto PressHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        Slate.ProcessMouseButtonUpEvent(Up);
        return PressHandled;
    }

    auto
        SaveCapture(
            const TSharedRef<SWidget>& InWidget,
            const FString& InName)
        -> FString
    {
        auto Pixels = TArray<FColor>{};
        auto Size = FIntVector{};
        if (NOT FSlateApplication::Get().TakeScreenshot(InWidget, Pixels, Size)
            || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() < Size.X * Size.Y)
        { return {}; }
        for (auto& Pixel : Pixels)
        { Pixel.A = 255; }
        const auto Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ProceduralAction"),
            TEXT("PoseWorkspace"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const auto Path = FPaths::Combine(Directory, InName + TEXT(".png"));
        auto Compressed = TArray64<uint8>{};
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>{Pixels.GetData(), Size.X * Size.Y},
            Compressed);
        auto Bytes = TArray<uint8>{};
        Bytes.Append(Compressed.GetData(), Compressed.Num());
        return FFileHelper::SaveArrayToFile(Bytes, *Path) ? Path : FString{};
    }

    // Writes the body key position the way the Details panel does: the member, then one property-changed notification.
    auto
        EditKeyPosition(
            UCk_ProceduralActionSelectionProxy& InProxy,
            const FVector& InPosition)
        -> void
    {
        InProxy._Position = InPosition;
        auto Event = FPropertyChangedEvent{UCk_ProceduralActionSelectionProxy::StaticClass()->FindPropertyByName(
            GET_MEMBER_NAME_CHECKED(UCk_ProceduralActionSelectionProxy, _Position))};
        InProxy.PostEditChangeProperty(Event);
    }

    // A disposable copy of BodyPulse in its own content root, so the shipped example is never edited.
    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UTransactor> PreviousTransactions;
        TStrongObjectPtr<UTransBuffer> PrivateTransactions;
        FString MountRoot;
        FString MountDirectory;
        FString PackageName;
        FString FailureName;
        FString LastWaitReason;
        double DeadlineSeconds = 0.0;
        int32 Stage = 0;
        int32 SettleFrames = 0;
        int32 Actions = 0;
        int32 FrameSteps = 0;
        int32 SolvedAttempts = 0;
        int32 DistinctSteps = 0;
        int32 SolvedFrames = 0;
        int64 LastObservedStep = INDEX_NONE;
        TSet<FGuid> GroupsBefore;
        FGuid RequestedPose;
        FName RequestedPhase;
        FCk_FloatRange_0to1 RequestedTime;
        FCk_Time PhaseStart;
        FVector BodyBefore = FVector::ZeroVector;
        bool Mounted = false;

        auto Toolkit() const -> FCk_ProceduralAction_EditorToolkit*
        { return FindToolkit(Editors.Get(), Clip.Get()); }

        // True once InCondition holds, or once it has been given MaxSettleFrames editor frames; the caller asserts after.
        auto Settle(bool InCondition, const FString& InReason) -> bool
        {
            if (InCondition)
            { SettleFrames = 0; return true; }
            LastWaitReason = InReason;
            if (++SettleFrames < MaxSettleFrames)
            { return false; }
            SettleFrames = 0;
            return true;
        }

        auto Groups() const -> TSet<FGuid>
        {
            auto Ids = TSet<FGuid>{};
            if (Clip.IsValid())
            {
                for (const auto& Group : Clip->Get_EditorPoseGroups())
                { Ids.Add(Group.Get_Id()); }
            }
            return Ids;
        }

        auto NewGroup() const -> const FCk_ProceduralAction_PoseGroup*
        {
            if (NOT Clip.IsValid())
            { return nullptr; }
            return Clip->Get_EditorPoseGroups().FindByPredicate([this](const FCk_ProceduralAction_PoseGroup& InGroup)
                { return NOT GroupsBefore.Contains(InGroup.Get_Id()); });
        }

        auto BodyKeyAt(FName InPhaseId, FCk_FloatRange_0to1 InTime) const -> TOptional<FVector>
        {
            const auto* Phase = Clip.IsValid() ? Clip->Get_Phases().FindByPredicate(
                [InPhaseId](const FCk_ProceduralAction_Phase& InPhase) { return InPhase.Get_Id() == InPhaseId; }) : nullptr;
            const auto* Key = Phase == nullptr ? nullptr : Phase->Get_BodyKeys().FindByPredicate(
                [InTime](const FCk_ProceduralAction_TransformKey& InKey)
                { return ck::procedural_action_editor::IsSamePoseTime(InKey.Get_Time().Get_Value(), InTime.Get_Value()); });
            return Key == nullptr ? TOptional<FVector>{} : TOptional<FVector>{Key->Get_Translation()};
        }

        auto RequestedBody() const -> TOptional<FVector>
        { return BodyKeyAt(RequestedPhase, RequestedTime); }

        auto Status() const -> FString
        {
            const auto* Editor = Toolkit();
            return Editor != nullptr ? Editor->Get_StatusText().ToString() : FString{};
        }

        auto Message() const -> FString
        {
            const auto* Editor = Toolkit();
            return Editor != nullptr ? Editor->Get_Message() : FString{};
        }

        auto ProxyShowsBodyKey() const -> bool
        {
            const auto* Editor = Toolkit();
            const auto* Proxy = Editor != nullptr ? Editor->Get_SelectionProxy() : nullptr;
            return Proxy != nullptr && Proxy->_Shows == ECk_ProceduralActionSelectionProxy_Shows::Key
                && Proxy->_Channel == FCk_ProceduralAction_PoseGroup::Get_BodyChannelName();
        }

        auto ClickTagged(FName InTag) -> bool
        {
            auto* Editor = Toolkit();
            const auto Target = Editor != nullptr ? FindInToolkit(*Editor, InTag) : TSharedPtr<SWidget>{};
            auto Hit = FHitDiagnostic{};
            if (NOT Target.IsValid() || NOT ClickWidget(Target.ToSharedRef(), &Hit))
            {
                LastWaitReason = FString::Printf(TEXT("routed click on %s failed: found=%d enabled=%d point=%s hitTarget=%d hitPath=%s"),
                    *InTag.ToString(), Target.IsValid(), Target.IsValid() && Target->IsEnabled(), *Hit.Point, Hit.HitTarget,
                    *Hit.HitPath);
                return false;
            }
            return true;
        }

        auto Fail(const FString& InMessage) -> bool
        {
            auto* Editor = Toolkit();
            const auto Tab = Editor != nullptr ? FindTab(*Editor, FCk_ProceduralAction_EditorToolkit::ViewportTabId)
                : TSharedPtr<SDockTab>{};
            const auto Capture = Tab.IsValid() ? SaveCapture(Tab->GetContent(), FailureName) : FString{};
            Test->AddError(FString::Printf(TEXT("%s (stage %d; last wait: %s; message: %s; failure screenshot: %s)"),
                *InMessage, Stage, *LastWaitReason, *Message(), Capture.IsEmpty() ? TEXT("unavailable") : *Capture));
            Close();
            return true;
        }

        auto Open(const FString& InName) -> bool
        {
            if (GEditor == nullptr || GEditor->Trans == nullptr || GEditor->Trans->IsActive())
            { return false; }
            const auto Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            MountRoot = FString::Printf(TEXT("/CkActionPoseWorkspace_%s/"), *Unique);
            MountDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ProceduralAction"),
                TEXT("PoseWorkspace"), Unique) + TEXT("/");
            if (NOT IFileManager::Get().MakeDirectory(*MountDirectory, true))
            { return false; }
            FPackageName::RegisterMountPoint(MountRoot, MountDirectory);
            Mounted = true;
            PreviousTransactions.Reset(GEditor->Trans.Get());
            PrivateTransactions.Reset(NewObject<UTransBuffer>());
            PrivateTransactions->Initialize(8 * 1024 * 1024);
            GEditor->Trans = PrivateTransactions.Get();

            PackageName = MountRoot + InName;
            auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
            auto* Package = CreatePackage(*PackageName);
            if (Source == nullptr || Package == nullptr)
            { return false; }
            Clip.Reset(DuplicateObject<UCk_ProceduralAction_Data>(Source, Package, FName{*InName}));
            if (NOT Clip.IsValid())
            { return false; }
            Clip->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
            auto Subject = FCk_ProceduralAction_EditorSubject{};
            Subject.Set_Scenario(TSoftObjectPtr<UObject>{FSoftObjectPath{BodyPreviewPath}});
            Clip->Set_EditorSubject(Subject);
            Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            return Editors.IsValid() && Editors->OpenEditorForAsset(Clip.Get(), EToolkitMode::Standalone);
        }

        auto Close() -> void
        {
            if (Editors.IsValid() && Clip.IsValid())
            { Editors->CloseAllEditorsForAsset(Clip.Get()); }
            Clip.Reset();
            if (PrivateTransactions.IsValid())
            { PrivateTransactions->Reset(FText::FromString(TEXT("Discard disposable pose workspace transactions"))); }
            if (GEditor != nullptr && PrivateTransactions.IsValid() && GEditor->Trans.Get() == PrivateTransactions.Get())
            { GEditor->Trans = PreviousTransactions.Get(); }
            PrivateTransactions.Reset();
            PreviousTransactions.Reset();
            if (auto* Package = PackageName.IsEmpty() ? nullptr : FindPackage(nullptr, *PackageName))
            {
                auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
                Unload.bResetTransBuffer = false;
                UPackageTools::UnloadPackages(Unload);
            }
            PackageName.Reset();
            CollectGarbage(RF_NoFlags);
            if (Mounted)
            {
                FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory);
                IFileManager::Get().DeleteDirectory(*MountDirectory, false, true);
                Mounted = false;
            }
        }

        ~FState() { Close(); }
    };

    auto
        MountToolkit(
            FState& InState)
        -> bool
    {
        auto* Editor = InState.Toolkit();
        if (Editor == nullptr)
        { return false; }
        const auto Tab = FindTab(*Editor, FCk_ProceduralAction_EditorToolkit::ViewportTabId);
        if (NOT Tab.IsValid() || NOT FSlateApplication::Get().FindWidgetWindow(Tab.ToSharedRef()).IsValid())
        { return false; }
        Editor->FocusWindow(InState.Clip.Get());
        if (const auto OwnerTab = Editor->GetAssociatedTabManager()->GetOwnerTab())
        { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
        Tab->ActivateInParent(ETabActivationCause::SetDirectly);
        return true;
    }

    // ----------------------------------------------------------------------------------------------------------------

    class FRunPanelsShowTheSelection final : public IAutomationLatentCommand
    {
    public:
        explicit FRunPanelsShowTheSelection(TSharedRef<FState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            { return State.Fail(TEXT("panel selection row timed out")); }
            if (State.Stage == 0)
            {
                State.LastWaitReason = TEXT("toolkit viewport tab not mounted");
                if (NOT MountToolkit(State))
                { return false; }
                ++State.Stage;
                return false;
            }
            auto* Editor = State.Toolkit();
            if (Editor == nullptr || NOT State.Clip.IsValid())
            { return State.Fail(TEXT("toolkit or clip disappeared")); }
            const auto Model = Editor->Get_Model();
            const auto Session = Editor->Get_Session();
            const auto* Proxy = Editor->Get_SelectionProxy();
            const auto& FirstPhase = State.Clip->Get_Phases()[0];
            const auto Body = FCk_ProceduralAction_PoseGroup::Get_BodyChannelName();
            if (State.Stage == 1)
            {
                for (const auto Tag : {FName{TEXT("CkActionEditor.Viewport")}, FName{TEXT("CkActionEditor.Status")},
                    FName{TEXT("CkActionEditor.Channels")}, FName{TEXT("CkActionEditor.Details")},
                    FName{TEXT("CkActionEditor.Timeline")}, FName{TEXT("CkActionEditor.Ruler")},
                    FName{TEXT("CkActionEditor.Transport")}, FName{TEXT("CkActionEditor.TransportTime")}})
                {
                    const auto Widget = FindInToolkit(*Editor, Tag);
                    State.Test->TestTrue(FString::Printf(TEXT("the clip editor shows %s by default"), *Tag.ToString()),
                        Widget.IsValid() && IsReachable(Widget.ToSharedRef()));
                }
                for (const auto Tag : {FName{TEXT("CkActionEditor.Curves")}, FName{TEXT("CkActionEditor.Issues")}})
                {
                    State.Test->TestTrue(FString::Printf(TEXT("the clip editor mounts %s"), *Tag.ToString()),
                        FindInToolkit(*Editor, Tag).IsValid());
                }
                State.Test->TestTrue(TEXT("BodyPulse poses are listed: the first section has a pose at its start"),
                    Session.IsValid() && Session->GetPoseGroups().ContainsByPredicate(
                        [&FirstPhase](const FCk_ProceduralActionPoseGroupView& InView)
                        {
                            return InView.Get_Group().Get_PhaseId() == FirstPhase.Get_Id()
                                && ck::procedural_action_editor::IsSamePoseTime(InView.Get_Group().Get_Time().Get_Value(), 0.0);
                        }));
                State.Test->TestTrue(TEXT("Record covers the body channel by default"), Model->Get_IsRecorded(Body));
                State.Test->TestTrue(TEXT("the body channel has a Record tick"), FindInToolkit(*Editor, BodyRecordTag).IsValid());
                Model->SelectClip();
                ++State.Stage;
                return false;
            }
            if (State.Stage == 2)
            {
                const auto ShowsClip = Proxy != nullptr && Proxy->_Shows == ECk_ProceduralActionSelectionProxy_Shows::Clip;
                if (NOT State.Settle(ShowsClip, TEXT("Details did not load the clip")))
                { return false; }
                State.Test->TestTrue(TEXT("the Details show the clip when the clip is selected"),
                    ShowsClip && FMath::IsNearlyEqual(Proxy->_Length, static_cast<float>(Session->GetDuration().Get_Seconds()), 1.0e-3f)
                    && Proxy->_Subject.Get_Scenario().ToString() == FString{BodyPreviewPath});
                Model->SelectSection(FirstPhase.Get_Id());
                ++State.Stage;
                return false;
            }
            if (State.Stage == 3)
            {
                const auto ShowsSection = Proxy != nullptr && Proxy->_Shows == ECk_ProceduralActionSelectionProxy_Shows::Section
                    && Proxy->_SectionName == FirstPhase.Get_Id();
                if (NOT State.Settle(ShowsSection, TEXT("Details did not load the section")))
                { return false; }
                State.Test->TestTrue(TEXT("the Details show the selected section and its duration"),
                    ShowsSection && FMath::IsNearlyEqual(Proxy->_Duration,
                        static_cast<float>(FirstPhase.Get_Duration().Get_Seconds()), 1.0e-4f));
                if (NOT State.Test->TestFalse(TEXT("BodyPulse's first section has a body key"), FirstPhase.Get_BodyKeys().IsEmpty()))
                { return State.Fail(TEXT("no body key to select")); }
                State.RequestedPhase = FirstPhase.Get_Id();
                State.RequestedTime = FirstPhase.Get_BodyKeys()[0].Get_Time();
                Model->SelectKey(State.RequestedPhase, Body, State.RequestedTime);
                ++State.Stage;
                return false;
            }
            if (State.Stage == 4)
            {
                const auto Expected = State.RequestedBody();
                const auto ShowsKey = State.ProxyShowsBodyKey() && Expected.IsSet() && Proxy->_Position.Equals(Expected.GetValue());
                if (NOT State.Settle(ShowsKey, TEXT("Details did not load the body key")))
                { return false; }
                State.Test->TestTrue(TEXT("the Details show the selected body key's position"), ShowsKey);
                if (State.Clip->Get_Markers().IsEmpty())
                {
                    State.Test->AddWarning(TEXT("BodyPulse has no marker; the event selection is not exercised."));
                    State.Stage = 6;
                    return false;
                }
                Model->SelectEvent(State.Clip->Get_Markers()[0].Get_Id());
                ++State.Stage;
                return false;
            }
            if (State.Stage == 5)
            {
                const auto MarkerId = State.Clip->Get_Markers()[0].Get_Id();
                const auto ShowsEvent = Proxy != nullptr && Proxy->_Shows == ECk_ProceduralActionSelectionProxy_Shows::Event
                    && Proxy->_EventName == MarkerId;
                if (NOT State.Settle(ShowsEvent, TEXT("Details did not load the marker")))
                { return false; }
                State.Test->TestTrue(TEXT("the Details show the selected marker"), ShowsEvent);
                ++State.Stage;
                return false;
            }
            if (State.Stage == 6)
            {
                if (Model->Get_SelectedChannel() == Body)
                {
                    Model->SelectChannel(NAME_None);
                    if (NOT State.Test->TestTrue(TEXT("the body channel selection clears before the row click"),
                        Model->Get_SelectedChannel() != Body))
                    { return State.Fail(TEXT("the body channel stays selected, so a row click would prove nothing")); }
                }
                if (NOT State.ClickTagged(BodyRowTag))
                { return false; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 7)
            {
                const auto Selected = Model->Get_SelectedChannel() == Body;
                if (NOT State.Settle(Selected, TEXT("the routed body row click did not select the body")))
                { return false; }
                State.Test->TestTrue(TEXT("a routed click on the body row selects the body channel"), Selected);
                if (NOT State.ClickTagged(BodyRecordTag))
                { return false; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 8)
            {
                const auto Cleared = NOT Model->Get_IsRecorded(Body);
                if (NOT State.Settle(Cleared, TEXT("the routed Record tick did not clear the body")))
                { return false; }
                State.Test->TestTrue(TEXT("a routed click on the body Record tick takes the body out of Record"), Cleared);
                if (NOT State.ClickTagged(BodyRecordTag))
                { return false; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 9)
            {
                const auto Restored = Model->Get_IsRecorded(Body);
                if (NOT State.Settle(Restored, TEXT("the second routed Record tick did not restore the body")))
                { return false; }
                State.Test->TestTrue(TEXT("a second routed click puts the body back in Record"), Restored);
                State.Close();
                return true;
            }
            return State.Fail(TEXT("unexpected stage"));
        }

    private:
        TSharedRef<FState> _State;
    };

    // ----------------------------------------------------------------------------------------------------------------

    class FRunCaptureAndPlay final : public IAutomationLatentCommand
    {
    public:
        explicit FRunCaptureAndPlay(TSharedRef<FState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            { return State.Fail(TEXT("pose workspace capture/play row timed out")); }
            if (State.Stage == 0)
            {
                State.LastWaitReason = TEXT("toolkit viewport tab not mounted");
                if (NOT MountToolkit(State))
                { return false; }
                State.Stage = 10;
                return false;
            }
            auto* Editor = State.Toolkit();
            if (Editor == nullptr || NOT State.Clip.IsValid())
            { return State.Fail(TEXT("toolkit or clip disappeared")); }
            const auto Model = Editor->Get_Model();
            const auto Session = Editor->Get_Session();
            const auto* Modes = Editor->Get_Modes();
            const auto FirstPhase = State.Clip->Get_Phases()[0].Get_Id();
            const auto Body = FCk_ProceduralAction_PoseGroup::Get_BodyChannelName();

            // Requested capture: put the playhead on the section-start pose, step two frames, record, then edit the body.
            if (State.Stage == 10)
            {
                Model->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Authored);
                const auto* StartPose = Session->GetPoseGroups().FindByPredicate(
                    [FirstPhase](const FCk_ProceduralActionPoseGroupView& InView)
                    {
                        return InView.Get_Group().Get_PhaseId() == FirstPhase
                            && ck::procedural_action_editor::IsSamePoseTime(InView.Get_Group().Get_Time().Get_Value(), 0.0);
                    });
                State.Test->AddInfo(FString::Printf(TEXT("Section-start pose present: %d"), StartPose != nullptr));
                if (StartPose != nullptr)
                { Model->SelectPose(StartPose->Get_Group().Get_Id(), FirstPhase); }
                if (NOT State.Test->TestTrue(TEXT("the first section has a start time"), Session->Get_PhaseStart(FirstPhase, State.PhaseStart)))
                { return State.Fail(TEXT("the first section has no start time")); }
                Model->SetTime(State.PhaseStart);
                ++State.Actions;
                State.Stage = 11;
                return false;
            }
            if (State.Stage == 11)
            {
                Editor->StepFrames(2);
                ++State.Actions;
                State.FrameSteps += 2;
                State.Stage = 12;
                return false;
            }
            if (State.Stage == 12)
            {
                State.Test->TestTrue(TEXT("two frame steps move the playhead two 60 Hz frames past the section start"),
                    FMath::IsNearlyEqual(Model->Get_Time().Get_Seconds(), State.PhaseStart.Get_Seconds() + 2.0 * FrameSeconds, 1.0e-3));
                State.Test->TestTrue(TEXT("Record covers the body"), Model->Get_IsRecorded(Body));
                State.GroupsBefore = State.Groups();
                const auto Accepted = Editor->RecordPose();
                ++State.Actions;
                State.Test->AddInfo(FString::Printf(TEXT("Authored RecordPose: %s"), *State.Message()));
                State.Test->TestTrue(TEXT("RecordPose records the authored pose at the playhead"), Accepted);
                State.Stage = 13;
                return false;
            }
            if (State.Stage == 13)
            {
                const auto* Captured = State.NewGroup();
                if (Captured == nullptr)
                { return State.Fail(TEXT("RecordPose wrote no pose group")); }
                State.Test->TestEqual(TEXT("the recorded pose lands in the selected section"), Captured->Get_PhaseId(), FirstPhase);
                State.Test->TestTrue(TEXT("the recorded pose lands two frames after the section start"),
                    Captured->Get_Time().Get_Value() > 0.0 && Captured->Get_Time().Get_Value() < 1.0);
                State.Test->TestEqual(TEXT("the authored recording is requested"), Captured->Get_Provenance(),
                    ECk_ProceduralAction_PoseProvenance::Requested);
                State.Test->TestTrue(TEXT("the authored recording covers the body"), Captured->Get_Channels().Contains(Body));
                State.RequestedPose = Captured->Get_Id();
                State.RequestedPhase = Captured->Get_PhaseId();
                State.RequestedTime = Captured->Get_Time();
                Model->SelectKey(State.RequestedPhase, Body, State.RequestedTime);
                ++State.Actions;
                State.Stage = 14;
                return false;
            }
            if (State.Stage == 14)
            {
                const auto Before = State.RequestedBody();
                const auto Loaded = State.ProxyShowsBodyKey() && Before.IsSet()
                    && Editor->Get_SelectionProxy()->_Position.Equals(Before.GetValue());
                if (NOT State.Settle(Loaded, TEXT("Details did not load the recorded body key")))
                { return false; }
                if (NOT State.Test->TestTrue(TEXT("the Details show the recorded body key"), Loaded))
                { return State.Fail(TEXT("the recorded body key is not editable")); }
                State.BodyBefore = Before.GetValue();
                EditKeyPosition(*Editor->Get_SelectionProxy(), State.BodyBefore + BodyEdit);
                ++State.Actions;
                State.Stage = 15;
                return false;
            }
            if (State.Stage == 15)
            {
                const auto After = State.RequestedBody();
                State.Test->TestTrue(TEXT("a Details edit moves the recorded body key"),
                    After.IsSet() && After.GetValue().Equals(State.BodyBefore + BodyEdit, 1.0e-3));
                State.Stage = 20;
                return false;
            }

            // Solved playback simulates to terminal from one Play and keeps every frame; the toolkit records it without per-frame input.
            if (State.Stage == 20)
            {
                Model->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Solved);
                Editor->TogglePlay();
                ++State.Actions;
                State.DistinctSteps = 0;
                State.LastObservedStep = INDEX_NONE;
                State.Stage = 21;
                return false;
            }
            if (State.Stage == 21)
            {
                const auto Status = State.Status();
                const auto Frames = Modes != nullptr ? Modes->GetFrameCount() : 0;
                if (Frames != State.LastObservedStep)
                {
                    State.LastObservedStep = Frames;
                    ++State.DistinctSteps;
                }
                State.LastWaitReason = FString::Printf(TEXT("waiting for the solved preview to complete: %s"), *Status);
                if (Modes == nullptr || NOT Modes->GetIsComplete() || Model->Get_IsPlaying())
                { return false; }
                State.Test->AddInfo(FString::Printf(TEXT("Solved preview complete: %d frames over %d observed editor frames; %s"),
                    Frames, State.DistinctSteps, *Status));
                State.Test->TestTrue(TEXT("solved playback advanced over several editor frames from one Play"),
                    State.DistinctSteps >= 3);
                State.Test->TestTrue(TEXT("the solved recording is continuous"), Status.Contains(TEXT(" 0 gaps")));
                State.Test->TestTrue(TEXT("playback stops on the last solved frame"),
                    FMath::IsNearlyEqual(Model->Get_Time().Get_Seconds(), Modes->GetSolvedTime().Get_Seconds(), 1.0e-6));
                State.GroupsBefore = State.Groups();
                const auto Accepted = Editor->RecordPose();
                ++State.Actions;
                State.Test->AddInfo(FString::Printf(TEXT("RecordPose at terminal: %s"), *State.Message()));
                State.Test->TestFalse(TEXT("RecordPose refuses a finished preview"), Accepted);
                State.Test->TestNull(TEXT("a finished preview is not a ready solved snapshot"), State.NewGroup());
                State.SolvedFrames = Frames;
                State.Stage = 30;
                return false;
            }

            // Solved capture: scrub back into the cached frames and record; nothing re-simulates.
            if (State.Stage == 30)
            {
                Model->SetTime(FCk_Time{(4 + 3 * State.SolvedAttempts) / 60.0});
                ++State.Actions;
                State.Stage = 31;
                return false;
            }
            if (State.Stage == 31)
            {
                State.Test->TestEqual(TEXT("scrubbing back keeps the solved frames"), Modes != nullptr ? Modes->GetFrameCount() : 0,
                    State.SolvedFrames);
                State.GroupsBefore = State.Groups();
                Editor->RecordPose();
                ++State.Actions;
                ++State.SolvedAttempts;
                State.Stage = 33;
                return false;
            }
            if (State.Stage == 33)
            {
                const auto* Solved = State.NewGroup();
                if (Solved == nullptr)
                {
                    State.Test->AddInfo(FString::Printf(TEXT("Solved capture attempt %d refused: %s"), State.SolvedAttempts,
                        *State.Message()));
                    if (State.SolvedAttempts >= 4)
                    { return State.Fail(TEXT("a settled solved frame was never accepted")); }
                    State.Stage = 30;
                    return false;
                }
                State.Test->AddInfo(FString::Printf(TEXT("Solved capture accepted on attempt %d: %s"), State.SolvedAttempts,
                    *State.Message()));
                State.Test->TestEqual(TEXT("the solved capture records its provenance"), Solved->Get_Provenance(),
                    ECk_ProceduralAction_PoseProvenance::Solved);
                State.Stage = 40;
                return false;
            }

            // An edit drops the solved frames and re-simulates the new revision; capture works again once the playhead is solved.
            if (State.Stage == 40)
            {
                Model->SelectKey(State.RequestedPhase, Body, State.RequestedTime);
                ++State.Actions;
                State.Stage = 41;
                return false;
            }
            if (State.Stage == 41)
            {
                const auto Before = State.RequestedBody();
                const auto Loaded = State.ProxyShowsBodyKey() && Before.IsSet()
                    && Editor->Get_SelectionProxy()->_Position.Equals(Before.GetValue());
                if (NOT State.Settle(Loaded, TEXT("Details did not reload the requested body key")))
                { return false; }
                if (NOT State.Test->TestTrue(TEXT("the Details reload the requested body key"), Loaded))
                { return State.Fail(TEXT("the requested body key is not editable")); }
                State.BodyBefore = Before.GetValue();
                EditKeyPosition(*Editor->Get_SelectionProxy(), State.BodyBefore + BodyEdit);
                ++State.Actions;
                State.Stage = 42;
                return false;
            }
            if (State.Stage == 42)
            {
                const auto After = State.RequestedBody();
                State.Test->TestTrue(TEXT("the Details edit changed the recorded body key"),
                    After.IsSet() && NOT After.GetValue().Equals(State.BodyBefore));
                State.Test->TestTrue(TEXT("the edit drops the previous revision's solved frames"),
                    Modes != nullptr && Modes->GetFrameCount() < State.SolvedFrames);
                State.SolvedAttempts = 0;
                State.Stage = 50;
                return false;
            }
            if (State.Stage == 50)
            {
                State.LastWaitReason = FString::Printf(TEXT("waiting for the new revision to solve the playhead: %s"), *State.Status());
                if (Modes == nullptr || Modes->GetDisplayedFrame() == nullptr
                    || Modes->GetTime().Get_Seconds() + 1.0e-6 < Model->Get_Time().Get_Seconds())
                { return false; }
                State.GroupsBefore = State.Groups();
                Editor->RecordPose();
                ++State.Actions;
                ++State.SolvedAttempts;
                State.Stage = 53;
                return false;
            }
            if (State.Stage == 53)
            {
                if (State.NewGroup() == nullptr)
                {
                    State.Test->AddInfo(FString::Printf(TEXT("Solved capture after the edit, attempt %d refused: %s"),
                        State.SolvedAttempts, *State.Message()));
                    if (State.SolvedAttempts >= 4)
                    { return State.Fail(TEXT("re-simulating the edited clip did not make solved capture available again")); }
                    Model->SetTime(FCk_Time{(6 + 3 * State.SolvedAttempts) / 60.0});
                    State.Stage = 50;
                    return false;
                }
                State.Test->TestTrue(TEXT("re-simulating the edited clip restores solved capture"), true);
                State.Test->AddInfo(FString::Printf(
                    TEXT("Scripted body exercise: %d author actions (%d transport frame steps) for requested capture, edit, solved play, solved capture, edit and re-simulation."),
                    State.Actions, State.FrameSteps));
                State.Close();
                return true;
            }
            return State.Fail(TEXT("unexpected stage"));
        }

    private:
        TSharedRef<FState> _State;
    };
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseWorkspace_PanelsShowTheSelection,
    "Ck.ProceduralAnimation.Action.Editor.MountedToolkit.PoseWorkspace.PanelsShowTheSelection",
    ck_test_procedural_action_pose_workspace::TestFlags)

auto
    FCkTest_ProceduralAction_PoseWorkspace_PanelsShowTheSelection::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_workspace;
    if (NOT FApp::CanEverRender() || NOT FSlateApplication::IsInitialized() || GEditor == nullptr)
    { AddError(TEXT("The pose workspace rows need a real RHI and an initialized Slate editor.")); return false; }
    auto State = MakeShared<FState>();
    State->Test = this;
    State->FailureName = TEXT("PanelsShowTheSelection-Failure");
    if (NOT TestTrue(TEXT("a disposable BodyPulse copy opens in the action toolkit"), State->Open(TEXT("PulsePanels")))
        || NOT TestFalse(TEXT("BodyPulse has sections to select"), State->Clip->Get_Phases().IsEmpty()))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunPanelsShowTheSelection(State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseWorkspace_CaptureAndPlay,
    "Ck.ProceduralAnimation.Action.Editor.MountedToolkit.PoseWorkspace.CaptureAndPlay",
    ck_test_procedural_action_pose_workspace::TestFlags)

auto
    FCkTest_ProceduralAction_PoseWorkspace_CaptureAndPlay::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_workspace;
    if (NOT FApp::CanEverRender() || NOT FSlateApplication::IsInitialized() || GEditor == nullptr)
    { AddError(TEXT("The pose workspace rows need a real RHI and an initialized Slate editor.")); return false; }
    auto State = MakeShared<FState>();
    State->Test = this;
    State->FailureName = TEXT("CaptureAndPlay-Failure");
    if (NOT TestTrue(TEXT("a disposable BodyPulse copy opens in the action toolkit"), State->Open(TEXT("PulseWorkspace")))
        || NOT TestFalse(TEXT("BodyPulse has phases to pose"), State->Clip->Get_Phases().IsEmpty()))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 60.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunCaptureAndPlay(State));
    return true;
}

#endif

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditor_UserSettings.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Framework/Application/SlateApplication.h"
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
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_test_procedural_action_pose_workspace
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    const FName EditorTabId{TEXT("CkProceduralAction_Editor")};
    const TCHAR* BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* BodyPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    const FName AdvancedTag{TEXT("CkActionEditorAdvanced")};
    const FName PlayTag{TEXT("CkActionEditorPlay")};
    const FName FrameTag{TEXT("CkActionEditorFrame")};
    const FName PoseChipTag{TEXT("CkActionEditorPose")};
    const FName CapturePoseTag{TEXT("CkActionEditorCapturePose")};
    const FName UpdatePoseTag{TEXT("CkActionEditorUpdatePose")};
    const FName CaptureSolvedTag{TEXT("CkActionEditorCaptureSolved")};
    const FName PoseMessageTag{TEXT("CkActionEditorPoseMessage")};
    const FName StatusTag{TEXT("CkActionEditorStatus")};
    const FName BodyNudgeTag{TEXT("CkActionEditorNudge_Body_XPlus")};

    auto
        CollectText(
            const TSharedRef<SWidget>& InWidget,
            FString& OutText,
            int32 InDepth = 0)
        -> void
    {
        if (InDepth > 64)
        { return; }
        if (InWidget->GetType() == FName{TEXT("STextBlock")})
        { OutText += StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString() + TEXT("\n"); }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        { CollectText(Children->GetChildAt(Index), OutText, InDepth + 1); }
    }

    auto
        TextOf(
            const TSharedPtr<SWidget>& InWidget)
        -> FString
    {
        auto Text = FString{};
        if (InWidget.IsValid())
        { CollectText(InWidget.ToSharedRef(), Text); }
        return Text;
    }

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
        FindAllTagged(
            const TSharedRef<SWidget>& InWidget,
            FName InTag,
            TArray<TSharedRef<SWidget>>& OutWidgets,
            int32 InDepth = 0)
        -> void
    {
        if (InDepth > 64)
        { return; }
        if (InWidget->GetTag() == InTag)
        { OutWidgets.Add(InWidget); }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        { FindAllTagged(Children->GetChildAt(Index), InTag, OutWidgets, InDepth + 1); }
    }

    auto
        FindAllOfType(
            const TSharedRef<SWidget>& InWidget,
            FName InType,
            TArray<TSharedRef<SWidget>>& OutWidgets,
            int32 InDepth = 0)
        -> void
    {
        if (InDepth > 64)
        { return; }
        if (InWidget->GetType() == InType)
        { OutWidgets.Add(InWidget); }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        { FindAllOfType(Children->GetChildAt(Index), InType, OutWidgets, InDepth + 1); }
    }

    // The chip whose label line is exactly InLabel (a chip may add a provenance suffix after it).
    auto
        FindPoseChip(
            const TSharedRef<SWidget>& InRoot,
            const FString& InLabel)
        -> TSharedPtr<SWidget>
    {
        auto Chips = TArray<TSharedRef<SWidget>>{};
        FindAllTagged(InRoot, PoseChipTag, Chips);
        for (const auto& Chip : Chips)
        {
            auto Lines = TArray<FString>{};
            TextOf(Chip).ParseIntoArrayLines(Lines);
            if (NOT Lines.IsEmpty() && (Lines[0] == InLabel || Lines[0].StartsWith(InLabel + TEXT(" ("))))
            { return Chip; }
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

    auto
        ParseSolverStep(
            const FString& InStatus)
        -> int64
    {
        constexpr auto Marker = TEXT("GamePreview step ");
        const auto Index = InStatus.Find(Marker);
        if (Index == INDEX_NONE)
        { return INDEX_NONE; }
        auto Digits = FString{};
        for (auto Cursor = Index + FCString::Strlen(Marker); Cursor < InStatus.Len() && FChar::IsDigit(InStatus[Cursor]); ++Cursor)
        { Digits.AppendChar(InStatus[Cursor]); }
        return Digits.IsEmpty() ? INDEX_NONE : FCString::Atoi64(*Digits);
    }

    // A disposable copy of BodyPulse in its own content root, so the shipped example is never edited.
    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TSharedPtr<SDockTab> Tab;
        TStrongObjectPtr<UTransactor> PreviousTransactions;
        TStrongObjectPtr<UTransBuffer> PrivateTransactions;
        FString MountRoot;
        FString MountDirectory;
        FString PackageName;
        FString FailureName;
        FString LastWaitReason;
        double DeadlineSeconds = 0.0;
        double StageStartedSeconds = 0.0;
        int32 Stage = 0;
        int32 Clicks = 0;
        int32 FrameClicks = 0;
        int32 SolvedAttempts = 0;
        int64 FirstObservedStep = INDEX_NONE;
        int32 DistinctSteps = 0;
        int64 LastObservedStep = INDEX_NONE;
        TSet<FGuid> GroupsBefore;
        FGuid RequestedPose;
        FString RequestedLabel;
        bool Mounted = false;

        auto Content() const -> TSharedPtr<SWidget>
        { return Tab.IsValid() ? TSharedPtr<SWidget>{Tab->GetContent()} : TSharedPtr<SWidget>{}; }

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

        auto RequestedBody() const -> TOptional<FVector>
        {
            const auto* Group = Clip.IsValid() ? Clip->Get_EditorPoseGroups().FindByPredicate(
                [this](const FCk_ProceduralAction_PoseGroup& InGroup) { return InGroup.Get_Id() == RequestedPose; }) : nullptr;
            const auto* Phase = Group == nullptr ? nullptr : Clip->Get_Phases().FindByPredicate(
                [Group](const FCk_ProceduralAction_Phase& InPhase) { return InPhase.Get_Id() == Group->Get_PhaseId(); });
            const auto* Key = Phase == nullptr ? nullptr : Phase->Get_BodyKeys().FindByPredicate(
                [Group](const FCk_ProceduralAction_TransformKey& InKey) { return InKey.Get_Time().Get_Value() == Group->Get_Time().Get_Value(); });
            return Key == nullptr ? TOptional<FVector>{} : TOptional<FVector>{Key->Get_Translation()};
        }

        auto Status() const -> FString
        {
            const auto Root = Content();
            return Root.IsValid() ? TextOf(FindTagged(Root.ToSharedRef(), StatusTag)) : FString{};
        }

        auto PoseMessage() const -> FString
        {
            const auto Root = Content();
            return Root.IsValid() ? TextOf(FindTagged(Root.ToSharedRef(), PoseMessageTag)) : FString{};
        }

        // Clicks a tagged control through Slate's routed input and counts it as one author action.
        auto ClickTagged(FName InTag) -> bool
        {
            const auto Root = Content();
            const auto Target = Root.IsValid() ? FindTagged(Root.ToSharedRef(), InTag) : TSharedPtr<SWidget>{};
            auto Hit = FHitDiagnostic{};
            if (NOT Target.IsValid() || NOT ClickWidget(Target.ToSharedRef(), &Hit))
            {
                LastWaitReason = FString::Printf(TEXT("routed click on %s failed: found=%d enabled=%d point=%s hitTarget=%d hitPath=%s"),
                    *InTag.ToString(), Target.IsValid(), Target.IsValid() && Target->IsEnabled(), *Hit.Point, Hit.HitTarget,
                    *Hit.HitPath);
                return false;
            }
            ++Clicks;
            return true;
        }

        auto ClickChip(const FString& InLabel) -> bool
        {
            const auto Root = Content();
            const auto Chip = Root.IsValid() ? FindPoseChip(Root.ToSharedRef(), InLabel) : TSharedPtr<SWidget>{};
            auto Hit = FHitDiagnostic{};
            if (NOT Chip.IsValid() || NOT ClickWidget(Chip.ToSharedRef(), &Hit))
            {
                LastWaitReason = FString::Printf(TEXT("routed click on pose chip '%s' failed: found=%d point=%s hitTarget=%d hitPath=%s"),
                    *InLabel, Chip.IsValid(), *Hit.Point, Hit.HitTarget, *Hit.HitPath);
                return false;
            }
            ++Clicks;
            return true;
        }

        auto Fail(const FString& InMessage) -> bool
        {
            const auto Root = Content();
            const auto Capture = Root.IsValid() ? SaveCapture(Root.ToSharedRef(), FailureName) : FString{};
            Test->AddError(FString::Printf(TEXT("%s (stage %d; last wait: %s; failure screenshot: %s)"),
                *InMessage, Stage, *LastWaitReason, Capture.IsEmpty() ? TEXT("unavailable") : *Capture));
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
            auto* Settings = GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>();
            if (Settings->SetPreviewPath(Clip->GetPathName(), BodyPreviewPath))
            { Settings->SaveConfig(); }
            Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            return Editors.IsValid() && Editors->OpenEditorForAsset(Clip.Get(), EToolkitMode::Standalone);
        }

        auto Close() -> void
        {
            Tab.Reset();
            if (Editors.IsValid() && Clip.IsValid())
            { Editors->CloseAllEditorsForAsset(Clip.Get()); }
            if (Clip.IsValid())
            {
                auto* Settings = GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>();
                if (Settings->SetPreviewPath(Clip->GetPathName(), FString{}))
                { Settings->SaveConfig(); }
            }
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
        FindTab(
            FState& InState)
        -> bool
    {
        auto* Editor = InState.Editors.IsValid() && InState.Clip.IsValid()
            ? InState.Editors->FindEditorForAsset(InState.Clip.Get(), false) : nullptr;
        if (Editor == nullptr || Editor->GetAssociatedTabManager() == nullptr)
        { return false; }
        InState.Tab = Editor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId});
        if (NOT InState.Tab.IsValid() || NOT FSlateApplication::Get().FindWidgetWindow(InState.Tab.ToSharedRef()).IsValid())
        { return false; }
        Editor->FocusWindow(InState.Clip.Get());
        if (const auto OwnerTab = Editor->GetAssociatedTabManager()->GetOwnerTab())
        { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
        InState.Tab->ActivateInParent(ETabActivationCause::SetDirectly);
        return true;
    }

    // ----------------------------------------------------------------------------------------------------------------

    class FRunAdvancedHidesRawDetails final : public IAutomationLatentCommand
    {
    public:
        explicit FRunAdvancedHidesRawDetails(TSharedRef<FState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            { return _State->Fail(TEXT("Advanced visibility row timed out")); }
            if (_State->Stage == 0)
            {
                _State->LastWaitReason = TEXT("toolkit tab not mounted");
                if (NOT FindTab(*_State))
                { return false; }
                ++_State->Stage;
                return false;
            }
            const auto Content = _State->Content();
            if (NOT Content.IsValid())
            { return _State->Fail(TEXT("toolkit tab closed")); }
            auto Details = TArray<TSharedRef<SWidget>>{};
            FindAllOfType(Content.ToSharedRef(), FName{TEXT("SDetailsView")}, Details);
            if (_State->Stage == 1)
            {
                _State->Test->TestEqual(TEXT("the toolkit still owns the clip and preview Details"), Details.Num(), 2);
                for (const auto& View : Details)
                { _State->Test->TestFalse(TEXT("raw Details are not reachable until Advanced opens"), IsReachable(View)); }
                const auto Recorded = FindTagged(Content.ToSharedRef(), FName{TEXT("CkActionEditorRecordedMode")});
                _State->Test->TestTrue(TEXT("recorded diagnostics are hidden in Advanced"),
                    Recorded.IsValid() && NOT IsReachable(Recorded.ToSharedRef()));
                for (const auto Tag : {PlayTag, CapturePoseTag, CaptureSolvedTag, FName{TEXT("CkActionEditorProgressPlot")},
                    FName{TEXT("CkActionEditorSubjectTree")}, FName{TEXT("CkActionEditorInspector")}})
                {
                    const auto Widget = FindTagged(Content.ToSharedRef(), Tag);
                    _State->Test->TestTrue(FString::Printf(TEXT("the pose workspace shows %s by default"), *Tag.ToString()),
                        Widget.IsValid() && IsReachable(Widget.ToSharedRef()));
                }
                _State->Test->TestNotNull(TEXT("BodyPulse poses are listed as timeline chips"),
                    FindPoseChip(Content.ToSharedRef(), _State->Clip->Get_Phases()[0].Get_Id().ToString() + TEXT(" 0")).Get());
                const auto Advanced = FindTagged(Content.ToSharedRef(), AdvancedTag);
                const auto Header = Advanced.IsValid() ? FindTagged(Advanced.ToSharedRef(), FName{TEXT("CkActionEditorAdvancedHeader")})
                    : TSharedPtr<SWidget>{};
                if (NOT Header.IsValid())
                { return _State->Fail(TEXT("Advanced header is missing")); }
                if (NOT _State->ClickTagged(FName{TEXT("CkActionEditorAdvancedHeader")}))
                { return false; }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 2)
            {
                _State->LastWaitReason = TEXT("Advanced did not reveal the raw Details");
                if (Details.Num() != 2 || NOT IsReachable(Details[0]) || NOT IsReachable(Details[1]))
                { return false; }
                _State->Test->TestTrue(TEXT("Advanced reveals the raw clip Details"), TextOf(Details[0]).Contains(TEXT("Body Channel")));
                const auto Recorded = FindTagged(Content.ToSharedRef(), FName{TEXT("CkActionEditorRecordedMode")});
                _State->Test->TestTrue(TEXT("Advanced reveals the recorded diagnostics"),
                    Recorded.IsValid() && IsReachable(Recorded.ToSharedRef()));
                if (NOT _State->ClickTagged(FName{TEXT("CkActionEditorAdvancedHeader")}))
                { return false; }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 3)
            {
                _State->LastWaitReason = TEXT("Advanced did not collapse");
                if (Details.Num() != 2 || IsReachable(Details[0]) || IsReachable(Details[1]))
                { return false; }
                _State->Test->TestTrue(TEXT("collapsing Advanced hides the raw Details again"), true);
                _State->Close();
                return true;
            }
            return _State->Fail(TEXT("unexpected stage"));
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
                State.LastWaitReason = TEXT("toolkit tab not mounted");
                if (NOT FindTab(State))
                { return false; }
                State.Stage = 10;
                return false;
            }
            if (NOT State.Content().IsValid() || NOT State.Clip.IsValid())
            { return State.Fail(TEXT("toolkit tab or clip disappeared")); }
            const auto FirstPhase = State.Clip->Get_Phases()[0].Get_Id();

            // Requested capture: select the phase-start pose, step the transport two frames, nudge the body, capture.
            if (State.Stage == 10)
            {
                if (NOT State.ClickChip(FirstPhase.ToString() + TEXT(" 0")))
                { return false; }
                State.Stage = 11;
                return false;
            }
            if (State.Stage == 11 || State.Stage == 12)
            {
                if (NOT State.ClickTagged(FrameTag))
                { return false; }
                ++State.FrameClicks;
                ++State.Stage;
                return false;
            }
            if (State.Stage == 13)
            {
                if (NOT State.ClickTagged(BodyNudgeTag))
                { return false; }
                State.GroupsBefore = State.Groups();
                State.Stage = 14;
                return false;
            }
            if (State.Stage == 14)
            {
                if (NOT State.ClickTagged(CapturePoseTag))
                { return false; }
                State.Stage = 15;
                return false;
            }
            if (State.Stage == 15)
            {
                const auto* Captured = State.NewGroup();
                if (Captured == nullptr)
                { return State.Fail(FString::Printf(TEXT("Capture pose wrote no pose group: %s"), *State.PoseMessage())); }
                State.Test->TestEqual(TEXT("the inspector capture lands in the selected phase"), Captured->Get_PhaseId(), FirstPhase);
                State.Test->TestTrue(TEXT("the inspector capture lands two frames after the phase start"),
                    Captured->Get_Time().Get_Value() > 0.0 && Captured->Get_Time().Get_Value() < 1.0);
                State.Test->TestEqual(TEXT("the inspector capture is requested"), Captured->Get_Provenance(),
                    ECk_ProceduralAction_PoseProvenance::Requested);
                State.Test->TestTrue(TEXT("the inspector capture covers the body"),
                    Captured->Get_Channels().Contains(FCk_ProceduralAction_PoseGroup::Get_BodyChannelName()));
                State.RequestedPose = Captured->Get_Id();
                State.RequestedLabel = Captured->Get_Label().ToString();
                State.Stage = 20;
                return false;
            }

            // Solved playback runs to terminal from one Play click; the toolkit records it without per-frame input.
            if (State.Stage == 20)
            {
                if (NOT State.ClickTagged(PlayTag))
                { return false; }
                State.FirstObservedStep = INDEX_NONE;
                State.LastObservedStep = INDEX_NONE;
                State.DistinctSteps = 0;
                State.Stage = 21;
                return false;
            }
            if (State.Stage == 21)
            {
                const auto Status = State.Status();
                const auto Step = ParseSolverStep(Status);
                if (Step != INDEX_NONE && Step != State.LastObservedStep)
                {
                    if (State.FirstObservedStep == INDEX_NONE)
                    { State.FirstObservedStep = Step; }
                    State.LastObservedStep = Step;
                    ++State.DistinctSteps;
                }
                State.LastWaitReason = FString::Printf(TEXT("waiting for the solved preview to reach terminal: %s"), *Status);
                if (NOT Status.Contains(TEXT("action terminal")))
                { return false; }
                State.Test->AddInfo(FString::Printf(TEXT("Solved preview reached terminal: steps %lld..%lld over %d observed editor frames; %s"),
                    State.FirstObservedStep, State.LastObservedStep, State.DistinctSteps, *Status));
                State.Test->TestTrue(TEXT("solved playback advanced over several editor frames from one Play click"),
                    State.DistinctSteps >= 3);
                State.Test->TestTrue(TEXT("the solved recording is continuous"), Status.Contains(TEXT(" 0 gaps")));
                State.GroupsBefore = State.Groups();
                if (NOT State.ClickTagged(CaptureSolvedTag))
                { return false; }
                State.Stage = 22;
                return false;
            }
            if (State.Stage == 22)
            {
                State.Test->AddInfo(FString::Printf(TEXT("Capture solved pose at terminal: %s"), *State.PoseMessage()));
                State.Test->TestNull(TEXT("a finished preview is not a ready solved snapshot"), State.NewGroup());
                State.Stage = 30;
                return false;
            }

            // Solved capture: re-run, pause on a pose and capture it.
            if (State.Stage == 30)
            {
                if (NOT State.ClickTagged(PlayTag))
                { return false; }
                State.StageStartedSeconds = FPlatformTime::Seconds();
                State.Stage = 31;
                return false;
            }
            if (State.Stage == 31)
            {
                const auto Status = State.Status();
                const auto Step = ParseSolverStep(Status);
                State.LastWaitReason = FString::Printf(TEXT("waiting for the re-run to play a few steps: %s"), *Status);
                if (Status.Contains(TEXT("action terminal")))
                { return State.Fail(TEXT("the re-run reached terminal before it could be paused")); }
                if (Step < 4 + 3 * State.SolvedAttempts)
                { return false; }
                if (NOT State.ClickTagged(PlayTag))
                { return false; }
                State.Stage = 32;
                return false;
            }
            if (State.Stage == 32)
            {
                State.LastWaitReason = TEXT("waiting for the paused transport");
                if (NOT TextOf(FindTagged(State.Content().ToSharedRef(), PlayTag)).Contains(TEXT("Play")))
                { return false; }
                State.GroupsBefore = State.Groups();
                if (NOT State.ClickTagged(CaptureSolvedTag))
                { return false; }
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
                        *State.PoseMessage()));
                    if (State.SolvedAttempts >= 4)
                    { return State.Fail(TEXT("a paused, settled solved pose was never accepted")); }
                    State.Stage = 30;
                    return false;
                }
                State.Test->AddInfo(FString::Printf(TEXT("Solved capture accepted on attempt %d: %s"), State.SolvedAttempts,
                    *State.PoseMessage()));
                State.Test->TestEqual(TEXT("the solved capture records its provenance"), Solved->Get_Provenance(),
                    ECk_ProceduralAction_PoseProvenance::Solved);
                State.Stage = 40;
                return false;
            }

            // Stale evidence: an edit after the run makes the next solved capture refuse until the preview re-runs.
            if (State.Stage == 40)
            {
                if (NOT State.ClickChip(State.RequestedLabel))
                { return false; }
                State.Stage = 41;
                return false;
            }
            if (State.Stage == 41)
            {
                if (NOT State.ClickTagged(BodyNudgeTag))
                { return false; }
                State.Stage = 42;
                return false;
            }
            if (State.Stage == 42)
            {
                const auto BodyBefore = State.RequestedBody();
                if (NOT State.ClickTagged(UpdatePoseTag))
                { return false; }
                State.Test->TestTrue(TEXT("Update pose edited the captured body key"),
                    BodyBefore.IsSet() && State.RequestedBody().IsSet()
                    && NOT BodyBefore.GetValue().Equals(State.RequestedBody().GetValue()));
                State.GroupsBefore = State.Groups();
                if (NOT State.ClickTagged(CaptureSolvedTag))
                { return false; }
                State.Stage = 43;
                return false;
            }
            if (State.Stage == 43)
            {
                const auto Message = State.PoseMessage();
                State.Test->AddInfo(FString::Printf(TEXT("Solved capture after an edit: %s"), *Message));
                State.Test->TestNull(TEXT("solved evidence from the previous revision is refused"), State.NewGroup());
                State.Test->TestTrue(TEXT("the refusal names the revision mismatch"), Message.Contains(TEXT("revision")));
                State.SolvedAttempts = 0;
                State.Stage = 50;
                return false;
            }
            if (State.Stage == 50)
            {
                if (NOT State.ClickTagged(PlayTag))
                { return false; }
                State.Stage = 51;
                return false;
            }
            if (State.Stage == 51)
            {
                const auto Status = State.Status();
                const auto Step = ParseSolverStep(Status);
                State.LastWaitReason = FString::Printf(TEXT("waiting for the second re-run to play a few steps: %s"), *Status);
                if (Status.Contains(TEXT("action terminal")))
                { return State.Fail(TEXT("the second re-run reached terminal before it could be paused")); }
                if (Step < 6 + 3 * State.SolvedAttempts)
                { return false; }
                if (NOT State.ClickTagged(PlayTag))
                { return false; }
                State.Stage = 52;
                return false;
            }
            if (State.Stage == 52)
            {
                State.LastWaitReason = TEXT("waiting for the paused transport after the second re-run");
                if (NOT TextOf(FindTagged(State.Content().ToSharedRef(), PlayTag)).Contains(TEXT("Play")))
                { return false; }
                State.GroupsBefore = State.Groups();
                if (NOT State.ClickTagged(CaptureSolvedTag))
                { return false; }
                ++State.SolvedAttempts;
                State.Stage = 53;
                return false;
            }
            if (State.Stage == 53)
            {
                if (State.NewGroup() == nullptr)
                {
                    State.Test->AddInfo(FString::Printf(TEXT("Solved capture after re-run, attempt %d refused: %s"),
                        State.SolvedAttempts, *State.PoseMessage()));
                    if (State.SolvedAttempts >= 4)
                    { return State.Fail(TEXT("re-running the preview did not make solved capture available again")); }
                    State.Stage = 50;
                    return false;
                }
                State.Test->TestTrue(TEXT("re-running the preview restores solved capture"), true);
                State.Test->AddInfo(FString::Printf(
                    TEXT("Scripted body exercise: %d routed author actions (%d transport frame steps) for requested capture, solved play, solved capture, edit, refusal and re-run."),
                    State.Clicks, State.FrameClicks));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_PoseWorkspace_AdvancedHidesRawDetails,
    "Ck.ProceduralAnimation.Action.Editor.MountedToolkit.PoseWorkspace.AdvancedHidesRawDetails",
    ck_test_procedural_action_pose_workspace::TestFlags)

auto
    FCkTest_ProceduralAction_PoseWorkspace_AdvancedHidesRawDetails::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_pose_workspace;
    if (NOT FApp::CanEverRender() || NOT FSlateApplication::IsInitialized() || GEditor == nullptr)
    { AddError(TEXT("The pose workspace rows need a real RHI and an initialized Slate editor.")); return false; }
    auto State = MakeShared<FState>();
    State->Test = this;
    State->FailureName = TEXT("AdvancedHidesRawDetails-Failure");
    if (NOT TestTrue(TEXT("a disposable BodyPulse copy opens in the action toolkit"), State->Open(TEXT("PulseAdvanced"))))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunAdvancedHidesRawDetails(State));
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

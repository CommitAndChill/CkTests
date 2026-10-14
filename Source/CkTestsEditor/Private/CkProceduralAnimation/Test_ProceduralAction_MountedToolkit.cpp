#include "Misc/AutomationTest.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModel.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModes.h"
#include "CkProceduralAnimationEditor/CkProceduralActionPreview_Config.h"
#include "CkProceduralAnimationEditor/CkProceduralActionSelectionProxy.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_EditorToolkit.h"

#include "Editor.h"
#include "Editor/Transactor.h"
#include "EditorViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Layout/WidgetPath.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"
#include "SEditorViewport.h"
#include "UnrealWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_test_procedural_action_mounted_toolkit
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    constexpr auto MaxSettleFrames = 60;
    const TCHAR* BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* LimbNodPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    const TCHAR* BodyPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    const TCHAR* LimbPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_Preview_DA.Ck_ProceduralAction_LimbNod_Preview_DA");
    const FName ViewportTag{TEXT("CkActionEditor.Viewport")};
    const FName StatusTag{TEXT("CkActionEditor.Status")};
    const FName DetailsTag{TEXT("CkActionEditor.Details")};
    const FName TimelineTag{TEXT("CkActionEditor.Timeline")};
    const FName TransportTag{TEXT("CkActionEditor.Transport")};
    const FName TransportTimeTag{TEXT("CkActionEditor.TransportTime")};

    auto
        FindTaggedWidget(
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
            if (const auto Tagged = FindTaggedWidget(Children->GetChildAt(Index), InTag, InDepth + 1))
            { return Tagged; }
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
            if (const auto Found = FindTaggedWidget(Tab->GetContent(), InTag))
            { return Found; }
        }
        return {};
    }

    auto
        TextOfTagged(
            FCk_ProceduralAction_EditorToolkit& InToolkit,
            FName InTag)
        -> FString
    {
        const auto Widget = FindInToolkit(InToolkit, InTag);
        return Widget.IsValid() && Widget->GetType() == FName{TEXT("STextBlock")}
            ? StaticCastSharedPtr<STextBlock>(Widget)->GetText().ToString() : FString{};
    }

    auto
        ActivateTab(
            FCk_ProceduralAction_EditorToolkit& InToolkit,
            UCk_ProceduralAction_Data* InClip,
            FName InTabId)
        -> void
    {
        InToolkit.FocusWindow(InClip);
        const auto TabManager = InToolkit.GetAssociatedTabManager();
        if (const auto OwnerTab = TabManager.IsValid() ? TabManager->GetOwnerTab() : TSharedPtr<SDockTab>{})
        { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
        if (const auto Tab = FindTab(InToolkit, InTabId))
        { Tab->ActivateInParent(ETabActivationCause::SetDirectly); }
    }

    auto
        FindViewport(
            FCk_ProceduralAction_EditorToolkit& InToolkit)
        -> TSharedPtr<SEditorViewport>
    {
        const auto Widget = FindInToolkit(InToolkit, ViewportTag);
        return Widget.IsValid() && Widget->GetType() == FName{TEXT("SCkProceduralAction_Viewport")}
            ? StaticCastSharedPtr<SEditorViewport>(Widget) : TSharedPtr<SEditorViewport>{};
    }

    auto
        FindViewportSurface(
            const TSharedRef<SWidget>& InWidget,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetType() == FName{TEXT("SViewport")})
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Surface = FindViewportSurface(Children->GetChildAt(Index), InDepth + 1))
            { return Surface; }
        }
        return {};
    }

    struct FAxisSample
    {
        FIntPoint Pixel = FIntPoint::ZeroValue;
        FVector2D Drag = FVector2D::ZeroVector;
        int32 PixelCount = 0;
    };

    auto
        FindXAxisSample(
            FViewport& InViewport,
            FAxisSample& OutSample)
        -> bool
    {
        const auto Size = InViewport.GetSizeXY();
        if (Size.X <= 0 || Size.Y <= 0)
        { return false; }
        auto Proxies = TArray<HHitProxy*>{};
        InViewport.GetHitProxyMap(FIntRect{0, 0, Size.X, Size.Y}, Proxies);
        if (Proxies.Num() != Size.X * Size.Y)
        { return false; }
        auto Pixels = TArray<FIntPoint>{};
        auto SumX = int64{0};
        auto SumY = int64{0};
        for (auto Index = 0; Index < Proxies.Num(); ++Index)
        {
            const auto* Proxy = Proxies[Index];
            if (Proxy == nullptr || NOT Proxy->IsA(HWidgetAxis::StaticGetType())
                || static_cast<const HWidgetAxis*>(Proxy)->Axis != EAxisList::X)
            { continue; }
            const auto Pixel = FIntPoint{Index % Size.X, Index / Size.X};
            Pixels.Add(Pixel);
            SumX += Pixel.X;
            SumY += Pixel.Y;
        }
        if (Pixels.IsEmpty())
        { return false; }
        const auto Center = FVector2D{static_cast<double>(SumX) / Pixels.Num(),
            static_cast<double>(SumY) / Pixels.Num()};
        auto MaximumDistance = 0.0;
        auto Farthest = Pixels[0];
        for (const auto& Pixel : Pixels)
        {
            const auto Delta = FVector2D{static_cast<double>(Pixel.X),
                static_cast<double>(Pixel.Y)} - Center;
            if (Delta.SizeSquared() > MaximumDistance)
            { MaximumDistance = Delta.SizeSquared(); Farthest = Pixel; }
        }
        if (MaximumDistance < 16.0)
        { return false; }
        OutSample.Pixel = Farthest;
        OutSample.Drag = (FVector2D{static_cast<double>(Farthest.X),
            static_cast<double>(Farthest.Y)} - Center).GetSafeNormal() * 60.0;
        OutSample.PixelCount = Pixels.Num();
        return true;
    }

    struct FAxisDragObservation
    {
        bool PressHandled = false;
        bool PickedXAxis = false;
        bool MoveHandled = false;
        bool ReleaseHandled = false;
    };

    struct FAxisDragState
    {
        FVector2D Start = FVector2D::ZeroVector;
        FVector2D ScreenDrag = FVector2D::ZeroVector;
        FVector2D Previous = FVector2D::ZeroVector;
        FAxisDragObservation Observation;
        bool Active = false;
    };

    auto
        BeginXAxisDragThroughSlate(
            const TSharedRef<SWidget>& InSurface,
            FEditorViewportClient& InClient,
            const FAxisSample& InSample,
            FAxisDragState& OutState)
        -> bool
    {
        auto& Slate = FSlateApplication::Get();
        auto Path = FWidgetPath{};
        const auto Window = Slate.FindWidgetWindow(InSurface);
        if (NOT Slate.FindPathToWidget(InSurface, Path) || NOT Path.IsValid()
            || NOT Window.IsValid() || NOT Window->GetNativeWindow().IsValid()
            || InClient.Viewport == nullptr)
        { return false; }
        const auto Size = InClient.Viewport->GetSizeXY();
        if (Size.X <= 0 || Size.Y <= 0)
        { return false; }
        const auto& Geometry = Path.Widgets.Last().Geometry;
        OutState.Start = FVector2D{Geometry.GetAbsolutePositionAtCoordinates(FVector2D{
            (static_cast<double>(InSample.Pixel.X) + 0.5) / Size.X,
            (static_cast<double>(InSample.Pixel.Y) + 0.5) / Size.Y})};
        const auto LocalSize = Geometry.GetLocalSize();
        OutState.ScreenDrag = FVector2D{InSample.Drag.X * LocalSize.X / Size.X,
            InSample.Drag.Y * LocalSize.Y / Size.Y};
        const auto Pressed = TSet<FKey>{EKeys::LeftMouseButton};
        const auto Released = TSet<FKey>{};
        Slate.SetCursorPos(OutState.Start);
        Slate.ProcessMouseMoveEvent(FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            OutState.Start, OutState.Start - FVector2D{3.0, 0.0}, Released,
            EKeys::Invalid, 0.0f,
            FModifierKeysState{}});
        const auto Down = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            OutState.Start, OutState.Start, Pressed, EKeys::LeftMouseButton, 0.0f,
            FModifierKeysState{}};
        OutState.Observation.PressHandled = Slate.ProcessMouseButtonDownEvent(
            Window->GetNativeWindow(), Down);
        OutState.Observation.PickedXAxis = InClient.GetCurrentWidgetAxis() == EAxisList::X;
        OutState.Previous = OutState.Start;
        OutState.Active = OutState.Observation.PressHandled;
        return OutState.Observation.PressHandled && OutState.Observation.PickedXAxis;
    }

    auto
        AdvanceXAxisDragThroughSlate(
            FAxisDragState& InOutState,
            int32 InStep)
        -> void
    {
        if (NOT InOutState.Active)
        { return; }
        auto& Slate = FSlateApplication::Get();
        const auto Position = InOutState.Start
            + InOutState.ScreenDrag * (static_cast<double>(InStep) / 4.0);
        Slate.SetCursorPos(Position);
        const auto Pressed = TSet<FKey>{EKeys::LeftMouseButton};
        InOutState.Observation.MoveHandled = Slate.ProcessMouseMoveEvent(FPointerEvent{
            0, FSlateApplication::CursorPointerIndex, Position, InOutState.Previous,
            Pressed, EKeys::Invalid, 0.0f, FModifierKeysState{}})
            || InOutState.Observation.MoveHandled;
        InOutState.Previous = Position;
    }

    auto
        FinishXAxisDragThroughSlate(
            FAxisDragState& InOutState)
        -> bool
    {
        if (NOT InOutState.Active)
        { return false; }
        InOutState.Active = false;
        auto& Slate = FSlateApplication::Get();
        const auto Up = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            InOutState.Previous, InOutState.Previous, TSet<FKey>{},
            EKeys::LeftMouseButton, 0.0f,
            FModifierKeysState{}};
        InOutState.Observation.ReleaseHandled = Slate.ProcessMouseButtonUpEvent(Up);
        return InOutState.Observation.PressHandled && InOutState.Observation.PickedXAxis
            && InOutState.Observation.MoveHandled && InOutState.Observation.ReleaseHandled;
    }

    auto
        ClickWidget(
            const TSharedRef<SWidget>& InWidget)
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
        const auto Position = FVector2D{Path.Widgets.Last().Geometry.GetAbsolutePositionAtCoordinates(
            FVector2D{0.5, 0.5})};
        const auto Pressed = TSet<FKey>{EKeys::LeftMouseButton};
        const auto Released = TSet<FKey>{};
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released, EKeys::Invalid, 0.0f, FModifierKeysState{}});
        const auto Down = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Pressed,
            EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto Up = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released,
            EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto PressHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        Slate.ProcessMouseButtonUpEvent(Up);
        return PressHandled;
    }

    auto
        SaveCapture(
            const TSharedRef<SWidget>& InWidget,
            const FString& InName,
            TArray<FColor>& OutPixels)
        -> FString
    {
        auto Size = FIntVector{};
        if (NOT FSlateApplication::Get().TakeScreenshot(InWidget, OutPixels, Size)
            || Size.X < 200 || Size.Y < 100 || OutPixels.Num() < Size.X * Size.Y)
        { return {}; }
        const auto Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
            TEXT("ProceduralAction"), TEXT("MountedToolkit"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        const auto Path = FPaths::Combine(Directory, InName + TEXT(".png"));
        auto Opaque = OutPixels;
        for (auto& Pixel : Opaque)
        { Pixel.A = 255; }
        auto Compressed = TArray64<uint8>{};
        FImageUtils::PNGCompressImageArray(Size.X, Size.Y,
            TArrayView64<const FColor>{Opaque.GetData(), Size.X * Size.Y}, Compressed);
        auto Bytes = TArray<uint8>{};
        Bytes.Append(Compressed.GetData(), Compressed.Num());
        if (NOT FFileHelper::SaveArrayToFile(Bytes, *Path)
            || IFileManager::Get().FileSize(*Path) <= 0)
        { return {}; }
        return Path;
    }

    auto
        HasPixelVariation(
            const TArray<FColor>& InPixels)
        -> bool
    {
        if (InPixels.IsEmpty())
        { return false; }
        const auto First = InPixels[0].ToPackedARGB();
        for (auto Index = 1; Index < InPixels.Num(); Index += FMath::Max(1, InPixels.Num() / 10000))
        {
            if (InPixels[Index].ToPackedARGB() != First)
            { return true; }
        }
        return false;
    }

    auto
        CountChangedPixels(
            const TArray<FColor>& InFirst,
            const TArray<FColor>& InSecond)
        -> int32
    {
        if (InFirst.Num() != InSecond.Num())
        { return 0; }
        auto Changed = 0;
        for (auto Index = 0; Index < InFirst.Num(); ++Index)
        {
            const auto& A = InFirst[Index];
            const auto& B = InSecond[Index];
            if (FMath::Abs(int32(A.R) - int32(B.R)) > 8
                || FMath::Abs(int32(A.G) - int32(B.G)) > 8
                || FMath::Abs(int32(A.B) - int32(B.B)) > 8)
            { ++Changed; }
        }
        return Changed;
    }

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TStrongObjectPtr<UCk_ProceduralAction_Data> BodyPulse;
        TStrongObjectPtr<UCk_ProceduralAction_Data> LimbNod;
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> BodyPreview;
        TStrongObjectPtr<UCk_ProceduralActionPreview_Config> LimbPreview;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TArray<FColor> AuthoredPixels;
        TArray<FColor> EarlyPlaybackPixels;
        TWeakPtr<SEditorViewport> BodyViewport;
        double PlaybackStartedSeconds = 0.0;
        double EarlyCaptureSeconds = 0.0;
        double DeadlineSeconds = 0.0;
        int32 Stage = 0;
        int32 SettleFrames = 0;
        FString LastWaitReason;
        FCk_ProceduralActionEditor_Subject PreviousBodySubject;
        FCk_ProceduralActionEditor_Subject PreviousLimbSubject;
        bool SettingsCaptured = false;
        FName LimbSlot;
        FAxisDragState AxisDrag;
        FVector GizmoBefore = FVector::ZeroVector;
        FGuid UndoBeforeGizmo;
        bool GizmoAttempted = false;

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

        auto GizmoKey() const -> const FCk_ProceduralAction_VectorKey*
        {
            if (NOT LimbNod.IsValid() || LimbNod->Get_Phases().IsEmpty()
                || LimbNod->Get_Phases()[0].Get_LimbTracks().IsEmpty()
                || LimbNod->Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys().IsEmpty())
            { return nullptr; }
            return &LimbNod->Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys()[0];
        }

        auto HasChangedGizmoKey() const -> bool
        {
            const auto* Key = GizmoKey();
            return GizmoAttempted && Key != nullptr && NOT Key->Get_Value().Equals(GizmoBefore);
        }

        // The newest undo entry is the gizmo's when it is newer than the drag and holds the LimbNod clip.
        auto UndoOwnedGizmoKey() -> bool
        {
            if (NOT HasChangedGizmoKey() || GEditor == nullptr || GEditor->Trans == nullptr)
            { return false; }
            auto* Transactions = GEditor->Trans.Get();
            const auto Context = Transactions->GetUndoContext(false);
            if (NOT Context.TransactionId.IsValid() || Context.TransactionId == UndoBeforeGizmo)
            { return false; }
            const auto Index = Transactions->GetQueueLength() - Transactions->GetUndoCount() - 1;
            const auto* Transaction = Index >= 0 ? Transactions->GetTransaction(Index) : nullptr;
            if (Transaction == nullptr)
            { return false; }
            auto Objects = TArray<UObject*>{};
            Transaction->GetTransactionObjects(Objects);
            if (NOT Objects.Contains(LimbNod.Get()))
            { return false; }
            const auto Undone = GEditor->UndoTransaction();
            if (Undone)
            { GizmoAttempted = false; }
            return Undone;
        }

        auto Close() -> void
        {
            FinishXAxisDragThroughSlate(AxisDrag);
            if (HasChangedGizmoKey() && NOT UndoOwnedGizmoKey() && Test != nullptr)
            { Test->AddError(TEXT("Test-owned gizmo edit could not be safely restored.")); }
            if (Editors.IsValid())
            {
                Editors->CloseAllEditorsForAsset(BodyPulse.Get());
                Editors->CloseAllEditorsForAsset(LimbNod.Get());
            }
            if (SettingsCaptured && BodyPulse.IsValid() && LimbNod.IsValid())
            {
                UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(BodyPulse.Get())->Set_Subject(PreviousBodySubject);
                UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(LimbNod.Get())->Set_Subject(PreviousLimbSubject);
                SettingsCaptured = false;
            }
        }
    };

    class FRunMountedToolkit final : public IAutomationLatentCommand
    {
    public:
        explicit FRunMountedToolkit(TSharedRef<FState> InState) : _State(InState) {}

        auto Update() -> bool override
        {
            auto& State = *_State;
            if (FPlatformTime::Seconds() > State.DeadlineSeconds)
            {
                State.Test->AddError(FString::Printf(
                    TEXT("Mounted procedural action toolkit timed out at stage %d: %s"),
                    State.Stage, *State.LastWaitReason));
                State.Close();
                return true;
            }
            auto* Editors = State.Editors.Get();
            if (Editors == nullptr)
            {
                State.Test->AddError(TEXT("Asset editor subsystem disappeared."));
                State.Close();
                return true;
            }
            auto* Body = FindToolkit(Editors, State.BodyPulse.Get());
            auto* Limb = FindToolkit(Editors, State.LimbNod.Get());
            if (State.Stage == 0)
            {
                const auto BodyViewportTab = Body != nullptr
                    ? FindTab(*Body, FCk_ProceduralAction_EditorToolkit::ViewportTabId) : TSharedPtr<SDockTab>{};
                const auto LimbViewportTab = Limb != nullptr
                    ? FindTab(*Limb, FCk_ProceduralAction_EditorToolkit::ViewportTabId) : TSharedPtr<SDockTab>{};
                State.LastWaitReason = FString::Printf(TEXT("BodyToolkit=%d LimbToolkit=%d BodyViewportTab=%d LimbViewportTab=%d"),
                    Body != nullptr, Limb != nullptr, BodyViewportTab.IsValid(), LimbViewportTab.IsValid());
                if (NOT BodyViewportTab.IsValid() || NOT LimbViewportTab.IsValid())
                { return false; }
                State.Test->AddInfo(TEXT("Mounted toolkit stage 0: both clip editors are mounted."));
                State.Test->TestTrue(TEXT("two saved clips opened distinct asset editor instances"),
                    Body != Limb && BodyViewportTab != LimbViewportTab);
                State.Test->TestTrue(TEXT("mounted BodyPulse has a viewport, a timeline and its transport"),
                    FindViewport(*Body).IsValid()
                    && FindInToolkit(*Body, TimelineTag).IsValid()
                    && FindInToolkit(*Body, TransportTag).IsValid()
                    && FindInToolkit(*Body, TransportTimeTag).IsValid());
                ActivateTab(*Body, State.BodyPulse.Get(), FCk_ProceduralAction_EditorToolkit::ViewportTabId);
                ++State.Stage;
                return false;
            }
            if (State.Stage >= 1 && State.Stage <= 11 && Body == nullptr)
            {
                State.Test->AddError(TEXT("BodyPulse toolkit closed while the test drove it."));
                State.Close();
                return true;
            }
            if (State.Stage == 1)
            {
                Body->SetScenario(State.BodyPreview.Get());
                State.Test->TestEqual(TEXT("BodyPulse selected its saved sibling preview"),
                    ck::procedural_action_editor::Get_Subject(*State.BodyPulse).Get_Scenario().ToString(), FString{BodyPreviewPath});
                Body->Get_Model()->SelectClip();
                ++State.Stage;
                return false;
            }
            if (State.Stage == 2)
            {
                const auto* Proxy = Body->Get_SelectionProxy();
                const auto ShowsClip = Proxy != nullptr && Proxy->_Shows == ECk_ProceduralActionSelectionProxy_Shows::Clip;
                if (NOT State.Settle(ShowsClip, TEXT("Details did not load the clip selection")))
                { return false; }
                State.Test->TestTrue(TEXT("the Details panel is mounted"), FindInToolkit(*Body, DetailsTag).IsValid());
                State.Test->TestTrue(TEXT("the clip Details show the selected preview"),
                    ShowsClip && Proxy->_Subject.Get_Scenario().ToString() == FString{BodyPreviewPath});
                Body->Get_Model()->SelectSection(State.BodyPulse->Get_Phases()[0].Get_Id());
                ++State.Stage;
                return false;
            }
            if (State.Stage == 3 || State.Stage == 4)
            {
                const auto PhaseIndex = State.Stage - 3;
                const auto& Phases = State.BodyPulse->Get_Phases();
                if (NOT Phases.IsValidIndex(PhaseIndex))
                { State.Stage = 5; return false; }
                const auto* Proxy = Body->Get_SelectionProxy();
                const auto ShowsSection = Proxy != nullptr && Proxy->_Shows == ECk_ProceduralActionSelectionProxy_Shows::Section
                    && Proxy->_SectionName == Phases[PhaseIndex].Get_Id();
                if (NOT State.Settle(ShowsSection, FString::Printf(TEXT("section %s did not reach the Details"),
                    *Phases[PhaseIndex].Get_Id().ToString())))
                { return false; }
                State.Test->TestTrue(FString::Printf(TEXT("authored section %d is selectable and shown"), PhaseIndex),
                    ShowsSection);
                if (Phases.IsValidIndex(PhaseIndex + 1))
                { Body->Get_Model()->SelectSection(Phases[PhaseIndex + 1].Get_Id()); }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 5)
            {
                const auto ViewportTab = FindTab(*Body, FCk_ProceduralAction_EditorToolkit::ViewportTabId);
                if (NOT ViewportTab.IsValid())
                { State.LastWaitReason = TEXT("BodyPulse viewport tab is gone"); return false; }
                const auto Path = SaveCapture(ViewportTab->GetContent(), TEXT("BodyPulse-Authored"), State.AuthoredPixels);
                if (Path.IsEmpty())
                { State.LastWaitReason = TEXT("authored viewport capture unavailable"); return false; }
                State.Test->AddInfo(TEXT("Mounted toolkit stage 5: configured authored capture saved."));
                State.Test->TestTrue(TEXT("mounted authored Slate screenshot has visible pixels"),
                    HasPixelVariation(State.AuthoredPixels));
                State.Test->TestTrue(TEXT("the timeline is mounted"), FindInToolkit(*Body, TimelineTag).IsValid());
                const auto ViewportWidget = FindViewport(*Body);
                if (ViewportWidget.IsValid())
                {
                    if (const auto Client = ViewportWidget->GetViewportClient())
                    {
                        const auto ViewportSize = Client->Viewport != nullptr
                            ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
                        State.Test->AddInfo(FString::Printf(
                            TEXT("Authored viewport frame: size=%d x %d, camera=%s, look-at=%s"),
                            ViewportSize.X, ViewportSize.Y, *Client->GetViewLocation().ToString(),
                            *Client->GetLookAtLocation().ToString()));
                    }
                }
                State.Test->AddInfo(FString::Printf(TEXT("Mounted authored screenshot: %s"), *Path));
                State.BodyViewport = ViewportWidget;
                if (NOT State.Test->TestTrue(TEXT("mounted body viewport supports playback capture"),
                    ViewportWidget.IsValid()))
                { State.Close(); return true; }
                State.Stage = 6;
                return false;
            }
            if (State.Stage == 6)
            {
                const auto Model = Body->Get_Model();
                Model->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Authored);
                State.Test->TestTrue(TEXT("the authored preview is selected"),
                    Model->Get_PreviewMode() == ECk_ProceduralActionEditor_PreviewMode::Authored);
                Model->SetTime(FCk_Time{0.0});
                Body->TogglePlay();
                State.Test->TestTrue(TEXT("Play starts authored playback"), Model->Get_IsPlaying());
                State.PlaybackStartedSeconds = FPlatformTime::Seconds();
                State.Stage = 7;
                return false;
            }
            if (State.Stage == 7)
            {
                if (FPlatformTime::Seconds() - State.PlaybackStartedSeconds < 0.04)
                { return false; }
                const auto Viewport = State.BodyViewport.Pin();
                if (NOT State.Test->TestTrue(TEXT("body viewport survives authored playback"),
                    Viewport.IsValid()))
                { State.Close(); return true; }
                const auto Path = SaveCapture(Viewport.ToSharedRef(), TEXT("BodyPulse-PlayingEarly"),
                    State.EarlyPlaybackPixels);
                if (Path.IsEmpty())
                { State.LastWaitReason = TEXT("early authored viewport capture unavailable"); return false; }
                State.EarlyCaptureSeconds = FPlatformTime::Seconds();
                State.Stage = 8;
                return false;
            }
            if (State.Stage == 8)
            {
                if (FPlatformTime::Seconds() - State.EarlyCaptureSeconds < 0.27)
                { return false; }
                const auto Viewport = State.BodyViewport.Pin();
                if (NOT Viewport.IsValid())
                { State.Test->AddError(TEXT("body viewport vanished during authored playback")); State.Close(); return true; }
                auto LatePixels = TArray<FColor>{};
                const auto Path = SaveCapture(Viewport.ToSharedRef(), TEXT("BodyPulse-PlayingLate"),
                    LatePixels);
                if (Path.IsEmpty())
                { State.LastWaitReason = TEXT("late authored viewport capture unavailable"); return false; }
                const auto Changed = CountChangedPixels(State.EarlyPlaybackPixels, LatePixels);
                State.Test->AddInfo(FString::Printf(
                    TEXT("Authored viewport motion: %d RGB pixels changed across real editor frames."), Changed));
                State.Test->TestTrue(TEXT("rendered authored body changes across playback frames"),
                    Changed > 60);
                const auto Model = Body->Get_Model();
                State.Test->AddInfo(FString::Printf(TEXT("Authored transport during playback: model=%.3fs label='%s'"),
                    Model->Get_Time().Get_Seconds(), *TextOfTagged(*Body, TransportTimeTag)));
                State.Test->TestTrue(TEXT("authored transport advanced during rendered playback"),
                    Model->Get_Time().Get_Seconds() > 0.1);
                if (Model->Get_IsPlaying())
                { Body->TogglePlay(); }
                Model->SetTime(FCk_Time{0.0});
                State.Stage = 9;
                return false;
            }
            if (State.Stage == 9)
            {
                const auto Model = Body->Get_Model();
                State.Test->TestFalse(TEXT("reset leaves the transport paused"), Model->Get_IsPlaying());
                State.Test->TestTrue(TEXT("reset returns the authored transport to zero"),
                    FMath::IsNearlyZero(Model->Get_Time().Get_Seconds(), 1.0e-4));
                Model->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Solved);
                State.Test->TestTrue(TEXT("RestartSolver starts the production Solver"), Body->RestartSolver());
                State.Stage = 10;
                return false;
            }
            if (State.Stage == 10)
            {
                const auto Status = Body->Get_StatusText().ToString();
                State.LastWaitReason = FString::Printf(TEXT("production Solver status or mounted capture unavailable: %s"), *Status);
                if (NOT Status.Contains(TEXT("SOLVER:")))
                { return false; }
                State.Test->TestTrue(TEXT("the viewport HUD shows the Solver status"),
                    TextOfTagged(*Body, StatusTag).Contains(TEXT("SOLVER:")));
                const auto ViewportTab = FindTab(*Body, FCk_ProceduralAction_EditorToolkit::ViewportTabId);
                auto SolverPixels = TArray<FColor>{};
                const auto Path = ViewportTab.IsValid()
                    ? SaveCapture(ViewportTab->GetContent(), TEXT("BodyPulse-Solver"), SolverPixels) : FString{};
                if (Path.IsEmpty())
                { return false; }
                State.Test->AddInfo(FString::Printf(TEXT("Mounted solver screenshot: %s"), *Path));
                State.Test->TestTrue(TEXT("mode change altered mounted Slate pixels"),
                    SolverPixels != State.AuthoredPixels);
                Body->Get_Model()->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Authored);
                State.Stage = 11;
                return false;
            }
            if (State.Stage == 11)
            {
                const auto Model = Body->Get_Model();
                State.Test->TestTrue(TEXT("the preview returns to Authored"),
                    Model->Get_PreviewMode() == ECk_ProceduralActionEditor_PreviewMode::Authored);
                State.Test->TestFalse(TEXT("authored mode removed the Solver status"),
                    Body->Get_StatusText().ToString().Contains(TEXT("SOLVER:")));
                Model->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Solved);
                State.Test->TestTrue(TEXT("RestartSolver restarts the Solver before close"), Body->RestartSolver());
                const auto TimeRequestBefore = Model->Get_TimeRequestRevision();
                Body->StepFrames(1);
                State.Test->TestTrue(TEXT("a frame step requests a Solver step before close"),
                    Model->Get_TimeRequestRevision() > TimeRequestBefore);
                const auto Closed = Editors->CloseAllEditorsForAsset(State.BodyPulse.Get());
                State.Test->TestTrue(TEXT("closing one toolkit closes its editor"), Closed > 0);
                State.Test->TestTrue(TEXT("second toolkit remains independently open"),
                    FindToolkit(Editors, State.LimbNod.Get()) != nullptr);
                if (Limb != nullptr)
                { ActivateTab(*Limb, State.LimbNod.Get(), FCk_ProceduralAction_EditorToolkit::ViewportTabId); }
                State.Stage = 12;
                return false;
            }
            if (Limb == nullptr)
            {
                State.Test->AddError(TEXT("LimbNod toolkit closed while the test drove it."));
                State.Close();
                return true;
            }
            if (State.Stage == 12)
            {
                const auto ViewportTab = FindTab(*Limb, FCk_ProceduralAction_EditorToolkit::ViewportTabId);
                State.LastWaitReason = TEXT("LimbNod editor window unavailable");
                if (NOT ViewportTab.IsValid() || NOT FSlateApplication::Get().FindWidgetWindow(ViewportTab->GetContent()).IsValid())
                { return false; }
                Limb->SetScenario(State.LimbPreview.Get());
                State.Test->TestEqual(TEXT("LimbNod selected its saved sibling preview"),
                    ck::procedural_action_editor::Get_Subject(*State.LimbNod).Get_Scenario().ToString(), FString{LimbPreviewPath});
                Limb->Get_Model()->SetPreviewMode(ECk_ProceduralActionEditor_PreviewMode::Authored);
                ++State.Stage;
                return false;
            }
            if (State.Stage == 13)
            {
                const auto Status = Limb->Get_StatusText().ToString();
                const auto HasPose = Status.Contains(TEXT("Authored requested-space pose"));
                if (NOT State.Settle(HasPose, FString::Printf(TEXT("LimbNod status has no authored pose: %s"), *Status)))
                { return false; }
                State.Test->TestTrue(TEXT("saved limb preview supplies authored initial pose"), HasPose);
                Limb->Get_Model()->SelectSection(State.LimbNod->Get_Phases()[0].Get_Id());
                ActivateTab(*Limb, State.LimbNod.Get(), FCk_ProceduralAction_EditorToolkit::ChannelsTabId);
                ++State.Stage;
                return false;
            }
            if (State.Stage == 14)
            {
                const auto* Proxy = Limb->Get_SelectionProxy();
                const auto ShowsSection = Proxy != nullptr && Proxy->_Shows == ECk_ProceduralActionSelectionProxy_Shows::Section
                    && Proxy->_SectionName == State.LimbNod->Get_Phases()[0].Get_Id();
                if (NOT State.Settle(ShowsSection, TEXT("LimbNod section selection did not reach the Details")))
                { return false; }
                State.Test->TestTrue(TEXT("LimbNod section selection reaches the Details"), ShowsSection);
                const auto Row = FindInToolkit(*Limb, FName{*FString::Printf(TEXT("CkActionEditor.Channel.%s"),
                    *State.LimbSlot.ToString())});
                if (NOT Row.IsValid())
                { State.LastWaitReason = TEXT("LimbNod slot row is absent from the Channels panel."); return false; }
                if (NOT ClickWidget(Row.ToSharedRef()))
                { State.LastWaitReason = TEXT("LimbNod slot row did not accept a routed click."); return false; }
                ++State.Stage;
                return false;
            }
            if (State.Stage == 15)
            {
                const auto Model = Limb->Get_Model();
                const auto Selected = Model->Get_SelectedChannel() == State.LimbSlot;
                if (NOT State.Settle(Selected, TEXT("Routed LimbNod slot click did not select the slot.")))
                { return false; }
                State.Test->TestTrue(TEXT("LimbNod slot selection receives routed click"), Selected);
                const auto* Key = State.GizmoKey();
                if (NOT State.Test->TestNotNull(TEXT("saved limb clip has an endpoint key"), Key))
                { State.Close(); return true; }
                Model->SelectKey(State.LimbNod->Get_Phases()[0].Get_Id(), State.LimbSlot, Key->Get_Time());
                ActivateTab(*Limb, State.LimbNod.Get(), FCk_ProceduralAction_EditorToolkit::ViewportTabId);
                ++State.Stage;
                return false;
            }
            if (State.Stage == 16)
            {
                const auto ViewportWidget = FindViewport(*Limb);
                if (NOT State.Test->TestTrue(TEXT("mounted authored viewport found"), ViewportWidget.IsValid()))
                { State.Close(); return true; }
                const auto ViewportClient = ViewportWidget->GetViewportClient();
                if (NOT State.Test->TestTrue(TEXT("mounted production viewport client found"),
                    ViewportClient.IsValid() && ViewportClient->Viewport != nullptr))
                { State.Close(); return true; }
                const auto Translates = ViewportClient->GetWidgetMode() == UE::Widget::WM_Translate;
                auto Axis = FAxisSample{};
                const auto HasAxis = Translates && FindXAxisSample(*ViewportClient->Viewport, Axis);
                if (NOT State.Settle(HasAxis, TEXT("the selected endpoint has no rendered translation X axis yet")))
                { return false; }
                const auto ViewportTab = FindTab(*Limb, FCk_ProceduralAction_EditorToolkit::ViewportTabId);
                auto LimbPixels = TArray<FColor>{};
                const auto Path = ViewportTab.IsValid()
                    ? SaveCapture(ViewportTab->GetContent(), TEXT("LimbNod-AuthoredGizmo"), LimbPixels) : FString{};
                State.Test->AddInfo(FString::Printf(TEXT("Mounted configured limb screenshot: %s"),
                    Path.IsEmpty() ? TEXT("unavailable") : *Path));
                if (NOT State.Test->TestTrue(TEXT("selected endpoint exposes translation widget"), Translates))
                { State.Close(); return true; }
                const auto* Key = State.GizmoKey();
                if (NOT State.Test->TestNotNull(TEXT("saved limb clip has an endpoint key"), Key))
                { State.Close(); return true; }
                if (NOT State.Test->TestTrue(TEXT("rendered endpoint X axis has real hit-proxy pixels"), HasAxis))
                { State.Close(); return true; }
                State.Test->AddInfo(FString::Printf(
                    TEXT("Rendered endpoint X axis: %d pixels, sample=(%d,%d), drag=%s"),
                    Axis.PixelCount, Axis.Pixel.X, Axis.Pixel.Y, *Axis.Drag.ToString()));
                const auto Surface = FindViewportSurface(ViewportWidget.ToSharedRef());
                if (NOT State.Test->TestTrue(TEXT("mounted authored viewport has routed Slate surface"),
                    Surface.IsValid()))
                { State.Close(); return true; }
                State.GizmoBefore = Key->Get_Value();
                State.UndoBeforeGizmo = GEditor->Trans != nullptr
                    ? GEditor->Trans->GetUndoContext(false).TransactionId : FGuid{};
                State.GizmoAttempted = true;
                const auto Accepted = BeginXAxisDragThroughSlate(Surface.ToSharedRef(),
                    *ViewportClient, Axis, State.AxisDrag);
                State.Test->TestTrue(TEXT("rendered endpoint axis receives routed mouse down"),
                    Accepted);
                State.Stage = 17;
                return false;
            }
            if (State.Stage >= 17 && State.Stage <= 20)
            {
                State.LastWaitReason = TEXT("gizmo drag is advancing over editor frames");
                AdvanceXAxisDragThroughSlate(State.AxisDrag, State.Stage - 16);
                ++State.Stage;
                return false;
            }
            if (State.Stage == 21)
            {
                const auto Accepted = FinishXAxisDragThroughSlate(State.AxisDrag);
                const auto& DragObservation = State.AxisDrag.Observation;
                State.Test->AddInfo(FString::Printf(
                    TEXT("Routed axis drag: down=%d selectedX=%d move=%d up=%d"),
                    DragObservation.PressHandled, DragObservation.PickedXAxis,
                    DragObservation.MoveHandled, DragObservation.ReleaseHandled));
                State.Test->TestTrue(TEXT("rendered endpoint axis accepts a routed Slate mouse drag"),
                    Accepted);
                State.Test->TestTrue(TEXT("mounted gizmo committed a changed endpoint key"),
                    State.HasChangedGizmoKey());
                const auto Undone = State.UndoOwnedGizmoKey();
                State.Test->TestTrue(TEXT("mounted gizmo transaction undoes"), Undone);
                const auto* Key = State.GizmoKey();
                State.Test->TestTrue(TEXT("Undo restores the saved endpoint key"),
                    Key != nullptr && Key->Get_Value().Equals(State.GizmoBefore));
                ++State.Stage;
                return false;
            }
            State.Close();
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            State.Test->TestNull(TEXT("BodyPulse toolkit released after close"),
                Editors->FindEditorForAsset(State.BodyPulse.Get(), false));
            State.Test->TestNull(TEXT("LimbNod toolkit released after close"),
                Editors->FindEditorForAsset(State.LimbNod.Get(), false));
            return true;
        }

    private:
        TSharedRef<FState> _State;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_MountedToolkit,
    "Ck.ProceduralAnimation.Action.Editor.MountedToolkit.TwoTabsModesAndCapture",
    ck_test_procedural_action_mounted_toolkit::TestFlags)

auto
    FCkTest_ProceduralAction_MountedToolkit::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_mounted_toolkit;
    if (NOT FApp::CanEverRender() || NOT FSlateApplication::IsInitialized() || GEditor == nullptr)
    { AddError(TEXT("Mounted toolkit test requires a real RHI and initialized Slate editor.")); return false; }
    auto State = MakeShared<FState>();
    State->Test = this;
    State->BodyPulse.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath));
    State->LimbNod.Reset(LoadObject<UCk_ProceduralAction_Data>(nullptr, LimbNodPath));
    State->BodyPreview.Reset(LoadObject<UCk_ProceduralActionPreview_Config>(nullptr, BodyPreviewPath));
    State->LimbPreview.Reset(LoadObject<UCk_ProceduralActionPreview_Config>(nullptr, LimbPreviewPath));
    State->Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (NOT TestTrue(TEXT("both saved example clips, their previews and asset editor subsystem are available"),
        State->BodyPulse.IsValid() && State->LimbNod.IsValid() && State->Editors.IsValid()
            && State->BodyPreview.IsValid() && State->LimbPreview.IsValid()
            && NOT State->BodyPulse->Get_Phases().IsEmpty()
            && NOT State->LimbNod->Get_Phases().IsEmpty()
            && NOT State->LimbNod->Get_Phases()[0].Get_LimbTracks().IsEmpty()))
    { return false; }
    State->LimbSlot = State->LimbNod->Get_Phases()[0].Get_LimbTracks()[0].Get_Slot();
    State->PreviousBodySubject = ck::procedural_action_editor::Get_Subject(*State->BodyPulse);
    State->PreviousLimbSubject = ck::procedural_action_editor::Get_Subject(*State->LimbNod);
    State->SettingsCaptured = true;
    if (NOT TestTrue(TEXT("BodyPulse opens through its registered asset definition"),
        State->Editors->OpenEditorForAsset(State->BodyPulse.Get(), EToolkitMode::Standalone))
        || NOT TestTrue(TEXT("LimbNod opens as a second toolkit"),
            State->Editors->OpenEditorForAsset(State->LimbNod.Get(), EToolkitMode::Standalone)))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 20.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunMountedToolkit(State));
    return true;
}

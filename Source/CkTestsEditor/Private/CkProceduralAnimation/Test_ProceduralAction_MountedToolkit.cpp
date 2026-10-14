#include "Misc/AutomationTest.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditor_UserSettings.h"

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
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SScrollBox.h"
#include "SEditorViewport.h"
#include "UnrealWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace ck_test_procedural_action_mounted_toolkit
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI;
    const FName EditorTabId{TEXT("CkProceduralAction_Editor")};
    const TCHAR* BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    const TCHAR* LimbNodPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    const TCHAR* BodyPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    const TCHAR* LimbPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_Preview_DA.Ck_ProceduralAction_LimbNod_Preview_DA");

    auto
        HasText(
            const TSharedRef<SWidget>& InWidget,
            const FString& InText,
            int32 InDepth = 0)
        -> bool
    {
        if (InDepth > 64)
        { return false; }
        if (InWidget->GetType() == FName{TEXT("STextBlock")}
            && StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString().Contains(InText))
        { return true; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (HasText(Children->GetChildAt(Index), InText, InDepth + 1))
            { return true; }
        }
        return false;
    }

    auto
        FindButton(
            const TSharedRef<SWidget>& InWidget,
            const FString& InText,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetType() == FName{TEXT("SButton")}
            && HasText(InWidget, InText))
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Button = FindButton(Children->GetChildAt(Index), InText, InDepth + 1))
            { return Button; }
        }
        return {};
    }

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
        FindTextWidget(
            const TSharedRef<SWidget>& InWidget,
            const FString& InText,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetType() == FName{TEXT("STextBlock")}
            && StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString() == InText)
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto TextWidget = FindTextWidget(Children->GetChildAt(Index), InText, InDepth + 1))
            { return TextWidget; }
        }
        return {};
    }

    auto
        ContainsWidget(
            const TSharedRef<SWidget>& InRoot,
            const TSharedRef<SWidget>& InTarget,
            int32 InDepth = 0)
        -> bool
    {
        if (InRoot == InTarget)
        { return true; }
        if (InDepth > 64)
        { return false; }
        auto* Children = InRoot->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (ContainsWidget(Children->GetChildAt(Index), InTarget, InDepth + 1))
            { return true; }
        }
        return false;
    }

    auto
        FindOwningScrollBox(
            const TSharedRef<SWidget>& InRoot,
            const TSharedRef<SWidget>& InTarget,
            int32 InDepth = 0)
        -> TSharedPtr<SScrollBox>
    {
        if (InDepth > 64)
        { return {}; }
        auto* Children = InRoot->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Nested = FindOwningScrollBox(Children->GetChildAt(Index),
                InTarget, InDepth + 1))
            { return Nested; }
        }
        if (InRoot->GetType() == FName{TEXT("SScrollBox")}
            && ContainsWidget(InRoot, InTarget))
        { return StaticCastSharedRef<SScrollBox>(InRoot); }
        return {};
    }

    auto
        FindDetailsByIndex(
            const TSharedRef<SWidget>& InRoot,
            int32 InIndex,
            int32& InOutSeen,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InRoot->GetType() == FName{TEXT("SDetailsView")})
        {
            if (InOutSeen++ == InIndex)
            { return InRoot; }
        }
        auto* Children = InRoot->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Details = FindDetailsByIndex(Children->GetChildAt(Index),
                InIndex, InOutSeen, InDepth + 1))
            { return Details; }
        }
        return {};
    }

    auto
        FindPreviewDetails(
            const TSharedRef<SWidget>& InRoot)
        -> TSharedPtr<SWidget>
    {
        auto Seen = 0;
        return FindDetailsByIndex(InRoot, 1, Seen);
    }

    auto
        IsCenterInside(
            const TSharedRef<SWidget>& InWidget,
            const TSharedRef<SWidget>& InViewport)
        -> bool
    {
        const auto Center = InWidget->GetTickSpaceGeometry()
            .GetAbsolutePositionAtCoordinates(FVector2D{0.5, 0.5});
        const auto Geometry = InViewport->GetTickSpaceGeometry();
        const auto TopLeft = Geometry.GetAbsolutePosition();
        const auto BottomRight = Geometry.GetAbsolutePositionAtCoordinates(FVector2D{1.0, 1.0});
        return Center.X >= TopLeft.X && Center.X <= BottomRight.X
            && Center.Y >= TopLeft.Y && Center.Y <= BottomRight.Y;
    }

    auto
        ScrollDownThroughSlate(
            const TSharedRef<SWidget>& InWidget)
        -> bool
    {
        auto& Slate = FSlateApplication::Get();
        auto Path = FWidgetPath{};
        if (NOT Slate.FindPathToWidget(InWidget, Path) || NOT Path.IsValid())
        { return false; }
        const auto Position = FVector2D{Path.Widgets.Last().Geometry
            .GetAbsolutePositionAtCoordinates(FVector2D{0.5, 0.5})};
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{}, EKeys::Invalid, 0.0f, FModifierKeysState{}});
        return Slate.ProcessMouseWheelOrGestureEvent(
            FPointerEvent{0, FSlateApplication::CursorPointerIndex,
                Position, Position, TSet<FKey>{}, EKeys::Invalid, -12.0f,
                FModifierKeysState{}}, nullptr);
    }

    auto
        FindAuthoredViewport(
            const TSharedRef<SWidget>& InWidget,
            int32 InDepth = 0)
        -> TSharedPtr<SEditorViewport>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetType() == FName{TEXT("SCkProceduralAction_AuthoredViewport")})
        { return StaticCastSharedRef<SEditorViewport>(InWidget); }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Viewport = FindAuthoredViewport(Children->GetChildAt(Index), InDepth + 1))
            { return Viewport; }
        }
        return {};
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
        FindTransportSlider(
            const TSharedRef<SWidget>& InWidget)
        -> TSharedPtr<SSlider>
    {
        const auto Tagged = FindTaggedWidget(InWidget,
            FName{TEXT("CkActionEditorTransportTime")});
        return Tagged.IsValid() && Tagged->GetType() == FName{TEXT("SSlider")}
            ? StaticCastSharedPtr<SSlider>(Tagged) : TSharedPtr<SSlider>{};
    }

    struct FClickObservation
    {
        bool HoverBeforeDown = false;
        bool HoverAfterDown = false;
        bool CapturedAfterDown = false;
        bool ReleaseHandled = false;
    };

    auto
        ClickWidget(
            const TSharedRef<SWidget>& InWidget,
            bool InRequireReleaseHandled = true,
            FClickObservation* OutObservation = nullptr)
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
        if (OutObservation != nullptr)
        { OutObservation->HoverBeforeDown = InWidget->IsHovered(); }
        const auto Down = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Pressed,
            EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto Up = FPointerEvent{0, FSlateApplication::CursorPointerIndex,
            Position, Position, Released,
            EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const auto PressHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        if (OutObservation != nullptr)
        {
            OutObservation->HoverAfterDown = InWidget->IsHovered();
            OutObservation->CapturedAfterDown = InWidget->HasMouseCapture();
        }
        const auto ReleaseHandled = Slate.ProcessMouseButtonUpEvent(Up);
        if (OutObservation != nullptr)
        { OutObservation->ReleaseHandled = ReleaseHandled; }
        return PressHandled && (NOT InRequireReleaseHandled || ReleaseHandled);
    }

    auto
        ClickButton(
            const TSharedRef<SWidget>& InRoot,
            const FString& InLabel)
        -> bool
    {
        const auto Button = FindButton(InRoot, InLabel);
        return Button.IsValid() && ClickWidget(Button.ToSharedRef());
    }

    auto
        ClickTagged(
            const TSharedRef<SWidget>& InRoot,
            const TCHAR* InTag)
        -> bool
    {
        const auto Widget = FindTaggedWidget(InRoot, FName{InTag});
        return Widget.IsValid() && ClickWidget(Widget.ToSharedRef(), false);
    }

    // The raw Details and the recorded diagnostics live in the collapsed Advanced area of the pose workspace.
    auto
        IsAdvancedOpen(
            const TSharedRef<SWidget>& InRoot)
        -> bool
    {
        auto Seen = 0;
        const auto Details = FindDetailsByIndex(InRoot, 0, Seen);
        auto Path = FWidgetPath{};
        return Details.IsValid() && FSlateApplication::Get().FindPathToWidget(Details.ToSharedRef(), Path) && Path.IsValid();
    }

    auto
        ToggleAdvanced(
            const TSharedRef<SWidget>& InRoot)
        -> bool
    { return ClickTagged(InRoot, TEXT("CkActionEditorAdvancedHeader")); }

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
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TSharedPtr<SDockTab> BodyTab;
        TSharedPtr<SDockTab> LimbTab;
        TArray<FColor> AuthoredPixels;
        TArray<FColor> EarlyPlaybackPixels;
        TWeakPtr<SEditorViewport> BodyViewport;
        double PlaybackStartedSeconds = 0.0;
        double EarlyCaptureSeconds = 0.0;
        double DeadlineSeconds = 0.0;
        int32 Stage = 0;
        FString LastWaitReason;
        FString PreviousBodyPreview;
        FString PreviousLimbPreview;
        bool SettingsCaptured = false;
        bool PlacementExpanded = false;
        bool RootFrameObserved = false;
        bool AppearanceClicked = false;
        int32 AppearanceScrollAttempts = 0;
        int32 AppearanceRevealScrollAttempts = 0;
        bool SlotScrollRequested = false;
        bool AdvancedClosedForPlayback = false;
        FAxisDragState AxisDrag;
        FVector GizmoBefore = FVector::ZeroVector;
        FGuid UndoBeforeGizmo;
        bool GizmoAttempted = false;

        auto HasChangedGizmoKey() const -> bool
        {
            if (NOT GizmoAttempted || NOT LimbNod.IsValid()
                || LimbNod->Get_Phases().IsEmpty()
                || LimbNod->Get_Phases()[0].Get_LimbTracks().IsEmpty()
                || LimbNod->Get_Phases()[0].Get_LimbTracks()[0].Get_PositionKeys().IsEmpty())
            { return false; }
            return NOT LimbNod->Get_Phases()[0].Get_LimbTracks()[0]
                .Get_PositionKeys()[0].Get_Value().Equals(GizmoBefore);
        }

        auto UndoOwnedGizmoKey() -> bool
        {
            if (NOT HasChangedGizmoKey() || GEditor == nullptr || GEditor->Trans == nullptr)
            { return false; }
            auto* Transactions = GEditor->Trans.Get();
            const auto Context = Transactions->GetUndoContext(false);
            if (NOT Context.TransactionId.IsValid()
                || Context.TransactionId == UndoBeforeGizmo
                || NOT Context.Title.EqualTo(NSLOCTEXT("CkProceduralActionEditor", "SetEndpoint",
                    "Move Action Endpoint")))
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
            BodyTab.Reset();
            LimbTab.Reset();
            if (Editors.IsValid())
            {
                Editors->CloseAllEditorsForAsset(BodyPulse.Get());
                Editors->CloseAllEditorsForAsset(LimbNod.Get());
            }
            if (SettingsCaptured && BodyPulse.IsValid() && LimbNod.IsValid())
            {
                auto* Settings = GetMutableDefault<UCk_ProceduralActionEditor_UserSettings>();
                const auto BodyChanged = Settings->SetPreviewPath(
                    BodyPulse->GetPathName(), PreviousBodyPreview);
                const auto LimbChanged = Settings->SetPreviewPath(
                    LimbNod->GetPathName(), PreviousLimbPreview);
                if (BodyChanged || LimbChanged)
                { Settings->SaveConfig(); }
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
            if (FPlatformTime::Seconds() > _State->DeadlineSeconds)
            {
                if (_State->Stage == 2 && _State->BodyTab.IsValid())
                {
                    const auto Content = _State->BodyTab->GetContent();
                    auto Pixels = TArray<FColor>{};
                    const auto Capture = SaveCapture(Content, TEXT("BodyPulse-Stage2-Failure"), Pixels);
                    _State->Test->AddInfo(FString::Printf(
                        TEXT("Stage 2 Details diagnostic: screenshot=%s placementClickAssumedExpanded=%d rootObserved=%d globalPlacement=%d globalRoot=%d"),
                        *Capture, _State->PlacementExpanded, _State->RootFrameObserved,
                        FindTextWidget(Content, TEXT("Placement")).IsValid(),
                        FindTextWidget(Content, TEXT("Root Frame")).IsValid()));
                    for (auto Index = 0; Index < 2; ++Index)
                    {
                        auto Seen = 0;
                        const auto Details = FindDetailsByIndex(Content, Index, Seen);
                        if (NOT Details.IsValid())
                        {
                            _State->Test->AddInfo(FString::Printf(
                                TEXT("Stage 2 Details diagnostic: index=%d unavailable seen=%d"),
                                Index, Seen));
                            continue;
                        }
                        const auto RootFrame = FindTextWidget(Details.ToSharedRef(), TEXT("Root Frame"));
                        const auto Size = Details->GetTickSpaceGeometry().GetLocalSize();
                        _State->Test->AddInfo(FString::Printf(
                            TEXT("Stage 2 Details diagnostic: index=%d size=(%.1f,%.1f) placement=%d root=%d rootCenterInside=%d appearance=%d subject=%d"),
                            Index, Size.X, Size.Y,
                            FindTextWidget(Details.ToSharedRef(), TEXT("Placement")).IsValid(),
                            RootFrame.IsValid(),
                            RootFrame.IsValid()
                                && IsCenterInside(RootFrame.ToSharedRef(), Details.ToSharedRef()),
                            FindTextWidget(Details.ToSharedRef(), TEXT("Appearance")).IsValid(),
                            FindTextWidget(Details.ToSharedRef(), TEXT("Subject")).IsValid()));
                    }
                }
                _State->Test->AddError(FString::Printf(
                    TEXT("Mounted procedural action toolkit timed out at stage %d: %s"),
                    _State->Stage, *_State->LastWaitReason));
                _State->Close();
                return true;
            }
            auto* Editors = _State->Editors.Get();
            if (Editors == nullptr)
            {
                _State->Test->AddError(TEXT("Asset editor subsystem disappeared."));
                _State->Close();
                return true;
            }
            auto* BodyEditor = Editors->FindEditorForAsset(_State->BodyPulse.Get(), false);
            auto* LimbEditor = Editors->FindEditorForAsset(_State->LimbNod.Get(), false);
            if (_State->Stage == 0)
            {
                _State->LastWaitReason = FString::Printf(
                    TEXT("BodyEditor=%d LimbEditor=%d BodyManager=%d LimbManager=%d"),
                    BodyEditor != nullptr, LimbEditor != nullptr,
                    BodyEditor != nullptr && BodyEditor->GetAssociatedTabManager().IsValid(),
                    LimbEditor != nullptr && LimbEditor->GetAssociatedTabManager().IsValid());
                if (BodyEditor == nullptr || LimbEditor == nullptr
                    || BodyEditor->GetAssociatedTabManager() == nullptr
                    || LimbEditor->GetAssociatedTabManager() == nullptr)
                { return false; }
                _State->BodyTab = BodyEditor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId});
                _State->LimbTab = LimbEditor->GetAssociatedTabManager()->FindExistingLiveTab(FTabId{EditorTabId});
                _State->LastWaitReason += FString::Printf(TEXT(" BodyTab=%d LimbTab=%d"),
                    _State->BodyTab.IsValid(), _State->LimbTab.IsValid());
                if (NOT _State->BodyTab.IsValid() || NOT _State->LimbTab.IsValid())
                { return false; }
                _State->Test->AddInfo(TEXT("Mounted toolkit stage 0: both editor tabs found."));
                _State->Test->TestTrue(TEXT("two saved clips opened distinct asset editor instances"),
                    BodyEditor != LimbEditor && _State->BodyTab != _State->LimbTab);
                _State->Test->TestTrue(TEXT("mounted BodyPulse has timeline and mode transport"),
                    HasText(_State->BodyTab->GetContent(), TEXT("Authored"))
                    && FindButton(_State->BodyTab->GetContent(), TEXT("Recorded")).IsValid()
                    && FindButton(_State->BodyTab->GetContent(), TEXT("Move endpoint key")).IsValid());
                BodyEditor->FocusWindow(_State->BodyPulse.Get());
                if (const auto OwnerTab = BodyEditor->GetAssociatedTabManager()->GetOwnerTab())
                { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                _State->BodyTab->ActivateInParent(ETabActivationCause::SetDirectly);
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 1)
            {
                if (NOT ClickButton(_State->BodyTab->GetContent(), TEXT("Create Preview Config")))
                { _State->LastWaitReason = TEXT("BodyPulse preview selection button could not receive a routed click."); return false; }
                _State->Test->TestEqual(TEXT("BodyPulse selected saved sibling preview through Slate"),
                    GetDefault<UCk_ProceduralActionEditor_UserSettings>()->GetPreviewPath(
                        _State->BodyPulse->GetPathName()), FString{BodyPreviewPath});
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 2)
            {
                const auto Content = _State->BodyTab->GetContent();
                if (NOT IsAdvancedOpen(Content))
                {
                    if (NOT ToggleAdvanced(Content))
                    { _State->LastWaitReason = TEXT("Advanced area header did not accept a routed click."); }
                    else
                    { _State->LastWaitReason = TEXT("Waiting for Advanced to reveal the raw Details."); }
                    return false;
                }
                if (NOT _State->PlacementExpanded)
                {
                    if (NOT HasText(Content, TEXT("Root Frame")))
                    {
                        const auto Placement = FindTextWidget(Content, TEXT("Placement"));
                        if (NOT Placement.IsValid() || NOT ClickWidget(Placement.ToSharedRef(), false))
                        { _State->LastWaitReason = TEXT("Preview Placement category did not accept a routed click."); return false; }
                    }
                    _State->PlacementExpanded = true;
                    return false;
                }
                const auto Details = FindPreviewDetails(Content);
                if (NOT Details.IsValid())
                { _State->LastWaitReason = TEXT("Preview Details widget is unavailable for scrolling."); return false; }
                if (NOT _State->RootFrameObserved)
                {
                    const auto RootFrame = FindTextWidget(Details.ToSharedRef(), TEXT("Root Frame"));
                    _State->RootFrameObserved = RootFrame.IsValid()
                        && IsCenterInside(RootFrame.ToSharedRef(), Details.ToSharedRef());
                    if (NOT _State->RootFrameObserved)
                    { _State->LastWaitReason = TEXT("Expanded Preview Placement has no visible Root Frame."); return false; }
                }
                const auto Scale = FindTextWidget(Details.ToSharedRef(), TEXT("Body Preview Mesh Scale"));
                if (NOT Scale.IsValid()
                    || NOT IsCenterInside(Scale.ToSharedRef(), Details.ToSharedRef()))
                {
                    if (NOT _State->AppearanceClicked)
                    {
                        const auto Appearance = FindTextWidget(Details.ToSharedRef(), TEXT("Appearance"));
                        if (Appearance.IsValid()
                            && IsCenterInside(Appearance.ToSharedRef(), Details.ToSharedRef()))
                        {
                            if (NOT ClickWidget(Appearance.ToSharedRef(), false))
                            {
                                _State->LastWaitReason = TEXT("Visible Preview Appearance category did not accept a routed click.");
                                return false;
                            }
                            _State->AppearanceClicked = true;
                            return false;
                        }
                        if (_State->AppearanceScrollAttempts < 8)
                        {
                            ++_State->AppearanceScrollAttempts;
                            const auto Handled = ScrollDownThroughSlate(Details.ToSharedRef());
                            _State->LastWaitReason = FString::Printf(
                                TEXT("Scrolling Preview Appearance into view: attempt %d/8, routed=%d"),
                                _State->AppearanceScrollAttempts, Handled);
                            return false;
                        }
                        _State->LastWaitReason = TEXT("Preview Appearance category remained off-screen after eight routed wheels.");
                        return false;
                    }
                    if (_State->AppearanceRevealScrollAttempts < 8)
                    {
                        ++_State->AppearanceRevealScrollAttempts;
                        const auto Handled = ScrollDownThroughSlate(Details.ToSharedRef());
                        _State->LastWaitReason = FString::Printf(
                            TEXT("Scrolling expanded Preview Appearance: attempt %d/8, routed=%d"),
                            _State->AppearanceRevealScrollAttempts, Handled);
                        return false;
                    }
                    _State->LastWaitReason = TEXT("Expanded Preview Appearance did not reveal a visible Body Preview Mesh Scale after eight routed wheels.");
                    return false;
                }
                const auto Window = FSlateApplication::Get().FindWidgetWindow(Content);
                const auto Size = Content->GetTickSpaceGeometry().GetLocalSize();
                _State->LastWaitReason = FString::Printf(
                    TEXT("Authoring capture: WidgetWindow=%d LocalSize=(%.1f,%.1f)"),
                    Window.IsValid(), Size.X, Size.Y);
                const auto Path = SaveCapture(_State->BodyTab->GetContent(), TEXT("BodyPulse-Authored"),
                    _State->AuthoredPixels);
                if (Path.IsEmpty())
                { return false; }
                _State->Test->AddInfo(TEXT("Mounted toolkit stage 2: configured authored capture saved."));
                _State->Test->TestTrue(TEXT("mounted authored Slate screenshot has visible pixels"),
                    HasPixelVariation(_State->AuthoredPixels));
                _State->Test->TestTrue(TEXT("saved preview is visible in mounted picker"),
                    HasText(Content, TEXT("Ck_ProceduralAction_BodyPulse_Preview_DA")));
                _State->Test->TestTrue(TEXT("first authored phase has a mounted row"),
                    HasText(Content, TEXT("Phase Windup")));
                _State->Test->TestTrue(TEXT("second authored phase has a mounted row"),
                    HasText(Content, TEXT("Phase FollowThrough")));
                _State->Test->TestTrue(TEXT("clip Details exposes editable body property"),
                    HasText(Content, TEXT("Body Channel")));
                _State->Test->TestTrue(TEXT("selected preview Details exposes root frame"),
                    _State->RootFrameObserved);
                _State->Test->TestTrue(TEXT("mounted preview Details exposes appearance-only body scale"),
                    Scale.IsValid() && IsCenterInside(Scale.ToSharedRef(), Details.ToSharedRef()));
                if (const auto ViewportWidget = FindAuthoredViewport(Content))
                {
                    if (const auto Client = ViewportWidget->GetViewportClient())
                    {
                        const auto ViewportSize = Client->Viewport != nullptr
                            ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
                        _State->Test->AddInfo(FString::Printf(
                            TEXT("Authored viewport frame: size=%d x %d, camera=%s, look-at=%s"),
                            ViewportSize.X, ViewportSize.Y, *Client->GetViewLocation().ToString(),
                            *Client->GetLookAtLocation().ToString()));
                    }
                }
                _State->Test->AddInfo(FString::Printf(TEXT("Mounted authored screenshot: %s"), *Path));
                _State->BodyViewport = FindAuthoredViewport(Content);
                if (NOT _State->Test->TestTrue(TEXT("mounted body viewport supports playback capture"),
                    _State->BodyViewport.IsValid()))
                { _State->Close(); return true; }
                _State->Test->TestTrue(TEXT("routed Slate click closes Advanced before playback"), ToggleAdvanced(Content));
                _State->Stage = 19;
                return false;
            }
            if (_State->Stage == 19)
            {
                const auto Content = _State->BodyTab->GetContent();
                if (IsAdvancedOpen(Content))
                { _State->LastWaitReason = TEXT("Waiting for Advanced to collapse before authored playback."); return false; }
                _State->Test->TestTrue(TEXT("routed Slate click selects the requested (authored) preview"),
                    ClickTagged(Content, TEXT("CkActionEditorRequestedPreview")));
                _State->Test->TestTrue(TEXT("routed Slate click starts authored playback"),
                    ClickButton(Content, TEXT("Play")));
                _State->PlaybackStartedSeconds = FPlatformTime::Seconds();
                _State->Stage = 20;
                return false;
            }
            if (_State->Stage == 20)
            {
                if (FPlatformTime::Seconds() - _State->PlaybackStartedSeconds < 0.04)
                { return false; }
                const auto Viewport = _State->BodyViewport.Pin();
                if (NOT _State->Test->TestTrue(TEXT("body viewport survives authored playback"),
                    Viewport.IsValid()))
                { _State->Close(); return true; }
                const auto Path = SaveCapture(Viewport.ToSharedRef(), TEXT("BodyPulse-PlayingEarly"),
                    _State->EarlyPlaybackPixels);
                if (Path.IsEmpty())
                { _State->LastWaitReason = TEXT("early authored viewport capture unavailable"); return false; }
                _State->EarlyCaptureSeconds = FPlatformTime::Seconds();
                _State->Stage = 21;
                return false;
            }
            if (_State->Stage == 21)
            {
                if (FPlatformTime::Seconds() - _State->EarlyCaptureSeconds < 0.27)
                { return false; }
                const auto Viewport = _State->BodyViewport.Pin();
                if (NOT Viewport.IsValid())
                { _State->Test->AddError(TEXT("body viewport vanished during authored playback")); _State->Close(); return true; }
                auto LatePixels = TArray<FColor>{};
                const auto Path = SaveCapture(Viewport.ToSharedRef(), TEXT("BodyPulse-PlayingLate"),
                    LatePixels);
                if (Path.IsEmpty())
                { _State->LastWaitReason = TEXT("late authored viewport capture unavailable"); return false; }
                const auto Changed = CountChangedPixels(_State->EarlyPlaybackPixels, LatePixels);
                _State->Test->AddInfo(FString::Printf(
                    TEXT("Authored viewport motion: %d RGB pixels changed across real editor frames."), Changed));
                _State->Test->TestTrue(TEXT("rendered authored body changes across playback frames"),
                    Changed > 60);
                const auto Content = _State->BodyTab->GetContent();
                const auto Transport = FindTransportSlider(Content);
                _State->Test->TestTrue(TEXT("authored transport advanced during rendered playback"),
                    Transport.IsValid() && Transport->GetValue() > 0.1f);
                const auto Reset = FindTaggedWidget(Content,
                    FName{TEXT("CkActionEditorTransportReset")});
                auto ResetClick = FClickObservation{};
                const auto ResetPressed = Reset.IsValid()
                    && ClickWidget(Reset.ToSharedRef(), false, &ResetClick);
                if (Reset.IsValid())
                {
                    auto& Slate = FSlateApplication::Get();
                    auto ResetPath = FWidgetPath{};
                    const auto InWindow = Slate.FindPathToWidget(Reset.ToSharedRef(), ResetPath);
                    auto TransportPath = FWidgetPath{};
                    const auto HasTransportPath = Transport.IsValid()
                        && Slate.FindPathToWidget(Transport.ToSharedRef(), TransportPath);
                    const auto Position = InWindow && ResetPath.IsValid()
                        ? FVector2D{ResetPath.Widgets.Last().Geometry.GetAbsolutePositionAtCoordinates(
                            FVector2D{0.5, 0.5})} : FVector2D::ZeroVector;
                    const auto HitPath = Slate.LocateWindowUnderMouse(
                        Position, Slate.GetInteractiveTopLevelWindows());
                    _State->Test->AddInfo(FString::Printf(
                        TEXT("Reset hit: pressed=%d release=%d hoverBefore=%d hoverDown=%d capturedDown=%d enabled=%d visible=%d path=%d point=%s hitReset=%d hitPath=%s slider=%f transportPath=%s"),
                        ResetPressed, ResetClick.ReleaseHandled, ResetClick.HoverBeforeDown,
                        ResetClick.HoverAfterDown, ResetClick.CapturedAfterDown,
                        Reset->IsEnabled(), Reset->GetVisibility().IsVisible(),
                        InWindow && ResetPath.IsValid(), *Position.ToString(),
                        HitPath.IsValid() && HitPath.ContainsWidget(Reset.Get()),
                        HitPath.IsValid() ? *HitPath.ToString() : TEXT("None"),
                        Transport.IsValid() ? Transport->GetValue() : -1.0f,
                        HasTransportPath ? *TransportPath.ToString() : TEXT("None")));
                }
                _State->Test->TestTrue(TEXT("routed Slate press reaches the reset button"),
                    ResetPressed);
                // SSlider::GetValue reads a TSlateAttribute cached at the next Slate prepass.
                _State->Stage = 22;
                return false;
            }
            if (_State->Stage == 22)
            {
                const auto Content = _State->BodyTab->GetContent();
                const auto Transport = FindTransportSlider(Content);
                _State->Test->TestTrue(TEXT("routed reset returns authored transport to zero"),
                    Transport.IsValid() && FMath::IsNearlyZero(Transport->GetValue(), 1.0e-4f));
                _State->Test->TestTrue(TEXT("routed Slate click starts production Solver"),
                    ClickButton(Content, TEXT("Solver (restart)")));
                _State->Stage = 3;
                return false;
            }
            if (_State->Stage == 3)
            {
                _State->LastWaitReason = TEXT("production Solver status or mounted capture unavailable");
                if (NOT HasText(_State->BodyTab->GetContent(), TEXT("SOLVER:")))
                { return false; }
                if (NOT IsAdvancedOpen(_State->BodyTab->GetContent()))
                {
                    _State->LastWaitReason = TEXT("Advanced must open to reach the Recorded mode.");
                    ToggleAdvanced(_State->BodyTab->GetContent());
                    return false;
                }
                auto SolverPixels = TArray<FColor>{};
                const auto Path = SaveCapture(_State->BodyTab->GetContent(), TEXT("BodyPulse-Solver"),
                    SolverPixels);
                if (Path.IsEmpty())
                { return false; }
                _State->Test->AddInfo(FString::Printf(TEXT("Mounted solver screenshot: %s"), *Path));
                _State->Test->TestTrue(TEXT("routed Slate click enters Recorded mode"),
                    ClickButton(_State->BodyTab->GetContent(), TEXT("Recorded")));
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 4)
            {
                _State->LastWaitReason = TEXT("Recorded capture could not be saved.");
                _State->Test->TestTrue(TEXT("mounted toolkit displays Recorded status after routed click"),
                    HasText(_State->BodyTab->GetContent(), TEXT("RECORDED:")));
                auto RecordedPixels = TArray<FColor>{};
                const auto Path = SaveCapture(_State->BodyTab->GetContent(), TEXT("BodyPulse-Recorded"),
                    RecordedPixels);
                if (Path.IsEmpty())
                { return false; }
                _State->Test->AddInfo(TEXT("Mounted toolkit stage 4: recorded capture saved."));
                _State->Test->AddInfo(FString::Printf(TEXT("Mounted recorded screenshot: %s"), *Path));
                _State->Test->TestTrue(TEXT("mode change altered mounted Slate pixels"),
                    RecordedPixels != _State->AuthoredPixels);
                _State->Test->TestTrue(TEXT("routed Slate click returns to Authored mode"),
                    ClickButton(_State->BodyTab->GetContent(), TEXT("Authored")));
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 5)
            {
                _State->Test->TestFalse(TEXT("authored mode removed Recorded status"),
                    HasText(_State->BodyTab->GetContent(), TEXT("RECORDED:")));
                _State->Test->TestTrue(TEXT("routed Slate click restarts Solver before close"),
                    ClickButton(_State->BodyTab->GetContent(), TEXT("Solver (restart)")));
                _State->Test->TestTrue(TEXT("routed Frame requests a Solver seek before close"),
                    ClickButton(_State->BodyTab->GetContent(), TEXT("Frame")));
                _State->BodyTab.Reset();
                const auto Closed = Editors->CloseAllEditorsForAsset(_State->BodyPulse.Get());
                _State->Test->TestTrue(TEXT("closing one toolkit closes its editor"), Closed > 0);
                _State->Test->TestTrue(TEXT("second toolkit remains independently open"),
                    Editors->FindEditorForAsset(_State->LimbNod.Get(), false) != nullptr);
                if (LimbEditor != nullptr && _State->LimbTab.IsValid())
                {
                    LimbEditor->FocusWindow(_State->LimbNod.Get());
                    if (const auto OwnerTab = LimbEditor->GetAssociatedTabManager()->GetOwnerTab())
                    { OwnerTab->ActivateInParent(ETabActivationCause::SetDirectly); }
                    _State->LimbTab->ActivateInParent(ETabActivationCause::SetDirectly);
                }
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 6)
            {
                _State->LastWaitReason = TEXT("LimbNod preview selection button or editor window unavailable");
                if (NOT _State->LimbTab.IsValid()
                    || NOT FSlateApplication::Get().FindWidgetWindow(_State->LimbTab->GetContent()).IsValid())
                { return false; }
                if (NOT ClickButton(_State->LimbTab->GetContent(), TEXT("Create Preview Config")))
                { return false; }
                _State->Test->TestEqual(TEXT("LimbNod selected saved sibling preview through Slate"),
                    GetDefault<UCk_ProceduralActionEditor_UserSettings>()->GetPreviewPath(
                        _State->LimbNod->GetPathName()), FString{LimbPreviewPath});
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 7)
            {
                const auto Content = _State->LimbTab->GetContent();
                if (NOT HasText(Content, TEXT("Ck_ProceduralAction_LimbNod_Preview_DA")))
                { _State->LastWaitReason = TEXT("LimbNod preview asset is not shown in mounted picker"); return false; }
                _State->Test->TestTrue(TEXT("saved limb preview supplies authored initial pose"),
                    HasText(Content, TEXT("Authored requested-space pose")));
                _State->Test->TestTrue(TEXT("LimbNod phase selection receives routed click"),
                    ClickButton(Content, FString::Printf(TEXT("Phase %s"),
                        *_State->LimbNod->Get_Phases()[0].Get_Id().ToString())));
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 8)
            {
                const auto Content = _State->LimbTab->GetContent();
                const auto Label = FString::Printf(TEXT("CkActionEditorChannel_%s"),
                    *_State->LimbNod->Get_Slots()[0].Get_Id().ToString());
                const auto SlotButton = FindTaggedWidget(Content, FName{*Label});
                if (NOT SlotButton.IsValid())
                { _State->LastWaitReason = TEXT("LimbNod slot row is absent from the mounted subject tree."); return false; }
                const auto ScrollBox = FindOwningScrollBox(Content, SlotButton.ToSharedRef());
                if (NOT ScrollBox.IsValid())
                { _State->LastWaitReason = TEXT("LimbNod slot row has no owning timeline scroll box."); return false; }
                if (NOT _State->SlotScrollRequested
                    || NOT IsCenterInside(SlotButton.ToSharedRef(), ScrollBox.ToSharedRef()))
                {
                    ScrollBox->ScrollDescendantIntoView(SlotButton, false);
                    _State->SlotScrollRequested = true;
                    _State->LastWaitReason = TEXT("Waiting for the LimbNod slot row to scroll into view.");
                    return false;
                }
                if (NOT ClickWidget(SlotButton.ToSharedRef()))
                { _State->LastWaitReason = TEXT("Visible LimbNod slot row did not accept a routed click."); return false; }
                const auto SelectedSlot = FindTaggedWidget(Content, FName{*Label});
                if (NOT SelectedSlot.IsValid() || NOT HasText(SelectedSlot.ToSharedRef(), TEXT("SELECTED")))
                { _State->LastWaitReason = TEXT("Routed LimbNod slot click did not select the slot."); return false; }
                _State->Test->TestTrue(TEXT("LimbNod slot selection receives routed click"), true);
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 9)
            {
                const auto Content = _State->LimbTab->GetContent();
                if (NOT ClickButton(Content, TEXT("Move endpoint key")))
                { _State->LastWaitReason = TEXT("Mounted endpoint gizmo control did not accept a routed click."); return false; }
                _State->Test->TestTrue(TEXT("mounted endpoint gizmo control receives routed click"), true);
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 10)
            {
                const auto Content = _State->LimbTab->GetContent();
                auto LimbPixels = TArray<FColor>{};
                const auto Path = SaveCapture(Content, TEXT("LimbNod-AuthoredGizmo"), LimbPixels);
                if (Path.IsEmpty())
                { _State->LastWaitReason = TEXT("selected LimbNod authored capture unavailable"); return false; }
                _State->Test->AddInfo(FString::Printf(TEXT("Mounted configured limb screenshot: %s"), *Path));
                const auto ViewportWidget = FindAuthoredViewport(Content);
                if (NOT _State->Test->TestTrue(TEXT("mounted authored viewport found"), ViewportWidget.IsValid()))
                { _State->Close(); return true; }
                const auto ViewportClient = ViewportWidget->GetViewportClient();
                if (NOT _State->Test->TestTrue(TEXT("mounted production viewport client found"),
                    ViewportClient.IsValid() && ViewportClient->Viewport != nullptr))
                { _State->Close(); return true; }
                if (NOT _State->Test->TestTrue(TEXT("selected endpoint exposes translation widget"),
                    ViewportClient->GetWidgetMode() == UE::Widget::WM_Translate))
                { _State->Close(); return true; }
                const auto& Tracks = _State->LimbNod->Get_Phases()[0].Get_LimbTracks();
                if (NOT _State->Test->TestTrue(TEXT("saved limb clip has an endpoint key"),
                    NOT Tracks.IsEmpty() && NOT Tracks[0].Get_PositionKeys().IsEmpty()))
                { _State->Close(); return true; }
                const auto Before = Tracks[0].Get_PositionKeys()[0].Get_Value();
                auto Axis = FAxisSample{};
                if (NOT _State->Test->TestTrue(TEXT("rendered endpoint X axis has real hit-proxy pixels"),
                    FindXAxisSample(*ViewportClient->Viewport, Axis)))
                { _State->Close(); return true; }
                _State->Test->AddInfo(FString::Printf(
                    TEXT("Rendered endpoint X axis: %d pixels, sample=(%d,%d), drag=%s"),
                    Axis.PixelCount, Axis.Pixel.X, Axis.Pixel.Y, *Axis.Drag.ToString()));
                const auto Surface = FindViewportSurface(ViewportWidget.ToSharedRef());
                if (NOT _State->Test->TestTrue(TEXT("mounted authored viewport has routed Slate surface"),
                    Surface.IsValid()))
                { _State->Close(); return true; }
                _State->GizmoBefore = Before;
                _State->UndoBeforeGizmo = GEditor->Trans != nullptr
                    ? GEditor->Trans->GetUndoContext(false).TransactionId : FGuid{};
                _State->GizmoAttempted = true;
                const auto Accepted = BeginXAxisDragThroughSlate(Surface.ToSharedRef(),
                    *ViewportClient, Axis, _State->AxisDrag);
                _State->Test->TestTrue(TEXT("rendered endpoint axis receives routed mouse down"),
                    Accepted);
                _State->Stage = 11;
                return false;
            }
            if (_State->Stage >= 11 && _State->Stage <= 14)
            {
                _State->LastWaitReason = TEXT("gizmo drag is advancing over editor frames");
                AdvanceXAxisDragThroughSlate(_State->AxisDrag, _State->Stage - 10);
                ++_State->Stage;
                return false;
            }
            if (_State->Stage == 15)
            {
                const auto Accepted = FinishXAxisDragThroughSlate(_State->AxisDrag);
                const auto& DragObservation = _State->AxisDrag.Observation;
                _State->Test->AddInfo(FString::Printf(
                    TEXT("Routed axis drag: down=%d selectedX=%d move=%d up=%d"),
                    DragObservation.PressHandled, DragObservation.PickedXAxis,
                    DragObservation.MoveHandled, DragObservation.ReleaseHandled));
                _State->Test->TestTrue(TEXT("rendered endpoint axis accepts a routed Slate mouse drag"),
                    Accepted);
                const auto After = _State->LimbNod->Get_Phases()[0]
                    .Get_LimbTracks()[0].Get_PositionKeys()[0].Get_Value();
                _State->Test->TestTrue(TEXT("mounted gizmo committed a changed endpoint key"),
                    NOT After.Equals(_State->GizmoBefore));
                const auto Undone = _State->UndoOwnedGizmoKey();
                _State->Test->TestTrue(TEXT("mounted gizmo transaction undoes"), Undone);
                _State->Test->TestTrue(TEXT("Undo restores the saved endpoint key"),
                    _State->LimbNod->Get_Phases()[0].Get_LimbTracks()[0]
                        .Get_PositionKeys()[0].Get_Value().Equals(_State->GizmoBefore));
                ++_State->Stage;
                return false;
            }
            _State->Close();
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
            _State->Test->TestNull(TEXT("BodyPulse toolkit released after close"),
                Editors->FindEditorForAsset(_State->BodyPulse.Get(), false));
            _State->Test->TestNull(TEXT("LimbNod toolkit released after close"),
                Editors->FindEditorForAsset(_State->LimbNod.Get(), false));
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
    State->Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (NOT TestTrue(TEXT("both saved example clips and asset editor subsystem are available"),
        State->BodyPulse.IsValid() && State->LimbNod.IsValid() && State->Editors.IsValid()
            && NOT State->BodyPulse->Get_Phases().IsEmpty()
            && NOT State->LimbNod->Get_Phases().IsEmpty()
            && NOT State->LimbNod->Get_Slots().IsEmpty()))
    { return false; }
    const auto* Settings = GetDefault<UCk_ProceduralActionEditor_UserSettings>();
    State->PreviousBodyPreview = Settings->GetPreviewPath(State->BodyPulse->GetPathName());
    State->PreviousLimbPreview = Settings->GetPreviewPath(State->LimbNod->GetPathName());
    State->SettingsCaptured = true;
    if (NOT TestTrue(TEXT("BodyPulse opens through its registered asset definition"),
        State->Editors->OpenEditorForAsset(State->BodyPulse.Get(), EToolkitMode::Standalone))
        || NOT TestTrue(TEXT("LimbNod opens as a second toolkit"),
            State->Editors->OpenEditorForAsset(State->LimbNod.Get(), EToolkitMode::Standalone)))
    { State->Close(); return false; }
    State->DeadlineSeconds = FPlatformTime::Seconds() + 15.0;
    ADD_LATENT_AUTOMATION_COMMAND(FRunMountedToolkit(State));
    return true;
}

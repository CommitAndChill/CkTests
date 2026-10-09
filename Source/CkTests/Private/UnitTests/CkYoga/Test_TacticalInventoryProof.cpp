#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "Layout/SlateRect.h"
#include "CkSlateLayout/CkUiInventoryGrid.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Styling/SlateBrush.h"
#include "Types/SlateAttributeMetaData.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SWindow.h"

namespace ck_tactical_inventory_proof
{
    auto ImageSize(const TSharedPtr<SImage>& Image) -> FVector2D
    {
        if (!Image.IsValid()) { return FVector2D::ZeroVector; }
        FSlateAttributeMetaData::UpdateAllAttributes(*Image,
            FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidation);
        Image->Invalidate(EInvalidateWidgetReason::Layout);
        Image->SlatePrepass(1.0f);
        return Image->GetDesiredSize();
    }
    template <typename TWidget>
    auto FindTagged(const TSharedRef<SWidget>& Root, const FString& Type, FName Tag) -> TSharedPtr<TWidget>
    {
        if (Root->GetTypeAsString() == Type && Root->GetTag() == Tag)
        { return StaticCastSharedRef<TWidget>(Root); }
        const auto* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = FindTagged<TWidget>(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Type, Tag);
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }
    auto FindTag(const TSharedRef<SWidget>& Root, FName Tag) -> TSharedPtr<SWidget>
    {
        if (Root->GetTag() == Tag) { return Root; }
        const auto* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = FindTag(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Tag);
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }
    auto RectFor(const TSharedRef<SWidget>& Root, const TCHAR* Tag, FSlateRect& Out) -> bool
    {
        const auto Widget = FindTag(Root, FName(Tag));
        if (!Widget.IsValid()) { return false; }
        Out = Widget->GetCachedGeometry().GetRenderBoundingRect();
        return FMath::IsFinite(Out.Left) && FMath::IsFinite(Out.Top)
            && FMath::IsFinite(Out.Right) && FMath::IsFinite(Out.Bottom)
            && Out.Right > Out.Left && Out.Bottom > Out.Top;
    }
    auto Inside(const FSlateRect& Outer, const FSlateRect& Inner, float Inset = 0.0f) -> bool
    {
        constexpr float RoundingAllowance = 2.0f;
        return Inner.Left >= Outer.Left + Inset - RoundingAllowance
            && Inner.Top >= Outer.Top + Inset - RoundingAllowance
            && Inner.Right <= Outer.Right - Inset + RoundingAllowance
            && Inner.Bottom <= Outer.Bottom - Inset + RoundingAllowance;
    }
    auto LayoutFits(UWorld* World, const TSharedRef<SWidget>& Root) -> bool
    {
        auto* Client = IsValid(World) ? World->GetGameViewport() : nullptr;
        const auto Viewport = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Viewport.IsValid()) { return false; }
        const FSlateRect Screen = Viewport->GetCachedGeometry().GetRenderBoundingRect();
        FSlateRect Shell, Operator, Pack, Stash, Inspection, PackGrid, StashGrid, CaseGrid, CasePanel;
        if (!RectFor(Root, TEXT("shell"), Shell)
            || !RectFor(Root, TEXT("operator-panel"), Operator)
            || !RectFor(Root, TEXT("pack-panel"), Pack)
            || !RectFor(Root, TEXT("stash-panel"), Stash)
            || !RectFor(Root, TEXT("inspection-panel"), Inspection)
            || !RectFor(Root, TEXT("pack-grid"), PackGrid)
            || !RectFor(Root, TEXT("stash-grid"), StashGrid)
            || !RectFor(Root, TEXT("case-grid"), CaseGrid)
            || !RectFor(Root, TEXT("case-panel"), CasePanel)) { return false; }
        if (!Inside(Screen, Shell) || !Inside(Shell, Operator) || !Inside(Shell, Pack)
            || !Inside(Shell, Stash) || !Inside(Shell, Inspection)
            || !Inside(Pack, PackGrid) || !Inside(Pack, CasePanel)
            || !Inside(CasePanel, CaseGrid) || !Inside(Stash, StashGrid)) { return false; }
        FSlateRect Content, Name, Description, Condition, Ownership;
        if (!RectFor(Root, TEXT("inspection-content"), Content)
            || !RectFor(Root, TEXT("inspection-name"), Name)
            || !RectFor(Root, TEXT("inspection-description"), Description)
            || !RectFor(Root, TEXT("condition-value"), Condition)
            || !RectFor(Root, TEXT("ownership-value"), Ownership)) { return false; }
        const auto ContentWidget = FindTag(Root, TEXT("inspection-content"));
        const float Scale = ContentWidget.IsValid()
            ? ContentWidget->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale() : 0.0f;
        return Scale > 0.0f && Inside(Inspection, Content)
            && Inside(Content, Name, 4.0f * Scale)
            && Inside(Content, Description, 4.0f * Scale)
            && Inside(Content, Condition, 4.0f * Scale)
            && Inside(Content, Ownership, 4.0f * Scale);
    }

    auto TextField(const FString& Name, const FString& Value) -> FCk_UI_AuthoredFieldValue
    {
        FCk_UI_AuthoredFieldValue Field{Name, ECk_UI_AuthoredFieldKind::Text};
        Field.Set_Text(FText::FromString(Value)); return Field;
    }
    auto NumberField(const FString& Name, float Value) -> FCk_UI_AuthoredFieldValue
    {
        FCk_UI_AuthoredFieldValue Field{Name, ECk_UI_AuthoredFieldKind::Number};
        Field.Set_Number(Value); return Field;
    }
    auto BoolField(const FString& Name, bool Value) -> FCk_UI_AuthoredFieldValue
    {
        FCk_UI_AuthoredFieldValue Field{Name, ECk_UI_AuthoredFieldKind::Bool};
        Field.Set_Bool(Value); return Field;
    }
    auto Record(const FString& Key, float X, float Y, float W, float H, bool Rotated = false)
        -> FCk_UI_AuthoredRecord
    {
        FCk_UI_AuthoredRecord Result{Key};
        Result.Set_Fields({TextField(TEXT("name"), Key), TextField(TEXT("kind"), TEXT("TEST")),
            NumberField(TEXT("x"), X), NumberField(TEXT("y"), Y), NumberField(TEXT("w"), W),
            NumberField(TEXT("h"), H), NumberField(TEXT("icon"), 0), NumberField(TEXT("quantity"), 1),
            BoolField(TEXT("rotated"), Rotated), BoolField(TEXT("selected"), false),
            BoolField(TEXT("enabled"), true)});
        return Result;
    }
    auto Collection() -> FCk_UI_AuthoredCollection
    {
        FCk_UI_AuthoredCollection Result{TEXT("Items")};
        Result.Set_Schema({{TEXT("name"), ECk_UI_AuthoredFieldKind::Text},
            {TEXT("kind"), ECk_UI_AuthoredFieldKind::Text},
            {TEXT("x"), ECk_UI_AuthoredFieldKind::Number}, {TEXT("y"), ECk_UI_AuthoredFieldKind::Number},
            {TEXT("w"), ECk_UI_AuthoredFieldKind::Number}, {TEXT("h"), ECk_UI_AuthoredFieldKind::Number},
            {TEXT("icon"), ECk_UI_AuthoredFieldKind::Number},
            {TEXT("quantity"), ECk_UI_AuthoredFieldKind::Number},
            {TEXT("rotated"), ECk_UI_AuthoredFieldKind::Bool},
            {TEXT("selected"), ECk_UI_AuthoredFieldKind::Bool},
            {TEXT("enabled"), ECk_UI_AuthoredFieldKind::Bool}});
        Result.Set_Records({Record(TEXT("a"), 0, 0, 2, 1), Record(TEXT("b"), 3, 1, 1, 2)});
        return Result;
    }
    auto CropCollection(bool PartialSchema, bool InvertedBounds) -> FCk_UI_AuthoredCollection
    {
        auto Result = Collection();
        auto Schema = Result.Get_Schema();
        Schema.Add({TEXT("icon-left"), ECk_UI_AuthoredFieldKind::Number});
        if (!PartialSchema)
        {
            Schema.Add({TEXT("icon-top"), ECk_UI_AuthoredFieldKind::Number});
            Schema.Add({TEXT("icon-right"), ECk_UI_AuthoredFieldKind::Number});
            Schema.Add({TEXT("icon-bottom"), ECk_UI_AuthoredFieldKind::Number});
        }
        Result.Set_Schema(Schema);
        auto Records = Result.Get_Records();
        for (auto& Item : Records)
        {
            auto Fields = Item.Get_Fields();
            Fields.Add(NumberField(TEXT("icon-left"), InvertedBounds ? 0.8f : 0.1f));
            if (!PartialSchema)
            {
                Fields.Add(NumberField(TEXT("icon-top"), 0.2f));
                Fields.Add(NumberField(TEXT("icon-right"), InvertedBounds ? 0.2f : 0.9f));
                Fields.Add(NumberField(TEXT("icon-bottom"), 0.8f));
            }
            Item.Set_Fields(Fields);
        }
        Result.Set_Records(Records);
        return Result;
    }

    auto Invoke(UObject* Object, FName Name, const TArray<FString>& Strings = {},
        const TArray<int32>& Ints = {}, const TArray<bool>& Bools = {}) -> bool
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        if (Function == nullptr || !CastField<FBoolProperty>(Function->GetReturnProperty())
            || Function->NumParms != Strings.Num() + Ints.Num() + Bools.Num() + 1) { return false; }
        FStructOnScope Params{Function};
        void* Memory = Params.GetStructMemory();
        int32 StringIndex = 0, IntIndex = 0, BoolIndex = 0;
        bool Valid = true;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm)) { continue; }
            if (auto* Text = CastField<FStrProperty>(Property))
            { if (!Strings.IsValidIndex(StringIndex)) { Valid = false; break; }
              Text->SetPropertyValue_InContainer(Memory, Strings[StringIndex++]); }
            else if (auto* Number = CastField<FIntProperty>(Property))
            { if (!Ints.IsValidIndex(IntIndex)) { Valid = false; break; }
              Number->SetPropertyValue_InContainer(Memory, Ints[IntIndex++]); }
            else if (auto* Flag = CastField<FBoolProperty>(Property))
            { if (!Bools.IsValidIndex(BoolIndex)) { Valid = false; break; }
              Flag->SetPropertyValue_InContainer(Memory, Bools[BoolIndex++]); }
            else { Valid = false; break; }
        }
        Valid = Valid && StringIndex == Strings.Num() && IntIndex == Ints.Num() && BoolIndex == Bools.Num();
        if (Valid) { Object->ProcessEvent(Function, Memory); }
        return Valid && CastFieldChecked<FBoolProperty>(Function->GetReturnProperty())
            ->GetPropertyValue_InContainer(Memory);
    }

    template <typename TProperty, typename TValue>
    auto Getter(UObject* Object, FName Name, TValue Default) -> TValue
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        const auto* Return = Function != nullptr ? CastField<TProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return Default; }
        FStructOnScope Params{Function};
        Object->ProcessEvent(Function, Params.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Params.GetStructMemory());
    }
    template <typename TProperty, typename TValue>
    auto KeyGetter(UObject* Object, FName Name, const FString& Key, TValue Default) -> TValue
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        const auto* Return = Function != nullptr ? CastField<TProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 2) { return Default; }
        FStructOnScope Params{Function};
        int32 Count = 0;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm)) { continue; }
            auto* Text = CastField<FStrProperty>(Property);
            if (Text == nullptr) { return Default; }
            Text->SetPropertyValue_InContainer(Params.GetStructMemory(), Key); ++Count;
        }
        if (Count != 1) { return Default; }
        Object->ProcessEvent(Function, Params.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Params.GetStructMemory());
    }
    auto Hud(UWorld* World) -> UUserWidget*
    {
        auto* Player = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
        UFunction* Function = IsValid(Player) ? Player->FindFunction(TEXT("Get_Hud")) : nullptr;
        if (Function == nullptr || Function->NumParms != 1) { return nullptr; }
        FStructOnScope Params{Function}; Player->ProcessEvent(Function, Params.GetStructMemory());
        auto* Return = CastField<FObjectProperty>(Function->GetReturnProperty());
        return Return != nullptr ? Cast<UUserWidget>(Return->GetObjectPropertyValue_InContainer(Params.GetStructMemory())) : nullptr;
    }
    auto Session(UWorld* World) -> UCk_UI_AuthoredSession_UE*
    {
        auto* Widget = Hud(World);
        UFunction* Function = IsValid(Widget) ? Widget->FindFunction(TEXT("Get_Session")) : nullptr;
        if (Function == nullptr || Function->NumParms != 1) { return nullptr; }
        FStructOnScope Params{Function}; Widget->ProcessEvent(Function, Params.GetStructMemory());
        auto* Return = CastField<FObjectProperty>(Function->GetReturnProperty());
        return Return != nullptr ? Cast<UCk_UI_AuthoredSession_UE>(Return->GetObjectPropertyValue_InContainer(Params.GetStructMemory())) : nullptr;
    }
    auto Registry(UWorld* World) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }
    auto Mutations(UObject* Widget) -> int32
    { return Getter<FIntProperty, int32>(Widget, TEXT("Get_MutationCount"), -1); }
    auto Container(UObject* Widget, const FString& Key) -> FString
    { return KeyGetter<FStrProperty, FString>(Widget, TEXT("Get_ItemContainer"), Key, {}); }
    auto X(UObject* Widget, const FString& Key) -> int32
    { return KeyGetter<FIntProperty, int32>(Widget, TEXT("Get_ItemX"), Key, -1); }
    auto Y(UObject* Widget, const FString& Key) -> int32
    { return KeyGetter<FIntProperty, int32>(Widget, TEXT("Get_ItemY"), Key, -1); }
    auto Rotated(UObject* Widget, const FString& Key) -> bool
    { return KeyGetter<FBoolProperty, bool>(Widget, TEXT("Get_ItemRotated"), Key, false); }

    auto Grid(UWorld* World, const TCHAR* Tag) -> TSharedPtr<SCkUiInventoryGrid>
    {
        auto* Owner = Session(World);
        const auto View = IsValid(Owner) ? Owner->Get_View() : nullptr;
        return View.IsValid() ? FindTagged<SCkUiInventoryGrid>(View->GetRegion(TEXT("main")),
            TEXT("SCkUiInventoryGrid"), FName(Tag)) : nullptr;
    }
    auto GridPoint(const TSharedPtr<SCkUiInventoryGrid>& GridWidget, float CellX, float CellY) -> FVector2D
    {
        const FGeometry Geometry = GridWidget->GetCachedGeometry();
        return Geometry.LocalToAbsolute(FVector2D(CellX * GridWidget->GetCellSize(),
            CellY * GridWidget->GetCellSize()));
    }
    auto ClickGridCell(const TSharedPtr<SCkUiInventoryGrid>& GridWidget, float CellX, float CellY,
        int32 User) -> bool
    {
        if (!GridWidget.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(GridWidget.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true); Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        const FVector2D Position = GridPoint(GridWidget, CellX, CellY);
        if (!GridWidget->GetCachedGeometry().IsUnderLocation(Position)) { return false; }
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(FPointerEvent(static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{}, EKeys::Invalid, 0.0f, FModifierKeysState{}), true);
        const TArray<TSharedRef<SWindow>> Windows{Window.ToSharedRef()};
        if (!Slate.LocateWindowUnderMouse(Position, Windows, false, User).ContainsWidget(GridWidget.Get()))
        { return false; }
        const FPointerEvent Down{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const FPointerEvent Up{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const bool DownHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const bool UpHandled = Slate.ProcessMouseButtonUpEvent(Up);
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        return DownHandled && UpHandled;
    }
    auto DragGridCell(const TSharedPtr<SCkUiInventoryGrid>& Source,
        const TSharedPtr<SCkUiInventoryGrid>& Target, float FromX, float FromY,
        float ToX, float ToY, int32 User) -> bool
    {
        if (!Source.IsValid() || !Target.IsValid())
        { UE_LOG(LogTemp, Warning, TEXT("TacticalInventory drag: missing source or target grid")); return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Source.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()
            || Slate.FindWidgetWindow(Target.ToSharedRef()) != Window)
        { UE_LOG(LogTemp, Warning, TEXT("TacticalInventory drag: grids do not share a native window")); return false; }
        Window->BringToFront(true); Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        const FVector2D Start = GridPoint(Source, FromX, FromY);
        const FVector2D Finish = GridPoint(Target, ToX, ToY);
        const FVector2D Trigger = Start + FVector2D{24.0f, 0.0f};
        if (!Source->GetCachedGeometry().IsUnderLocation(Start)
            || !Target->GetCachedGeometry().IsUnderLocation(Finish))
        {
            UE_LOG(LogTemp, Warning, TEXT("TacticalInventory drag: point outside grid; start=(%.2f,%.2f) finish=(%.2f,%.2f)"),
                Start.X, Start.Y, Finish.X, Finish.Y);
            return false;
        }
        const TArray<TSharedRef<SWindow>> Windows{Window.ToSharedRef()};
        const bool SourceHit = Slate.LocateWindowUnderMouse(Start, Windows, false, User).ContainsWidget(Source.Get());
        if (!SourceHit)
        {
            UE_LOG(LogTemp, Warning, TEXT("TacticalInventory drag: source misses Slate hit test at (%.2f,%.2f) user=%d"),
                Start.X, Start.Y, User);
            return false;
        }
        const TSet<FKey> Held{EKeys::LeftMouseButton};
        Slate.SetCursorPos(Start);
        Slate.ProcessMouseMoveEvent(FPointerEvent(static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Start, Start, TSet<FKey>{}, EKeys::Invalid, 0.0f, FModifierKeysState{}), true);
        const FPointerEvent Down{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Start, Start, Held, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const bool DownHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        if (!DownHandled)
        {
            UE_LOG(LogTemp, Warning, TEXT("TacticalInventory drag: pointer down unhandled at (%.2f,%.2f) user=%d"),
                Start.X, Start.Y, User);
            return false;
        }
        Slate.SetCursorPos(Trigger);
        Slate.ProcessMouseMoveEvent(FPointerEvent(static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Trigger, Start, Held, EKeys::Invalid, 0.0f, FModifierKeysState{}), false);
        const bool DragStarted = Slate.IsDragDropping();
        Slate.SetCursorPos(Finish);
        Slate.ProcessMouseMoveEvent(FPointerEvent(static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Finish, Trigger, Held, EKeys::Invalid, 0.0f, FModifierKeysState{}), false);
        const bool DragReachedTarget = Slate.IsDragDropping();
        const bool TargetHit = Slate.LocateWindowUnderMouse(Finish, Windows, false, User).ContainsWidget(Target.Get());
        const FPointerEvent Up{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Finish, Trigger, TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const bool UpHandled = Slate.ProcessMouseButtonUpEvent(Up);
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        const bool Routed = DragStarted && DragReachedTarget && TargetHit && UpHandled;
        if (!Routed)
        {
            const FVector2D Local = Target->GetCachedGeometry().AbsoluteToLocal(Finish);
            UE_LOG(LogTemp, Warning,
                TEXT("TacticalInventory drag: start=(%.2f,%.2f) trigger=(%.2f,%.2f) finish=(%.2f,%.2f) targetLocal=(%.3f,%.3f) cell=%.2f user=%d pointer=%d down=%d dragStarted=%d dragAtTarget=%d targetHit=%d upHandled=%d"),
                Start.X, Start.Y, Trigger.X, Trigger.Y, Finish.X, Finish.Y, Local.X, Local.Y,
                Target->GetCellSize(), User, FSlateApplication::CursorPointerIndex,
                DownHandled, DragStarted, DragReachedTarget, TargetHit, UpHandled);
        }
        return Routed;
    }

    struct FState
    {
        FState()
        {
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            StartupMode = Settings->StartupMode; PanelMode = Settings->ControlPanelMode;
            DefaultGym = Settings->DefaultGymName; LastGym = Settings->LastGymName;
            RecentGyms = Settings->RecentGymNames;
            Settings->StartupMode = ECkGym_StartupMode::Cycler;
            Settings->ControlPanelMode = ECkGym_ControlPanel_Mode::Hidden;
        }
        ~FState() { Restore(); }
        void Restore()
        {
            if (Restored) { return; }
            if (const auto Window = OriginalWindow.Pin(); Window.IsValid()) { Window->Resize(OriginalWindowSize); }
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = StartupMode; Settings->ControlPanelMode = PanelMode;
            Settings->DefaultGymName = DefaultGym; Settings->LastGymName = LastGym;
            Settings->RecentGymNames = RecentGyms; Settings->SaveConfig(); Restored = true;
        }
        ECkGym_StartupMode StartupMode;
        ECkGym_ControlPanel_Mode PanelMode;
        FString DefaultGym, LastGym;
        TArray<FString> RecentGyms;
        TWeakPtr<SWindow> OriginalWindow;
        FVector2D OriginalWindowSize = FVector2D::ZeroVector;
        bool Restored = false;
    };

    auto Capture(UWorld* World, FState& State, const FString& Name, FIntPoint Desired) -> bool
    {
        auto* Client = IsValid(World) ? World->GetGameViewport() : nullptr;
        const auto Target = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid()) { return false; }
        if (!State.OriginalWindow.IsValid())
        { State.OriginalWindow = Window; State.OriginalWindowSize = Window->GetClientSizeInScreen(); }
        IConsoleVariable* Watermark = IConsoleManager::Get().FindConsoleVariable(TEXT("ck.UI.WatermarkDisplayPolicy"));
        if (Watermark == nullptr) { return false; }
        const int32 PreviousWatermarkPolicy = Watermark->GetInt();
        Watermark->Set(0, ECVF_SetByCode);
        ON_SCOPE_EXIT { Watermark->Set(PreviousWatermarkPolicy, ECVF_SetByCode); };
        TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
        for (int32 Attempt = 0; Attempt < 5; ++Attempt)
        {
            Slate.PumpMessages(); Slate.Tick(); Slate.Tick(); Pixels.Reset();
            if (!Slate.TakeScreenshot(Target.ToSharedRef(), Pixels, Size) || Size.X <= 0 || Size.Y <= 0
                || Pixels.Num() < Size.X * Size.Y) { return false; }
            if (Size.X == Desired.X && Size.Y == Desired.Y) { break; }
            Window->Resize(Window->GetClientSizeInScreen() + FVector2D(Desired.X - Size.X, Desired.Y - Size.Y));
        }
        if (Size.X != Desired.X || Size.Y != Desired.Y) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/TacticalInventoryProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Name + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }
    auto CaptureHud(UWorld* World, const FString& Name, FIntPoint Desired) -> bool
    {
        auto* Widget = Hud(World);
        if (!IsValid(Widget)) { return false; }
        IConsoleVariable* Watermark = IConsoleManager::Get().FindConsoleVariable(TEXT("ck.UI.WatermarkDisplayPolicy"));
        if (Watermark == nullptr) { return false; }
        const int32 PreviousWatermarkPolicy = Watermark->GetInt();
        Watermark->Set(0, ECVF_SetByCode);
        ON_SCOPE_EXIT { Watermark->Set(PreviousWatermarkPolicy, ECVF_SetByCode); };
        auto& Slate = FSlateApplication::Get();
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
        if (!Slate.TakeScreenshot(Widget->TakeWidget(), Pixels, Size)
            || Size.X != Desired.X || Size.Y != Desired.Y
            || Pixels.Num() < Size.X * Size.Y) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/TacticalInventoryProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Name + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTacticalInventory_NativeGrid,
    "Ck.UiAuthoring.TacticalInventory.NativeGrid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkTacticalInventory_NativeGrid::RunTest(const FString&) -> bool
{
    using namespace ck_tactical_inventory_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Native grid proof requires Slate.")); return false; }
    TStrongObjectPtr<UTexture2D> Atlas{UTexture2D::CreateTransient(400, 300)};
    FCk_UI_AuthoredImage Image{TEXT("Atlas")}; Image.Set_Texture(Atlas.Get());
    FCk_UI_AuthoredSession_Config Config;
    Config.Set_Collections({Collection()}); Config.Set_Images({Image});
    Config.Set_Text({FCk_UI_AuthoredText{TEXT("SelectedItemKey"), FText::FromString(TEXT("a"))}});
    Config.Set_ItemActions({TEXT("SelectItem"), TEXT("MoveItem")});
    Config.Set_RegisterItemControls(true);
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Owner{NewObject<UCk_UI_AuthoredSession_UE>()};
    TArray<FString> Errors;
    if (!TestTrue(TEXT("Native grid collection initializes through authored session"),
        Owner->TryInitialize(Config, Errors))) { return false; }
    const FString Markup = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
        "<inventory-grid id=\"grid\" items-bind=\"Items\" image-bind=\"Atlas\" columns=\"4\" rows=\"3\" "
        "cell-size=\"32\" atlas-columns=\"4\" atlas-rows=\"3\" select=\"SelectItem\" move=\"MoveItem\" "
        "selected-key-bind=\"SelectedItemKey\"/>"
        "</column></region></ui>");
    if (!TestTrue(TEXT("Native grid factory accepts a valid authored document"),
        Owner->TryReload(Markup, TEXT(""), Errors)))
    { Owner->Request_Release(); return false; }
    const auto View = Owner->Get_View();
    const auto Grid = FindTagged<SCkUiInventoryGrid>(View->GetRegion(TEXT("main")),
        TEXT("SCkUiInventoryGrid"), TEXT("grid"));
    if (!TestTrue(TEXT("Authoring realizes the native grid"), Grid.IsValid()))
    { Owner->Request_Release(); return false; }
    TestTrue(TEXT("Grid dimensions and item count come from the live source"),
        Grid->GetColumns() == 4 && Grid->GetRows() == 3 && Grid->GetCellSize() == 32.0f
        && Grid->GetItemCount() == 2);
    const auto A = Grid->GetItemBounds(TEXT("a"));
    const auto B = Grid->GetItemBounds(TEXT("b"));
    TestTrue(TEXT("Rectangular items occupy exact cell bounds"),
        A.IsSet() && B.IsSet() && A.GetValue().Left == 0.0f && A.GetValue().Top == 0.0f
        && A.GetValue().Right == 64.0f && A.GetValue().Bottom == 32.0f
        && B.GetValue().Left == 96.0f && B.GetValue().Top == 32.0f
        && B.GetValue().Right == 128.0f && B.GetValue().Bottom == 96.0f);
    TestTrue(TEXT("Placement rejects overlap, edge overflow and rotated overflow"),
        !Grid->CanPlace(TEXT("a"), 2, 1, false)
        && !Grid->CanPlace(TEXT("a"), 3, 0, false)
        && !Grid->CanPlace(TEXT("b"), 3, 2, true));
    TestTrue(TEXT("Legal proposal does not directly mutate the model"),
        Grid->CanPlace(TEXT("a"), 0, 2, false) && Grid->TryMove(TEXT("a"), 0, 2, false)
        && Grid->GetItemBounds(TEXT("a")).IsSet()
        && Grid->GetItemBounds(TEXT("a")).GetValue().Top == 0.0f);
    const int64 Revision = Grid->GetAcceptedRevision();
    TestTrue(TEXT("Extreme coordinates reject without changing accepted geometry"),
        !Grid->CanPlace(TEXT("a"), MAX_int32, 0, false)
        && !Grid->CanPlace(TEXT("a"), 0, MAX_int32, true)
        && !Grid->TryMove(TEXT("a"), MAX_int32, 0, false)
        && Grid->GetAcceptedRevision() == Revision
        && Grid->GetItemBounds(TEXT("a")).IsSet()
        && Grid->GetItemBounds(TEXT("a")).GetValue().Top == 0.0f);
    TArray<FCk_UI_AuthoredRecord> Initial;
    TestTrue(TEXT("Fixture collection remains readable"), Owner->TryGetRecords(TEXT("Items"), Initial));
    for (const auto& Invalid : {Record(TEXT("bad-overlap"), 1, 0, 2, 1),
        Record(TEXT("bad-bounds"), 4, 0, 1, 1), Record(TEXT("bad-fraction"), 1.5f, 0, 1, 1)})
    {
        auto Candidate = Initial; Candidate.Add(Invalid); Errors.Reset();
        Owner->TrySetRecords(TEXT("Items"), Candidate, Errors);
        TestTrue(TEXT("Invalid replacement retains the complete last accepted layout"),
            Grid->GetAcceptedRevision() == Revision && Grid->GetItemCount() == 2
            && Grid->GetItemBounds(TEXT("a")).IsSet() && Grid->GetItemBounds(TEXT("b")).IsSet()
            && !Grid->GetItemBounds(Invalid.Get_Key()).IsSet());
        TestFalse(TEXT("Rejected collection revision cannot accept a move proposal"),
            Grid->TryMove(TEXT("a"), 0, 2, false));
    }
    Errors.Reset();
    TestTrue(TEXT("Valid replacement restores the active native grid"),
        Owner->TrySetRecords(TEXT("Items"), Initial, Errors)
        && Grid->CanPlace(TEXT("a"), 0, 2, false));
    const int64 RestoredRevision = Grid->GetAcceptedRevision();
    auto& Slate = FSlateApplication::Get();
    const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(160, 128))
        .CreateTitleBar(false)[View->GetRegion(TEXT("main"))];
    Slate.AddWindow(Window, true);
    ON_SCOPE_EXIT { Slate.DestroyWindowImmediately(Window); };
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    const FGeometry Geometry = Grid->GetCachedGeometry();
    const FVector2D SourcePoint = Geometry.LocalToAbsolute(FVector2D(16.0f, 16.0f));
    const FVector2D DropPoint = Geometry.LocalToAbsolute(FVector2D(32.0f, 80.0f));
    const FPointerEvent SourceEvent{0, FSlateApplication::CursorPointerIndex,
        SourcePoint, SourcePoint, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton,
        0.0f, FModifierKeysState{}};
    const FPointerEvent DropEvent{0, FSlateApplication::CursorPointerIndex,
        DropPoint, SourcePoint, TSet<FKey>{}, EKeys::LeftMouseButton,
        0.0f, FModifierKeysState{}};
    const FReply Press = Grid->OnMouseButtonDown(Geometry, SourceEvent);
    const FReply DragReply = Press.IsEventHandled() ? Grid->OnDragDetected(Geometry, SourceEvent) : FReply::Unhandled();
    const auto Drag = DragReply.IsEventHandled()
        ? StaticCastSharedPtr<FCkUiInventoryGridDragOperation>(DragReply.GetDragDropContent()) : nullptr;
    if (TestTrue(TEXT("Native item starts a keyed drag from the real grid"),
        Drag.IsValid() && Drag->GetKey() == TEXT("a")))
    {
        const FDragDropEvent HoverEvent(DropEvent, Drag);
        Grid->OnDragEnter(Geometry, HoverEvent);
        const FKeyEvent RepeatedR(EKeys::R, FModifierKeysState{}, 0, true, 0, 0);
        TestTrue(TEXT("Repeated R is consumed without rotating a held drag"),
            Grid->OnKeyDown(Geometry, RepeatedR).IsEventHandled() && !Drag->GetRotated()
            && Grid->GetAcceptedRevision() == RestoredRevision);
        TestTrue(TEXT("Compatible document reload preserves native grid instance"),
            Owner->TryReload(Markup, TEXT(""), Errors)
            && FindTagged<SCkUiInventoryGrid>(View->GetRegion(TEXT("main")),
                TEXT("SCkUiInventoryGrid"), TEXT("grid")) == Grid);
        TestFalse(TEXT("Pre-reload drag is rejected without dispatch"),
            Grid->OnDrop(Geometry, FDragDropEvent(DropEvent, Drag)).IsEventHandled());
    }
    Owner->Request_Release();
    TestTrue(TEXT("Released grid rejects proposals and retains no active interaction"),
        View->IsOwnerReleased() && !Grid->TryMove(TEXT("a"), 0, 2, false)
        && (!Drag.IsValid() || !Grid->OnDrop(Geometry, FDragDropEvent(DropEvent, Drag)).IsEventHandled()));
    const auto CropAdmission = [this, &Config, &Markup](bool PartialSchema, bool InvertedBounds,
        bool ExpectSuccess, const TCHAR* Label)
    {
        auto CropConfig = Config;
        CropConfig.Set_Collections({CropCollection(PartialSchema, InvertedBounds)});
        TStrongObjectPtr<UCk_UI_AuthoredSession_UE> CropOwner{NewObject<UCk_UI_AuthoredSession_UE>()};
        TArray<FString> CropErrors;
        if (!TestTrue(TEXT("Crop fixture collection initializes"),
            CropOwner->TryInitialize(CropConfig, CropErrors))) { return; }
        const bool Accepted = CropOwner->TryReload(Markup, TEXT(""), CropErrors);
        TestEqual(Label, Accepted, ExpectSuccess);
        if (Accepted)
        {
            const auto CropView = CropOwner->Get_View();
            const auto CropGrid = FindTagged<SCkUiInventoryGrid>(CropView->GetRegion(TEXT("main")),
                TEXT("SCkUiInventoryGrid"), TEXT("grid"));
            TestTrue(TEXT("Valid crop reaches the registered native grid"),
                CropGrid.IsValid() && CropGrid->GetItemCount() == 2);
        }
        CropOwner->Request_Release();
    };
    CropAdmission(false, false, true, TEXT("Complete finite normalized icon crops admit natively"));
    CropAdmission(true, false, false, TEXT("Partial icon crop schema rejects native document"));
    CropAdmission(false, true, false, TEXT("Inverted icon crop rejects native document"));
    const bool CreatedUserOne = !Slate.GetUser(1).IsValid();
    if (CreatedUserOne)
    {
        const FKeyEvent Bootstrap(EKeys::F12, FModifierKeysState{}, 1u, false, 0, 0);
        Slate.ProcessKeyDownEvent(Bootstrap); Slate.ProcessKeyUpEvent(Bootstrap);
    }
    const auto UserOne = Slate.GetUser(1);
    if (!TestTrue(TEXT("Nonzero Slate user registers before owner-specific focus proof"), UserOne.IsValid()))
    { return !HasAnyErrors(); }
    const auto PriorUserOneFocus = Slate.GetUserFocusedWidget(1);
    ON_SCOPE_EXIT
    {
        if (Slate.GetUser(1) == UserOne)
        {
            if (PriorUserOneFocus.IsValid())
            { Slate.SetUserFocus(1, PriorUserOneFocus, EFocusCause::SetDirectly); }
            else { Slate.ClearUserFocus(1, EFocusCause::SetDirectly); }
            if (CreatedUserOne) { Slate.UnregisterUser(1); }
        }
    };
    TSharedPtr<FCkUiCollection> OwnedItems;
    const TArray<FCkUiFieldSchema> OwnedSchema = {
        {TEXT("name"), ECkUiFieldKind::Text}, {TEXT("kind"), ECkUiFieldKind::Text},
        {TEXT("x"), ECkUiFieldKind::Number}, {TEXT("y"), ECkUiFieldKind::Number},
        {TEXT("w"), ECkUiFieldKind::Number}, {TEXT("h"), ECkUiFieldKind::Number},
        {TEXT("icon"), ECkUiFieldKind::Number}, {TEXT("quantity"), ECkUiFieldKind::Number},
        {TEXT("rotated"), ECkUiFieldKind::Bool}, {TEXT("selected"), ECkUiFieldKind::Bool},
        {TEXT("enabled"), ECkUiFieldKind::Bool}};
    if (!TestTrue(TEXT("Owned native collection creates"),
        FCkUiCollection::TryCreate(OwnedSchema, OwnedItems).Succeeded)) { return !HasAnyErrors(); }
    FCkUiRecordData OwnedRecord; OwnedRecord.Key = TEXT("a");
    OwnedRecord.Fields.Add(TEXT("name"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text,
        .Text = FText::FromString(TEXT("Owned item"))});
    OwnedRecord.Fields.Add(TEXT("kind"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text,
        .Text = FText::FromString(TEXT("TEST"))});
    for (const auto& Number : {TPair<FString, float>{TEXT("x"), 0.0f},
        {TEXT("y"), 0.0f}, {TEXT("w"), 2.0f}, {TEXT("h"), 1.0f},
        {TEXT("icon"), 0.0f}, {TEXT("quantity"), 1.0f}})
    { OwnedRecord.Fields.Add(Number.Key, FCkUiFieldValue{.Kind = ECkUiFieldKind::Number,
        .Number = Number.Value}); }
    for (const TCHAR* Flag : {TEXT("rotated"), TEXT("selected"), TEXT("enabled")})
    { OwnedRecord.Fields.Add(Flag, FCkUiFieldValue{.Kind = ECkUiFieldKind::Bool,
        .Bool = FCString::Strcmp(Flag, TEXT("enabled")) == 0}); }
    if (!TestTrue(TEXT("Owned native item publishes"), OwnedItems->TrySetRecords({OwnedRecord}).Succeeded))
    { return !HasAnyErrors(); }
    FCkUiWidgetRegistry OwnedRegistry;
    if (!TestTrue(TEXT("Owned native grid registers"), RegisterInventoryGridUiControl(OwnedRegistry).Succeeded))
    { return !HasAnyErrors(); }
    FSlateBrush OwnedAtlas; OwnedAtlas.ImageSize = FVector2D(400, 300);
    TArray<FString> OwnedProposals;
    FCkUiView::FDataBindings OwnedData;
    OwnedData.SlateUserIndex = 1;
    OwnedData.CanDispatchEvents = TAttribute<bool>(true);
    OwnedData.Collections.Add(TEXT("Items"), OwnedItems);
    OwnedData.Images.Add(TEXT("Atlas"), TAttribute<const FSlateBrush*>::CreateLambda(
        [&OwnedAtlas]() -> const FSlateBrush* { return &OwnedAtlas; }));
    OwnedData.Text.Add(TEXT("SelectedItemKey"), FText::FromString(TEXT("a")));
    OwnedData.StringChanged.Add(TEXT("SelectItem"), FCkUiOnStringChanged::CreateLambda([](const FString&) {}));
    OwnedData.StringChanged.Add(TEXT("MoveItem"), FCkUiOnStringChanged::CreateLambda(
        [&OwnedProposals](const FString& Proposal) { OwnedProposals.Add(Proposal); }));
    const auto OwnedView = FCkUiView::Create({}, {}, {}, {}, MoveTemp(OwnedData), OwnedRegistry.CreateSnapshot());
    const auto OwnedRegion = OwnedView->GetRegion(TEXT("main"));
    const auto OwnedLoad = OwnedView->TryReload(Markup, TEXT(""), TEXT("OwnedInventoryGrid"));
    if (!TestTrue(TEXT("User-one authored grid mounts"), OwnedLoad.Succeeded))
    { AddError(FString::Join(OwnedLoad.Errors, TEXT("; "))); return !HasAnyErrors(); }
    const auto OwnedGrid = FindTagged<SCkUiInventoryGrid>(OwnedRegion,
        TEXT("SCkUiInventoryGrid"), TEXT("grid"));
    if (!TestTrue(TEXT("User-one native grid exists"), OwnedGrid.IsValid())) { return !HasAnyErrors(); }
    const auto PriorUserZeroFocus = Slate.GetUserFocusedWidget(0);
    const TSharedRef<SWindow> OwnedWindow = SNew(SWindow).ClientSize(FVector2D(160, 128))
        .CreateTitleBar(false)[OwnedRegion];
    Slate.AddWindow(OwnedWindow, true);
    ON_SCOPE_EXIT
    {
        Slate.DestroyWindowImmediately(OwnedWindow);
        Slate.SetUserFocus(0, PriorUserZeroFocus, EFocusCause::SetDirectly);
    };
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    const FGeometry OwnedGeometry = OwnedGrid->GetCachedGeometry();
    const FVector2D OwnedStart = OwnedGeometry.LocalToAbsolute(FVector2D(16, 16));
    const FVector2D OwnedDrop = OwnedGeometry.LocalToAbsolute(FVector2D(32, 80));
    const FPointerEvent OwnedPress{1, FSlateApplication::CursorPointerIndex,
        OwnedStart, OwnedStart, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton,
        0.0f, FModifierKeysState{}};
    const FReply OwnedDown = OwnedGrid->OnMouseButtonDown(OwnedGeometry, OwnedPress);
    const FReply OwnedDragReply = OwnedDown.IsEventHandled()
        ? OwnedGrid->OnDragDetected(OwnedGeometry, OwnedPress) : FReply::Unhandled();
    const auto OwnedDrag = OwnedDragReply.IsEventHandled()
        ? StaticCastSharedPtr<FCkUiInventoryGridDragOperation>(OwnedDragReply.GetDragDropContent()) : nullptr;
    if (TestTrue(TEXT("Registered owner one begins a native grid drag"), OwnedDrag.IsValid()))
    {
        const FPointerEvent ForeignDrop{0, FSlateApplication::CursorPointerIndex,
            OwnedDrop, OwnedStart, TSet<FKey>{}, EKeys::LeftMouseButton,
            0.0f, FModifierKeysState{}};
        const auto BeforeForeignFocus = Slate.GetUserFocusedWidget(0);
        OwnedGrid->OnDragEnter(OwnedGeometry, FDragDropEvent(ForeignDrop, OwnedDrag));
        TestTrue(TEXT("Foreign user drag entry leaves focus and model untouched"),
            Slate.GetUserFocusedWidget(0) == BeforeForeignFocus && OwnedProposals.IsEmpty());
        TestFalse(TEXT("Foreign user cannot drop owner-one drag"),
            OwnedGrid->OnDrop(OwnedGeometry, FDragDropEvent(ForeignDrop, OwnedDrag)).IsEventHandled());
        TestTrue(TEXT("Foreign drop emits no proposal"), OwnedProposals.IsEmpty());
        const FPointerEvent OwnerDrop{1, FSlateApplication::CursorPointerIndex,
            OwnedDrop, OwnedStart, TSet<FKey>{}, EKeys::LeftMouseButton,
            0.0f, FModifierKeysState{}};
        OwnedGrid->OnDragEnter(OwnedGeometry, FDragDropEvent(OwnerDrop, OwnedDrag));
        TestTrue(TEXT("Valid held drag focuses registered nonzero owner"),
            Slate.GetUserFocusedWidget(1) == OwnedGrid);
        TestTrue(TEXT("Owner-one compatible reload keeps grid"),
            OwnedView->TryReload(Markup, TEXT(""), TEXT("OwnedInventoryGridReload")).Succeeded
            && FindTagged<SCkUiInventoryGrid>(OwnedRegion,
                TEXT("SCkUiInventoryGrid"), TEXT("grid")) == OwnedGrid);
        Slate.ClearUserFocus(1, EFocusCause::SetDirectly);
        const auto BeforeStaleFocus = Slate.GetUserFocusedWidget(1);
        OwnedGrid->OnDragEnter(OwnedGrid->GetCachedGeometry(), FDragDropEvent(OwnerDrop, OwnedDrag));
        TestTrue(TEXT("Stale held drag cannot refocus owner after reload"),
            Slate.GetUserFocusedWidget(1) == BeforeStaleFocus && OwnedProposals.IsEmpty());
    }
    OwnedView->ReleaseOwnerInteractions();
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTacticalInventory_AtlasCrop,
    "Ck.UiAuthoring.TacticalInventory.AtlasCrop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkTacticalInventory_AtlasCrop::RunTest(const FString&) -> bool
{
    using namespace ck_tactical_inventory_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Atlas crop proof requires Slate.")); return false; }
    TStrongObjectPtr<UTexture2D> Texture{UTexture2D::CreateTransient(400, 300)};
    if (!TestNotNull(TEXT("Crop fixture texture exists"), Texture.Get())) { return false; }
    auto* Platform = Texture->GetPlatformData();
    if (!TestTrue(TEXT("Crop fixture has a writable mip"), Platform != nullptr && Platform->Mips.Num() == 1))
    { return false; }
    auto* Pixels = static_cast<FColor*>(Platform->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    if (!TestNotNull(TEXT("Crop fixture mip pixels are writable"), Pixels)) { return false; }
    for (int32 Y = 0; Y < 300; ++Y)
    {
        for (int32 X = 0; X < 400; ++X)
        {
            FColor Color(16, 20, 24, 255);
            if (Y >= 100 && Y < 200 && X >= 100 && X < 200)
            { Color = X < 150 ? FColor(220, 28, 26, 255) : FColor(28, 42, 220, 255); }
            if (Y >= 100 && Y < 200 && X >= 200 && X < 300)
            { Color = X < 260 ? FColor(32, 200, 52, 255) : FColor(224, 206, 28, 255); }
            Pixels[Y * 400 + X] = Color;
        }
    }
    Platform->Mips[0].BulkData.Unlock(); Texture->UpdateResource();
    FCk_UI_AuthoredImage Atlas{TEXT("atlas")}; Atlas.Set_Texture(Texture.Get());
    FCk_UI_AuthoredSession_Config Config;
    Config.Set_Images({Atlas});
    Config.Set_Number({FCk_UI_AuthoredNumber{TEXT("cell"), 5.0f},
        FCk_UI_AuthoredNumber{TEXT("left"), 0.0f}, FCk_UI_AuthoredNumber{TEXT("top"), 0.0f},
        FCk_UI_AuthoredNumber{TEXT("right"), 0.4f}, FCk_UI_AuthoredNumber{TEXT("bottom"), 1.0f}});
    Config.Set_RegisterItemControls(true);
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Owner{NewObject<UCk_UI_AuthoredSession_UE>()};
    TArray<FString> Errors;
    if (!TestTrue(TEXT("Crop fixture initializes through authored session"), Owner->TryInitialize(Config, Errors)))
    { return false; }
    const FString Markup = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
        "<atlas-image id=\"cropped\" image-bind=\"atlas\" index-bind=\"cell\" columns=\"4\" rows=\"3\" "
        "crop-left-bind=\"left\" crop-top-bind=\"top\" crop-right-bind=\"right\" crop-bottom-bind=\"bottom\" "
        "aspect-fit=\"true\"/>"
        "</column></region></ui>");
    if (!TestTrue(TEXT("Complete normalized crop and aspect fit admit through the registered factory"),
        Owner->TryReload(Markup, TEXT(""), Errors)))
    { Owner->Request_Release(); return false; }
    const auto View = Owner->Get_View();
    const auto Root = View->GetRegion(TEXT("main"));
    const auto Scale = FindTagged<SScaleBox>(Root, TEXT("SScaleBox"), TEXT("cropped"));
    const auto Image = FindTagged<SImage>(Root, TEXT("SImage"), TEXT("cropped"));
    if (!TestTrue(TEXT("Aspect-fit factory retains tagged ScaleBox and child image"),
        Scale.IsValid() && Image.IsValid()))
    { Owner->Request_Release(); return false; }
    TestTrue(TEXT("Initial crop gives a 40 by 100 intrinsic painted image"),
        ImageSize(Image).Equals(FVector2D(40, 100), 0.001f));
    Scale->SlatePrepass(1.0f);
    TestTrue(TEXT("Native aspect-fit root follows the cropped image ratio"),
        Scale->GetDesiredSize().Equals(FVector2D(40, 100), 0.001f));
    auto& Slate = FSlateApplication::Get();
    const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(180, 180))
        .CreateTitleBar(false)[Root];
    Slate.AddWindow(Window, true);
    ON_SCOPE_EXIT { Slate.DestroyWindowImmediately(Window); };
    const auto Sample = [this, &Slate, &Image](bool ExpectYellow, const TCHAR* Label)
    {
        if (GUsingNullRHI) { return; }
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        TArray<FColor> Shot; FIntVector Size = FIntVector::ZeroValue;
        if (!TestTrue(TEXT("Rendered crop screenshot is available"),
            Slate.TakeScreenshot(Image.ToSharedRef(), Shot, Size)
            && Size.X > 10 && Size.Y > 10 && Shot.Num() >= Size.X * Size.Y)) { return; }
        const FColor Center = Shot[(Size.Y / 2) * Size.X + Size.X / 2];
        TestTrue(Label, ExpectYellow
            ? Center.R > 145 && Center.G > 135 && Center.B < 105
            : Center.R > 145 && Center.G < 105 && Center.B < 105);
    };
    Sample(false, TEXT("Rendered first crop samples red pixels from atlas cell five"));
    const int64 BeforeReload = Owner->Get_Revision();
    TestTrue(TEXT("Same-mode crop reload retains ScaleBox and image"),
        Owner->TryReload(Markup, TEXT(""), Errors) && Owner->Get_Revision() > BeforeReload
        && FindTagged<SScaleBox>(View->GetRegion(TEXT("main")), TEXT("SScaleBox"), TEXT("cropped")) == Scale
        && FindTagged<SImage>(View->GetRegion(TEXT("main")), TEXT("SImage"), TEXT("cropped")) == Image);
    const int64 Revision = Owner->Get_Revision();
    TestFalse(TEXT("Partial crop binding rejects the entire reload"), Owner->TryReload(
        Markup.Replace(TEXT(" crop-bottom-bind=\"bottom\""), TEXT("")), TEXT(""), Errors));
    TestFalse(TEXT("Changing retained aspect-fit mode rejects the entire reload"), Owner->TryReload(
        Markup.Replace(TEXT("aspect-fit=\"true\""), TEXT("aspect-fit=\"false\"")), TEXT(""), Errors));
    TestTrue(TEXT("Rejected reloads preserve prior root and revision"), Owner->Get_Revision() == Revision
        && FindTagged<SScaleBox>(View->GetRegion(TEXT("main")), TEXT("SScaleBox"), TEXT("cropped")) == Scale);
    TestTrue(TEXT("Inverted crop number publishes for admission check"), Owner->TrySetNumber(TEXT("left"), 0.95f));
    TestFalse(TEXT("Inverted dynamic crop rejects native reload admission"),
        Owner->TryReload(Markup, TEXT(""), Errors));
    TestEqual(TEXT("Inverted crop rejection preserves revision"), Owner->Get_Revision(), Revision);
    TestTrue(TEXT("Restore ordered crop and switch selected atlas cell"),
        Owner->TrySetNumber(TEXT("left"), 0.6f)
        && Owner->TrySetNumber(TEXT("top"), 0.2f)
        && Owner->TrySetNumber(TEXT("right"), 1.0f)
        && Owner->TrySetNumber(TEXT("bottom"), 0.8f)
        && Owner->TrySetNumber(TEXT("cell"), 6.0f));
    TestTrue(TEXT("Updated crop changes native image aspect"),
        ImageSize(Image).Equals(FVector2D(40, 60), 0.001f));
    Scale->SlatePrepass(1.0f);
    TestTrue(TEXT("Retained ScaleBox tracks changed crop size"),
        Scale->GetDesiredSize().Equals(FVector2D(40, 60), 0.001f));
    Sample(true, TEXT("Rendered changed crop samples yellow pixels from atlas cell six"));
    Owner->Request_Release();
    TestTrue(TEXT("Released retained root no longer paints an atlas image"),
        View->IsOwnerReleased() && ImageSize(Image) == FVector2D::ZeroVector);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTacticalInventory_Pie,
    "Ck.UiAuthoring.TacticalInventory.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkTacticalInventory_Pie::RunTest(const FString&) -> bool
{
    using namespace ck_tactical_inventory_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Inventory gym proof requires Slate.")); return false; }
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* R = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: Tactical Inventory")) != INDEX_NONE;
        }), 15.0, TEXT("Tactical inventory production gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("Gym registry exists"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: Tactical Inventory"));
            if (!TestTrue(TEXT("Registered tactical inventory gym entry exists"), R->Get_Entries().IsValidIndex(Index)))
            { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("Inventory gym category"), Entry.Category, FString(TEXT("CkUI")));
            TestTrue(TEXT("Inventory gym selects the script game mode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_TacticalInventory_GameMode")));
            R->Request_TravelToGym(Index);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto* Owner = Session(World);
            return IsValid(Hud(World)) && ((IsValid(Owner) && Owner->Get_IsUsable() && Owner->Get_Revision() > 0)
                || !Getter<FStrProperty, FString>(Hud(World), TEXT("Get_StartupError"), {}).IsEmpty());
        }), 30.0, TEXT("Production tactical inventory mounts or reports startup failure")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Hud(World); auto* Owner = Session(World);
            const FString StartupError = Getter<FStrProperty, FString>(Widget, TEXT("Get_StartupError"), {});
            if (!StartupError.IsEmpty())
            { AddError(FString::Printf(TEXT("Tactical inventory startup: %s"), *StartupError)); return; }
            if (!TestNotNull(TEXT("Production inventory HUD exists"), Widget)
                || !TestNotNull(TEXT("Production authored session exists"), Owner)) { return; }
            const auto View = Owner->Get_View();
            if (!TestTrue(TEXT("Production inventory authored view commits"), View.IsValid())) { return; }
            auto Stash = Grid(World, TEXT("stash-grid"));
            auto Pack = Grid(World, TEXT("pack-grid"));
            auto Case = Grid(World, TEXT("case-grid"));
            if (!TestTrue(TEXT("Three native inventory grids mount"),
                Stash.IsValid() && Pack.IsValid() && Case.IsValid())) { return; }
            TestTrue(TEXT("Grid capacities match model containers"),
                Stash->GetColumns() == 10 && Stash->GetRows() == 14
                && Pack->GetColumns() == 6 && Pack->GetRows() == 6
                && Case->GetColumns() == 4 && Case->GetRows() == 4);
            TestTrue(TEXT("Starting model contains the intended rectangular items"),
                Container(Widget, TEXT("m4a1")) == TEXT("Stash") && X(Widget, TEXT("m4a1")) == 0
                && Y(Widget, TEXT("m4a1")) == 0 && !Rotated(Widget, TEXT("m4a1"))
                && Container(Widget, TEXT("pistol")) == TEXT("Pack")
                && Container(Widget, TEXT("keycard")) == TEXT("Case")
                && Stash->GetItemBounds(TEXT("m4a1")).IsSet());
            TestEqual(TEXT("Initial selection is the M4A1"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("m4a1")));
            const int32 InitialMutations = Mutations(Widget);
            TestTrue(TEXT("Initial model exposes a valid mutation count"), InitialMutations >= 0);
            if (!GUsingNullRHI)
            {
                for (const auto Size : {FIntPoint{1280, 720}, FIntPoint{1920, 1080}, FIntPoint{2400, 1350}})
                {
                    TestTrue(TEXT("Production tactical inventory native screenshot"), Capture(World, *State,
                        FString::Printf(TEXT("inventory-%dx%d"), Size.X, Size.Y), Size));
                    if (Size == FIntPoint{1920, 1080})
                    {
                        TestTrue(TEXT("Clean production HUD screenshot at 1920x1080"),
                            CaptureHud(World, TEXT("inventory-clean-1920x1080"), Size));
                    }
                    TestTrue(TEXT("All three native grids have finite viewport geometry"),
                        Stash->GetCachedGeometry().GetLocalSize().X > 0.0f
                        && Pack->GetCachedGeometry().GetLocalSize().X > 0.0f
                        && Case->GetCachedGeometry().GetLocalSize().X > 0.0f);
                    TestTrue(*FString::Printf(TEXT("Shell, panels, grids and inspection text fit at %dx%d"),
                        Size.X, Size.Y), LayoutFits(World, View->GetRegion(TEXT("main"))));
                }
            }
            const int32 User = Owner->Get_SlateUserIndex();
            TestTrue(TEXT("A real click on the stash item selects the script key"),
                ClickGridCell(Stash, 4.5f, 4.5f, User)
                && Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}) == TEXT("water"));
            TestTrue(TEXT("A routed grid click restores selection to the rifle"),
                ClickGridCell(Stash, 0.5f, 0.5f, User)
                && Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}) == TEXT("m4a1"));
            const bool DragRouted = DragGridCell(Stash, Pack, 0.5f, 0.5f, 3.25f, 5.25f, User);
            const FString DragContainer = Container(Widget, TEXT("m4a1"));
            const int32 DragX = X(Widget, TEXT("m4a1"));
            const int32 DragY = Y(Widget, TEXT("m4a1"));
            const int32 DragMutations = Mutations(Widget);
            if (!DragRouted || DragContainer != TEXT("Pack") || DragX != 1 || DragY != 4
                || DragMutations != InitialMutations + 1)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("TacticalInventory physical drag result: routed=%d container=%s x=%d y=%d mutations=%d expected=%d; stashRevision=%lld packRevision=%lld"),
                    DragRouted, *DragContainer, DragX, DragY, DragMutations, InitialMutations + 1,
                    Stash->GetAcceptedRevision(), Pack->GetAcceptedRevision());
            }
            TestTrue(TEXT("Physical cross-grid drag moves the rifle into the pack"),
                DragRouted && DragContainer == TEXT("Pack") && DragX == 1 && DragY == 4
                && DragMutations == InitialMutations + 1);
            TestTrue(TEXT("Fixture reset restores the complete starting placement"),
                Invoke(Widget, TEXT("Request_ResetFixture"))
                && Container(Widget, TEXT("m4a1")) == TEXT("Stash")
                && X(Widget, TEXT("m4a1")) == 0 && Y(Widget, TEXT("m4a1")) == 0);
            const int32 BeforeReject = Mutations(Widget);
            const FGeometry StashGeometry = Stash->GetCachedGeometry();
            const FVector2D DragStart = GridPoint(Stash, 0.5f, 0.5f);
            const FVector2D ForeignDrop = GridPoint(Pack, 3.0f, 5.0f);
            const FPointerEvent OwnerPress{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
                DragStart, DragStart, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton,
                0.0f, FModifierKeysState{}};
            const FReply DirectPress = Stash->OnMouseButtonDown(StashGeometry, OwnerPress);
            const FReply DirectDrag = DirectPress.IsEventHandled()
                ? Stash->OnDragDetected(StashGeometry, OwnerPress) : FReply::Unhandled();
            const auto ForeignDrag = DirectDrag.IsEventHandled()
                ? StaticCastSharedPtr<FCkUiInventoryGridDragOperation>(DirectDrag.GetDragDropContent()) : nullptr;
            if (TestTrue(TEXT("Owner can begin a native cross-grid drag"), ForeignDrag.IsValid()))
            {
                const FPointerEvent ForeignPointer{static_cast<uint32>(User + 1),
                    FSlateApplication::CursorPointerIndex, ForeignDrop, DragStart,
                    TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
                TestFalse(TEXT("Foreign Slate user cannot drop an owner's drag"),
                    Pack->OnDrop(Pack->GetCachedGeometry(), FDragDropEvent(ForeignPointer, ForeignDrag))
                        .IsEventHandled());
                TestTrue(TEXT("Foreign drop preserves item and mutation count"),
                    Container(Widget, TEXT("m4a1")) == TEXT("Stash")
                    && X(Widget, TEXT("m4a1")) == 0 && Mutations(Widget) == BeforeReject);
            }
            TestFalse(TEXT("Native extreme coordinate proposal cannot reach the script model"),
                Stash->TryMove(TEXT("m4a1"), MAX_int32, 0, false));
            TestEqual(TEXT("Extreme coordinate leaves mutation count unchanged"), Mutations(Widget), BeforeReject);
            TestFalse(TEXT("Overlap with the SV-98 rejects the entire move"),
                Invoke(Widget, TEXT("Request_TryMove"), {TEXT("m4a1"), TEXT("Stash")}, {4, 0}, {false}));
            TestFalse(TEXT("Out-of-bounds footprint rejects the entire move"),
                Invoke(Widget, TEXT("Request_TryMove"), {TEXT("m4a1"), TEXT("Stash")}, {9, 13}, {false}));
            TestFalse(TEXT("Document case cannot move inside itself"),
                Invoke(Widget, TEXT("Request_TryMove"), {TEXT("docs"), TEXT("Case")}, {0, 2}, {false}));
            TestTrue(TEXT("Rejected moves preserve placement and mutation count"),
                Mutations(Widget) == BeforeReject && Container(Widget, TEXT("m4a1")) == TEXT("Stash")
                && X(Widget, TEXT("m4a1")) == 0 && Y(Widget, TEXT("m4a1")) == 0
                && Container(Widget, TEXT("docs")) == TEXT("Stash"));
            TestTrue(TEXT("Rotating selected rifle changes its actual footprint"),
                Invoke(Widget, TEXT("Request_RotateSelected"))
                && Rotated(Widget, TEXT("m4a1")) && Y(Widget, TEXT("m4a1")) == 0
                && Stash->GetItemBounds(TEXT("m4a1")).IsSet()
                && Stash->GetItemBounds(TEXT("m4a1")).GetValue().Right
                    - Stash->GetItemBounds(TEXT("m4a1")).GetValue().Left == Stash->GetCellSize() * 2.0f
                && Mutations(Widget) == BeforeReject + 1);
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Rotated rifle screenshot"),
                Capture(World, *State, TEXT("inventory-rotated-1920x1080"), {1920, 1080})); }
            TestTrue(TEXT("Native move proposal is model-owned and remains atomic"),
                Stash->TryMove(TEXT("m4a1"), 4, 8, true)
                && X(Widget, TEXT("m4a1")) == 4 && Y(Widget, TEXT("m4a1")) == 8);
            TestTrue(TEXT("Fixture reset restores unrotated quick-transfer input"),
                Invoke(Widget, TEXT("Request_ResetFixture")) && !Rotated(Widget, TEXT("m4a1")));
            const int32 BeforeTransfer = Mutations(Widget);
            TestTrue(TEXT("Quick transfer moves the selected rifle into a real pack opening"),
                Invoke(Widget, TEXT("Request_QuickTransfer"))
                && Container(Widget, TEXT("m4a1")) == TEXT("Pack")
                && Mutations(Widget) == BeforeTransfer + 1);
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Post-transfer pack screenshot"),
                Capture(World, *State, TEXT("inventory-post-transfer-1920x1080"), {1920, 1080})); }
            TestTrue(TEXT("Selecting case and toggling its visibility updates the model"),
                Invoke(Widget, TEXT("Request_CloseCase"))
                && Invoke(Widget, TEXT("Request_Select"), {TEXT("docs")})
                && Invoke(Widget, TEXT("Request_OpenCase")));
            if (!GUsingNullRHI)
            { TestTrue(TEXT("Selected document case screenshot"),
                Capture(World, *State, TEXT("inventory-selected-case-1920x1080"), {1920, 1080})); }
            const int64 BeforeReload = Owner->Get_Revision();
            TestTrue(TEXT("Production document reloads without replacing the mounted grids"),
                Invoke(Widget, TEXT("Request_Reload")) && Owner->Get_Revision() > BeforeReload
                && Grid(World, TEXT("stash-grid")) == Stash);
            TestTrue(TEXT("Hiding inventory ends authored input"),
                Invoke(Widget, TEXT("Request_Show"), {}, {}, {false}) && !Owner->Get_IsInputActive());
            const int32 BeforeHidden = Mutations(Widget);
            TestFalse(TEXT("Hidden inventory rejects native move proposals"), Stash->TryMove(TEXT("docs"), 8, 4, false));
            TestEqual(TEXT("Hidden proposal leaves model unchanged"), Mutations(Widget), BeforeHidden);
            TestTrue(TEXT("Reopening restores authored input"),
                Invoke(Widget, TEXT("Request_Show"), {}, {}, {true}) && Owner->Get_IsInputActive());
            Owner->Request_Release();
            TestTrue(TEXT("Released owner rejects retained native move proposals"),
                View->IsOwnerReleased() && !Stash->TryMove(TEXT("docs"), 8, 4, false)
                && Mutations(Widget) == BeforeHidden);
            State->Restore();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

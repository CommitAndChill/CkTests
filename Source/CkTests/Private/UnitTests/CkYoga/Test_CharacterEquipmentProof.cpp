#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkGym_Registry.h"
#include "CkGym_StartupSettings.h"
#include "CkSlateLayout/CkUiAtlasImage.h"
#include "CkSlateLayout/CkUiItemSlot.h"
#include "CkSlateLayout/SCkUiRepeat.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Layout/SlateRect.h"
#include "Layout/WidgetPath.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "Types/SlateAttributeMetaData.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SWindow.h"

#include <limits>

namespace ck_character_equipment_proof
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
    auto FindTagged(const TSharedRef<SWidget>& Root, const FString& Type, const FName Tag) -> TSharedPtr<TWidget>
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

    auto FindTag(const TSharedRef<SWidget>& Root, const FName Tag) -> TSharedPtr<SWidget>
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

    auto Invoke(UObject* Object, const FName Name, const TArray<FString>& Strings = {},
        const TArray<bool>& Bools = {}) -> bool
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        if (Function == nullptr || !CastField<FBoolProperty>(Function->GetReturnProperty())
            || Function->NumParms != Strings.Num() + Bools.Num() + 1) { return false; }
        FStructOnScope Params{Function};
        void* Memory = Params.GetStructMemory();
        int32 StringIndex = 0, BoolIndex = 0;
        bool Valid = true;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Parm) || Property->HasAnyPropertyFlags(CPF_ReturnParm)) { continue; }
            if (auto* Text = CastField<FStrProperty>(Property))
            {
                if (!Strings.IsValidIndex(StringIndex)) { Valid = false; break; }
                Text->SetPropertyValue_InContainer(Memory, Strings[StringIndex++]);
            }
            else if (auto* Flag = CastField<FBoolProperty>(Property))
            {
                if (!Bools.IsValidIndex(BoolIndex)) { Valid = false; break; }
                Flag->SetPropertyValue_InContainer(Memory, Bools[BoolIndex++]);
            }
            else { Valid = false; break; }
        }
        Valid = Valid && StringIndex == Strings.Num() && BoolIndex == Bools.Num();
        if (Valid)
        {
            UE_LOG(LogTemp, Display, TEXT("CharacterEquipment reflected request %s(%s)"),
                *Name.ToString(), *FString::Join(Strings, TEXT(", ")));
            Object->ProcessEvent(Function, Memory);
        }
        const bool Result = Valid && CastFieldChecked<FBoolProperty>(Function->GetReturnProperty())
            ->GetPropertyValue_InContainer(Memory);
        return Result;
    }

    template <typename TProperty, typename TValue>
    auto Getter(UObject* Object, const FName Name, const TValue Default) -> TValue
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        const auto* Return = Function != nullptr ? CastField<TProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return Default; }
        FStructOnScope Params{Function};
        Object->ProcessEvent(Function, Params.GetStructMemory());
        const TValue Value = Return->GetPropertyValue_InContainer(Params.GetStructMemory());
        return Value;
    }

    auto ObjectGetter(UObject* Object, const FName Name) -> UObject*
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        const auto* Return = Function != nullptr ? CastField<FObjectProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 1) { return nullptr; }
        FStructOnScope Params{Function};
        Object->ProcessEvent(Function, Params.GetStructMemory());
        UObject* Value = Return->GetObjectPropertyValue_InContainer(Params.GetStructMemory());
        return IsValid(Value) ? Value : nullptr;
    }

    auto StringGetter(UObject* Object, const FName Name, const FString& Argument) -> FString
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        auto* Return = Function != nullptr ? CastField<FStrProperty>(Function->GetReturnProperty()) : nullptr;
        if (Return == nullptr || Function->NumParms != 2) { return {}; }
        FStructOnScope Params{Function};
        int32 Count = 0;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Property = *It;
            if (Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                if (auto* Text = CastField<FStrProperty>(Property))
                { Text->SetPropertyValue_InContainer(Params.GetStructMemory(), Argument); ++Count; }
            }
        }
        if (Count == 1) { Object->ProcessEvent(Function, Params.GetStructMemory()); }
        const FString Value = Count == 1 ? Return->GetPropertyValue_InContainer(Params.GetStructMemory()) : FString{};
        return Value;
    }

    auto Hud(UWorld* World) -> UUserWidget*
    {
        auto* Player = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
        return Cast<UUserWidget>(ObjectGetter(Player, TEXT("Get_Hud")));
    }
    auto Session(UWorld* World) -> UCk_UI_AuthoredSession_UE*
    { return Cast<UCk_UI_AuthoredSession_UE>(ObjectGetter(Hud(World), TEXT("Get_Session"))); }
    auto Registry(UWorld* World) -> UCkGym_Registry_Subsystem*
    {
        auto* Instance = IsValid(World) ? World->GetGameInstance() : nullptr;
        return IsValid(Instance) ? Instance->GetSubsystem<UCkGym_Registry_Subsystem>() : nullptr;
    }
    auto Equipped(UObject* HudWidget, const FString& Slot) -> FString
    { return StringGetter(HudWidget, TEXT("Get_EquippedKey"), Slot); }
    auto Mutations(UObject* HudWidget) -> int32
    { return Getter<FIntProperty, int32>(HudWidget, TEXT("Get_MutationCount"), -1); }
    auto Text(UCk_UI_AuthoredSession_UE* Owner, const FString& Key) -> FString
    {
        FText Value;
        return IsValid(Owner) && Owner->TryGetText(Key, Value) ? Value.ToString() : FString{};
    }
    auto Flag(UCk_UI_AuthoredSession_UE* Owner, const FString& Key) -> bool
    {
        bool Value = false;
        return IsValid(Owner) && Owner->TryGetBool(Key, Value) && Value;
    }
    auto IsRenderedVisible(const TSharedPtr<SWidget>& Widget) -> bool
    {
        for (auto Current = Widget; Current.IsValid(); Current = Current->GetParentWidget())
        {
            if (Current->HasRegisteredSlateAttribute() && Current->IsAttributesUpdatesEnabled())
            {
                FSlateAttributeMetaData::UpdateOnlyVisibilityAttributes(*Current,
                    FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
            }
            const EVisibility Visibility = Current->GetVisibility();
            if (Visibility == EVisibility::Collapsed || Visibility == EVisibility::Hidden) { return false; }
        }
        return Widget.IsValid();
    }
    auto RecordText(UCk_UI_AuthoredSession_UE* Owner, const FString& RecordKey,
        const FString& FieldName) -> FString
    {
        TArray<FCk_UI_AuthoredRecord> Records;
        if (!IsValid(Owner) || !Owner->TryGetRecords(TEXT("Items"), Records)) { return {}; }
        for (const auto& Record : Records)
        {
            if (Record.Get_Key() != RecordKey) { continue; }
            for (const auto& Field : Record.Get_Fields())
            { if (Field.Get_Name() == FieldName && Field.Get_Kind() == ECk_UI_AuthoredFieldKind::Text)
                { return Field.Get_Text().ToString(); } }
        }
        return {};
    }
    auto Card(const TSharedPtr<FCkUiView>& View, const FString& Key) -> TSharedPtr<SCkUiItemSlot>
    {
        const auto Repeat = View.IsValid() ? View->GetRepeat(TEXT("items")) : nullptr;
        const auto Item = Repeat.IsValid() ? Repeat->GetItemWidget(Key) : nullptr;
        return Item.IsValid() ? FindTagged<SCkUiItemSlot>(Item.ToSharedRef(),
            TEXT("SCkUiItemSlot"), TEXT("card")) : nullptr;
    }
    auto GridFitsSixColumns(UCk_UI_AuthoredSession_UE* Owner) -> bool
    {
        const auto View = IsValid(Owner) ? Owner->Get_View() : nullptr;
        const auto Repeat = View.IsValid() ? View->GetRepeat(TEXT("items")) : nullptr;
        TArray<FCk_UI_AuthoredRecord> Records;
        if (!Repeat.IsValid() || !Owner->TryGetRecords(TEXT("Items"), Records)
            || Records.Num() != 24 || Repeat->GetItemCount() != 24) { return false; }
        const FSlateRect Grid = Repeat->GetCachedGeometry().GetRenderBoundingRect();
        if (!FMath::IsFinite(Grid.Left) || !FMath::IsFinite(Grid.Top)
            || Grid.Right <= Grid.Left || Grid.Bottom <= Grid.Top) { return false; }
        TArray<FSlateRect> Cells;
        for (const auto& Record : Records)
        {
            const auto Item = Repeat->GetItemWidget(Record.Get_Key());
            if (!Item.IsValid()) { return false; }
            const FSlateRect Cell = Item->GetCachedGeometry().GetRenderBoundingRect();
            if (!FMath::IsFinite(Cell.Left) || !FMath::IsFinite(Cell.Top)
                || !FMath::IsFinite(Cell.Right) || !FMath::IsFinite(Cell.Bottom)
                || Cell.Right <= Cell.Left || Cell.Bottom <= Cell.Top
                || Cell.Left < Grid.Left - 2.0f || Cell.Top < Grid.Top - 2.0f
                || Cell.Right > Grid.Right + 2.0f || Cell.Bottom > Grid.Bottom + 2.0f)
            { return false; }
            Cells.Add(Cell);
        }
        for (int32 Column = 1; Column < 6; ++Column)
        {
            if (FMath::Abs(Cells[Column].Top - Cells[0].Top) > 2.0f
                || Cells[Column].Left <= Cells[Column - 1].Left
                || Cells[Column].Right > Grid.Right + 2.0f) { return false; }
        }
        for (int32 Row = 1; Row < 4; ++Row)
        {
            if (Cells[Row * 6].Top < Cells[(Row - 1) * 6].Bottom - 2.0f) { return false; }
            for (int32 Column = 1; Column < 6; ++Column)
            { if (FMath::Abs(Cells[Row * 6 + Column].Top - Cells[Row * 6].Top) > 2.0f) { return false; } }
        }
        return true;
    }
    auto RectFor(const TSharedRef<SWidget>& Root, const FString& Tag, FSlateRect& Out) -> bool
    {
        const auto Widget = FindTag(Root, FName(*Tag));
        if (!Widget.IsValid()) { return false; }
        Out = Widget->GetCachedGeometry().GetRenderBoundingRect();
        return FMath::IsFinite(Out.Left) && FMath::IsFinite(Out.Top)
            && FMath::IsFinite(Out.Right) && FMath::IsFinite(Out.Bottom)
            && Out.Right > Out.Left && Out.Bottom > Out.Top;
    }
    auto WithinInset(const FSlateRect& Outer, const FSlateRect& Inner,
        const float Horizontal, const float Vertical, const float Scale) -> bool
    {
        constexpr float RoundingAllowance = 1.0f;
        return Inner.Left >= Outer.Left + Horizontal * Scale - RoundingAllowance
            && Inner.Right <= Outer.Right - Horizontal * Scale + RoundingAllowance
            && Inner.Top >= Outer.Top + Vertical * Scale - RoundingAllowance
            && Inner.Bottom <= Outer.Bottom - Vertical * Scale + RoundingAllowance;
    }
    auto IdentityHasReadablePadding(const TSharedRef<SWidget>& Root) -> bool
    {
        const auto Identity = FindTag(Root, TEXT("identity"));
        if (!Identity.IsValid()) { return false; }
        const float Scale = Identity->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
        FSlateRect Bounds{0, 0, 0, 0}, Name{0, 0, 0, 0}, Level{0, 0, 0, 0};
        const bool Valid = Scale > 0.0f && RectFor(Root, TEXT("identity"), Bounds)
            && RectFor(Root, TEXT("character-name"), Name)
            && RectFor(Root, TEXT("character-class"), Level)
            && WithinInset(Bounds, Name, 0.0f, 12.0f, Scale)
            && WithinInset(Bounds, Level, 0.0f, 12.0f, Scale)
            && Name.Bottom <= Level.Top + 1.0f;
        if (!Valid)
        {
            UE_LOG(LogTemp, Warning, TEXT("CharacterEquipment identity geometry: scale=%.3f frame=[%.1f,%.1f,%.1f,%.1f] name=[%.1f,%.1f,%.1f,%.1f] level=[%.1f,%.1f,%.1f,%.1f]"),
                Scale, Bounds.Left, Bounds.Top, Bounds.Right, Bounds.Bottom,
                Name.Left, Name.Top, Name.Right, Name.Bottom,
                Level.Left, Level.Top, Level.Right, Level.Bottom);
        }
        return Valid;
    }
    auto StatGlyphsFitCells(const TSharedRef<SWidget>& Root) -> bool
    {
        for (const TCHAR* Stat : {TEXT("strength"), TEXT("dexterity"), TEXT("armor"), TEXT("life")})
        {
            const FString Prefix{Stat};
            const auto CellWidget = FindTag(Root, FName(*(Prefix + TEXT("-stat"))));
            if (!CellWidget.IsValid()) { return false; }
            const float Scale = CellWidget->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
            FSlateRect Cell{0, 0, 0, 0}, Glyph{0, 0, 0, 0};
            FSlateRect Label{0, 0, 0, 0}, Value{0, 0, 0, 0};
            const bool Valid = Scale > 0.0f && RectFor(Root, Prefix + TEXT("-stat"), Cell)
                && RectFor(Root, Prefix + TEXT("-glyph"), Glyph)
                && RectFor(Root, Prefix + TEXT("-label"), Label)
                && RectFor(Root, Prefix + TEXT("-value"), Value)
                && WithinInset(Cell, Glyph, 1.0f, 1.0f, Scale)
                && WithinInset(Cell, Label, 1.0f, 0.0f, Scale)
                && WithinInset(Cell, Value, 1.0f, 0.0f, Scale)
                && Glyph.Bottom <= Label.Top + 1.0f
                && Label.Bottom <= Value.Top + 1.0f;
            if (!Valid)
            {
                UE_LOG(LogTemp, Warning, TEXT("CharacterEquipment %s stat geometry: scale=%.3f cell=[%.1f,%.1f,%.1f,%.1f] glyph=[%.1f,%.1f,%.1f,%.1f] label=[%.1f,%.1f,%.1f,%.1f] value=[%.1f,%.1f,%.1f,%.1f]"),
                    *Prefix, Scale, Cell.Left, Cell.Top, Cell.Right, Cell.Bottom,
                    Glyph.Left, Glyph.Top, Glyph.Right, Glyph.Bottom,
                    Label.Left, Label.Top, Label.Right, Label.Bottom,
                    Value.Left, Value.Top, Value.Right, Value.Bottom);
                return false;
            }
        }
        return true;
    }
    auto DetailCardsKeepTextInsidePadding(const TSharedRef<SWidget>& Root) -> bool
    {
        for (const auto& Pair : {TPair<FString, FString>{TEXT("selected-details"), TEXT("selected-card")},
            TPair<FString, FString>{TEXT("comparison-actions"), TEXT("equipped-card")}})
        {
            const auto FrameWidget = FindTag(Root, FName(*Pair.Key));
            if (!FrameWidget.IsValid()) { return false; }
            const float Scale = FrameWidget->GetCachedGeometry().GetAccumulatedLayoutTransform().GetScale();
            FSlateRect Frame, Heading, Name, Primary, Description;
            if (Scale <= 0.0f || !RectFor(Root, Pair.Key, Frame)
                || !RectFor(Root, Pair.Value + TEXT("/heading"), Heading)
                || !RectFor(Root, Pair.Value + TEXT("/name"), Name)
                || !RectFor(Root, Pair.Value + TEXT("/primary"), Primary)
                || !RectFor(Root, Pair.Value + TEXT("/description"), Description)
                || !WithinInset(Frame, Heading, 12.0f, 8.0f, Scale)
                || !WithinInset(Frame, Name, 12.0f, 8.0f, Scale)
                || !WithinInset(Frame, Primary, 12.0f, 8.0f, Scale)
                || !WithinInset(Frame, Description, 12.0f, 8.0f, Scale)) { return false; }
            for (const TCHAR* Affix : {TEXT("critical"), TEXT("strength"), TEXT("dexterity"), TEXT("life")})
            {
                const FString Prefix = Pair.Value + TEXT("/") + Affix;
                FSlateRect Value, Label, Delta;
                if (!RectFor(Root, Prefix + TEXT("-value"), Value)
                    || !RectFor(Root, Prefix + TEXT("-row-label"), Label)
                    || !WithinInset(Frame, Value, 12.0f, 8.0f, Scale)
                    || !WithinInset(Frame, Label, 12.0f, 8.0f, Scale)
                    || Value.Right > Label.Left + 1.0f) { return false; }
                if (Pair.Value == TEXT("selected-card"))
                {
                    if (!RectFor(Root, Prefix + TEXT("-delta"), Delta)
                        || !WithinInset(Frame, Delta, 12.0f, 8.0f, Scale)
                        || Label.Right > Delta.Left + 1.0f) { return false; }
                }
            }
        }
        return true;
    }
    auto TooltipFitsViewport(const TSharedPtr<SCkUiItemSlot>& Slot) -> bool
    {
        if (!Slot.IsValid()) { return false; }
        FSlateRect Popup, Viewport;
        return Slot->TryGetTooltipScreenBounds(Popup, Viewport)
            && Popup.Right > Popup.Left && Popup.Bottom > Popup.Top
            && Popup.Left >= Viewport.Left - 1.0f && Popup.Top >= Viewport.Top - 1.0f
            && Popup.Right <= Viewport.Right + 1.0f && Popup.Bottom <= Viewport.Bottom + 1.0f;
    }
    auto PressKey(const FKey& Key, const int32 User) -> bool
    {
        auto& Slate = FSlateApplication::Get();
        const FKeyEvent Event{Key, FModifierKeysState{}, static_cast<uint32>(User), false, 0, 0};
        const bool Down = Slate.ProcessKeyDownEvent(Event);
        const bool Up = Slate.ProcessKeyUpEvent(Event);
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        return Down && Up;
    }
    auto IsHitTestReachable(const TSharedPtr<SWidget>& Widget, const int32 User) -> bool
    {
        if (!Widget.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Widget.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        const FGeometry Geometry = Widget->GetCachedGeometry();
        if (Geometry.GetLocalSize().X <= 0.0f || Geometry.GetLocalSize().Y <= 0.0f) { return false; }
        const FVector2D Center = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
        return Geometry.IsUnderLocation(Center)
            && Slate.LocateWindowUnderMouse(Center,
                TArray<TSharedRef<SWindow>>{Window.ToSharedRef()}, false, User).ContainsWidget(Widget.Get());
    }
    auto ClickThroughHitTest(const TSharedPtr<SWidget>& Slot, const int32 User) -> bool
    {
        if (!Slot.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Slot.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true);
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        const FGeometry Geometry = Slot->GetCachedGeometry();
        const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
        if (Geometry.GetLocalSize().X <= 0.0f || Geometry.GetLocalSize().Y <= 0.0f
            || !Geometry.IsUnderLocation(Position)) { return false; }
        Slate.SetCursorPos(Position);
        const FPointerEvent Move{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{}, EKeys::Invalid, 0.0f, FModifierKeysState{}};
        Slate.ProcessMouseMoveEvent(Move, true);
        const FWidgetPath Hit = Slate.LocateWindowUnderMouse(Position,
            TArray<TSharedRef<SWindow>>{Window.ToSharedRef()}, false, User);
        if (!Hit.ContainsWidget(Slot.Get())) { return false; }
        const FPointerEvent Down{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const FPointerEvent Up{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        const bool DownHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const bool UpHandled = Slate.ProcessMouseButtonUpEvent(Up);
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        return DownHandled && UpHandled;
    }

    auto DragThroughHitTest(const TSharedPtr<SCkUiItemSlot>& Source,
        const TSharedPtr<SCkUiItemSlot>& Target, const int32 User) -> bool
    {
        if (!Source.IsValid() || !Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Source.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()
            || Slate.FindWidgetWindow(Target.ToSharedRef()) != Window) { return false; }
        Window->BringToFront(true);
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        const FGeometry SourceGeometry = Source->GetCachedGeometry();
        const FGeometry TargetGeometry = Target->GetCachedGeometry();
        if (SourceGeometry.GetLocalSize().X <= 0.0f || SourceGeometry.GetLocalSize().Y <= 0.0f
            || TargetGeometry.GetLocalSize().X <= 0.0f || TargetGeometry.GetLocalSize().Y <= 0.0f)
        { return false; }
        const FVector2D Start = SourceGeometry.LocalToAbsolute(SourceGeometry.GetLocalSize() * 0.5f);
        const FVector2D Finish = TargetGeometry.LocalToAbsolute(TargetGeometry.GetLocalSize() * 0.5f);
        const FVector2D Trigger = Start + FVector2D{24.0f, 0.0f};
        if (!SourceGeometry.IsUnderLocation(Start) || !TargetGeometry.IsUnderLocation(Finish)) { return false; }
        Slate.SetCursorPos(Start);
        Slate.ProcessMouseMoveEvent(FPointerEvent(static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Start, Start, TSet<FKey>{}, EKeys::Invalid, 0.0f, FModifierKeysState{}), true);
        const TArray<TSharedRef<SWindow>> Windows{Window.ToSharedRef()};
        if (!Slate.LocateWindowUnderMouse(Start, Windows, false, User).ContainsWidget(Source.Get())) { return false; }
        const TSet<FKey> Held{EKeys::LeftMouseButton};
        const FPointerEvent Down{static_cast<uint32>(User), FSlateApplication::CursorPointerIndex,
            Start, Start, Held, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{}};
        if (!Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down)) { return false; }
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
        return DragStarted && DragReachedTarget && TargetHit && UpHandled;
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

    auto Capture(UWorld* World, FState& State, const FString& Name, const FIntPoint Desired) -> bool
    {
        auto* Client = IsValid(World) ? World->GetGameViewport() : nullptr;
        const auto Target = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid()) { return false; }
        if (!State.OriginalWindow.IsValid())
        { State.OriginalWindow = Window; State.OriginalWindowSize = Window->GetClientSizeInScreen(); }
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
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/CharacterEquipmentProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Name + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }

    auto CapturePopupWindow(UWorld* World, const FString& Name) -> bool
    {
        auto* Client = IsValid(World) ? World->GetGameViewport() : nullptr;
        const auto Target = IsValid(Client) ? Client->GetGameViewportWidget() : nullptr;
        if (!Target.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Target.ToSharedRef());
        if (!Window.IsValid()) { return false; }
        Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
        TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
        if (!Slate.TakeScreenshot(Window.ToSharedRef(), Pixels, Size) || Size.X < 1000 || Size.Y < 600
            || Pixels.Num() < Size.X * Size.Y) { return false; }
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/CharacterEquipmentProof"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        return FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Name + TEXT(".png")),
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkCharacterEquipment_Atlas,
    "Ck.UiAuthoring.CharacterEquipment.Atlas",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkCharacterEquipment_Atlas::RunTest(const FString&) -> bool
{
    using namespace ck_character_equipment_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Atlas proof requires Slate.")); return false; }
    TStrongObjectPtr<UTexture2D> Texture{UTexture2D::CreateTransient(400, 300)};
    if (!TestNotNull(TEXT("Atlas fixture texture"), Texture.Get())) { return false; }
    // Every cell has a solid, unique color, so a native screenshot checks the sampled UVs.
    {
        auto* Data = Texture->GetPlatformData();
        if (!TestTrue(TEXT("Transient atlas has writable mip pixels"), Data != nullptr && Data->Mips.Num() == 1))
        { return false; }
        auto* Pixels = static_cast<FColor*>(Data->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
        if (!TestNotNull(TEXT("Atlas mip pixels are writable"), Pixels)) { return false; }
        for (int32 Y = 0; Y < 300; ++Y)
        {
            for (int32 X = 0; X < 400; ++X)
            { Pixels[Y * 400 + X] = FColor(30 + (X / 100) * 50, 30 + (Y / 100) * 65, 80, 255); }
        }
        Data->Mips[0].BulkData.Unlock();
        Texture->UpdateResource();
    }
    FCk_UI_AuthoredImage Atlas{TEXT("atlas")}; Atlas.Set_Texture(Texture.Get());
    FCk_UI_AuthoredSession_Config Config;
    Config.Set_Images({Atlas}); Config.Set_Number({FCk_UI_AuthoredNumber{TEXT("cell"), 5.0f}});
    Config.Set_RegisterItemControls(true);
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Owner{NewObject<UCk_UI_AuthoredSession_UE>()};
    TArray<FString> Errors;
    if (!TestTrue(TEXT("Atlas enters through real authored session and registered factory"),
        Owner->TryInitialize(Config, Errors))) { return false; }
    const FString Markup = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
        "<atlas-image id=\"atlas-control\" image-bind=\"atlas\" index-bind=\"cell\" columns=\"4\" rows=\"3\"/>"
        "</column></region></ui>");
    if (!TestTrue(TEXT("Regular four by three atlas document commits"), Owner->TryReload(Markup, TEXT(""), Errors)))
    { Owner->Request_Release(); return false; }
    const auto View = Owner->Get_View();
    const auto Image = FindTagged<SImage>(View->GetRegion(TEXT("main")), TEXT("SImage"), TEXT("atlas-control"));
    if (!TestTrue(TEXT("Atlas factory realizes tagged native SImage"), Image.IsValid()))
    { Owner->Request_Release(); return false; }
    TestEqual(TEXT("Atlas cell uses one-fourth width and one-third height"), ImageSize(Image), FVector2D(100, 100));
    if (!GUsingNullRHI)
    {
        auto& Slate = FSlateApplication::Get();
        const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(120, 120))
            .CreateTitleBar(false)[View->GetRegion(TEXT("main"))];
        Slate.AddWindow(Window, true);
        ON_SCOPE_EXIT { Slate.DestroyWindowImmediately(Window); };
        const auto CheckCell = [this, &Slate, &Image, &Owner](int32 Index, FColor Expected) -> void
        {
            TestTrue(TEXT("Atlas accepts a valid index"), Owner->TrySetNumber(TEXT("cell"), static_cast<float>(Index)));
            Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
            TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
            if (!TestTrue(TEXT("Atlas cell captures real rendered pixels"),
                Slate.TakeScreenshot(Image.ToSharedRef(), Pixels, Size)
                && Size.X >= 50 && Size.Y >= 50 && Pixels.Num() >= Size.X * Size.Y)) { return; }
            const FColor Actual = Pixels[(Size.Y / 2) * Size.X + Size.X / 2];
            TestTrue(TEXT("Rendered color identifies selected atlas row and column"),
                FMath::Abs(int32(Actual.R) - int32(Expected.R)) <= 20
                && FMath::Abs(int32(Actual.G) - int32(Expected.G)) <= 20
                && FMath::Abs(int32(Actual.B) - int32(Expected.B)) <= 20);
        };
        CheckCell(5, FColor(80, 95, 80));
        CheckCell(11, FColor(180, 160, 80));
    }
    const int64 Revision = Owner->Get_Revision();
    TestFalse(TEXT("Zero atlas columns reject entire reload"), Owner->TryReload(
        Markup.Replace(TEXT("columns=\"4\""), TEXT("columns=\"0\"")), TEXT(""), Errors));
    TestTrue(TEXT("Invalid reload leaves accepted native image and revision"),
        View == Owner->Get_View() && Owner->Get_Revision() == Revision
        && FindTagged<SImage>(View->GetRegion(TEXT("main")), TEXT("SImage"), TEXT("atlas-control")) == Image);
    for (const float Invalid : {-1.0f, 12.0f, 1.5f})
    {
        TestTrue(TEXT("Invalid atlas index publishes safely"), Owner->TrySetNumber(TEXT("cell"), Invalid));
        TestEqual(TEXT("Negative, noninteger and overflow cells draw blank"),
            ImageSize(Image), FVector2D::ZeroVector);
    }
    TestFalse(TEXT("Session rejects nonfinite scalar before publication"),
        Owner->TrySetNumber(TEXT("cell"), std::numeric_limits<float>::quiet_NaN()));
    FCkUiWidgetRegistry Registry;
    TestTrue(TEXT("Direct nonfinite branch uses the registered atlas factory"),
        RegisterAtlasImageUiControl(Registry).Succeeded);
    const auto Registration = Registry.CreateSnapshot()->Find(TEXT("atlas-image"));
    if (TestTrue(TEXT("Registered atlas factory is available"), Registration != nullptr))
    {
        FSlateBrush SourceBrush;
        SourceBrush.ImageSize = FVector2D(400, 300);
        float DirectIndex = std::numeric_limits<float>::quiet_NaN();
        FCkUiCustomWidgetArguments Args;
        Args.Id = TEXT("direct-atlas");
        Args.ImageBindings.Add(TEXT("image"), TAttribute<const FSlateBrush*>::CreateLambda(
            [&SourceBrush]() -> const FSlateBrush* { return &SourceBrush; }));
        Args.NumberBindings.Add(TEXT("index"), TAttribute<float>::CreateLambda([&DirectIndex]() { return DirectIndex; }));
        Args.NumberProperties.Add(TEXT("columns"), 4.0f);
        Args.NumberProperties.Add(TEXT("rows"), 3.0f);
        FString Failure;
        const auto Retained = Registration->RetainedFactory(Args, Failure);
        if (TestTrue(TEXT("Factory accepts valid declaration with dynamic index"), Retained.IsValid()))
        {
            const auto DirectImage = StaticCastSharedRef<SImage>(Retained->GetWidget());
            TestEqual(TEXT("Nonfinite dynamic atlas index draws blank"),
                ImageSize(DirectImage), FVector2D::ZeroVector);
            DirectIndex = 5.0f;
            TestEqual(TEXT("Finite dynamic index restores regular atlas cell"),
                ImageSize(DirectImage), FVector2D(100, 100));
            Retained->ReleaseOwnerInteraction();
            TestEqual(TEXT("Direct factory owner release blanks retained image"),
                ImageSize(DirectImage), FVector2D::ZeroVector);
        }
    }
    TestTrue(TEXT("Compatible atlas reload succeeds"), Owner->TryReload(Markup, TEXT(""), Errors));
    TestTrue(TEXT("Compatible reload retains atlas widget"),
        FindTagged<SImage>(View->GetRegion(TEXT("main")), TEXT("SImage"), TEXT("atlas-control")) == Image);
    Owner->Request_Release();
    TestTrue(TEXT("Released atlas owner blanks retained image"),
        View->IsOwnerReleased() && ImageSize(Image) == FVector2D::ZeroVector);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkCharacterEquipment_Pie,
    "Ck.UiAuthoring.CharacterEquipment.PIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkCharacterEquipment_Pie::RunTest(const FString&) -> bool
{
    using namespace ck_character_equipment_proof;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Equipment gym proof requires Slate.")); return false; }
    const auto State = MakeShared<FState>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* R = Registry(ck::auto_test::net::Get_ServerWorld());
            return IsValid(R) && R->Find_GymIndexByName(TEXT("UI: Character & Equipment")) != INDEX_NONE;
        }), 15.0, TEXT("Character equipment production gym registers")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* World)
        {
            auto* R = Registry(World);
            if (!TestNotNull(TEXT("Gym registry exists"), R)) { return; }
            const int32 Index = R->Find_GymIndexByName(TEXT("UI: Character & Equipment"));
            if (!TestTrue(TEXT("Registered equipment gym entry exists"), R->Get_Entries().IsValidIndex(Index))) { return; }
            const auto& Entry = R->Get_Entries()[Index];
            TestEqual(TEXT("Equipment gym category"), Entry.Category, FString(TEXT("CkUI")));
            TestTrue(TEXT("Equipment gym selects script game mode"), IsValid(Entry.GameModeClass.Get())
                && Entry.GameModeClass.Get()->GetName().Contains(TEXT("Ck_CharacterEquipment_GameMode")));
            R->Request_TravelToGym(Index);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            auto* Owner = Session(World);
            return IsValid(Hud(World)) && ((IsValid(Owner) && Owner->Get_IsUsable() && Owner->Get_Revision() > 0)
                || !Getter<FStrProperty, FString>(Hud(World), TEXT("Get_StartupError"), {}).IsEmpty());
        }), 30.0, TEXT("Production equipment screen mounts or reports startup failure")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* World)
        {
            auto* Widget = Hud(World); auto* Owner = Session(World);
            const FString StartupError = Getter<FStrProperty, FString>(Widget, TEXT("Get_StartupError"), {});
            if (!StartupError.IsEmpty()) { AddError(FString::Printf(TEXT("Equipment startup: %s"), *StartupError)); return; }
            if (!TestNotNull(TEXT("Equipment HUD widget exists"), Widget)
                || !TestNotNull(TEXT("Equipment authored session exists"), Owner)) { return; }
            const auto View = Owner->Get_View();
            if (!TestTrue(TEXT("Production equipment authored view commits"), View.IsValid())) { return; }
            const auto Root = View->GetRegion(TEXT("main"));
            const auto Weapon = FindTagged<SCkUiItemSlot>(Root, TEXT("SCkUiItemSlot"), TEXT("equip-weapon/card"));
            const auto Sword = Card(View, TEXT("veilcutter"));
            const auto ComparisonCard = FindTag(Root, TEXT("comparison-actions"));
            const auto SelectedCard = FindTag(Root, TEXT("selected-details"));
            const auto SelectedGearRows = FindTag(Root, TEXT("selected-card/gear-stats"));
            const auto EquippedGearRows = FindTag(Root, TEXT("equipped-card/gear-stats"));
            const auto SelectedPowerWidget = FindTag(Root, TEXT("selected-card/power"));
            const auto CompareKey = FindTag(Root, TEXT("compare-key"));
            const auto PrimaryKey = FindTag(Root, TEXT("equip-key"));
            const auto EquipmentFilter = FindTagged<SWidget>(Root, TEXT("SActionCard"), TEXT("filter-equipment"));
            const auto SuppliesFilter = FindTagged<SWidget>(Root, TEXT("SActionCard"), TEXT("filter-supplies"));
            const auto AllFilter = FindTagged<SWidget>(Root, TEXT("SActionCard"), TEXT("filter-all"));
            if (!TestTrue(TEXT("Production screen mounts native equipment and inventory item slots"),
                Weapon.IsValid() && Sword.IsValid())) { return; }
            Capture(World, *State, TEXT("equipment-mount-diagnostic"), FIntPoint{1920, 1080});
            if (!TestTrue(TEXT("Contextual comparison, footer keys and selectable filters mount"),
                ComparisonCard.IsValid() && SelectedCard.IsValid()
                && SelectedGearRows.IsValid() && EquippedGearRows.IsValid()
                && SelectedPowerWidget.IsValid() && CompareKey.IsValid()
                && PrimaryKey.IsValid() && EquipmentFilter.IsValid() && SuppliesFilter.IsValid()
                && AllFilter.IsValid() && EquipmentFilter->GetTypeAsString() == TEXT("SActionCard")))
            { return; }
            TestEqual(TEXT("Initial weapon is the ashen edge"), Equipped(Widget, TEXT("Weapon")), FString(TEXT("ashen-edge")));
            TestEqual(TEXT("Initial selection is veilcutter"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("veilcutter")));
            const int32 InitialMutations = Mutations(Widget);
            TestTrue(TEXT("Initial model has a valid mutation counter"), InitialMutations >= 0);
            TestTrue(TEXT("Nine real equipment positions begin occupied and Ring II is open"),
                Equipped(Widget, TEXT("Head")) == TEXT("worn-hood")
                && Equipped(Widget, TEXT("Chest")) == TEXT("worn-chest")
                && Equipped(Widget, TEXT("Hands")) == TEXT("worn-hands")
                && Equipped(Widget, TEXT("Legs")) == TEXT("worn-legs")
                && Equipped(Widget, TEXT("Boots")) == TEXT("worn-boots")
                && Equipped(Widget, TEXT("Amulet")) == TEXT("worn-amulet")
                && Equipped(Widget, TEXT("Ring1")) == TEXT("worn-ring")
                && Equipped(Widget, TEXT("Ring2")).IsEmpty()
                && Equipped(Widget, TEXT("Offhand")) == TEXT("worn-offhand"));
            TestTrue(TEXT("Initial totals derive from occupied gear and inventory reports occupied capacity"),
                Text(Owner, TEXT("Armor")) == TEXT("269")
                && Text(Owner, TEXT("Strength")) == TEXT("39")
                && Text(Owner, TEXT("Dexterity")) == TEXT("38")
                && Text(Owner, TEXT("Life")) == TEXT("524 / 524")
                && Text(Owner, TEXT("InventoryCount")) == TEXT("16 / 24"));
            TestTrue(TEXT("Selected blade exposes a numeric gain and loss against current weapon"),
                Text(Owner, TEXT("SelectedDeltaStrength")) == TEXT("+11")
                && Text(Owner, TEXT("SelectedDeltaDexterity")) == TEXT("-3")
                && Flag(Owner, TEXT("SelectedDeltaStrengthGain"))
                && Flag(Owner, TEXT("SelectedDeltaDexterityLoss"))
                && IsRenderedVisible(SelectedGearRows)
                && IsRenderedVisible(EquippedGearRows));
            TestTrue(TEXT("Authored input owner is active"), Owner->Get_IsInputActive());
            TestTrue(TEXT("Session focus API targets a real native inventory slot"),
                Owner->TryFocusControl(TEXT("items/veilcutter/card")));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
            TestTrue(TEXT("Native Slate user focus reaches the selected slot"),
                FSlateApplication::Get().GetUserFocusedWidget(Owner->Get_SlateUserIndex()) == Sword);
            TestTrue(TEXT("Focused item opens authored tooltip"), Sword->HasAuthoredTooltip() && Sword->GetIsTooltipOpen());
            const FVector2D TooltipLimit = Sword->GetTooltipMaxSize();
            const auto TooltipWindow = FSlateApplication::Get().FindWidgetWindow(Sword.ToSharedRef());
            TestTrue(TEXT("Tooltip bounds use the current physical viewport window"), TooltipWindow.IsValid()
                && TooltipLimit.X > 0.0 && TooltipLimit.Y > 0.0
                && TooltipLimit.X <= TooltipWindow->GetClientSizeInScreen().X
                && TooltipLimit.Y <= TooltipWindow->GetClientSizeInScreen().Y);
            TestTrue(TEXT("Focused tooltip actual rendered bounds stay inside viewport"), TooltipFitsViewport(Sword));
            TestTrue(TEXT("Overview capture moves focus off the item tooltip"),
                Owner->TryFocusControl(TEXT("filter-all")));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick();
            TestFalse(TEXT("Overview tooltip closes before baseline capture"), Sword->GetIsTooltipOpen());
            if (!GUsingNullRHI)
            {
                for (const auto Size : {FIntPoint{1280, 720}, FIntPoint{1920, 1080}, FIntPoint{2400, 1350}})
                {
                    TestTrue(TEXT("Production equipment native screenshot"), Capture(World, *State,
                        FString::Printf(TEXT("equipment-%dx%d"), Size.X, Size.Y), Size));
                    TestTrue(*FString::Printf(TEXT("Six columns and all 24 cells fit at %dx%d"), Size.X, Size.Y),
                        GridFitsSixColumns(Owner));
                    TestTrue(*FString::Printf(TEXT("Identity name and level have readable top and bottom padding at %dx%d"),
                        Size.X, Size.Y), IdentityHasReadablePadding(Root));
                    TestTrue(*FString::Printf(TEXT("Attribute glyphs, labels and values fit their cells at %dx%d"),
                        Size.X, Size.Y), StatGlyphsFitCells(Root));
                    TestTrue(*FString::Printf(TEXT("Both detail cards keep text and affix columns padded without overlap at %dx%d"),
                        Size.X, Size.Y), DetailCardsKeepTextInsidePadding(Root));
                    TestTrue(*FString::Printf(TEXT("Primary footer key remains inside the routed pointer hit path at %dx%d"),
                        Size.X, Size.Y), IsHitTestReachable(PrimaryKey, Owner->Get_SlateUserIndex()));
                }
            }
            TestTrue(TEXT("Inventory navigation returns focus to selected sword"),
                Owner->TryFocusControl(TEXT("items/veilcutter/card")));
            const int32 User = Owner->Get_SlateUserIndex();
            TestTrue(TEXT("Keyboard Right navigates the real inventory grid"), PressKey(EKeys::Right, User));
            TestEqual(TEXT("Right selects second item"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("duskbow")));
            TestTrue(TEXT("Keyboard Down navigates six columns"), PressKey(EKeys::Down, User));
            TestEqual(TEXT("Down selects the next row"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("sapphire-ring")));
            TestTrue(TEXT("Gamepad D-pad Left navigates inventory"), PressKey(EKeys::Gamepad_DPad_Left, User));
            TestEqual(TEXT("Gamepad D-pad selects previous item"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("amulet")));
            TestTrue(TEXT("Keyboard Q enters equipment navigation"), PressKey(EKeys::Q, User));
            TestEqual(TEXT("Equipment navigation selects first occupied slot"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("worn-hood")));
            TestTrue(TEXT("Gamepad shoulder returns to inventory navigation"),
                PressKey(EKeys::Gamepad_LeftShoulder, User));
            TestEqual(TEXT("Inventory navigation restores first visible item"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("veilcutter")));
            const auto Bow = Card(View, TEXT("duskbow"));
            TestTrue(TEXT("Focus moves off item tooltip before pointer hit test"),
                Owner->TryFocusControl(TEXT("filter-all")));
            TestTrue(TEXT("Rendered inventory card wins actual Slate hit test and mouse click"),
                ClickThroughHitTest(Bow, User));
            TestEqual(TEXT("Routed native click selects the bow in script model"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("duskbow")));
            TestTrue(TEXT("Comparable selected bow exposes the comparison and contextual action"),
                Flag(Owner, TEXT("CompareVisible")) && Text(Owner, TEXT("ComparisonStats")).Contains(TEXT("Ashen Edge"))
                && Text(Owner, TEXT("PrimaryActionLabel")) == TEXT("EQUIP")
                && Text(Owner, TEXT("SelectedDeltaStrength")) == TEXT("-3")
                && Flag(Owner, TEXT("SelectedDeltaStrengthLoss"))
                && RecordText(Owner, TEXT("duskbow"), TEXT("compare")).Contains(TEXT("Ashen Edge")));
            TestTrue(TEXT("Comparison footer is reachable after item selection"),
                Owner->TryFocusControl(TEXT("filter-all")));
            TestTrue(TEXT("Clicking visible compare footer key hides the whole comparison card"),
                ClickThroughHitTest(CompareKey, User)
                && !Flag(Owner, TEXT("CompareVisible"))
                && Text(Owner, TEXT("ComparisonStats")).IsEmpty()
                && Text(Owner, TEXT("CompareActionLabel")) == TEXT("SHOW COMPARISON")
                && RecordText(Owner, TEXT("duskbow"), TEXT("compare")).IsEmpty()
                && ComparisonCard->GetVisibility() == EVisibility::Collapsed
                && SelectedCard->GetVisibility() == EVisibility::Visible);
            TestTrue(TEXT("Clicking compare footer key restores the entire equipped card"),
                ClickThroughHitTest(CompareKey, User)
                && Flag(Owner, TEXT("CompareVisible"))
                && Text(Owner, TEXT("CompareActionLabel")) == TEXT("HIDE COMPARISON")
                && ComparisonCard->GetVisibility() == EVisibility::Visible);
            TestTrue(TEXT("Selection restores sword before routed pointer drag proof"),
                Invoke(Widget, TEXT("Request_Select"), {TEXT("veilcutter")})
                && Owner->TryFocusControl(TEXT("filter-all")));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
            const auto LiveSword = Card(View, TEXT("veilcutter"));
            if (!TestTrue(TEXT("Selected sword has a current enabled native card after collection refresh"),
                LiveSword.IsValid() && LiveSword->GetItemKey() == TEXT("veilcutter")
                && LiveSword->GetItemKind() == TEXT("weapon") && LiveSword->IsEnabled())) { return; }
            if (!TestTrue(TEXT("Slate routes physical sword drag into the rendered compatible weapon slot"),
                DragThroughHitTest(LiveSword, Weapon, User)))
            { return; }
            if (!TestEqual(TEXT("Weapon slot receives veilcutter after physical drop"),
                Equipped(Widget, TEXT("Weapon")), FString(TEXT("veilcutter")))) { return; }
            TestTrue(TEXT("Routed swap changes derived Strength and preserves inventory occupancy"),
                Mutations(Widget) == InitialMutations + 1
                && Text(Owner, TEXT("Strength")) == TEXT("50")
                && Text(Owner, TEXT("Dexterity")) == TEXT("35")
                && Text(Owner, TEXT("InventoryCount")) == TEXT("16 / 24"));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
            const auto Displaced = Card(View, TEXT("ashen-edge"));
            TestTrue(TEXT("Displaced weapon returns to inventory without duplicate equipped copy"),
                Displaced.IsValid() && !Card(View, TEXT("veilcutter")).IsValid());
            const int32 AfterSwap = Mutations(Widget);
            TestFalse(TEXT("Incompatible supply cannot equip head"),
                Invoke(Widget, TEXT("Request_TryEquip"), {TEXT("red-potion"), TEXT("Head")}));
            TestFalse(TEXT("Unknown item cannot equip"),
                Invoke(Widget, TEXT("Request_TryEquip"), {TEXT("removed-key"), TEXT("Weapon")}));
            TestTrue(TEXT("Rejected requests retain equipment and mutation count"),
                Equipped(Widget, TEXT("Weapon")) == TEXT("veilcutter") && Mutations(Widget) == AfterSwap);
            TestTrue(TEXT("Ring selection exposes the target picker and default first destination"),
                Invoke(Widget, TEXT("Request_Select"), {TEXT("sapphire-ring")})
                && Flag(Owner, TEXT("RingTargetVisible"))
                && Text(Owner, TEXT("RingTargetLabel")).EndsWith(TEXT("Ring1"), ESearchCase::IgnoreCase));
            TestTrue(TEXT("Ring destination cycles to the genuinely empty second slot"),
                Invoke(Widget, TEXT("Request_CycleRingTarget"))
                && Text(Owner, TEXT("RingTargetLabel")).EndsWith(TEXT("Ring2"), ESearchCase::IgnoreCase)
                && !Flag(Owner, TEXT("CompareVisible")));
            TestTrue(TEXT("Footer action remains physically reachable with ring selected"),
                Owner->TryFocusControl(TEXT("filter-all")));
            TestTrue(TEXT("Contextual primary action equips ring into selected destination"),
                ClickThroughHitTest(PrimaryKey, User)
                && Equipped(Widget, TEXT("Ring2")) == TEXT("sapphire-ring")
                && Equipped(Widget, TEXT("Ring1")) == TEXT("worn-ring")
                && Mutations(Widget) == AfterSwap + 1
                && Text(Owner, TEXT("InventoryCount")) == TEXT("15 / 24")
                && Text(Owner, TEXT("Dexterity")) == TEXT("41")
                && Text(Owner, TEXT("Life")) == TEXT("533 / 533"));
            TestTrue(TEXT("Second ring can replace occupied first ring through destination choice"),
                Invoke(Widget, TEXT("Request_Select"), {TEXT("skull-ring")})
                && Invoke(Widget, TEXT("Request_CycleRingTarget"))
                && Text(Owner, TEXT("RingTargetLabel")).EndsWith(TEXT("Ring1"), ESearchCase::IgnoreCase)
                && Invoke(Widget, TEXT("Request_PrimaryAction"))
                && Equipped(Widget, TEXT("Ring1")) == TEXT("skull-ring"));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
            TestTrue(TEXT("Published displaced ring appears in the rendered inventory repeat"),
                Card(View, TEXT("worn-ring")).IsValid());
            const int32 AfterRings = Mutations(Widget);
            TestFalse(TEXT("Moving an equipped ring into its current slot is rejected"),
                Invoke(Widget, TEXT("Request_TryEquip"), {TEXT("sapphire-ring"), TEXT("Ring2")}));
            TestEqual(TEXT("Rejected same-slot ring request does not mutate"), Mutations(Widget), AfterRings);
            TestTrue(TEXT("Equipped ring can move to occupied compatible slot and swap both rings"),
                Invoke(Widget, TEXT("Request_TryEquip"), {TEXT("sapphire-ring"), TEXT("Ring1")})
                && Equipped(Widget, TEXT("Ring1")) == TEXT("sapphire-ring")
                && Equipped(Widget, TEXT("Ring2")) == TEXT("skull-ring")
                && Mutations(Widget) == AfterRings + 1);
            TestFalse(TEXT("Re-equipping into the same slot is rejected"),
                Invoke(Widget, TEXT("Request_TryEquip"), {TEXT("sapphire-ring"), TEXT("Ring1")}));
            TestTrue(TEXT("Rejected same-slot move retains both rings and mutation count"),
                Equipped(Widget, TEXT("Ring1")) == TEXT("sapphire-ring")
                && Equipped(Widget, TEXT("Ring2")) == TEXT("skull-ring") && Mutations(Widget) == AfterRings + 1);
            TestTrue(TEXT("Equipped selection gives UNEQUIP primary action and returns ring to inventory"),
                Text(Owner, TEXT("PrimaryActionLabel")) == TEXT("UNEQUIP")
                && Invoke(Widget, TEXT("Request_PrimaryAction"))
                && Equipped(Widget, TEXT("Ring1")).IsEmpty()
                && Card(View, TEXT("sapphire-ring")).IsValid()
                && Text(Owner, TEXT("InventoryCount")) == TEXT("16 / 24")
                && Text(Owner, TEXT("Dexterity")) == TEXT("35")
                && Text(Owner, TEXT("Life")) == TEXT("525 / 525"));
            TestTrue(TEXT("Select supply before filtering"), Invoke(Widget, TEXT("Request_Select"), {TEXT("red-potion")}));
            const int32 BeforeSupply = Mutations(Widget);
            TestTrue(TEXT("Native Equipment action-card filter commits from a routed click"),
                ClickThroughHitTest(EquipmentFilter, User));
            TestTrue(TEXT("Equipment filter publishes visible selected filter state"),
                Flag(Owner, TEXT("FilterEquipmentActive"))
                && !Flag(Owner, TEXT("FilterAllActive"))
                && Text(Owner, TEXT("FilterContext")).StartsWith(TEXT("Equipment"), ESearchCase::IgnoreCase));
            TestEqual(TEXT("Filtering away selected supply reconciles selection"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("duskbow")));
            TestFalse(TEXT("Filtered supply cannot be selected through stale key"),
                Invoke(Widget, TEXT("Request_Select"), {TEXT("red-potion")}));
            TestEqual(TEXT("Rejected filtered selection leaves model unchanged"), Mutations(Widget), BeforeSupply);
            TestTrue(TEXT("Native Supplies action-card filter commits from a routed click"),
                ClickThroughHitTest(SuppliesFilter, User));
            TestTrue(TEXT("Supplies filter publishes visible selected filter state"),
                Flag(Owner, TEXT("FilterSuppliesActive"))
                && !Flag(Owner, TEXT("FilterEquipmentActive"))
                && Text(Owner, TEXT("FilterContext")).StartsWith(TEXT("Supplies"), ESearchCase::IgnoreCase));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
            TestEqual(TEXT("Supplies filter selects first visible item"),
                Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}), FString(TEXT("red-potion")));
            const auto Supply = Card(View, TEXT("red-potion"));
            TestTrue(TEXT("Supply slot is present and focusable"), Supply.IsValid()
                && Owner->TryFocusControl(TEXT("items/red-potion/card")));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
            TestTrue(TEXT("Selected supply tooltip is open before capture"),
                Supply.IsValid() && Supply->GetIsTooltipOpen());
            TestTrue(TEXT("Supply details do not present gear affixes or item power"),
                !Flag(Owner, TEXT("SelectedGearStats"))
                && !Flag(Owner, TEXT("EquippedGearStats"))
                && Text(Owner, TEXT("SelectedPower")).IsEmpty()
                && !IsRenderedVisible(SelectedGearRows)
                && !IsRenderedVisible(EquippedGearRows)
                && !IsRenderedVisible(SelectedPowerWidget));
            if (!GUsingNullRHI)
            {
                TestTrue(TEXT("Selected supply native screenshot"),
                    Capture(World, *State, TEXT("equipment-selected-supply-1920x1080"), {1920, 1080}));
                TestTrue(TEXT("Opened tooltip captured from containing Slate window"),
                    CapturePopupWindow(World, TEXT("equipment-open-tooltip-window")));
            }
            TestTrue(TEXT("Selected tooltip actual rendered bounds stay inside viewport"), TooltipFitsViewport(Supply));
            TestTrue(TEXT("Selected supply exposes a contextual USE action"),
                Text(Owner, TEXT("PrimaryActionLabel")) == TEXT("USE")
                && !Flag(Owner, TEXT("CompareVisible")));
            TestTrue(TEXT("Primary action consumes one potion without changing equipped totals"),
                Invoke(Widget, TEXT("Request_PrimaryAction"))
                && Mutations(Widget) == BeforeSupply + 1
                && Card(View, TEXT("red-potion")).IsValid()
                && Text(Owner, TEXT("InventoryCount")) == TEXT("16 / 24")
                && Text(Owner, TEXT("Life")) == TEXT("525 / 525"));
            TestTrue(TEXT("Using the remaining potion stack removes its inventory record"),
                Invoke(Widget, TEXT("Request_PrimaryAction"))
                && Invoke(Widget, TEXT("Request_PrimaryAction"))
                && Mutations(Widget) == BeforeSupply + 3
                && Text(Owner, TEXT("InventoryCount")) == TEXT("15 / 24")
                && Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}) == TEXT("blue-potion")
                && !Card(View, TEXT("red-potion")).IsValid());
            TestTrue(TEXT("Native All action-card filter restores equipment inventory"),
                ClickThroughHitTest(AllFilter, User));
            TestTrue(TEXT("All filter selected state and count reconcile after supply removal"),
                Flag(Owner, TEXT("FilterAllActive"))
                && !Flag(Owner, TEXT("FilterSuppliesActive"))
                && Text(Owner, TEXT("InventoryCount")) == TEXT("15 / 24"));
            FSlateApplication::Get().PumpMessages(); FSlateApplication::Get().Tick(); FSlateApplication::Get().Tick();
            const auto CurrentDisplaced = Card(View, TEXT("ashen-edge"));
            if (!TestTrue(TEXT("Returning to All creates a current displaced weapon card"), CurrentDisplaced.IsValid()))
            { return; }
            const int64 BeforeReload = Owner->Get_Revision();
            TestTrue(TEXT("Installed equipment source reloads"), Invoke(Widget, TEXT("Request_Reload")));
            TestTrue(TEXT("Compatible source retains weapon slot and model"),
                Owner->Get_Revision() > BeforeReload && Equipped(Widget, TEXT("Weapon")) == TEXT("veilcutter")
                && FindTagged<SCkUiItemSlot>(View->GetRegion(TEXT("main")), TEXT("SCkUiItemSlot"),
                    TEXT("equip-weapon/card")) == Weapon);
            const auto StaleDrag = CurrentDisplaced->TryBeginDrag();
            TestTrue(TEXT("Held inventory drag starts before owner release"), StaleDrag.IsValid());
            const int32 BeforeHide = Mutations(Widget);
            TestTrue(TEXT("Hiding equipment screen ends authored input"),
                Invoke(Widget, TEXT("Request_Show"), {}, {false}) && !Owner->Get_IsInputActive());
            TestTrue(TEXT("Hidden screen rejects drag and preserves equipment"),
                !CurrentDisplaced->TryBeginDrag().IsValid() && !Weapon->TryAcceptDrag(StaleDrag)
                && Equipped(Widget, TEXT("Weapon")) == TEXT("veilcutter") && Mutations(Widget) == BeforeHide);
            TestTrue(TEXT("Reopening equipment screen restores authored input"),
                Invoke(Widget, TEXT("Request_Show"), {}, {true}) && Owner->Get_IsInputActive());
            const auto FreshDrag = CurrentDisplaced->TryBeginDrag();
            TestTrue(TEXT("Reopened screen accepts a fresh proposal but not the hidden stale drag"),
                FreshDrag.IsValid() && Weapon->CanAcceptDrag(FreshDrag) && !Weapon->CanAcceptDrag(StaleDrag));
            TestTrue(TEXT("Fixture reset restores starting equipment and selection"),
                Invoke(Widget, TEXT("Request_Reset")) && Equipped(Widget, TEXT("Weapon")) == TEXT("ashen-edge")
                && Equipped(Widget, TEXT("Ring1")) == TEXT("worn-ring") && Equipped(Widget, TEXT("Ring2")).IsEmpty()
                && Getter<FStrProperty, FString>(Widget, TEXT("Get_SelectedKey"), {}) == TEXT("veilcutter")
                && Mutations(Widget) == BeforeHide + 1
                && Text(Owner, TEXT("Armor")) == TEXT("269")
                && Text(Owner, TEXT("Strength")) == TEXT("39")
                && Text(Owner, TEXT("Dexterity")) == TEXT("38")
                && Text(Owner, TEXT("Life")) == TEXT("524 / 524")
                && Text(Owner, TEXT("InventoryCount")) == TEXT("16 / 24"));
            const auto ResetSword = Card(View, TEXT("veilcutter"));
            const auto ReleaseDrag = ResetSword.IsValid() ? ResetSword->TryBeginDrag() : nullptr;
            TestTrue(TEXT("A live drag is admissible immediately before owner release"),
                ReleaseDrag.IsValid() && Weapon->CanAcceptDrag(ReleaseDrag));
            Owner->Request_Release();
            TestTrue(TEXT("Owner release disables retained native drop and model mutation"),
                View->IsOwnerReleased() && !Weapon->TryAcceptDrag(ReleaseDrag)
                && !Invoke(Widget, TEXT("Request_TryEquip"), {TEXT("duskbow"), TEXT("Weapon")})
                && Mutations(Widget) == BeforeHide + 1);
            State->Restore();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

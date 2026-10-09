#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/CkUiItemSlot.h"
#include "CkSlateLayout/SCkUiRepeat.h"
#include "CkSlateLayout/SCkUiSurface.h"

#include "Layout/Children.h"
#include "Layout/SlateRect.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/DragAndDrop.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Widgets/SWindow.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_item_slot
{
    auto DragAtlasBrush() -> FSlateBrush&
    { static FSlateBrush Brush; return Brush; }

    auto CountTagged(const TSharedRef<SWidget>& Root, const FName Tag) -> int32
    {
        int32 Count = Root->GetTag() == Tag ? 1 : 0;
        const FChildren* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { Count += CountTagged(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Tag); }
        return Count;
    }

    struct FWindowScope
    {
        FSlateApplication& Slate;
        TSharedPtr<SWindow> Window;
        ~FWindowScope() { if (Window.IsValid()) { Slate.DestroyWindowImmediately(Window.ToSharedRef()); } }
    };

    auto Pointer(FSlateApplication& Slate, const TSharedRef<SWidget>& Widget) -> FPointerEvent
    {
        const FVector2D Position = Widget->GetCachedGeometry().LocalToAbsolute(FVector2D{8.0f, 8.0f});
        return FPointerEvent(Slate.GetCursorUser()->GetUserIndex(), FSlateApplication::CursorPointerIndex,
            Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState{});
    }

    auto Find(const TSharedRef<SWidget>& Root, const FName Tag) -> TSharedPtr<SCkUiItemSlot>
    {
        if (Root->GetTag() == Tag && Root->GetTypeAsString() == TEXT("SCkUiItemSlot"))
        { return StaticCastSharedRef<SCkUiItemSlot>(Root); }
        const FChildren* Children = Root->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const auto Result = Find(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), Tag))
            { return Result; }
        }
        return {};
    }

    auto MakeData(FString& SourceKey, FString& SourceKind, TArray<FString>& Selected,
        TArray<FString>& Dropped, float* DragIndex = nullptr) -> FCkUiView::FDataBindings
    {
        FCkUiView::FDataBindings Data;
        DragAtlasBrush().ImageSize = FVector2D{400.0f, 300.0f};
        Data.Text.Add(TEXT("source-key"), TAttribute<FText>::CreateLambda([&SourceKey]()
        { return FText::FromString(SourceKey); }));
        Data.Text.Add(TEXT("source-kind"), TAttribute<FText>::CreateLambda([&SourceKind]()
        { return FText::FromString(SourceKind); }));
        Data.Text.Add(TEXT("target-key"), FText::FromString(TEXT("equipped-sword")));
        Data.Text.Add(TEXT("weapon-kind"), FText::FromString(TEXT("weapon")));
        Data.Text.Add(TEXT("armor-kind"), FText::FromString(TEXT("armor")));
        Data.Number.Add(TEXT("slot-tier"), 2.0f);
        Data.Text.Add(TEXT("drag-label"), FText::FromString(TEXT("Iron Sword")));
        Data.Images.Add(TEXT("drag-atlas"), TAttribute<const FSlateBrush*>::CreateLambda([]()
        { return &DragAtlasBrush(); }));
        Data.Number.Add(TEXT("drag-index"), DragIndex != nullptr
            ? TAttribute<float>::CreateLambda([DragIndex]() { return *DragIndex; }) : TAttribute<float>(1.0f));
        Data.StringChanged.Add(TEXT("select"), FCkUiOnStringChanged::CreateLambda([&Selected](const FString& Key)
        { Selected.Add(Key); }));
        Data.StringChanged.Add(TEXT("drop"), FCkUiOnStringChanged::CreateLambda([&Dropped](const FString& Key)
        { Dropped.Add(Key); }));
        return Data;
    }

    auto Markup() -> FString
    {
        return TEXT("<ui version=\"1\"><region name=\"main\"><row id=\"root\">"
            "<item-slot id=\"source\" key-bind=\"source-key\" kind-bind=\"source-kind\" rarity-bind=\"slot-tier\" select=\"select\" drag-label-bind=\"drag-label\" drag-image-bind=\"drag-atlas\" drag-index-bind=\"drag-index\" drag-columns=\"4\" drag-rows=\"3\">"
            "<slot name=\"content\"><text id=\"source-label\">Sword</text></slot>"
            "<slot name=\"tooltip\"><column id=\"comparison\"><text id=\"comparison-title\">Compare damage</text></column></slot>"
            "</item-slot>"
            "<item-slot id=\"weapon\" key-bind=\"target-key\" kind-bind=\"weapon-kind\" accepts=\"weapon\" select=\"select\" drop=\"drop\">"
            "<slot name=\"content\"><text id=\"weapon-label\">Weapon</text></slot></item-slot>"
            "<item-slot id=\"armor\" key-bind=\"target-key\" kind-bind=\"armor-kind\" accepts=\"armor\" select=\"select\" drop=\"drop\">"
            "<slot name=\"content\"><text id=\"armor-label\">Armor</text></slot></item-slot>"
            "</row></region></ui>");
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiItemSlotInteraction,
    "Ck.UiAuthoring.ItemSlot.DragEligibilityAndLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkUiItemSlotInteraction::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_item_slot;
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("item slot registration succeeds"), FCkUiItemSlot::RegisterItemSlotUiControl(Registry).Succeeded))
    { return false; }
    TestFalse(TEXT("duplicate registration rejects"), FCkUiItemSlot::RegisterItemSlotUiControl(Registry).Succeeded);
    FString SourceKey = TEXT("iron-sword");
    FString SourceKind = TEXT("weapon");
    TArray<FString> Selected;
    TArray<FString> Dropped;
    float DragIndex = 1.0f;
    auto View = FCkUiView::Create({}, {}, {}, {}, MakeData(SourceKey, SourceKind, Selected, Dropped, &DragIndex), Registry.CreateSnapshot());
    const auto Region = View->GetRegion(TEXT("main"));
    const auto Loaded = View->TryReload(Markup(), TEXT(""), TEXT("ItemSlot"));
    if (!TestTrue(TEXT("authored item slots load"), Loaded.Succeeded))
    { AddError(FString::Join(Loaded.Errors, TEXT("\n"))); return false; }
    const auto Source = Find(Region, TEXT("source"));
    const auto Weapon = Find(Region, TEXT("weapon"));
    const auto Armor = Find(Region, TEXT("armor"));
    if (!TestTrue(TEXT("native item slots mount"), Source.IsValid() && Weapon.IsValid() && Armor.IsValid()))
    { return false; }
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Item slot test requires Slate.")); return false; }
    FSlateApplication& Slate = FSlateApplication::Get();
    FWindowScope Scope{Slate, {}};
    Scope.Window = SNew(SWindow).ClientSize(FVector2D{320.0f, 220.0f}).CreateTitleBar(false)[Region];
    Slate.AddWindow(Scope.Window.ToSharedRef(), true);
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    TestEqual(TEXT("source key reads live binding"), Source->GetItemKey(), SourceKey);
    TestTrue(TEXT("rich authored tooltip is mounted"), Source->HasAuthoredTooltip());
    TestEqual(TEXT("closed tooltip has one root-owned authored subtree"),
        CountTagged(Region, TEXT("comparison")), 1);
    const auto PointerEvent = Pointer(Slate, Source.ToSharedRef());
    Source->OnMouseEnter(Source->GetCachedGeometry(), PointerEvent);
    Slate.Tick();
    const FVector2D TooltipMax = Source->GetTooltipMaxSize();
    TestEqual(TEXT("open tooltip still has one root-owned authored subtree"),
        CountTagged(Region, TEXT("comparison")), 1);
    const FVector2D WindowSize = Scope.Window->GetClientSizeInScreen();
    TestTrue(TEXT("tooltip opens inside owning window with bounded content"), Source->GetIsTooltipOpen()
        && TooltipMax.X > 0.0f && TooltipMax.X < WindowSize.X
        && TooltipMax.Y > 0.0f && TooltipMax.Y < WindowSize.Y);
    FSlateRect PopupBounds;
    FSlateRect ViewportBounds;
    if (TestTrue(TEXT("tooltip has arranged popup geometry"),
        Source->TryGetTooltipScreenBounds(PopupBounds, ViewportBounds)))
    {
        TestTrue(TEXT("actual tooltip remains within owning window"),
            PopupBounds.Left >= ViewportBounds.Left && PopupBounds.Top >= ViewportBounds.Top
            && PopupBounds.Right <= ViewportBounds.Right && PopupBounds.Bottom <= ViewportBounds.Bottom);
    }
    TestTrue(TEXT("compatible reload accepts open tooltip mount"),
        View->TryReload(Markup(), TEXT(""), TEXT("ItemSlotOpenTooltipReload")).Succeeded);
    TestFalse(TEXT("accepted reload closes tooltip and parks its mount"), Source->GetIsTooltipOpen());
    TestEqual(TEXT("open reload parks exactly one authored subtree"),
        CountTagged(Region, TEXT("comparison")), 1);
    TestTrue(TEXT("compatible reload accepts closed tooltip mount"),
        View->TryReload(Markup(), TEXT(""), TEXT("ItemSlotClosedTooltipReload")).Succeeded);
    TestEqual(TEXT("closed reload keeps exactly one authored subtree"),
        CountTagged(Region, TEXT("comparison")), 1);
    Source->OnMouseLeave(PointerEvent);
    TestFalse(TEXT("tooltip closes on hover exit"), Source->GetIsTooltipOpen());
    Source->SimulateClick();
    TestTrue(TEXT("select emits the source key"), Selected == TArray<FString>{SourceKey});
    const FReply DragReply = Source->OnDragDetected(Source->GetCachedGeometry(), PointerEvent);
    const auto Drag = DragReply.IsEventHandled()
        ? StaticCastSharedPtr<FCkUiItemSlotDragOperation>(DragReply.GetDragDropContent()) : nullptr;
    if (!TestTrue(TEXT("native source begins a keyed drag"), Drag.IsValid())) { return false; }
    TestEqual(TEXT("drag decorator snapshots readable label"), Drag->GetDragLabel(), FString(TEXT("Iron Sword")));
    TestTrue(TEXT("drag decorator mounts image and label"), Drag->GetDragImageBrush() != nullptr
        && Drag->GetDefaultDecorator().IsValid());
    if (const FSlateBrush* Brush = Drag->GetDragImageBrush())
    {
        TestEqual(TEXT("drag thumbnail width uses atlas cell"), Brush->ImageSize.X, 100.0f);
        TestEqual(TEXT("drag thumbnail height uses atlas cell"), Brush->ImageSize.Y, 100.0f);
        const FBox2f UV = Brush->GetUVRegion();
        TestTrue(TEXT("drag thumbnail snapshots second atlas cell UV"),
            UV.Min.Equals(FVector2f{0.25f, 0.0f})
            && UV.Max.Equals(FVector2f{0.5f, 1.0f / 3.0f}));
    }
    DragIndex = -1.0f;
    const auto InvalidVisualDrag = Source->TryBeginDrag();
    TestTrue(TEXT("invalid dynamic atlas index keeps keyed drag and label fallback"),
        InvalidVisualDrag.IsValid() && InvalidVisualDrag->GetDragImageBrush() == nullptr
        && InvalidVisualDrag->GetDragLabel() == TEXT("Iron Sword"));
    DragIndex = 1.0f;
    for (const float BadWidth : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()})
    {
        DragAtlasBrush().ImageSize = FVector2D{BadWidth, 300.0f};
        const auto BadBrushDrag = Source->TryBeginDrag();
        TestTrue(TEXT("invalid atlas width keeps label-only keyed drag"),
            BadBrushDrag.IsValid() && BadBrushDrag->GetDragImageBrush() == nullptr
            && BadBrushDrag->GetDragLabel() == TEXT("Iron Sword"));
    }
    DragAtlasBrush().ImageSize = FVector2D{400.0f, 0.0f};
    const auto BadHeightDrag = Source->TryBeginDrag();
    TestTrue(TEXT("invalid atlas height keeps label-only keyed drag"),
        BadHeightDrag.IsValid() && BadHeightDrag->GetDragImageBrush() == nullptr);
    DragAtlasBrush().ImageSize = FVector2D{400.0f, 300.0f};
    TestTrue(TEXT("matching equipment target highlights valid"), Weapon->CanAcceptDrag(Drag));
    const auto SelfDrag = Weapon->TryBeginDrag();
    TestTrue(TEXT("occupied equipment slot can originate drag"), SelfDrag.IsValid());
    if (SelfDrag.IsValid())
    { TestEqual(TEXT("unconfigured drag retains stable key label"), SelfDrag->GetDragLabel(), FString(TEXT("equipped-sword"))); }
    TestFalse(TEXT("same slot never highlights as drop target"), Weapon->CanAcceptDrag(SelfDrag));
    TestFalse(TEXT("same slot cannot dispatch a drop"), Weapon->TryAcceptDrag(SelfDrag));
    TestTrue(TEXT("self drop leaves consumer untouched"), Dropped.IsEmpty());
    TestFalse(TEXT("wrong equipment category rejects"), Armor->CanAcceptDrag(Drag));
    const FDragDropEvent DragEvent(PointerEvent, Drag);
    Armor->OnDragEnter(Armor->GetCachedGeometry(), DragEvent);
    TestFalse(TEXT("wrong category native drop does not dispatch"),
        Armor->OnDrop(Armor->GetCachedGeometry(), DragEvent).IsEventHandled());
    Weapon->OnDragEnter(Weapon->GetCachedGeometry(), DragEvent);
    TestTrue(TEXT("matching target native drop accepts once"),
        Weapon->OnDrop(Weapon->GetCachedGeometry(), DragEvent).IsEventHandled());
    TestFalse(TEXT("same native drag cannot dispatch twice"),
        Weapon->OnDrop(Weapon->GetCachedGeometry(), DragEvent).IsEventHandled());
    TestTrue(TEXT("accepted drop submits only a key"), Dropped == TArray<FString>{SourceKey});

    FString OtherKey = TEXT("other-sword");
    FString OtherKind = TEXT("weapon");
    TArray<FString> OtherSelected;
    TArray<FString> OtherDropped;
    auto Other = FCkUiView::Create({}, {}, {}, {}, MakeData(OtherKey, OtherKind, OtherSelected, OtherDropped), Registry.CreateSnapshot());
    const auto OtherRegion = Other->GetRegion(TEXT("main"));
    if (!TestTrue(TEXT("unrelated authored view loads"), Other->TryReload(Markup(), TEXT("")).Succeeded))
    { return false; }
    const auto OtherSource = Find(OtherRegion, TEXT("source"));
    if (!TestTrue(TEXT("unrelated source mounts"), OtherSource.IsValid())) { return false; }
    TestFalse(TEXT("unrelated root drag domain rejects"), Weapon->TryAcceptDrag(OtherSource->TryBeginDrag()));

    const auto ChangedDrag = Source->TryBeginDrag();
    SourceKey = TEXT("oak-bow");
    TestFalse(TEXT("changed source key invalidates pending drag"), Weapon->TryAcceptDrag(ChangedDrag));
    SourceKey = TEXT("iron-sword");
    const auto ReloadDrag = Source->TryBeginDrag();
    const int64 Revision = View->GetRevision();
    const auto MissingAtlasDimension = View->TryReload(
        Markup().Replace(TEXT(" drag-rows=\"3\""), TEXT("")), TEXT(""), TEXT("ItemSlotPartialDragAtlas"));
    TestFalse(TEXT("partial drag atlas rejects before publication"), MissingAtlasDimension.Succeeded);
    TestEqual(TEXT("partial atlas rejection preserves revision"), View->GetRevision(), Revision);
    TestTrue(TEXT("partial atlas rejection retains pending drag"), Weapon->CanAcceptDrag(ReloadDrag));
    const auto Rejected = View->TryReload(Markup().Replace(TEXT("select=\"select\""), TEXT("select=\"missing\"")),
        TEXT(""), TEXT("ItemSlotRejected"));
    TestFalse(TEXT("missing callback rejects before publication"), Rejected.Succeeded);
    TestEqual(TEXT("rejected reload preserves revision"), View->GetRevision(), Revision);
    TestTrue(TEXT("rejected reload retains pending proposal"), Weapon->CanAcceptDrag(ReloadDrag));
    TestTrue(TEXT("compatible reload succeeds"), View->TryReload(Markup(), TEXT(""), TEXT("ItemSlotReload")).Succeeded);
    TestFalse(TEXT("accepted reload invalidates pending drag"), Weapon->TryAcceptDrag(ReloadDrag));
    const auto ReleaseDrag = Source->TryBeginDrag();
    View->ReleaseOwnerInteractions();
    TestFalse(TEXT("released source cannot submit drag"), Weapon->TryAcceptDrag(ReleaseDrag));
    TestFalse(TEXT("released slot cannot begin drag"), Source->TryBeginDrag().IsValid());
    TestTrue(TEXT("rejected and retired proposals have no extra publication"), Dropped == TArray<FString>{TEXT("iron-sword")});

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiItemSlotRepeatDomain,
    "Ck.UiAuthoring.ItemSlot.RepeatSharesRootDomain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkUiItemSlotRepeatDomain::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_item_slot;
    TSharedPtr<FCkUiCollection> Items;
    if (!TestTrue(TEXT("repeat collection creates"), FCkUiCollection::TryCreate(
        {{TEXT("keytext"), ECkUiFieldKind::Text}, {TEXT("kindtext"), ECkUiFieldKind::Text},
            {TEXT("quantity"), ECkUiFieldKind::Text}}, Items).Succeeded))
    { return false; }
    FCkUiRecordData Record;
    Record.Key = TEXT("iron-sword");
    Record.Fields.Add(TEXT("keytext"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = FText::FromString(Record.Key)});
    Record.Fields.Add(TEXT("kindtext"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = FText::FromString(TEXT("weapon"))});
    Record.Fields.Add(TEXT("quantity"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = FText::FromString(TEXT("1"))});
    if (!TestTrue(TEXT("repeat record publishes"), Items->TrySetRecords({Record}).Succeeded)) { return false; }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("item slot registers"), FCkUiItemSlot::RegisterItemSlotUiControl(Registry).Succeeded))
    { return false; }
    FString Key = TEXT("unused");
    FString Kind = TEXT("weapon");
    TArray<FString> Selected;
    TArray<FString> Dropped;
    auto Data = MakeData(Key, Kind, Selected, Dropped);
    Data.Collections.Add(TEXT("items"), Items);
    auto View = FCkUiView::Create({}, {}, {}, {}, MoveTemp(Data), Registry.CreateSnapshot());
    const auto Region = View->GetRegion(TEXT("main"));
    const FString Html = TEXT("<ui version=\"1\"><region name=\"main\"><row id=\"root\">"
        "<repeat id=\"items-repeat\" bind=\"items\"><item-slot id=\"source\" key-field=\"keytext\" kind-field=\"kindtext\" select=\"select\">"
        "<slot name=\"content\"><text id=\"item-label\">Sword</text></slot></item-slot></repeat>"
        "<item-slot id=\"target\" key-bind=\"target-key\" kind-bind=\"weapon-kind\" accepts=\"weapon\" select=\"select\" drop=\"drop\">"
        "<slot name=\"content\"><text id=\"target-label\">Weapon</text></slot></item-slot>"
        "</row></region></ui>");
    const auto Loaded = View->TryReload(Html, TEXT(""));
    if (!TestTrue(TEXT("repeat and target load"), Loaded.Succeeded))
    { AddError(FString::Join(Loaded.Errors, TEXT("\n"))); return false; }
    const auto Repeat = View->GetRepeat(TEXT("items-repeat"));
    if (!TestTrue(TEXT("repeat mounts"), Repeat.IsValid() && Repeat->GetItemWidget(TEXT("iron-sword")).IsValid()))
    { return false; }
    const auto Source = Find(Repeat->GetItemWidget(TEXT("iron-sword")).ToSharedRef(), TEXT("source"));
    const auto Target = Find(Region, TEXT("target"));
    if (!TestTrue(TEXT("nested and root slots mount"), Source.IsValid() && Target.IsValid())) { return false; }
    TestTrue(TEXT("repeat child uses root domain for valid drop"), Target->TryAcceptDrag(Source->TryBeginDrag()));
    TestTrue(TEXT("drop reports repeated source key"), Dropped == TArray<FString>{TEXT("iron-sword")});
    const auto Pending = Source->TryBeginDrag();
    Record.Fields.FindChecked(TEXT("quantity")).Text = FText::FromString(TEXT("2"));
    if (!TestTrue(TEXT("record mutation publishes before presenter refresh"), Items->TrySetRecords({Record}).Succeeded))
    { return false; }
    TestFalse(TEXT("pending drag rejects changed collection revision before repeat refresh"),
        Target->TryAcceptDrag(Pending));
    TestTrue(TEXT("stale publication does not emit another drop"), Dropped == TArray<FString>{TEXT("iron-sword")});
    return true;
}

#endif

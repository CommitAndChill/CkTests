#include "CkTests/UI/CkPowerWheelProof.h"
#include "UI/CkPowerWheelProofModel.h"
#include "UI/CkCombatHudReplicaArt.h"
#include "UI/CkUiReplicaPresentation.h"
#include "CkSlateLayout/CkUiKeyHint.h"

#include "CkSlateLayout/CkUiRadialSelector.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSurface_Widget.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"
#include "CkUI/Layout/CkUI_LayoutConfigAsset.h"
#include "CkUI/Layout/CkUI_LayerStack.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Layout/Children.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

namespace ck_power_wheel_host
{
    auto Layer() -> FGameplayTag
    { return FGameplayTag::RequestGameplayTag(TEXT("CkTests.UI.Layer.PauseMenuProof")); }

    auto FindNode(const FCkUiNode& InNode, const FString& InId) -> const FCkUiNode*
    {
        if (InNode.Id == InId) { return &InNode; }
        for (const auto& Child : InNode.Children)
        { if (const auto* Found = FindNode(Child, InId)) { return Found; } }
        return nullptr;
    }

    auto FindWidget(const TSharedRef<SWidget>& InRoot, FName InId) -> TSharedPtr<SWidget>
    {
        if (InRoot->GetTag() == InId && InRoot->SupportsKeyboardFocus()) { return InRoot; }
        auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        { if (auto Found = FindWidget(Children->GetChildAt(Index), InId)) { return Found; } }
        return {};
    }

    auto Resource(const TCHAR* InFile) -> FString
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
        return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(),
            TEXT("Resources/UiAuthoring/PowerWheelProof"), InFile) : FString{};
    }

    auto ActorName(int32 InActor) -> FString
    { return InActor == 0 ? TEXT("SHEPARD") : InActor == 1 ? TEXT("GARRUS") : TEXT("LIARA"); }
}

// --------------------------------------------------------------------------------------------------------------------

class FCkPowerWheelInput final : public IInputProcessor, public TSharedFromThis<FCkPowerWheelInput>
{
public:
    explicit FCkPowerWheelInput(UCk_PowerWheelProof_UE* InHost) : _Host(InHost) {}

    auto Tick(float, FSlateApplication& InSlate, TSharedRef<ICursor>) -> void override
    {
        if (NOT _Host.IsValid()) { InSlate.UnregisterInputPreProcessor(AsShared()); return; }
        if (_Host->IsWheelOpen() && NOT InSlate.IsActive()) { _Host->Request_Cancel(); }
    }

    auto HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InEvent) -> bool override
    {
        if (NOT _Host.IsValid() || InEvent.GetUserIndex() != _Host->_UserIndex) { return false; }
        const auto Key = InEvent.GetKey();
        if (_Host->IsWheelOpen())
        {
            _HeldKeys.Add(Key);
            if (Key == EKeys::Escape || Key == EKeys::Tab) { _Host->Request_Cancel(); return true; }
            if (Key == EKeys::SpaceBar) { if (NOT InEvent.IsRepeat()) { _Host->Request_Deploy(); } return true; }
            if (Key == EKeys::Two) { if (NOT InEvent.IsRepeat()) { _Host->Request_ToggleTarget(); } return true; }
            return Key == _Host->_HoldKey;
        }
        if (InEvent.IsRepeat() && _HeldKeys.Contains(Key)) { return true; }
        _HeldKeys.Remove(Key);
        const auto Hold = Key == EKeys::LeftShift || Key == EKeys::RightShift || Key == EKeys::Gamepad_RightShoulder;
        if (Hold && NOT InEvent.IsRepeat() && _Host->CanOpenFromInput() && _Host->DoOpen(Key))
        { _HeldKeys.Add(Key); return true; }
        return false;
    }

    auto HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& InEvent) -> bool override
    {
        if (NOT _Host.IsValid() || InEvent.GetUserIndex() != _Host->_UserIndex) { return false; }
        if (_Host->IsWheelOpen() && InEvent.GetKey() == _Host->_HoldKey)
        { _HeldKeys.Remove(InEvent.GetKey()); _Host->Request_Deploy(); return true; }
        return _HeldKeys.Remove(InEvent.GetKey()) > 0;
    }

    auto HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& InEvent) -> bool override
    {
        if (NOT _Host.IsValid() || InEvent.GetUserIndex() != _Host->_UserIndex) { return false; }
        auto& Keys = _HeldPointers.FindOrAdd(InEvent.GetPointerIndex());
        if (_Host->IsWheelOpen()) { Keys.Add(InEvent.GetEffectingButton()); }
        else { Keys.Remove(InEvent.GetEffectingButton()); }
        return false;
    }

    auto HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& InEvent) -> bool override
    {
        if (NOT _Host.IsValid() || InEvent.GetUserIndex() != _Host->_UserIndex) { return false; }
        auto* Keys = _HeldPointers.Find(InEvent.GetPointerIndex());
        const auto WasHeld = Keys != nullptr && Keys->Remove(InEvent.GetEffectingButton()) > 0;
        return WasHeld && NOT _Host->IsWheelOpen();
    }

private:
    TWeakObjectPtr<UCk_PowerWheelProof_UE> _Host;
    TSet<FKey> _HeldKeys;
    TMap<uint32, TSet<FKey>> _HeldPointers;
};

// --------------------------------------------------------------------------------------------------------------------

UCk_PowerWheelProof_UE::UCk_PowerWheelProof_UE(const FObjectInitializer& InInitializer)
    : Super(InInitializer)
{ SetIsFocusable(true); }

auto UCk_PowerWheelProof_UE::Create(APlayerController* InController) -> UCk_PowerWheelProof_UE*
{
    const auto ValidController = IsValid(InController) && InController->IsLocalController();
    CK_ENSURE_IF_NOT(ValidController, TEXT("Power wheel requires a local controller")) {}
    if (NOT ValidController) { return nullptr; }
    const auto ValidContext = IsValid(InController->GetWorld()) && IsValid(InController->GetLocalPlayer())
        && FSlateApplication::IsInitialized();
    CK_ENSURE_IF_NOT(ValidContext, TEXT("Power wheel requires a world, local player and Slate")) {}
    if (NOT ValidContext) { return nullptr; }
    auto* Host = CreateWidget<UCk_PowerWheelProof_UE>(InController);
    if (NOT IsValid(Host)) { return nullptr; }
    Host->_Model = MakeShared<FCkPowerWheelProofModel>();
    const auto SlateUser = InController->GetLocalPlayer()->GetSlateUser();
    if (NOT SlateUser.IsValid()) { return nullptr; }
    Host->_UserIndex = SlateUser->GetUserIndex();
    Host->_Alive = true;
    Host->_Input = MakeShared<FCkPowerWheelInput>(Host);
    FSlateApplication::Get().RegisterInputPreProcessor(Host->_Input, 0);
    Host->_TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(Host, &ThisClass::DoTick));
    return Host;
}

auto UCk_PowerWheelProof_UE::CanOpenFromInput() const -> bool
{
    const auto* World = GetWorld();
    const auto* ViewportClient = IsValid(World) ? World->GetGameViewport() : nullptr;
    const auto ValidContext = _Alive && NOT _Open && IsValid(ViewportClient)
        && FSlateApplication::IsInitialized();
    if (NOT ValidContext) { return false; }
    const auto Viewport = ViewportClient->GetGameViewportWidget();
    if (NOT Viewport.IsValid() || World->IsPaused()) { return false; }
    auto& Slate = FSlateApplication::Get();
    const auto Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
    return Slate.IsActive() && Window.IsValid() && Window->IsActive()
        && Slate.GetUserFocusedWidget(_UserIndex).Get() == Viewport.Get();
}

auto UCk_PowerWheelProof_UE::Request_Open() -> bool { return DoOpen(FKey{}); }

auto UCk_PowerWheelProof_UE::DoOpen(FKey InHoldKey) -> bool
{
    if (NOT _Alive || _Open || NOT _Model.IsValid()) { return false; }
    auto* Controller = GetOwningPlayer();
    if (NOT IsValid(Controller) || NOT IsValid(GetWorld()) || NOT IsValid(GetOwningLocalPlayer())) { return false; }
    auto* Subsystem = GetOwningLocalPlayer()->GetSubsystem<UCk_UI_Layout_Subsystem_UE>();
    if (NOT IsValid(Subsystem)) { return false; }
    const auto HadLayout = Subsystem->Has_Layout();
    if (HadLayout)
    {
        const auto* ExistingLayer = Subsystem->Get_Layer(ck_power_wheel_host::Layer());
        if (NOT IsValid(ExistingLayer) || ExistingLayer->GetActiveWidget() != nullptr)
        { _SourceError = TEXT("The power-wheel layer is unavailable or occupied."); return false; }
    }
    auto* Config = LoadObject<UCk_UI_LayoutConfigAsset_UE>(nullptr,
        TEXT("/Script/AngelscriptAssets.CkTests_PauseMenuProof_Layout"));
    if (NOT IsValid(Config) || NOT Config->Get_LayerConfig(ck_power_wheel_host::Layer()).IsSet()) { return false; }
    if (NOT _Model->TryBegin()) { return false; }
    if (NOT DoPrepare()) { _Model->RequestCancel(); DoClosePresentation(); return false; }
    if (IsValid(_Surface) && NOT _Surface->TryConfigure(_View, TEXT("main")))
    { _Model->RequestCancel(); DoClosePresentation(); return false; }
    // Keep the prepared Slate wrapper alive until the layer takes ownership.
    const TSharedRef<SWidget> PreparedSlateRoot = TakeWidget();
    if (NOT IsValid(_Surface) || NOT _Surface->HasConfiguredView() || NOT _Surface->GetCachedWidget().IsValid())
    {
        _SourceError = TEXT("The native authored surface could not be retained before mounting.");
        _Model->RequestCancel(); DoClosePresentation(); return false;
    }
    if (NOT HadLayout) { Subsystem->CreateLayout(Config); }
    if (NOT Subsystem->HasLayer(ck_power_wheel_host::Layer()))
    { _SourceError = TEXT("The power-wheel layer could not be created."); _Model->RequestCancel(); DoClosePresentation(); if (NOT HadLayout) { Subsystem->DestroyLayout(); } return false; }
    _PreviousFocus = FSlateApplication::Get().GetUserFocusedWidget(_UserIndex);
    _HadCursor = Controller->bShowMouseCursor;
    if (NOT GetWorld()->IsPaused())
    {
        _OwnsPause = Controller->SetPause(true);
        if (NOT _OwnsPause)
        { _SourceError = TEXT("The power wheel could not acquire pause."); _Model->RequestCancel(); DoClosePresentation(); if (NOT HadLayout) { Subsystem->DestroyLayout(); } return false; }
    }
    _Open = true;
    _HoldKey = InHoldKey;
    _Status = TEXT("Select a power, then queue it. Release Shift to deploy.");
    Controller->bShowMouseCursor = true;
    auto* Pushed = Subsystem->PushWidgetToLayer_Instance(ck_power_wheel_host::Layer(), this);
    if (Pushed != this || NOT _Open || NOT IsValid(_Surface) || NOT _Surface->HasConfiguredView())
    {
        _SourceError = TEXT("The power wheel was rejected while mounting on its layer.");
        Request_Cancel();
        Subsystem->RemoveWidget(this);
        if (NOT HadLayout) { Subsystem->DestroyLayout(); }
        return false;
    }
    ActivateWidget();
    _OpenElapsed = 0.0f;
    SetRenderOpacity(0.0f);
    _NeedsFocus = NOT DoFocus();
    return true;
}

auto UCk_PowerWheelProof_UE::DoPrepare() -> bool
{
    auto Registry = FCkUiWidgetRegistry{};
    const auto Radial = RegisterRadialUiControls(Registry);
    if (NOT Radial.Succeeded) { _SourceError = FString::Join(Radial.Errors, TEXT("\n")); return false; }
    const auto Art = FCkCombatHudReplicaArt::Register(Registry);
    if (NOT Art.Succeeded) { _SourceError = FString::Join(Art.Errors, TEXT("\n")); return false; }
    const auto Hints = RegisterKeyHintUiControl(Registry);
    if (!Hints.Succeeded) { _SourceError = FString::Join(Hints.Errors, TEXT("\n")); return false; }
    _Registry = Registry.CreateSnapshot();
    const auto Created = FCkUiCollection::TryCreate({
        {TEXT("actor"), ECkUiFieldKind::Text}, {TEXT("glyph"), ECkUiFieldKind::Text},
        {TEXT("label"), ECkUiFieldKind::Text}, {TEXT("angle"), ECkUiFieldKind::Number},
        {TEXT("cooldown"), ECkUiFieldKind::Number}, {TEXT("enabled"), ECkUiFieldKind::Bool},
        {TEXT("queued"), ECkUiFieldKind::Bool}}, _Powers);
    if (NOT Created.Succeeded || NOT DoSync()) { _SourceError = TEXT("Power collection admission failed."); return false; }
    if (NOT IsValid(_PortraitTexture))
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
        if (NOT Plugin.IsValid()) { return false; }
        _PortraitTexture = FImageUtils::ImportFileAsTexture2D(FPaths::Combine(Plugin->GetBaseDir(),
            TEXT("Resources/UiAuthoring/CombatHudProof/Futuristic/squad-portraits.png")));
        if (NOT IsValid(_PortraitTexture)) { _SourceError = TEXT("Squad portrait resource is unavailable."); return false; }
        _Portraits.Reset();
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto Brush = MakeShared<FSlateBrush>();
            Brush->DrawAs = ESlateBrushDrawType::Image;
            Brush->ImageSize = FVector2D{72.0f, 72.0f};
            Brush->SetResourceObject(_PortraitTexture);
            Brush->SetUVRegion(FBox2f{FVector2f{Index * 0.5f, 0.0f}, FVector2f{(Index + 1) * 0.5f, 1.0f}});
            _Portraits.Add(Brush);
        }
    }
    auto Data = FCkUiView::FDataBindings{};
    const auto Weak = TWeakObjectPtr<UCk_PowerWheelProof_UE>{this};
    Data.CanDispatchEvents = TAttribute<bool>::CreateLambda([Weak]() { return Weak.IsValid() && Weak->IsWheelOpen(); });
    Data.SlateUserIndex = _UserIndex;
    Data.Visibility.Add(TEXT("TargetShielded"), TAttribute<bool>::CreateLambda([Weak]()
    { return Weak.IsValid() && Weak->_Model.IsValid() && Weak->_Model->Get_TargetShielded(); }));
    Data.Collections.Add(TEXT("Powers"), _Powers);
    Data.String.Add(TEXT("Selection"), TAttribute<FString>::CreateLambda([Weak]()
    { return Weak.IsValid() ? Weak->_Model->Get_SelectedKey().ToString() : FString{}; }));
    Data.StringChanged.Add(TEXT("SelectPower"), FCkUiOnStringChanged::CreateLambda([Weak](FString Key)
    { if (Weak.IsValid()) { Weak->DoSelect(Key, false); } }));
    Data.StringChanged.Add(TEXT("QueuePower"), FCkUiOnStringChanged::CreateLambda([Weak](FString Key)
    { if (Weak.IsValid()) { Weak->DoSelect(Key, true); } }));
    for (const auto* Name : {TEXT("PowerName"), TEXT("PowerDescription"), TEXT("PowerState"), TEXT("ActorName"),
        TEXT("QueueShepard"), TEXT("QueueGarrus"), TEXT("QueueLiara"), TEXT("TargetState"), TEXT("StatusText")})
    {
        const auto Key = FString{Name};
        Data.Text.Add(Key, TAttribute<FText>::CreateLambda([Weak, Key]()
        { return Weak.IsValid() ? Weak->DoText(Key) : FText{}; }));
    }
    for (int32 Index = 0; Index < 2; ++Index)
    {
        Data.Images.Add(Index == 0 ? TEXT("GarrusPortrait") : TEXT("LiaraPortrait"),
            TAttribute<const FSlateBrush*>::CreateLambda([Weak, Index]()
            { return Weak.IsValid() && Weak->_Portraits.IsValidIndex(Index) ? Weak->_Portraits[Index].Get() : nullptr; }));
    }
    TSharedPtr<const FCkUiFontRegistrySnapshot> Fonts;
    if (!ck_ui_replica_presentation::TryLoad(TEXT("MassEffect"), Data, Fonts, _SourceError))
    { return false; }
    _View = FCkUiView::Create({}, {}, {}, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 20), MoveTemp(Data), _Registry, Fonts);
    _View->GetRegion(TEXT("main"));
    _LastMarkup.Reset();
    _LastStyle.Reset();
    return DoReload();
}

auto UCk_PowerWheelProof_UE::DoSync() -> bool
{
    if (NOT _Model.IsValid() || NOT _Powers.IsValid()) { return false; }
    auto Records = TArray<FCkUiRecordData>{};
    for (const auto& Power : _Model->Get_Powers())
    {
        auto Record = FCkUiRecordData{};
        Record.Key = Power.Key.ToString();
        for (const auto& Pair : TMap<FString, FString>{{TEXT("actor"), ck_power_wheel_host::ActorName(Power.Actor)},
            {TEXT("glyph"), Power.Glyph}, {TEXT("label"), Power.Label}})
        { auto Field = FCkUiFieldValue{}; Field.Kind = ECkUiFieldKind::Text; Field.Text = FText::FromString(Pair.Value); Record.Fields.Add(Pair.Key, Field); }
        for (const auto& Pair : TMap<FString, float>{{TEXT("angle"), Power.Angle},
            {TEXT("cooldown"), _Model->Get_CooldownFraction(Power.Key)}})
        { auto Field = FCkUiFieldValue{}; Field.Kind = ECkUiFieldKind::Number; Field.Number = Pair.Value; Record.Fields.Add(Pair.Key, Field); }
        for (const auto& Pair : TMap<FString, bool>{{TEXT("enabled"), _Model->IsUsable(Power.Key)},
            {TEXT("queued"), _Model->Get_QueuedKey(Power.Actor) == Power.Key}})
        { auto Field = FCkUiFieldValue{}; Field.Kind = ECkUiFieldKind::Bool; Field.Bool = Pair.Value; Record.Fields.Add(Pair.Key, Field); }
        Records.Add(MoveTemp(Record));
    }
    return _Powers->TrySetRecords(MoveTemp(Records)).Succeeded;
}

auto UCk_PowerWheelProof_UE::DoText(const FString& InName) const -> FText
{
    if (NOT _Model.IsValid()) { return {}; }
    if (InName == TEXT("StatusText")) { return FText::FromString(_Status); }
    if (InName == TEXT("TargetState")) { return FText::FromString(_Model->Get_TargetShielded() ? TEXT("CERBERUS CENTURION  /  SHIELDS") : TEXT("CERBERUS CENTURION  /  ARMOR")); }
    const auto* Selected = _Model->Get_Powers().FindByPredicate([this](const FCkPowerWheelPower& Power)
    { return Power.Key == _Model->Get_SelectedKey(); });
    if (Selected != nullptr)
    {
        if (InName == TEXT("PowerName")) { return FText::FromString(Selected->Label.ToUpper()); }
        if (InName == TEXT("PowerDescription")) { return FText::FromString(Selected->Description); }
        if (InName == TEXT("ActorName")) { return FText::FromString(ck_power_wheel_host::ActorName(Selected->Actor)); }
        if (InName == TEXT("PowerState"))
        { return FText::FromString(_Model->Get_QueuedKey(Selected->Actor) == Selected->Key ? TEXT("QUEUED") : _Model->IsUsable(Selected->Key) ? TEXT("READY  /  CLICK OR ENTER TO QUEUE") : _Model->Get_BlockReason(Selected->Key)); }
    }
    int32 Actor = InName == TEXT("QueueShepard") ? 0 : InName == TEXT("QueueGarrus") ? 1 : InName == TEXT("QueueLiara") ? 2 : INDEX_NONE;
    if (Actor != INDEX_NONE)
    {
        const auto Key = _Model->Get_QueuedKey(Actor);
        const auto* Queued = _Model->Get_Powers().FindByPredicate([Key](const FCkPowerWheelPower& Power) { return Power.Key == Key; });
        return FText::FromString(Queued != nullptr ? Queued->Label.ToUpper() : TEXT("NO POWER QUEUED"));
    }
    return {};
}

auto UCk_PowerWheelProof_UE::DoSelect(const FString& InKey, bool InQueue) -> void
{
    if (NOT IsWheelOpen()) { return; }
    const auto Key = FName{*InKey};
    if (NOT _Model->TrySelect(Key)) { return; }
    if (InQueue)
    { _Status = _Model->TryQueue(Key) ? TEXT("Power queued. Release Shift to deploy your squad.") : _Model->Get_BlockReason(Key); }
    DoSync();
}

auto UCk_PowerWheelProof_UE::Request_ApplySource(const FString& InMarkup, const FString& InStyle) -> bool
{
    if (NOT _View.IsValid() || _View->IsOwnerReleased()) { return false; }
    auto Document = FCkUiDocument{};
    const auto Parsed = FCkUiDocumentParser::TryParse(InMarkup, InStyle, {}, Document, TEXT("PowerWheelProof"), _Registry);
    if (NOT Parsed.Succeeded) { _SourceError = FString::Join(Parsed.Errors, TEXT("\n")); return false; }
    const auto* Main = Document.Regions.Find(TEXT("main"));
    const auto* Wheel = Main != nullptr ? ck_power_wheel_host::FindNode(*Main, TEXT("power-wheel")) : nullptr;
    if (Document.Regions.Num() != 1 || Wheel == nullptr || Wheel->CustomTag != TEXT("radial-selector"))
    { _SourceError = TEXT("Wheel requires exactly region main and radial-selector power-wheel."); return false; }
    for (const auto& Pair : TMap<FString, FString>{{TEXT("items"), TEXT("Powers")}, {TEXT("selection"), TEXT("Selection")},
        {TEXT("select"), TEXT("SelectPower")}, {TEXT("activate"), TEXT("QueuePower")}})
    {
        const auto* Property = Wheel->CustomProperties.Find(Pair.Key);
        if (Property == nullptr || Property->Name != Pair.Value)
        { _SourceError = FString::Printf(TEXT("Wheel requires binding %s=%s."), *Pair.Key, *Pair.Value); return false; }
    }
    for (const auto& Pair : TMap<FString, FString>{{TEXT("power-name"), TEXT("PowerName")}, {TEXT("power-description"), TEXT("PowerDescription")},
        {TEXT("power-state"), TEXT("PowerState")}, {TEXT("target-state"), TEXT("TargetState")}})
    {
        const auto* Text = ck_power_wheel_host::FindNode(*Main, Pair.Key);
        if (Text == nullptr || Text->Kind != ECkUiNodeKind::Text || Text->Binding != Pair.Value)
        { _SourceError = TEXT("Wheel requires its bound description and target labels."); return false; }
    }
    const auto Result = _View->TryReload(InMarkup, InStyle, TEXT("PowerWheelProof"));
    _SourceError = Result.Succeeded ? FString{} : FString::Join(Result.Errors, TEXT("\n"));
    return Result.Succeeded;
}

auto UCk_PowerWheelProof_UE::DoReload() -> bool
{
    auto Markup = FString{};
    auto Style = FString{};
    if (NOT FFileHelper::LoadFileToString(Markup, *ck_power_wheel_host::Resource(TEXT("PowerWheelProof.ui.html")))
        || NOT FFileHelper::LoadFileToString(Style, *ck_power_wheel_host::Resource(TEXT("PowerWheelProof.ui.css"))))
    { _SourceError = TEXT("Both power-wheel source files are required."); return false; }
    if (Markup == _LastMarkup && Style == _LastStyle) { return _SourceError.IsEmpty(); }
    _LastMarkup = Markup;
    _LastStyle = Style;
    return Request_ApplySource(Markup, Style);
}

auto UCk_PowerWheelProof_UE::RebuildWidget() -> TSharedRef<SWidget>
{
    if (NOT _View.IsValid()) { return SNullWidget::NullWidget; }
    if (NOT IsValid(WidgetTree)) { WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree")); }
    if (NOT IsValid(WidgetTree->RootWidget))
    {
        auto* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PowerWheelOverlay"));
        WidgetTree->RootWidget = Root;
        auto Fill = [](UOverlaySlot* InSlot) { InSlot->SetHorizontalAlignment(HAlign_Fill); InSlot->SetVerticalAlignment(VAlign_Fill); };
        auto* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CombatDimmer"));
        Dim->SetBrushColor(FLinearColor{0.002f, 0.012f, 0.026f, 0.46f});
        Dim->SetVisibility(ESlateVisibility::HitTestInvisible);
        Fill(Root->AddChildToOverlay(Dim));
        auto* Safe = WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(), TEXT("SafeZone"));
        Fill(Root->AddChildToOverlay(Safe));
        auto* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("ReferenceScale"));
        Scale->SetStretch(EStretch::ScaleToFit);
        Safe->AddChild(Scale);
        auto* Reference = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ReferenceViewport"));
        Reference->SetWidthOverride(1920.0f);
        Reference->SetHeightOverride(1080.0f);
        Scale->AddChild(Reference);
        _Surface = WidgetTree->ConstructWidget<UCk_UI_AuthoredSurface_UE>(UCk_UI_AuthoredSurface_UE::StaticClass(), TEXT("AuthoredWheel"));
        Reference->AddChild(_Surface);
    }
    const auto Configured = IsValid(_Surface) && _Surface->TryConfigure(_View, TEXT("main"));
    CK_ENSURE_IF_NOT(Configured, TEXT("Power wheel could not mount its authored view")) {}
    if (NOT Configured) { return SNullWidget::NullWidget; }
    return Super::RebuildWidget();
}

auto UCk_PowerWheelProof_UE::DoFocus() -> bool
{
    if (NOT _View.IsValid() || NOT _Open) { return false; }
    const auto Wheel = ck_power_wheel_host::FindWidget(_View->GetRegion(TEXT("main")), TEXT("power-wheel"));
    if (NOT Wheel.IsValid()) { return false; }
    auto& Slate = FSlateApplication::Get();
    Slate.SetUserFocus(_UserIndex, Wheel, EFocusCause::SetDirectly);
    return Slate.GetUserFocusedWidget(_UserIndex) == Wheel;
}

auto UCk_PowerWheelProof_UE::DoTick(float InDeltaTime) -> bool
{
    if (NOT _Alive || NOT IsValid(GetOwningPlayer()) || NOT IsValid(GetWorld())) { Request_Shutdown(); return false; }
    if (NOT FMath::IsFinite(InDeltaTime) || InDeltaTime < 0.0f) { return true; }
    if (NOT GetWorld()->IsPaused()) { _Model->Advance(InDeltaTime); }
    if (_Open)
    {
        _OpenElapsed += InDeltaTime;
        SetRenderOpacity(FMath::Clamp(_OpenElapsed / 0.14f, 0.0f, 1.0f));
        if (_NeedsFocus) { _NeedsFocus = NOT DoFocus(); }
        _PollRemaining -= InDeltaTime;
        if (_PollRemaining <= 0.0f) { _PollRemaining = 0.5f; DoReload(); }
    }
    return true;
}

auto UCk_PowerWheelProof_UE::Request_Deploy() -> bool
{
    if (NOT IsWheelOpen()) { return false; }
    auto Commands = TArray<FCkPowerWheelCommand>{};
    const auto Accepted = _Model->TryRelease(Commands);
    _Status = Accepted ? FString::Printf(TEXT("DEPLOYED %d POWER%s / %d total"), Commands.Num(),
        Commands.Num() == 1 ? TEXT("") : TEXT("S"), _Model->Get_DispatchCount()) : TEXT("Queue rejected: target or power state changed. No commands deployed.");
    DoClosePresentation();
    return Accepted;
}

auto UCk_PowerWheelProof_UE::Request_Cancel() -> void
{
    if (_Model.IsValid()) { _Model->RequestCancel(); }
    _Status = TEXT("Selection canceled. No commands deployed.");
    DoClosePresentation();
}

auto UCk_PowerWheelProof_UE::DoClosePresentation() -> void
{
    const auto WasOpen = _Open;
    _Open = false;
    _HoldKey = FKey{};
    _NeedsFocus = false;
    if (IsValid(_Surface)) { _Surface->ReleaseView(); }
    if (_View.IsValid()) { _View->ReleaseOwnerInteractions(); }
    _View.Reset();
    _Powers.Reset();
    auto* Controller = GetOwningPlayer();
    if (_OwnsPause)
    { _OwnsPause = false; if (IsValid(Controller)) { Controller->SetPause(false); } }
    if (WasOpen && IsValid(Controller)) { Controller->bShowMouseCursor = _HadCursor; }
    if (WasOpen)
    {
        DeactivateWidget();
        if (auto* Player = GetOwningLocalPlayer(); IsValid(Player))
        { if (auto* Subsystem = Player->GetSubsystem<UCk_UI_Layout_Subsystem_UE>(); IsValid(Subsystem)) { Subsystem->RemoveWidget(this); } }
        if (const auto Previous = _PreviousFocus.Pin(); Previous.IsValid() && FSlateApplication::IsInitialized())
        { FSlateApplication::Get().SetUserFocus(_UserIndex, Previous, EFocusCause::SetDirectly); }
    }
}

auto UCk_PowerWheelProof_UE::Request_Shutdown() -> void
{
    if (NOT _Alive) { return; }
    _Alive = false;
    Request_Cancel();
    if (FSlateApplication::IsInitialized() && _Input.IsValid()) { FSlateApplication::Get().UnregisterInputPreProcessor(_Input); }
    _Input.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(_TickHandle);
    _TickHandle.Reset();
}

auto UCk_PowerWheelProof_UE::Request_ToggleTarget() -> bool
{ if (NOT _Alive) { return false; } const auto Result = _Model->SetTargetShielded(NOT _Model->Get_TargetShielded()); if (_Open) { DoSync(); } return Result; }
auto UCk_PowerWheelProof_UE::Request_ResetFixture() -> bool
{ if (NOT _Alive || _Open) { return false; } _Model->ResetFixture(); _Status = TEXT("Fixture reset. Hold Shift to select squad powers."); return true; }
auto UCk_PowerWheelProof_UE::IsWheelOpen() const -> bool { return _Alive && _Open; }
auto UCk_PowerWheelProof_UE::Get_Summary() const -> FString { return _Status; }
auto UCk_PowerWheelProof_UE::Get_SourceError() const -> FString { return _SourceError; }
auto UCk_PowerWheelProof_UE::NativeGetDesiredFocusTarget() const -> UWidget* { return const_cast<UCk_PowerWheelProof_UE*>(this); }
auto UCk_PowerWheelProof_UE::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InEvent) -> FReply
{
    if (_Open && IsActivated() && _View.IsValid() && InEvent.GetUser() == _UserIndex)
    {
        const auto Wheel = ck_power_wheel_host::FindWidget(_View->GetRegion(TEXT("main")), TEXT("power-wheel"));
        if (Wheel.IsValid())
        {
            _NeedsFocus = false;
            return FReply::Handled().SetUserFocus(Wheel.ToSharedRef(), EFocusCause::Navigation);
        }
    }
    return Super::NativeOnFocusReceived(InGeometry, InEvent);
}
auto UCk_PowerWheelProof_UE::NativeOnDeactivated() -> void { if (_Open) { Request_Cancel(); } Super::NativeOnDeactivated(); }
auto UCk_PowerWheelProof_UE::NativeDestruct() -> void { if (_Open) { Request_Cancel(); } Super::NativeDestruct(); }
auto UCk_PowerWheelProof_UE::BeginDestroy() -> void { Request_Shutdown(); Super::BeginDestroy(); }

#include "CkTests/UI/CkCombatHudProof.h"
#include "UI/CkCombatHudProofModel.h"
#include "UI/CkCombatHudReplicaArt.h"
#include "CkTests/CkTests_Log.h"

#include "CkSlateLayout/CkUiHudControls.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSurface_Widget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SNullWidget.h"

namespace ck_combat_hud_host
{
    auto ThemeName(ECk_CombatHudProof_Theme InTheme) -> FString
    {
        switch (InTheme)
        {
        case ECk_CombatHudProof_Theme::Futuristic: return TEXT("Futuristic");
        case ECk_CombatHudProof_Theme::Tactical: return TEXT("Tactical");
        case ECk_CombatHudProof_Theme::Fantasy: return TEXT("Fantasy");
        default: return {};
        }
    }

    auto FindNode(const FCkUiNode& InNode, const FString& InId) -> const FCkUiNode*
    {
        if (InNode.Id == InId) { return &InNode; }
        for (const auto& Child : InNode.Children)
        {
            if (const auto* Found = FindNode(Child, InId)) { return Found; }
        }
        return nullptr;
    }

    auto HasProperty(const FCkUiNode* InNode, const FString& InProperty, const FString& InBinding,
        ECkUiCustomPropertyKind InKind) -> bool
    {
        if (InNode == nullptr) { return false; }
        const auto* Property = InNode->CustomProperties.Find(InProperty);
        return Property != nullptr && Property->Kind == InKind && Property->Name == InBinding;
    }
}

UCk_CombatHudProof_UE::UCk_CombatHudProof_UE(const FObjectInitializer& InObjectInitializer)
    : Super(InObjectInitializer)
{
    SetIsFocusable(false);
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

auto UCk_CombatHudProof_UE::Open(APlayerController* InPlayerController, ECk_CombatHudProof_Theme InTheme)
    -> UCk_CombatHudProof_UE*
{
    const auto ValidController = IsValid(InPlayerController) && InPlayerController->IsLocalController();
    CK_ENSURE_IF_NOT(ValidController, TEXT("Combat HUD requires a valid local player controller")) {}
    if (NOT ValidController) { return nullptr; }
    const auto ValidWorld = IsValid(InPlayerController->GetWorld()) && IsValid(InPlayerController->GetLocalPlayer());
    CK_ENSURE_IF_NOT(ValidWorld, TEXT("Combat HUD requires a world and local player")) {}
    if (NOT ValidWorld) { return nullptr; }
    auto* Host = CreateWidget<UCk_CombatHudProof_UE>(InPlayerController, StaticClass());
    if (NOT IsValid(Host))
    { UE_LOG(CkTests, Warning, TEXT("Combat HUD could not create its UMG host.")); return nullptr; }
    if (NOT Host->DoInitialize(InTheme))
    {
        UE_LOG(CkTests, Warning, TEXT("Combat HUD source initialization failed: %s"), *Host->_SourceError);
        Host->DoRelease();
        return nullptr;
    }
    Host->_Active = true;
    const auto Added = Host->AddToPlayerScreen(0);
    const auto Mounted = Added && Host->_Active && IsValid(Host->_Surface) && Host->_Surface->HasConfiguredView();
    if (NOT Mounted)
    {
        UE_LOG(CkTests, Warning, TEXT("Combat HUD mount failed: added=%d active=%d surface=%d configured=%d"),
            Added, Host->_Active, IsValid(Host->_Surface),
            IsValid(Host->_Surface) && Host->_Surface->HasConfiguredView());
        Host->DoRelease();
        Host->RemoveFromParent();
        return nullptr;
    }
    Host->_TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(Host,
        &UCk_CombatHudProof_UE::DoTick));
    return Host;
}

auto UCk_CombatHudProof_UE::DoInitialize(ECk_CombatHudProof_Theme InTheme) -> bool
{
    const auto Theme = ck_combat_hud_host::ThemeName(InTheme);
    if (Theme.IsEmpty()) { _SourceError = TEXT("Unknown combat HUD theme."); return false; }
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
    if (NOT Plugin.IsValid()) { _SourceError = TEXT("CkTests resource directory is unavailable."); return false; }
    _Theme = InTheme;
    _ResourceRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/UiAuthoring/CombatHudProof"), Theme);
    auto Registry = FCkUiWidgetRegistry{};
    const auto Registered = FCkUiHudControls::Register(Registry);
    if (NOT Registered.Succeeded) { _SourceError = FString::Join(Registered.Errors, TEXT("\n")); return false; }
    const auto ArtRegistered = FCkCombatHudReplicaArt::Register(Registry);
    if (NOT ArtRegistered.Succeeded) { _SourceError = FString::Join(ArtRegistered.Errors, TEXT("\n")); return false; }
    _ControlRegistry = Registry.CreateSnapshot();
    if (NOT FCkCombatHudProofModel::TryCreate(InTheme, _ResourceRoot,
        _Model, _SourceError)) { return false; }
    auto Data = FCkUiView::FDataBindings{};
    _Model->PopulateBindings(Data);
    const auto WeakThis = TWeakObjectPtr<UCk_CombatHudProof_UE>{this};
    Data.CanDispatchEvents = TAttribute<bool>::CreateLambda([WeakThis]()
    { return WeakThis.IsValid() && WeakThis->IsActive(); });
    auto BaseFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 24);
    BaseFont.OutlineSettings.OutlineSize = 1;
    BaseFont.OutlineSettings.OutlineColor = FLinearColor::Black;
    _View = FCkUiView::Create({}, {}, {}, BaseFont,
        MoveTemp(Data), _ControlRegistry);
    _View->GetRegion(TEXT("main"));
    return DoReloadFiles();
}

auto UCk_CombatHudProof_UE::Request_ApplySource(const FString& InMarkup, const FString& InStylesheet) -> bool
{
    if (NOT _View.IsValid() || _View->IsOwnerReleased())
    { _SourceError = TEXT("No live HUD view is configured."); return false; }
    auto Document = FCkUiDocument{};
    const auto Parsed = FCkUiDocumentParser::TryParse(InMarkup, InStylesheet, {}, Document,
        TEXT("CombatHudProof"), _ControlRegistry);
    if (NOT Parsed.Succeeded) { _SourceError = FString::Join(Parsed.Errors, TEXT("\n")); return false; }
    const auto* Main = Document.Regions.Find(TEXT("main"));
    if (Main == nullptr || Document.Regions.Num() != 1)
    { _SourceError = TEXT("Combat HUD requires exactly region 'main'."); return false; }
    const auto RequireArt = [&](const TCHAR* Id, const TCHAR* Piece, const TCHAR* Binding) -> bool
    {
        const auto* Node = ck_combat_hud_host::FindNode(*Main, Id);
        const auto* Art = Node != nullptr ? Node->CustomProperties.Find(TEXT("piece")) : nullptr;
        const bool Valid = Node != nullptr && Node->Kind == ECkUiNodeKind::Custom
            && Node->CustomTag == TEXT("replica-art") && Art != nullptr
            && Art->Kind == ECkUiCustomPropertyKind::Text && Art->Text.ToString() == Piece
            && (Binding == nullptr || ck_combat_hud_host::HasProperty(Node, TEXT("value"), Binding,
                ECkUiCustomPropertyKind::NumberBinding));
        if (NOT Valid) { _SourceError = FString::Printf(TEXT("Replica requires '%s' artwork '%s' with its live binding."), Id, Piece); }
        return Valid;
    };
    const auto RequireNode = [&](const TCHAR* Id, ECkUiNodeKind Kind, const TCHAR* InVisibility = nullptr) -> bool
    {
        const auto* Node = ck_combat_hud_host::FindNode(*Main, Id);
        const bool Valid = Node != nullptr && Node->Kind == Kind
            && (InVisibility == nullptr || Node->VisibilityBinding == InVisibility);
        if (NOT Valid) { _SourceError = FString::Printf(TEXT("Replica requires '%s' with its declared kind and context."), Id); }
        return Valid;
    };
    if (NOT RequireArt(TEXT("aim-reticle"), _Theme == ECk_CombatHudProof_Theme::Futuristic
        ? TEXT("me-reticle") : TEXT("crosshair"), nullptr)) { return false; }
    auto RequiredText = TMap<FString, FString>{{TEXT("ammo-value"), TEXT("AmmoText")}};
    if (NOT RequireArt(TEXT("damage-overlay"), TEXT("damage"), TEXT("DamageOpacity"))) { return false; }
    if (_Theme == ECk_CombatHudProof_Theme::Futuristic)
    {
        if (NOT RequireArt(TEXT("health-meter"), TEXT("me-health"), TEXT("HealthFraction"))
            || NOT RequireArt(TEXT("shield-meter"), TEXT("me-shield"), TEXT("ShieldFraction"))
            || NOT RequireArt(TEXT("cooldown-meter"), TEXT("me-cooldown"), TEXT("CooldownProgress"))) { return false; }
        RequiredText.Add(TEXT("reserve-value"), TEXT("ReserveText"));
        RequiredText.Add(TEXT("reload-status"), TEXT("ReloadText"));
        if (NOT RequireNode(TEXT("quickslots"), ECkUiNodeKind::Row)
            || NOT RequireNode(TEXT("squad-portraits"), ECkUiNodeKind::Row)
            || NOT RequireNode(TEXT("objective-prompt"), ECkUiNodeKind::Column, TEXT("ShowObjective"))
            || NOT RequireNode(TEXT("reload-status"), ECkUiNodeKind::Text, TEXT("ShowReload"))) { return false; }
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const auto Id = FString::Printf(TEXT("quickslot-%d-glyph"), Index);
            if (NOT RequireArt(*Id, TEXT("me-power"), nullptr)) { return false; }
        }
        for (const auto& [Id, Binding] : TMap<FString, FString>{
            {TEXT("squad-one-portrait"), TEXT("SquadOnePortrait")}, {TEXT("squad-two-portrait"), TEXT("SquadTwoPortrait")}})
        {
            const auto* Node = ck_combat_hud_host::FindNode(*Main, Id);
            if (Node == nullptr || Node->Kind != ECkUiNodeKind::Image || Node->Binding != Binding)
            { _SourceError = TEXT("Mass Effect replica requires both bound squad portraits."); return false; }
        }
    }
    else if (_Theme == ECk_CombatHudProof_Theme::Tactical)
    {
        if (NOT RequireArt(TEXT("minimap"), TEXT("bf-map"), TEXT("MapPhase"))) { return false; }
        RequiredText.Add(TEXT("reserve-value"), TEXT("ReserveText"));
        RequiredText.Add(TEXT("health-value"), TEXT("HealthText"));
        RequiredText.Add(TEXT("tickets-friendly"), TEXT("FriendlyTickets"));
        RequiredText.Add(TEXT("tickets-enemy"), TEXT("EnemyTickets"));
        RequiredText.Add(TEXT("capture-status"), TEXT("CaptureText"));
        RequiredText.Add(TEXT("conquest-flags"), TEXT("FlagText"));
        RequiredText.Add(TEXT("kill-feed-text"), TEXT("KillFeedText"));
        RequiredText.Add(TEXT("reload-status"), TEXT("ReloadText"));
        if (NOT RequireArt(TEXT("grenade-icon"), TEXT("bf-grenade"), nullptr)
            || NOT RequireArt(TEXT("objective-icon"), TEXT("bf-objective"), nullptr)
            || NOT RequireNode(TEXT("squad-list"), ECkUiNodeKind::Column)
            || NOT RequireNode(TEXT("kill-feed"), ECkUiNodeKind::Column, TEXT("ShowKillFeed"))
            || NOT RequireNode(TEXT("capture-status"), ECkUiNodeKind::Text, TEXT("ShowCapture"))
            || NOT RequireNode(TEXT("reload-status"), ECkUiNodeKind::Text, TEXT("ShowReload"))
            || NOT RequireNode(TEXT("fire-mode"), ECkUiNodeKind::Text)) { return false; }
    }
    else
    {
        if (NOT RequireArt(TEXT("compass-strip"), TEXT("sk-compass"), nullptr)
            || NOT RequireArt(TEXT("health-meter"), TEXT("sk-meter"), TEXT("HealthFraction"))
            || NOT RequireArt(TEXT("magicka-meter"), TEXT("sk-meter"), TEXT("MagickaFraction"))
            || NOT RequireArt(TEXT("stamina-meter"), TEXT("sk-meter"), TEXT("StaminaFraction"))) { return false; }
        for (const auto& [Id, Binding] : TMap<FString, FString>{
            {TEXT("health-meter"), TEXT("HealthOpacity")}, {TEXT("magicka-meter"), TEXT("MagickaOpacity")},
            {TEXT("stamina-meter"), TEXT("StaminaOpacity")}})
        {
            if (NOT ck_combat_hud_host::HasProperty(ck_combat_hud_host::FindNode(*Main, Id), TEXT("opacity"),
                Binding, ECkUiCustomPropertyKind::NumberBinding))
            { _SourceError = TEXT("Skyrim replica requires independent resource fading."); return false; }
        }
        if (NOT RequireArt(TEXT("sneak-eye"), TEXT("sk-eye"), TEXT("SneakFraction"))
            || NOT RequireNode(TEXT("sneak-eye"), ECkUiNodeKind::Custom, TEXT("ShowSneak"))
            || NOT RequireNode(TEXT("target-group"), ECkUiNodeKind::Column, TEXT("ShowTarget"))
            || NOT RequireNode(TEXT("arrow-readout"), ECkUiNodeKind::Column, TEXT("ShowArrows"))
            || NOT RequireNode(TEXT("interaction-row"), ECkUiNodeKind::Row, TEXT("ShowInteraction"))) { return false; }
        if (NOT RequireArt(TEXT("target-health"), TEXT("sk-meter"), TEXT("TargetHealthFraction"))) { return false; }
        RequiredText.Add(TEXT("target-name"), TEXT("TargetName"));
        RequiredText.Add(TEXT("sneak-label"), TEXT("SneakText"));
    }
    for (const auto& [Id, Binding] : RequiredText)
    {
        const auto* Node = ck_combat_hud_host::FindNode(*Main, Id);
        if (Node == nullptr || Node->Kind != ECkUiNodeKind::Text || Node->Binding != Binding)
        { _SourceError = FString::Printf(TEXT("Replica requires text '%s' bound to '%s'."), *Id, *Binding); return false; }
    }
    const auto Result = _View->TryReload(InMarkup, InStylesheet, TEXT("CombatHudProof"));
    _SourceError = Result.Succeeded ? FString{} : FString::Join(Result.Errors, TEXT("\n"));
    return Result.Succeeded;
}

auto UCk_CombatHudProof_UE::DoReloadFiles() -> bool
{
    auto Markup = FString{};
    auto Style = FString{};
    if (NOT FFileHelper::LoadFileToString(Markup, *FPaths::Combine(_ResourceRoot, TEXT("CombatHudProof.ui.html")))
        || NOT FFileHelper::LoadFileToString(Style, *FPaths::Combine(_ResourceRoot, TEXT("CombatHudProof.ui.css"))))
    { _SourceError = TEXT("Could not read both combat HUD source files."); _HasSourceAttempt = false; return false; }
    if (_HasSourceAttempt && Markup == _LastMarkup && Style == _LastStyle) { return _SourceError.IsEmpty(); }
    _HasSourceAttempt = true;
    _LastMarkup = Markup;
    _LastStyle = Style;
    return Request_ApplySource(Markup, Style);
}

auto UCk_CombatHudProof_UE::DoTick(float InDeltaTime) -> bool
{
    if (NOT _Active || NOT IsValid(GetWorld()) || NOT _Model.IsValid()) { DoRelease(); return false; }
    if (FMath::IsFinite(InDeltaTime) && InDeltaTime >= 0.0f)
    {
        if (NOT GetWorld()->IsPaused()) { _Model->Advance(InDeltaTime); }
        _PollRemaining -= InDeltaTime;
        if (_PollRemaining <= 0.0f) { _PollRemaining = 0.5f; DoReloadFiles(); }
    }
    return true;
}

auto UCk_CombatHudProof_UE::RebuildWidget() -> TSharedRef<SWidget>
{
    if (NOT _View.IsValid()) { return SNullWidget::NullWidget; }
    if (NOT IsValid(WidgetTree)) { WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree")); }
    if (NOT IsValid(WidgetTree->RootWidget))
    {
        auto* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("HudOverlay"));
        Root->SetVisibility(ESlateVisibility::HitTestInvisible);
        WidgetTree->RootWidget = Root;
        auto* Safe = WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(), TEXT("SafeContent"));
        auto* SafeSlot = Root->AddChildToOverlay(Safe);
        SafeSlot->SetHorizontalAlignment(HAlign_Fill);
        SafeSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("ReferenceScale"));
        Scale->SetStretch(EStretch::ScaleToFit);
        Safe->AddChild(Scale);
        auto* Reference = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ReferenceViewport"));
        Reference->SetWidthOverride(1920.0f);
        Reference->SetHeightOverride(1080.0f);
        Scale->AddChild(Reference);
        _Surface = WidgetTree->ConstructWidget<UCk_UI_AuthoredSurface_UE>(UCk_UI_AuthoredSurface_UE::StaticClass(), TEXT("AuthoredHud"));
        Reference->AddChild(_Surface);
    }
    const auto Configured = IsValid(_Surface) && _Surface->TryConfigure(_View, TEXT("main"));
    CK_ENSURE_IF_NOT(Configured, TEXT("Combat HUD could not mount its accepted view")) {}
    if (NOT Configured) { DoRelease(); return SNullWidget::NullWidget; }
    return Super::RebuildWidget();
}

auto UCk_CombatHudProof_UE::DoRelease() -> void
{
    _Active = false;
    FTSTicker::GetCoreTicker().RemoveTicker(_TickHandle);
    _TickHandle.Reset();
    if (IsValid(_Surface)) { _Surface->ReleaseView(); }
    if (_View.IsValid()) { _View->ReleaseOwnerInteractions(); }
}

auto UCk_CombatHudProof_UE::Request_Close() -> void { DoRelease(); RemoveFromParent(); }
auto UCk_CombatHudProof_UE::NativeDestruct() -> void { DoRelease(); Super::NativeDestruct(); }
auto UCk_CombatHudProof_UE::Request_Command(ECk_CombatHudProof_Command InCommand) -> bool
{ return IsActive() && _Model.IsValid() && _Model->Request_Command(InCommand); }
auto UCk_CombatHudProof_UE::Get_State() const -> FCk_CombatHudProof_State
{ return _Model.IsValid() ? _Model->Get_State() : FCk_CombatHudProof_State{}; }
auto UCk_CombatHudProof_UE::Get_Theme() const -> ECk_CombatHudProof_Theme { return _Theme; }
auto UCk_CombatHudProof_UE::Get_Summary() const -> FString { return _Model.IsValid() ? _Model->Get_Summary() : FString{}; }
auto UCk_CombatHudProof_UE::Get_SourceError() const -> FString { return _SourceError; }
auto UCk_CombatHudProof_UE::IsActive() const -> bool { return _Active; }
auto UCk_CombatHudProof_UE::Get_AuthoredView() const -> TSharedPtr<FCkUiView> { return _View; }
auto UCk_CombatHudProof_UE::Get_Model() const -> TSharedPtr<FCkCombatHudProofModel> { return _Model; }

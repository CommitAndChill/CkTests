#include "CkCombatHudProofModel.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/SlateBrush.h"

namespace ck_combat_hud_proof
{
    constexpr float ReloadDuration = 1.6f;
    constexpr float AbilityDuration = 3.0f;
    constexpr float ShieldDelay = 1.5f;

    constexpr const TCHAR* ImageNames[] = {TEXT("SquadOnePortrait"), TEXT("SquadTwoPortrait")};

    /** The resource must outlive a surviving authored image widget. */
    struct FAtlasBrush final : FSlateBrush
    {
        TSharedPtr<TStrongObjectPtr<UTexture2D>> TextureRoot;
    };

    auto IsTheme(ECk_CombatHudProof_Theme InTheme) -> bool
    {
        switch (InTheme)
        {
        case ECk_CombatHudProof_Theme::Futuristic:
        case ECk_CombatHudProof_Theme::Tactical:
        case ECk_CombatHudProof_Theme::Fantasy: return true;
        default: return false;
        }
    }

    auto InitialState(ECk_CombatHudProof_Theme InTheme) -> FCk_CombatHudProof_State
    {
        auto State = FCk_CombatHudProof_State{};
        State.Set_Health(100.0f);
        State.Set_Shield(InTheme == ECk_CombatHudProof_Theme::Futuristic ? 100.0f : 0.0f);
        State.Set_Stamina(100.0f);
        State.Set_Magicka(100.0f);
        State.Set_Ammo(InTheme == ECk_CombatHudProof_Theme::Futuristic ? 50
            : InTheme == ECk_CombatHudProof_Theme::Tactical ? 31 : 18);
        State.Set_Reserve(InTheme == ECk_CombatHudProof_Theme::Futuristic ? 383
            : InTheme == ECk_CombatHudProof_Theme::Tactical ? 97 : 60);
        State.Set_ReloadProgress(0.0f);
        State.Set_CooldownProgress(0.0f);
        State.Set_DamageOpacity(0.0f);
        State.Set_Sprinting(false);
        State.Set_Reloading(false);
        return State;
    }

    auto Capacity(ECk_CombatHudProof_Theme InTheme) -> int32
    { return InTheme == ECk_CombatHudProof_Theme::Futuristic ? 50
        : InTheme == ECk_CombatHudProof_Theme::Tactical ? 31 : 18; }

    auto IsPortraitPng(const FString& InPath, int32& OutWidth, int32& OutHeight) -> bool
    {
        auto Bytes = TArray<uint8>{};
        if (NOT FFileHelper::LoadFileToArray(Bytes, *InPath) || Bytes.Num() < 33) { return false; }
        constexpr uint8 Signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Signature); ++Index)
        { if (Bytes[Index] != Signature[Index]) { return false; } }
        if (Bytes[8] != 0 || Bytes[9] != 0 || Bytes[10] != 0 || Bytes[11] != 13
            || Bytes[12] != 'I' || Bytes[13] != 'H' || Bytes[14] != 'D' || Bytes[15] != 'R'
            || Bytes[24] != 8 || (Bytes[25] != 2 && Bytes[25] != 6))
        { return false; }
        const uint32 Width = (uint32(Bytes[16]) << 24) | (uint32(Bytes[17]) << 16)
            | (uint32(Bytes[18]) << 8) | uint32(Bytes[19]);
        const uint32 Height = (uint32(Bytes[20]) << 24) | (uint32(Bytes[21]) << 16)
            | (uint32(Bytes[22]) << 8) | uint32(Bytes[23]);
        if (Width < 2 || Width > 4096 || Height < 1 || Height > 4096 || Width != Height * 2)
        { return false; }
        OutWidth = static_cast<int32>(Width);
        OutHeight = static_cast<int32>(Height);
        return true;
    }
}

auto FCkCombatHudProofModel::TryCreate(ECk_CombatHudProof_Theme InTheme, const FString& InResourceRoot,
    TSharedPtr<FCkCombatHudProofModel>& OutModel, FString& OutError) -> bool
{
    OutError.Reset();
    if (NOT ck_combat_hud_proof::IsTheme(InTheme))
    { OutError = TEXT("Unknown combat HUD theme."); return false; }
    if (InResourceRoot.IsEmpty()
        || NOT FPaths::FileExists(FPaths::Combine(InResourceRoot, TEXT("CombatHudProof.ui.html")))
        || NOT FPaths::FileExists(FPaths::Combine(InResourceRoot, TEXT("CombatHudProof.ui.css"))))
    { OutError = TEXT("Combat HUD requires its installed source pair."); return false; }
    auto Candidate = MakeShared<FCkCombatHudProofModel>();
    Candidate->_Theme = InTheme;
    Candidate->_State = ck_combat_hud_proof::InitialState(InTheme);
    if (InTheme == ECk_CombatHudProof_Theme::Futuristic)
    {
        const auto PortraitPath = FPaths::Combine(InResourceRoot, TEXT("squad-portraits.png"));
        int32 Width = 0;
        int32 Height = 0;
        if (NOT ck_combat_hud_proof::IsPortraitPng(PortraitPath, Width, Height))
        { OutError = TEXT("Squad portrait sheet requires a bounded 2:1 8-bit RGB/RGBA PNG."); return false; }
        auto* Texture = FImageUtils::ImportFileAsTexture2D(PortraitPath);
        if (NOT IsValid(Texture) || Texture->GetSizeX() != Width || Texture->GetSizeY() != Height)
        { OutError = TEXT("Could not import squad portraits at their declared dimensions."); return false; }
        Candidate->_AtlasRoot = MakeShared<TStrongObjectPtr<UTexture2D>>(Texture);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto Brush = MakeShared<ck_combat_hud_proof::FAtlasBrush>();
            Brush->TextureRoot = Candidate->_AtlasRoot;
            Brush->DrawAs = ESlateBrushDrawType::Image;
            Brush->ImageSize = FVector2D{48.0f, 48.0f};
            Brush->SetResourceObject(Texture);
            Brush->SetUVRegion(FBox2f{FVector2f{Index * 0.5f, 0.0f}, FVector2f{(Index + 1) * 0.5f, 1.0f}});
            Candidate->_Brushes.Add(MoveTemp(Brush));
        }
    }
    OutModel = MoveTemp(Candidate);
    return true;
}

auto FCkCombatHudProofModel::Get_Brush(int32 InIndex) const -> const FSlateBrush*
{ return _Brushes.IsValidIndex(InIndex) ? _Brushes[InIndex].Get() : nullptr; }

auto FCkCombatHudProofModel::Get_Number(const FString& InName) const -> float
{
    if (InName == TEXT("HealthFraction")) { return _State.Get_Health() / 100.0f; }
    if (InName == TEXT("AmmoFraction")) { return float(_State.Get_Ammo()) / ck_combat_hud_proof::Capacity(_Theme); }
    if (InName == TEXT("ShieldFraction")) { return _State.Get_Shield() / 100.0f; }
    if (InName == TEXT("StaminaFraction")) { return _State.Get_Stamina() / 100.0f; }
    if (InName == TEXT("MagickaFraction")) { return _State.Get_Magicka() / 100.0f; }
    if (InName == TEXT("ReloadProgress")) { return _State.Get_ReloadProgress(); }
    if (InName == TEXT("CooldownProgress")) { return _State.Get_CooldownProgress(); }
    if (InName == TEXT("DamageOpacity")) { return _State.Get_DamageOpacity(); }
    if (InName == TEXT("MapPhase")) { return _FixtureTime / 20.0f; }
    if (InName == TEXT("TargetHealthFraction")) { return _TargetHealth; }
    if (InName == TEXT("SneakFraction")) { return _TargetHealth > 0.5f ? 0.0f : 1.0f; }
    if (InName == TEXT("HealthOpacity")) { return _State.Get_Health() < 100.0f || _HealthHoldRemaining > 0.0f ? 1.0f : 0.0f; }
    if (InName == TEXT("StaminaOpacity")) { return _State.Get_Stamina() < 100.0f || _StaminaHoldRemaining > 0.0f ? 1.0f : 0.0f; }
    if (InName == TEXT("MagickaOpacity")) { return _State.Get_Magicka() < 100.0f || _MagickaHoldRemaining > 0.0f ? 1.0f : 0.0f; }
    return 0.0f;
}

auto FCkCombatHudProofModel::Get_Text(const FString& InName) const -> FText
{
    if (InName == TEXT("FriendlyTickets")) { return FText::AsNumber(_FriendlyTickets); }
    if (InName == TEXT("EnemyTickets")) { return FText::AsNumber(_EnemyTickets); }
    if (InName == TEXT("FlagText")) { return FText::FromString(_ObjectiveCaptured ? TEXT("A   B   [C]   D   E   F") : TEXT("A   B   C   D   E   F")); }
    if (InName == TEXT("CaptureText")) { return FText::FromString(_AbilityRemaining > 0.0f ? TEXT("FLAG STATUS / CAPTURING") : TEXT("FLAG STATUS / NEUTRALIZING")); }
    if (InName == TEXT("KillFeedText")) { return FText::FromString(TEXT("Operator [AK-12] Recon01")); }
    if (InName == TEXT("TargetName")) { return FText::FromString(TEXT("Orc Hunter")); }
    if (InName == TEXT("SneakText")) { return FText::FromString(_TargetHealth > 0.5f ? TEXT("HIDDEN") : TEXT("DETECTED")); }
    if (InName == TEXT("HeadingText"))
    {
        return FText::FromString(_Theme == ECk_CombatHudProof_Theme::Futuristic ? TEXT("MASS EFFECT 3")
            : _Theme == ECk_CombatHudProof_Theme::Tactical ? TEXT("BATTLEFIELD 4") : TEXT("SKYRIM"));
    }
    if (InName == TEXT("StatusText")) { return FText::FromString(Get_Summary()); }
    if (InName == TEXT("HealthText")) { return FText::AsNumber(FMath::RoundToInt(_State.Get_Health())); }
    if (InName == TEXT("ShieldText")) { return FText::AsNumber(FMath::RoundToInt(_State.Get_Shield())); }
    if (InName == TEXT("StaminaText")) { return FText::AsNumber(FMath::RoundToInt(_State.Get_Stamina())); }
    if (InName == TEXT("MagickaText")) { return FText::AsNumber(FMath::RoundToInt(_State.Get_Magicka())); }
    if (InName == TEXT("AmmoText")) { return FText::AsNumber(_State.Get_Ammo()); }
    if (InName == TEXT("ReserveText")) { return FText::AsNumber(_State.Get_Reserve()); }
    if (InName == TEXT("ReloadText"))
    { return FText::FromString(_State.Get_Reloading() ? TEXT("RELOADING") : TEXT(" ")); }
    if (InName == TEXT("AbilityText"))
    { return FText::FromString(_AbilityRemaining > 0.0f ? TEXT("COOLDOWN") : TEXT("READY")); }
    return FText::GetEmpty();
}

auto FCkCombatHudProofModel::Get_Summary() const -> FString
{
    if (_State.Get_Health() <= 20.0f) { return TEXT("LOW HEALTH"); }
    if (_State.Get_Reloading()) { return TEXT("RELOADING"); }
    if (_State.Get_Sprinting()) { return TEXT("SPRINTING"); }
    if (_AbilityRemaining > 0.0f) { return TEXT("ABILITY COOLING"); }
    return TEXT("SYSTEM READY");
}

auto FCkCombatHudProofModel::Request_Command(ECk_CombatHudProof_Command InCommand) -> bool
{
    auto Next = _State;
    auto ReloadRemaining = _ReloadRemaining;
    auto AbilityRemaining = _AbilityRemaining;
    auto ShieldRegenDelay = _ShieldRegenDelay;
    switch (InCommand)
    {
    case ECk_CombatHudProof_Command::Damage:
    {
        const float Absorbed = FMath::Min(30.0f, Next.Get_Shield());
        Next.Set_Shield(Next.Get_Shield() - Absorbed);
        Next.Set_Health(FMath::Max(0.0f, Next.Get_Health() - (30.0f - Absorbed)));
        Next.Set_DamageOpacity(1.0f);
        ShieldRegenDelay = ck_combat_hud_proof::ShieldDelay;
        break;
    }
    case ECk_CombatHudProof_Command::Sprint:
        if (NOT Next.Get_Sprinting() && Next.Get_Stamina() <= 0.0f) { return false; }
        Next.Set_Sprinting(NOT Next.Get_Sprinting());
        break;
    case ECk_CombatHudProof_Command::Fire:
        if (Next.Get_Reloading() || Next.Get_Ammo() <= 0) { return false; }
        Next.Set_Ammo(Next.Get_Ammo() - 1);
        break;
    case ECk_CombatHudProof_Command::Reload:
        if (Next.Get_Reloading() || Next.Get_Ammo() >= ck_combat_hud_proof::Capacity(_Theme)
            || Next.Get_Reserve() <= 0)
        { return false; }
        Next.Set_Reloading(true);
        Next.Set_ReloadProgress(0.0f);
        ReloadRemaining = ck_combat_hud_proof::ReloadDuration;
        break;
    case ECk_CombatHudProof_Command::Ability:
        if (AbilityRemaining > 0.0f || Next.Get_Magicka() < 25.0f) { return false; }
        Next.Set_Magicka(Next.Get_Magicka() - 25.0f);
        Next.Set_CooldownProgress(1.0f);
        AbilityRemaining = ck_combat_hud_proof::AbilityDuration;
        break;
    case ECk_CombatHudProof_Command::Recover:
        Next.Set_Health(FMath::Min(100.0f, Next.Get_Health() + 25.0f));
        Next.Set_Shield(FMath::Min(100.0f, Next.Get_Shield()
            + (_Theme == ECk_CombatHudProof_Theme::Futuristic ? 25.0f : 0.0f)));
        Next.Set_Stamina(FMath::Min(100.0f, Next.Get_Stamina() + 25.0f));
        Next.Set_Magicka(FMath::Min(100.0f, Next.Get_Magicka() + 25.0f));
        break;
    case ECk_CombatHudProof_Command::Reset:
        Next = ck_combat_hud_proof::InitialState(_Theme);
        ReloadRemaining = 0.0f;
        AbilityRemaining = 0.0f;
        ShieldRegenDelay = 0.0f;
        break;
    case ECk_CombatHudProof_Command::LowHealth:
        Next.Set_Health(20.0f);
        Next.Set_Shield(0.0f);
        Next.Set_DamageOpacity(1.0f);
        ShieldRegenDelay = ck_combat_hud_proof::ShieldDelay;
        break;
    case ECk_CombatHudProof_Command::Context:
        break;
    default: return false;
    }
    if (InCommand == ECk_CombatHudProof_Command::Reset)
    {
        _FixtureTime = 0.0f;
        _HealthHoldRemaining = 3.0f;
        _StaminaHoldRemaining = 3.0f;
        _MagickaHoldRemaining = 3.0f;
        _KillFeedRemaining = 0.0f;
        _TargetHealth = 0.75f;
        _FriendlyTickets = 454;
        _EnemyTickets = 335;
        _ContextMode = 0;
        _ObjectiveCaptured = false;
    }
    else
    {
        if (InCommand == ECk_CombatHudProof_Command::Damage || InCommand == ECk_CombatHudProof_Command::LowHealth
            || InCommand == ECk_CombatHudProof_Command::Recover) { _HealthHoldRemaining = 2.0f; }
        if (InCommand == ECk_CombatHudProof_Command::Sprint || InCommand == ECk_CombatHudProof_Command::Recover)
        { _StaminaHoldRemaining = 2.0f; }
        if (InCommand == ECk_CombatHudProof_Command::Ability || InCommand == ECk_CombatHudProof_Command::Recover)
        { _MagickaHoldRemaining = 2.0f; }
        if (InCommand == ECk_CombatHudProof_Command::Fire)
        { _KillFeedRemaining = 4.0f; _TargetHealth = FMath::Max(0.0f, _TargetHealth - 0.08f); }
        if (InCommand == ECk_CombatHudProof_Command::Context) { _ContextMode = (_ContextMode + 1) % 3; }
        if (InCommand == ECk_CombatHudProof_Command::Ability && _Theme == ECk_CombatHudProof_Theme::Tactical)
        { _ObjectiveCaptured = NOT _ObjectiveCaptured; _EnemyTickets = FMath::Max(0, _EnemyTickets - 10); }
    }
    _State = MoveTemp(Next);
    _ReloadRemaining = ReloadRemaining;
    _AbilityRemaining = AbilityRemaining;
    _ShieldRegenDelay = ShieldRegenDelay;
    return true;
}

auto FCkCombatHudProofModel::Advance(float InDeltaSeconds) -> bool
{
    if (NOT FMath::IsFinite(InDeltaSeconds) || InDeltaSeconds < 0.0f) { return false; }
    if (InDeltaSeconds == 0.0f) { return true; }
    const double Delta = static_cast<double>(InDeltaSeconds);
    auto Next = _State;
    if (Next.Get_Sprinting())
    {
        Next.Set_Stamina(static_cast<float>(FMath::Max(0.0,
            double(Next.Get_Stamina()) - 24.0 * Delta)));
        if (Next.Get_Stamina() <= 0.0f) { Next.Set_Sprinting(false); }
    }
    else
    {
        Next.Set_Stamina(static_cast<float>(FMath::Min(100.0,
            double(Next.Get_Stamina()) + 20.0 * Delta)));
    }
    Next.Set_Magicka(static_cast<float>(FMath::Min(100.0,
        double(Next.Get_Magicka()) + 8.0 * Delta)));
    const double ShieldWait = FMath::Min(Delta, double(_ShieldRegenDelay));
    const float ShieldRegenDelay = static_cast<float>(FMath::Max(0.0, double(_ShieldRegenDelay) - Delta));
    if (_Theme == ECk_CombatHudProof_Theme::Futuristic && Delta > ShieldWait)
    {
        Next.Set_Shield(static_cast<float>(FMath::Min(100.0,
            double(Next.Get_Shield()) + 15.0 * (Delta - ShieldWait))));
    }
    const float ReloadRemaining = static_cast<float>(FMath::Max(0.0, double(_ReloadRemaining) - Delta));
    if (Next.Get_Reloading())
    {
        if (ReloadRemaining <= 0.0f)
        {
            const int32 Loaded = FMath::Min(ck_combat_hud_proof::Capacity(_Theme) - Next.Get_Ammo(),
                Next.Get_Reserve());
            Next.Set_Ammo(Next.Get_Ammo() + Loaded);
            Next.Set_Reserve(Next.Get_Reserve() - Loaded);
            Next.Set_Reloading(false);
            Next.Set_ReloadProgress(0.0f);
        }
        else
        {
            Next.Set_ReloadProgress(1.0f - ReloadRemaining / ck_combat_hud_proof::ReloadDuration);
        }
    }
    const float AbilityRemaining = static_cast<float>(FMath::Max(0.0, double(_AbilityRemaining) - Delta));
    Next.Set_CooldownProgress(AbilityRemaining / ck_combat_hud_proof::AbilityDuration);
    Next.Set_DamageOpacity(static_cast<float>(FMath::Max(0.0,
        double(Next.Get_DamageOpacity()) - Delta / 0.25)));
    _FixtureTime = static_cast<float>(FMath::Fmod(double(_FixtureTime) + Delta, 20.0));
    _HealthHoldRemaining = static_cast<float>(FMath::Max(0.0, double(_HealthHoldRemaining) - Delta));
    _StaminaHoldRemaining = static_cast<float>(FMath::Max(0.0, double(_StaminaHoldRemaining) - Delta));
    _MagickaHoldRemaining = static_cast<float>(FMath::Max(0.0, double(_MagickaHoldRemaining) - Delta));
    _KillFeedRemaining = static_cast<float>(FMath::Max(0.0, double(_KillFeedRemaining) - Delta));
    _State = MoveTemp(Next);
    _ReloadRemaining = ReloadRemaining;
    _AbilityRemaining = AbilityRemaining;
    _ShieldRegenDelay = ShieldRegenDelay;
    return true;
}

auto FCkCombatHudProofModel::Get_Visible(const FString& InName) const -> bool
{
    if (InName == TEXT("ShowReload")) { return _State.Get_Reloading(); }
    if (InName == TEXT("ShowDamage")) { return _State.Get_DamageOpacity() > 0.0f; }
    if (InName == TEXT("ShowLowHealth")) { return _State.Get_Health() <= 20.0f; }
    if (InName == TEXT("ShowNoAmmo")) { return _State.Get_Ammo() == 0; }
    if (InName == TEXT("ShowTarget")) { return _ContextMode == 0 && _TargetHealth > 0.0f; }
    if (InName == TEXT("ShowSneak")) { return _Theme == ECk_CombatHudProof_Theme::Fantasy && _ContextMode == 1; }
    if (InName == TEXT("ShowArrows")) { return _Theme == ECk_CombatHudProof_Theme::Fantasy && _ContextMode == 0; }
    if (InName == TEXT("ShowInteraction")) { return _Theme == ECk_CombatHudProof_Theme::Fantasy && _ContextMode == 2; }
    if (InName == TEXT("ShowKillFeed")) { return _KillFeedRemaining > 0.0f || _ContextMode == 2; }
    if (InName == TEXT("ShowCapture")) { return _AbilityRemaining > 0.0f || _ContextMode == 1; }
    if (InName == TEXT("ShowObjective")) { return _Theme == ECk_CombatHudProof_Theme::Futuristic && _ContextMode == 1; }
    return false;
}

auto FCkCombatHudProofModel::PopulateBindings(FCkUiView::FDataBindings& InOutData) -> void
{
    const auto Weak = TWeakPtr<FCkCombatHudProofModel>{AsShared()};
    for (const auto* Name : {TEXT("AmmoFraction"), TEXT("HealthFraction"), TEXT("ShieldFraction"), TEXT("StaminaFraction"),
        TEXT("MagickaFraction"), TEXT("ReloadProgress"), TEXT("CooldownProgress"), TEXT("DamageOpacity"),
        TEXT("MapPhase"), TEXT("TargetHealthFraction"), TEXT("SneakFraction"), TEXT("HealthOpacity"),
        TEXT("StaminaOpacity"), TEXT("MagickaOpacity")})
    {
        const FString Key{Name};
        InOutData.Number.Add(Key, TAttribute<float>::CreateLambda([Weak, Key]()
        { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->Get_Number(Key) : 0.0f; }));
    }
    for (const auto* Name : {TEXT("HealthText"), TEXT("ShieldText"), TEXT("StaminaText"),
        TEXT("MagickaText"), TEXT("AmmoText"), TEXT("ReserveText"), TEXT("ReloadText"),
        TEXT("AbilityText"), TEXT("StatusText"), TEXT("HeadingText"), TEXT("FriendlyTickets"),
        TEXT("EnemyTickets"), TEXT("FlagText"), TEXT("CaptureText"), TEXT("KillFeedText"), TEXT("TargetName"), TEXT("SneakText")})
    {
        const FString Key{Name};
        InOutData.Text.Add(Key, TAttribute<FText>::CreateLambda([Weak, Key]()
        { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->Get_Text(Key) : FText::GetEmpty(); }));
    }
    for (const auto* Name : {TEXT("ShowReload"), TEXT("ShowDamage"), TEXT("ShowLowHealth"),
        TEXT("ShowTarget"), TEXT("ShowSneak"), TEXT("ShowArrows"), TEXT("ShowInteraction"),
        TEXT("ShowKillFeed"), TEXT("ShowCapture"), TEXT("ShowObjective"), TEXT("ShowNoAmmo")})
    {
        const FString Key{Name};
        InOutData.Visibility.Add(Key, TAttribute<bool>::CreateLambda([Weak, Key]()
        { const auto Self = Weak.Pin(); return Self.IsValid() && Self->Get_Visible(Key); }));
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(ck_combat_hud_proof::ImageNames); ++Index)
    {
        InOutData.Images.Add(ck_combat_hud_proof::ImageNames[Index],
            TAttribute<const FSlateBrush*>::CreateLambda([Weak, Index]()
            { const auto Self = Weak.Pin(); return Self.IsValid() ? Self->Get_Brush(Index) : nullptr; }));
    }
}

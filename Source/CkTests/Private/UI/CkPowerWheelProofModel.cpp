#include "CkPowerWheelProofModel.h"

#include "CkCore/Macros/CkMacros.h"

namespace ck_power_wheel_proof
{
    auto MakePower(const TCHAR* InKey, int32 InActor, const TCHAR* InLabel,
        const TCHAR* InDescription, const TCHAR* InGlyph, float InAngle,
        float InCooldown, bool InRequiresUnshielded = false) -> FCkPowerWheelPower
    {
        return FCkPowerWheelPower{FName{InKey}, InActor, InLabel, InDescription, InGlyph,
            InAngle, 0.0f, InCooldown, true, InRequiresUnshielded};
    }

    auto MakeFixture() -> TArray<FCkPowerWheelPower>
    {
        return {
            MakePower(TEXT("shepard-warp"), 0, TEXT("Warp"), TEXT("Tears through armor and barriers."), TEXT("warp"), 25.0f, 5.0f),
            MakePower(TEXT("shepard-throw"), 0, TEXT("Throw"), TEXT("Hurls an exposed target."), TEXT("throw"), 44.0f, 4.0f, true),
            MakePower(TEXT("shepard-overload"), 0, TEXT("Overload"), TEXT("Disrupts shields and electronics."), TEXT("overload"), 63.0f, 5.0f),
            MakePower(TEXT("shepard-incinerate"), 0, TEXT("Incinerate"), TEXT("Burns armor and exposed foes."), TEXT("incinerate"), 82.0f, 5.0f),
            MakePower(TEXT("shepard-singularity"), 0, TEXT("Singularity"), TEXT("Suspends nearby foes."), TEXT("singularity"), 101.0f, 7.0f, true),
            MakePower(TEXT("shepard-energy-drain"), 0, TEXT("Energy Drain"), TEXT("Drains shields to restore protection."), TEXT("energy-drain"), 120.0f, 6.0f),
            MakePower(TEXT("shepard-concussive-shot"), 0, TEXT("Concussive Shot"), TEXT("Launches a heavy impact blast."), TEXT("concussive"), 139.0f, 4.0f),
            MakePower(TEXT("shepard-adrenaline-rush"), 0, TEXT("Adrenaline Rush"), TEXT("Briefly sharpens aim and reflexes."), TEXT("adrenaline"), 158.0f, 8.0f),
            MakePower(TEXT("garrus-overload"), 1, TEXT("Overload"), TEXT("Garrus strips enemy shields."), TEXT("overload"), -178.0f, 6.0f),
            MakePower(TEXT("garrus-concussive-shot"), 1, TEXT("Concussive Shot"), TEXT("Garrus staggers an exposed foe."), TEXT("concussive"), -158.0f, 5.0f, true),
            MakePower(TEXT("garrus-armor-piercing-ammo"), 1, TEXT("Armor-Piercing Ammo"), TEXT("Garrus loads armor-piercing rounds."), TEXT("ammo"), -138.0f, 8.0f),
            MakePower(TEXT("garrus-proximity-mine"), 1, TEXT("Proximity Mine"), TEXT("Garrus deploys a proximity charge."), TEXT("mine"), -118.0f, 7.0f),
            MakePower(TEXT("liara-singularity"), 2, TEXT("Singularity"), TEXT("Liara suspends exposed enemies."), TEXT("singularity"), -62.0f, 7.0f, true),
            MakePower(TEXT("liara-warp"), 2, TEXT("Warp"), TEXT("Liara weakens armor and barriers."), TEXT("warp"), -42.0f, 5.0f),
            MakePower(TEXT("liara-stasis"), 2, TEXT("Stasis"), TEXT("Liara holds an exposed target."), TEXT("stasis"), -22.0f, 6.0f, true),
            MakePower(TEXT("liara-warp-ammo"), 2, TEXT("Warp Ammo"), TEXT("Liara primes biotic ammunition."), TEXT("ammo"), -2.0f, 8.0f),
        };
    }

    auto IsValidPower(const FCkPowerWheelPower& InPower) -> bool
    {
        return NOT InPower.Key.IsNone() && InPower.Actor >= 0 && InPower.Actor < 3
            && NOT InPower.Label.IsEmpty() && NOT InPower.Description.IsEmpty() && NOT InPower.Glyph.IsEmpty()
            && FMath::IsFinite(InPower.Angle) && InPower.Angle >= -180.0f && InPower.Angle <= 180.0f
            && FMath::IsFinite(InPower.CooldownDuration) && InPower.CooldownDuration > 0.0f
            && InPower.CooldownDuration <= 120.0f && FMath::IsFinite(InPower.CooldownSeconds)
            && InPower.CooldownSeconds >= 0.0f && InPower.CooldownSeconds <= InPower.CooldownDuration;
    }
}

FCkPowerWheelProofModel::FCkPowerWheelProofModel()
{ ResetFixture(); }

auto FCkPowerWheelProofModel::Get_QueuedKey(int32 InActor) const -> FName
{ return InActor >= 0 && InActor < 3 ? _QueuedKeys[InActor] : NAME_None; }

auto FCkPowerWheelProofModel::FindPower(FName InKey) const -> const FCkPowerWheelPower*
{
    if (InKey.IsNone()) { return nullptr; }
    return _Powers.FindByPredicate([InKey](const FCkPowerWheelPower& InPower)
    { return InPower.Key == InKey; });
}

auto FCkPowerWheelProofModel::Get_BlockReason(FName InKey) const -> FString
{
    const auto* Power = FindPower(InKey);
    if (Power == nullptr) { return TEXT("Unknown power"); }
    if (NOT Power->Available) { return TEXT("Unavailable"); }
    if (Power->CooldownSeconds > 0.0f) { return TEXT("Cooling down"); }
    if (Power->RequiresUnshieldedTarget && _TargetShielded) { return TEXT("Target is shielded"); }
    return {};
}

auto FCkPowerWheelProofModel::IsUsable(FName InKey) const -> bool
{ return Get_BlockReason(InKey).IsEmpty(); }

auto FCkPowerWheelProofModel::Get_CooldownFraction(FName InKey) const -> float
{
    const auto* Power = FindPower(InKey);
    if (Power == nullptr || Power->CooldownDuration <= 0.0f) { return 0.0f; }
    return FMath::Clamp(Power->CooldownSeconds / Power->CooldownDuration, 0.0f, 1.0f);
}

auto FCkPowerWheelProofModel::TryBegin() -> bool
{
    if (_IsOpen || _Powers.IsEmpty()) { return false; }
    _IsOpen = true;
    _SelectedKey = _Powers[0].Key;
    for (auto& Queued : _QueuedKeys) { Queued = NAME_None; }
    return true;
}

auto FCkPowerWheelProofModel::TrySelect(FName InKey) -> bool
{
    if (NOT _IsOpen || FindPower(InKey) == nullptr) { return false; }
    _SelectedKey = InKey;
    return true;
}

auto FCkPowerWheelProofModel::TryQueue(FName InKey) -> bool
{
    if (NOT _IsOpen) { return false; }
    const auto* Power = FindPower(InKey);
    if (Power == nullptr || NOT IsUsable(InKey)) { return false; }
    _QueuedKeys[Power->Actor] = InKey;
    return true;
}

auto FCkPowerWheelProofModel::TryQueueSelected() -> bool
{ return TryQueue(_SelectedKey); }

auto FCkPowerWheelProofModel::TryRelease(TArray<FCkPowerWheelCommand>& OutCommands) -> bool
{
    if (NOT _IsOpen) { return false; }
    auto CandidateCommands = TArray<FCkPowerWheelCommand>{};
    float Durations[3] = {0.0f, 0.0f, 0.0f};
    auto Valid = true;
    for (auto Actor = 0; Actor < 3; ++Actor)
    {
        const auto Key = _QueuedKeys[Actor];
        if (Key.IsNone()) { continue; }
        const auto* Power = FindPower(Key);
        if (Power == nullptr || Power->Actor != Actor || NOT IsUsable(Key))
        {
            Valid = false;
            break;
        }
        CandidateCommands.Add({Key, Actor});
        Durations[Actor] = Power->CooldownDuration;
    }
    _IsOpen = false;
    _SelectedKey = NAME_None;
    for (auto& Queued : _QueuedKeys) { Queued = NAME_None; }
    if (NOT Valid) { return false; }
    for (auto& Power : _Powers)
    {
        if (Durations[Power.Actor] > 0.0f)
        {
            Power.CooldownDuration = Durations[Power.Actor];
            Power.CooldownSeconds = Durations[Power.Actor];
        }
    }
    _DispatchCount += CandidateCommands.Num();
    OutCommands = MoveTemp(CandidateCommands);
    return true;
}

auto FCkPowerWheelProofModel::RequestCancel() -> bool
{
    if (NOT _IsOpen) { return false; }
    _IsOpen = false;
    _SelectedKey = NAME_None;
    for (auto& Queued : _QueuedKeys) { Queued = NAME_None; }
    return true;
}

auto FCkPowerWheelProofModel::Advance(float InDeltaSeconds) -> bool
{
    if (_IsOpen || NOT FMath::IsFinite(InDeltaSeconds) || InDeltaSeconds < 0.0f) { return false; }
    for (auto Index = 0; Index < _Powers.Num(); ++Index)
    {
        auto& Power = _Powers[Index];
        Power.CooldownSeconds = FMath::Max(0.0f, Power.CooldownSeconds - InDeltaSeconds);
        if (Power.CooldownSeconds == 0.0f) { Power.CooldownDuration = _BaseCooldownDurations[Index]; }
    }
    return true;
}

auto FCkPowerWheelProofModel::SetTargetShielded(bool InShielded) -> bool
{
    if (_TargetShielded == InShielded) { return false; }
    _TargetShielded = InShielded;
    return true;
}

auto FCkPowerWheelProofModel::ResetFixture() -> void
{
    _Powers = ck_power_wheel_proof::MakeFixture();
    _BaseCooldownDurations.Reset(_Powers.Num());
    for (const auto& Power : _Powers) { _BaseCooldownDurations.Add(Power.CooldownDuration); }
    _SelectedKey = NAME_None;
    for (auto& Queued : _QueuedKeys) { Queued = NAME_None; }
    _IsOpen = false;
    _TargetShielded = false;
    _DispatchCount = 0;
}

auto FCkPowerWheelProofModel::TryConfigure(TArray<FCkPowerWheelPower> InPowers) -> bool
{
    if (InPowers.IsEmpty() || InPowers.Num() > 24) { return false; }
    auto Seen = TSet<FName>{};
    for (const auto& Power : InPowers)
    {
        if (NOT ck_power_wheel_proof::IsValidPower(Power) || Seen.Contains(Power.Key)) { return false; }
        Seen.Add(Power.Key);
    }
    for (auto Index = 0; Index < InPowers.Num(); ++Index)
    {
        for (auto Other = 0; Other < Index; ++Other)
        {
            const float Difference = FMath::Abs(InPowers[Index].Angle - InPowers[Other].Angle);
            if (FMath::Min(Difference, 360.0f - Difference) < 15.0f) { return false; }
        }
    }
    _Powers = MoveTemp(InPowers);
    _BaseCooldownDurations.Reset(_Powers.Num());
    for (const auto& Power : _Powers) { _BaseCooldownDurations.Add(Power.CooldownDuration); }
    _SelectedKey = NAME_None;
    for (auto& Queued : _QueuedKeys) { Queued = NAME_None; }
    _IsOpen = false;
    _TargetShielded = false;
    _DispatchCount = 0;
    return true;
}

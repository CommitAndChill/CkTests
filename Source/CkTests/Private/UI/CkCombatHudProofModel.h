#pragma once

#include "CkTests/UI/CkCombatHudProofData.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "UObject/StrongObjectPtr.h"

class UTexture2D;

/** A local, deterministic combat fixture. It never writes gameplay state. */
class FCkCombatHudProofModel final : public TSharedFromThis<FCkCombatHudProofModel>
{
public:
    static auto TryCreate(ECk_CombatHudProof_Theme InTheme, const FString& InResourceRoot,
        TSharedPtr<FCkCombatHudProofModel>& OutModel, FString& OutError) -> bool;

    auto PopulateBindings(FCkUiView::FDataBindings& InOutData) -> void;

    auto Get_State() const -> const FCk_CombatHudProof_State& { return _State; }
    auto Get_Summary() const -> FString;
    auto Request_Command(ECk_CombatHudProof_Command InCommand) -> bool;
    auto Advance(float InDeltaSeconds) -> bool;

private:
    auto Get_Number(const FString& InName) const -> float;
    auto Get_Text(const FString& InName) const -> FText;
    auto Get_Brush(int32 InIndex) const -> const FSlateBrush*;
    auto Get_Visible(const FString& InName) const -> bool;

    ECk_CombatHudProof_Theme _Theme = ECk_CombatHudProof_Theme::Futuristic;
    FCk_CombatHudProof_State _State;
    TSharedPtr<TStrongObjectPtr<UTexture2D>> _AtlasRoot;
    TArray<TSharedPtr<FSlateBrush>> _Brushes;
    float _ReloadRemaining = 0.0f;
    float _AbilityRemaining = 0.0f;
    float _ShieldRegenDelay = 0.0f;
    float _FixtureTime = 0.0f;
    float _HealthHoldRemaining = 3.0f;
    float _StaminaHoldRemaining = 3.0f;
    float _MagickaHoldRemaining = 3.0f;
    float _KillFeedRemaining = 0.0f;
    float _TargetHealth = 0.75f;
    int32 _FriendlyTickets = 454;
    int32 _EnemyTickets = 335;
    int32 _ContextMode = 0;
    bool _ObjectiveCaptured = false;
};

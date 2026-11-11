#pragma once

#include "CoreMinimal.h"

struct CKTESTS_API FCkPowerWheelPower
{
    FName Key;
    int32 Actor = 0;
    FString Label;
    FString Description;
    FString Glyph;
    float Angle = 0.0f;
    float CooldownSeconds = 0.0f;
    float CooldownDuration = 0.0f;
    bool Available = true;
    bool RequiresUnshieldedTarget = false;
};

struct CKTESTS_API FCkPowerWheelCommand
{
    FName Key;
    int32 Actor = 0;
};

/** Deterministic power-wheel fixture; dispatch returns commands without touching gameplay state. */
class CKTESTS_API FCkPowerWheelProofModel final
{
public:
    FCkPowerWheelProofModel();

    auto Get_Powers() const -> const TArray<FCkPowerWheelPower>& { return _Powers; }
    auto Get_SelectedKey() const -> FName { return _SelectedKey; }
    auto Get_QueuedKey(int32 InActor) const -> FName;
    auto Get_IsOpen() const -> bool { return _IsOpen; }
    auto Get_TargetShielded() const -> bool { return _TargetShielded; }
    auto Get_DispatchCount() const -> int32 { return _DispatchCount; }

    auto TryBegin() -> bool;
    auto TrySelect(FName InKey) -> bool;
    auto TryQueue(FName InKey) -> bool;
    auto TryQueueSelected() -> bool;
    auto TryRelease(TArray<FCkPowerWheelCommand>& OutCommands) -> bool;
    auto RequestCancel() -> bool;
    auto Advance(float InDeltaSeconds) -> bool;
    auto SetTargetShielded(bool InShielded) -> bool;
    auto ResetFixture() -> void;
    auto TryConfigure(TArray<FCkPowerWheelPower> InPowers) -> bool;

    auto IsUsable(FName InKey) const -> bool;
    auto Get_BlockReason(FName InKey) const -> FString;
    auto Get_CooldownFraction(FName InKey) const -> float;

private:
    auto FindPower(FName InKey) const -> const FCkPowerWheelPower*;

    TArray<FCkPowerWheelPower> _Powers;
    TArray<float> _BaseCooldownDurations;
    FName _SelectedKey;
    FName _QueuedKeys[3];
    bool _IsOpen = false;
    bool _TargetShielded = false;
    int32 _DispatchCount = 0;
};

#pragma once

#include "CoreMinimal.h"
#include "CkCore/Macros/CkMacros.h"

#include "CkCombatHudProofData.generated.h"

UENUM(BlueprintType)
enum class ECk_CombatHudProof_Theme : uint8
{
    Futuristic,
    Tactical,
    Fantasy
};

UENUM(BlueprintType)
enum class ECk_CombatHudProof_Command : uint8
{
    Damage,
    Sprint,
    Fire,
    Reload,
    Ability,
    Recover,
    Reset,
    LowHealth,
    Context
};

/** Deterministic display fixture; production gameplay feeds the same view bindings. */
USTRUCT(BlueprintType)
struct CKTESTS_API FCk_CombatHudProof_State
{
    GENERATED_BODY()
    CK_GENERATED_BODY(FCk_CombatHudProof_State);

private:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _Health = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _Shield = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _Stamina = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _Magicka = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    int32 _Ammo = 24;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    int32 _Reserve = 120;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _ReloadProgress = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _CooldownProgress = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    float _DamageOpacity = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    bool _Sprinting = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
    bool _Reloading = false;

public:
    CK_PROPERTY(_Health);
    CK_PROPERTY(_Shield);
    CK_PROPERTY(_Stamina);
    CK_PROPERTY(_Magicka);
    CK_PROPERTY(_Ammo);
    CK_PROPERTY(_Reserve);
    CK_PROPERTY(_ReloadProgress);
    CK_PROPERTY(_CooldownProgress);
    CK_PROPERTY(_DamageOpacity);
    CK_PROPERTY(_Sprinting);
    CK_PROPERTY(_Reloading);
};

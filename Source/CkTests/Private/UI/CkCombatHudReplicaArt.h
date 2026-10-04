#pragma once

#include "CkSlateLayout/CkUiWidgetRegistry.h"
#include "Widgets/SLeafWidget.h"

enum class ECkCombatHudReplicaPiece : uint8
{
    MeShield,
    MeHealth,
    MeCooldown,
    MePower,
    MeRifle,
    MeReticle,
    MeGrenade,
    Crosshair,
    BfMap,
    BfObjective,
    BfGrenade,
    SkCompass,
    SkMeter,
    SkEye,
    Damage
};

/** Passive vector artwork; the authored view owns its bindings and the retained adapter freezes them on release. */
class SCkCombatHudReplicaArt final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCkCombatHudReplicaArt) {} SLATE_END_ARGS()

    void Construct(const FArguments& InArguments);
    void Configure(ECkCombatHudReplicaPiece InPiece, int32 InVariant,
        TAttribute<float> InValue, TAttribute<float> InOpacity);
    void AdvancePresentation(float InDeltaTime);
    void Freeze();

    auto GetDisplayedFraction() const -> float { return _Displayed; }
    auto GetDisplayedOpacity() const -> float { return _Opacity; }
    auto IsFrozen() const -> bool { return _Frozen; }
    auto GetPiece() const -> ECkCombatHudReplicaPiece { return _Piece; }
    auto GetVariant() const -> int32 { return _Variant; }

    virtual void Tick(const FGeometry& InGeometry, double InCurrentTime, float InDeltaTime) override;
    virtual auto ComputeDesiredSize(float InScale) const -> FVector2D override;
    virtual auto OnPaint(const FPaintArgs& InArgs, const FGeometry& InGeometry,
        const FSlateRect& InCullingRect, FSlateWindowElementList& OutDrawElements,
        int32 InLayer, const FWidgetStyle& InWidgetStyle, bool InParentEnabled) const -> int32 override;

private:
    ECkCombatHudReplicaPiece _Piece = ECkCombatHudReplicaPiece::Crosshair;
    int32 _Variant = 0;
    TAttribute<float> _Value;
    TAttribute<float> _OpacityBinding;
    float _Displayed = 1.0f;
    float _Opacity = 1.0f;
    bool _Initialized = false;
    bool _Frozen = false;
};

class FCkCombatHudReplicaArt final
{
public:
    static auto Register(FCkUiWidgetRegistry& InRegistry) -> FCkUiLoadResult;
};

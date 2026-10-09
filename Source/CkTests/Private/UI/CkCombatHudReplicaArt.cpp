#include "UI/CkCombatHudReplicaArt.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace ck_combat_hud_art
{
    auto Unit(const float InValue, const float InFallback) -> float
    {
        return FMath::IsFinite(InValue) ? FMath::Clamp(InValue, 0.0f, 1.0f) : InFallback;
    }

    auto ParsePiece(const FString& InName, ECkCombatHudReplicaPiece& OutPiece) -> bool
    {
        static const TPair<const TCHAR*, ECkCombatHudReplicaPiece> Pieces[] = {
            {TEXT("me-shield"), ECkCombatHudReplicaPiece::MeShield},
            {TEXT("me-health"), ECkCombatHudReplicaPiece::MeHealth},
            {TEXT("me-cooldown"), ECkCombatHudReplicaPiece::MeCooldown},
            {TEXT("me-power"), ECkCombatHudReplicaPiece::MePower},
            {TEXT("me-rifle"), ECkCombatHudReplicaPiece::MeRifle},
            {TEXT("me-reticle"), ECkCombatHudReplicaPiece::MeReticle},
            {TEXT("me-grenade"), ECkCombatHudReplicaPiece::MeGrenade},
            {TEXT("crosshair"), ECkCombatHudReplicaPiece::Crosshair},
            {TEXT("bf-map"), ECkCombatHudReplicaPiece::BfMap},
            {TEXT("bf-objective"), ECkCombatHudReplicaPiece::BfObjective},
            {TEXT("bf-grenade"), ECkCombatHudReplicaPiece::BfGrenade},
            {TEXT("bf-kit"), ECkCombatHudReplicaPiece::BfKit},
            {TEXT("sk-compass"), ECkCombatHudReplicaPiece::SkCompass},
            {TEXT("sk-meter"), ECkCombatHudReplicaPiece::SkMeter},
            {TEXT("sk-eye"), ECkCombatHudReplicaPiece::SkEye},
            {TEXT("damage"), ECkCombatHudReplicaPiece::Damage}
        };
        for (const auto& Entry : Pieces)
        {
            if (InName == Entry.Key)
            {
                OutPiece = Entry.Value;
                return true;
            }
        }
        return false;
    }

    struct FConfiguration
    {
        ECkCombatHudReplicaPiece Piece = ECkCombatHudReplicaPiece::Crosshair;
        int32 Variant = 0;
        TAttribute<float> Value;
        TAttribute<float> Opacity;
    };

    auto MakeConfiguration(const FCkUiCustomWidgetArguments& InArguments,
        FConfiguration& OutConfiguration, FString& OutFailure) -> bool
    {
        const FText* PieceText = InArguments.TextProperties.Find(TEXT("piece"));
        const bool HasRequired = !InArguments.Id.IsEmpty() && PieceText != nullptr;
        if (!HasRequired)
        {
            OutFailure = TEXT("Replica art requires an id and piece.");
            return false;
        }
        ECkCombatHudReplicaPiece Piece;
        if (!ParsePiece(PieceText->ToString(), Piece))
        {
            OutFailure = TEXT("Replica art piece is unknown.");
            return false;
        }
        int32 Variant = 0;
        if (const float* RawVariant = InArguments.NumberProperties.Find(TEXT("variant")))
        {
            const bool Valid = FMath::IsFinite(*RawVariant) && *RawVariant >= 0.0f
                && *RawVariant <= 7.0f && *RawVariant == FMath::FloorToFloat(*RawVariant);
            if (!Valid)
            {
                OutFailure = TEXT("Replica art variant must be an integer from 0 to 7.");
                return false;
            }
            Variant = static_cast<int32>(*RawVariant);
        }
        if (!InArguments.Style.CustomProperties.IsEmpty())
        {
            OutFailure = TEXT("Replica art does not accept custom style properties.");
            return false;
        }
        const TAttribute<float>* Value = InArguments.NumberBindings.Find(TEXT("value"));
        const TAttribute<float>* Opacity = InArguments.NumberBindings.Find(TEXT("opacity"));
        if ((Value != nullptr && !Value->IsSet()) || (Opacity != nullptr && !Opacity->IsSet()))
        {
            OutFailure = TEXT("Replica art bindings must be set when supplied.");
            return false;
        }
        OutConfiguration.Piece = Piece;
        OutConfiguration.Variant = Variant;
        OutConfiguration.Value = Value != nullptr ? *Value : TAttribute<float>(1.0f);
        OutConfiguration.Opacity = Opacity != nullptr ? *Opacity : TAttribute<float>(1.0f);
        return true;
    }

    class FAdapter final : public ICkUiRetainedWidget, public TSharedFromThis<FAdapter>
    {
    public:
        explicit FAdapter(FConfiguration InConfiguration) : _Configuration(MoveTemp(InConfiguration)) {}
        virtual ~FAdapter() override
        {
            if (_Widget.IsValid())
            { _Widget->Freeze(); }
        }
        void Initialize(const FString& InId)
        {
            SAssignNew(_Widget, SCkCombatHudReplicaArt).Tag(FName(*InId));
            _Widget->Configure(_Configuration.Piece, _Configuration.Variant,
                _Configuration.Value, _Configuration.Opacity);
        }
        virtual auto GetWidget() const -> TSharedRef<SWidget> override { return _Widget.ToSharedRef(); }
        virtual void ReleaseOwnerInteraction() override { _Widget->Freeze(); }
        virtual auto PrepareReload(const FCkUiCustomWidgetArguments& InArguments, FString& OutFailure) const
            -> TUniquePtr<ICkUiPreparedWidgetUpdate> override
        {
            FConfiguration Candidate;
            if (!MakeConfiguration(InArguments, Candidate, OutFailure))
            { return {}; }
            return MakeUnique<FUpdate>(ConstCastSharedRef<FAdapter>(AsShared()), MoveTemp(Candidate));
        }

    private:
        class FUpdate final : public ICkUiPreparedWidgetUpdate
        {
        public:
            FUpdate(TSharedRef<FAdapter> InOwner, FConfiguration InConfiguration)
                : _Owner(MoveTemp(InOwner)), _Configuration(MoveTemp(InConfiguration)) {}
            virtual void Commit() noexcept override
            {
                _Owner->_Configuration = MoveTemp(_Configuration);
                _Owner->_Widget->Configure(_Owner->_Configuration.Piece, _Owner->_Configuration.Variant,
                    _Owner->_Configuration.Value, _Owner->_Configuration.Opacity);
            }
        private:
            TSharedRef<FAdapter> _Owner;
            FConfiguration _Configuration;
        };

        FConfiguration _Configuration;
        TSharedPtr<SCkCombatHudReplicaArt> _Widget;
    };

    class FPainter final
    {
    public:
        FPainter(const FGeometry& InGeometry, FSlateWindowElementList& InElements,
            const int32 InLayer, const float InOpacity, const FLinearColor& InTint)
            : Geometry(InGeometry), Elements(InElements), Layer(InLayer), Opacity(InOpacity), Tint(InTint) {}

        auto P(const float X, const float Y) const -> FVector2f
        {
            const FVector2D Size = Geometry.GetLocalSize();
            return FVector2f(X * static_cast<float>(Size.X), Y * static_cast<float>(Size.Y));
        }
        void Line(const float X0, const float Y0, const float X1, const float Y1,
            const FLinearColor& InColor, const float InThickness = 1.0f) const
        {
            TArray<FVector2f> Points;
            Points.Add(P(X0, Y0));
            Points.Add(P(X1, Y1));
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points),
                ESlateDrawEffect::None, InColor * Tint * Opacity, true, InThickness);
        }
        template <typename TCurve>
        void Curve(const float InStart, const float InEnd, const int32 InSteps, const TCurve& InY,
            const FLinearColor& InColor, const float InThickness) const
        {
            if (InEnd <= InStart)
            { return; }
            const int32 Steps = FMath::Max(1, InSteps);
            TArray<FVector2f> Points;
            Points.Reserve(Steps + 1);
            for (int32 Index = 0; Index <= Steps; ++Index)
            {
                const float X = FMath::Lerp(InStart, InEnd, static_cast<float>(Index) / Steps);
                Points.Add(P(X, InY(X)));
            }
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points),
                ESlateDrawEffect::None, InColor * Tint * Opacity, true, InThickness);
        }
        void Box(const float X, const float Y, const float W, const float H, const FLinearColor& InColor) const
        {
            if (W <= 0.0f || H <= 0.0f)
            { return; }
            const FVector2D Size = Geometry.GetLocalSize();
            FSlateDrawElement::MakeBox(Elements, Layer,
                Geometry.ToPaintGeometry(FVector2f(W * Size.X, H * Size.Y),
                    FSlateLayoutTransform(P(X, Y))),
                FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),
                ESlateDrawEffect::None, InColor * Tint * Opacity);
        }
        void Arc(const float CX, const float CY, const float RX, const float RY,
            const float Start, const float End, const FLinearColor& InColor,
            const float InThickness = 1.0f, const int32 Steps = 36) const
        {
            TArray<FVector2f> Points;
            Points.Reserve(Steps + 1);
            for (int32 Index = 0; Index <= Steps; ++Index)
            {
                const float Angle = FMath::Lerp(Start, End, static_cast<float>(Index) / Steps);
                Points.Add(P(CX + RX * FMath::Cos(Angle), CY + RY * FMath::Sin(Angle)));
            }
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points),
                ESlateDrawEffect::None, InColor * Tint * Opacity, true, InThickness);
        }
        void Diamond(const float X, const float Y, const float RX, const float RY,
            const FLinearColor& InColor, const float InThickness = 1.0f) const
        {
            Line(X, Y - RY, X + RX, Y, InColor, InThickness);
            Line(X + RX, Y, X, Y + RY, InColor, InThickness);
            Line(X, Y + RY, X - RX, Y, InColor, InThickness);
            Line(X - RX, Y, X, Y - RY, InColor, InThickness);
        }
    private:
        const FGeometry& Geometry;
        FSlateWindowElementList& Elements;
        int32 Layer;
        float Opacity;
        FLinearColor Tint;
    };

    const FLinearColor Blue(0.22f, 0.79f, 1.0f);
    const FLinearColor BlueDim(0.08f, 0.30f, 0.49f, 0.72f);
    const FLinearColor Red(0.95f, 0.18f, 0.16f);
    const FLinearColor Amber(1.0f, 0.55f, 0.17f);
    const FLinearColor White(0.94f, 0.97f, 0.98f);
    const FLinearColor Metal(0.53f, 0.55f, 0.56f);

    void Shield(const FPainter& P, const float Value)
    {
        auto Y = [](const float X) -> float
        {
            const float Radius = (X - 0.5f) / 0.48f;
            return 0.10f + 0.85f * Radius * Radius;
        };
        P.Curve(0.02f, 0.98f, 64, Y, FLinearColor(0.01f, 0.04f, 0.07f, 0.86f), 14.0f);
        P.Curve(0.02f, 0.98f, 64, Y, BlueDim, 10.0f);
        const float FillEnd = 0.02f + 0.96f * Value;
        if (Value > 0.0f)
        {
            const int32 FillSteps = FMath::Max(1, FMath::CeilToInt(64.0f * Value));
            P.Curve(0.02f, FillEnd, FillSteps, Y,
                FLinearColor(0.12f, 0.57f, 0.78f, 0.85f), 10.0f);
            P.Curve(0.02f, FillEnd, FillSteps, Y, Blue, 5.0f);
            P.Curve(0.02f, FillEnd, FillSteps, [Y](float X) { return Y(X) - 0.065f; },
                FLinearColor(0.68f, 0.96f, 1.0f, 0.88f), 1.2f);
        }
    }

    void Health(const FPainter& P, const float Value)
    {
        auto Y = [](const float X) -> float
        {
            const float Radius = (X - 0.5f) / 0.48f;
            return 0.24f + 0.60f * Radius * Radius;
        };
        for (int32 Index = 0; Index < 5; ++Index)
        {
            const float Start = 0.035f + Index * 0.19f;
            const float End = Start + 0.17f;
            const float Fill = FMath::Clamp(Value * 5.0f - Index, 0.0f, 1.0f);
            const float FillEnd = FMath::Lerp(Start, End, Fill);
            P.Curve(Start, End, 8, Y, FLinearColor(0.16f, 0.025f, 0.035f, 0.86f), 10.0f);
            if (Fill > 0.0f)
            {
                P.Curve(Start, FillEnd, FMath::Max(1, FMath::CeilToInt(8.0f * Fill)), Y, FLinearColor(0.58f, 0.045f, 0.055f), 8.0f);
                P.Curve(Start, FillEnd, FMath::Max(1, FMath::CeilToInt(8.0f * Fill)),
                    [Y](float X) { return Y(X) - 0.07f; }, Red, 1.5f);
            }
        }
    }

    void Cooldown(const FPainter& P, const float Value)
    {
        if (Value <= 0.0f)
        { return; }
        const float Radius = FMath::Lerp(0.25f, 0.42f, Value);
        const FLinearColor Warning(1.0f, 0.12f, 0.14f, Value);
        P.Arc(0.5f, 0.5f, Radius, Radius, HALF_PI, 3.0f * HALF_PI, Warning, 2.0f, 18);
        P.Arc(0.5f, 0.5f, Radius, Radius, -HALF_PI, HALF_PI, Warning, 2.0f, 18);
    }

    void Power(const FPainter& P, const int32 Variant)
    {
        const FLinearColor Ink(0.36f, 0.88f, 1.0f);
        const FLinearColor Halo(0.06f, 0.36f, 0.57f, 0.55f);
        const auto Stroke = [&](float X0, float Y0, float X1, float Y1, float Width = 3.0f)
        {
            P.Line(X0, Y0, X1, Y1, Halo, Width + 3.0f);
            P.Line(X0, Y0, X1, Y1, Ink, Width);
        };
        switch (Variant)
        {
        case 0: // Biotic palm and fingers.
            P.Arc(0.50f, 0.59f, 0.20f, 0.24f, 0.0f, PI, Ink, 4.0f);
            for (int32 Finger = 0; Finger < 4; ++Finger)
            {
                const float X = 0.33f + Finger * 0.11f;
                Stroke(X, 0.57f, X - 0.03f, 0.19f + FMath::Abs(Finger - 1.5f) * 0.07f, 4.0f);
            }
            Stroke(0.35f, 0.60f, 0.19f, 0.40f, 4.0f);
            Stroke(0.43f, 0.78f, 0.57f, 0.78f, 4.0f);
            break;
        case 1:
        case 7: // Accelerating projectile.
            for (int32 Trail = 0; Trail < 3; ++Trail)
            { Stroke(0.12f + Trail * 0.07f, 0.32f + Trail * 0.18f, 0.62f, 0.32f + Trail * 0.18f); }
            Stroke(0.48f, 0.22f, 0.84f, 0.50f, 4.0f);
            Stroke(0.84f, 0.50f, 0.48f, 0.78f, 4.0f);
            break;
        case 2: // Radial nova.
            P.Arc(0.50f, 0.50f, 0.16f, 0.16f, 0.0f, TWO_PI, Ink, 3.0f, 24);
            for (int32 Ray = 0; Ray < 8; ++Ray)
            {
                const float Angle = Ray * PI * 0.25f;
                Stroke(0.50f + 0.24f * FMath::Cos(Angle), 0.50f + 0.24f * FMath::Sin(Angle),
                    0.50f + 0.40f * FMath::Cos(Angle), 0.50f + 0.40f * FMath::Sin(Angle));
            }
            break;
        case 3: // Electric discharge.
            Stroke(0.57f, 0.10f, 0.32f, 0.50f, 4.0f);
            Stroke(0.32f, 0.50f, 0.64f, 0.44f, 4.0f);
            Stroke(0.64f, 0.44f, 0.43f, 0.89f, 4.0f);
            Stroke(0.22f, 0.25f, 0.15f, 0.16f, 1.5f);
            Stroke(0.77f, 0.70f, 0.85f, 0.80f, 1.5f);
            break;
        case 4: // Flame silhouette.
            Stroke(0.48f, 0.12f, 0.32f, 0.42f, 4.0f);
            Stroke(0.32f, 0.42f, 0.24f, 0.64f, 4.0f);
            P.Arc(0.50f, 0.62f, 0.26f, 0.25f, 0.0f, PI, Ink, 4.0f, 24);
            Stroke(0.76f, 0.62f, 0.68f, 0.30f, 4.0f);
            Stroke(0.68f, 0.30f, 0.53f, 0.47f, 3.0f);
            Stroke(0.53f, 0.47f, 0.48f, 0.12f, 3.0f);
            Stroke(0.49f, 0.70f, 0.53f, 0.54f, 5.0f);
            break;
        case 5: // Defensive barrier.
            Stroke(0.18f, 0.24f, 0.50f, 0.13f);
            Stroke(0.50f, 0.13f, 0.82f, 0.24f);
            Stroke(0.18f, 0.24f, 0.25f, 0.63f);
            Stroke(0.82f, 0.24f, 0.75f, 0.63f);
            Stroke(0.25f, 0.63f, 0.50f, 0.88f);
            Stroke(0.75f, 0.63f, 0.50f, 0.88f);
            Stroke(0.50f, 0.28f, 0.50f, 0.63f, 2.0f);
            Stroke(0.33f, 0.46f, 0.67f, 0.46f, 2.0f);
            break;
        case 6: // Singularity spiral.
            for (int32 Arm = 0; Arm < 3; ++Arm)
            {
                for (int32 Step = 0; Step < 18; ++Step)
                {
                    const float A0 = Arm * TWO_PI / 3.0f + Step * 0.12f;
                    const float A1 = A0 + 0.12f;
                    const float R0 = 0.10f + Step * 0.015f;
                    const float R1 = R0 + 0.015f;
                    Stroke(0.50f + R0 * FMath::Cos(A0), 0.50f + R0 * FMath::Sin(A0),
                        0.50f + R1 * FMath::Cos(A1), 0.50f + R1 * FMath::Sin(A1), 2.0f);
                }
            }
            break;
        default: break;
        }
    }

    void Rifle(const FPainter& P)
    {
        P.Line(0.09f, 0.46f, 0.84f, 0.46f, Blue, 1.5f);
        P.Line(0.20f, 0.37f, 0.76f, 0.37f, Blue, 1.0f);
        P.Line(0.25f, 0.37f, 0.30f, 0.25f, Blue, 1.0f);
        P.Line(0.30f, 0.25f, 0.52f, 0.25f, Blue, 1.0f);
        P.Line(0.52f, 0.25f, 0.56f, 0.37f, Blue, 1.0f);
        P.Line(0.08f, 0.46f, 0.10f, 0.67f, Blue, 1.5f);
        P.Line(0.10f, 0.67f, 0.30f, 0.67f, Blue, 1.0f);
        P.Line(0.30f, 0.67f, 0.38f, 0.46f, Blue, 1.0f);
        P.Line(0.42f, 0.46f, 0.46f, 0.78f, Blue, 1.2f);
        P.Line(0.46f, 0.78f, 0.56f, 0.78f, Blue, 1.2f);
        P.Line(0.56f, 0.78f, 0.59f, 0.46f, Blue, 1.2f);
        P.Line(0.84f, 0.40f, 0.84f, 0.52f, Blue, 1.4f);
        P.Line(0.84f, 0.46f, 0.96f, 0.46f, Blue, 1.4f);
    }

    void Reticle(const FPainter& P, const FLinearColor& Color)
    {
        const auto Stroke = [&](float Ax, float Ay, float Bx, float By)
        {
            P.Line(Ax, Ay, Bx, By, FLinearColor(0.0f, 0.025f, 0.035f, 0.90f), 4.0f);
            P.Line(Ax, Ay, Bx, By, Color, 1.5f);
        };
        Stroke(0.16f, 0.27f, 0.07f, 0.27f);
        Stroke(0.07f, 0.27f, 0.07f, 0.73f);
        Stroke(0.07f, 0.73f, 0.16f, 0.73f);
        Stroke(0.84f, 0.27f, 0.93f, 0.27f);
        Stroke(0.93f, 0.27f, 0.93f, 0.73f);
        Stroke(0.93f, 0.73f, 0.84f, 0.73f);
    }

    void Grenade(const FPainter& P, const FLinearColor& Color)
    {
        P.Arc(0.50f, 0.56f, 0.22f, 0.30f, 0.0f, 2.0f * PI, Color, 1.6f, 24);
        P.Line(0.42f, 0.27f, 0.42f, 0.15f, Color, 1.5f);
        P.Line(0.42f, 0.15f, 0.61f, 0.15f, Color, 1.5f);
        P.Line(0.61f, 0.15f, 0.61f, 0.29f, Color, 1.5f);
        P.Line(0.61f, 0.18f, 0.82f, 0.12f, Color, 1.5f);
        P.Line(0.37f, 0.43f, 0.63f, 0.43f, Color, 1.0f);
        P.Line(0.29f, 0.61f, 0.71f, 0.61f, Color, 1.0f);
    }

    void BattlefieldMap(const FPainter& P, const float Value)
    {
        const FLinearColor Ground(0.13f, 0.18f, 0.18f, 0.88f);
        const FLinearColor Water(0.10f, 0.23f, 0.26f, 0.79f);
        const FLinearColor Park(0.20f, 0.31f, 0.26f, 0.78f);
        const FLinearColor Road(0.49f, 0.55f, 0.53f, 0.78f);
        const FLinearColor Alley(0.30f, 0.36f, 0.35f, 0.73f);
        P.Box(0.0f, 0.0f, 1.0f, 1.0f, Ground);
        P.Box(0.00f, 0.00f, 0.30f, 0.23f, Water);
        P.Box(0.00f, 0.20f, 0.21f, 0.17f, Water);
        P.Box(0.00f, 0.34f, 0.12f, 0.17f, Water);
        P.Box(0.62f, 0.02f, 0.32f, 0.17f, Park);
        P.Box(0.72f, 0.16f, 0.25f, 0.12f, Park);
        P.Box(0.04f, 0.75f, 0.23f, 0.20f, Park);
        P.Line(0.09f, 0.00f, 0.34f, 0.28f, Road, 9.0f);
        P.Line(0.34f, 0.28f, 0.59f, 0.48f, Road, 9.0f);
        P.Line(0.59f, 0.48f, 1.00f, 0.69f, Road, 9.0f);
        P.Line(0.25f, 0.00f, 0.36f, 0.45f, Road, 6.0f);
        P.Line(0.36f, 0.45f, 0.30f, 1.00f, Road, 6.0f);
        P.Line(0.00f, 0.58f, 0.38f, 0.55f, Road, 8.0f);
        P.Line(0.38f, 0.55f, 0.76f, 0.44f, Road, 8.0f);
        P.Line(0.76f, 0.44f, 1.00f, 0.33f, Road, 8.0f);
        P.Line(0.63f, 0.00f, 0.57f, 0.44f, Road, 5.0f);
        P.Line(0.57f, 0.44f, 0.72f, 1.00f, Road, 5.0f);
        P.Line(0.00f, 0.84f, 0.32f, 0.74f, Alley, 4.0f);
        P.Line(0.32f, 0.74f, 0.67f, 0.83f, Alley, 4.0f);
        P.Line(0.67f, 0.83f, 1.00f, 0.89f, Alley, 4.0f);
        P.Line(0.47f, 0.12f, 0.91f, 0.31f, Alley, 3.0f);
        P.Line(0.14f, 0.41f, 0.54f, 0.34f, Alley, 3.0f);
        struct FBlock { float X; float Y; float W; float H; };
        constexpr FBlock Blocks[] = {
            {0.38f, 0.08f, 0.08f, 0.09f}, {0.49f, 0.08f, 0.07f, 0.13f},
            {0.72f, 0.31f, 0.07f, 0.08f}, {0.83f, 0.38f, 0.11f, 0.08f},
            {0.44f, 0.27f, 0.07f, 0.06f}, {0.15f, 0.40f, 0.12f, 0.07f},
            {0.42f, 0.62f, 0.11f, 0.08f}, {0.53f, 0.72f, 0.08f, 0.07f},
            {0.80f, 0.58f, 0.10f, 0.08f}, {0.80f, 0.74f, 0.08f, 0.06f},
            {0.40f, 0.88f, 0.12f, 0.06f}, {0.11f, 0.63f, 0.10f, 0.07f}
        };
        for (const FBlock& Block : Blocks)
        {
            P.Box(Block.X, Block.Y, Block.W, Block.H, FLinearColor(0.07f, 0.11f, 0.12f, 0.86f));
            P.Line(Block.X, Block.Y, Block.X + Block.W, Block.Y,
                FLinearColor(0.67f, 0.70f, 0.65f, 0.42f), 1.0f);
        }
        P.Line(0.02f, 0.02f, 0.98f, 0.02f, Blue, 1.5f);
        P.Line(0.98f, 0.02f, 0.98f, 0.98f, Blue, 1.5f);
        P.Line(0.98f, 0.98f, 0.02f, 0.98f, Blue, 1.5f);
        P.Line(0.02f, 0.98f, 0.02f, 0.02f, Blue, 1.5f);
        const float Phase = Value * 2.0f * PI;
        P.Diamond(0.22f, 0.56f, 0.038f, 0.038f, Blue, 2.2f);
        P.Diamond(0.79f, 0.47f, 0.038f, 0.038f, Amber, 2.2f);
        P.Diamond(0.46f, 0.78f, 0.027f, 0.027f, Blue, 1.8f);
        P.Box(0.66f + 0.07f * FMath::Cos(Phase), 0.29f + 0.07f * FMath::Sin(Phase),
            0.024f, 0.024f, Amber);
        P.Box(0.31f, 0.68f, 0.025f, 0.025f, FLinearColor(0.53f, 0.96f, 0.48f));
        P.Box(0.35f, 0.71f, 0.016f, 0.016f, FLinearColor(0.53f, 0.96f, 0.48f));
        P.Line(0.50f, 0.44f, 0.56f, 0.57f, White, 2.0f);
        P.Line(0.56f, 0.57f, 0.50f, 0.53f, White, 2.0f);
        P.Line(0.50f, 0.53f, 0.50f, 0.44f, White, 2.0f);
    }

    void BattlefieldKit(const FPainter& P, const int32 Variant)
    {
        const FLinearColor Squad(0.55f, 0.91f, 0.49f);
        P.Box(0.05f, 0.05f, 0.90f, 0.90f, FLinearColor(0.02f, 0.08f, 0.05f, 0.72f));
        P.Line(0.08f, 0.08f, 0.92f, 0.08f, Squad, 1.0f);
        P.Line(0.08f, 0.92f, 0.92f, 0.92f, Squad, 1.0f);
        switch (Variant)
        {
        case 0:
            P.Line(0.20f, 0.35f, 0.50f, 0.67f, Squad, 2.0f);
            P.Line(0.50f, 0.67f, 0.80f, 0.35f, Squad, 2.0f);
            P.Line(0.20f, 0.53f, 0.50f, 0.85f, Squad, 2.0f);
            P.Line(0.50f, 0.85f, 0.80f, 0.53f, Squad, 2.0f);
            break;
        case 1:
            P.Box(0.42f, 0.22f, 0.16f, 0.56f, Squad);
            P.Box(0.22f, 0.42f, 0.56f, 0.16f, Squad);
            break;
        case 2:
            P.Box(0.27f, 0.24f, 0.46f, 0.52f, Squad);
            P.Box(0.32f, 0.29f, 0.36f, 0.42f, FLinearColor(0.02f, 0.08f, 0.05f));
            P.Line(0.36f, 0.43f, 0.64f, 0.43f, Squad, 1.6f);
            P.Line(0.36f, 0.57f, 0.64f, 0.57f, Squad, 1.6f);
            break;
        case 3:
            P.Arc(0.50f, 0.50f, 0.27f, 0.27f, 0.0f, TWO_PI, Squad, 1.5f, 24);
            P.Line(0.50f, 0.14f, 0.50f, 0.40f, Squad, 1.5f);
            P.Line(0.50f, 0.60f, 0.50f, 0.86f, Squad, 1.5f);
            P.Line(0.14f, 0.50f, 0.40f, 0.50f, Squad, 1.5f);
            P.Line(0.60f, 0.50f, 0.86f, 0.50f, Squad, 1.5f);
            break;
        default: break;
        }
    }

    void Compass(const FPainter& P, const float Value)
    {
        P.Line(0.0f, 0.47f, 0.5f, 0.33f, Metal, 1.5f);
        P.Line(0.5f, 0.33f, 1.0f, 0.47f, Metal, 1.5f);
        P.Line(0.0f, 0.66f, 0.5f, 0.79f, Metal, 1.5f);
        P.Line(0.5f, 0.79f, 1.0f, 0.66f, Metal, 1.5f);
        for (int32 Index = 1; Index < 20; ++Index)
        {
            const float X = Index / 20.0f;
            P.Line(X, 0.49f, X, Index % 5 == 0 ? 0.70f : 0.61f, Metal, Index % 5 == 0 ? 1.5f : 1.0f);
        }
        P.Diamond(0.60f, 0.50f, 0.020f, 0.35f, White, 1.7f);
        P.Box(0.18f + 0.18f * Value, 0.49f, 0.008f, 0.13f, Red);
        P.Box(0.72f, 0.49f, 0.008f, 0.13f, Red);
    }

    void SkyrimMeter(const FPainter& P, const float Value, const int32 Variant)
    {
        const FLinearColor Fill = Variant == 0 ? FLinearColor(0.27f, 0.65f, 1.0f)
            : Variant == 1 ? Red : FLinearColor(0.40f, 0.82f, 0.34f);
        P.Box(0.06f, 0.40f, 0.88f, 0.22f, FLinearColor(0.025f, 0.027f, 0.031f, 0.90f));
        P.Line(0.05f, 0.30f, 0.95f, 0.30f, Metal, 1.3f);
        P.Line(0.05f, 0.72f, 0.95f, 0.72f, Metal, 1.3f);
        P.Line(0.01f, 0.51f, 0.05f, 0.30f, Metal, 1.4f);
        P.Line(0.01f, 0.51f, 0.05f, 0.72f, Metal, 1.4f);
        P.Line(0.99f, 0.51f, 0.95f, 0.30f, Metal, 1.4f);
        P.Line(0.99f, 0.51f, 0.95f, 0.72f, Metal, 1.4f);
        P.Line(0.08f, 0.35f, 0.92f, 0.35f, FLinearColor(0.22f, 0.22f, 0.23f), 1.0f);
        P.Line(0.08f, 0.67f, 0.92f, 0.67f, FLinearColor(0.22f, 0.22f, 0.23f), 1.0f);
        P.Line(0.5f - 0.43f * Value, 0.51f, 0.5f + 0.43f * Value, 0.51f,
            Fill * FLinearColor(0.43f, 0.43f, 0.43f, 1.0f), 6.0f);
        P.Line(0.5f - 0.43f * Value, 0.47f, 0.5f + 0.43f * Value, 0.47f, Fill, 2.0f);
        for (const float End : {0.035f, 0.965f})
        {
            P.Diamond(End, 0.51f, 0.019f, 0.23f, Metal, 1.0f);
            P.Diamond(End, 0.51f, 0.009f, 0.12f, FLinearColor(0.79f, 0.80f, 0.76f), 0.8f);
        }
        P.Diamond(0.5f, 0.51f, 0.010f, 0.27f, White, 1.1f);
    }

    void Eye(const FPainter& P, const float Value)
    {
        const float Opening = 0.02f + 0.30f * Value;
        P.Arc(0.5f, 0.5f + Opening, 0.42f, Opening, -PI, 0.0f, White, 1.8f, 24);
        P.Arc(0.5f, 0.5f - Opening, 0.42f, Opening, 0.0f, PI, White, 1.8f, 24);
        if (Value > 0.08f)
        { P.Arc(0.5f, 0.5f, 0.08f, 0.18f * Value, 0.0f, 2.0f * PI, White, 1.2f, 20); }
    }

    void Damage(const FPainter& P, const float Value)
    {
        if (Value <= 0.0f)
        { return; }
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const float Distance = Index * 0.016f;
            const float Alpha = Value * (0.13f - Index * 0.012f);
            const FLinearColor Edge(0.90f, 0.025f, 0.035f, Alpha);
            P.Line(Distance, 0.0f, Distance, 1.0f, Edge, 8.0f);
            P.Line(1.0f - Distance, 0.0f, 1.0f - Distance, 1.0f, Edge, 8.0f);
            P.Line(0.0f, Distance, 1.0f, Distance, Edge, 8.0f);
            P.Line(0.0f, 1.0f - Distance, 1.0f, 1.0f - Distance, Edge, 8.0f);
        }
    }
}

void SCkCombatHudReplicaArt::Construct(const FArguments&)
{
    SetVisibility(EVisibility::SelfHitTestInvisible);
    SetCanTick(true);
}

void SCkCombatHudReplicaArt::Configure(const ECkCombatHudReplicaPiece InPiece, const int32 InVariant,
    TAttribute<float> InValue, TAttribute<float> InOpacity)
{
    _Piece = InPiece;
    _Variant = InVariant;
    _Value = MoveTemp(InValue);
    _OpacityBinding = MoveTemp(InOpacity);
    SetClipping(InPiece == ECkCombatHudReplicaPiece::BfMap
        ? EWidgetClipping::ClipToBounds : EWidgetClipping::Inherit);
    Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}

void SCkCombatHudReplicaArt::AdvancePresentation(const float InDeltaTime)
{
    if (_Frozen)
    { return; }
    const float DeltaTime = FMath::IsFinite(InDeltaTime) ? FMath::Clamp(InDeltaTime, 0.0f, 1.0f) : 0.0f;
    const float Value = ck_combat_hud_art::Unit(_Value.Get(1.0f), 0.0f);
    const float Opacity = ck_combat_hud_art::Unit(_OpacityBinding.Get(1.0f), 0.0f);
    if (!_Initialized)
    {
        _Displayed = Value;
        _Opacity = Opacity;
        _Initialized = true;
    }
    else
    {
        _Displayed = FMath::Lerp(_Displayed, Value, 1.0f - FMath::Exp(-DeltaTime / 0.15f));
        _Opacity = FMath::Lerp(_Opacity, Opacity, 1.0f - FMath::Exp(-DeltaTime / 0.20f));
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SCkCombatHudReplicaArt::Freeze()
{
    _Frozen = true;
    Invalidate(EInvalidateWidgetReason::Paint);
}

void SCkCombatHudReplicaArt::Tick(const FGeometry& InGeometry, const double InCurrentTime, const float InDeltaTime)
{
    SLeafWidget::Tick(InGeometry, InCurrentTime, InDeltaTime);
    AdvancePresentation(InDeltaTime);
}

auto SCkCombatHudReplicaArt::ComputeDesiredSize(float) const -> FVector2D
{
    switch (_Piece)
    {
    case ECkCombatHudReplicaPiece::MeShield: return FVector2D(320.0f, 32.0f);
    case ECkCombatHudReplicaPiece::MeHealth: return FVector2D(280.0f, 20.0f);
    case ECkCombatHudReplicaPiece::MeCooldown: return FVector2D(110.0f, 110.0f);
    case ECkCombatHudReplicaPiece::MePower: return FVector2D(38.0f, 38.0f);
    case ECkCombatHudReplicaPiece::MeRifle: return FVector2D(104.0f, 36.0f);
    case ECkCombatHudReplicaPiece::MeReticle: return FVector2D(84.0f, 48.0f);
    case ECkCombatHudReplicaPiece::MeGrenade: return FVector2D(32.0f, 36.0f);
    case ECkCombatHudReplicaPiece::Crosshair: return FVector2D(32.0f, 32.0f);
    case ECkCombatHudReplicaPiece::BfMap: return FVector2D(210.0f, 210.0f);
    case ECkCombatHudReplicaPiece::BfObjective: return FVector2D(28.0f, 28.0f);
    case ECkCombatHudReplicaPiece::BfGrenade: return FVector2D(28.0f, 28.0f);
    case ECkCombatHudReplicaPiece::BfKit: return FVector2D(26.0f, 26.0f);
    case ECkCombatHudReplicaPiece::SkCompass: return FVector2D(600.0f, 28.0f);
    case ECkCombatHudReplicaPiece::SkMeter: return FVector2D(450.0f, 20.0f);
    case ECkCombatHudReplicaPiece::SkEye: return FVector2D(48.0f, 24.0f);
    case ECkCombatHudReplicaPiece::Damage: return FVector2D(1920.0f, 1080.0f);
    }
    return FVector2D(32.0f, 32.0f);
}

auto SCkCombatHudReplicaArt::OnPaint(const FPaintArgs&, const FGeometry& InGeometry,
    const FSlateRect&, FSlateWindowElementList& OutDrawElements, const int32 InLayer,
    const FWidgetStyle& InWidgetStyle, bool) const -> int32
{
    if (_Frozen || _Opacity <= 0.0f || InGeometry.GetLocalSize().IsNearlyZero())
    { return InLayer; }
    const ck_combat_hud_art::FPainter P(InGeometry, OutDrawElements, InLayer,
        _Opacity, InWidgetStyle.GetColorAndOpacityTint());
    using namespace ck_combat_hud_art;
    switch (_Piece)
    {
    case ECkCombatHudReplicaPiece::MeShield: Shield(P, _Displayed); break;
    case ECkCombatHudReplicaPiece::MeHealth: Health(P, _Displayed); break;
    case ECkCombatHudReplicaPiece::MeCooldown: Cooldown(P, _Displayed); break;
    case ECkCombatHudReplicaPiece::MePower: Power(P, _Variant); break;
    case ECkCombatHudReplicaPiece::MeRifle: Rifle(P); break;
    case ECkCombatHudReplicaPiece::MeReticle: Reticle(P, Blue); break;
    case ECkCombatHudReplicaPiece::MeGrenade: Grenade(P, Blue); break;
    case ECkCombatHudReplicaPiece::Crosshair:
        for (const float Thickness : {3.2f, 1.3f})
        {
            const FLinearColor Color = Thickness > 2.0f
                ? FLinearColor(0.01f, 0.04f, 0.05f, 0.92f) : White;
            P.Line(0.5f, 0.04f, 0.5f, 0.34f, Color, Thickness);
            P.Line(0.5f, 0.66f, 0.5f, 0.96f, Color, Thickness);
            P.Line(0.04f, 0.5f, 0.34f, 0.5f, Color, Thickness);
            P.Line(0.66f, 0.5f, 0.96f, 0.5f, Color, Thickness);
        }
        break;
    case ECkCombatHudReplicaPiece::BfMap: BattlefieldMap(P, _Displayed); break;
    case ECkCombatHudReplicaPiece::BfObjective:
        P.Diamond(0.5f, 0.44f, 0.33f, 0.33f, FLinearColor(0.01f, 0.05f, 0.07f, 0.90f), 5.0f);
        P.Diamond(0.5f, 0.44f, 0.33f, 0.33f, _Variant % 2 == 0 ? Blue : Amber, 2.4f);
        P.Arc(0.5f, 0.44f, 0.11f, 0.17f, 0.25f * PI, 1.75f * PI, White, 1.8f, 24);
        P.Line(0.50f, 0.77f, 0.50f, 0.96f, White, 1.5f);
        break;
    case ECkCombatHudReplicaPiece::BfGrenade:
        P.Box(0.37f, 0.34f, 0.28f, 0.51f, FLinearColor(0.02f, 0.05f, 0.05f, 0.75f));
        Grenade(P, White);
        P.Line(0.35f, 0.53f, 0.66f, 0.53f, White, 1.0f);
        P.Arc(0.69f, 0.22f, 0.13f, 0.09f, 0.0f, PI, White, 1.4f, 12);
        break;
    case ECkCombatHudReplicaPiece::BfKit: BattlefieldKit(P, _Variant); break;
    case ECkCombatHudReplicaPiece::SkCompass: Compass(P, _Displayed); break;
    case ECkCombatHudReplicaPiece::SkMeter: SkyrimMeter(P, _Displayed, _Variant); break;
    case ECkCombatHudReplicaPiece::SkEye: Eye(P, _Displayed); break;
    case ECkCombatHudReplicaPiece::Damage: Damage(P, _Displayed); break;
    }
    return InLayer;
}

auto FCkCombatHudReplicaArt::Register(FCkUiWidgetRegistry& InRegistry) -> FCkUiLoadResult
{
    FCkUiWidgetRegistry Candidate = InRegistry;
    FCkUiCustomWidgetRegistration Art;
    Art.Schema.Tag = TEXT("replica-art");
    Art.Schema.Properties = {
        {TEXT("piece"), ECkUiCustomPropertyKind::Text, true},
        {TEXT("value"), ECkUiCustomPropertyKind::NumberBinding, false},
        {TEXT("opacity"), ECkUiCustomPropertyKind::NumberBinding, false},
        {TEXT("variant"), ECkUiCustomPropertyKind::Number, false}
    };
    Art.RetainedFactory = [](const FCkUiCustomWidgetArguments& InArguments, FString& OutFailure)
        -> TSharedPtr<ICkUiRetainedWidget>
    {
        ck_combat_hud_art::FConfiguration Configuration;
        if (!ck_combat_hud_art::MakeConfiguration(InArguments, Configuration, OutFailure))
        { return {}; }
        const TSharedRef<ck_combat_hud_art::FAdapter> Adapter =
            MakeShared<ck_combat_hud_art::FAdapter>(MoveTemp(Configuration));
        Adapter->Initialize(InArguments.Id);
        return Adapter;
    };
    const FCkUiLoadResult Result = Candidate.Register(MoveTemp(Art));
    if (Result.Succeeded)
    { InRegistry = MoveTemp(Candidate); }
    return Result;
}

#include "UI/CkJournalProofArt.h"

#include "Algo/Find.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

namespace ck_journal_art
{
    enum class EPiece : uint8 { Frame, TabRail, TitleOrnament, FooterLine, TabMarkers };

    auto
        ParsePiece(const FCkUiCustomWidgetArguments& InArguments, EPiece& OutPiece,
            TAttribute<float>& OutValue, FString& OutFailure)
        -> bool
    {
        const FText* Piece = InArguments.TextProperties.Find(TEXT("piece"));
        if (InArguments.Id.IsEmpty() || Piece == nullptr)
        {
            OutFailure = TEXT("Journal art requires an id and piece.");
            return false;
        }
        const TPair<const TCHAR*, EPiece> Pieces[] = {
            {TEXT("frame"), EPiece::Frame}, {TEXT("tab-rail"), EPiece::TabRail},
            {TEXT("title-ornament"), EPiece::TitleOrnament}, {TEXT("footer-line"), EPiece::FooterLine},
            {TEXT("tab-markers"), EPiece::TabMarkers}
        };
        const auto* Found = Algo::FindByPredicate(Pieces, [Piece](const auto& InEntry)
        { return Piece->ToString() == InEntry.Key; });
        if (Found == nullptr || !InArguments.Style.CustomProperties.IsEmpty())
        {
            OutFailure = TEXT("Journal art piece or custom style is unsupported.");
            return false;
        }
        const TAttribute<float>* Value = InArguments.NumberBindings.Find(TEXT("value"));
        if (Value != nullptr && !Value->IsSet())
        {
            OutFailure = TEXT("Journal art value binding must be set.");
            return false;
        }
        OutPiece = Found->Value;
        OutValue = Value != nullptr ? *Value : TAttribute<float>(0.0f);
        return true;
    }

    class SArt final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SArt) {} SLATE_END_ARGS()

        void Construct(const FArguments&)
        { SetVisibility(EVisibility::HitTestInvisible); }

        auto Configure(EPiece InPiece, TAttribute<float> InValue) -> void
        {
            if (_Frozen) { return; }
            _Piece = InPiece;
            _Value = MoveTemp(InValue);
            Invalidate(EInvalidateWidgetReason::Paint);
        }

        auto Freeze() -> void
        {
            if (_Frozen) { return; }
            _LastValue = ReadValue();
            _Value = TAttribute<float>(_LastValue);
            _Frozen = true;
        }

        auto ComputeDesiredSize(float) const -> FVector2D override
        { return FVector2D(16.0f, 16.0f); }

        auto OnPaint(const FPaintArgs&, const FGeometry& InGeometry, const FSlateRect&,
            FSlateWindowElementList& InElements, int32 InLayer, const FWidgetStyle& InStyle,
            bool) const -> int32 override
        {
            const FVector2D Size = InGeometry.GetLocalSize();
            if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X <= 0.0 || Size.Y <= 0.0)
            { return InLayer; }
            const FLinearColor Ink = FLinearColor(0.64f, 0.64f, 0.61f, 0.84f) * InStyle.GetColorAndOpacityTint();
            const FLinearColor Bright = FLinearColor(0.89f, 0.89f, 0.85f, 1.0f) * InStyle.GetColorAndOpacityTint();
            auto Line = [&](std::initializer_list<FVector2f> InPoints, FLinearColor InColor, float InThickness = 1.4f)
            {
                auto Points = TArray<FVector2f>{};
                for (const FVector2f Point : InPoints)
                { Points.Add(FVector2f(Point.X * Size.X, Point.Y * Size.Y)); }
                FSlateDrawElement::MakeLines(InElements, InLayer + 1, InGeometry.ToPaintGeometry(),
                    MoveTemp(Points), ESlateDrawEffect::None, InColor, true, InThickness);
            };
            auto Diamond = [&](float InX, float InY, float InRadius, FLinearColor InColor)
            {
                const float X = InRadius / static_cast<float>(Size.X);
                const float Y = InRadius / static_cast<float>(Size.Y);
                Line({{InX, InY - Y}, {InX + X, InY}, {InX, InY + Y},
                    {InX - X, InY}, {InX, InY - Y}}, InColor);
            };
            if (_Piece == EPiece::Frame)
            {
                Line({{0.08f, 0.0f}, {0.92f, 0.0f}, {1.0f, 0.15f}, {1.0f, 1.0f},
                    {0.0f, 1.0f}, {0.0f, 0.15f}, {0.08f, 0.0f}}, Ink);
                Line({{0.083f, 0.007f}, {0.917f, 0.007f}, {0.996f, 0.153f}, {0.996f, 0.992f},
                    {0.004f, 0.992f}, {0.004f, 0.153f}, {0.083f, 0.007f}}, Ink * 0.52f, 1.0f);
                Line({{0.31f, 0.0f}, {0.31f, 1.0f}}, Ink * 0.8f);
                Line({{0.306f, 0.0f}, {0.306f, 1.0f}}, Ink * 0.4f, 1.0f);
                for (const float X : {0.0f, 1.0f})
                {
                    const float Direction = X == 0.0f ? 1.0f : -1.0f;
                    Line({{X, 0.96f}, {X + Direction * 0.016f, 0.96f},
                        {X + Direction * 0.016f, 1.0f}, {X + Direction * 0.009f, 1.0f},
                        {X + Direction * 0.009f, 0.972f}, {X + Direction * 0.026f, 0.972f}}, Ink);
                }
            }
            else if (_Piece == EPiece::TabRail)
            {
                Line({{0.026f, 0.0f}, {0.974f, 0.0f}, {1.0f, 0.5f}, {0.974f, 1.0f},
                    {0.026f, 1.0f}, {0.0f, 0.5f}, {0.026f, 0.0f}}, Ink);
                Line({{0.03f, 0.08f}, {0.97f, 0.08f}, {0.992f, 0.5f}, {0.97f, 0.92f},
                    {0.03f, 0.92f}, {0.008f, 0.5f}, {0.03f, 0.08f}}, Ink * 0.6f, 1.0f);
                Diamond(0.018f, 0.5f, 10.0f, Ink);
                Diamond(0.982f, 0.5f, 10.0f, Ink);
            }
            else if (_Piece == EPiece::TitleOrnament)
            {
                auto Curve = [&](FVector2f InA, FVector2f InB, FVector2f InC,
                    FVector2f InD, FLinearColor InColor, float InThickness)
                {
                    auto Points = TArray<FVector2f>{};
                    Points.Reserve(19);
                    for (int32 Step = 0; Step <= 18; ++Step)
                    {
                        const float T = static_cast<float>(Step) / 18.0f;
                        const float S = 1.0f - T;
                        const FVector2f Point = InA * (S * S * S) + InB * (3.0f * S * S * T)
                            + InC * (3.0f * S * T * T) + InD * (T * T * T);
                        Points.Add(FVector2f(Point.X * Size.X, Point.Y * Size.Y));
                    }
                    FSlateDrawElement::MakeLines(InElements, InLayer + 1,
                        InGeometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None,
                        InColor, true, InThickness);
                };
                for (const float Direction : {-1.0f, 1.0f})
                {
                    const float Center = Direction < 0.0f ? 0.12f : 0.88f;
                    auto Knot = [&](float InX, float InY)
                    { return FVector2f(Center + Direction * InX, InY); };
                    Curve(Knot(-0.10f, 0.52f), Knot(-0.065f, 0.12f),
                        Knot(0.015f, 0.15f), Knot(0.08f, 0.50f), Ink, 1.5f);
                    Curve(Knot(-0.10f, 0.52f), Knot(-0.055f, 0.83f),
                        Knot(0.02f, 0.85f), Knot(0.08f, 0.50f), Ink, 1.5f);
                    Curve(Knot(-0.073f, 0.31f), Knot(-0.015f, 0.76f),
                        Knot(0.025f, 0.24f), Knot(0.066f, 0.60f), Ink * 0.78f, 1.1f);
                    Curve(Knot(-0.073f, 0.68f), Knot(-0.015f, 0.24f),
                        Knot(0.025f, 0.76f), Knot(0.066f, 0.40f), Ink * 0.78f, 1.1f);
                    Diamond(Center, 0.5f, 7.0f, Ink);
                }
            }
            else if (_Piece == EPiece::FooterLine)
            { Line({{0.0f, 0.5f}, {1.0f, 0.5f}}, Ink); }
            else
            {
                const int32 Index = FMath::RoundToInt(ReadValue());
                const float Center = (static_cast<float>(Index) + 0.5f) / 3.0f;
                const float Span = Index == 1 ? 0.102f : 0.060f;
                Diamond(Center - Span, 0.5f, 10.0f, Bright);
                Diamond(Center + Span, 0.5f, 10.0f, Bright);
                Line({{Center - Span + 0.004f, 0.36f}, {Center - Span - 0.004f, 0.5f},
                    {Center - Span + 0.004f, 0.64f}}, Bright);
                Line({{Center + Span - 0.004f, 0.36f}, {Center + Span + 0.004f, 0.5f},
                    {Center + Span - 0.004f, 0.64f}}, Bright);
            }
            return InLayer + 1;
        }

    private:
        auto ReadValue() const -> float
        {
            const float Value = _Frozen ? _LastValue : _Value.Get(0.0f);
            _LastValue = FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 2.0f) : 0.0f;
            return _LastValue;
        }
        EPiece _Piece = EPiece::Frame;
        TAttribute<float> _Value{0.0f};
        mutable float _LastValue = 0.0f;
        bool _Frozen = false;
    };

    class FAdapter final : public ICkUiRetainedWidget, public TSharedFromThis<FAdapter>
    {
    public:
        FAdapter(EPiece InPiece, TAttribute<float> InValue, FString InId)
        {
            SAssignNew(_Widget, SArt).Tag(FName(*InId));
            _Widget->Configure(InPiece, MoveTemp(InValue));
        }
        ~FAdapter() override { _Widget->Freeze(); }
        auto GetWidget() const -> TSharedRef<SWidget> override { return _Widget.ToSharedRef(); }
        auto ReleaseOwnerInteraction() -> void override { _Widget->Freeze(); }
        auto PrepareReload(const FCkUiCustomWidgetArguments& InArguments, FString& OutFailure) const
            -> TUniquePtr<ICkUiPreparedWidgetUpdate> override
        {
            EPiece Piece;
            TAttribute<float> Value;
            if (!ParsePiece(InArguments, Piece, Value, OutFailure)) { return {}; }
            return MakeUnique<FUpdate>(_Widget.ToSharedRef(), Piece, MoveTemp(Value));
        }
    private:
        class FUpdate final : public ICkUiPreparedWidgetUpdate
        {
        public:
            FUpdate(TSharedRef<SArt> InWidget, EPiece InPiece, TAttribute<float> InValue)
                : _Widget(MoveTemp(InWidget)), _Piece(InPiece), _Value(MoveTemp(InValue)) {}
            auto Commit() noexcept -> void override { _Widget->Configure(_Piece, MoveTemp(_Value)); }
        private:
            TSharedRef<SArt> _Widget;
            EPiece _Piece;
            TAttribute<float> _Value;
        };
        TSharedPtr<SArt> _Widget;
    };
}

auto
    RegisterJournalArt(FCkUiWidgetRegistry& InRegistry)
    -> FCkUiLoadResult
{
    FCkUiWidgetRegistry Candidate = InRegistry;
    auto Art = FCkUiCustomWidgetRegistration{};
    Art.Schema.Tag = TEXT("journal-art");
    Art.Schema.Properties = {{TEXT("piece"), ECkUiCustomPropertyKind::Text, true},
        {TEXT("value"), ECkUiCustomPropertyKind::NumberBinding, false}};
    Art.RetainedFactory = [](const FCkUiCustomWidgetArguments& InArguments, FString& OutFailure)
        -> TSharedPtr<ICkUiRetainedWidget>
    {
        ck_journal_art::EPiece Piece;
        TAttribute<float> Value;
        if (!ck_journal_art::ParsePiece(InArguments, Piece, Value, OutFailure)) { return {}; }
        return MakeShared<ck_journal_art::FAdapter>(Piece, MoveTemp(Value), InArguments.Id);
    };
    const FCkUiLoadResult Result = Candidate.Register(MoveTemp(Art));
    if (!Result.Succeeded) { return Result; }
    InRegistry = MoveTemp(Candidate);
    return Result;
}

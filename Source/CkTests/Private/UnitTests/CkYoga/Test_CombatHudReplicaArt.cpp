#include "UI/CkCombatHudReplicaArt.h"
#include "CkSlateLayout/SCkUiSurface.h"

#include "Framework/Application/SlateApplication.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_combat_hud_replica_test
{
    auto FindTagged(const TSharedRef<SWidget>& InRoot, const FName InTag)
        -> TSharedPtr<SCkCombatHudReplicaArt>
    {
        if (InRoot->GetTag() == InTag && InRoot->GetTypeAsString() == TEXT("SCkCombatHudReplicaArt"))
        { return StaticCastSharedRef<SCkCombatHudReplicaArt>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SCkCombatHudReplicaArt> Found =
                FindTagged(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InTag))
            { return Found; }
        }
        return {};
    }

    struct FWindowScope
    {
        FSlateApplication& Slate;
        TSharedPtr<SWindow> Window;
        ~FWindowScope()
        {
            if (Window.IsValid())
            { Slate.DestroyWindowImmediately(Window.ToSharedRef()); }
        }
    };

    auto Markup(const TCHAR* InPiece = TEXT("me-shield"), const TCHAR* InVariant = TEXT("1")) -> FString
    {
        return FString::Printf(TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
            "<replica-art id=\"art\" piece=\"%s\" variant=\"%s\" value-bind=\"health\" "
            "opacity-bind=\"alpha\" />"
            "</column></region></ui>"), InPiece, InVariant);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkCombatHudReplicaArtNative,
    "Ck.UiAuthoring.CombatHudProof.ReplicaArt.NativeRetentionAndFreeze",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkCombatHudReplicaArtNative::RunTest(const FString&) -> bool
{
    using namespace ck_combat_hud_replica_test;
    if (!FSlateApplication::IsInitialized())
    {
        AddError(TEXT("Replica art requires initialized Slate."));
        return false;
    }
    FCkUiWidgetRegistry Registry;
    if (!TestTrue(TEXT("Replica art registers"), FCkCombatHudReplicaArt::Register(Registry).Succeeded))
    { return false; }
    const TSharedRef<const FCkUiWidgetRegistrySnapshot> Snapshot = Registry.CreateSnapshot();
    const FCkUiCustomWidgetRegistration* Registration = Snapshot->Find(TEXT("replica-art"));
    if (!TestTrue(TEXT("Replica art has a retained factory"),
        Registration != nullptr && !!Registration->RetainedFactory))
    { return false; }

    FString Failure;
    FCkUiCustomWidgetArguments Invalid;
    TestFalse(TEXT("Missing id and piece reject"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Required error is explicit"), Failure, FString(TEXT("Replica art requires an id and piece.")));
    Invalid.Id = TEXT("piece");
    Invalid.TextProperties.Add(TEXT("piece"), FText::FromString(TEXT("unknown")));
    Failure.Reset();
    TestFalse(TEXT("Unknown piece rejects"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Unknown piece error is exact"), Failure, FString(TEXT("Replica art piece is unknown.")));
    Invalid.TextProperties.Add(TEXT("piece"), FText::FromString(TEXT("sk-meter")));
    Invalid.NumberProperties.Add(TEXT("variant"), 8.0f);
    Failure.Reset();
    TestFalse(TEXT("Out-of-range variant rejects"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Variant error is exact"), Failure,
        FString(TEXT("Replica art variant must be an integer from 0 to 7.")));
    Invalid.NumberProperties.Add(TEXT("variant"), std::numeric_limits<float>::quiet_NaN());
    Failure.Reset();
    TestFalse(TEXT("Nonfinite variant rejects"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    Invalid.NumberProperties.Add(TEXT("variant"), 1.5f);
    Failure.Reset();
    TestFalse(TEXT("Fractional variant rejects"), Registration->RetainedFactory(Invalid, Failure).IsValid());
    Invalid.NumberProperties.Add(TEXT("variant"), 1.0f);
    FCkUiCustomStyleValue Unexpected;
    Unexpected.Kind = ECkUiCustomStyleKind::Number;
    Unexpected.Number = std::numeric_limits<float>::quiet_NaN();
    Invalid.Style.CustomProperties.Add(TEXT("-ck-replica-unknown"), Unexpected);
    Failure.Reset();
    TestFalse(TEXT("Undeclared style rejects without partial control"),
        Registration->RetainedFactory(Invalid, Failure).IsValid());
    TestEqual(TEXT("Style error is exact"), Failure,
        FString(TEXT("Replica art does not accept custom style properties.")));
    Invalid.Style.CustomProperties.Reset();

    const TCHAR* Pieces[] = { TEXT("me-shield"), TEXT("me-health"), TEXT("me-cooldown"),
        TEXT("me-power"), TEXT("me-rifle"), TEXT("me-reticle"), TEXT("me-grenade"),
        TEXT("crosshair"), TEXT("bf-map"), TEXT("bf-objective"), TEXT("bf-grenade"), TEXT("bf-kit"),
        TEXT("sk-compass"), TEXT("sk-meter"), TEXT("sk-eye"), TEXT("damage") };
    for (const TCHAR* Piece : Pieces)
    {
        Invalid.TextProperties.Add(TEXT("piece"), FText::FromString(Piece));
        Failure.Reset();
        TSharedPtr<ICkUiRetainedWidget> Control = Registration->RetainedFactory(Invalid, Failure);
        if (!TestTrue(*FString::Printf(TEXT("%s constructs as retained artwork: %s"), Piece, *Failure),
            Control.IsValid()))
        { return false; }
        const TSharedRef<SCkCombatHudReplicaArt> Native =
            StaticCastSharedRef<SCkCombatHudReplicaArt>(Control->GetWidget());
        TestTrue(*FString::Printf(TEXT("%s has a positive native desired size"), Piece),
            Native->GetDesiredSize().X > 0.0f || Native->ComputeDesiredSize(1.0f).X > 0.0f);
        TestEqual(*FString::Printf(TEXT("%s is passive"), Piece),
            Native->GetVisibility(), EVisibility::SelfHitTestInvisible);
        Control->ReleaseOwnerInteraction();
        TestTrue(*FString::Printf(TEXT("%s freezes"), Piece), Native->IsFrozen());
    }

    float Health = 1.0f;
    float Alpha = 1.0f;
    int32 Reads = 0;
    FCkUiView::FDataBindings Data;
    Data.Number.Add(TEXT("health"), TAttribute<float>::CreateLambda([&Health, &Reads]()
    {
        ++Reads;
        return Health;
    }));
    Data.Number.Add(TEXT("alpha"), TAttribute<float>::CreateLambda([&Alpha, &Reads]()
    {
        ++Reads;
        return Alpha;
    }));
    TSharedPtr<FCkUiView> View = FCkUiView::Create({}, {}, {},
        FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12), MoveTemp(Data), Registry.CreateSnapshot());
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    const FCkUiLoadResult Initial = View->TryReload(Markup(), TEXT(""), TEXT("ReplicaInitial"));
    if (!TestTrue(TEXT("Authored replica-art parses and mounts"), Initial.Succeeded))
    {
        AddError(FString::Join(Initial.Errors, TEXT("\n")));
        return false;
    }
    FSlateApplication& Slate = FSlateApplication::Get();
    FWindowScope Scope{Slate, {}};
    Scope.Window = SNew(SWindow).ClientSize(FVector2D(640.0f, 180.0f)).CreateTitleBar(false)[Region];
    Slate.AddWindow(Scope.Window.ToSharedRef(), true);
    Slate.PumpMessages();
    Slate.Tick();
    const TSharedPtr<SCkCombatHudReplicaArt> Native = FindTagged(Region, FName(TEXT("art")));
    if (!TestTrue(TEXT("Authored id resolves to native artwork"), Native.IsValid()))
    { return false; }
    Native->AdvancePresentation(0.0f);
    TestTrue(TEXT("Initial live health is presented"), FMath::IsNearlyEqual(Native->GetDisplayedFraction(), 1.0f));
    Health = 0.25f;
    Alpha = 0.1f;
    Native->AdvancePresentation(0.15f);
    TestTrue(TEXT("Live health and opacity ease rather than jump"),
        Native->GetDisplayedFraction() > 0.25f && Native->GetDisplayedFraction() < 1.0f
        && Native->GetDisplayedOpacity() > 0.1f && Native->GetDisplayedOpacity() < 1.0f);
    const float BeforeReload = Native->GetDisplayedFraction();
    const int64 Revision = View->GetRevision();
    const FCkUiLoadResult BadPiece = View->TryReload(Markup(TEXT("invalid")), TEXT(""), TEXT("BadPiece"));
    TestFalse(TEXT("Authored unknown piece rejects"), BadPiece.Succeeded);
    TestEqual(TEXT("Rejected piece preserves revision"), View->GetRevision(), Revision);
    TestTrue(TEXT("Rejected piece preserves native identity"), FindTagged(Region, FName(TEXT("art"))) == Native);
    TestTrue(TEXT("Rejected piece preserves presentation"),
        FMath::IsNearlyEqual(Native->GetDisplayedFraction(), BeforeReload));
    const FCkUiLoadResult BadVariant = View->TryReload(Markup(TEXT("me-shield"), TEXT("8")),
        TEXT(""), TEXT("BadVariant"));
    TestFalse(TEXT("Authored invalid variant rejects"), BadVariant.Succeeded);
    TestEqual(TEXT("Rejected variant preserves revision"), View->GetRevision(), Revision);
    if (!TestTrue(TEXT("Compatible artwork reload publishes"),
        View->TryReload(Markup(TEXT("me-health"), TEXT("2")), TEXT(""), TEXT("ReplicaUpdated")).Succeeded))
    { return false; }
    TestTrue(TEXT("Compatible reload retains native widget"), FindTagged(Region, FName(TEXT("art"))) == Native);
    TestTrue(TEXT("Compatible reload updates piece and variant without resetting presentation"),
        Native->GetPiece() == ECkCombatHudReplicaPiece::MeHealth && Native->GetVariant() == 2
        && FMath::IsNearlyEqual(Native->GetDisplayedFraction(), BeforeReload));
    Health = std::numeric_limits<float>::quiet_NaN();
    Alpha = std::numeric_limits<float>::infinity();
    Native->AdvancePresentation(1.0f);
    TestTrue(TEXT("Nonfinite live values remain total"),
        FMath::IsFinite(Native->GetDisplayedFraction()) && Native->GetDisplayedFraction() >= 0.0f
        && Native->GetDisplayedFraction() <= 1.0f && FMath::IsFinite(Native->GetDisplayedOpacity()));
    View->ReleaseOwnerInteractions();
    const float Frozen = Native->GetDisplayedFraction();
    const int32 BeforeReleaseReads = Reads;
    Native->AdvancePresentation(1.0f);
    Slate.Tick();
    TestTrue(TEXT("Owner release freezes native artwork without binding reads"),
        Native->IsFrozen() && FMath::IsNearlyEqual(Native->GetDisplayedFraction(), Frozen)
        && Reads == BeforeReleaseReads);
    View.Reset();
    TestTrue(TEXT("Held native artwork remains frozen after view destruction"), Native->IsFrozen());
    return true;
}

#endif

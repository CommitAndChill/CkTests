#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/CkUiWidgetRegistry.h"
#include "CkWidgets/Authoring/CkAuthoredSurface_Widget.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_authored_umg_surface
{
    struct FReleaseProbeState final
    {
        int32 Calls = 0;
        TFunction<void()> OnRelease;
    };

    class FNoopUpdate final : public ICkUiPreparedWidgetUpdate
    {
    public:
        virtual void Commit() noexcept override {}
    };

    class FReleaseProbe final : public ICkUiRetainedWidget
    {
    public:
        explicit FReleaseProbe(TSharedRef<FReleaseProbeState> InState)
            : State(MoveTemp(InState)), Widget(SNew(SBox)) {}

        virtual auto GetWidget() const -> TSharedRef<SWidget> override { return Widget; }
        virtual void ReleaseOwnerInteraction() override
        {
            ++State->Calls;
            TFunction<void()> Callback = MoveTemp(State->OnRelease);
            State->OnRelease = {};
            if (Callback) { Callback(); }
        }
        virtual auto PrepareReload(const FCkUiCustomWidgetArguments&, FString&) const
            -> TUniquePtr<ICkUiPreparedWidgetUpdate> override
        { return MakeUnique<FNoopUpdate>(); }

    private:
        TSharedRef<FReleaseProbeState> State;
        TSharedRef<SWidget> Widget;
    };

    auto Markup(const TCHAR* InRegion) -> FString
    {
        return FString::Printf(TEXT("<ui version=\"1\"><region name=\"%s\"><column id=\"root\">"
            "<button id=\"fire\" action=\"fire\">Fire</button></column></region></ui>"), InRegion);
    }

    auto MakeView(int32& InOutCalls) -> TSharedRef<FCkUiView>
    {
        auto Actions = FCkUiView::FActions{};
        Actions.Add(TEXT("fire"), FSimpleDelegate::CreateLambda([&InOutCalls]() { ++InOutCalls; }));
        const TSharedRef<FCkUiView> View = FCkUiView::Create({}, MoveTemp(Actions));
        View->GetRegion(TEXT("main"));
        return View;
    }

    auto MakeReleaseView(const TSharedRef<FReleaseProbeState>& InState) -> TSharedRef<FCkUiView>
    {
        FCkUiWidgetRegistry Registry;
        FCkUiCustomWidgetRegistration Registration;
        Registration.Schema.Tag = TEXT("release-probe");
        Registration.RetainedFactory = [InState](const FCkUiCustomWidgetArguments&, FString&)
            -> TSharedPtr<ICkUiRetainedWidget>
        { return MakeShared<FReleaseProbe>(InState); };
        Registry.Register(MoveTemp(Registration));
        const TSharedRef<FCkUiView> View = FCkUiView::Create({}, {}, {}, {}, {}, Registry.CreateSnapshot());
        View->GetRegion(TEXT("main"));
        return View;
    }

    auto ReleaseMarkup() -> FString
    { return TEXT("<ui version=\"1\"><region name=\"main\"><release-probe id=\"probe\"/></region></ui>"); }

    auto MountedChild(const TSharedRef<SWidget>& InHost) -> TSharedRef<SWidget>
    {
        return ConstCastSharedRef<SWidget>(InHost->GetChildren()->GetChildAt(0));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiAuthoredUmgSurface_Lifetime,
    "Ck.UiAuthoring.UMG.SurfaceLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkUiAuthoredUmgSurface_Lifetime::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_authored_umg_surface;

    int32 OriginalCalls = 0;
    const TSharedRef<FCkUiView> Original = MakeView(OriginalCalls);
    const TSharedRef<SWidget> OriginalMount = Original->GetRegion(TEXT("main"));
    if (!TestTrue(TEXT("Original authored document commits"),
        Original->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    const TSharedPtr<SButton> OriginalButton = Original->GetButton(TEXT("fire"));
    if (!TestTrue(TEXT("Original button is committed"), OriginalButton.IsValid()))
    { return false; }

    TStrongObjectPtr<UCk_UI_AuthoredSurface_UE> Surface{NewObject<UCk_UI_AuthoredSurface_UE>()};
    ON_SCOPE_EXIT { Surface->ReleaseView(); };
    if (!TestTrue(TEXT("Accepted view configures the UMG surface"),
        Surface->TryConfigure(Original, TEXT("main"))))
    { return false; }
    const TSharedRef<SWidget> HostWidget = Surface->TakeWidget();
    if (!TestTrue(TEXT("UMG Slate widget mounts the accepted region"), Surface->HasConfiguredView()
        && MountedChild(HostWidget) == OriginalMount))
    { return false; }

    const auto RejectPreserves = [this, &Surface, &HostWidget, &Original, &OriginalMount, &OriginalButton, &OriginalCalls]
        (const TCHAR* InDescription, const TSharedPtr<FCkUiView>& InCandidate, const FString& InRegion) -> bool
    {
        if (!TestFalse(InDescription, Surface->TryConfigure(InCandidate, InRegion)))
        { return false; }
        if (!TestTrue(TEXT("Rejected configuration preserves the original mount and owner"),
            Surface->HasConfiguredView() && MountedChild(HostWidget) == OriginalMount && !Original->IsOwnerReleased()))
        { return false; }
        const int32 Before = OriginalCalls;
        OriginalButton->SimulateClick();
        return TestEqual(TEXT("Rejected configuration preserves original callback usability"), OriginalCalls, Before + 1);
    };

    if (!RejectPreserves(TEXT("Null candidate is rejected"), {}, TEXT("main"))) { return false; }
    int32 UncommittedCalls = 0;
    const TSharedRef<FCkUiView> Uncommitted = MakeView(UncommittedCalls);
    Uncommitted->GetRegion(TEXT("main"));
    if (!RejectPreserves(TEXT("Uncommitted candidate is rejected"), Uncommitted, TEXT("main"))) { return false; }
    if (!RejectPreserves(TEXT("Empty region is rejected"), Original, TEXT(""))) { return false; }
    if (!RejectPreserves(TEXT("Missing accepted region is rejected"), Original, TEXT("other"))) { return false; }

    int32 ReleasedCalls = 0;
    const TSharedRef<FCkUiView> Released = MakeView(ReleasedCalls);
    Released->GetRegion(TEXT("main"));
    if (!TestTrue(TEXT("Released candidate first commits"),
        Released->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    Released->ReleaseOwnerInteractions();
    if (!RejectPreserves(TEXT("Released candidate is rejected"), Released, TEXT("main"))) { return false; }

    const int32 CallsBeforeRelease = OriginalCalls;
    Surface->ReleaseView();
    TestFalse(TEXT("Release clears the configured view"), Surface->HasConfiguredView());
    TestTrue(TEXT("Release detaches the visible region"), MountedChild(HostWidget) == SNullWidget::NullWidget);
    TestTrue(TEXT("Release revokes the original view"), Original->IsOwnerReleased());
    OriginalButton->SimulateClick();
    TestEqual(TEXT("Externally held button cannot dispatch after release"), OriginalCalls, CallsBeforeRelease);

    int32 FreshCalls = 0;
    const TSharedRef<FCkUiView> Fresh = MakeView(FreshCalls);
    const TSharedRef<SWidget> FreshMount = Fresh->GetRegion(TEXT("main"));
    if (!TestTrue(TEXT("Fresh document commits after release"),
        Fresh->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    if (!TestTrue(TEXT("Fresh view configures the released UMG surface"),
        Surface->TryConfigure(Fresh, TEXT("main"))))
    { return false; }
    TestTrue(TEXT("Fresh region remounts into the existing UMG Slate widget"),
        Surface->HasConfiguredView() && MountedChild(HostWidget) == FreshMount);
    const TSharedPtr<SButton> FreshButton = Fresh->GetButton(TEXT("fire"));
    if (!TestTrue(TEXT("Fresh button is committed"), FreshButton.IsValid())) { return false; }
    FreshButton->SimulateClick();
    TestEqual(TEXT("Fresh owner callback dispatches"), FreshCalls, 1);

    const TSharedRef<FReleaseProbeState> ReleaseState = MakeShared<FReleaseProbeState>();
    const TSharedRef<FCkUiView> ReleaseView = MakeReleaseView(ReleaseState);
    if (!TestTrue(TEXT("Release callback probe commits"), ReleaseView->TryReload(ReleaseMarkup(), TEXT("")).Succeeded)
        || !TestTrue(TEXT("Release callback probe configures"), Surface->TryConfigure(ReleaseView, TEXT("main"))))
    { return false; }
    int32 UnusedCalls = 0;
    const TSharedRef<FCkUiView> InterruptedCandidate = MakeView(UnusedCalls);
    if (!TestTrue(TEXT("Interrupted candidate commits"),
        InterruptedCandidate->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    ReleaseState->OnRelease = [&Surface]() { Surface->ReleaseView(); };
    ON_SCOPE_EXIT { ReleaseState->OnRelease = {}; };
    TestFalse(TEXT("Callback release wins over outer reconfiguration"),
        Surface->TryConfigure(InterruptedCandidate, TEXT("main")));
    TestEqual(TEXT("Owner release callback runs once"), ReleaseState->Calls, 1);
    TestTrue(TEXT("Callback release leaves the surface empty"),
        !Surface->HasConfiguredView() && MountedChild(HostWidget) == SNullWidget::NullWidget);
    TestFalse(TEXT("Interrupted candidate was not released"), InterruptedCandidate->IsOwnerReleased());

    const TSharedRef<FReleaseProbeState> ReplaceState = MakeShared<FReleaseProbeState>();
    const TSharedRef<FCkUiView> ReplaceView = MakeReleaseView(ReplaceState);
    if (!TestTrue(TEXT("Replacement callback probe commits"), ReplaceView->TryReload(ReleaseMarkup(), TEXT("")).Succeeded)
        || !TestTrue(TEXT("Replacement callback probe configures"), Surface->TryConfigure(ReplaceView, TEXT("main"))))
    { return false; }
    int32 OuterCalls = 0;
    int32 InnerCalls = 0;
    const TSharedRef<FCkUiView> OuterCandidate = MakeView(OuterCalls);
    const TSharedRef<FCkUiView> InnerCandidate = MakeView(InnerCalls);
    if (!TestTrue(TEXT("Outer candidate commits"), OuterCandidate->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded)
        || !TestTrue(TEXT("Inner candidate commits"), InnerCandidate->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    const TSharedRef<SWidget> InnerMount = InnerCandidate->GetRegion(TEXT("main"));
    bool InnerAccepted = false;
    ReplaceState->OnRelease = [&Surface, &InnerCandidate, &InnerAccepted]()
    { InnerAccepted = Surface->TryConfigure(InnerCandidate, TEXT("main")); };
    ON_SCOPE_EXIT { ReplaceState->OnRelease = {}; };
    TestFalse(TEXT("Outer configuration yields to callback replacement"),
        Surface->TryConfigure(OuterCandidate, TEXT("main")));
    TestTrue(TEXT("Callback replacement mounts its accepted view"), InnerAccepted
        && Surface->HasConfiguredView() && MountedChild(HostWidget) == InnerMount);
    TestFalse(TEXT("Outer candidate remains usable"), OuterCandidate->IsOwnerReleased());

    const TSharedRef<FReleaseProbeState> CandidateState = MakeShared<FReleaseProbeState>();
    const TSharedRef<FCkUiView> CandidateReleaseView = MakeReleaseView(CandidateState);
    if (!TestTrue(TEXT("Candidate release probe commits"), CandidateReleaseView->TryReload(ReleaseMarkup(), TEXT("")).Succeeded)
        || !TestTrue(TEXT("Candidate release probe configures"), Surface->TryConfigure(CandidateReleaseView, TEXT("main"))))
    { return false; }
    int32 ReleasedCandidateCalls = 0;
    const TSharedRef<FCkUiView> ReleasedDuringTransition = MakeView(ReleasedCandidateCalls);
    if (!TestTrue(TEXT("Later released candidate commits"),
        ReleasedDuringTransition->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    const TSharedRef<SWidget> ReleasedCandidateMount = ReleasedDuringTransition->GetRegion(TEXT("main"));
    CandidateState->OnRelease = [&ReleasedDuringTransition]() { ReleasedDuringTransition->ReleaseOwnerInteractions(); };
    ON_SCOPE_EXIT { CandidateState->OnRelease = {}; };
    TestFalse(TEXT("Candidate released by owner callback is rejected"),
        Surface->TryConfigure(ReleasedDuringTransition, TEXT("main")));
    TestTrue(TEXT("Released candidate is not published"), !Surface->HasConfiguredView()
        && MountedChild(HostWidget) == SNullWidget::NullWidget
        && MountedChild(HostWidget) != ReleasedCandidateMount);

    const TSharedRef<FReleaseProbeState> TeardownState = MakeShared<FReleaseProbeState>();
    const TSharedRef<FCkUiView> TeardownView = MakeReleaseView(TeardownState);
    if (!TestTrue(TEXT("Teardown callback probe commits"), TeardownView->TryReload(ReleaseMarkup(), TEXT("")).Succeeded)
        || !TestTrue(TEXT("Teardown callback probe configures"), Surface->TryConfigure(TeardownView, TEXT("main"))))
    { return false; }
    int32 LaterCalls = 0;
    const TSharedRef<FCkUiView> LaterView = MakeView(LaterCalls);
    if (!TestTrue(TEXT("Post-teardown candidate commits"), LaterView->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    const TSharedRef<SWidget> LaterMount = LaterView->GetRegion(TEXT("main"));
    bool TeardownCallbackAccepted = false;
    TeardownState->OnRelease = [&Surface, &LaterView, &TeardownCallbackAccepted]()
    { TeardownCallbackAccepted = Surface->TryConfigure(LaterView, TEXT("main")); };
    ON_SCOPE_EXIT { TeardownState->OnRelease = {}; };
    Surface->ReleaseSlateResources(true);
    TestEqual(TEXT("Teardown invokes the owner release hook"), TeardownState->Calls, 1);
    TestFalse(TEXT("Teardown callback cannot configure a replacement"), TeardownCallbackAccepted);
    TestTrue(TEXT("Teardown releases its previous owner and empties the surface"),
        TeardownView->IsOwnerReleased() && !Surface->HasConfiguredView()
        && MountedChild(HostWidget) == SNullWidget::NullWidget);
    if (!TestTrue(TEXT("Fresh configuration is accepted after teardown"), Surface->TryConfigure(LaterView, TEXT("main"))))
    { return false; }
    const TSharedRef<SWidget> RebuiltHost = Surface->TakeWidget();
    TestTrue(TEXT("Slate host rebuilds rather than reusing the retired cache"), RebuiltHost != HostWidget);
    TestTrue(TEXT("Fresh configuration remounts after teardown"),
        Surface->HasConfiguredView() && MountedChild(RebuiltHost) == LaterMount);
    TestTrue(TEXT("Retired host remains detached after fresh configuration"),
        MountedChild(HostWidget) == SNullWidget::NullWidget);

    const TSharedRef<FReleaseProbeState> AliasedState = MakeShared<FReleaseProbeState>();
    const TSharedRef<FCkUiView> AliasedPrevious = MakeReleaseView(AliasedState);
    if (!TestTrue(TEXT("Aliased-input release probe commits"), AliasedPrevious->TryReload(ReleaseMarkup(), TEXT("")).Succeeded)
        || !TestTrue(TEXT("Aliased-input release probe configures"), Surface->TryConfigure(AliasedPrevious, TEXT("main"))))
    { return false; }
    int32 AliasedCalls = 0;
    TSharedPtr<FCkUiView> CallerCandidate = MakeView(AliasedCalls);
    FString CallerRegion = TEXT("main");
    if (!TestTrue(TEXT("Caller-owned candidate commits"),
        CallerCandidate->TryReload(Markup(TEXT("main")), TEXT("")).Succeeded))
    { return false; }
    const TSharedRef<SWidget> AliasedMount = CallerCandidate->GetRegion(TEXT("main"));
    const TSharedPtr<SButton> AliasedButton = CallerCandidate->GetButton(TEXT("fire"));
    AliasedState->OnRelease = [&CallerCandidate, &CallerRegion]()
    {
        CallerCandidate.Reset();
        CallerRegion = TEXT("other");
    };
    ON_SCOPE_EXIT { AliasedState->OnRelease = {}; };
    TestTrue(TEXT("Configuration snapshots caller-owned candidate and region before release callbacks"),
        Surface->TryConfigure(CallerCandidate, CallerRegion));
    TestTrue(TEXT("Caller mutations occur while accepted main region remains mounted"),
        !CallerCandidate.IsValid() && CallerRegion == TEXT("other")
        && Surface->HasConfiguredView() && MountedChild(RebuiltHost) == AliasedMount);
    if (!TestTrue(TEXT("Mounted aliased-input button remains available"), AliasedButton.IsValid()))
    { return false; }
    AliasedButton->SimulateClick();
    TestEqual(TEXT("Aliased-input candidate remains live after caller reset"), AliasedCalls, 1);

    Surface->ReleaseView();
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

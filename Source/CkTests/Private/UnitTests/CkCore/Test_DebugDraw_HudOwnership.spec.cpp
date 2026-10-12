#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CanvasTypes.h"
#include "CkCore/Debug/CkDebugDraw_Subsystem.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/SubsystemCollection.h"
#include "Subsystems/WorldSubsystem.h"

namespace ck_tests_debug_draw_hud
{
    constexpr auto MapPath = TEXT("/Engine/Maps/Entry");

    struct FDrawSurface
    {
        FCanvas Batches;
        TStrongObjectPtr<UCanvas> Canvas;

        explicit FDrawSurface(UWorld* InWorld)
            : Batches(nullptr, nullptr, InWorld, InWorld->GetFeatureLevel()),
              Canvas(NewObject<UCanvas>(GetTransientPackage()))
        { Canvas->Init(128, 128, nullptr, &Batches); }

        auto BatchCount() -> int32
        { return Batches.GetSortElement(0).RenderBatchArray.Num(); }

        auto ContainsLineBatch() -> bool
        {
            // ET_Line reuses an existing line batch, or creates a new empty one.
            const auto Before = BatchCount();
            Batches.GetBatchedElements(FCanvas::ET_Line);
            return BatchCount() == Before;
        }
    };

    auto Broadcast(AHUD* InHUD, FDrawSurface& InSurface) -> void
    {
        InHUD->SetCanvas(InSurface.Canvas.Get(), InSurface.Canvas.Get());
        AHUD::OnHUDPostRender.Broadcast(InHUD, InSurface.Canvas.Get());
        InHUD->SetCanvas(nullptr, nullptr);
    }

    auto SpawnOwnedHUD(UWorld* InWorld) -> AHUD*
    {
        auto* Controller = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
        if (!IsValid(Controller) || !Controller->IsLocalController())
        { return nullptr; }
        FActorSpawnParameters Params;
        Params.Owner = Controller;
        auto* HUD = InWorld->SpawnActor<AHUD>(AHUD::StaticClass(), FTransform::Identity, Params);
        return IsValid(HUD) && HUD->GetOwningPlayerController() == Controller ? HUD : nullptr;
    }

    struct FFixture
    {
        TWeakObjectPtr<APlayerController> TargetPC;
        TWeakObjectPtr<AHUD> NativeHUD;
        TWeakObjectPtr<AHUD> First;
        TWeakObjectPtr<AHUD> Second;
        TWeakObjectPtr<AHUD> Foreign;
        FDelegateHandle ObserverHandle;
        uint64 RectEnqueuedFrame = MAX_uint64;
        uint64 LineEnqueuedFrame = MAX_uint64;
        uint64 LineRenderedFrame = MAX_uint64;
        int32 Phase = 0;
        bool bInObserver = false;
        bool bPassed = true;

        void RemoveObserver()
        {
            if (ObserverHandle.IsValid())
            {
                AHUD::OnHUDPostRender.Remove(ObserverHandle);
                ObserverHandle.Reset();
            }
        }

        ~FFixture()
        { RemoveObserver(); }
    };

    auto ObserveNativeHUD(const TSharedPtr<FFixture>& InFixture, FAutomationTestBase* InTest,
        AHUD* InHUD, UCanvas* InCanvas) -> void
    {
        auto* PC = InFixture->TargetPC.Get();
        auto* Native = InFixture->NativeHUD.Get();
        if (InFixture->bInObserver || !IsValid(PC) || !IsValid(Native) ||
            InHUD != Native || PC->GetHUD() != Native || !IsValid(InCanvas) ||
            Native->GetOwningPlayerController() != PC || Native->GetWorld() != PC->GetWorld())
        { return; }

        const auto Frame = GFrameCounter;
        auto* First = InFixture->First.Get();
        auto* Second = InFixture->Second.Get();
        if (!IsValid(First) || !IsValid(Second))
        { return; }

        if (InFixture->Phase == 0)
        {
            // The native HUD just passed the production callback in this frame. Its next
            // callback must be in a new frame, so the queued rectangle cannot be consumed early.
            InFixture->bInObserver = true;
            auto* Subsystem = Native->GetWorld()->GetSubsystem<UCk_DebugDraw_Subsystem_UE>();
            if (InTest->TestNotNull(TEXT("target subsystem before rectangle enqueue"), Subsystem))
            {
                Subsystem->Request_DrawRect_OnScreen(FCk_Request_DebugDrawOnScreen_Rect(
                    FBox2D(FVector2D(10.0, 10.0), FVector2D(30.0, 30.0))));
                InFixture->RectEnqueuedFrame = Frame;
            }
            else
            { InFixture->bPassed = false; }
            AHUD::OnHUDPostRender.Broadcast(nullptr, nullptr);
            auto* Foreign = InFixture->Foreign.Get();
            if (IsValid(Foreign))
            {
                FDrawSurface ForeignSurface(Foreign->GetWorld());
                Broadcast(Foreign, ForeignSurface);
            }
            InFixture->Phase = 1;
            InFixture->bInObserver = false;
        }
        else if (InFixture->Phase == 1 && Frame > InFixture->RectEnqueuedFrame)
        {
            InFixture->bInObserver = true;
            FDrawSurface FirstSurface(Native->GetWorld());
            FDrawSurface DuplicateSurface(Native->GetWorld());
            FDrawSurface SecondSurface(Native->GetWorld());

            // The production subsystem was bound before this observer. Its native-HUD callback
            // has already frozen this frame's batch; these nested callbacks inspect that batch.
            Broadcast(First, FirstSurface);
            InFixture->bPassed &= InTest->TestEqual(TEXT("foreign and invalid callbacks leave target rectangle"),
                FirstSurface.BatchCount(), 1);
            Broadcast(First, DuplicateSurface);
            InFixture->bPassed &= InTest->TestEqual(TEXT("same HUD cannot draw twice in a frame"),
                DuplicateSurface.BatchCount(), 0);

            auto* Subsystem = Native->GetWorld()->GetSubsystem<UCk_DebugDraw_Subsystem_UE>();
            if (InTest->TestNotNull(TEXT("target subsystem remains live"), Subsystem))
            {
                Subsystem->Request_DrawLine_OnScreen(FCk_Request_DebugDrawOnScreen_Line(
                    FVector2D(2.0, 2.0), FVector2D(20.0, 20.0)));
                InFixture->LineEnqueuedFrame = Frame;
            }
            else
            { InFixture->bPassed = false; }
            Broadcast(Second, SecondSurface);
            InFixture->bPassed &= InTest->TestEqual(TEXT("second local HUD receives same rectangle"),
                SecondSurface.BatchCount(), 1);
            InFixture->bPassed &= InTest->TestFalse(TEXT("late line is absent in this frame"),
                SecondSurface.ContainsLineBatch());
            InFixture->Phase = 2;
            InFixture->bInObserver = false;
        }
        else if (InFixture->Phase == 2 && Frame > InFixture->LineEnqueuedFrame)
        {
            InFixture->bInObserver = true;
            FDrawSurface FirstSurface(Native->GetWorld());
            FDrawSurface SecondSurface(Native->GetWorld());
            Broadcast(First, FirstSurface);
            Broadcast(Second, SecondSurface);
            InFixture->bPassed &= InTest->TestEqual(TEXT("first local HUD receives one deferred batch"),
                FirstSurface.BatchCount(), 1);
            InFixture->bPassed &= InTest->TestEqual(TEXT("second local HUD receives one deferred batch"),
                SecondSurface.BatchCount(), 1);
            InFixture->bPassed &= InTest->TestTrue(TEXT("first local HUD batch is the line"),
                FirstSurface.ContainsLineBatch());
            InFixture->bPassed &= InTest->TestTrue(TEXT("second local HUD batch is the line"),
                SecondSurface.ContainsLineBatch());
            InFixture->LineRenderedFrame = Frame;
            InFixture->Phase = 3;
            InFixture->bInObserver = false;
        }
        else if (InFixture->Phase == 3 && Frame > InFixture->LineRenderedFrame)
        {
            InFixture->bInObserver = true;
            FDrawSurface Surface(Native->GetWorld());
            Broadcast(First, Surface);
            InFixture->bPassed &= InTest->TestEqual(TEXT("requests expire after their frame"),
                Surface.BatchCount(), 0);
            InFixture->Phase = 4;
            InFixture->bInObserver = false;
        }
    }

    // NullRHI need not paint a viewport. Drive the real owning HUD's PostRender path once
    // per observed frame; natural paints, if any, may also occur without changing phase order.
    class FCk_Latent_DriveNativeHUD : public IAutomationLatentCommand
    {
    public:
        explicit FCk_Latent_DriveNativeHUD(TSharedPtr<FFixture> InFixture)
            : Fixture(MoveTemp(InFixture)) {}

        virtual bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Fixture->Phase == 4)
            { return true; }
            if (StartTime < 0.0)
            { StartTime = FPlatformTime::Seconds(); }
            if (FPlatformTime::Seconds() - StartTime > 15.0)
            {
                ck::auto_test::net::FailSession(TEXT("native HUD did not complete debug-draw frame phases"));
                return true;
            }

            auto* Native = Fixture->NativeHUD.Get();
            if (!IsValid(Native) || !IsValid(Native->GetWorld()))
            {
                ck::auto_test::net::FailSession(TEXT("target native HUD was lost during debug-draw phases"));
                return true;
            }
            if (LastDrivenFrame == GFrameCounter)
            { return false; }

            LastDrivenFrame = GFrameCounter;
            FDrawSurface Surface(Native->GetWorld());
            Native->SetCanvas(Surface.Canvas.Get(), Surface.Canvas.Get());
            Native->PostRender();
            Native->SetCanvas(nullptr, nullptr);
            return Fixture->Phase == 4;
        }

    private:
        TSharedPtr<FFixture> Fixture;
        uint64 LastDrivenFrame = MAX_uint64;
        double StartTime = -1.0;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkDebugDraw_HudOwnership,
    "Ck.DebugDraw.HudOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkDebugDraw_HudOwnership::RunTest(const FString& Parameters)
{
    using namespace ck_tests_debug_draw_hud;
    const auto Fixture = MakeShared<FFixture>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, MapPath, Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ClientWorld(0);
            auto* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
            return IsValid(PC) && PC->IsLocalController() && IsValid(PC->GetHUD()) &&
                PC->GetHUD()->GetOwningPlayerController() == PC;
        }), 15.0, TEXT("Entry GameMode spawned the target client's native local HUD")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Fixture](UWorld* InWorld)
        {
            auto* ForeignWorld = ck::auto_test::net::Get_ClientWorld(1);
            auto* Subsystem = IsValid(InWorld) ? InWorld->GetSubsystem<UCk_DebugDraw_Subsystem_UE>() : nullptr;
            auto* PC = IsValid(InWorld) ? InWorld->GetFirstPlayerController() : nullptr;
            Fixture->TargetPC = PC;
            Fixture->NativeHUD = IsValid(PC) ? PC->GetHUD() : nullptr;
            Fixture->First = SpawnOwnedHUD(InWorld);
            Fixture->Second = SpawnOwnedHUD(InWorld);
            Fixture->Foreign = SpawnOwnedHUD(ForeignWorld);
            if (!TestTrue(TEXT("native HUD, two extra local HUDs, foreign HUD, and subsystem exist"),
                IsValid(Subsystem) && Fixture->NativeHUD.IsValid() &&
                Fixture->First.IsValid() && Fixture->Second.IsValid() && Fixture->Foreign.IsValid() &&
                Fixture->First != Fixture->Second && Fixture->First != Fixture->NativeHUD &&
                Fixture->Second != Fixture->NativeHUD))
            {
                ck::auto_test::net::FailSession(TEXT("debug-draw HUD fixture prerequisites failed"));
                return;
            }

            TWeakPtr<FFixture> WeakFixture = Fixture;
            Fixture->ObserverHandle = AHUD::OnHUDPostRender.AddLambda(
                [WeakFixture, Test = this](AHUD* InHUD, UCanvas* InCanvas)
                {
                    if (auto Pinned = WeakFixture.Pin())
                    { ObserveNativeHUD(Pinned, Test, InHUD, InCanvas); }
                });

        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_DriveNativeHUD(Fixture));

    // This cleanup executes even when a prerequisite or observation wait failed.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            Fixture->RemoveObserver();
            return ck::auto_test::net::Get_HasSessionFailed() ||
                TestTrue(TEXT("all observed native-HUD draw phases passed"), Fixture->bPassed && Fixture->Phase == 4);
        }), TEXT("remove HUD observer before PIE teardown"), true));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Fixture]() -> bool
        {
            auto* First = Fixture->First.Get();
            if (!TestTrue(TEXT("local HUD survives through teardown probe"), IsValid(First)))
            { return false; }

            // A separate instance tests the delegate lifecycle without deinitializing the
            // subsystem managed by PIE's world subsystem collection twice.
            FSubsystemCollection<UWorldSubsystem> ProbeCollection;
            TStrongObjectPtr<UCk_DebugDraw_Subsystem_UE> Probe(
                NewObject<UCk_DebugDraw_Subsystem_UE>(First->GetWorld()));
            if (!TestNotNull(TEXT("isolated debug-draw subsystem probe"), Probe.Get()))
            { return false; }
            Probe->Initialize(ProbeCollection);
            Probe->Request_DrawRect_OnScreen(FCk_Request_DebugDrawOnScreen_Rect(
                FBox2D(FVector2D(10.0, 10.0), FVector2D(30.0, 30.0))));
            FDrawSurface BeforeDeinitialize(First->GetWorld());
            Broadcast(First, BeforeDeinitialize);
            const bool bProbeDrew = TestEqual(TEXT("initialized probe draws through the real HUD delegate"),
                BeforeDeinitialize.BatchCount(), 1);
            Probe->Deinitialize();
            Probe->Request_DrawRect_OnScreen(FCk_Request_DebugDrawOnScreen_Rect(
                FBox2D(FVector2D(10.0, 10.0), FVector2D(30.0, 30.0))));
            FDrawSurface AfterDeinitialize(First->GetWorld());
            Broadcast(First, AfterDeinitialize);
            return bProbeDrew && TestEqual(TEXT("deinitialized probe cannot draw"),
                AfterDeinitialize.BatchCount(), 0);
        }), TEXT("separate subsystem removes its delegate on deinitialize")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

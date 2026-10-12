#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCrowdGym_NetworkRuntime.h"
#include "CkGym_StartupSettings.h"
#include "CkCrowd/Agent/CkCrowdAgent_Utils.h"
#include "CkCrowd/Agent/CkCrowdAgent_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "EngineUtils.h"
#include "CkCrowd/Debug/CkCrowd_RemoteDebugRegistry.h"
#include "CkCrowdDebugger/Window/SCkCrowdDebuggerWindow.h"
#include "CkDebuggerCommon/Models/CkDebuggerModel_WorldSelector.h"
#include "CkDebuggerCommon/Widgets/SCkDebug_EventLog.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableText.h"

namespace ck_crowd_rendered_window
{
    constexpr auto Map = TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level");
    auto State(UWorld* W) -> ACkCrowdGym_GameState_UE*
    { return IsValid(W) ? W->GetGameState<ACkCrowdGym_GameState_UE>() : nullptr; }
    auto Session(APlayerController* PC) -> UCkCrowdGym_Session_UE*
    { return IsValid(PC) ? PC->FindComponentByClass<UCkCrowdGym_Session_UE>() : nullptr; }
    auto Local(UWorld* W) -> APlayerController*
    {
        APlayerController* PC = IsValid(W) ? W->GetFirstPlayerController() : nullptr;
        ULocalPlayer* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
        UGameInstance* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
        return IsValid(GI) && GI->GetWorld() == W && GI->GetLocalPlayers().Num() == 1 && IsValid(LP) && PC->IsLocalController() && PC->GetWorld() == W &&
            LP->GetWorld() == W && LP->GetPlayerController(W) == PC ? PC : nullptr;
    }
    auto CrowdCount(UWorld* W) -> int32
    {
        if (!IsValid(W)) { return INDEX_NONE; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
        if (!ck::IsValid(Root) || UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            Root, ECk_EntityLifetime_DestructionPhase::BeginDestroy)) { return INDEX_NONE; }
        int32 Count = 0;
        const auto Registry = Root.Get_RegistryView();
        Registry.View<ck::FFragment_CrowdAgent_Tunables>().ForEach(
            [&Count](FCk_Entity, const ck::FFragment_CrowdAgent_Tunables&) { ++Count; });
        return Count;
    }
    auto FloorReady(UWorld* W, UClass* Class) -> bool
    {
        if (!IsValid(W) || !Class) { return false; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(W, Class); It; ++It)
        {
            if (!IsValid(*It) || !It->GetActorLocation().Equals(FVector::ZeroVector) ||
                !It->GetActorRotation().IsNearlyZero() || !It->GetActorScale3D().Equals(FVector(20, 20, 0.5))) { return false; }
            ++Count;
        }
        return Count == 1;
    }
    auto Pump() -> void
    { auto& S = FSlateApplication::Get(); S.PumpMessages(); S.Tick(); S.Tick(); }
    auto FindType(const TSharedRef<SWidget>& Root, const FString& Type) -> TSharedPtr<SWidget>
    {
        if (Root->GetTypeAsString() == Type) { return Root; }
        FChildren* Children = Root->GetChildren();
        for (int32 I = 0; Children && I < Children->Num(); ++I)
        { if (auto Found = FindType(ConstCastSharedRef<SWidget>(Children->GetChildAt(I)), Type)) { return Found; } }
        return {};
    }
    auto FindText(const TSharedRef<SWidget>& Root, const FString& Text, bool Exact = true) -> bool
    {
        if (Root->GetTypeAsString() == TEXT("STextBlock"))
        {
            const auto Value = StaticCastSharedRef<STextBlock>(Root)->GetText().ToString();
            if (Exact ? Value == Text : Value.Contains(Text)) { return true; }
        }
        if (Root->GetTypeAsString() == TEXT("SEditableText"))
        {
            const auto Value = StaticCastSharedRef<SEditableText>(Root)->GetText().ToString();
            if (Exact ? Value == Text : Value.Contains(Text)) { return true; }
        }
        FChildren* Children = Root->GetChildren();
        for (int32 I = 0; Children && I < Children->Num(); ++I)
        { if (FindText(ConstCastSharedRef<SWidget>(Children->GetChildAt(I)), Text, Exact)) { return true; } }
        return false;
    }
    auto FindRow(const TSharedRef<SWidget>& Root, const FString& Identity) -> TSharedPtr<SWidget>
    {
        if (Root->GetTypeAsString().Contains(TEXT("STableRow")) && FindText(Root, Identity, false)) { return Root; }
        FChildren* Children = Root->GetChildren();
        for (int32 I = 0; Children && I < Children->Num(); ++I)
        { if (auto Found = FindRow(ConstCastSharedRef<SWidget>(Children->GetChildAt(I)), Identity)) { return Found; } }
        return {};
    }
    auto Buttons(const TSharedRef<SWidget>& Root, TArray<TSharedPtr<SWidget>>& Out) -> void
    {
        if (Root->GetTypeAsString() == TEXT("SButton")) { Out.Add(Root); return; }
        FChildren* Children = Root->GetChildren();
        for (int32 I = 0; Children && I < Children->Num(); ++I)
        { Buttons(ConstCastSharedRef<SWidget>(Children->GetChildAt(I)), Out); }
    }
    // Uses the normal application hit-test path and native mouse dispatch, never a delegate call.
    auto Click(const TSharedPtr<SWidget>& Widget) -> bool
    {
        if (!Widget.IsValid()) { return false; }
        auto& Slate = FSlateApplication::Get();
        const auto Window = Slate.FindWidgetWindow(Widget.ToSharedRef());
        if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
        Window->BringToFront(true); Pump(); Slate.CloseToolTip(); Slate.ReleaseAllPointerCapture(0);
        const auto Geometry = Widget->GetCachedGeometry(); const auto Size = Geometry.GetLocalSize();
        if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X <= 0 || Size.Y <= 0) { return false; }
        const auto Position = Geometry.LocalToAbsolute(Size * 0.5f);
        if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y)) { return false; }
        const FModifierKeysState Modifiers;
        const TSet<FKey> NoKeys; const TSet<FKey> DownKeys{EKeys::LeftMouseButton};
        const FPointerEvent Move(0, FSlateApplication::CursorPointerIndex, Position, Position,
            NoKeys, EKeys::Invalid, 0.0f, Modifiers);
        Slate.SetCursorPos(Position); Slate.ProcessMouseMoveEvent(Move, true);
        const auto Path = Slate.LocateWindowUnderMouse(Position, Slate.GetInteractiveTopLevelWindows(), false, 0);
        bool Found = false;
        for (int32 I = 0; I < Path.Widgets.Num(); ++I) { Found |= Path.Widgets[I].Widget == Widget; }
        if (!Found) { return false; }
        const FPointerEvent Down(0, FSlateApplication::CursorPointerIndex, Position, Position,
            DownKeys, EKeys::LeftMouseButton, 0.0f, Modifiers);
        const FPointerEvent Up(0, FSlateApplication::CursorPointerIndex, Position, Position,
            NoKeys, EKeys::LeftMouseButton, 0.0f, Modifiers);
        const bool DownHandled = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down);
        const bool UpHandled = Slate.ProcessMouseButtonUpEvent(Up);
        // No trailing pump: the Gone episode retires the pawn before the next engine/network frame.
        return DownHandled || UpHandled;
    }
    auto Screenshot(const TSharedPtr<SWindow>& Window, const FString& Path) -> bool
    {
        if (!Window.IsValid()) { return false; }
        Window->BringToFront(true); Pump();
        TArray<FColor> Pixels; FIntVector Size;
        if (!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(), Pixels, Size) ||
            Size.X <= 0 || Size.Y <= 0 || int64(Size.X) * Size.Y > Pixels.Num()) { return false; }
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)) { return false; }
        return FImageUtils::SaveImageByExtension(*Path,
            FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
    }
    struct FFixture
    {
        UClass* Mode = nullptr; UClass* StateClass = nullptr; UClass* PCClass = nullptr; UClass* FloorClass = nullptr;
        bool Dedicated = false;
        TWeakObjectPtr<UWorld> Server;
        TWeakObjectPtr<APlayerController> LocalPC[2], RemotePC[2];
        TWeakObjectPtr<APawn> LocalPawn[2], RemotePawn[2];
        TWeakObjectPtr<UCkCrowdGym_Session_UE> ClientSession[2], ServerSession[2];
        FCkCrowdRemoteSelectionEnvelope Observed[2], ObservedGone;
        FDelegateHandle Observe; bool GoneStarted = false;
        TSharedPtr<SWindow> Windows[2], OrphanWindow;
        TSharedPtr<SCkCrowdDebuggerWindow> Panels[2];
        TSharedPtr<SCkDebug_EventLog> HeldLog;
        TSharedPtr<SWidget> HeldRow;
        TWeakPtr<SCkCrowdDebuggerWindow> ReleasedWindow;
        TWeakPtr<SWidget> ReleasedPanel;
        FCkCrowdRemoteAgentRow Agents[2]; FVector Goals[2];
        int64 SelectionBefore[2]{}, ControlBefore[2]{}, Accepted[2]{};
        struct FTraffic
        {
            int64 SelectionAttempt = 0, ControlIssuer = 0, Ack = 0, Teardown = 0;
            bool Valid = false;
            static FTraffic Read(const UCkCrowdGym_Session_UE* S)
            {
                FTraffic T;
                if (!IsValid(S)) { return T; }
                T.SelectionAttempt = S->Get_LocalSelectionAttemptWatermark();
                T.ControlIssuer = S->Get_NextLocalControlSerial();
                T.Ack = S->Get_LocalAcknowledgeSendCount(); T.Teardown = S->Get_LocalTeardownSendCount();
                T.Valid = S->Get_LocalSendDiagnosticsValid(); return T;
            }
            bool Same(const FTraffic& T) const
            { return Valid && T.Valid && SelectionAttempt == T.SelectionAttempt && ControlIssuer == T.ControlIssuer && Ack == T.Ack && Teardown == T.Teardown; }
        } Traffic[2];
        uint64 Frame = 0; int64 GoneSerial = 0; int32 PureFrames = 0; bool Captured = false;
        FString Topology;
        ECkGym_StartupMode OldStartup = ECkGym_StartupMode::Cycler;
        FString OldLast; TArray<FString> OldRecent;
        UCkCrowdGym_Session_UE* Client(int32 I) const { return Session(LocalPC[I].Get()); }
        UCkCrowdGym_Session_UE* Authority(int32 I) const { return Session(RemotePC[I].Get()); }
        UCkCrowdGym_SharedSimulation* Shared() const
        { auto* S = State(Server.Get()); return IsValid(S) ? S->FindComponentByClass<UCkCrowdGym_SharedSimulation>() : nullptr; }
        bool Context() const
        {
            UWorld* W = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(W) || !IsValid(State(W)) || !IsValid(W->GetAuthGameMode()) ||
                (Captured && (!IsValid(Shared()) || W->GetAuthGameMode()->GetClass() != Mode)) ||
                ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 || (Captured && Server.Get() != W)) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                UWorld* C = ck::auto_test::net::Get_ClientWorld(I);
                APlayerController* PC = Local(C);
                APlayerController* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(W, I);
                if (!IsValid(C) || !IsValid(State(C)) || !IsValid(PC) || !IsValid(R) ||
                    !IsValid(Session(PC)) || !IsValid(Session(R)) ||
                    (Captured && (LocalPC[I].Get() != PC || RemotePC[I].Get() != R ||
                        ClientSession[I].Get() != Session(PC) || ServerSession[I].Get() != Session(R) ||
                        ((!GoneStarted || I == 1) && (PC->GetPawn() != LocalPawn[I].Get() ||
                            R->GetPawn() != RemotePawn[I].Get() || !LocalPawn[I].IsValid() || !RemotePawn[I].IsValid()))))) { return false; }
            }
            return true; // Pawn absence is deliberate only in the final Gone episode.
        }
        void Restore()
        {
            if (Observe.IsValid()) { FWorldDelegates::OnWorldPreActorTick.Remove(Observe); Observe.Reset(); }
            if (FSlateApplication::IsInitialized())
            {
                auto& Slate = FSlateApplication::Get(); Slate.DismissAllMenus(); Slate.ReleaseAllPointerCapture(0);
                for (int32 I = 0; I < 2; ++I)
                {
                    if (Panels[I].IsValid()) { Panels[I]->Release_Presentation(); }
                    if (Windows[I].IsValid()) { Slate.DestroyWindowImmediately(Windows[I].ToSharedRef()); }
                    Panels[I].Reset(); Windows[I].Reset();
                }
                if (OrphanWindow.IsValid()) { Slate.DestroyWindowImmediately(OrphanWindow.ToSharedRef()); }
            }
            OrphanWindow.Reset(); HeldRow.Reset(); HeldLog.Reset();
            auto* Settings = GetMutableDefault<UCkGym_StartupSettings>();
            Settings->StartupMode = OldStartup; Settings->LastGymName = OldLast;
            Settings->RecentGymNames = OldRecent; Settings->SaveConfig();
        }
    };
    auto Ready(const FFixture& F) -> bool
    {
        if (!F.Context()) { return false; }
        UWorld* W = ck::auto_test::net::Get_ServerWorld(); auto* S = State(W);
        if (!F.Mode || !F.StateClass || !F.PCClass || !S->IsA(F.StateClass) ||
            W->GetAuthGameMode()->GetClass() != F.Mode ||
            W->GetNetMode() != (F.Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
            !FloorReady(W, F.FloorClass) || CrowdCount(W) != 0 || !S->Get_HasReadyServerSimulation() ||
            !FCkCrowdGymWireValidation::IsValidReadySnapshot(S->Get_RowsSnapshot()) || !S->Get_RowsSnapshot().Rows.IsEmpty()) { return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            UWorld* C = ck::auto_test::net::Get_ClientWorld(I); APlayerController* PC = Local(C);
            APlayerController* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(W, I);
            TArray<FCkCrowdRemoteAgentRow> Rows;
            if (C->GetNetMode() != NM_Client || !State(C)->IsA(F.StateClass) ||
                !FloorReady(C, F.FloorClass) || CrowdCount(C) != 0 ||
                !PC->IsA(F.PCClass) || !R->IsA(F.PCClass) || !IsValid(PC->GetPawn()) || !IsValid(R->GetPawn()) ||
                !State(C)->Get_HasCompleteSnapshot() || !State(C)->Get_HasPublishedCatalog() ||
                State(C)->Get_RowsSnapshot().WorldGeneration != S->Get_RowsSnapshot().WorldGeneration ||
                !FCkCrowdRemoteDebugRegistry::TryCopyRows(*C, *PC, *PC->GetLocalPlayer(), Rows)) { return false; }
        }
        return true;
    }
    auto Envelope(const FFixture& F, int32 I, FCkCrowdRemoteSelectionEnvelope& Out) -> bool
    { auto* S = F.Client(I); return IsValid(S) && S->TryCopySelectionEnvelope(Out) && FCkCrowdRemoteDebugValidation::IsValid(Out); }
    auto Log(const TSharedPtr<SCkCrowdDebuggerWindow>& Panel) -> TSharedPtr<SCkDebug_EventLog>
    {
        const auto Found = Panel.IsValid() ? FindType(Panel.ToSharedRef(), TEXT("SCkDebug_EventLog")) : TSharedPtr<SWidget>{};
        return Found.IsValid() ? StaticCastSharedPtr<SCkDebug_EventLog>(Found) : nullptr;
    }
    auto Identity(const FCkCrowdRemoteAgentRow& Row) -> FString
    { return Row.DisplayId.IsEmpty() ? FString::Printf(TEXT("remote %lld:%lld"),
        static_cast<long long>(Row.Id.WorldGeneration), static_cast<long long>(Row.Id.AgentSerial)) : Row.DisplayId; }
    auto Chosen(const FFixture& F, int32 Owner, int32 Agent) -> bool
    {
        FCkCrowdRemoteSelectionEnvelope E;
        if (!F.Panels[Owner].IsValid()) { return false; }
        const auto Detail = FindType(F.Panels[Owner].ToSharedRef(), TEXT("SCkCrowdDebugger_AgentDetailPanel"));
        return Detail.IsValid() && Envelope(F, Owner, E) && E.Outcome == ECkCrowdRemoteSelectionOutcome::Accepted && E.bHasDetail &&
            E.SelectedId == F.Agents[Agent].Id && E.Detail.Id == E.SelectedId && E.Rows.Num() == 2 &&
            !E.Detail.Sample.DisplayId.IsEmpty() && FindText(Detail.ToSharedRef(), E.Detail.Sample.DisplayId) &&
            E.SelectionRequestSerial > F.SelectionBefore[Owner] &&
            F.Observed[Owner].bRetainedDelivery && F.Observed[Owner].SelectedId == E.SelectedId &&
            F.Observed[Owner].SelectionRequestSerial == E.SelectionRequestSerial &&
            F.Observed[Owner].WorldGeneration == E.WorldGeneration &&
            F.Observed[Owner].SessionGeneration == E.SessionGeneration &&
            F.Authority(Owner)->Get_SelectionAcceptedWatermark() == E.SelectionRequestSerial &&
            !F.Authority(Owner)->HasServerRetained() &&
            FindText(F.Panels[Owner].ToSharedRef(), TEXT("Remote selection: accepted"));
    }
    struct FStep { FString Name; TFunction<bool()> Body; bool Wait = false; };
    class FScenario final : public IAutomationLatentCommand
    {
    public:
        FScenario(FAutomationTestBase* Test, TSharedRef<FFixture> F, TArray<FStep>&& Steps)
            : T(Test), Fixture(F), Steps(MoveTemp(Steps)) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index >= Steps.Num()) { return true; }
            if (Started < 0) { Started = FPlatformTime::Seconds(); }
            const bool Context = Fixture->Context() && FSlateApplication::IsInitialized();
            auto& S = Steps[Index];
            if (Context && S.Body()) { ++Index; Started = -1; return Index >= Steps.Num(); }
            if (Context && S.Wait && FPlatformTime::Seconds() - Started < 45.0) { return false; }
            const auto Message = TEXT("rendered Crowd phase failed: ") + S.Name;
            T->AddError(Message); ck::auto_test::net::FailSession(Message); return true;
        }
    private:
        FAutomationTestBase* T; TSharedRef<FFixture> Fixture; TArray<FStep> Steps;
        int32 Index = 0; double Started = -1;
    };
    class FAlwaysRestore final : public IAutomationLatentCommand
    {
    public:
        explicit FAlwaysRestore(TSharedRef<FFixture> F) : Fixture(F) {}
        bool Update() override { Fixture->Restore(); return true; }
    private: TSharedRef<FFixture> Fixture;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdDebugger_RemoteWindow_Rendered,
    "Ck.CrowdDebugger.RemoteWindow.Rendered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCkCrowdDebugger_RemoteWindow_Rendered::RunTest(const FString& Parameters)
{
    using namespace ck_crowd_rendered_window;
    if (!TestTrue(TEXT("rendered Slate is initialized"), FSlateApplication::IsInitialized())) { return false; }
    const auto F = MakeShared<FFixture>(); F->Topology = Parameters;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated)) { return false; }
    F->Dedicated = Dedicated;
    F->Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_GameMode")).TryLoadClass<AGameModeBase>();
    F->StateClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_GameState")).TryLoadClass<ACkCrowdGym_GameState_UE>();
    F->PCClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_PlayerController")).TryLoadClass<APlayerController>();
    F->FloorClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_CrowdGym_Separation_Floor")).TryLoadClass<AActor>();
    if (!TestTrue(TEXT("actual Separation registered classes load"), F->Mode && F->StateClass && F->PCClass && F->FloorClass)) { return false; }
    const auto* Settings = GetDefault<UCkGym_StartupSettings>();
    F->OldStartup = Settings->StartupMode; F->OldLast = Settings->LastGymName; F->OldRecent = Settings->RecentGymNames;
    GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(3, Map, Parameters, F->Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([F]() { return Ready(*F); }), 60,
        TEXT("actual three-world Separation startup and complete native owner sessions")));
    TArray<FStep> Steps;
    auto Act = [&Steps](FString Name, TFunction<bool()> Body) { Steps.Add({MoveTemp(Name), MoveTemp(Body), false}); };
    auto Wait = [&Steps](FString Name, TFunction<bool()> Body) { Steps.Add({MoveTemp(Name), MoveTemp(Body), true}); };
    Act(TEXT("capture owners and actual append control"), [F]()
    {
        F->Server = ck::auto_test::net::Get_ServerWorld();
        for (int32 I = 0; I < 2; ++I)
        {
            F->LocalPC[I] = Local(ck::auto_test::net::Get_ClientWorld(I));
            F->RemotePC[I] = ck::auto_test::net::TryGet_RemoteClientPlayerController(F->Server.Get(), I);
            F->LocalPawn[I] = F->LocalPC[I]->GetPawn(); F->RemotePawn[I] = F->RemotePC[I]->GetPawn();
            F->ClientSession[I] = F->Client(I); F->ServerSession[I] = F->Authority(I);
        }
        F->Captured = true; return IsValid(F->Shared()) && F->Client(0)->TryQueueControl(1);
    });
    Wait(TEXT("normal AS control feedback and two production agents"), [F]()
    {
        auto* S = State(F->Server.Get()); const auto Rows = S->Get_RowsSnapshot().Rows;
        if (Rows.Num() != 2 || CrowdCount(F->Server.Get()) != 2 ||
            CrowdCount(F->LocalPC[0]->GetWorld()) != 0 || CrowdCount(F->LocalPC[1]->GetWorld()) != 0 ||
            !F->Shared()->Get_IsReady() || F->Authority(0)->HasServerRetained()) { return false; }
        auto* Serial = FindFProperty<FInt64Property>(F->LocalPC[0]->GetClass(), TEXT("LastControlSerial"));
        auto* Outcome = FindFProperty<FIntProperty>(F->LocalPC[0]->GetClass(), TEXT("LastControlOutcome"));
        if (!Serial || !Outcome || Serial->GetPropertyValue_InContainer(F->LocalPC[0].Get()) != F->Authority(0)->Get_ControlAcceptedWatermark() ||
            F->Authority(0)->Get_ControlAcceptedWatermark() <= 0 ||
            Outcome->GetPropertyValue_InContainer(F->LocalPC[0].Get()) != int32(ECkCrowdGym_RequestOutcome::Accepted)) { return false; }
        F->Agents[0] = Rows[0]; F->Agents[1] = Rows[1]; return Rows[0].Id != Rows[1].Id;
    });
    Act(TEXT("two actual public MoveTo episodes outside the authored navigation extent"), [F]()
    {
        auto* Sim = F->Shared(); if (!IsValid(Sim) || !Sim->Get_IsReady()) { return false; }
        FCk_Handle_CrowdAgent Agents[2];
        for (int32 I = 0; I < 2; ++I)
        { if (!Sim->TryResolveAgent(F->Agents[I].Id, F->Agents[I].ConfigurationRevision, Agents[I])) { return false; } }
        for (int32 I = 0; I < 2; ++I)
        {
            F->Goals[I] = FVector(1000000.0 + I * 1000.0, 1000000.0, 1000000.0);
            FCk_Request_CrowdAgent_MoveTo Request(F->Goals[I]); Request.Set_ForceRepath(true);
            UCk_Utils_CrowdAgent_UE::Request_MoveTo(Agents[I], Request, {});
        }
        return true;
    });
    Wait(TEXT("genuine new Trouble events and whole copied remote envelopes"), [F]()
    {
        for (int32 Owner = 0; Owner < 2; ++Owner)
        {
            FCkCrowdRemoteSelectionEnvelope E;
            if (!Envelope(*F, Owner, E) || E.Outcome != ECkCrowdRemoteSelectionOutcome::Accepted || E.Rows.Num() != 2) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                const auto* Row = E.Rows.FindByPredicate([F, I](const FCkCrowdRemoteAgentRow& R) { return R.Id == F->Agents[I].Id; });
                if (!Row || !Row->bHasPathTroubleEvent || !FMath::IsFinite(Row->PathTroubleEventTimeSeconds) ||
                    Row->PathTroubleEventTimeSeconds < 0 || !Row->PathTroubleGoal.Equals(F->Goals[I])) { return false; }
            }
        }
        return true;
    });
    Act(TEXT("mount two exported full debugger windows"), [F]()
    {
        for (int32 I = 0; I < 2; ++I)
        {
            F->Panels[I] = SNew(SCkCrowdDebuggerWindow);
            F->Windows[I] = SNew(SWindow).Title(FText::FromString(TEXT("Crowd remote rendered ") + FString::FromInt(I)))
                .ClientSize(FVector2D(1200, 900)).SupportsMaximize(false).SupportsMinimize(false)[F->Panels[I].ToSharedRef()];
            FSlateApplication::Get().AddWindow(F->Windows[I].ToSharedRef());
        }
        F->Observe = FWorldDelegates::OnWorldPreActorTick.AddLambda([F](UWorld* W, ELevelTick, float)
        {
            // Pure observation before presentation consumes the actual retained delivery.
            for (int32 I = 0; I < 2; ++I)
            {
                if (!F->LocalPC[I].IsValid() || F->LocalPC[I]->GetWorld() != W ||
                    !IsValid(F->Authority(I))) { continue; }
                FCkCrowdRemoteSelectionEnvelope E;
                if (!Envelope(*F, I, E) || !E.bRetainedDelivery || !F->Authority(I)->HasServerRetained()) { continue; }
                if (E.Outcome == ECkCrowdRemoteSelectionOutcome::Accepted) { F->Observed[I] = E; }
                if (I == 0 && E.Outcome == ECkCrowdRemoteSelectionOutcome::Gone) { F->ObservedGone = E; }
            }
        });
        Pump(); return true;
    });
    Act(TEXT("normal world-strip clicks choose the two exact remote worlds"), [F]()
    {
        FCkDebuggerModel_WorldSelector Inventory;
        const auto Worlds = Inventory.Get_AvailableWorlds();
        for (int32 I = 0; I < 2; ++I)
        {
            const auto Strip = FindType(F->Panels[I].ToSharedRef(), TEXT("SCkDebug_WorldSelector"));
            if (!Strip.IsValid()) { return false; }
            TArray<TSharedPtr<SWidget>> B; Buttons(Strip.ToSharedRef(), B);
            const auto Index = Worlds.IndexOfByKey(F->LocalPC[I]->GetWorld());
            if (B.Num() != Worlds.Num() || !B.IsValidIndex(Index) || !Click(B[Index])) { return false; }
        }
        return true;
    });
    Wait(TEXT("both actual EventLogs render two distinct real Trouble subjects"), [F]()
    {
        Pump();
        for (int32 I = 0; I < 2; ++I)
        {
            const auto L = Log(F->Panels[I]);
            if (!L.IsValid() || L->Get_EntryCount() != 2 ||
                !FindRow(L.ToSharedRef(), Identity(F->Agents[0])).IsValid() ||
                !FindRow(L.ToSharedRef(), Identity(F->Agents[1])).IsValid()) { return false; }
            F->SelectionBefore[I] = F->Authority(I)->Get_SelectionAcceptedWatermark();
        }
        return true;
    });
    Act(TEXT("physical row selection emits each owner's real selection"), [F]()
    {
        const auto PeerBefore = FFixture::FTraffic::Read(F->Client(1));
        auto L0 = Log(F->Panels[0]);
        if (!L0.IsValid() || !Click(FindRow(L0.ToSharedRef(), Identity(F->Agents[0]))) ||
            !PeerBefore.Same(FFixture::FTraffic::Read(F->Client(1)))) { return false; }
        auto L1 = Log(F->Panels[1]);
        return L1.IsValid() && Click(FindRow(L1.ToSharedRef(), Identity(F->Agents[1])));
    });
    Wait(TEXT("own complete selection installation and explicit receipt, independent subjects"), [F]()
    {
        Pump(); if (!Chosen(*F, 0, 0) || !Chosen(*F, 1, 1)) { return false; }
        for (int32 I = 0; I < 2; ++I)
        { F->Accepted[I] = F->Authority(I)->Get_SelectionAcceptedWatermark(); F->ControlBefore[I] = F->Authority(I)->Get_ControlAcceptedWatermark();
            F->Traffic[I] = FFixture::FTraffic::Read(F->Client(I)); }
        return true;
    });
    Act(TEXT("accepted rendered artifacts"), [this, F]()
    {
        for (int32 I = 0; I < 2; ++I)
        {
            const auto Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/CrowdRemoteWindow"),
                F->Topology + FString::Printf(TEXT("-owner%d-accepted.png"), I));
            if (!Screenshot(F->Windows[I], Path)) { return false; } AddInfo(TEXT("Rendered screenshot: ") + Path);
        }
        return true;
    });
    Wait(TEXT("pure UI and wire reads send zero requests, receipts or teardown and preserve retained state"), [F]()
    {
        Pump();
        for (int32 I = 0; I < 2; ++I)
        {
            FCkCrowdRemoteSelectionEnvelope E;
            if (!Envelope(*F, I, E) || !Log(F->Panels[I]).IsValid() ||
                !FindText(F->Panels[I].ToSharedRef(), TEXT("Remote selection: accepted")) ||
                F->Authority(I)->Get_SelectionAcceptedWatermark() != F->Accepted[I] ||
                F->Authority(I)->Get_ControlAcceptedWatermark() != F->ControlBefore[I] || F->Authority(I)->HasServerRetained() ||
                !F->Traffic[I].Same(FFixture::FTraffic::Read(F->Client(I)))) { return false; }
        }
        return ++F->PureFrames >= 3;
    });
    Act(TEXT("real different-subject row click then same-frame authority unpossession"), [F]()
    {
        const auto L = Log(F->Panels[0]); if (!L.IsValid() || !IsValid(F->RemotePC[0]->GetPawn())) { return false; }
        F->Frame = GFrameCounter;
        if (!Click(FindRow(L.ToSharedRef(), Identity(F->Agents[1])))) { return false; }
        if (GFrameCounter != F->Frame) { return false; }
        F->GoneSerial = F->Client(0)->Get_LocalSelectionAttemptWatermark();
        if (F->GoneSerial <= F->Accepted[0]) { return false; }
        F->GoneStarted = true; F->RemotePC[0]->UnPossess(); return !IsValid(F->RemotePC[0]->GetPawn());
    });
    Wait(TEXT("actual pawn-free Gone feedback clears history while peer stays accepted"), [F]()
    {
        Pump(); const auto& E = F->ObservedGone;
        return FCkCrowdRemoteDebugValidation::IsValid(E) && E.bRetainedDelivery &&
            E.Outcome == ECkCrowdRemoteSelectionOutcome::Gone &&
            E.Receipt.RequestSerial == F->GoneSerial &&
            E.Receipt.SessionGeneration == F->Client(0)->Get_TransportSessionGeneration() &&
            E.SelectionRequestSerial > F->Accepted[0] && !F->Authority(0)->HasServerRetained() &&
            F->Authority(0)->Get_SelectionAcceptedWatermark() == F->Accepted[0] &&
            Log(F->Panels[0]).IsValid() && Log(F->Panels[0])->Get_EntryCount() == 0 &&
            FindText(F->Panels[0].ToSharedRef(), TEXT("Remote selection: owner context ended")) && Chosen(*F, 1, 1);
    });
    Act(TEXT("Gone rendered artifact and retain genuine child/old row before releasing parents"), [this, F]()
    {
        const auto Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/CrowdRemoteWindow"), F->Topology + TEXT("-owner0-gone.png"));
        if (!Screenshot(F->Windows[0], Path)) { return false; } AddInfo(TEXT("Rendered screenshot: ") + Path);
        F->HeldLog = Log(F->Panels[1]);
        if (!F->HeldLog.IsValid()) { return false; }
        F->HeldRow = FindRow(F->HeldLog.ToSharedRef(), Identity(F->Agents[0]));
        const auto P = FindType(F->Panels[1].ToSharedRef(), TEXT("SCkCrowdDebugger_EventLogPanel"));
        if (!P.IsValid() || !F->HeldRow.IsValid()) { return false; }
        F->ReleasedPanel = P; F->ReleasedWindow = F->Panels[1];
        F->Panels[1]->Release_Presentation(); F->Windows[1]->SetContent(SNullWidget::NullWidget);
        FSlateApplication::Get().DestroyWindowImmediately(F->Windows[1].ToSharedRef());
        F->Windows[1].Reset(); F->Panels[1].Reset(); return true;
    });
    Act(TEXT("weak production parent lifetime and held generic child read/click safety"), [F]()
    {
        if (F->ReleasedWindow.IsValid() || F->ReleasedPanel.IsValid() || !F->HeldLog.IsValid() || !F->HeldRow.IsValid()) { return false; }
        F->OrphanWindow = SNew(SWindow).Title(FText::FromString(TEXT("Released Crowd EventLog child")))
            .ClientSize(FVector2D(900, 400))[F->HeldLog.ToSharedRef()];
        FSlateApplication::Get().AddWindow(F->OrphanWindow.ToSharedRef()); Pump();
        if (F->HeldLog->Get_EntryCount() != 2) { return false; }
        // No parent-owned delegate is invoked manually: click an actual retained production row.
        return Click(F->HeldRow);
    });
    Wait(TEXT("released parent callback does not admit a new owner selection"), [F]()
    {
        Pump(); return F->Authority(1)->Get_SelectionAcceptedWatermark() == F->Accepted[1] &&
            !F->Authority(1)->HasServerRetained() &&
            F->Traffic[1].Same(FFixture::FTraffic::Read(F->Client(1))) && F->ReleasedPanel.IsValid() == false &&
            F->ReleasedWindow.IsValid() == false;
    });
    // Background clear is deliberately outside this phase: no public deterministic hitless camera point.
    ADD_LATENT_AUTOMATION_COMMAND(FScenario(this, F, MoveTemp(Steps)));
    ADD_LATENT_AUTOMATION_COMMAND(FAlwaysRestore(F));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

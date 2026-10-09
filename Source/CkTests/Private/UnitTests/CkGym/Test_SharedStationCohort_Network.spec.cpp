#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace ck_shared_cohort_integration
{
    using namespace ck::auto_test::net;
    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    // Exact public reflected signatures only; rejected schemas never reach ProcessEvent.
    struct FCall
    {
        UObject* Target = nullptr; UFunction* Function = nullptr; TUniquePtr<FStructOnScope> Args;
        FCall(UObject* InTarget, const TCHAR* Name, int32 Count, bool Pure)
        {
            if (!IsValid(InTarget)) { return; }
            auto* F = InTarget->FindFunction(Name);
            if (!IsValid(F) || F->NumParms != Count || F->HasAnyFunctionFlags(FUNC_Net) ||
                !F->HasAllFunctionFlags(Pure ? FUNC_BlueprintPure : FUNC_BlueprintCallable)) { return; }
            int32 Actual = 0;
            for (TFieldIterator<FProperty> It(F); It; ++It) { if (It->HasAnyPropertyFlags(CPF_Parm)) { ++Actual; } }
            if (Actual != Count) { return; }
            Target = InTarget; Function = F; Args = MakeUnique<FStructOnScope>(F);
        }
        template<class P> P* Field(const TCHAR* Name, bool Return) const
        {
            auto* Q = Function ? FindFProperty<P>(Function, Name) : nullptr;
            if (!Q || !Q->HasAnyPropertyFlags(CPF_Parm) || Q->HasAnyPropertyFlags(CPF_ReferenceParm) ||
                Q->HasAnyPropertyFlags(CPF_ReturnParm) != Return ||
                Q->HasAnyPropertyFlags(CPF_OutParm) != Return) { return nullptr; }
            return Q;
        }
        bool Send() { if (!IsValid(Target) || !IsValid(Function) || !Args) { return false; } Target->ProcessEvent(Function, Args->GetStructMemory()); return true; }
    };
    auto Bool(UObject* T, const TCHAR* Name, bool& Out) -> bool
    { FCall C(T, Name, 1, true); auto* Q = C.Field<FBoolProperty>(TEXT("ReturnValue"), true); if (!Q || !C.Send()) { return false; } Out = Q->GetPropertyValue_InContainer(C.Args->GetStructMemory()); return true; }
    auto Int(UObject* T, const TCHAR* Name, int32& Out) -> bool
    { FCall C(T, Name, 1, true); auto* Q = C.Field<FIntProperty>(TEXT("ReturnValue"), true); if (!Q || !C.Send()) { return false; } Out = Q->GetPropertyValue_InContainer(C.Args->GetStructMemory()); return true; }
    auto Start(UObject* T, int32 Scenario) -> bool
    {
        FCall C(T, TEXT("Request_StartScenario"), 2, false);
        auto* In = C.Field<FIntProperty>(TEXT("InScenario"), false); auto* Ret = C.Field<FBoolProperty>(TEXT("ReturnValue"), true);
        if (!In || !Ret) { return false; }
        In->SetPropertyValue_InContainer(C.Args->GetStructMemory(), Scenario);
        return C.Send() && Ret->GetPropertyValue_InContainer(C.Args->GetStructMemory());
    }
    auto Stop(UObject* T) -> void { FCall C(T, TEXT("Request_Stop"), 0, false); C.Send(); }
    auto Handle(UObject* T, const TCHAR* Name, FCk_Handle& Out) -> bool
    {
        FCall C(T, Name, 1, true); auto* Q = C.Field<FStructProperty>(TEXT("ReturnValue"), true);
        if (!Q || Q->Struct != FCk_Handle::StaticStruct() || !C.Send()) { return false; }
        Out = *Q->ContainerPtrToValuePtr<FCk_Handle>(C.Args->GetStructMemory()); return true;
    }
    auto Handles(UObject* T, const TCHAR* Name, TArray<FCk_Handle>& Out) -> bool
    {
        FCall C(T, Name, 1, true); auto* Q = C.Field<FArrayProperty>(TEXT("ReturnValue"), true);
        auto* Inner = Q ? CastField<FStructProperty>(Q->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct() || !C.Send()) { return false; }
        Out = *Q->ContainerPtrToValuePtr<TArray<FCk_Handle>>(C.Args->GetStructMemory()); return true;
    }
    auto Failure(UObject* T) -> FString
    {
        FCall C(T, TEXT("Get_Failure"), 1, true); auto* Q = C.Field<FStrProperty>(TEXT("ReturnValue"), true);
        return Q && C.Send() ? Q->GetPropertyValue_InContainer(C.Args->GetStructMemory()) : TEXT("public diagnostic schema rejected");
    }
    auto ProbeMalformedTransform(UObject* Owner, const FCk_Handle& Station,
        UClass* ExpectedStationClass) -> bool
    {
        if (!IsValid(Owner) || !Live(Station) || !IsValid(ExpectedStationClass)) { return false; }
        auto* World = Owner->GetWorld();
        if (!IsValid(World)) { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        TArray<FCk_Handle> ChildrenBefore;
        int32 TagsBefore = -1;
        if (!Live(Root) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Station, Root, ChildrenBefore) ||
            !Int(Owner, TEXT("Get_CurrentRecipeTagCount"), TagsBefore) || TagsBefore < 0)
        { return false; }
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station);
        if (!IsValid(Script) || Script->GetClass() != ExpectedStationClass) { return false; }
        const auto* Transform = FindFProperty<FStructProperty>(Script->GetClass(), TEXT("InitialTransform"));
        const auto* Rotation = Transform && Transform->Struct == TBaseStructure<FTransform>::Get()
            ? FindFProperty<FStructProperty>(Transform->Struct, TEXT("Rotation")) : nullptr;
        if (!Rotation || Rotation->Struct != TBaseStructure<FQuat>::Get()) { return false; }
        auto* Value = Transform->ContainerPtrToValuePtr<FTransform>(Script);
        if (!Value || !Value->IsValid()) { return false; }
        auto* Raw = Rotation->ContainerPtrToValuePtr<FQuat>(Value);
        if (!Raw) { return false; }
        FCall C(Owner, TEXT("Request_ProbeMalformedTransform"), 1, false);
        auto* Return = C.Field<FBoolProperty>(TEXT("ReturnValue"), true);
        if (!Return) { return false; }
        const double Original = Raw->W;
        Raw->W = std::numeric_limits<double>::quiet_NaN(); // Direct reflected corruption: no diagnostic setter.
        const double Corrupt = Raw->W;
        const bool Called = C.Send(); // Probe synchronously, before any later station timer tick.
        const bool StillLive = IsValid(Script) && Live(Station);
        const bool Unchanged = StillLive && FMemory::Memcmp(&Corrupt, &Raw->W, sizeof(double)) == 0;
        TArray<FCk_Handle> ChildrenAfter;
        int32 TagsAfter = -1;
        const bool SamePublishedState = StillLive &&
            UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Station, Root, ChildrenAfter) &&
            Int(Owner, TEXT("Get_CurrentRecipeTagCount"), TagsAfter) &&
            TagsAfter == TagsBefore && ChildrenAfter.Num() == ChildrenBefore.Num();
        bool SameChildren = SamePublishedState;
        for (const auto& Child : ChildrenAfter)
        { if (!ChildrenBefore.Contains(Child)) { SameChildren = false; } }
        // Restore only fixture-induced corruption after proving production preserved it.
        // A later display resize can legitimately read InitialTransform on another tick.
        if (StillLive) { Raw->W = Original; }
        return Called && Unchanged && SameChildren &&
            Return->GetPropertyValue_InContainer(C.Args->GetStructMemory());
    }
    struct FPlayer
    { TWeakObjectPtr<UWorld> World; TWeakObjectPtr<APlayerController> PC; TWeakObjectPtr<APawn> Pawn; TWeakObjectPtr<APlayerState> PS; };
    struct FState
    {
        TArray<TWeakObjectPtr<UWorld>> Worlds; TArray<FPlayer> Players;
        TArray<TWeakObjectPtr<AActor>> Owners; TArray<TArray<FCk_Handle>> Carriers;
        TWeakObjectPtr<AGameModeBase> Mode; TWeakObjectPtr<APlayerController> Remote[2];
        UClass* HarnessClass = nullptr; UClass* StationClass = nullptr; bool Spawned = false, Dedicated = false;
        int32 Scenario = -1;
        bool Transfer[3]{};
        bool TransformCorrupted[3]{};
        FCk_Handle TerminalOwner[3];
        TArray<FCk_Handle> TerminalCaptured[3];
        int32 TerminalTags[3]{-1, -1, -1};
        int32 TerminalPostTicks[3]{};
        bool Context() const
        {
            auto Actual = Get_AllPIEWorlds(); auto* Server = Get_ServerWorld();
            if (Actual.Num() != 3 || Get_NumClientWorlds() != 2 || !IsValid(Server) ||
                !Mode.IsValid() || Server->GetAuthGameMode() != Mode.Get() ||
                Mode->GetClass() != AGameModeBase::StaticClass() || Server->GetNetMode() != (Dedicated ? NM_DedicatedServer : NM_ListenServer)) { return false; }
            for (int32 I = 0; I < Worlds.Num(); ++I)
            { auto* W = Worlds[I].Get(); if (!IsValid(W) || !Actual.Contains(W) || !W->HasBegunPlay() || (W != Server && W->GetNetMode() != NM_Client)) { return false; } }
            if (Worlds.Num() != 3 || Players.Num() != (Dedicated ? 4 : 5)) { return false; }
            int32 ServerLocal = 0;
            for (auto* W : Actual)
            {
                if (!IsValid(W) || !Worlds.Contains(W)) { return false; }
                int32 ActualCount = 0, CapturedCount = 0;
                for (auto It = W->GetPlayerControllerIterator(); It; ++It)
                {
                    auto* Controller = It->Get();
                    if (!IsValid(Controller)) { return false; }
                    ++ActualCount;
                    int32 Matches = 0;
                    for (const auto& P : Players)
                    { if (P.World.Get() == W && P.PC.Get() == Controller) { ++Matches; } }
                    if (Matches != 1) { return false; }
                    if (W == Server && Controller->IsLocalController()) { ++ServerLocal; }
                }
                for (const auto& P : Players) { if (P.World.Get() == W) { ++CapturedCount; } }
                const int32 Expected = W == Server ? (Dedicated ? 2 : 3) : 1;
                if (ActualCount != Expected || CapturedCount != Expected) { return false; }
            }
            if (ServerLocal != (Dedicated ? 0 : 1)) { return false; }
            for (const auto& P : Players)
            {
                auto* PC = P.PC.Get(); APawn* Pawn = P.Pawn.Get(); auto* PS = P.PS.Get();
                if (!P.World.IsValid() || !IsValid(PC) || !IsValid(Pawn) || !IsValid(PS) ||
                    PC->GetWorld() != P.World.Get() || PC->GetPawn() != Pawn || PC->PlayerState.Get() != PS ||
                    Pawn->GetWorld() != P.World.Get() || Pawn->GetController() != PC || Pawn->GetPlayerState() != PS || PS->GetWorld() != P.World.Get()) { return false; }
                if (P.World.Get() != Server && !PC->IsLocalController()) { return false; }
                if (PC->IsLocalController())
                {
                    auto* GI = P.World->GetGameInstance(); auto* LP = PC->GetLocalPlayer();
                    if (!PC->IsLocalController() || !IsValid(GI) || GI->GetWorld() != P.World.Get() || GI->GetLocalPlayers().Num() != 1 ||
                        !IsValid(LP) || GI->GetLocalPlayers()[0] != LP || LP->GetWorld() != P.World.Get() || LP->GetPlayerController(P.World.Get()) != PC) { return false; }
                }
            }
            for (int32 I = 0; I < 2; ++I)
            { if (!Remote[I].IsValid() || TryGet_RemoteClientPlayerController(Server, I) != Remote[I].Get()) { return false; } }
            return Worlds.Num() == 3;
        }
        FString ContextObservation() const
        {
            auto* Server = Get_ServerWorld();
            FString Result = FString::Printf(TEXT(" worlds=%d clients=%d serverLive=%d modeLive=%d remote0=%d remote1=%d"),
                Get_AllPIEWorlds().Num(), Get_NumClientWorlds(), IsValid(Server), Mode.IsValid(), Remote[0].IsValid(), Remote[1].IsValid());
            for (const auto& P : Players)
            {
                auto* W = P.World.Get(); auto* PC = P.PC.Get(); APawn* Pawn = P.Pawn.Get(); auto* PS = P.PS.Get();
                auto* GI = IsValid(W) ? W->GetGameInstance() : nullptr;
                auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
                Result += FString::Printf(TEXT(" [worldLive=%d net=%d pcLive=%d local=%d pawnLive=%d psLive=%d pawnMatch=%d psMatch=%d controllerMatch=%d pawnPSMatch=%d giWorld=%d locals=%d lpLive=%d lpWorld=%d lpPC=%d]"),
                    IsValid(W), IsValid(W) ? int32(W->GetNetMode()) : INDEX_NONE,
                    IsValid(PC), IsValid(PC) && PC->IsLocalController(), IsValid(Pawn), IsValid(PS),
                    IsValid(PC) && PC->GetPawn() == Pawn, IsValid(PC) && PC->PlayerState.Get() == PS,
                    IsValid(Pawn) && Pawn->GetController() == PC, IsValid(Pawn) && Pawn->GetPlayerState() == PS,
                    IsValid(GI) && GI->GetWorld() == W, IsValid(GI) ? GI->GetLocalPlayers().Num() : INDEX_NONE,
                    IsValid(LP), IsValid(LP) && LP->GetWorld() == W, IsValid(W) && IsValid(LP) && LP->GetWorld() == W && LP->GetPlayerController(W) == PC);
            }
            return Result;
        }
        void Cleanup()
        { for (auto& Weak : Owners) { auto* A = Weak.Get(); if (IsValid(A)) { Stop(A); A->Destroy(); } } }
    };
    class FExercise : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test; TSharedRef<FState> State; double Deadline = -1.0;
        bool Reject(const FString& Why) { Test->AddError(Why); FailSession(Why); return true; }
    public:
        FExercise(FAutomationTestBase* T, TSharedRef<FState> S) : Test(T), State(S) {}
        bool Update() override
        {
            if (Get_HasSessionFailed()) { return true; }
            if (Deadline < 0.0) { Deadline = FPlatformTime::Seconds() + 120.0; }
            if (!State->Spawned)
            {
                State->HarnessClass = FSoftClassPath(TEXT("/Script/Angelscript.CkSharedStationCohort_TestHarness")).TryLoadClass<AActor>();
                State->StationClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_EntityScript_GymStation")).TryLoadClass<UObject>();
                auto* Server = Get_ServerWorld();
                if (!IsValid(State->HarnessClass) || !IsValid(State->StationClass) || !IsValid(Server) || Get_AllPIEWorlds().Num() != 3) { return Reject(TEXT("Missing real AS fixture classes or three-world topology")); }
                // World BeginPlay does not imply completed controller/pawn/PlayerState login.
                // Capture only once the complete original topology passes its exact guard.
                State->Worlds.Empty(); State->Players.Empty();
                State->Mode = Server->GetAuthGameMode();
                for (int32 I = 0; I < 2; ++I) { State->Remote[I] = TryGet_RemoteClientPlayerController(Server, I); }
                for (auto* W : Get_AllPIEWorlds())
                {
                    if (!IsValid(W)) { return Reject(TEXT("Invalid world before owner capture")); }
                    State->Worlds.Add(W);
                    for (auto It = W->GetPlayerControllerIterator(); It; ++It)
                    { auto* PC = It->Get(); if (!IsValid(PC)) { return Reject(TEXT("Invalid real controller capture")); } APawn* Pawn = PC->GetPawn(); State->Players.Add({W, PC, Pawn, PC->PlayerState.Get()}); }
                }
                if (!State->Context())
                {
                    if (FPlatformTime::Seconds() > Deadline)
                    { return Reject(FString(TEXT("Initial exact controller/pawn/world correlation rejected")) + State->ContextObservation()); }
                    return false;
                }
                for (int32 I = 0; I < 3; ++I)
                {
                    auto* W = State->Worlds[I].Get();
                    auto* A = W->SpawnActor<AActor>(State->HarnessClass, FTransform::Identity);
                    State->Owners.Add(A);
                    if (!IsValid(A) || A->GetWorld() != W || A->GetIsReplicated() || !Start(A, State->Scenario)) { return Reject(TEXT("Real local callback owner/start rejected")); }
                    TArray<FCk_Handle> Requests;
                    if (!Handles(A, TEXT("Get_EarlyCandidates"), Requests) || Requests.Num() != 2) { return Reject(TEXT("Missing genuine preconstruction request candidates")); }
                    const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
                    for (const auto& H : Requests)
                    {
                        TArray<FCk_Handle> Children;
                        if (!Live(H) || !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(H, Root, Children) ||
                            !H.Has<ck::FFragment_EntityScript_RequestSpawnEntity>() || !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H)) { return Reject(TEXT("Early identity is not an actual live production spawn request carrier")); }
                        const auto Parent = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H);
                        const auto& Request = H.Get<ck::FFragment_EntityScript_RequestSpawnEntity>();
                        auto* Archetype = Request.Get_EntityScriptClassArchetype().Get();
                        if (!Live(Parent) || Request.Get_NewEntity() != Parent || !IsValid(Archetype) ||
                            Archetype->GetClass() != State->StationClass || !Request.Get_SpawnParams().IsValid())
                        { return Reject(TEXT("Actual carrier target/class/typed spawn tuple differs from the real station request")); }
                    }
                    State->Carriers.Add(Requests);
                }
                State->Spawned = true;
            }
            if (!State->Context()) { return Reject(TEXT("Original live worlds/controllers/pawns/playerstates changed")); }
            bool AllDone = true;
            int32 AllocatedCarriers = 0;
            for (int32 I = 0; I < 3; ++I)
            {
                auto* A = State->Owners[I].Get(); bool Failed = false, Done = false;
                if (!IsValid(A) || A->GetWorld() != State->Worlds[I].Get() || A->GetClass() != State->HarnessClass ||
                    !Bool(A, TEXT("Get_HasFailed"), Failed) || !Bool(A, TEXT("Get_IsDone"), Done))
                { return Reject(TEXT("Lost callback owner/public observation schema")); }
                if (Failed) { return Reject(FString::Printf(TEXT("World %d real caller failed: %s"), I, *Failure(A))); }
                if (State->Scenario == 0 && !State->Transfer[I])
                {
                    bool Waiting = false;
                    if (!Bool(A, TEXT("Get_IsWaitingTransfer"), Waiting))
                    { return Reject(TEXT("Transfer opportunity getter schema rejected")); }
                    if (Waiting)
                    {
                        FCk_Handle Late, NewOwner;
                        TArray<FCk_Handle> Captured, Children;
                        auto* W = State->Worlds[I].Get();
                        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W);
                        if (!Handle(A, TEXT("Get_Late"), Late) ||
                            !Handle(A, TEXT("Get_NewOwner"), NewOwner) ||
                            !Handles(A, TEXT("Get_Captured"), Captured) ||
                            !Live(Late) || !Live(NewOwner) || !Captured.Contains(Late) ||
                            Captured.Contains(NewOwner) ||
                            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Late, Root, Children) ||
                            Children.Num() != 1 || !Captured.Contains(Children[0]) ||
                            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(NewOwner, Root, Children) ||
                            !Children.IsEmpty() ||
                            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Late) ||
                            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(NewOwner) ||
                            UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(NewOwner) != Root)
                        { return Reject(TEXT("Late/deep transfer operands or original captured closure rejected")); }
                        const auto Parent = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Late);
                        auto* Script = Live(Parent) ? UCk_Utils_EntityScript_UE::TryGet_EntityScript(Parent) : nullptr;
                        if (!Captured.Contains(Parent) || !IsValid(Script) ||
                            Script->GetClass() != State->StationClass)
                        { return Reject(TEXT("Late child is not captured under the real station")); }
                        UCk_Utils_EntityLifetime_UE::Request_TransferLifetimeOwner(Late, NewOwner);
                        State->Transfer[I] = true;
                    }
                }
                if (State->Scenario == 4 && !State->TransformCorrupted[I])
                {
                    bool Waiting = false;
                    if (!Bool(A, TEXT("Get_IsWaitingTransformCorruption"), Waiting))
                    { return Reject(TEXT("Malformed-transform opportunity getter schema rejected")); }
                    if (Waiting)
                    {
                        FCk_Handle Station;
                        if (!Handle(A, TEXT("Get_TransformCorruptionTarget"), Station) ||
                            !ProbeMalformedTransform(A, Station, State->StationClass))
                        { return Reject(TEXT("Real malformed station transform was sanitized or accepted")); }
                        State->TransformCorrupted[I] = true;
                    }
                }
                AllDone &= Done && State->TerminalPostTicks[I] >= 6;
                if (Done)
                {
                    TArray<FCk_Handle> Captured;
                    FCk_Handle Owner;
                    bool HealthyRetired = false;
                    int32 TagCount = -1;
                    if (!Handles(A, TEXT("Get_Captured"), Captured) ||
                        !Handle(A, TEXT("Get_TerminalOwner"), Owner) ||
                        !Bool(A, TEXT("Get_HealthyRetirementObserved"), HealthyRetired) ||
                        !Int(A, TEXT("Get_CurrentRecipeTagCount"), TagCount) ||
                        TagCount < 0 || Captured.IsEmpty() || !Captured.Contains(Owner) ||
                        (State->Scenario != 3 && !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Owner)) ||
                        (State->Scenario != 1 && !HealthyRetired) ||
                        (State->Scenario == 1 && HealthyRetired) ||
                        (State->Scenario == 0 && !State->Transfer[I]) ||
                        (State->Scenario == 4 && !State->TransformCorrupted[I]))
                    { return Reject(TEXT("Terminal cohort failed to retain its exact owner after the healthy path")); }
                    bool AnyAllocated = false;
                    for (const auto& H : Captured)
                    { AnyAllocated |= UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }
                    if (!AnyAllocated) { return Reject(TEXT("Terminal failure destroyed every retained creator identity")); }
                    if (State->TerminalOwner[I] != FCk_Handle() && State->TerminalOwner[I] != Owner)
                    { return Reject(TEXT("Terminal owner changed during post-failure ticks")); }
                    if (State->TerminalPostTicks[I] == 0)
                    { State->TerminalCaptured[I] = Captured; State->TerminalTags[I] = TagCount; }
                    else
                    {
                        if (TagCount > State->TerminalTags[I] ||
                            Captured.Num() != State->TerminalCaptured[I].Num())
                        { return Reject(TEXT("Closed station published a new tag or allocated descendant")); }
                        for (const auto& H : Captured)
                        { if (!State->TerminalCaptured[I].Contains(H))
                          { return Reject(TEXT("Closed station added an unrecorded descendant")); } }
                    }
                    State->TerminalOwner[I] = Owner;
                    ++State->TerminalPostTicks[I];
                    for (const auto& H : State->Carriers[I])
                    { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { ++AllocatedCarriers; AllDone = false; } }
                }
            }
            if (AllDone) { Test->AddInfo(TEXT("All three real callback owners proved healthy retirement when applicable and retained terminal world closure")); return true; }
            if (FPlatformTime::Seconds() > Deadline)
            { return Reject(FString::Printf(TEXT("Bounded real shared-cohort episodes timed out; allocated initial request carriers=%d; no missing opportunity is skipped"), AllocatedCarriers)); }
            return false;
        }
    };
    class FAlwaysCleanup : public IAutomationLatentCommand
    {
        TSharedRef<FState> State;
    public: explicit FAlwaysCleanup(TSharedRef<FState> S) : State(S) {}
        bool Update() override { State->Cleanup(); return true; }
    };
    auto Run(FAutomationTestBase* Test, const FString& Topology, int32 Scenario) -> bool
    {
        const auto State = MakeShared<FState>(); State->Scenario = Scenario;
        if (Scenario < 0 || Scenario > 4) { Test->AddError(TEXT("Invalid Shared cohort scenario")); return false; }
        if (!TryGet_DedicatedServer(Topology, State->Dedicated)) { Test->AddError(TEXT("Invalid explicit topology")); return false; }
        Test->AddCommand(new FCk_Latent_StartPIEMultiClient(3, TEXT("/Engine/Maps/Entry"), Topology, AGameModeBase::StaticClass()));
        Test->AddCommand(new FCk_Latent_WaitForPIEReady(3, 60.0f));
        Test->AddCommand(new FExercise(Test, State));
        Test->AddCommand(new FAlwaysCleanup(State));
        Test->AddCommand(new FCk_Latent_EndPIE());
        Test->AddCommand(new FCk_Latent_AssertCondition(Test, FCk_NetAutoTest_Assertion::CreateLambda([State]()
        { if (!Get_AllPIEWorlds().IsEmpty()) { return false; } for (const auto& W : State->Worlds) { if (W.IsValid()) { return false; } } for (const auto& A : State->Owners) { if (A.IsValid()) { return false; } } return true; }), TEXT("unconditional owner cleanup/settings restoration/EndPIE"), true));
        return true;
    }
}
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSharedCohortProduction, "Ck.SharedStationCohort.Network.Production", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSharedCohortProduction::RunTest(const FString& Parameters) { return ck_shared_cohort_integration::Run(this, Parameters, 0); }
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSharedCohortInterrupted, "Ck.SharedStationCohort.Network.InterruptedConstruction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSharedCohortInterrupted::RunTest(const FString& Parameters) { return ck_shared_cohort_integration::Run(this, Parameters, 1); }
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSharedCohortMalformed, "Ck.SharedStationCohort.Network.MalformedCallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSharedCohortMalformed::RunTest(const FString& Parameters) { return ck_shared_cohort_integration::Run(this, Parameters, 2); }
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSharedCohortReentrant, "Ck.SharedStationCohort.Network.ReentrantRetirement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSharedCohortReentrant::RunTest(const FString& Parameters) { return ck_shared_cohort_integration::Run(this, Parameters, 3); }
CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkSharedCohortMalformedTransform, "Ck.SharedStationCohort.Network.MalformedTransform", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkSharedCohortMalformedTransform::RunTest(const FString& Parameters) { return ck_shared_cohort_integration::Run(this, Parameters, 4); }
#endif

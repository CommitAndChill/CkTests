#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkSelectionGym_BankCensus.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"

#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_sm_gym_source_closure_test
{
    constexpr auto RacingPath = TEXT("/Script/Angelscript.Ck_SmTest_RacingEventDriven_GymActor");
    constexpr auto GenericPath = TEXT("/Script/Angelscript.Ck_SmTest_GymActor");

    struct FRun
    {
        bool Dedicated = false;
        UClass* RacingClass = nullptr;
        UClass* GenericClass = nullptr;
        TWeakObjectPtr<AActor> Racing;
        TWeakObjectPtr<AActor> RacingNormal;
        TWeakObjectPtr<AActor> Generic;
        FCk_Handle RacingRoot;
        FCk_Handle RacingNormalRoot;
        FCk_Handle GenericRoot;
        TArray<FCk_Handle> GenericForest;
        FString FirstCensusReason;
        int32 GenericCensusAttempts = 0;
        FString RacingResultTextAtClose;
        double RacingClosedAt = 0;
        double GenericClosedAt = 0;
        bool RacingClosedSameAction = false;
        bool GenericClosed = false;
    };

    template<class T> T* Field(const UStruct* Type, const TCHAR* Name)
    { return Type ? FindFProperty<T>(Type, Name) : nullptr; }

    struct FCall
    {
        UObject* Target = nullptr;
        UFunction* Function = nullptr;
        TUniquePtr<FStructOnScope> Args;
        FCall(UObject* Object, const TCHAR* Name)
        {
            if (!IsValid(Object)) { return; }
            Function = Object->FindFunction(Name);
            if (!IsValid(Function)) { Function = nullptr; return; }
            Target = Object;
            Args = MakeUnique<FStructOnScope>(Function);
        }
        bool Send() const
        { return IsValid(Target) && IsValid(Function) && Args &&
            Args->GetStructMemory() &&
            (Target->ProcessEvent(Function, Args->GetStructMemory()), true); }
    };

    bool Bool(UObject* Object, const TCHAR* Name, bool& Out)
    {
        Out = false;
        FCall C(Object, Name);
        auto* Return = Field<FBoolProperty>(C.Function, TEXT("ReturnValue"));
        if (!Return || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = Return->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Int(UObject* Object, const TCHAR* Name, int32& Out)
    {
        Out = INDEX_NONE;
        FCall C(Object, Name);
        auto* Return = Field<FIntProperty>(C.Function, TEXT("ReturnValue"));
        if (!Return || C.Function->NumParms != 1 || !C.Send()) { return false; }
        Out = Return->GetPropertyValue_InContainer(C.Args->GetStructMemory());
        return true;
    }

    bool Roots(UObject* Object, TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        FCall C(Object, TEXT("Get_SourceRoots"));
        auto* Return = Field<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        if (!Return || !CastField<FStructProperty>(Return->Inner) ||
            CastField<FStructProperty>(Return->Inner)->Struct != FCk_Handle::StaticStruct() ||
            C.Function->NumParms != 1 || !C.Send()) { return false; }
        FScriptArrayHelper Array(Return,
            Return->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()));
        for (int32 I = 0; I < Array.Num(); ++I)
        { Out.Add(*reinterpret_cast<FCk_Handle*>(Array.GetRawPtr(I))); }
        return true;
    }

    bool Close(AActor* Actor)
    {
        FCall C(Actor, TEXT("Request_CloseSource"));
        return C.Function && C.Function->NumParms == 0 && C.Send();
    }

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    int32 SourceActorsInWorld(UWorld* World, UClass* Class)
    {
        if (!IsValid(World) || !IsValid(Class)) { return INDEX_NONE; }
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        { if ((*It)->GetClass() == Class) { ++Count; } }
        return Count;
    }

    uint64 IrisId(UWorld* World, AActor* Actor)
    {
        if (!IsValid(World) || !IsValid(Actor) || Actor->GetWorld() != World)
        { return 0; }
        auto* Driver = World->GetNetDriver();
        if (!IsValid(Driver) || Driver->GetWorld() != World) { return 0; }
        auto* System = Driver->GetReplicationSystem();
        auto* Bridge = IsValid(System) ? System->GetReplicationBridge() : nullptr;
        if (!IsValid(Bridge)) { return 0; }
        const auto Handle = Bridge->GetReplicatedRefHandle(Actor);
        return Handle.IsValid() ? Handle.GetId() : 0;
    }

    bool HasNoSmHandle(AActor* Actor);

    bool ExactNormalRacingClientReplicas(const FRun& Run)
    {
        auto* ServerWorld = ck::auto_test::net::Get_ServerWorld();
        auto* ServerActor = Run.RacingNormal.Get();
        if (!IsValid(ServerWorld) || !IsValid(ServerActor) ||
            ServerActor->GetWorld() != ServerWorld || !ServerActor->HasAuthority())
        { return false; }
        const uint64 ServerId = IrisId(ServerWorld, ServerActor);
        if (ServerId == 0) { return false; }
        for (int32 ClientIndex = 0; ClientIndex != 2; ++ClientIndex)
        {
            auto* World = ck::auto_test::net::Get_ClientWorld(ClientIndex);
            if (!IsValid(World) || World->GetNetMode() != NM_Client ||
                SourceActorsInWorld(World, Run.RacingClass) != 1)
            { return false; }
            int32 MatchingReplicas = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                auto* Actor = *It;
                if (!IsValid(Actor) || Actor->GetClass() != Run.RacingClass ||
                    Actor->GetWorld() != World || IrisId(World, Actor) != ServerId)
                { continue; }
                ++MatchingReplicas;
                TArray<FCk_Handle> RootsOnClient;
                int32 Constructed = INDEX_NONE;
                if (Actor->HasAuthority() || !Roots(Actor, RootsOnClient) ||
                    !RootsOnClient.IsEmpty() ||
                    !Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) ||
                    Constructed != 0 || !HasNoSmHandle(Actor))
                { return false; }
            }
            if (MatchingReplicas != 1) { return false; }
        }
        return true;
    }

    bool ClientLocalRacingSpawnClosesWithoutSource(UWorld* World, UClass* Class)
    {
        if (!IsValid(World) || World->GetNetMode() != NM_Client ||
            !IsValid(Class)) { return false; }
        auto* Actor = World->SpawnActorDeferred<AActor>(Class,
            FTransform(FVector(0, 250, 0)));
        if (!IsValid(Actor)) { return false; }
        Actor->FinishSpawning(FTransform(FVector(0, 250, 0)));
        TArray<FCk_Handle> RootsOnClient;
        int32 Constructed = INDEX_NONE;
        bool Closed = false;
        auto* A = Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestA"));
        auto* B = Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestB"));
        const bool Rejected = Actor->HasAuthority() &&
            Actor->GetWorld() == World && Actor->GetClass() == Class &&
            Bool(Actor, TEXT("Get_SourceClosed"), Closed) && Closed &&
            Roots(Actor, RootsOnClient) && RootsOnClient.IsEmpty() &&
            Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) &&
            Constructed == 0 && HasNoSmHandle(Actor) && A && B &&
            A->GetPropertyValue_InContainer(Actor) == 0 &&
            B->GetPropertyValue_InContainer(Actor) == 0;
        const bool Destroyed = Actor->Destroy();
        return Rejected && Destroyed;
    }

    bool ExactRoots(AActor* Actor, UWorld* World, int32 Expected,
        TArray<FCk_Handle>& Out)
    {
        Out.Reset();
        if (!IsValid(Actor) || !IsValid(World) || Actor->GetWorld() != World ||
            !Roots(Actor, Out) || Out.Num() != Expected) { return false; }
        const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Allocated(WorldRoot)) { return false; }
        TSet<FCk_Handle> Seen;
        for (const auto& Root : Out)
        {
            if (!Allocated(Root) || Seen.Contains(Root) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Root) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Root) != WorldRoot ||
                UCk_Utils_EntityLifetime_UE::Get_WorldForEntity(Root) != World)
            { return false; }
            Seen.Add(Root);
        }
        return true;
    }

    bool ClientSourceConstructorsRemainIdle(const FRun& Run)
    {
        for (int32 ClientIndex = 0; ClientIndex != 2; ++ClientIndex)
        {
            auto* World = ck::auto_test::net::Get_ClientWorld(ClientIndex);
            if (!IsValid(World) || World->GetNetMode() != NM_Client)
            { return false; }
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                auto* Actor = *It;
                if (!IsValid(Actor) || (Actor->GetClass() != Run.RacingClass &&
                    Actor->GetClass() != Run.GenericClass)) { continue; }
                int32 Count = INDEX_NONE;
                if (!Int(Actor, TEXT("Get_SourceConstructedCount"), Count) || Count != 0)
                { return false; }
            }
        }
        return true;
    }

    bool GenericVisual(AActor* Actor, int32& OutTransitions,
        FString& OutStateText, bool& OutPaused)
    {
        OutTransitions = INDEX_NONE; OutStateText.Reset(); OutPaused = false;
        if (!IsValid(Actor)) { return false; }
        auto* Transitions = Field<FIntProperty>(Actor->GetClass(), TEXT("TransitionCount"));
        auto* Paused = Field<FBoolProperty>(Actor->GetClass(), TEXT("IsPaused"));
        auto* StateText = Field<FObjectProperty>(Actor->GetClass(), TEXT("StateText"));
        if (!Transitions || !Paused || !StateText) { return false; }
        auto* Text = Cast<UTextRenderComponent>(
            StateText->GetObjectPropertyValue_InContainer(Actor));
        if (!IsValid(Text) || Text->GetOwner() != Actor) { return false; }
        OutTransitions = Transitions->GetPropertyValue_InContainer(Actor);
        OutPaused = Paused->GetPropertyValue_InContainer(Actor);
        OutStateText = Text->Text.ToString();
        return true;
    }

    bool ReadText(AActor* Actor, const TCHAR* PropertyName, FString& Out)
    {
        Out.Reset();
        if (!IsValid(Actor)) { return false; }
        auto* Property = Field<FObjectProperty>(Actor->GetClass(), PropertyName);
        auto* Text = Property ? Cast<UTextRenderComponent>(
            Property->GetObjectPropertyValue_InContainer(Actor)) : nullptr;
        if (!IsValid(Text) || Text->GetOwner() != Actor) { return false; }
        Out = Text->Text.ToString();
        return true;
    }

    bool HasNoSmHandle(AActor* Actor)
    {
        if (!IsValid(Actor)) { return false; }
        auto* Property = Field<FStructProperty>(Actor->GetClass(), TEXT("SmHandle"));
        if (!Property || Property->Struct != FCk_Handle_StateMachine::StaticStruct())
        { return false; }
        const auto& Handle = *Property->ContainerPtrToValuePtr<FCk_Handle_StateMachine>(Actor);
        return !ck::IsValid(Handle);
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStateMachineGym_SourceClosure,
    "Ck.StateMachine.Gym.SourceClosure.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineGym_SourceClosure::RunTest(const FString& Parameters)
{
    using namespace ck_sm_gym_source_closure_test;
    const auto Run = MakeShared<FRun>();
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Run->Dedicated))
    { AddError(TEXT("unsupported State Machine source closure topology")); return false; }
    Run->RacingClass = FSoftClassPath(RacingPath).TryLoadClass<AActor>();
    Run->GenericClass = FSoftClassPath(GenericPath).TryLoadClass<AActor>();
    if (!TestTrue(TEXT("real State Machine source classes and reflected closure API resolve"),
        Run->RacingClass && Run->GenericClass &&
        Run->RacingClass->FindFunctionByName(TEXT("Request_CloseSource")) &&
        Run->RacingClass->FindFunctionByName(TEXT("Get_SourceClosed")) &&
        Run->RacingClass->FindFunctionByName(TEXT("Get_SourceRoots")) &&
        Run->RacingClass->FindFunctionByName(TEXT("Get_SourceConstructedCount")) &&
        Run->GenericClass->FindFunctionByName(TEXT("Request_CloseSource")) &&
        Run->GenericClass->FindFunctionByName(TEXT("Get_SourceRoots"))))
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(Server) || Server->GetNetMode() !=
                (Run->Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                !Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                    TryGet_TransientEntityForWorld(*Server))) { return false; }
            for (int32 I = 0; I != 2; ++I)
            {
                auto* Client = ck::auto_test::net::Get_ClientWorld(I);
                if (!IsValid(Client) || Client->GetNetMode() != NM_Client ||
                    !Allocated(UCk_Utils_EcsWorld_Subsystem_UE::
                        TryGet_TransientEntityForWorld(*Client))) { return false; }
            }
            return true;
        }), 60.0, TEXT("authority and two client ECS worlds ready")));

    // FinishSpawning runs the actual authored BeginPlay. The source is then closed
    // in the same server action, before a deferred ECS construction processor tick.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!IsValid(World)) { AddError(TEXT("source server unavailable")); return; }
            auto* Actor = World->SpawnActorDeferred<AActor>(
                Run->RacingClass, FTransform::Identity);
            if (!TestTrue(TEXT("real Racing source deferred actor allocated"), IsValid(Actor)))
            { return; }
            Run->Racing = Actor;
            Actor->FinishSpawning(FTransform::Identity);
            TArray<FCk_Handle> RootsBefore;
            int32 Constructed = INDEX_NONE;
            const bool Receipt = ExactRoots(Actor, World, 1, RootsBefore) &&
                Int(Actor, TEXT("Get_SourceConstructedCount"), Constructed) &&
                Constructed == 0;
            TestTrue(TEXT("real Racing source pending target retained before deferred construction"), Receipt);
            if (!Receipt) { return; }
            Run->RacingRoot = RootsBefore[0];
            bool Closed = false;
            Run->RacingClosedSameAction = Close(Actor) &&
                Bool(Actor, TEXT("Get_SourceClosed"), Closed) && Closed;
            Run->RacingClosedAt = FPlatformTime::Seconds();
            const bool TextCaptured = ReadText(Actor, TEXT("ResultText"),
                Run->RacingResultTextAtClose);
            TestTrue(TEXT("pending Racing source closes in the same action as FinishSpawning"),
                Run->RacingClosedSameAction && TextCaptured && HasNoSmHandle(Actor));
            TArray<FCk_Handle> RootsAfter;
            TestTrue(TEXT("pending target identity retained after source closure"),
                Roots(Actor, RootsAfter) && RootsAfter.Num() == 1 &&
                RootsAfter[0] == Run->RacingRoot);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return Run->RacingClosedSameAction &&
                FPlatformTime::Seconds() - Run->RacingClosedAt >= 0.9 &&
                !Allocated(Run->RacingRoot);
        }), 15.0, TEXT("actual pending Racing source target physically deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            auto* Actor = Run->Racing.Get();
            int32 Count = INDEX_NONE;
            bool Closed = false;
            auto* A = Actor ? Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestA")) : nullptr;
            auto* B = Actor ? Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestB")) : nullptr;
            FString ResultText;
            TestTrue(TEXT("closed Racing source has no construction, counter or callback mutation"),
                IsValid(Actor) &&
                Int(Actor, TEXT("Get_SourceConstructedCount"), Count) && Count == 0 &&
                Bool(Actor, TEXT("Get_SourceClosed"), Closed) && Closed && A && B &&
                A->GetPropertyValue_InContainer(Actor) == 0 &&
                B->GetPropertyValue_InContainer(Actor) == 0 &&
                HasNoSmHandle(Actor) &&
                ReadText(Actor, TEXT("ResultText"), ResultText) &&
                ResultText == Run->RacingResultTextAtClose &&
                ClientSourceConstructorsRemainIdle(*Run));
            if (IsValid(Actor))
            { TestTrue(TEXT("exact Racing source actor destroy accepted"), Actor->Destroy()); }
        })));

    // A client-local spawn reports HasAuthority() in Unreal, yet must never
    // become an ECS source. Destroy it before testing the replicated source.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            TestTrue(TEXT("client 0 locally authoritative Racing actor rejects source construction"),
                ClientLocalRacingSpawnClosesWithoutSource(World, Run->RacingClass));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            TestTrue(TEXT("client 1 locally authoritative Racing actor rejects source construction"),
                ClientLocalRacingSpawnClosesWithoutSource(World, Run->RacingClass));
        })));

    // Preserve the real default Racing verdict in this focused gate. This is a
    // second source actor, with no test-side timer/counter or callback injection.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!IsValid(World)) { AddError(TEXT("normal Racing server unavailable")); return; }
            auto* Actor = World->SpawnActorDeferred<AActor>(
                Run->RacingClass, FTransform(FVector(0, 150, 0)));
            if (!TestTrue(TEXT("real default Racing source allocated"), IsValid(Actor)))
            { return; }
            Run->RacingNormal = Actor;
            Actor->FinishSpawning(FTransform(FVector(0, 150, 0)));
            TArray<FCk_Handle> RootsNow;
            TestTrue(TEXT("normal Racing first-return root retained"),
                ExactRoots(Actor, World, 1, RootsNow));
            if (RootsNow.Num() == 1) { Run->RacingNormalRoot = RootsNow[0]; }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            auto* Actor = Run->RacingNormal.Get();
            if (!IsValid(Actor) || !Allocated(Run->RacingNormalRoot) ||
                !UCk_Utils_StateMachine_UE::Has(Run->RacingNormalRoot) ||
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) !=
                    Run->RacingNormalRoot) { return false; }
            int32 Count = INDEX_NONE;
            auto* A = Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestA"));
            auto* B = Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestB"));
            FString ResultText;
            return ReadText(Actor, TEXT("ResultText"), ResultText) &&
                ResultText == TEXT("PASS") &&
                Int(Actor, TEXT("Get_SourceConstructedCount"), Count) &&
                Count == 1 && A && B &&
                A->GetPropertyValue_InContainer(Actor) == 0 &&
                B->GetPropertyValue_InContainer(Actor) == 1 &&
                ExactNormalRacingClientReplicas(*Run);
        }), 15.0, TEXT("default Racing PASS and exact actor received by both clients")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            auto* Actor = Run->RacingNormal.Get();
            TArray<FCk_Handle> RootsNow;
            int32 Count = INDEX_NONE;
            auto* A = Actor ? Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestA")) : nullptr;
            auto* B = Actor ? Field<FIntProperty>(Actor->GetClass(), TEXT("Counter_DestB")) : nullptr;
            const bool Verdict = ExactRoots(Actor, World, 1, RootsNow) &&
                RootsNow[0] == Run->RacingNormalRoot &&
                Int(Actor, TEXT("Get_SourceConstructedCount"), Count) &&
                Count == 1 && A && B &&
                A->GetPropertyValue_InContainer(Actor) == 0 &&
                B->GetPropertyValue_InContainer(Actor) == 1;
            FString ResultText;
            const bool DisplayPass = ReadText(Actor, TEXT("ResultText"), ResultText) &&
                ResultText == TEXT("PASS");
            const bool ExactClientReplicas = ExactNormalRacingClientReplicas(*Run);
            TestTrue(TEXT("normal Racing PASS and exact Iris source actor received inertly by both clients"),
                Verdict && DisplayPass && ExactClientReplicas);
            if (!Verdict || !DisplayPass || !ExactClientReplicas) { return; }
            TestTrue(TEXT("normal Racing source closes after genuine verdict"), Close(Actor));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            return !Allocated(Run->RacingNormalRoot) &&
                SourceActorsInWorld(ck::auto_test::net::Get_ServerWorld(),
                    Run->RacingClass) == 0;
        }), 15.0, TEXT("normal Racing ECS root deallocated and actor removed from world")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestTrue(TEXT("normal Racing source closes with root gone and no client source constructors"),
                !Allocated(Run->RacingNormalRoot) &&
                ClientSourceConstructorsRemainIdle(*Run));
        })));

    // The second real actor reaches a live actor-linked SM before closure.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            if (!IsValid(World)) { AddError(TEXT("generic source server unavailable")); return; }
            auto* Actor = World->SpawnActorDeferred<AActor>(
                Run->GenericClass, FTransform(FVector(150, 0, 0)));
            if (!TestTrue(TEXT("real generic source deferred actor allocated"), IsValid(Actor)))
            { return; }
            Run->Generic = Actor;
            auto* EnablePause = Field<FBoolProperty>(Actor->GetClass(), TEXT("EnablePauseResume"));
            auto* PauseAfter = Field<FIntProperty>(Actor->GetClass(), TEXT("PauseAfterTransitions"));
            auto* PauseDuration = Field<FFloatProperty>(Actor->GetClass(), TEXT("PauseDuration"));
            if (!TestTrue(TEXT("generic authored pause controls reflect"),
                EnablePause && PauseAfter && PauseDuration)) { return; }
            EnablePause->SetPropertyValue_InContainer(Actor, true);
            PauseAfter->SetPropertyValue_InContainer(Actor, 1);
            PauseDuration->SetPropertyValue_InContainer(Actor, 5.0f);
            Actor->FinishSpawning(FTransform(FVector(150, 0, 0)));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            auto* Actor = Run->Generic.Get();
            auto* World = ck::auto_test::net::Get_ServerWorld();
            TArray<FCk_Handle> RootsNow;
            int32 Count = INDEX_NONE;
            int32 Transitions = INDEX_NONE;
            FString Text; bool Paused = false;
            if (!ExactRoots(Actor, World, 1, RootsNow) ||
                !Int(Actor, TEXT("Get_SourceConstructedCount"), Count) || Count != 1 ||
                !UCk_Utils_StateMachine_UE::Has(RootsNow[0]) ||
                UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Actor) != RootsNow[0] ||
                !GenericVisual(Actor, Transitions, Text, Paused) || !Paused ||
                Transitions < 1) { return false; }
            TArray<FCk_Handle> Forest;
            FString Why;
            ++Run->GenericCensusAttempts;
            const bool Complete = ck_selection_gym_census::TryCensusAllocatedBank(
                World, RootsNow[0], {}, Forest, Why);
            if (!Complete)
            { if (Run->FirstCensusReason.IsEmpty()) { Run->FirstCensusReason = Why; } return false; }
            Run->GenericRoot = RootsNow[0];
            Run->GenericForest = MoveTemp(Forest);
            return Run->GenericForest.Contains(Run->GenericRoot);
        }), 15.0, TEXT("actual generic SM, pause and complete owner forest settled")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World) {
            auto* Actor = Run->Generic.Get();
            int32 Count = INDEX_NONE;
            int32 Transitions = INDEX_NONE;
            FString StateText; bool Paused = false;
            TArray<FCk_Handle> RootsNow;
            const bool Ready = IsValid(Actor) && Run->GenericForest.Num() > 0 &&
                ExactRoots(Actor, World, 1, RootsNow) &&
                RootsNow[0] == Run->GenericRoot &&
                Int(Actor, TEXT("Get_SourceConstructedCount"), Count) && Count == 1 &&
                GenericVisual(Actor, Transitions, StateText, Paused) && Paused;
            TestTrue(*FString::Printf(TEXT("constructed source ready; census attempts=%d first=%s"),
                Run->GenericCensusAttempts, *Run->FirstCensusReason), Ready);
            if (!Ready) { return; }
            Run->GenericClosed = Close(Actor);
            Run->GenericClosedAt = FPlatformTime::Seconds();
            // Closing an accepted WithActor root can destroy its actor via the
            // OwningActor processor; do not inspect the actor after Close.
            TestTrue(TEXT("constructed source close request accepted for exact retained root"),
                Run->GenericClosed);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            if (!Run->GenericClosed || Run->GenericForest.IsEmpty() ||
                FPlatformTime::Seconds() - Run->GenericClosedAt < 5.2)
            { return false; }
            for (const auto& H : Run->GenericForest)
            { if (Allocated(H)) { return false; } }
            return SourceActorsInWorld(ck::auto_test::net::Get_ServerWorld(),
                Run->GenericClass) == 0;
        }), 15.0, TEXT("full SM forest deallocated and actor removed past original resume timer")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*) {
            TestTrue(TEXT("no old generic source is runnable after original resume deadline"),
                Run->GenericClosed && !Allocated(Run->GenericRoot) &&
                ClientSourceConstructorsRemainIdle(*Run));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World)) { return false; }
            return SourceActorsInWorld(World, Run->RacingClass) == 0 &&
                SourceActorsInWorld(World, Run->GenericClass) == 0 &&
                !Allocated(Run->RacingRoot) &&
                !Allocated(Run->RacingNormalRoot) && !Allocated(Run->GenericRoot);
        }), 15.0, TEXT("all exact source actors removed and retained roots deallocated")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

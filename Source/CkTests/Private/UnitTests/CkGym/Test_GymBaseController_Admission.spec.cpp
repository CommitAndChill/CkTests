#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_basepc_admission_failure
{
    using namespace ck::auto_test::net;

    auto ReadOwner(AGameModeBase* Mode, FCk_Handle& Out) -> bool
    {
        Out = {};
        if (!IsValid(Mode)) { return false; }
        UFunction* Fn = Mode->FindFunction(TEXT("Get_AdmissionOwner"));
        if (!Fn || Fn->NumParms != 1 || !Fn->HasAnyFunctionFlags(FUNC_BlueprintPure)) { return false; }
        FStructProperty* Return = CastField<FStructProperty>(Fn->GetReturnProperty());
        if (!Return || Return->Struct != FCk_Handle::StaticStruct()) { return false; }
        FStructOnScope Args(Fn);
        Mode->ProcessEvent(Fn, Args.GetStructMemory());
        Out = *Return->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory());
        return true;
    }

    auto InvokeHandleProbe(AGameModeBase* Mode, const TCHAR* Name,
        const TCHAR* ArgumentName, const FCk_Handle& Input, FCk_Handle& Out) -> bool
    {
        Out = {};
        if (!IsValid(Mode)) { return false; }
        UFunction* Fn = Mode->FindFunction(FName(Name));
        FStructProperty* Argument = Fn ? FindFProperty<FStructProperty>(Fn, ArgumentName) : nullptr;
        FStructProperty* Return = Fn ? CastField<FStructProperty>(Fn->GetReturnProperty()) : nullptr;
        if (!Fn || Fn->NumParms != 2 || !Argument || !Return ||
            Argument->Struct != FCk_Handle::StaticStruct() ||
            Return->Struct != FCk_Handle::StaticStruct()) { return false; }
        FStructOnScope Args(Fn);
        *Argument->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory()) = Input;
        Mode->ProcessEvent(Fn, Args.GetStructMemory());
        Out = *Return->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory());
        return true;
    }

    auto InvokeBoolProbe(AGameModeBase* Mode, const TCHAR* Name,
        const TCHAR* ArgumentName, const FCk_Handle& Input, bool& Out) -> bool
    {
        Out = false;
        if (!IsValid(Mode)) { return false; }
        UFunction* Fn = Mode->FindFunction(FName(Name));
        FStructProperty* Argument = Fn ? FindFProperty<FStructProperty>(Fn, ArgumentName) : nullptr;
        FBoolProperty* Return = Fn ? CastField<FBoolProperty>(Fn->GetReturnProperty()) : nullptr;
        if (!Fn || Fn->NumParms != 2 || !Argument || !Return ||
            Argument->Struct != FCk_Handle::StaticStruct()) { return false; }
        FStructOnScope Args(Fn);
        *Argument->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory()) = Input;
        Mode->ProcessEvent(Fn, Args.GetStructMemory());
        Out = Return->GetPropertyValue_InContainer(Args.GetStructMemory());
        return true;
    }

    auto EnteredOwnerCreation(APlayerController* PC, bool& Out) -> bool
    {
        Out = false;
        if (!IsValid(PC)) { return false; }
        UFunction* Fn = PC->FindFunction(TEXT("Get_EnteredOwnerCreation"));
        if (!Fn || Fn->NumParms != 1 || !Fn->HasAnyFunctionFlags(FUNC_BlueprintPure)) { return false; }
        FBoolProperty* Return = CastField<FBoolProperty>(Fn->GetReturnProperty());
        if (!Return) { return false; }
        FStructOnScope Args(Fn);
        PC->ProcessEvent(Fn, Args.GetStructMemory());
        Out = Return->GetPropertyValue_InContainer(Args.GetStructMemory());
        return true;
    }

    auto ReadControllerCount(APlayerController* PC, const TCHAR* GetterName, int32& Out) -> bool
    {
        Out = 0;
        if (!IsValid(PC)) { return false; }
        UFunction* Fn = PC->FindFunction(FName(GetterName));
        if (!Fn || Fn->NumParms != 1 || !Fn->HasAnyFunctionFlags(FUNC_BlueprintPure)) { return false; }
        FIntProperty* Return = CastField<FIntProperty>(Fn->GetReturnProperty());
        if (!Return) { return false; }
        FStructOnScope Args(Fn);
        PC->ProcessEvent(Fn, Args.GetStructMemory());
        Out = Return->GetPropertyValue_InContainer(Args.GetStructMemory());
        return true;
    }

    class FRejectWorldOwner : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
    public:
        explicit FRejectWorldOwner(FAutomationTestBase* InTest) : Test(InTest) {}

        bool Update() override
        {
            if (Get_HasSessionFailed()) { return true; }
            UWorld* Server = Get_ServerWorld();
            if (!IsValid(Server) || Server->bIsTearingDown || Get_NumClientWorlds() != 2)
            { Test->AddError(TEXT("World-owner rejection fixture lost its real three-world PIE context"));
              FailSession(TEXT("World-owner rejection context lost")); return true; }
            AGameModeBase* Mode = Server->GetAuthGameMode();
            const FCk_Handle WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Server);
            if (!IsValid(Mode) || !ck::IsValid(WorldRoot))
            { Test->AddError(TEXT("World-owner rejection lacks its real authority mode/root"));
              FailSession(TEXT("World-owner rejection authority missing")); return true; }
            TArray<FCk_Handle> BeforeChildren;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    WorldRoot, WorldRoot, BeforeChildren))
            { Test->AddError(TEXT("Pre-attempt world allocated-child census failed"));
              FailSession(TEXT("World-owner pre-census failed")); return true; }
            bool bRejectedWithoutMutation = false;
            const bool bInvoked = InvokeBoolProbe(Mode, TEXT("Request_RejectWorldOwner"),
                TEXT("InWorld"), WorldRoot, bRejectedWithoutMutation);
            TArray<FCk_Handle> AfterChildren;
            const bool bAfterCensus =
                UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    WorldRoot, WorldRoot, AfterChildren);
            bool bSameAllocatedChildren = bAfterCensus && BeforeChildren.Num() == AfterChildren.Num();
            for (const FCk_Handle& Child : BeforeChildren)
            { bSameAllocatedChildren &= AfterChildren.Contains(Child); }
            Test->TestTrue(TEXT("real world root is rejected as station admission owner without changing reservation fields"),
                bInvoked && bRejectedWithoutMutation);
            Test->TestTrue(TEXT("rejected world-owner commit allocates no direct world child"),
                bSameAllocatedChildren);
            return true;
        }
    };

    class FExercise : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        UClass* ProbeClass = nullptr;
        double Deadline = -1.0;
        double ObservationDeadline = -1.0;
        FCk_Handle FirstOwner;
        TArray<FCk_Handle> FirstChildren;
        FCk_Handle SecondOwner;
        FCk_Handle SecondChild;
        bool bDestroyedFirstController = false;
        bool bSpawnedProbes = false;
        TWeakObjectPtr<APlayerController> FirstController;
        TWeakObjectPtr<APlayerController> OtherController;
        int32 OtherCallsBefore = 0;
        int32 OtherReadsBefore = 0;
        bool bOtherAttemptedOwnerCreation = false;
        bool bOtherRetried = false;
        bool bLateSpawned = false;
        bool bLateRejectedObserved = false;
        FCk_Handle LateTarget;
        double LateDeadline = -1.0;

        auto Reject(const FString& Why) -> bool
        {
            Test->AddError(Why);
            FailSession(Why);
            return true;
        }

    public:
        FExercise(FAutomationTestBase* InTest, UClass* InProbeClass)
            : Test(InTest), ProbeClass(InProbeClass) {}

        bool Update() override
        {
            if (Get_HasSessionFailed()) { return true; }
            if (Deadline < 0.0) { Deadline = FPlatformTime::Seconds() + 30.0; }
            UWorld* Server = Get_ServerWorld();
            if (!IsValid(Server) || Server->bIsTearingDown) { return Reject(TEXT("Server world left the real admission fixture")); }
            AGameModeBase* Mode = Server->GetAuthGameMode();
            if (!IsValid(Mode)) { return Reject(TEXT("Real admission game mode is unavailable")); }
            if (!bSpawnedProbes)
            {
                if (!IsValid(ProbeClass) || !ProbeClass->IsChildOf(APlayerController::StaticClass()))
                { return Reject(TEXT("Unconnected probe controller class is unavailable")); }
                FActorSpawnParameters Spawn;
                Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                APlayerController* First = Server->SpawnActor<APlayerController>(ProbeClass, Spawn);
                APlayerController* Other = Server->SpawnActor<APlayerController>(ProbeClass, Spawn);
                if (!IsValid(First) || !IsValid(Other) || First == Other ||
                    First->Player != nullptr || Other->Player != nullptr)
                { return Reject(TEXT("Two test-owned unconnected BasePC instances were not spawned")); }
                FirstController = First;
                OtherController = Other;
                bSpawnedProbes = true;
                return false;
            }
            const FCk_Handle WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*Server);
            if (!ck::IsValid(WorldRoot)) { return Reject(TEXT("Actual ECS world root is unavailable")); }

            FCk_Handle Current;
            if (!ReadOwner(Mode, Current)) { return Reject(TEXT("Admission getter schema rejected")); }
            if (!bDestroyedFirstController)
            {
                APlayerController* First = FirstController.Get();
                APlayerController* Other = OtherController.Get();
                if (!IsValid(First) || !IsValid(Other) || First->GetWorld() != Server || Other->GetWorld() != Server)
                { return Reject(TEXT("Test-owned controllers left the authority world before failure injection")); }
                bool bFirstEntered = false;
                bool bOtherEntered = false;
                if (!EnteredOwnerCreation(First, bFirstEntered) ||
                    !EnteredOwnerCreation(Other, bOtherEntered))
                { return Reject(TEXT("Controller probe getter schema rejected")); }
                // ECS construction can visit either newly spawned controller
                // first. Select the one that actually claimed this admission.
                if (!bFirstEntered && bOtherEntered)
                {
                    FirstController = Other;
                    OtherController = First;
                    APlayerController* PreviousFirst = First;
                    First = Other;
                    Other = PreviousFirst;
                    bFirstEntered = true;
                    bOtherEntered = false;
                }
                TArray<FCk_Handle> Children;
                const auto TaggedGood = UCk_Utils_EntityTag_UE::ForEach_Entity(
                    WorldRoot, FName(TEXT("Test.BasePC.Admission.Good")));
                if (bFirstEntered && !bOtherEntered &&
                    UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Current) &&
                    UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Current, WorldRoot, Children) &&
                    Children.Num() >= 2 && TaggedGood.Num() == 1 && Children.Contains(TaggedGood[0]))
                {
                    for (const FCk_Handle& Child : Children)
                    {
                        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child))
                        { return Reject(TEXT("Station spawn child was not an allocated identity")); }
                    }
                    FirstOwner = Current;
                    FirstChildren = Children;
                    if (!ReadControllerCount(Other, TEXT("Get_OwnerCreationCalls"), OtherCallsBefore) ||
                        !ReadControllerCount(Other, TEXT("Get_RequiredStationReads"), OtherReadsBefore))
                    { return Reject(TEXT("Surviving controller attempt getter schema rejected")); }
                    if (!First->Destroy())
                    { return Reject(TEXT("Actual first controller EndPlay could not be triggered")); }
                    bDestroyedFirstController = true;
                    ObservationDeadline = FPlatformTime::Seconds() + 6.0;
                    return false;
                }
                if (FPlatformTime::Seconds() > Deadline)
                { return Reject(TEXT("Partial startup did not reach two allocated children and one constructed tagged station")); }
                return false;
            }

            APlayerController* Other = OtherController.Get();
            if (!IsValid(Other) || Other->GetWorld() != Server || !IsValid(Mode) ||
                Get_NumClientWorlds() != 2)
            { return Reject(TEXT("Surviving controller or authority context was lost after first EndPlay")); }
            int32 OtherCallsNow = 0;
            int32 OtherReadsNow = 0;
            if (!ReadControllerCount(Other, TEXT("Get_OwnerCreationCalls"), OtherCallsNow) ||
                !ReadControllerCount(Other, TEXT("Get_RequiredStationReads"), OtherReadsNow))
            { return Reject(TEXT("Surviving controller attempt getter became unavailable")); }
            bOtherAttemptedOwnerCreation |= OtherCallsNow > OtherCallsBefore;
            bOtherRetried |= OtherReadsNow > OtherReadsBefore;

            if (Current != FCk_Handle() && Current != FirstOwner &&
                UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Current))
            {
                TArray<FCk_Handle> Children;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Current, WorldRoot, Children))
                { return Reject(TEXT("Second admission owner failed allocated child census")); }
                for (const FCk_Handle& Child : Children)
                {
                    if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child))
                    { SecondOwner = Current; SecondChild = Child; break; }
                }
            }
            if (!bLateSpawned &&
                (SecondOwner != FCk_Handle() || FPlatformTime::Seconds() > ObservationDeadline))
            {
                const bool bFirstRetained = Current == FirstOwner &&
                    UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FirstOwner);
                bool bOriginalChildrenRetained = FirstChildren.Num() >= 2;
                for (const FCk_Handle& Child : FirstChildren)
                { bOriginalChildrenRetained &= UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child); }
                Test->TestTrue(TEXT("failed first startup retains its raw admission identity until world teardown"), bFirstRetained);
                Test->TestTrue(TEXT("failed first startup retains its real station creator identities"), bOriginalChildrenRetained);
                Test->TestTrue(TEXT("surviving controller retried actual station admission after first EndPlay"), bOtherRetried);
                Test->TestFalse(TEXT("surviving controller must not reach owner allocation after unproven first EndPlay"), bOtherAttemptedOwnerCreation);
                Test->TestFalse(TEXT("a second controller must not allocate a new station admission owner and child"),
                    SecondOwner != FCk_Handle() && SecondChild != FCk_Handle());
                if (!bFirstRetained || !bOriginalChildrenRetained || !bOtherRetried ||
                    bOtherAttemptedOwnerCreation ||
                    (SecondOwner != FCk_Handle() && SecondChild != FCk_Handle()))
                { return true; }
                if (!InvokeHandleProbe(Mode, TEXT("Request_SpawnLateStation"),
                        TEXT("InOwner"), FirstOwner, LateTarget) ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(LateTarget))
                { return Reject(TEXT("Real late station request did not retain an allocated target")); }
                TArray<FCk_Handle> OwnerChildren;
                if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                        FirstOwner, WorldRoot, OwnerChildren) || !OwnerChildren.Contains(LateTarget))
                { return Reject(TEXT("Late station is not a direct allocated child of the retained failed owner")); }
                bLateSpawned = true;
                LateDeadline = FPlatformTime::Seconds() + 15.0;
                return false;
            }
            if (bLateSpawned)
            {
                if (Current != FirstOwner ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(FirstOwner) ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(LateTarget))
                { return Reject(TEXT("Late station or retained admission identity was lost")); }
                bool bRejected = false;
                if (!InvokeBoolProbe(Mode, TEXT("Get_IsLateStationRejected"),
                        TEXT("InStation"), LateTarget, bRejected))
                { return Reject(TEXT("Late station ledger getter schema rejected")); }
                if (bLateRejectedObserved && !bRejected)
                { return Reject(TEXT("Late station lost its previously observed closed creator ledger")); }
                if (bRejected)
                {
                    const auto PublishedLate = UCk_Utils_EntityTag_UE::ForEach_Entity(
                        WorldRoot, FName(TEXT("Test.BasePC.Admission.Late")));
                    if (PublishedLate.Contains(LateTarget))
                    { return Reject(TEXT("Late rejected station published its identity tag")); }
                    if (!bLateRejectedObserved)
                    {
                        Test->TestTrue(TEXT("late real station is closed and broken before any baseline creator returns"), bRejected);
                        Test->TestFalse(TEXT("late rejected station never publishes its identity tag"),
                            PublishedLate.Contains(LateTarget));
                        bLateRejectedObserved = true;
                    }
                    TArray<FCk_Handle> LateChildren;
                    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                            LateTarget, WorldRoot, LateChildren))
                    { return Reject(TEXT("Late station allocated-child census was invalid")); }
                    if (LateChildren.IsEmpty())
                    {
                        Test->TestTrue(TEXT("late real station has no allocated baseline descendants after request-carrier teardown"), LateChildren.IsEmpty());
                        return true;
                    }
                    if (FPlatformTime::Seconds() > LateDeadline)
                    {
                        return Reject(FString::Printf(TEXT("Late station retained %d allocated direct children after request-carrier teardown deadline"),
                            LateChildren.Num()));
                    }
                    return false;
                }
                if (FPlatformTime::Seconds() > LateDeadline)
                { return Reject(TEXT("Late station did not report closed, broken, empty creator ledger")); }
            }
            return false;
        }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkGymBaseControllerAdmissionFailure,
    "Ck.Gym.BaseController.AdmissionFailure.Production",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkGymBaseControllerAdmissionFailure::RunTest(const FString& Parameters)
{
    using namespace ck::auto_test::net;
    bool bDedicated = false;
    if (!TryGet_DedicatedServer(Parameters, bDedicated))
    { AddError(TEXT("Explicit network topology is invalid")); return false; }
    UClass* Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_GymAdmissionFailureProbe_Mode"))
        .TryLoadClass<AGameModeBase>();
    if (!TestNotNull(TEXT("actual BasePC admission test GameMode class"), Mode)) { return false; }
    UClass* Probe = FSoftClassPath(TEXT("/Script/Angelscript.Ck_GymAdmissionFailureProbe_PC"))
        .TryLoadClass<APlayerController>();
    if (!TestNotNull(TEXT("actual unconnected BasePC probe class"), Probe)) { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters, Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(ck_basepc_admission_failure::FExercise(this, Probe));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}


CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkGymBaseControllerRejectWorldOwner,
    "Ck.Gym.BaseController.AdmissionFailure.RejectWorldOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkGymBaseControllerRejectWorldOwner::RunTest(const FString& Parameters)
{
    using namespace ck::auto_test::net;
    bool bDedicated = false;
    if (!TryGet_DedicatedServer(Parameters, bDedicated))
    { AddError(TEXT("Explicit network topology is invalid")); return false; }
    UClass* Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_GymAdmissionFailureProbe_Mode"))
        .TryLoadClass<AGameModeBase>();
    if (!TestNotNull(TEXT("world-owner rejection GameMode class"), Mode)) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters, Mode));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(ck_basepc_admission_failure::FRejectWorldOwner(this));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

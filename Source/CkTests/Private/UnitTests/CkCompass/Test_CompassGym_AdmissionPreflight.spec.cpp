#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkGym_AdmissionRuntime.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "Engine/World.h"
#include "StructUtils/InstancedStruct.h"

namespace ck_compass_admission_preflight_test
{
    struct FRun
    {
        TWeakObjectPtr<UWorld> World;
        FCk_Handle Root;
        FDelegateHandle TeardownHandle;
        bool SawServerTeardown = false;
        bool OtherGetterPrerequisitesAtTeardown = false;
        bool WorldCleaningAtTeardown = false;
        bool CheckedReadRejectedTeardown = false;

        ~FRun()
        {
            if (TeardownHandle.IsValid())
            { FWorldDelegates::OnWorldBeginTearDown.Remove(TeardownHandle); }
        }
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCompassGym_AdmissionAvailability,
    "Ck.Compass.SourceMembership.AdmissionAvailability.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCompassGym_AdmissionAvailability::RunTest(const FString& Parameters)
{
    using namespace ck::auto_test::net;
    using namespace ck_compass_admission_preflight_test;
    bool Dedicated = false;
    if (!TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("Unsupported admission availability topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("actual server game world exists before admission probe"),
                IsValid(World) && World->IsGameWorld() && !World->bIsTearingDown))
            { return; }
            Run->World = World;
            Run->Root = UCk_Utils_EcsWorld_Subsystem_UE::
                TryGet_TransientEntityForWorld(*World);
            const auto* Type = FCkGym_StationAdmission::StaticStruct();
            if (!TestTrue(TEXT("actual exact server transient root starts without admission slot"),
                ck::IsValid(Run->Root) &&
                !UCk_Utils_DynamicFragment_UE::Has_Fragment(Run->Root, Type)))
            { return; }

            bool Closed = false;
            const bool DefaultAccepted = UCkGym_AdmissionRuntime_UE::
                TryGet_IsClosedUnproven({}, Closed);
            TestTrue(TEXT("default handle rejects and initializes fail-closed output"),
                !DefaultAccepted && Closed);
            Closed = true;
            const bool AbsentAccepted = UCkGym_AdmissionRuntime_UE::
                TryGet_IsClosedUnproven(Run->Root, Closed);
            if (!TestTrue(TEXT("absent slot on real root is readable and open"),
                AbsentAccepted && !Closed)) { return; }

            auto MutableRoot = Run->Root;
            auto* Storage = UCk_Utils_DynamicFragment_UE::
                TryAddOrGet_Fragment_TypeUnsafe(MutableRoot, Type);
            if (!TestTrue(TEXT("real native admission slot acquired"),
                Storage && Storage->GetScriptStruct() == Type)) { return; }
            Storage->InitializeAs(TBaseStructure<FVector>::Get());
            Closed = false;
            const bool WrongAccepted = UCkGym_AdmissionRuntime_UE::
                TryGet_IsClosedUnproven(Run->Root, Closed);
            const bool WrongPayloadUnchanged =
                Storage->GetScriptStruct() == TBaseStructure<FVector>::Get();
            // Restore immediately, before any scheduler tick or world teardown.
            Storage->InitializeAs(Type);
            if (!TestTrue(TEXT("wrong cached payload rejects without changing it or exposing open admission"),
                !WrongAccepted && Closed && WrongPayloadUnchanged)) { return; }

            Closed = true;
            const bool TypedOpen = UCkGym_AdmissionRuntime_UE::
                TryGet_IsClosedUnproven(Run->Root, Closed);
            if (!TestTrue(TEXT("restored typed slot reads open"), TypedOpen && !Closed))
            { return; }
            const bool CloseAccepted = UCkGym_AdmissionRuntime_UE::
                TryCloseWorldUnproven(Run->Root);
            Closed = false;
            const bool TypedClosed = UCkGym_AdmissionRuntime_UE::
                TryGet_IsClosedUnproven(Run->Root, Closed);
            TestTrue(TEXT("typed closed slot is readable through checked native path"),
                CloseAccepted && TypedClosed && Closed);

            TWeakPtr<FRun> WeakRun = Run;
            Run->TeardownHandle = FWorldDelegates::OnWorldBeginTearDown.AddLambda(
                [WeakRun](UWorld* TearingWorld)
                {
                    const auto State = WeakRun.Pin();
                    if (!State.IsValid() || TearingWorld != State->World.Get())
                    { return; }
                    State->SawServerTeardown = true;
                    State->WorldCleaningAtTeardown = IsValid(TearingWorld) &&
                        TearingWorld->IsBeingCleanedUp();
                    const bool WorldReady = IsInGameThread() && IsValid(TearingWorld) &&
                        TearingWorld->IsGameWorld() &&
                        TearingWorld->bIsTearingDown &&
                        !TearingWorld->HasAnyFlags(
                            RF_BeginDestroyed | RF_FinishDestroyed) &&
                        !State->WorldCleaningAtTeardown;
                    const auto Root = State->Root;
                    const bool RootReady = WorldReady && ck::IsValid(Root) &&
                        !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                            Root, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                        Root.Has<TWeakObjectPtr<UWorld>>() &&
                        Root.Get<TWeakObjectPtr<UWorld>>().Get() == TearingWorld &&
                        UCk_Utils_EcsWorld_Subsystem_UE::
                            TryGet_TransientEntityForWorld(*TearingWorld) == Root;
                    const auto* Type = FCkGym_StationAdmission::StaticStruct();
                    const bool HasSlot = RootReady &&
                        UCk_Utils_DynamicFragment_UE::Has_Fragment(Root, Type);
                    const auto* Storage = HasSlot ? UCk_Utils_DynamicFragment_UE::
                        TryGet_Fragment_TypeUnsafe(Root, Type) : nullptr;
                    const auto* Slot = Storage && Storage->GetScriptStruct() == Type
                        ? Storage->GetPtr<FCkGym_StationAdmission>() : nullptr;
                    State->OtherGetterPrerequisitesAtTeardown =
                        RootReady && Slot && Slot->ClosedUnproven;
                    bool ClosedOnFailure = false;
                    const bool Accepted = UCkGym_AdmissionRuntime_UE::
                        TryGet_IsClosedUnproven(State->Root, ClosedOnFailure);
                    State->CheckedReadRejectedTeardown = !Accepted && ClosedOnFailure;
                });
            TestTrue(TEXT("real world teardown callback registered"),
                Run->TeardownHandle.IsValid());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Run]()
        {
            if (Run->TeardownHandle.IsValid())
            {
                FWorldDelegates::OnWorldBeginTearDown.Remove(Run->TeardownHandle);
                Run->TeardownHandle.Reset();
            }
            return Run->SawServerTeardown &&
                Run->OtherGetterPrerequisitesAtTeardown &&
                !Run->WorldCleaningAtTeardown &&
                Run->CheckedReadRejectedTeardown;
        }), TEXT("actual server teardown had all other getter prerequisites and rejected checked admission read"), true));
    return true;
}

#endif

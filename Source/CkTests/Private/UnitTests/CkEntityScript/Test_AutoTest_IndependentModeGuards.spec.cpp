#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkAutoTest_Bridge.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "Engine/World.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace ck_autotest_independent_guards
{
    constexpr auto BodyClassPath = TEXT("/Script/Angelscript.Ck_AutoTest_Net_Float_Local_AddWorksOnBothWorlds");

    enum class ECase : uint8
    {
        ModeMismatch,
        SubjectBeforeAdmission,
        SubjectAfterAdmission
    };

    struct FPlaySettingsSnapshot
    {
        int32 Clients = 0;
        EPlayNetMode Mode = EPlayNetMode::PIE_Standalone;
        bool OneProcess = false;
        bool SeparateServer = false;

        FPlaySettingsSnapshot()
        {
            const auto* Settings = GetDefault<ULevelEditorPlaySettings>();
            Settings->GetPlayNumberOfClients(Clients);
            Settings->GetPlayNetMode(Mode);
            Settings->GetRunUnderOneProcess(OneProcess);
            SeparateServer = Settings->bLaunchSeparateServer;
        }

        auto MatchesCurrent() const -> bool
        {
            const auto Current = FPlaySettingsSnapshot{};
            return Clients == Current.Clients && Mode == Current.Mode &&
                OneProcess == Current.OneProcess && SeparateServer == Current.SeparateServer;
        }
    };

    auto HasSameDependents(const TArray<FCk_Handle>& A, const TArray<FCk_Handle>& B) -> bool
    {
        if (A.Num() != B.Num())
        { return false; }
        for (const auto& Handle : A)
        {
            if (NOT B.Contains(Handle))
            { return false; }
        }
        return true;
    }

    struct FLifetimeEvidence
    {
        TArray<TWeakObjectPtr<UWorld>> Worlds;
        TArray<FCk_Handle> Owners;
        TArray<FCk_Handle> Roots;
        TArray<FCk_Handle> Captured;
        FCk_Handle SubjectTarget;
        bool ExerciseCompleted = false;
        bool CaptureFailed = false;
        bool SubjectTransformPublished = false;
        FDelegateHandle TransformObserver;

        ~FLifetimeEvidence()
        { RemoveObserver(); }

        auto RemoveObserver() -> void
        {
            if (TransformObserver.IsValid())
            { UCk_Utils_Transform_UE::Get_OnAdded().Remove(TransformObserver); TransformObserver.Reset(); }
        }

        auto Capture(const FCk_Handle& Root, const FCk_Handle& Context) -> bool
        {
            if (NOT UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root))
            { return true; }
            TArray<FCk_Handle> Queue{Root};
            TArray<FCk_Handle> Seen;
            for (int32 Index = 0; Index < Queue.Num(); ++Index)
            {
                const auto Handle = Queue[Index];
                if (Seen.Contains(Handle)) { continue; }
                Seen.Add(Handle);
                if (NOT UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle)) { continue; }
                Captured.AddUnique(Handle);
                TArray<FCk_Handle> Children;
                if (NOT UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Handle, Context, Children))
                { CaptureFailed = true; return false; }
                Queue.Append(Children);
            }
            return true;
        }
    };

    class FObserveGuardRetirement final : public IAutomationLatentCommand
    {
    public:
        FObserveGuardRetirement(FAutomationTestBase* InTest, TSharedRef<FLifetimeEvidence> InEvidence, ECase InCase)
            : _Test(InTest), _Evidence(InEvidence), _Case(InCase) {}

        virtual bool Update() override
        {
            if (_Start < 0.0) { _Start = FPlatformTime::Seconds(); }
            // The production guard intentionally failed the session. That must never skip this tail.
            if (_Test == nullptr || NOT _Evidence->ExerciseCompleted)
            { return Fail(TEXT("independent guard exercise did not complete its lifetime evidence")); }
            if (_Evidence->Worlds.Num() != 2 || _Evidence->Owners.Num() != 2)
            { return Fail(TEXT("independent guard retirement lost its exact world contexts")); }
            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            if (Worlds.Num() != 2) { return Fail(TEXT("independent guard retirement lost PIE before observation")); }
            for (int32 Index = 0; Index < 2; ++Index)
            {
                auto* World = _Evidence->Worlds[Index].Get();
                if (NOT IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || Worlds[Index] != World ||
                    NOT UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(_Evidence->Owners[Index]))
                { return Fail(TEXT("independent guard retirement rejected a changed world/owner")); }
            }
            if (_Case != ECase::ModeMismatch)
            {
                if (_Evidence->SubjectTarget == FCk_Handle())
                { return Fail(TEXT("independent guard never captured the real Subject target")); }
                const auto Target = _Evidence->SubjectTarget;
                if (_Evidence->SubjectTransformPublished)
                { return Fail(TEXT("destroyed Subject target published Transform through its actual public OnAdded event")); }
                if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Target) &&
                    Target.Has_Any<ck::FFragment_EntityScript, ck::FFragment_Transform,
                        TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
                { return Fail(TEXT("destroyed Subject target published EntityScript/Transform/driver")); }
            }
            for (const auto& Root : _Evidence->Roots)
            {
                if (NOT UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Root)) { continue; }
                const auto ContextIndex = _Evidence->Owners.IndexOfByPredicate([&](const FCk_Handle& Owner)
                { return Owner.Get_RegistryView().Get_RegistryHandle() == Root.Get_RegistryView().Get_RegistryHandle(); });
                if (ContextIndex == INDEX_NONE || NOT _Evidence->Capture(Root, _Evidence->Owners[ContextIndex]))
                { return Fail(TEXT("independent guard retirement has an unproven allocated closure")); }
            }
            if (_Evidence->CaptureFailed) { return Fail(TEXT("independent guard lifetime capture failed")); }
            bool AllGone = true;
            for (const auto& Handle : _Evidence->Captured)
            { AllGone = AllGone && NOT UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }
            if (AllGone)
            {
                if (_Case == ECase::ModeMismatch)
                { _Test->TestTrue(TEXT("mode mismatch allocated no Subject/body closure"), _Evidence->Captured.IsEmpty()); }
                else
                { _Test->TestTrue(TEXT("queued Subject target and complete captured request/body closure are allocated-false"), AllGone); }
                _Test->TestEqual(TEXT("exact case-specific Subject and admitted-body root count"),
                    _Evidence->Roots.Num(), _Case == ECase::SubjectAfterAdmission ? 3 : (_Case == ECase::SubjectBeforeAdmission ? 1 : 0));
                _Evidence->RemoveObserver();
                return true;
            }
            if (FPlatformTime::Seconds() - _Start > 20.0)
            { return Fail(TEXT("queued Subject/body allocated retirement timed out")); }
            return false;
        }

    private:
        auto Fail(const TCHAR* Reason) -> bool
        { if (_Test != nullptr) { _Test->AddError(Reason); } _Evidence->RemoveObserver(); return true; }
        FAutomationTestBase* _Test = nullptr;
        TSharedRef<FLifetimeEvidence> _Evidence;
        ECase _Case;
        double _Start = -1.0;
    };

    class FExerciseGuard final : public IAutomationLatentCommand
    {
    public:
        FExerciseGuard(FAutomationTestBase* InTest, ECase InCase, bool InDedicated, TSharedRef<FLifetimeEvidence> InEvidence)
            : _Test(InTest), _Case(InCase), _Dedicated(InDedicated),
              _RunBodies(InTest, BodyClassPath, 30.0f, InCase != ECase::ModeMismatch), _Evidence(InEvidence) {}

        virtual bool Update() override
        {
            if (_Test == nullptr)
            { return Finish(); }
            if (ck::auto_test::net::Get_HasSessionFailed())
            { _Test->AddError(TEXT("independent guard fixture lost PIE readiness before exercise")); return Finish(); }

            if (NOT Preflight())
            { return Finish(); }

            if (_Case == ECase::SubjectBeforeAdmission)
            {
                if (NOT SpawnRealSubject())
                { return Finish(); }
            }

            // Snapshot after any subject setup. The delta below therefore isolates actual
            // per-world AS body requests made by the production latent command.
            TArray<TArray<FCk_Handle>> Before;
            for (const auto& Owner : _Owners)
            {
                TArray<FCk_Handle> Handles;
                if (NOT ReadCensus(Owner, Handles))
                { return Finish(); }
                Before.Add(MoveTemp(Handles));
            }

            const bool FirstDone = _RunBodies.Update();
            if (_Case != ECase::SubjectAfterAdmission)
            {
                if (NOT FirstDone)
                {
                    ck::auto_test::net::FailSession(TEXT("independent guard unexpectedly admitted an AS body"));
                    _RunBodies.Update();
                }
                _Test->TestTrue(TEXT("guard rejects before any body admission"), FirstDone);
                _Test->TestTrue(TEXT("guard failure marks the network session failed"),
                    ck::auto_test::net::Get_HasSessionFailed());
                for (int32 Index = 0; Index < _Owners.Num(); ++Index)
                {
                    TArray<FCk_Handle> After;
                    if (NOT ReadCensus(_Owners[Index], After))
                    { return Finish(); }
                    _Test->TestTrue(FString::Printf(TEXT("world %d received no AS body request"), Index),
                        HasSameDependents(Before[Index], After));
                }
                return Finish();
            }

            _Test->TestFalse(TEXT("real all-world body admission remains pending before a world tick"), FirstDone);
            if (FirstDone || ck::auto_test::net::Get_HasSessionFailed())
            {
                _Test->AddError(TEXT("independent body admission failed before subject injection"));
                _RunBodies.Update();
                return Finish();
            }

            TArray<FCk_Handle> Admitted;
            for (int32 Index = 0; Index < _Owners.Num(); ++Index)
            {
                TArray<FCk_Handle> After;
                if (NOT ReadCensus(_Owners[Index], After))
                {
                    ck::auto_test::net::FailSession(TEXT("independent fixture lost allocated body census"));
                    _RunBodies.Update();
                    return Finish();
                }
                TArray<FCk_Handle> Added;
                for (const auto& Candidate : After)
                {
                    if (NOT Before[Index].Contains(Candidate))
                    { Added.Add(Candidate); }
                }
                if (Added.Num() != 1 ||
                    NOT UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Added[0]))
                {
                    _Test->AddError(TEXT("real admission did not allocate exactly one body in every world"));
                    ck::auto_test::net::FailSession(TEXT("independent fixture could not identify admitted bodies"));
                    _RunBodies.Update();
                    return Finish();
                }
                Admitted.Add(Added[0]);
                _Evidence->Roots.Add(Added[0]);
                if (NOT _Evidence->Capture(Added[0], _Owners[Index]))
                {
                    _Test->AddError(TEXT("real admitted body closure could not be captured"));
                    ck::auto_test::net::FailSession(TEXT("independent fixture could not capture admitted body closure"));
                    _RunBodies.Update();
                    return Finish();
                }
            }

            if (NOT SpawnRealSubject())
            {
                ck::auto_test::net::FailSession(TEXT("independent fixture could not spawn the real subject"));
                _RunBodies.Update();
                return Finish();
            }

            // The second production Update runs on this same automation frame. No world tick
            // can construct a body or replicate the subject between these two calls.
            _Test->TestTrue(TEXT("late real NetSubject is rejected immediately"), _RunBodies.Update());
            _Test->TestTrue(TEXT("late subject fails the network session"),
                ck::auto_test::net::Get_HasSessionFailed());
            for (int32 Index = 0; Index < Admitted.Num(); ++Index)
            {
                _Test->TestTrue(FString::Printf(TEXT("admitted body %d remains allocated for rollback inspection"), Index),
                    UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Admitted[Index]));
                _Test->TestTrue(FString::Printf(TEXT("admitted body %d is marked BeginDestroy"), Index),
                    UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                        Admitted[Index], ECk_EntityLifetime_DestructionPhase::BeginDestroy));
            }
            return Finish();
        }

    private:
        auto ReadCensus(const FCk_Handle& Owner, TArray<FCk_Handle>& Out) -> bool
        {
            if (NOT UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    Owner, Owner, Out))
            { _Test->AddError(TEXT("allocated direct dependent census rejected the transient owner")); return false; }
            return true;
        }

        auto Preflight() -> bool
        {
            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            const auto ExpectedServerMode = _Dedicated ? NM_DedicatedServer : NM_ListenServer;
            if (Worlds.Num() != 2 || ck::auto_test::net::Get_NumClientWorlds() != 1 ||
                NOT IsValid(Server) || NOT IsValid(Client) || Server == Client ||
                Worlds[0] != Server || Worlds[1] != Client ||
                NOT Server->HasBegunPlay() || NOT Client->HasBegunPlay() ||
                Server->GetNetMode() != ExpectedServerMode || Client->GetNetMode() != NM_Client)
            { _Test->AddError(TEXT("independent guard fixture requires exact two-world PIE topology")); return false; }

            auto* Class = FSoftClassPath(BodyClassPath).TryLoadClass<UCk_EntityScript_UE>();
            if (NOT IsValid(Class) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
            { _Test->AddError(TEXT("real independent AS body class is unavailable")); return false; }
            const auto* CDO = Class->GetDefaultObject<UCk_EntityScript_UE>();
            const auto* Property = Class->FindPropertyByName(TEXT("_NetMode"));
            const auto* ExpectedEnum = StaticEnum<ECk_AutoTest_NetMode>();
            if (NOT IsValid(CDO) || Property == nullptr || ExpectedEnum == nullptr ||
                CDO->Get_EffectiveReplication() != ECk_Replication::DoesNotReplicate)
            { _Test->AddError(TEXT("real independent AS body CDO/schema is invalid")); return false; }
            uint8 RawMode = 255;
            if (const auto* Byte = CastField<FByteProperty>(Property))
            {
                if (Byte->Enum == ExpectedEnum)
                { RawMode = Byte->GetPropertyValue_InContainer(CDO); }
            }
            else if (const auto* Enum = CastField<FEnumProperty>(Property))
            {
                if (Enum->GetEnum() == ExpectedEnum)
                {
                    if (const auto* Underlying = CastField<FByteProperty>(Enum->GetUnderlyingProperty()))
                    { RawMode = Underlying->GetPropertyValue_InContainer(Enum->ContainerPtrToValuePtr<void>(CDO)); }
                }
            }
            if (RawMode != static_cast<uint8>(ECk_AutoTest_NetMode::ServerAndClientsIndependent))
            { _Test->AddError(TEXT("real authored AS body CDO is not Independent mode")); return false; }

            _Worlds = Worlds;
            for (auto* World : Worlds) { _Evidence->Worlds.Add(World); }
            for (auto* World : _Worlds)
            {
                if (NOT IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
                { _Test->AddError(TEXT("independent fixture has a stale PIE world")); return false; }
                auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
                if (NOT ck::IsValid(Owner) || NOT UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Owner))
                { _Test->AddError(TEXT("independent fixture has no admissible world transient owner")); return false; }
                _Owners.Add(Owner); _Evidence->Owners.Add(Owner);
            }
            return true;
        }

        auto SpawnRealSubject() -> bool
        {
            if (_Worlds.Num() != 2 || _Owners.Num() != 2 || NOT IsValid(_Worlds[0]))
            { _Test->AddError(TEXT("real Subject spawn has no exact captured context")); return false; }
            TArray<FCk_Handle> Before;
            if (NOT ReadCensus(_Owners[0], Before)) { return false; }
            auto Params = FActorSpawnParameters{};
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            _Subject = _Worlds[0]->SpawnActor<ACk_AutoTest_NetSubject>(
                ACk_AutoTest_NetSubject::StaticClass(), FTransform::Identity, Params);
            if (NOT IsValid(_Subject) || _Subject->GetWorld() != _Worlds[0])
            { _Test->AddError(TEXT("real NetSubject spawn failed on the exact server world")); return false; }
            // SpawnActor synchronously invokes the actual BeginPlay; no world tick intervenes.
            TArray<FCk_Handle> After, Added;
            if (NOT ReadCensus(_Owners[0], After)) { return false; }
            for (const auto& Handle : After) { if (NOT Before.Contains(Handle)) { Added.Add(Handle); } }
            if (Added.Num() != 1 || NOT UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Added[0]))
            { _Test->AddError(TEXT("actual Subject BeginPlay did not queue exactly one direct transient target")); return false; }
            _Evidence->SubjectTarget = Added[0]; _Evidence->Roots.Add(Added[0]);
            if (NOT _Evidence->Capture(Added[0], _Owners[0]))
            { _Test->AddError(TEXT("actual Subject target/request closure could not be captured")); return false; }
            TArray<FCk_Handle> Requests;
            if (NOT UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(Added[0],_Owners[0],Requests) ||
                Requests.Num() != 1 || NOT Requests[0].Has<ck::FFragment_EntityScript_RequestSpawnEntity>() ||
                Requests[0].Get<ck::FFragment_EntityScript_RequestSpawnEntity>().Get_NewEntity() != Added[0])
            { _Test->AddError(TEXT("Subject delta is not the real queued request target")); return false; }
            _Test->TestTrue(TEXT("actual OnActor request owns its freshly allocated target on failure"),
                Requests[0].Get<ck::FFragment_EntityScript_RequestSpawnEntity>().Get_DestroyTargetOnFailure());
            _Test->TestFalse(TEXT("same-frame Subject target has not published script/transform/driver"),
                Added[0].Has_Any<ck::FFragment_EntityScript, ck::FFragment_Transform,
                    TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>());
            return true;
        }

        auto Finish() -> bool
        {
            if (IsValid(_Subject))
            { if (_Test != nullptr) { _Test->TestTrue(TEXT("actual Subject accepts same-frame Destroy"), _Subject->Destroy()); }
              else { _Subject->Destroy(); } }
            _Subject = nullptr;
            _Evidence->ExerciseCompleted = true;
            return true;
        }

        FAutomationTestBase* _Test = nullptr;
        ECase _Case;
        bool _Dedicated = false;
        FCk_Latent_RunAsTestOnAllWorlds _RunBodies;
        TArray<UWorld*> _Worlds;
        TArray<FCk_Handle> _Owners;
        ACk_AutoTest_NetSubject* _Subject = nullptr;
        TSharedRef<FLifetimeEvidence> _Evidence;
    };

    auto RunCase(FAutomationTestBase* Test, const FString& Parameters, ECase Case) -> bool
    {
        bool Dedicated = false;
        if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
        { Test->AddError(TEXT("unsupported topology for independent guard fixture")); return false; }

        auto Expected = FString{};
        switch (Case)
        {
            case ECase::ModeMismatch:
                Expected = TEXT("stub no-subject policy disagrees with actual body mode");
                break;
            case ECase::SubjectBeforeAdmission:
                Expected = TEXT("has a NetSubject before body admission");
                break;
            case ECase::SubjectAfterAdmission:
                Expected = TEXT("gained a NetSubject while bodies ran");
                break;
            default:
                Test->AddError(TEXT("unknown independent guard case")); return false;
        }
        const auto SettingsBefore = FPlaySettingsSnapshot{};
        const auto Evidence = MakeShared<FLifetimeEvidence>();
        Evidence->TransformObserver = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
            [Weak = TWeakPtr<FLifetimeEvidence>{Evidence}](const FCk_Handle_Transform& Transform)
            {
                const auto Pinned = Weak.Pin();
                if (NOT Pinned.IsValid()) { return; }
                const FCk_Handle& Handle = Transform;
                if (Handle == Pinned->SubjectTarget) { Pinned->SubjectTransformPublished = true; }
            });
        Test->AddExpectedError(Expected, EAutomationExpectedErrorFlags::Contains, 1);
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FExerciseGuard(Test, Case, Dedicated, Evidence));
        ADD_LATENT_AUTOMATION_COMMAND(FObserveGuardRetirement(Test, Evidence, Case));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(Test,
            FCk_NetAutoTest_Assertion::CreateLambda([Test, SettingsBefore]() -> bool
            {
                Test->TestTrue(TEXT("PIE worlds are gone after independent guard rejection"),
                    ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
                Test->TestTrue(TEXT("editor play settings are restored"), SettingsBefore.MatchesCurrent());
                return true;
            }), TEXT("independent guard cleanup"), true));
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkAutoTest_Independent_ModeMismatch,
    "Ck.AutoTest.IndependentModeGuards.ModeMismatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkAutoTest_Independent_ModeMismatch::RunTest(const FString& Parameters)
{ return ck_autotest_independent_guards::RunCase(this, Parameters, ck_autotest_independent_guards::ECase::ModeMismatch); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkAutoTest_Independent_SubjectBeforeAdmission,
    "Ck.AutoTest.IndependentModeGuards.SubjectBeforeAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkAutoTest_Independent_SubjectBeforeAdmission::RunTest(const FString& Parameters)
{ return ck_autotest_independent_guards::RunCase(this, Parameters, ck_autotest_independent_guards::ECase::SubjectBeforeAdmission); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkAutoTest_Independent_SubjectAfterAdmission,
    "Ck.AutoTest.IndependentModeGuards.SubjectAfterAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkAutoTest_Independent_SubjectAfterAdmission::RunTest(const FString& Parameters)
{ return ck_autotest_independent_guards::RunCase(this, Parameters, ck_autotest_independent_guards::ECase::SubjectAfterAdmission); }

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

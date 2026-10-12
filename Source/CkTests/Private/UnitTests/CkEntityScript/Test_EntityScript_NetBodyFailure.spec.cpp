#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkAutoTest_Utils.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Settings/LevelEditorPlaySettings.h"

namespace ck_entity_script_net_body_failure
{
    constexpr auto BodyClassPath = TEXT("/Script/Angelscript.Ck_AutoTest_EntityScript_BasicSpawn");

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

    // Call the real latent body's admission and poll paths on the same automation frame.
    // This makes the target cancellation deterministic: no world tick can Construct either
    // body between the two calls.
    class FCancelAdmittedBody final : public IAutomationLatentCommand
    {
    public:
        explicit FCancelAdmittedBody(FAutomationTestBase* InTest)
            : _Test(InTest), _RunBodies(InTest, BodyClassPath, 30.0f) {}

        virtual bool Update() override
        {
            if (_Test == nullptr || ck::auto_test::net::Get_HasSessionFailed())
            { return true; }

            const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
            if (Worlds.Num() != 2 || Worlds[0] == nullptr || Worlds[1] == nullptr || Worlds[0] == Worlds[1])
            { _Test->AddError(TEXT("Body cancellation requires two distinct PIE worlds")); return true; }

            TArray<FCk_Handle> Owners;
            TArray<TArray<FCk_Handle>> Before;
            for (auto* World : Worlds)
            {
                auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
                if (NOT ck::IsValid(Owner))
                { _Test->AddError(TEXT("PIE world lost its TransientEntity")); return true; }
                Before.Add(UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owner));
                Owners.Add(Owner);
            }

            _Test->TestFalse(TEXT("real per-world body admission remains pending until a world tick"),
                _RunBodies.Update());

            TArray<FCk_Handle> Bodies;
            for (auto Index = 0; Index < Owners.Num(); ++Index)
            {
                const auto After = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owners[Index]);
                TArray<FCk_Handle> Added;
                for (const auto& Candidate : After)
                {
                    if (NOT Before[Index].Contains(Candidate))
                    { Added.Add(Candidate); }
                }
                if (Added.Num() != 1)
                {
                    for (auto& Candidate : Added)
                    {
                        if (ck::IsValid(Candidate))
                        { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Candidate); }
                    }
                    _Test->AddError(TEXT("real body admission did not add exactly one dependent per world"));
                    return true;
                }
                Bodies.Add(Added[0]);
            }

            const auto BodiesReady = ck::IsValid(Bodies[0]) && ck::IsValid(Bodies[1]);
            _Test->TestTrue(TEXT("both admitted bodies are live before cancellation"), BodiesReady);
            if (NOT BodiesReady)
            { return true; }
            _Test->TestFalse(TEXT("cancelled target has not constructed"),
                Bodies[0].Has<ck::FFragment_EntityScript>());
            _Test->TestFalse(TEXT("sibling has not constructed"),
                Bodies[1].Has<ck::FFragment_EntityScript>());

            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Bodies[0]);
            _Test->TestTrue(TEXT("real latent command terminates on lost admitted body"),
                _RunBodies.Update());
            _Test->TestTrue(TEXT("body loss fails the network session"),
                ck::auto_test::net::Get_HasSessionFailed());
            _Test->TestTrue(TEXT("cancelled target is marked for destruction"),
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Bodies[0], ECk_EntityLifetime_DestructionPhase::BeginDestroy));
            _Test->TestTrue(TEXT("sibling is marked for destruction on the same frame"),
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Bodies[1], ECk_EntityLifetime_DestructionPhase::BeginDestroy));
            _Test->TestFalse(TEXT("sibling never reached Construct before cancellation"),
                Bodies[1].Has<ck::FFragment_EntityScript>());
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        FCk_Latent_RunAsTestOnAllWorlds _RunBodies;
    };

    // Keep the real body command pending while both authored bodies construct.
    // Once their result fragments exist, corrupt one through the same reflected
    // Set_Result API used by the AS base and poll the command before another tick.
    class FRejectMalformedBodyResult final : public IAutomationLatentCommand
    {
    public:
        explicit FRejectMalformedBodyResult(FAutomationTestBase* InTest)
            : _Test(InTest), _RunBodies(InTest, BodyClassPath, 30.0f) {}

        virtual bool Update() override
        {
            if (_Test == nullptr || ck::auto_test::net::Get_HasSessionFailed())
            { return true; }

            if (NOT _Admitted)
            {
                const auto Worlds = ck::auto_test::net::Get_AllPIEWorlds();
                if (Worlds.Num() != 2 || Worlds[0] == nullptr || Worlds[1] == nullptr || Worlds[0] == Worlds[1])
                { _Test->AddError(TEXT("Malformed-result test requires two distinct PIE worlds")); return true; }

                TArray<FCk_Handle> Owners;
                TArray<TArray<FCk_Handle>> Before;
                for (auto* World : Worlds)
                {
                    auto Owner = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
                    if (NOT ck::IsValid(Owner))
                    { _Test->AddError(TEXT("PIE world lost its TransientEntity")); return true; }
                    Before.Add(UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owner));
                    Owners.Add(Owner);
                }

                if (_RunBodies.Update())
                { _Test->AddError(TEXT("real body command rejected admission")); return true; }
                _Admitted = true;
                _StartSeconds = FPlatformTime::Seconds();

                for (auto Index = 0; Index < Owners.Num(); ++Index)
                {
                    const auto After = UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(Owners[Index]);
                    TArray<FCk_Handle> Added;
                    for (const auto& Candidate : After)
                    {
                        if (NOT Before[Index].Contains(Candidate))
                        { Added.Add(Candidate); }
                    }
                    if (Added.Num() != 1)
                    {
                        for (auto& Candidate : Added)
                        {
                            if (ck::IsValid(Candidate))
                            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Candidate); }
                        }
                        _Test->AddError(TEXT("real body admission did not add exactly one dependent per world"));
                        ck::auto_test::net::FailSession(TEXT("Malformed-result body identity was not observable"));
                        _RunBodies.Update();
                        return true;
                    }
                    _Bodies.Add(Added[0]);
                }
                return false;
            }

            if (FPlatformTime::Seconds() - _StartSeconds > 30.0)
            {
                ck::auto_test::net::FailSession(TEXT("Malformed-result bodies did not construct in time"));
                _RunBodies.Update();
                return true;
            }
            if (_Bodies.Num() != 2 || NOT ck::IsValid(_Bodies[0]) || NOT ck::IsValid(_Bodies[1]))
            {
                ck::auto_test::net::FailSession(TEXT("Malformed-result body disappeared before injection"));
                _RunBodies.Update();
                return true;
            }
            for (auto& Body : _Bodies)
            {
                if (NOT Body.Has<ck::FFragment_EntityScript>() ||
                    NOT UCk_Utils_AutoTest_UE::Has_Result(Body))
                { return false; }
            }

            UCk_Utils_AutoTest_UE::Set_Result(_Bodies[0],
                static_cast<ECk_AutoTest_Status>(255), TEXT("injected invalid status"), 0, 0);
            _Test->TestEqual(TEXT("actual result API published the malformed status"),
                static_cast<uint8>(UCk_Utils_AutoTest_UE::Get_Result(_Bodies[0]).Status), uint8{255});
            _Test->TestTrue(TEXT("real latent command rejects the malformed result immediately"),
                _RunBodies.Update());
            _Test->TestTrue(TEXT("malformed result fails the network session"),
                ck::auto_test::net::Get_HasSessionFailed());
            _Test->TestTrue(TEXT("malformed body is marked for destruction on the same frame"),
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    _Bodies[0], ECk_EntityLifetime_DestructionPhase::BeginDestroy));
            _Test->TestTrue(TEXT("constructed sibling is marked for destruction on the same frame"),
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    _Bodies[1], ECk_EntityLifetime_DestructionPhase::BeginDestroy));
            return true;
        }

    private:
        FAutomationTestBase* _Test = nullptr;
        FCk_Latent_RunAsTestOnAllWorlds _RunBodies;
        TArray<FCk_Handle> _Bodies;
        bool _Admitted = false;
        double _StartSeconds = -1.0;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkEntityScript_NetBodyCancellation,
    "Ck.EntityScript.Net.BodyCancellationCleansSibling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScript_NetBodyCancellation::RunTest(const FString& Parameters)
{
    auto Dedicated = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("Unsupported topology for net body cancellation")); return false; }

    const auto SettingsBefore = ck_entity_script_net_body_failure::FPlaySettingsSnapshot{};
    AddExpectedError(TEXT("lost its admitted body"), EAutomationExpectedErrorFlags::Contains, 1);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(ck_entity_script_net_body_failure::FCancelAdmittedBody(this));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SettingsBefore]() -> bool
        {
            TestTrue(TEXT("PIE worlds are gone after body cancellation"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
            TestTrue(TEXT("editor play settings are restored"), SettingsBefore.MatchesCurrent());
            return true;
        }), TEXT("body-cancellation cleanup")));
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(
    FCkEntityScript_NetBodyMalformedResult,
    "Ck.EntityScript.Net.MalformedBodyResultCleansSibling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkEntityScript_NetBodyMalformedResult::RunTest(const FString& Parameters)
{
    auto Dedicated = false;
    if (NOT ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("Unsupported topology for malformed net body result")); return false; }

    const auto SettingsBefore = ck_entity_script_net_body_failure::FPlaySettingsSnapshot{};
    AddExpectedError(TEXT("reported an invalid result status"), EAutomationExpectedErrorFlags::Contains, 1);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(ck_entity_script_net_body_failure::FRejectMalformedBodyResult(this));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, SettingsBefore]() -> bool
        {
            TestTrue(TEXT("PIE worlds are gone after malformed result rejection"),
                ck::auto_test::net::Get_AllPIEWorlds().IsEmpty());
            TestTrue(TEXT("editor play settings are restored"), SettingsBefore.MatchesCurrent());
            return true;
        }), TEXT("malformed-result cleanup")));
    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

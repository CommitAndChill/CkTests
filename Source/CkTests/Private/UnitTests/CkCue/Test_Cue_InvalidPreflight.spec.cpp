#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkCue/CkCueSubsystem_Base.h"
#include "CkCue/CkCue_EntityScript.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "CoreGlobals.h"

#include <utility>

namespace ck_cue_invalid_preflight_test
{
    struct FRun
    {
        TArray<uint32> BeforeEntities;
        bool BaselineCaptured = false;
        bool Rejected = false;
    };

    // Entity storage includes allocated entities before they acquire any fragments.
    auto TryReadAllocatedEntities(UWorld* World, TArray<uint32>& Out) -> bool
    {
        if (!IsValid(World)) { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!ck::IsValid(Root)) { return false; }
        const auto Registry = Root.Get_RegistryView();
        const auto* Native = ck::registry_table::TryResolve(Registry.Get_RegistryHandle());
        if (!Native) { return false; }
        const auto* Entities = std::as_const(*Native).storage<FCk_Entity::IdType>();
        if (!Entities) { return false; }
        TArray<uint32> IDs;
        for (const auto& [Entity] : Entities->each())
        { IDs.Add(static_cast<uint32>(Entity)); }
        IDs.Sort();
        Out = MoveTemp(IDs);
        return true;
    }

    class FObserveThirtyFrames final : public IAutomationLatentCommand
    {
    public:
        FObserveThirtyFrames(FAutomationTestBase* InTest, TSharedRef<FRun> InRun)
            : Test(InTest), Run(InRun) {}

        bool Update() override
        {
            if (!Test || ck::auto_test::net::Get_HasSessionFailed()) { return true; }
            if (GFrameCounter == LastFrame) { return false; }
            LastFrame = GFrameCounter;
            TArray<uint32> Now;
            if (!Run->Rejected || !Run->BaselineCaptured ||
                !TryReadAllocatedEntities(ck::auto_test::net::Get_ServerWorld(), Now) ||
                Now != Run->BeforeEntities)
            {
                Test->AddError(FString::Printf(
                    TEXT("Cue rejection changed allocated entity IDs at observed frame %d"),
                    Frames + 1));
                return true;
            }
            return ++Frames >= 30;
        }

    private:
        FAutomationTestBase* Test = nullptr;
        TSharedRef<FRun> Run;
        uint64 LastFrame = MAX_uint64;
        int32 Frames = 0;
    };

    auto TagsAndPoliciesReady(UWorld* World) -> bool
    {
        if (!IsValid(World) || !IsValid(GEngine)) { return false; }
        const auto* Discovery = GEngine->GetEngineSubsystem<UCk_GenericCueSubsystem_UE>();
        const auto* Executor = World->GetSubsystem<UCk_GenericCueExecutor_Subsystem_UE>();
        if (!IsValid(Discovery) || !IsValid(Executor) || Executor->GetWorld() != World)
        { return false; }
        const auto Skip = FGameplayTag::RequestGameplayTag(
            FName(TEXT("CueGym.Owner.SkipInvalid")), false);
        const auto Require = FGameplayTag::RequestGameplayTag(
            FName(TEXT("CueGym.Owner.RequireValid")), false);
        const auto Missing = FGameplayTag::RequestGameplayTag(
            FName(TEXT("ActorRelay.Cue.Generic")), false);
        if (!Skip.IsValid() || !Require.IsValid() || !Missing.IsValid()) { return false; }
        const auto& Discovered = Discovery->Get_DiscoveredCues();
        const auto* SkipClass = Discovered.Find(Skip);
        const auto* RequireClass = Discovered.Find(Require);
        if (!SkipClass || !RequireClass || !ck::IsValid(*SkipClass) ||
            !ck::IsValid(*RequireClass) || Discovered.Contains(Missing)) { return false; }
        const auto* SkipDefault = (*SkipClass)->GetDefaultObject<UCk_CueBase_EntityScript>();
        const auto* RequireDefault = (*RequireClass)->GetDefaultObject<UCk_CueBase_EntityScript>();
        return IsValid(SkipDefault) && IsValid(RequireDefault) &&
            SkipDefault->Get_OwnerValidationPolicy() ==
                ECk_Cue_OwnerValidationPolicy::SkipIfInvalid &&
            RequireDefault->Get_OwnerValidationPolicy() ==
                ECk_Cue_OwnerValidationPolicy::RequireValid;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCue_InvalidPreflight,
    "Ck.Cue.Preflight.InvalidInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCue_InvalidPreflight::RunTest(const FString& Parameters)
{
    using namespace ck_cue_invalid_preflight_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Cue preflight network topology")); return false; }

    // Each one fixed ensure is recorded by both CkEnsure and CkEnsures.
    // The public request below invokes each failure site exactly once.
    AddExpectedErrorPlain(TEXT("Failed to find Cue with Name [ActorRelay.Cue.Generic]!"),
        EAutomationExpectedErrorFlags::Contains, 2);
    AddExpectedErrorPlain(TEXT("CueClass was INVALID when trying to execute Cue [ActorRelay.Cue.Generic]"),
        EAutomationExpectedErrorFlags::Contains, 2);
    AddExpectedErrorPlain(TEXT("OwnerEntity is invalid when trying to execute Cue [CueGym.Owner.RequireValid] (OwnerValidationPolicy = RequireValid)"),
        EAutomationExpectedErrorFlags::Contains, 2);

    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        { return TagsAndPoliciesReady(ck::auto_test::net::Get_ServerWorld()); }), 60.0,
        TEXT("authored Cue tags, discovered classes, and owner policies ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(5));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            Run->BaselineCaptured = TryReadAllocatedEntities(World, Run->BeforeEntities);
            TestTrue(TEXT("real Cue PIE world ECS census available"),
                Run->BaselineCaptured && !Run->BeforeEntities.IsEmpty());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(15));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            TArray<uint32> Quiescent;
            if (!TestTrue(TEXT("Cue world quiescent before invalid calls"),
                Run->BaselineCaptured && !Run->BeforeEntities.IsEmpty() &&
                TryReadAllocatedEntities(World, Quiescent) &&
                Quiescent == Run->BeforeEntities &&
                TagsAndPoliciesReady(World))) { return; }
            auto* Executor = World->GetSubsystem<UCk_GenericCueExecutor_Subsystem_UE>();
            const auto Missing = FGameplayTag::RequestGameplayTag(
                FName(TEXT("ActorRelay.Cue.Generic")), false);
            const auto Skip = FGameplayTag::RequestGameplayTag(
                FName(TEXT("CueGym.Owner.SkipInvalid")), false);
            const auto Require = FGameplayTag::RequestGameplayTag(
                FName(TEXT("CueGym.Owner.RequireValid")), false);
            const FInstancedStruct EmptyParams;
            const FCk_Handle InvalidOwner;
            const auto MissingResult = Executor->Request_ExecuteCue(InvalidOwner,
                Missing, EmptyParams, ECk_Cue_ReliabilityPolicy::Unreliable,
                ECk_Cue_MulticastPolicy::LocalOnly);
            TArray<uint32> AfterMissing;
            const bool MissingUnchanged = TryReadAllocatedEntities(World, AfterMissing) &&
                AfterMissing == Run->BeforeEntities;
            TestTrue(TEXT("missing Cue tag returns invalid with unchanged allocated IDs"),
                ck::Is_NOT_Valid(MissingResult) && MissingUnchanged);
            const auto SkipResult = Executor->Request_ExecuteCue(InvalidOwner,
                Skip, EmptyParams, ECk_Cue_ReliabilityPolicy::Unreliable,
                ECk_Cue_MulticastPolicy::LocalOnly);
            TArray<uint32> AfterSkip;
            const bool SkipUnchanged = TryReadAllocatedEntities(World, AfterSkip) &&
                AfterSkip == Run->BeforeEntities;
            TestTrue(TEXT("SkipIfInvalid returns invalid with unchanged allocated IDs"),
                ck::Is_NOT_Valid(SkipResult) && SkipUnchanged);
            const auto RequireResult = Executor->Request_ExecuteCue(InvalidOwner,
                Require, EmptyParams, ECk_Cue_ReliabilityPolicy::Unreliable,
                ECk_Cue_MulticastPolicy::LocalOnly);
            TArray<uint32> AfterRequire;
            const bool RequireUnchanged = TryReadAllocatedEntities(World, AfterRequire) &&
                AfterRequire == Run->BeforeEntities;
            TestTrue(TEXT("RequireValid returns invalid with unchanged allocated IDs"),
                ck::Is_NOT_Valid(RequireResult) && RequireUnchanged);
            Run->Rejected = ck::Is_NOT_Valid(MissingResult) &&
                ck::Is_NOT_Valid(SkipResult) && ck::Is_NOT_Valid(RequireResult) &&
                MissingUnchanged && SkipUnchanged && RequireUnchanged;
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FObserveThirtyFrames(this, Run));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        { return TestTrue(TEXT("Cue preflight PIE worlds drained"),
            ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()); }),
        TEXT("Cue preflight PIE cleanup"), true));
    return true;
}

#endif

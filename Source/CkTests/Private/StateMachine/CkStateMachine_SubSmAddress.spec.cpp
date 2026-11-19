// Sub-SM network addresses and the replay where-it-stands check, on one machine.
//
// S1 SiblingsHaveDistinctAddresses - every level of a sub-SM's address names the hosting state's tag AND the sub-SM
//    task's ordinal among that state's sub-SM tasks, so two sub-SM tasks on one state no longer share an address, and
//    nesting composes. A state with a single sub-SM keeps its tag path (ordinal 0) and its states' hierarchies, and an
//    address in the old shape (tags only) still resolves to ordinal 0.
// R1 ReplayChecksWhereItStands - an event placed on a sub-SM's replay queue (exactly what the relay and the
//    rep-receive dispatch do) is checked before it is applied:
//      a. addressed to another sub-SM                       -> dropped, the SM does not move
//      b. its previous state is not where the SM is, and no
//         event is missing before it                        -> dropped, the SM does not move
//      c. same, but events before it are missing (seq gap)  -> resync: the SM snaps to the event's state
//      d. a matching event                                  -> applied as before, no warning
// R2 ReplayCheckEdgeCases - the rest of the where-it-stands rule:
//      e. previous state is an override class of where the SM stands  -> applied, no warning
//      f. a null previous state makes no claim                         -> applied, no warning
//      g. a gapped event whose new state is where the SM already is    -> consumed: watermark only, same state entity
//      h. a lagged-out ring for a sub-SM, through the WithHistory receive dispatch -> the snap carries the sub-SM's
//         address, so it lands instead of being dropped as misaddressed
//      i. WithoutHistory, mismatched, nothing missing                  -> resync (latest-state snaps to current)
//      j. WithoutHistory, an event naming the state it stands in       -> re-enters (a re-entry, not consumed)
//      k. a never-entered SM stands at its initial state               -> no warning, watermark advances
// D1 ADuplicateAddressIsRefused - a sub-SM task entered a second time builds a sub-SM at the address its first one
//    still holds: the task ensures, destroys the new sub-SM, fails, and the first sub-SM keeps the address.
//
// Both run on a DoesNotReplicate SM on the server world: Hold [ A [ C, D ], B ] from
// Script/CkStateMachine/CkAutoTest_StateMachine_Net_SubSmSiblings.as, whose transitions need a byte input the bare
// entity does not have, so nothing moves on its own.
//
// Surface in Session Frontend: Ck.StateMachine.SubSmAddress

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Misc/OutputDevice.h"
#include "UObject/SoftObjectPath.h"

#include "CkCore/Ensure/CkEnsure_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkStateMachine/Net/CkStateMachine_NetContextUtils.h"
#include "CkStateMachine/Net/CkStateMachine_RepData.h"
#include "CkStateMachine/Net/CkStateMachine_NetContext.h"
#include "CkStateMachine/State/CkSmState_Fragment.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment_Data.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkStateMachine/Task/CkSmTask_Fragment.h"
#include "CkStateMachine/Task/EntityScripts/CkSmTask_SubStateMachine.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_sm_subsm_address_test
{
    using FSubSmAddress = ck::statemachine::FSubSmAddress;

    static FCk_Handle_StateMachine GRoot;
    static FCk_Handle_StateMachine GSingleRoot;
    static FCk_Handle_StateMachine GNoHistoryRoot;
    static FCk_Handle_StateMachine GNeverStartedRoot;

    class FReplayCheckWatch final : public FOutputDevice
    {
    public:
        auto Reset() -> void
        {
            FScopeLock Lock{&_Guard};
            _Lines.Reset();
        }

        auto Get_Lines() -> TArray<FString>
        {
            FScopeLock Lock{&_Guard};
            return _Lines;
        }

    protected:
        virtual void Serialize(const TCHAR* InText, ELogVerbosity::Type InVerbosity, const FName& InCategory) override
        {
            if (InCategory != FName{TEXT("CkStateMachine")} || InVerbosity > ELogVerbosity::Warning)
            { return; }

            const auto Text = FString{InText};
            if (NOT Text.Contains(TEXT("Replay check")))
            { return; }

            FScopeLock Lock{&_Guard};
            _Lines.Add(Text);
        }

    private:
        FCriticalSection _Guard;
        TArray<FString> _Lines;
    };

    auto Get_Watch() -> FReplayCheckWatch&
    {
        static FReplayCheckWatch Watch;
        return Watch;
    }

    auto Get_AsState(const TCHAR* InName) -> TSubclassOf<UCk_SmState_EntityScript>
    {
        return FSoftClassPath{FString::Printf(TEXT("/Script/Angelscript.%s"), InName)}.TryLoadClass<UCk_SmState_EntityScript>();
    }

    auto Get_Tag(const TCHAR* InStateName) -> FGameplayTag
    {
        return UCk_SmState_EntityScript::Get_StateTagForClass(Get_AsState(InStateName));
    }

    auto Get_SubSmAtPath(const FCk_Handle_StateMachine& InRoot, const TArray<int32>& InPath) -> FCk_Handle_StateMachine
    {
        auto Current = InRoot;
        for (const auto Ordinal : InPath)
        {
            if (ck::Is_NOT_Valid(Current))
            { return {}; }

            const auto State = UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(Current);
            if (ck::Is_NOT_Valid(State))
            { return {}; }

            auto Found = FCk_Handle_StateMachine{};
            auto Seen = 0;
            UCk_Utils_StateMachine_UE::RecordOfSmTasks_Utils::ForEach_ValidEntry(State,
            [&](FCk_Handle_SmTask InTask) -> ECk_Record_ForEachIterationResult
            {
                if (NOT InTask.Has<ck::FFragment_SmTask_SubStateMachine>())
                { return ECk_Record_ForEachIterationResult::Continue; }

                if (Seen++ == Ordinal)
                {
                    Found = InTask.Get<ck::FFragment_SmTask_SubStateMachine>().Get_SubStateMachineHandle();
                    return ECk_Record_ForEachIterationResult::Break;
                }
                return ECk_Record_ForEachIterationResult::Continue;
            });

            Current = Found;
        }
        return Current;
    }

    auto Get_StateName(const FCk_Handle_StateMachine& InSm) -> FString
    {
        if (ck::Is_NOT_Valid(InSm))
        { return TEXT("<no sm>"); }

        const auto* CurrentClass = UCk_Utils_StateMachine_UE::Get_CurrentStateClass(InSm).Get();
        return CurrentClass != nullptr ? CurrentClass->GetName() : TEXT("<none>");
    }

    auto Get_IsTopologyUp() -> bool
    {
        return Get_StateName(Get_SubSmAtPath(GRoot, {0, 0})) == TEXT("Ck_SmNetSibTest_N3C_Idle")
            && Get_StateName(Get_SubSmAtPath(GRoot, {0, 1})) == TEXT("Ck_SmNetSibTest_N3D_Idle")
            && Get_StateName(Get_SubSmAtPath(GRoot, {1})) == TEXT("Ck_SmNetSibTest_N3B_Idle");
    }

    auto DoAddRoot(
        FAutomationTestBase* InTest,
        UWorld* InServer,
        const FCk_StateMachine_Spec& InSpec,
        const TCHAR* InInitialState,
        FCk_Handle_StateMachine& OutSm) -> void
    {
        auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InServer, {});
        if (ck::Is_NOT_Valid(Owner))
        { InTest->AddError(TEXT("failed to create the transient owner entity")); return; }

        OutSm = UCk_Utils_StateMachine_UE::Add(Owner, InSpec);
        if (ck::Is_NOT_Valid(OutSm))
        { InTest->AddError(FString::Printf(TEXT("failed to add the SM with initial state [%s]"), InInitialState)); }
    }

    auto DoAddStart(FAutomationTestBase* InTest, bool InWithSingle) -> void
    {
        GRoot = {};
        GSingleRoot = {};

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, FString{TEXT("/Engine/Maps/Entry")}));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InWithSingle](UWorld* InServer) -> void
            {
                GLog->AddOutputDevice(&Get_Watch());
                Get_Watch().Reset();

                // Defaults: AutoStart OnSetup, DoesNotReplicate, WithHistory.
                DoAddRoot(InTest, InServer, FCk_StateMachine_Spec{Get_AsState(TEXT("Ck_SmNetSibTest_N3_Hold"))},
                    TEXT("Ck_SmNetSibTest_N3_Hold"), GRoot);
                if (InWithSingle)
                {
                    DoAddRoot(InTest, InServer, FCk_StateMachine_Spec{Get_AsState(TEXT("Ck_SmNetSubTest_Parent_Hold"))},
                        TEXT("Ck_SmNetSubTest_Parent_Hold"), GSingleRoot);
                }
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([InWithSingle]() -> bool
            {
                return Get_IsTopologyUp()
                    && (NOT InWithSingle || ck::IsValid(Get_SubSmAtPath(GSingleRoot, {0})));
            }),
            8.0,
            TEXT("both levels of sub-SMs are constructed and in their initial states")));
    }

    auto DoAddEnd(FAutomationTestBase* InTest) -> void
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
            {
                GLog->RemoveOutputDevice(&Get_Watch());
                GRoot = {};
                GSingleRoot = {};
                GNoHistoryRoot = {};
                GNeverStartedRoot = {};
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("restore log suppression statics")));
    }

    auto Make_Event(
        const FSubSmAddress& InAddress,
        const TCHAR* InFrom,
        const TCHAR* InTo,
        int32 InSeq) -> FCk_Sm_TransitionEvent
    {
        auto Event = FCk_Sm_TransitionEvent{Get_AsState(InFrom), Get_AsState(InTo), InSeq, 0};
        Event.Set_SubSmIdentity(InAddress.Get_Hierarchy());
        Event.Set_SubSmOrdinals(InAddress.Get_Ordinals());
        return Event;
    }

    auto DoEnqueue(const FCk_Handle_StateMachine& InSm, const FCk_Sm_TransitionEvent& InEvent) -> void
    {
        auto Entity = FCk_Handle{InSm};
        Entity.AddOrGet<ck::FFragment_Sm_ReplayQueue>().Get_Queue().Add(InEvent);
    }

    auto Get_LastAppliedSeq(const FCk_Handle_StateMachine& InSm) -> int32
    {
        return InSm.Has<ck::FFragment_Sm_ClientReplayState>()
            ? InSm.Get<ck::FFragment_Sm_ClientReplayState>().Get_ClientLastAppliedSeq()
            : 0;
    }

    auto Get_WatchCount(const TCHAR* InNeedle) -> int32
    {
        auto Count = 0;
        for (const auto& Line : Get_Watch().Get_Lines())
        {
            if (Line.Contains(InNeedle))
            { ++Count; }
        }
        return Count;
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachine_SubSmAddress_SiblingsHaveDistinctAddresses,
    "Ck.StateMachine.SubSmAddress.SiblingsHaveDistinctAddresses",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachine_SubSmAddress_SiblingsHaveDistinctAddresses::RunTest(const FString& Parameters)
{
    using namespace ck_sm_subsm_address_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    DoAddStart(this, true);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            const auto A = Get_SubSmAtPath(GRoot, {0});
            const auto B = Get_SubSmAtPath(GRoot, {1});
            const auto C = Get_SubSmAtPath(GRoot, {0, 0});
            const auto D = Get_SubSmAtPath(GRoot, {0, 1});
            if (ck::Is_NOT_Valid(A) || ck::Is_NOT_Valid(B) || ck::Is_NOT_Valid(C) || ck::Is_NOT_Valid(D))
            { AddError(TEXT("a sub-SM of Hold [ A [ C, D ], B ] is missing")); return false; }

            const auto HoldTag = Get_Tag(TEXT("Ck_SmNetSibTest_N3_Hold"));
            const auto HostTag = Get_Tag(TEXT("Ck_SmNetSibTest_N3A_Host"));

            const auto AddressA = UCk_Utils_StateMachine_UE::Get_SubSmAddress(A);
            const auto AddressB = UCk_Utils_StateMachine_UE::Get_SubSmAddress(B);
            const auto AddressC = UCk_Utils_StateMachine_UE::Get_SubSmAddress(C);
            const auto AddressD = UCk_Utils_StateMachine_UE::Get_SubSmAddress(D);

            TestTrue(TEXT("sibling A's address is (Hold#0)"),
                AddressA == FSubSmAddress::Make({HoldTag}, {0}));
            TestTrue(TEXT("sibling B's address is (Hold#1)"),
                AddressB == FSubSmAddress::Make({HoldTag}, {1}));
            TestTrue(TEXT("nested C's address is (Hold#0 > Host#0)"),
                AddressC == FSubSmAddress::Make({HoldTag, HostTag}, {0, 0}));
            TestTrue(TEXT("nested D's address is (Hold#0 > Host#1)"),
                AddressD == FSubSmAddress::Make({HoldTag, HostTag}, {0, 1}));

            TestTrue(TEXT("siblings A and B have different addresses"), NOT (AddressA == AddressB));
            TestTrue(TEXT("nested siblings C and D have different addresses"), NOT (AddressC == AddressD));

            TestTrue(TEXT("the lookup resolves A's address to A"),
                UCk_Utils_StateMachine_UE::TryFind_ActiveSubSm_ByAddress(GRoot, AddressA) == A);
            TestTrue(TEXT("the lookup resolves B's address to B"),
                UCk_Utils_StateMachine_UE::TryFind_ActiveSubSm_ByAddress(GRoot, AddressB) == B);
            TestTrue(TEXT("the lookup resolves C's address to C"),
                UCk_Utils_StateMachine_UE::TryFind_ActiveSubSm_ByAddress(GRoot, AddressC) == C);
            TestTrue(TEXT("the lookup resolves D's address to D"),
                UCk_Utils_StateMachine_UE::TryFind_ActiveSubSm_ByAddress(GRoot, AddressD) == D);

            TestTrue(TEXT("an old-shape address (tags only) reads as ordinal 0 and resolves to A"),
                UCk_Utils_StateMachine_UE::TryFind_ActiveSubSm_ByAddress(GRoot, FSubSmAddress::Make({HoldTag}, {})) == A);

            // The tag half is what the address always was, and states below a sub-SM still build their
            // hierarchy from it.
            TestTrue(TEXT("nested C's tag path is still its hosting state's hierarchy"),
                UCk_Utils_StateMachine_UE::Get_SubSmParentHierarchy(C) == TArray<FGameplayTag>{HoldTag, HostTag});

            const auto Single = Get_SubSmAtPath(GSingleRoot, {0});
            if (ck::Is_NOT_Valid(Single))
            { AddError(TEXT("the single sub-SM is missing")); return false; }

            const auto ParentTag = Get_Tag(TEXT("Ck_SmNetSubTest_Parent_Hold"));
            const auto AddressSingle = UCk_Utils_StateMachine_UE::Get_SubSmAddress(Single);

            TestTrue(TEXT("a single sub-SM's address is its hosting state's tag path at ordinal 0"),
                AddressSingle == FSubSmAddress::Make({ParentTag}, {0}));
            TestTrue(TEXT("a single sub-SM's tag path is unchanged"),
                UCk_Utils_StateMachine_UE::Get_SubSmParentHierarchy(Single) == TArray<FGameplayTag>{ParentTag});
            TestTrue(TEXT("an old-shape address resolves the single sub-SM"),
                UCk_Utils_StateMachine_UE::TryFind_ActiveSubSm_ByAddress(GSingleRoot, FSubSmAddress::Make({ParentTag}, {})) == Single);

            const auto SingleState = UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(Single);
            if (ck::IsValid(SingleState) && SingleState.Has<ck::FFragment_SmState_Hierarchy>())
            {
                const auto CurrentClass = UCk_Utils_StateMachine_UE::Get_CurrentStateClass(Single);
                TestTrue(TEXT("the single sub-SM's state hierarchy is (hosting state, own state)"),
                    SingleState.Get<ck::FFragment_SmState_Hierarchy>().Get_Hierarchy()
                        == TArray<FGameplayTag>{ParentTag, UCk_SmState_EntityScript::Get_StateTagForClass(CurrentClass)});
            }
            else
            { AddError(TEXT("the single sub-SM has no current state with a hierarchy")); }

            TestEqual(TEXT("no replay check fired"), Get_Watch().Get_Lines().Num(), 0);
            return true;
        }),
        TEXT("sibling, nested and single sub-SM addresses")));

    DoAddEnd(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachine_SubSmAddress_ReplayChecksWhereItStands,
    "Ck.StateMachine.SubSmAddress.ReplayChecksWhereItStands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachine_SubSmAddress_ReplayChecksWhereItStands::RunTest(const FString& Parameters)
{
    using namespace ck_sm_subsm_address_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    constexpr auto FramesPerCase = 20;

    DoAddStart(this, false);

    // a. An event addressed to sibling A, whose previous state happens to match where B stands, placed on B's queue.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            const auto A = Get_SubSmAtPath(GRoot, {0});
            const auto B = Get_SubSmAtPath(GRoot, {1});
            DoEnqueue(B, Make_Event(UCk_Utils_StateMachine_UE::Get_SubSmAddress(A),
                TEXT("Ck_SmNetSibTest_N3B_Idle"), TEXT("Ck_SmNetSibTest_N3B_One"), 1));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerCase));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("a. an event addressed to another sub-SM does not move B"),
                Get_StateName(Get_SubSmAtPath(GRoot, {1})), FString{TEXT("Ck_SmNetSibTest_N3B_Idle")});
            TestEqual(TEXT("a. the drop names the misaddressing"), Get_WatchCount(TEXT("addressed to")), 1);
            TestEqual(TEXT("a. sibling A is untouched"),
                Get_StateName(Get_SubSmAtPath(GRoot, {0})), FString{TEXT("Ck_SmNetSibTest_N3A_Host")});
            return true;
        }),
        TEXT("a. misaddressed event dropped")));

    // b. B's own address, but from a state B is not in, with no event missing before it (seq 1 after none applied).
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            const auto B = Get_SubSmAtPath(GRoot, {1});
            DoEnqueue(B, Make_Event(UCk_Utils_StateMachine_UE::Get_SubSmAddress(B),
                TEXT("Ck_SmNetSibTest_N3B_One"), TEXT("Ck_SmNetSibTest_N3B_Two"), 1));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerCase));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("b. an event from a state B is not in, with nothing missed, does not move B"),
                Get_StateName(Get_SubSmAtPath(GRoot, {1})), FString{TEXT("Ck_SmNetSibTest_N3B_Idle")});
            TestEqual(TEXT("b. the drop names where B stands"), Get_WatchCount(TEXT("nothing is missing")), 1);
            return true;
        }),
        TEXT("b. stale event dropped")));

    // c. The same event after a gap: seqs 1..4 never arrived, so B missed transitions and resyncs to the event's state.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            const auto B = Get_SubSmAtPath(GRoot, {1});
            DoEnqueue(B, Make_Event(UCk_Utils_StateMachine_UE::Get_SubSmAddress(B),
                TEXT("Ck_SmNetSibTest_N3B_One"), TEXT("Ck_SmNetSibTest_N3B_Two"), 5));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_StateName(Get_SubSmAtPath(GRoot, {1})) == TEXT("Ck_SmNetSibTest_N3B_Two");
        }),
        5.0,
        TEXT("c. B resyncs to the gapped event's state")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("c. after missed events B resyncs to the event's state"),
                Get_StateName(Get_SubSmAtPath(GRoot, {1})), FString{TEXT("Ck_SmNetSibTest_N3B_Two")});
            TestEqual(TEXT("c. the resync is logged"), Get_WatchCount(TEXT("resyncs")), 1);
            return true;
        }),
        TEXT("c. gapped event resyncs")));

    // d. A matching event (previous state = where B stands, next seq) applies as it always did, without a warning.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            Get_Watch().Reset();
            const auto B = Get_SubSmAtPath(GRoot, {1});
            DoEnqueue(B, Make_Event(UCk_Utils_StateMachine_UE::Get_SubSmAddress(B),
                TEXT("Ck_SmNetSibTest_N3B_Two"), TEXT("Ck_SmNetSibTest_N3B_One"), 6));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_StateName(Get_SubSmAtPath(GRoot, {1})) == TEXT("Ck_SmNetSibTest_N3B_One");
        }),
        5.0,
        TEXT("d. B applies the matching event")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("d. a matching event moves B"),
                Get_StateName(Get_SubSmAtPath(GRoot, {1})), FString{TEXT("Ck_SmNetSibTest_N3B_One")});
            TestEqual(TEXT("d. a matching event logs no replay check"), Get_Watch().Get_Lines().Num(), 0);
            return true;
        }),
        TEXT("d. matching event applies")));

    DoAddEnd(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachine_SubSmAddress_ReplayCheckEdgeCases,
    "Ck.StateMachine.SubSmAddress.ReplayCheckEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachine_SubSmAddress_ReplayCheckEdgeCases::RunTest(const FString& Parameters)
{
    using namespace ck_sm_subsm_address_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    constexpr auto FramesPerCase = 20;
    constexpr auto WaitSeconds = 5.0;

    struct FObservations
    {
        FCk_Handle_SmState _StateBefore;
    };
    auto Observations = MakeShared<FObservations>();

    DoAddStart(this, false);

    // Two more roots on the N3B chain (Idle -> One -> Two, sinks without the input): one WithoutHistory, one never started.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto NoHistorySpec = FCk_StateMachine_Spec{Get_AsState(TEXT("Ck_SmNetSibTest_N3B_Idle"))};
            NoHistorySpec.Set_ReplicationModel(ECk_Sm_ReplicationModel::WithoutHistory);
            DoAddRoot(this, InServer, NoHistorySpec, TEXT("Ck_SmNetSibTest_N3B_Idle"), GNoHistoryRoot);

            auto NeverStartedSpec = FCk_StateMachine_Spec{Get_AsState(TEXT("Ck_SmNetSibTest_N3B_Idle"))};
            NeverStartedSpec.Set_AutoStart(ECk_SmAutoStart::Disabled);
            DoAddRoot(this, InServer, NeverStartedSpec, TEXT("Ck_SmNetSibTest_N3B_Idle"), GNeverStartedRoot);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_StateName(GNoHistoryRoot) == TEXT("Ck_SmNetSibTest_N3B_Idle");
        }),
        WaitSeconds,
        TEXT("the WithoutHistory root stands in N3B_Idle")));

    // e. B stands in N3B_Idle; an authority that overrode N3B_Idle names the override class as the previous state.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            const auto B = Get_SubSmAtPath(GRoot, {1});
            DoEnqueue(B, Make_Event(UCk_Utils_StateMachine_UE::Get_SubSmAddress(B),
                TEXT("Ck_SmNetSibTest_N3B_IdleOverride"), TEXT("Ck_SmNetSibTest_N3B_One"), 1));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_StateName(Get_SubSmAtPath(GRoot, {1})) == TEXT("Ck_SmNetSibTest_N3B_One")
                || Get_Watch().Get_Lines().Num() > 0;
        }),
        WaitSeconds,
        TEXT("e. B applies an event from an override of where it stands")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("e. an override class of the current state stands for it"),
                Get_StateName(Get_SubSmAtPath(GRoot, {1})), FString{TEXT("Ck_SmNetSibTest_N3B_One")});
            TestEqual(TEXT("e. no replay check fired"), Get_Watch().Get_Lines().Num(), 0);
            return true;
        }),
        TEXT("e. override previous class applies")));

    // f. A null previous class (a synthesized snap) makes no claim.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            const auto B = Get_SubSmAtPath(GRoot, {1});
            auto Event = FCk_Sm_TransitionEvent{TSubclassOf<UCk_SmState_EntityScript>{}, Get_AsState(TEXT("Ck_SmNetSibTest_N3B_Two")), 2, 0};
            UCk_Utils_StateMachine_UE::Get_SubSmAddress(B).Request_WriteTo(Event);
            DoEnqueue(B, Event);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_StateName(Get_SubSmAtPath(GRoot, {1})) == TEXT("Ck_SmNetSibTest_N3B_Two")
                || Get_Watch().Get_Lines().Num() > 0;
        }),
        WaitSeconds,
        TEXT("f. B applies an event with no previous class")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("f. a null previous class moves B"),
                Get_StateName(Get_SubSmAtPath(GRoot, {1})), FString{TEXT("Ck_SmNetSibTest_N3B_Two")});
            TestEqual(TEXT("f. a null previous class makes no claim"), Get_Watch().Get_Lines().Num(), 0);
            return true;
        }),
        TEXT("f. null previous class applies")));

    // g. Seqs 3..9 never arrived and the event moves B to where it already stands: consume, do not re-enter.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([Observations](UWorld*) -> void
        {
            const auto B = Get_SubSmAtPath(GRoot, {1});
            Observations->_StateBefore = UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(B);
            DoEnqueue(B, Make_Event(UCk_Utils_StateMachine_UE::Get_SubSmAddress(B),
                TEXT("Ck_SmNetSibTest_N3B_One"), TEXT("Ck_SmNetSibTest_N3B_Two"), 10));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_LastAppliedSeq(Get_SubSmAtPath(GRoot, {1})) == 10;
        }),
        WaitSeconds,
        TEXT("g. B's watermark reaches the gapped event's seq")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerCase));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Observations]() -> bool
        {
            const auto B = Get_SubSmAtPath(GRoot, {1});
            TestEqual(TEXT("g. B stays in N3B_Two"), Get_StateName(B), FString{TEXT("Ck_SmNetSibTest_N3B_Two")});
            TestTrue(TEXT("g. B was not re-entered (same state entity)"),
                UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(B) == Observations->_StateBefore);
            TestEqual(TEXT("g. consuming in place logs no replay check"), Get_Watch().Get_Lines().Num(), 0);
            return true;
        }),
        TEXT("g. already in the new state: watermark only")));

    // h. A lagged-out ring for B (seqs 20..21 after a watermark of 10) through the root's WithHistory receive dispatch.
    //    Lagged-out recovery synthesizes one snap event; it must carry B's address or the replay check drops it.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld*) -> void
        {
            const auto B = Get_SubSmAtPath(GRoot, {1});
            const auto AddressB = UCk_Utils_StateMachine_UE::Get_SubSmAddress(B);

            auto Payload = FCk_RepData_StateMachine_WithHistory{};
            Payload.Set_RunStatus(ECk_SmRunStatus::Running);
            Payload.Get_History().Add(Make_Event(AddressB, TEXT("Ck_SmNetSibTest_N3B_Idle"), TEXT("Ck_SmNetSibTest_N3B_One"), 20));
            Payload.Get_History().Add(Make_Event(AddressB, TEXT("Ck_SmNetSibTest_N3B_Two"), TEXT("Ck_SmNetSibTest_N3B_Idle"), 21));

            const auto* Handler = FCk_PersistenceHandlerRegistry::Resolve(FCk_RepData_StateMachine_WithHistory::StaticStruct());
            if (Handler == nullptr || NOT static_cast<bool>(Handler->NetApply))
            { AddError(TEXT("h. no NetApply registered for the WithHistory payload")); return; }

            auto RootEntity = FCk_Handle{GRoot};
            Handler->NetApply(RootEntity, FInstancedStruct::Make(Payload), {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_StateName(Get_SubSmAtPath(GRoot, {1})) == TEXT("Ck_SmNetSibTest_N3B_Idle")
                || Get_WatchCount(TEXT("addressed to")) > 0;
        }),
        WaitSeconds,
        TEXT("h. B snaps to the ring's latest state (or the snap is dropped)")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("h. the lagged-out snap lands on B"),
                Get_StateName(Get_SubSmAtPath(GRoot, {1})), FString{TEXT("Ck_SmNetSibTest_N3B_Idle")});
            TestEqual(TEXT("h. the snap is not dropped as misaddressed"), Get_WatchCount(TEXT("addressed to")), 0);
            TestEqual(TEXT("h. B's watermark is the ring's latest seq"), Get_LastAppliedSeq(Get_SubSmAtPath(GRoot, {1})), 21);
            TestEqual(TEXT("h. sibling A is untouched"),
                Get_StateName(Get_SubSmAtPath(GRoot, {0})), FString{TEXT("Ck_SmNetSibTest_N3A_Host")});
            return true;
        }),
        TEXT("h. lagged-out sub-SM snap")));

    // i. WithoutHistory: mismatched previous state, nothing missing -> resync anyway (latest-state snaps to current).
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            Get_Watch().Reset();
            DoEnqueue(GNoHistoryRoot, Make_Event(FSubSmAddress{}, TEXT("Ck_SmNetSibTest_N3B_One"), TEXT("Ck_SmNetSibTest_N3B_Two"), 1));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_StateName(GNoHistoryRoot) == TEXT("Ck_SmNetSibTest_N3B_Two")
                || Get_WatchCount(TEXT("dropped")) > 0;
        }),
        WaitSeconds,
        TEXT("i. the WithoutHistory root resyncs")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Observations]() -> bool
        {
            TestEqual(TEXT("i. a WithoutHistory SM resyncs with nothing missing"),
                Get_StateName(GNoHistoryRoot), FString{TEXT("Ck_SmNetSibTest_N3B_Two")});
            TestEqual(TEXT("i. the resync names latest-state replication"), Get_WatchCount(TEXT("latest-state")), 1);
            TestEqual(TEXT("i. nothing was dropped"), Get_WatchCount(TEXT("dropped")), 0);
            Observations->_StateBefore = UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(GNoHistoryRoot);
            return true;
        }),
        TEXT("i. WithoutHistory always resyncs")));

    // j. WithoutHistory: the authority re-entered N3B_Two (a second delivery behind a multi-frame exit synthesizes
    //    One -> Two again). A new latest-state seq is always a committed transition, so it re-enters.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            Get_Watch().Reset();
            DoEnqueue(GNoHistoryRoot, Make_Event(FSubSmAddress{}, TEXT("Ck_SmNetSibTest_N3B_One"), TEXT("Ck_SmNetSibTest_N3B_Two"), 2));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            return Get_LastAppliedSeq(GNoHistoryRoot) == 2;
        }),
        WaitSeconds,
        TEXT("j. the WithoutHistory root takes the re-entry's seq")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerCase));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Observations]() -> bool
        {
            TestEqual(TEXT("j. the re-entry stays in N3B_Two"),
                Get_StateName(GNoHistoryRoot), FString{TEXT("Ck_SmNetSibTest_N3B_Two")});
            TestTrue(TEXT("j. N3B_Two was re-entered (a new state entity)"),
                ck::IsValid(UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(GNoHistoryRoot))
                && NOT (UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(GNoHistoryRoot) == Observations->_StateBefore));
            return true;
        }),
        TEXT("j. WithoutHistory re-entry re-enters")));

    // k. The never-started root has no current state; it stands at its initial state, so an event from it is no misfit.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([](UWorld*) -> void
        {
            Get_Watch().Reset();
            DoEnqueue(GNeverStartedRoot, Make_Event(FSubSmAddress{}, TEXT("Ck_SmNetSibTest_N3B_Idle"), TEXT("Ck_SmNetSibTest_N3B_One"), 1));
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesPerCase));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        {
            TestEqual(TEXT("k. a never-entered SM at its initial state passes the check"), Get_Watch().Get_Lines().Num(), 0);
            TestEqual(TEXT("k. the event was taken (watermark advanced)"), Get_LastAppliedSeq(GNeverStartedRoot), 1);
            return true;
        }),
        TEXT("k. never-entered SM stands at its initial state")));

    DoAddEnd(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachine_SubSmAddress_ADuplicateAddressIsRefused,
    "Ck.StateMachine.SubSmAddress.ADuplicateAddressIsRefused",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachine_SubSmAddress_ADuplicateAddressIsRefused::RunTest(const FString& Parameters)
{
    using namespace ck_sm_subsm_address_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    struct FObservations
    {
        FCk_Handle_StateMachine _FirstA;
        int64 _EnsuresBefore = 0;
        int64 _EnsuresAfter = 0;
    };
    auto Observations = MakeShared<FObservations>();

    DoAddStart(this, false);

    // Enter sibling A's task a second time: its new sub-SM gets the address the first A still holds.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Observations](UWorld*) -> void
        {
            Observations->_FirstA = Get_SubSmAtPath(GRoot, {0});

            auto TaskA = FCk_Handle_SmTask{};
            UCk_Utils_StateMachine_UE::RecordOfSmTasks_Utils::ForEach_ValidEntry(
                UCk_Utils_StateMachine_UE::Get_CurrentStateHandle(GRoot),
            [&](FCk_Handle_SmTask InTask) -> ECk_Record_ForEachIterationResult
            {
                if (NOT InTask.Has<ck::FFragment_SmTask_SubStateMachine>())
                { return ECk_Record_ForEachIterationResult::Continue; }

                TaskA = InTask;
                return ECk_Record_ForEachIterationResult::Break;
            });

            auto* Script = ck::IsValid(TaskA) && TaskA.Has<ck::FFragment_EntityScript>()
                ? Cast<UCk_SmTask_SubStateMachine>(TaskA.Get<ck::FFragment_EntityScript>().Get_Script().Get())
                : nullptr;
            if (Script == nullptr)
            { AddError(TEXT("sibling A's sub-SM task script is missing")); return; }

            Observations->_EnsuresBefore = UCk_Utils_Ensure_UE::Get_EnsureCount();
            Script->EnterTask(TaskA, ck::statemachine::ComputeNetContext(GRoot));
            Observations->_EnsuresAfter = UCk_Utils_Ensure_UE::Get_EnsureCount();
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Observations]() -> bool
        {
            TestEqual(TEXT("the second sub-SM at a held address ensured once"),
                Observations->_EnsuresAfter - Observations->_EnsuresBefore, static_cast<int64>(1));

            const auto FirstA = Observations->_FirstA;
            TestTrue(TEXT("the first A is still alive"), ck::IsValid(FirstA));
            TestTrue(TEXT("the first A still holds the address"),
                UCk_Utils_StateMachine_UE::TryFind_ActiveSubSm_ByAddress(GRoot, UCk_Utils_StateMachine_UE::Get_SubSmAddress(FirstA)) == FirstA);
            TestTrue(TEXT("the task still points at the first A"), Get_SubSmAtPath(GRoot, {0}) == FirstA);
            TestEqual(TEXT("the first A is still in its state"), Get_StateName(FirstA), FString{TEXT("Ck_SmNetSibTest_N3A_Host")});
            TestEqual(TEXT("nested C under the first A is untouched"),
                Get_StateName(Get_SubSmAtPath(GRoot, {0, 0})), FString{TEXT("Ck_SmNetSibTest_N3C_Idle")});
            return true;
        }),
        TEXT("duplicate address refused")));

    DoAddEnd(this);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

// Sibling sub-SMs across machines: one state hosting more than one sub-SM task.
//
// A sub-SM's transitions travel through its ROOT SM's transport, addressed so the receiver can find the matching
// local sub-SM. When the address was only the hosting state's tag path, two sub-SM tasks on one state shared it and
// every receiver resolved both to the first one: the second sibling's events moved the first sibling into a state
// class that belongs to the second, and anything nested under the first sibling was destroyed, so its later events
// were dropped ("sub-SM for identity-path ... not active under root").
//
// Each test drives one byte input (ByteAttribute.Gyms.Intent.R) up one step at a time; every step moves exactly one
// sub-SM. After each step every machine's copy of that sub-SM must be in the same state, and no relayed or replayed
// event may be dropped or resynced along the way. Topologies: Script/CkStateMachine/CkAutoTest_StateMachine_Net_SubSmSiblings.as.
//
//   N1 OwningClient_EachSiblingFollowsItsOwnEvents  - owning-client-authoritative root, Hold [A [L], B]; the owning
//      client moves B, the nested L, then A. Server and a non-owning observer must follow each one.
//   N2 ServerAuth_SiblingsFollowTheRootOnEveryMachine - server-authoritative root whose own transition replicates
//      server -> client into a state hosting two siblings; each machine's siblings derive from the replicated input.
//   N3 OwningClient_NestedPairBesideASibling - Hold [A [C, D], B]: a sibling pair at depth 2 beside a sibling.
//   N4 OwningClient_AReenteredHostingStateStartsItsSubSmsFresh - Hold [A, B] left and re-entered. An address names a
//      slot, not an instance: the re-entered A restarts its seqs at 1 while the root's ring still held the previous A's
//      events. The observer's fresh A must neither replay the dead A's history (moving B first delivers the ring to it
//      before it has published anything) nor drop the new A's first event as stale.
//
// Surface in Session Frontend: Ck.StateMachine.Net.SubSmSiblings

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/OutputDevice.h"

#include "CkAttribute/ByteAttribute/CkByteAttribute_Utils.h"
#include "CkEcs/EntityBridge/CkEntityBridge_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Utils.h"
#include "CkStateMachine/State/EntityScripts/CkSmState_EntityScript.h"
#include "CkStateMachine/Task/CkSmTask_Fragment.h"

#include "CkTests/Net/CkAutoTest_NetSubject_StateMachineSiblingSubSm.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "GameplayTagContainer.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_sm_siblings_test
{
    using PawnBaseType = ACk_AutoTest_NetSubject_StateMachineOwningClient_Pawn;

    constexpr auto k_InputTagName = TEXT("ByteAttribute.Gyms.Intent.R");
    constexpr auto k_StepTimeoutSeconds = 8.0;

    // Counts the CkStateMachine warnings that mean a relayed/replayed sub-SM event did not land as sent: the
    // "not active under root" lookup drop on either leg, and the replay where-it-stands check (drop or resync).
    class FReplayDropWatch final : public FOutputDevice
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
            if (NOT Text.Contains(TEXT("not active under root")) && NOT Text.Contains(TEXT("Replay check")))
            { return; }

            FScopeLock Lock{&_Guard};
            _Lines.Add(Text);
        }

    private:
        FCriticalSection _Guard;
        TArray<FString> _Lines;
    };

    auto Get_DropWatch() -> FReplayDropWatch&
    {
        static FReplayDropWatch Watch;
        return Watch;
    }

    // One sub-SM to check: the task ordinal path from the root (index among the current state's sub-SM tasks, in
    // record order, per level) and the state class name it must be in. AS class names drop their leading 'U'.
    struct FExpect
    {
        TArray<int32> _Path;
        FString _StateName;
        FString _Label;
    };

    struct FStep
    {
        uint8 _Input = 0;
        TArray<FExpect> _Expect;
    };

    struct FScenario
    {
        UClass* _PawnClass = nullptr;
        bool _IsOwningClientAuthoritative = true;
        int32 _NumWorlds = 3;
        TArray<FStep> _Steps;
        TArray<FExpect> _Final;

        // A step that exits a hosting state on every machine, tearing a nested sub-SM down on purpose. The root's
        // history ring still holds that sub-SM's relayed events, and leg-2 dispatch resolves the target before it
        // deduplicates, so each later ring delivery logs it as "not active under root". Not a misroute: from that
        // step on, those lines (and only those, for exactly that address) are tolerated. 0 = no teardown.
        uint8 _TeardownInput = 0;
        FString _TornDownAddress;
    };

    // The watched lines that are not the tolerated stale-ring entries of the torn-down sub-SM.
    auto Get_UnexpectedDropLines(const FScenario& InScenario, const TArray<FString>& InLines) -> TArray<FString>
    {
        return InLines.FilterByPredicate([&](const FString& InLine) -> bool
        {
            const auto IsStaleRingEntry = NOT InScenario._TornDownAddress.IsEmpty()
                && InLine.Contains(TEXT("WithHistory Apply"))
                && InLine.Contains(InScenario._TornDownAddress);
            return NOT IsStaleRingEntry;
        });
    }

    auto Find_Pawn(UWorld* InWorld, UClass* InPawnClass) -> PawnBaseType*
    {
        if (InWorld == nullptr)
        { return nullptr; }

        for (auto It = TActorIterator<PawnBaseType>(InWorld); It; ++It)
        {
            if (It->IsA(InPawnClass))
            { return *It; }
        }
        return nullptr;
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

    auto Get_StateNameAtPath(UWorld* InWorld, UClass* InPawnClass, const TArray<int32>& InPath) -> FString
    {
        const auto* Pawn = Find_Pawn(InWorld, InPawnClass);
        if (Pawn == nullptr)
        { return TEXT("<no pawn>"); }

        const auto Sm = InPath.IsEmpty() ? Pawn->_TestStateMachine : Get_SubSmAtPath(Pawn->_TestStateMachine, InPath);
        if (ck::Is_NOT_Valid(Sm))
        { return TEXT("<no sm>"); }

        const auto* CurrentClass = UCk_Utils_StateMachine_UE::Get_CurrentStateClass(Sm).Get();
        return CurrentClass != nullptr ? CurrentClass->GetName() : TEXT("<none>");
    }

    struct FWorldRef
    {
        FString _Name;
        UWorld* _World = nullptr;
    };

    auto Get_Worlds(const FScenario& InScenario) -> TArray<FWorldRef>
    {
        auto Worlds = TArray<FWorldRef>{};
        Worlds.Add(FWorldRef{TEXT("server"), ck::auto_test::net::Get_ServerWorld()});
        for (auto ClientIdx = 0; ClientIdx < InScenario._NumWorlds - 1; ++ClientIdx)
        {
            const auto Name = InScenario._IsOwningClientAuthoritative
                ? (ClientIdx == 0 ? FString{TEXT("owning client")} : FString::Printf(TEXT("observer client[%d]"), ClientIdx))
                : FString::Printf(TEXT("client[%d]"), ClientIdx);
            Worlds.Add(FWorldRef{Name, ck::auto_test::net::Get_ClientWorld(ClientIdx)});
        }
        return Worlds;
    }

    auto Get_AllMet(const FScenario& InScenario, const TArray<FExpect>& InExpect) -> bool
    {
        for (const auto& World : Get_Worlds(InScenario))
        {
            if (World._World == nullptr)
            { return false; }

            for (const auto& Expect : InExpect)
            {
                if (Get_StateNameAtPath(World._World, InScenario._PawnClass, Expect._Path) != Expect._StateName)
                { return false; }
            }
        }
        return true;
    }

    auto DoTestAll(FAutomationTestBase* InTest, const FScenario& InScenario, const TArray<FExpect>& InExpect, const FString& InWhen) -> void
    {
        for (const auto& World : Get_Worlds(InScenario))
        {
            if (World._World == nullptr)
            {
                InTest->AddError(FString::Printf(TEXT("%s: %s world unavailable"), *InWhen, *World._Name));
                continue;
            }

            for (const auto& Expect : InExpect)
            {
                InTest->TestEqual(
                    FString::Printf(TEXT("%s: %s's %s"), *InWhen, *World._Name, *Expect._Label),
                    Get_StateNameAtPath(World._World, InScenario._PawnClass, Expect._Path),
                    Expect._StateName);
            }
        }
    }

    auto DoSetInput(UWorld* InWorld, UClass* InPawnClass, uint8 InValue) -> bool
    {
        auto* Pawn = Find_Pawn(InWorld, InPawnClass);
        if (Pawn == nullptr)
        { return false; }

        const auto Entity = UCk_Utils_EntityBridge_UE::TryGet_Entity(Pawn);
        if (ck::Is_NOT_Valid(Entity))
        { return false; }

        auto InputAttr = UCk_Utils_ByteAttribute_UE::TryGet(Entity, FGameplayTag::RequestGameplayTag(FName{k_InputTagName}));
        if (ck::Is_NOT_Valid(InputAttr))
        { return false; }

        UCk_Utils_ByteAttribute_UE::Request_Override(InputAttr, InValue, ECk_MinMaxCurrent::Current, {});
        return true;
    }

    auto DoAddScenario(FAutomationTestBase* InTest, const TSharedRef<FScenario>& InScenario) -> void
    {
        constexpr auto ReadyTimeoutSeconds = 30.0f;
        constexpr auto FramesAfterSpawn = 30;
        constexpr auto FramesAfterPossess = 30;
        constexpr auto FramesAfterStart = 30;

        const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(InScenario->_NumWorlds, MapPath));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(InScenario->_NumWorlds, ReadyTimeoutSeconds));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
            FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InScenario](UWorld* InServer) -> void
            {
                GLog->AddOutputDevice(&Get_DropWatch());
                Get_DropWatch().Reset();

                auto SpawnInfo = FActorSpawnParameters{};
                SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                if (InServer->SpawnActor<PawnBaseType>(InScenario->_PawnClass, FTransform::Identity, SpawnInfo) == nullptr)
                { InTest->AddError(TEXT("server-side SpawnActor of the subject pawn returned null")); }
            })));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterSpawn));

        if (InScenario->_IsOwningClientAuthoritative)
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
                FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InScenario](UWorld* InServer) -> void
                {
                    auto* Pawn = Find_Pawn(InServer, InScenario->_PawnClass);
                    auto* ClientPC = ck::auto_test::net::Get_RemoteClientPlayerController(InServer, 0);
                    if (Pawn == nullptr || ClientPC == nullptr)
                    { InTest->AddError(TEXT("server-side pawn or remote client[0] PlayerController missing at possession")); return; }

                    ClientPC->Possess(Pawn);
                })));

            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterPossess));

            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
                FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InScenario](UWorld* InClient) -> void
                {
                    auto* Pawn = Find_Pawn(InClient, InScenario->_PawnClass);
                    if (Pawn == nullptr || ck::Is_NOT_Valid(Pawn->_TestStateMachine))
                    { InTest->AddError(TEXT("owning client's pawn or SM missing at start")); return; }

                    InTest->TestEqual(TEXT("PRECONDITION: the owning client's pawn is locally controlled by its player"),
                        static_cast<int32>(UCk_Utils_Net_UE::Get_IsActorLocallyControlled_ByPlayer(Pawn)),
                        static_cast<int32>(ECk_Utils_Net_IsLocallyControlled_Result::IsLocallyControlled));

                    UCk_Utils_StateMachine_UE::Request_Start(Pawn->_TestStateMachine, {});
                    UCk_Utils_StateMachine_UE::Acquire_RelayChannel(Pawn->_TestStateMachine);
                })));

            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterStart));
        }
        else
        {
            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterStart));
        }

        for (auto StepIdx = 0; StepIdx < InScenario->_Steps.Num(); ++StepIdx)
        {
            const auto Step = InScenario->_Steps[StepIdx];
            const auto When = FString::Printf(TEXT("after input %d"), Step._Input);

            const auto SetInput = FCk_NetAutoTest_ServerAction::CreateLambda([InTest, InScenario, Step](UWorld* InWorld) -> void
            {
                if (NOT DoSetInput(InWorld, InScenario->_PawnClass, Step._Input))
                { InTest->AddError(FString::Printf(TEXT("could not set the byte input to %d"), Step._Input)); }
            });

            if (InScenario->_IsOwningClientAuthoritative)
            { ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0, SetInput)); }
            else
            { ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(SetInput)); }

            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
                FCk_NetAutoTest_Condition::CreateLambda([InScenario, Step]() -> bool
                {
                    return Get_AllMet(*InScenario, Step._Expect);
                }),
                k_StepTimeoutSeconds,
                FString::Printf(TEXT("every machine's sub-SMs reach their states %s"), *When)));

            ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
                FCk_NetAutoTest_Assertion::CreateLambda([InTest, InScenario, Step, When]() -> bool
                {
                    DoTestAll(InTest, *InScenario, Step._Expect, When);

                    // Until a hosting state is exited on purpose, not even a stale-ring line is acceptable.
                    if (InScenario->_TeardownInput == 0 || Step._Input < InScenario->_TeardownInput)
                    {
                        const auto Lines = Get_DropWatch().Get_Lines();
                        InTest->TestEqual(FString::Printf(TEXT("%s: no relayed or replayed sub-SM event was dropped or resynced"), *When),
                            Lines.Num(), 0);
                        if (NOT Lines.IsEmpty())
                        { InTest->AddError(FString::Printf(TEXT("%s: first: %s"), *When, *Lines[0])); }
                    }
                    return true;
                }),
                When));
        }

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(30));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([InTest, InScenario]() -> bool
            {
                DoTestAll(InTest, *InScenario, InScenario->_Final, TEXT("settled"));

                const auto Lines = Get_DropWatch().Get_Lines();
                for (const auto& Line : Lines)
                { UE_LOG(LogTemp, Display, TEXT("[SubSmSiblingsDiag] %s"), *Line); }

                const auto Unexpected = Get_UnexpectedDropLines(*InScenario, Lines);
                InTest->TestEqual(TEXT("no relayed or replayed sub-SM event was dropped or resynced"), Unexpected.Num(), 0);
                if (NOT Unexpected.IsEmpty())
                { InTest->AddError(FString::Printf(TEXT("first: %s"), *Unexpected[0])); }

                return true;
            }),
            TEXT("settled state on every machine, no drops")));

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(InTest,
            FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
            {
                GLog->RemoveOutputDevice(&Get_DropWatch());
                FAutomationTestBase::bSuppressLogErrors = false;
                FAutomationTestBase::bSuppressLogWarnings = false;
                return true;
            }),
            TEXT("restore log suppression statics")));
    }

    auto MakeExpect(TArray<int32> InPath, const TCHAR* InState, const TCHAR* InLabel) -> FExpect
    {
        return FExpect{MoveTemp(InPath), FString{InState}, FString{InLabel}};
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_SubSmSiblings_OwningClient_EachSiblingFollowsItsOwnEvents,
    "Ck.StateMachine.Net.SubSmSiblings.OwningClient_EachSiblingFollowsItsOwnEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_SubSmSiblings_OwningClient_EachSiblingFollowsItsOwnEvents::RunTest(const FString& Parameters)
{
    using namespace ck_sm_siblings_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    // Hold [ A [ L ], B ]. Paths: A = {0}, B = {1}, L = {0, 0}.
    auto Scenario = MakeShared<FScenario>();
    Scenario->_PawnClass = ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N1_Pawn::StaticClass();
    Scenario->_IsOwningClientAuthoritative = true;
    Scenario->_NumWorlds = 3;
    // Input 5 moves A out of N1A_Host on every machine, which tears the nested L down on purpose.
    Scenario->_TeardownInput = 5;
    Scenario->_TornDownAddress = TEXT("(Ck.SmNetSibTest.N1.Hold#0 > Ck.SmNetSibTest.N1A.Host#0)");
    Scenario->_Steps =
    {
        FStep{1, {MakeExpect({1}, TEXT("Ck_SmNetSibTest_N1B_One"), TEXT("sibling B")),
                  MakeExpect({0}, TEXT("Ck_SmNetSibTest_N1A_Host"), TEXT("sibling A"))}},
        FStep{2, {MakeExpect({0, 0}, TEXT("Ck_SmNetSibTest_N1L_One"), TEXT("nested L under A"))}},
        FStep{3, {MakeExpect({1}, TEXT("Ck_SmNetSibTest_N1B_Two"), TEXT("sibling B"))}},
        FStep{4, {MakeExpect({0, 0}, TEXT("Ck_SmNetSibTest_N1L_Two"), TEXT("nested L under A")),
                  MakeExpect({0}, TEXT("Ck_SmNetSibTest_N1A_Host"), TEXT("sibling A"))}},
        FStep{5, {MakeExpect({0}, TEXT("Ck_SmNetSibTest_N1A_One"), TEXT("sibling A"))}},
        FStep{6, {MakeExpect({0}, TEXT("Ck_SmNetSibTest_N1A_Two"), TEXT("sibling A"))}},
    };
    Scenario->_Final =
    {
        MakeExpect({}, TEXT("Ck_SmNetSibTest_N1_Hold"), TEXT("root")),
        MakeExpect({0}, TEXT("Ck_SmNetSibTest_N1A_Two"), TEXT("sibling A")),
        MakeExpect({1}, TEXT("Ck_SmNetSibTest_N1B_Two"), TEXT("sibling B")),
    };

    DoAddScenario(this, Scenario);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_SubSmSiblings_ServerAuth_SiblingsFollowTheRootOnEveryMachine,
    "Ck.StateMachine.Net.SubSmSiblings.ServerAuth_SiblingsFollowTheRootOnEveryMachine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_SubSmSiblings_ServerAuth_SiblingsFollowTheRootOnEveryMachine::RunTest(const FString& Parameters)
{
    using namespace ck_sm_siblings_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    // Wait --(1)--> Hold [ A, B ]. The root's transition replicates server -> client; the siblings derive per machine.
    auto Scenario = MakeShared<FScenario>();
    Scenario->_PawnClass = ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N2_Pawn::StaticClass();
    Scenario->_IsOwningClientAuthoritative = false;
    Scenario->_NumWorlds = 2;
    Scenario->_Steps =
    {
        FStep{1, {MakeExpect({}, TEXT("Ck_SmNetSibTest_N2_Hold"), TEXT("root")),
                  MakeExpect({0}, TEXT("Ck_SmNetSibTest_N2A_Idle"), TEXT("sibling A")),
                  MakeExpect({1}, TEXT("Ck_SmNetSibTest_N2B_Idle"), TEXT("sibling B"))}},
        FStep{2, {MakeExpect({0}, TEXT("Ck_SmNetSibTest_N2A_One"), TEXT("sibling A"))}},
        FStep{3, {MakeExpect({1}, TEXT("Ck_SmNetSibTest_N2B_One"), TEXT("sibling B"))}},
        FStep{4, {MakeExpect({0}, TEXT("Ck_SmNetSibTest_N2A_Two"), TEXT("sibling A"))}},
        FStep{5, {MakeExpect({1}, TEXT("Ck_SmNetSibTest_N2B_Two"), TEXT("sibling B"))}},
    };
    Scenario->_Final =
    {
        MakeExpect({}, TEXT("Ck_SmNetSibTest_N2_Hold"), TEXT("root")),
        MakeExpect({0}, TEXT("Ck_SmNetSibTest_N2A_Two"), TEXT("sibling A")),
        MakeExpect({1}, TEXT("Ck_SmNetSibTest_N2B_Two"), TEXT("sibling B")),
    };

    DoAddScenario(this, Scenario);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_SubSmSiblings_OwningClient_NestedPairBesideASibling,
    "Ck.StateMachine.Net.SubSmSiblings.OwningClient_NestedPairBesideASibling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_SubSmSiblings_OwningClient_NestedPairBesideASibling::RunTest(const FString& Parameters)
{
    using namespace ck_sm_siblings_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    // Hold [ A [ C, D ], B ]. Paths: A = {0}, B = {1}, C = {0, 0}, D = {0, 1}.
    auto Scenario = MakeShared<FScenario>();
    Scenario->_PawnClass = ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N3_Pawn::StaticClass();
    Scenario->_IsOwningClientAuthoritative = true;
    Scenario->_NumWorlds = 3;
    Scenario->_Steps =
    {
        FStep{1, {MakeExpect({1}, TEXT("Ck_SmNetSibTest_N3B_One"), TEXT("sibling B")),
                  MakeExpect({0}, TEXT("Ck_SmNetSibTest_N3A_Host"), TEXT("sibling A"))}},
        FStep{2, {MakeExpect({0, 0}, TEXT("Ck_SmNetSibTest_N3C_One"), TEXT("nested C")),
                  MakeExpect({0, 1}, TEXT("Ck_SmNetSibTest_N3D_Idle"), TEXT("nested D"))}},
        FStep{3, {MakeExpect({0, 1}, TEXT("Ck_SmNetSibTest_N3D_One"), TEXT("nested D")),
                  MakeExpect({0, 0}, TEXT("Ck_SmNetSibTest_N3C_One"), TEXT("nested C"))}},
        FStep{4, {MakeExpect({0, 0}, TEXT("Ck_SmNetSibTest_N3C_Two"), TEXT("nested C"))}},
        FStep{5, {MakeExpect({0, 1}, TEXT("Ck_SmNetSibTest_N3D_Two"), TEXT("nested D"))}},
        FStep{6, {MakeExpect({1}, TEXT("Ck_SmNetSibTest_N3B_Two"), TEXT("sibling B"))}},
    };
    Scenario->_Final =
    {
        MakeExpect({}, TEXT("Ck_SmNetSibTest_N3_Hold"), TEXT("root")),
        MakeExpect({0}, TEXT("Ck_SmNetSibTest_N3A_Host"), TEXT("sibling A")),
        MakeExpect({0, 0}, TEXT("Ck_SmNetSibTest_N3C_Two"), TEXT("nested C")),
        MakeExpect({0, 1}, TEXT("Ck_SmNetSibTest_N3D_Two"), TEXT("nested D")),
        MakeExpect({1}, TEXT("Ck_SmNetSibTest_N3B_Two"), TEXT("sibling B")),
    };

    DoAddScenario(this, Scenario);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkStateMachineNet_SubSmSiblings_OwningClient_AReenteredHostingStateStartsItsSubSmsFresh,
    "Ck.StateMachine.Net.SubSmSiblings.OwningClient_AReenteredHostingStateStartsItsSubSmsFresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkStateMachineNet_SubSmSiblings_OwningClient_AReenteredHostingStateStartsItsSubSmsFresh::RunTest(const FString& Parameters)
{
    using namespace ck_sm_siblings_test;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    // Hold [ A, B ] --(=3)--> Away --(=4)--> Hold. Paths: A = {0}, B = {1}.
    auto Scenario = MakeShared<FScenario>();
    Scenario->_PawnClass = ACk_AutoTest_NetSubject_StateMachineSiblingSubSm_N4_Pawn::StaticClass();
    Scenario->_IsOwningClientAuthoritative = true;
    Scenario->_NumWorlds = 3;
    // Input 3 leaves Hold on every machine; the first A's relayed events are in the ring while the root is Away.
    Scenario->_TeardownInput = 3;
    Scenario->_TornDownAddress = TEXT("(Ck.SmNetSibTest.N4.Hold#0)");
    Scenario->_Steps =
    {
        FStep{1, {MakeExpect({0}, TEXT("Ck_SmNetSibTest_N4A_One"), TEXT("sibling A")),
                  MakeExpect({1}, TEXT("Ck_SmNetSibTest_N4B_Idle"), TEXT("sibling B"))}},
        FStep{2, {MakeExpect({0}, TEXT("Ck_SmNetSibTest_N4A_Two"), TEXT("sibling A"))}},
        FStep{3, {MakeExpect({}, TEXT("Ck_SmNetSibTest_N4_Away"), TEXT("root"))}},
        FStep{4, {MakeExpect({}, TEXT("Ck_SmNetSibTest_N4_Hold"), TEXT("root")),
                  MakeExpect({0}, TEXT("Ck_SmNetSibTest_N4A_Idle"), TEXT("re-entered sibling A")),
                  MakeExpect({1}, TEXT("Ck_SmNetSibTest_N4B_Idle"), TEXT("re-entered sibling B"))}},
        // B's event is the first ring delivery to reach the fresh A; it must not replay the first A's history.
        FStep{5, {MakeExpect({1}, TEXT("Ck_SmNetSibTest_N4B_One"), TEXT("re-entered sibling B")),
                  MakeExpect({0}, TEXT("Ck_SmNetSibTest_N4A_Idle"), TEXT("re-entered sibling A"))}},
        // The fresh A's seq 1: must land, not be filtered or dropped as stale.
        FStep{6, {MakeExpect({0}, TEXT("Ck_SmNetSibTest_N4A_One"), TEXT("re-entered sibling A"))}},
    };
    Scenario->_Final =
    {
        MakeExpect({}, TEXT("Ck_SmNetSibTest_N4_Hold"), TEXT("root")),
        MakeExpect({0}, TEXT("Ck_SmNetSibTest_N4A_One"), TEXT("re-entered sibling A")),
        MakeExpect({1}, TEXT("Ck_SmNetSibTest_N4B_One"), TEXT("re-entered sibling B")),
    };

    DoAddScenario(this, Scenario);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

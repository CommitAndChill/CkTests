#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkHexRulesTestListener.h"
#include "CkGrid/HexGridSystem/Blocker/CkHexGridBlocker_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Cell/CkHexCell_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "NativeGameplayTags.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_hex_rules_test
{
    using Grid = UCk_Utils_HexGrid_UE;
    using Cell = UCk_Utils_HexCell_UE;
    using Blocker = UCk_Utils_HexGridBlocker_UE;
    using Object = UCk_Utils_HexGridObject_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    using Result = ECk_Request_OperationResult;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Ground, "Ck.Test.HexRules.Ground");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Stone, "Ck.Test.HexRules.Ground.Stone");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Hazard, "Ck.Test.HexRules.Hazard");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(Lava, "Ck.Test.HexRules.Hazard.Lava");

    struct FCase
    {
        FCk_Handle Owner;
        FCk_Handle_HexGrid Board;
        FCk_Handle_HexCell Center, Base, Manual;
        FCk_Handle_HexGridBlocker A, B;
        FCk_Handle_HexGridObject Shape;
        TStrongObjectPtr<UCk_HexRulesTestListener> Listener;
        FCk_Delegate_Request_OnCompleted Callback;
        TArray<Result> Results;
        TArray<FCk_Handle> Owners;
        TArray<bool> Active;
        TArray<bool> Blocked;
        TFunction<void()> AfterObservation;

        bool Init(UWorld* World)
        {
            Owner = Life::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
            FCk_HexDomain Domain;
            Domain.Set_RingRadius(1);
            FCk_HexGrid_Spec Spec;
            Spec.Set_Domain(Domain);
            Spec.Set_DefaultCellTags(FGameplayTagContainer{Stone});
            Spec.Set_InitialDisabledCells({FCk_HexCoord{1,0}});
            ECk_HexBoardFailure Failure{};
            Listener.Reset(NewObject<UCk_HexRulesTestListener>());
            Listener->Observe = [this](FCk_Handle Who, Result Outcome)
            {
                Results.Add(Outcome);
                Owners.Add(Who);
                Active.Add(ck::IsValid(A) && Blocker::Get_IsActive(A));
                bool IsBlocked = false;
                Cell::TryGet_IsDisabled(Center, IsBlocked);
                Blocked.Add(IsBlocked);
                if (AfterObservation) { AfterObservation(); }
            };
            Callback.BindDynamic(Listener.Get(), &UCk_HexRulesTestListener::OnCompleted);
            return Grid::TryCreate(Owner, FTransform::Identity, Spec, Board, Failure)
                && Grid::TryGet_CellAt(Board, {}, Center, Failure) && Grid::TryGet_CellAt(Board, {1,0}, Base, Failure)
                && Grid::TryGet_CellAt(Board, {-1,0}, Manual, Failure);
        }

        bool MakeBlocker(FCk_Handle_HexGridBlocker& Out, bool InitiallyActive)
        {
            FCk_HexGridBlocker_Spec Spec;
            Spec.Set_Grid(Board);
            Spec.Set_Cells({FCk_HexCoord{}, FCk_HexCoord{1,0}, FCk_HexCoord{-1,0}});
            Spec.Set_InitiallyActive(InitiallyActive);
            ECk_HexGridBlocker_Failure Failure{};
            return Blocker::TryCreate(Board, Spec, Out, Failure);
        }
    };

    void Start()
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1,30.f));
    }

    void Step(TFunction<void()> Body)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [Body=MoveTemp(Body)](UWorld*) { Body(); })));
    }

    void Wait(FAutomationTestBase& T, TFunction<bool()> Until, const TCHAR* Message)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(&T, FCk_NetAutoTest_Condition::CreateLambda(
            [Until=MoveTemp(Until)](){ return Until(); }), 10.f, Message));
    }

    TSharedRef<FCase> Begin(FAutomationTestBase& T)
    {
        Start();
        auto S = MakeShared<FCase>();
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [S,&T](UWorld* World){ T.TestTrue(TEXT("Real board and cells"), S->Init(World)); })));
        return S;
    }

    void End()
    { ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE()); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRulesOrdered,"Ck.HexGrid.Rules.OrderedToggleCompletion",ck_hex_rules_test::Flags)
bool FCkHexRulesOrdered::RunTest(const FString&)
{
    using namespace ck_hex_rules_test;
    auto S = Begin(*this);
    Step([S,this]()
    {
        if (!TestTrue(TEXT("Inactive contributor"), S->MakeBlocker(S->A,false))) { return; }
        S->AfterObservation = [P=&S.Get(),this]()
        {
            if (P->Results.Num() == 1)
            {
                TestTrue(TEXT("Reentrant composition succeeds"), P->MakeBlocker(P->B,true));
                TestFalse(TEXT("New contribution is pending during callback"), Blocker::Get_IsActive(P->B));
            }
            else if (P->Results.Num() <= 4)
            { TestFalse(TEXT("Pending activation stays behind the current batch"), Blocker::Get_IsActive(P->B)); }
        };
        for (bool Turn : {false,true,false,false}) { Blocker::Request_SetActive(S->A,Turn,S->Callback); }
    });
    Wait(*this,[S](){return S->Results.Num() >= 4 && Blocker::Get_IsActive(S->B);},TEXT("Four completions and deferred activation"));
    Step([S,this]()
    {
        TestTrue(TEXT("Visible state in each callback includes both no-ops"), S->Active == TArray<bool>{false,true,false,false}
            && S->Blocked == S->Active);
        TestEqual(TEXT("Exactly four completions"),S->Results.Num(),4);
        for (int32 I=0; I<S->Results.Num(); ++I)
        { TestTrue(TEXT("Successful original blocker completion"), S->Owners[I]==S->A && S->Results[I]==Result::Succeeded); }
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRulesVotes,"Ck.HexGrid.Rules.VoteOwnership",ck_hex_rules_test::Flags)
bool FCkHexRulesVotes::RunTest(const FString&)
{
    using namespace ck_hex_rules_test;
    auto S = Begin(*this);
    for (bool Reverse : {false,true})
    {
        Step([S,this]()
        {
            TestTrue(TEXT("Two overlapping blockers"),
                S->MakeBlocker(S->A,true) && S->MakeBlocker(S->B,true));
            Cell::Request_SetBlocked(S->Manual,true,{});
        });
        Wait(*this,[S](){return Blocker::Get_IsActive(S->A) && Blocker::Get_IsActive(S->B);},TEXT("Both votes committed"));
        Step([S,Reverse](){auto First=Reverse?S->B:S->A; Life::Request_DestroyEntity(First);});
        Wait(*this,[S](){return Blocker::Get_Blockers(S->Board).Num()==1;},TEXT("First contributor released"));
        Step([S,Reverse,this]()
        {
            bool Blocked=false;
            TestTrue(TEXT("Surviving contributor keeps cell blocked"),Cell::TryGet_IsDisabled(S->Center,Blocked) && Blocked);
            auto Second=Reverse?S->A:S->B; Blocker::Request_SetActive(Second,false,{});
            Blocker::Request_SetActive(Second,false,{});
        });
        Wait(*this,[S](){bool V=true; return Cell::TryGet_IsDisabled(S->Center,V) && !V;},TEXT("Final contributor removed"));
        Step([S,Reverse,this]()
        {
            bool V=false;
            TestTrue(TEXT("Base vote survives"),Cell::TryGet_IsDisabled(S->Base,V) && V);
            TestTrue(TEXT("Manual vote survives"),Cell::TryGet_IsDisabled(S->Manual,V) && V);
            auto Second=Reverse?S->A:S->B; Life::Request_DestroyEntity(Second);
            Cell::Request_SetBlocked(S->Manual,false,{});
        });
        Wait(*this,[S](){bool V=true; return Blocker::Get_Blockers(S->Board).IsEmpty() &&
            Cell::TryGet_IsDisabled(S->Manual,V) && !V;},TEXT("Inactive teardown and manual release"));
    }
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRulesCancel,"Ck.HexGrid.Rules.TeardownAndCancellation",ck_hex_rules_test::Flags)
bool FCkHexRulesCancel::RunTest(const FString&)
{
    using namespace ck_hex_rules_test;
    auto S=Begin(*this);
    Step([S,this]()
    {
        if (!TestTrue(TEXT("Pending contributor created"),S->MakeBlocker(S->A,true))) { return; }
        Blocker::Request_SetActive(S->A,true,S->Callback);
        auto Target=S->A; Life::Request_DestroyEntity(Target);
    });
    Wait(*this,[S](){return !S->Results.IsEmpty() && !ck::IsValid(S->A);},TEXT("Target-first cancellation"));
    Step([S,this]()
    {
        TestTrue(TEXT("Dead blocker omitted"),Blocker::Get_Blockers(S->Board).IsEmpty());
        if (!TestTrue(TEXT("Second blocker created"),S->MakeBlocker(S->B,false))) { return; }
        FCk_HexGridBlocker_Spec External;
        External.Set_Grid(S->Board);
        External.Set_Cells({FCk_HexCoord{}});
        ECk_HexGridBlocker_Failure Failure{};
        FCk_Handle_HexGridBlocker Surviving;
        if (!TestTrue(TEXT("Externally owned blocker created"),
            Blocker::TryCreate(S->Owner, External, Surviving, Failure))) { return; }
        Cell::Request_SetBlocked(S->Center,true,S->Callback);
        Blocker::Request_SetActive(S->B,true,S->Callback);
        auto Board=S->Board; Life::Request_DestroyEntity(Board);
        TestTrue(TEXT("Surviving contributor immediately hides retiring board"),
            ck::IsValid(Surviving) && ck::Is_NOT_Valid(Blocker::Get_Grid(Surviving)));
    });
    Wait(*this,[S](){return S->Results.Num()>=3 && !ck::IsValid(S->Center) && !ck::IsValid(S->B);},TEXT("Board-first cancellations and private-cell retirement"));
    Step([S,this]()
    {
        TestTrue(TEXT("Every submission cancelled once"),S->Results==TArray<Result>{Result::Failed_Cancelled,Result::Failed_Cancelled,Result::Failed_Cancelled});
        TestTrue(TEXT("Cancellation retained original owner"),S->Owners.Num()==3 && S->Owners[0]==S->A
            && S->Owners.Contains(S->Center) && S->Owners.Contains(S->B));
        bool V=true; TestTrue(TEXT("Stale query safe and cleared"),!Cell::TryGet_IsDisabled(S->Center,V) && !V);
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRulesTags,"Ck.HexGrid.Rules.EffectiveTags",ck_hex_rules_test::Flags)
bool FCkHexRulesTags::RunTest(const FString&)
{
    using namespace ck_hex_rules_test;
    auto S=Begin(*this);
    Step([S,this]()
    {
        FCk_HexGridObject_Spec Spec;
        Spec.Set_FootprintOffsets({FCk_HexCoord{},FCk_HexCoord{1,0}});
        Spec.Set_RequiredCellTags(FGameplayTagContainer{Ground});
        Spec.Set_ForbiddenCellTags(FGameplayTagContainer{Hazard});
        ECk_HexGridObject_Failure F{};
        TestTrue(TEXT("Shape created"),Object::TryCreate(S->Owner,Spec,S->Shape,F));
        Cell::Request_AddTag(S->Center,Stone,{});
        Cell::Request_RemoveTag(S->Center,Stone,S->Callback);
        Cell::Request_AddTag(S->Base,Lava,S->Callback);
    });
    Wait(*this,[S](){return S->Results.Num()>=2;},TEXT("Ordered local tag commands drained"));
    Step([S,this]()
    {
        FGameplayTagContainer Local,Tags;
        TestTrue(TEXT("Removing local contribution preserves default"),
            Cell::TryGet_LocalTags(S->Center,Local) && Local.IsEmpty() && Cell::TryGet_Tags(S->Center,Tags) && Tags.HasTagExact(Stone));
        auto Spec=Object::Get_Spec(S->Shape);
        auto Resolved=Object::TryResolve_Cells(S->Shape,{},ECk_HexRotation::Turn0,ECk_HexGridConnectivity::Ignore);
        int32 Allowed=0, Forbidden=0;
        for (const auto& Coord : Resolved.Get_Cells())
        {
            FCk_Handle_HexCell C; ECk_HexBoardFailure F{};
            if (Grid::TryGet_CellAt(S->Board,Coord,C,F) && Cell::TryGet_Tags(C,Tags))
            {
                Allowed+=Tags.HasAll(Spec.Get_RequiredCellTags())?1:0;
                Forbidden+=Tags.HasAny(Spec.Get_ForbiddenCellTags())?1:0;
            }
        }
        TestEqual(TEXT("Both cells match required parent"),Allowed,2);
        TestEqual(TEXT("Only the second cell has a forbidden descendant"),Forbidden,1);
        TestTrue(TEXT("Original cell receives successful tag completion"),S->Owners.Num()==2 &&
            S->Owners[0]==S->Center && S->Owners[1]==S->Base && S->Results==TArray<Result>{Result::Succeeded,Result::Succeeded});
    });
    End(); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRulesAS,"Ck.HexGrid.Rules.AS_Consumer",ck_hex_rules_test::Flags)
bool FCkHexRulesAS::RunTest(const FString&)
{
    ck_hex_rules_test::Start();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunAsTestOnAllWorlds(this,TEXT("/Script/Angelscript.Ck_AutoTest_HexGrid_Rules"),30.f));
    ck_hex_rules_test::End(); return true;
}
#endif

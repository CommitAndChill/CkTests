#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkHexPlacementBlueprintProbe.h"
#include "CkTest_HexBlueprintGraph.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MakeStruct.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_hex_placement_bp
{
    using Probe = UCkHexPlacementBlueprintProbe;
    using Place = UCk_Utils_HexGridPlacement_UE;
    const FName Receiver = TEXT("ReceivePlacement");

    template <typename T>
    T* StructNode(UEdGraph& Graph, UScriptStruct* Type)
    {
        FGraphNodeCreator<T> Creator(Graph);
        auto* Node = Creator.CreateNode();
        Node->StructType = Type;
        Creator.Finalize();
        return Node;
    }

    bool Link(FAutomationTestBase& Test, UEdGraph& Graph, UEdGraphPin* A, UEdGraphPin* B, const TCHAR* Label)
    {
        return Test.TestTrue(Label, A && B && Graph.GetSchema()->TryCreateConnection(A, B));
    }

    bool AddReceiver(FAutomationTestBase& Test, UBlueprint& BP)
    {
        auto* Prop = FindFProperty<FDelegateProperty>(FCk_Request_HexGridPlacement_Place::StaticStruct(), TEXT("_OnResult"));
        if (!Test.TestNotNull(TEXT("Typed delegate reflected"), Prop)) { return false; }
        auto* Graph = FBlueprintEditorUtils::CreateNewGraph(&BP, Receiver, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph(&BP, Graph, true, Prop->SignatureFunction.Get());
        TArray<UK2Node_FunctionEntry*> Entries;
        Graph->GetNodesOfClass(Entries);
        if (Entries.Num() != 1) { return false; }
        auto* Break = StructNode<UK2Node_BreakStruct>(*Graph, FCk_HexGridPlacement_Result::StaticStruct());
        FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*Graph);
        auto* Branch = BranchCreator.CreateNode();
        BranchCreator.Finalize();
        auto Call = [&](FName Fn)
        {
            FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
            auto* Node = Creator.CreateNode();
            Node->SetFromFunction(Probe::StaticClass()->FindFunctionByName(Fn));
            Creator.Finalize();
            return Node;
        };
        auto* Ok = Call(TEXT("AcceptPlacement"));
        auto* Failed = Call(TEXT("RejectPlacement"));
        return Link(Test,*Graph,Entries[0]->FindPin(TEXT("InResult")),Break->Pins[0],TEXT("Break terminal result"))
            && Link(Test,*Graph,Entries[0]->FindPin(UEdGraphSchema_K2::PN_Then),Branch->GetExecPin(),TEXT("Dispatch result"))
            && Link(Test,*Graph,Break->FindPin(TEXT("_Succeeded")),Branch->GetConditionPin(),TEXT("Branch on succeeded"))
            && Link(Test,*Graph,Branch->GetThenPin(),Ok->GetExecPin(),TEXT("Accept branch"))
            && Link(Test,*Graph,Branch->GetElsePin(),Failed->GetExecPin(),TEXT("Reject branch"))
            && Link(Test,*Graph,Break->FindPin(TEXT("_PlacementId")),Ok->FindPin(TEXT("InId")),TEXT("Store durable GUID"))
            && Link(Test,*Graph,Break->FindPin(TEXT("_Failure")),Failed->FindPin(TEXT("InFailure")),TEXT("Read typed failure"));
    }

    bool AddRequest(FAutomationTestBase& Test, UBlueprint& BP, FName Name, UScriptStruct* Type,
        std::initializer_list<FName> Fields)
    {
        auto* Fn = Place::StaticClass()->FindFunctionByName(Name);
        if (!Fn || !ck_test_hex_geometry_blueprint::AddGraph(Test,BP,*Fn)) { return false; }
        auto* Graph = BP.FunctionGraphs.Last().Get();
        TArray<UK2Node_FunctionEntry*> Entries;
        TArray<UK2Node_CallFunction*> Calls;
        Graph->GetNodesOfClass(Entries);
        Graph->GetNodesOfClass(Calls);
        if (Entries.Num()!=1 || Calls.Num()!=1) { return false; }
        auto* RequestPin = Calls[0]->FindPin(TEXT("InRequest"));
        if (!Test.TestNotNull(TEXT("Request input"), RequestPin)) { return false; }
        RequestPin->BreakAllPinLinks();
        auto* Make = StructNode<UK2Node_MakeStruct>(*Graph, Type);
        auto* Break = StructNode<UK2Node_BreakStruct>(*Graph, Type);
        if (!Link(Test,*Graph,Make->Pins[0],RequestPin,TEXT("Make request in Blueprint"))
            || !Link(Test,*Graph,Entries[0]->FindPin(TEXT("InRequest")),Break->Pins[0],TEXT("Read request arguments"))) { return false; }
        for (FName Field : Fields)
        {
            if (!Link(Test,*Graph,Break->FindPin(Field),Make->FindPin(Field),*Field.ToString())) { return false; }
        }
        FGraphNodeCreator<UK2Node_CreateDelegate> Creator(*Graph);
        auto* Bind = Creator.CreateNode();
        Creator.Finalize();
        if (!Link(Test,*Graph,Bind->GetDelegateOutPin(),Make->FindPin(TEXT("_OnResult")),TEXT("Bind typed terminal delegate")))
        { return false; }
        Bind->SetFunction(Receiver);
        return true;
    }

    struct FScenario
    {
        TStrongObjectPtr<UBlueprint> BP;
        TStrongObjectPtr<Probe> Instance;
        FCk_Handle_HexGrid Board;
        FCk_Handle_HexGridObject Object;
        FGuid Id;
    };

    template <typename R>
    void Submit(FAutomationTestBase& Test, FScenario& S, FName Name, R Request)
    {
        if (Test.HasAnyErrors()) { return; }
        auto* Fn = S.Instance->FindFunction(ck_test_hex_geometry_blueprint::GraphName(Name));
        if (!Test.TestTrue(TEXT("Compiled submission graph"), Fn && !Fn->Script.IsEmpty())) { return; }
        FStructOnScope Stack(Fn);
        auto* Board = FindFProperty<FStructProperty>(Fn,TEXT("InGrid"));
        auto* Req = FindFProperty<FStructProperty>(Fn,TEXT("InRequest"));
        if (!Test.TestTrue(TEXT("Typed graph inputs"), Board && Board->Struct == FCk_Handle_HexGrid::StaticStruct() && Req && Req->Struct == R::StaticStruct())) { return; }
        Board->CopyCompleteValue(Board->ContainerPtrToValuePtr<void>(Stack.GetStructMemory()), &S.Board);
        Req->CopyCompleteValue(Req->ContainerPtrToValuePtr<void>(Stack.GetStructMemory()), &Request);
        S.Instance->ProcessEvent(Fn,Stack.GetStructMemory());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPlacementBP,"Ck.HexGrid.Placement.Blueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexPlacementBP::RunTest(const FString&)
{
    using namespace ck_hex_placement_bp;
    auto S = MakeShared<FScenario>();
    S->BP.Reset(FKismetEditorUtilities::CreateBlueprint(Probe::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("HexPlacementFixture")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    if (!S->BP.IsValid() || !AddReceiver(*this,*S->BP)
        || !AddRequest(*this,*S->BP,TEXT("Request_Place"),FCk_Request_HexGridPlacement_Place::StaticStruct(),{TEXT("_Spec")})
        || !AddRequest(*this,*S->BP,TEXT("Request_Move"),FCk_Request_HexGridPlacement_Move::StaticStruct(),{TEXT("_PlacementId"),TEXT("_Anchor"),TEXT("_Rotation")})
        || !AddRequest(*this,*S->BP,TEXT("Request_Remove"),FCk_Request_HexGridPlacement_Remove::StaticStruct(),{TEXT("_PlacementId")})) { return false; }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(S->BP.Get());
    FKismetEditorUtilities::CompileBlueprint(S->BP.Get());
    if (!TestTrue(TEXT("Placement and terminal branch graphs compile"), S->BP->Status == BS_UpToDate)) { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1,TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1,30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,S](UWorld* World)
        {
            S->Instance.Reset(NewObject<Probe>(GetTransientPackage(), S->BP->GeneratedClass));
            auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Owner,FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                ECk_Net_NetModeType::ClientAndHost,ECk_Net_EntityNetRole::Authority});
            ECk_HexBoardFailure BoardFailure{};
            ECk_HexGridObject_Failure ShapeFailure{};
            FCk_HexGrid_Spec BoardSpec;
            FCk_HexDomain Domain;
            Domain.Set_RingRadius(1);
            BoardSpec.Set_Domain(Domain);
            FCk_HexGridObject_Spec Shape;
            Shape.Set_FootprintOffsets({FCk_HexCoord{}});
            if (!TestTrue(TEXT("Compose Blueprint fixture"), UCk_Utils_HexGrid_UE::TryCreate(Owner,FTransform::Identity,BoardSpec,S->Board,BoardFailure)
                && UCk_Utils_HexGridObject_UE::TryCreate(Owner,Shape,S->Object,ShapeFailure))) { return; }
            FCk_Delegate_HexGridPlacement_ObjectPlaced Signal;
            Signal.BindDynamic(S->Instance.Get(), &Probe::OnPlaced);
            Place::BindTo_OnObjectPlaced(S->Board, Signal);
            Submit(*this,*S,TEXT("Request_Place"),FCk_Request_HexGridPlacement_Place{FCk_HexGridPlacement_Spec{}});
        })));
    auto Step = [this,S](int32 Accepted, int32 Rejected, TFunction<void()> Action)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,FCk_NetAutoTest_Condition::CreateLambda(
            [this,S,Accepted,Rejected](){ return HasAnyErrors() || (S->Instance.IsValid() && S->Instance->Accepted==Accepted && S->Instance->Rejected==Rejected); }),
            10.f,TEXT("Blueprint terminal branch reached")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [this,Action](UWorld*){ if (!HasAnyErrors()) { Action(); } })));
    };
    Step(0,1,[this,S]()
    {
        TestTrue(TEXT("Typed rejection did not emit placement signal"), S->Instance->Failure==ECk_HexGridFailure::InvalidObject && S->Instance->PlacedSignals==0);
        FCk_HexGridPlacement_Spec Spec;
        Spec.Set_Object(S->Object);
        Submit(*this,*S,TEXT("Request_Place"),FCk_Request_HexGridPlacement_Place{Spec});
    });
    Step(1,1,[this,S]()
    {
        S->Id = S->Instance->Id;
        TestTrue(TEXT("Blueprint stored admitted GUID"),S->Id.IsValid() && S->Instance->PlacedSignals == 1);
        Submit(*this,*S,TEXT("Request_Move"),FCk_Request_HexGridPlacement_Move{S->Id,{1,0},ECk_HexRotation::Turn0});
    });
    Step(2,1,[this,S]()
    {
        TestEqual(TEXT("Move retains Blueprint GUID"),S->Id,S->Instance->Id);
        Submit(*this,*S,TEXT("Request_Remove"),FCk_Request_HexGridPlacement_Remove{S->Id});
    });
    Step(3,1,[this,S](){ TestEqual(TEXT("Remove delivers captured GUID"), S->Id,S->Instance->Id); });
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}
#endif

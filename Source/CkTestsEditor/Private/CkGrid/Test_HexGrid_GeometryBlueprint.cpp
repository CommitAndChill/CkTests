#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_HexBlueprintGraph.h"

#include "CkCore/Macros/CkMacros.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include <initializer_list>

namespace ck_test_hex_geometry_blueprint
{
    auto
        GraphName(
            FName InFunction) -> FName
    {
        return FName{FString{TEXT("Exercise_")} + InFunction.ToString()};
    }

    auto
        AddGraph(
            FAutomationTestBase& InTest,
            UBlueprint& InBlueprint,
            UFunction& InFunction) -> bool
    {
        auto* Graph = FBlueprintEditorUtils::CreateNewGraph(&InBlueprint, GraphName(InFunction.GetFName()),
            UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph(&InBlueprint, Graph, true, &InFunction);
        TArray<UK2Node_FunctionEntry*> Entries;
        TArray<UK2Node_FunctionResult*> Results;
        Graph->GetNodesOfClass(Entries);
        Graph->GetNodesOfClass(Results);
        if (NOT InTest.TestTrue(TEXT("Graph owns one entry and at most one result"), Entries.Num() == 1 && Results.Num() <= 1))
        { return false; }

        FGraphNodeCreator<UK2Node_CallFunction> Creator{*Graph};
        auto* Call = Creator.CreateNode();
        Call->SetFromFunction(&InFunction);
        Creator.Finalize();
        Call->NodePosX = 420;
        if (!Results.IsEmpty())
        { Results[0]->NodePosX = 930; }
        const auto* Schema = Graph->GetSchema();
        auto Connect = [&](UEdGraphPin* From, UEdGraphPin* To, FName PinName)
        {
            return InTest.TestTrue(FString::Printf(TEXT("%s connects pin %s"),
                *InFunction.GetName(), *PinName.ToString()), From && To && Schema->TryCreateConnection(From, To));
        };
        auto* Then = Entries[0]->FindPin(UEdGraphSchema_K2::PN_Then);
        auto* ReturnExec = Results.IsEmpty() ? nullptr : Results[0]->FindPin(UEdGraphSchema_K2::PN_Execute);
        if (!Call->IsNodePure() && Then && ReturnExec)
        { Then->BreakLinkTo(ReturnExec); }
        if (!Call->IsNodePure() && (NOT Connect(Then, Call->GetExecPin(), UEdGraphSchema_K2::PN_Execute)
            || (ReturnExec && NOT Connect(Call->GetThenPin(), ReturnExec, UEdGraphSchema_K2::PN_Then))))
        { return false; }

        for (TFieldIterator<FProperty> It{&InFunction}; It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
        {
            const auto Name = It->GetFName();
            auto* Pin = Call->FindPin(Name);
            if (NOT InTest.TestNotNull(Name.ToString(), Pin))
            { return false; }
            if (Pin->Direction == EGPD_Input)
            {
                if (NOT Connect(Entries[0]->FindPin(Name), Pin, Name))
                { return false; }
            }
            else if (Results.IsEmpty() || NOT Connect(Pin, Results[0]->FindPin(Name), Name))
            { return false; }
        }
        return true;
    }

    struct FPinValue
    {
        const TCHAR* Name;
        const TCHAR* Text;
    };

    auto
        Exercise(
            FAutomationTestBase& InTest,
            UObject& InInstance,
            FName InName,
            std::initializer_list<FPinValue> InInputs,
            std::initializer_list<FPinValue> InOutputs) -> bool
    {
        auto* Function = InInstance.FindFunction(GraphName(InName));
        if (NOT InTest.TestTrue(InName.ToString() + TEXT(" has compiled Blueprint bytecode"),
            Function && Function->Script.Num() > 0))
        { return false; }
        FStructOnScope Params{Function};
        FStructOnScope Expected{Function};
        auto ReadLiteral = [&](const FPinValue& InPin, FStructOnScope& InValues) -> FProperty*
        {
            auto* Prop = FindFProperty<FProperty>(Function, InPin.Name);
            if (NOT InTest.TestNotNull(InPin.Name, Prop))
            { return nullptr; }
            auto* Dest = Prop->ContainerPtrToValuePtr<void>(InValues.GetStructMemory());
            const auto* End = Prop->ImportText_Direct(InPin.Text, Dest, &InInstance, PPF_None);
            if (NOT InTest.TestTrue(FString::Printf(TEXT("%s reads %s = %s"), *InName.ToString(), InPin.Name, InPin.Text),
                End && *End == 0))
            { return nullptr; }
            return Prop;
        };
        for (const auto& Input : InInputs)
        {
            if (NOT ReadLiteral(Input, Params))
            { return false; }
        }
        InInstance.ProcessEvent(Function, Params.GetStructMemory());
        for (const auto& Output : InOutputs)
        {
            const auto* Prop = ReadLiteral(Output, Expected);
            if (NOT Prop || NOT InTest.TestTrue(FString::Printf(TEXT("%s produced %s = %s"),
                *InName.ToString(), Output.Name, Output.Text),
                Prop->Identical_InContainer(Params.GetStructMemory(), Expected.GetStructMemory())))
            { return false; }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexGeometry_BlueprintGraph, "Ck.HexGrid.Geometry.BlueprintGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCk_HexGeometry_BlueprintGraph::
    RunTest(
        const FString&) -> bool
{
    using namespace ck_test_hex_geometry_blueprint;
    auto* Library = LoadClass<UBlueprintFunctionLibrary>(nullptr, TEXT("/Script/CkGrid.Ck_Utils_HexGeometry_UE"));
    if (NOT TestNotNull(TEXT("Hex geometry library can be resolved by Blueprint"), Library))
    { return false; }
    auto Blueprint = TStrongObjectPtr<UBlueprint>{FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(),
        TEXT("HexGeometryFixture")), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass())};
    if (NOT TestNotNull(TEXT("Created transient Blueprint"), Blueprint.Get()))
    { return false; }
    for (FName Name : {FName{TEXT("TryRotate")}, FName{TEXT("TryResolve_Domain")},
                       FName{TEXT("TryGet_CoordFromWorldPoint")}, FName{TEXT("TryGet_CoordFromWorldRay")}})
    {
        auto* Function = Library->FindFunctionByName(Name);
        if (NOT TestNotNull(Name.ToString(), Function) || NOT AddGraph(*this, *Blueprint.Get(), *Function))
        { return false; }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint.Get());
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (NOT TestTrue(TEXT("Four wired geometry graphs compiled"), Blueprint->Status == BS_UpToDate && Blueprint->GeneratedClass))
    { return false; }
    auto Instance = TStrongObjectPtr<UObject>{NewObject<UObject>(GetTransientPackage(), Blueprint->GeneratedClass)};
    if (NOT TestNotNull(TEXT("Created Blueprint instance"), Instance.Get()))
    { return false; }
    const FPinValue Success{TEXT("ReturnValue"), TEXT("True")};
    const FPinValue NoFailure{TEXT("OutFailure"), TEXT("None")};
    const FPinValue OriginCell{TEXT("OutCoordinate"), TEXT("(_Q=0,_R=0)")};
    return Exercise(*this, *Instance.Get(), TEXT("TryRotate"),
        {{TEXT("InOffset"), TEXT("(_Q=2,_R=-1)")}, {TEXT("InRotation"), TEXT("Turn1")}},
        {Success, NoFailure, {TEXT("OutOffset"), TEXT("(_Q=1,_R=1)")}})
        && Exercise(*this, *Instance.Get(), TEXT("TryResolve_Domain"),
            {{TEXT("InDomain"), TEXT("(_Center=(_Q=-3,_R=2),_RingRadius=0)")}},
            {Success, NoFailure, {TEXT("OutCoordinates"), TEXT("((_Q=-3,_R=2))")}})
        && Exercise(*this, *Instance.Get(), TEXT("TryGet_CoordFromWorldPoint"),
            {{TEXT("InPoint"), TEXT("(X=0,Y=0,Z=0)")}}, {Success, NoFailure, OriginCell})
        && Exercise(*this, *Instance.Get(), TEXT("TryGet_CoordFromWorldRay"),
            {{TEXT("InOrigin"), TEXT("(X=0,Y=0,Z=20)")}, {TEXT("InDirection"), TEXT("(X=0,Y=0,Z=-3)")}},
            {Success, NoFailure, OriginCell, {TEXT("OutWorldHit"), TEXT("(X=0,Y=0,Z=0)")}})
        && Exercise(*this, *Instance.Get(), TEXT("TryGet_CoordFromWorldRay"),
            {{TEXT("InOrigin"), TEXT("(X=0,Y=0,Z=20)")}, {TEXT("InDirection"), TEXT("(X=1,Y=0,Z=0)")}},
            {{TEXT("ReturnValue"), TEXT("False")}, {TEXT("OutFailure"), TEXT("ParallelRay")}, OriginCell,
             {TEXT("OutWorldHit"), TEXT("(X=0,Y=0,Z=0)")}});
}

#endif

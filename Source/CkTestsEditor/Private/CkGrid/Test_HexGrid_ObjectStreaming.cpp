#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridObject_EntityScript.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkTests/Net/CkAutoTest_NetSubject_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "UObject/StrongObjectPtr.h"
#include "CkHexRecipeStreamingProbe.h"
#include "CkEcs/Persistence/CkPersistenceHandlerRegistry.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Fragment_Data.h"
#include "CkResourceLoader/Settings/CkResourceLoader_Settings.h"
#include "CkTests/HexGrid/CkHexPersistence_Fixture.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace ck_test_hex_object_streaming
{
    using FObject = UCk_Utils_HexGridObject_UE;
    using FLifetime = UCk_Utils_EntityLifetime_UE;
    const FString PackageName = TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_RulesObject");
    const FSoftObjectPath AssetPath{PackageName + TEXT(".DA_HexGrid_RulesObject")};
    const FSoftObjectPath MissingExport{PackageName + TEXT(".MissingObjectFootprint")};

    struct FScenario
    {
        TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE> Captures[3];
        FCk_Handle Bodies[3];
        int32 RequestsObserved = 0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexObjectStreaming, "Ck.HexGrid.Rules.ObjectStreaming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexObjectStreaming::RunTest(const FString&)
{
    using namespace ck_test_hex_object_streaming;
    if (!TestTrue(TEXT("Authored object fixture exists on disk"), FPackageName::DoesPackageExist(PackageName)))
    { return false; }
    if (auto* Package = FindPackage(nullptr, *PackageName))
    {
        if (!TestFalse(TEXT("Fixture is clean before targeted unload"), Package->IsDirty())) { return false; }
        FText Error;
        if (!UPackageTools::UnloadPackages({Package}, Error))
        { AddError(Error.ToString()); return false; }
    }
    if (!TestNull(TEXT("Asset is absent from memory before PIE"), AssetPath.ResolveObject())) { return false; }

    auto Scenario = MakeShared<FScenario>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld* World)
        {
            if (!TestNull(TEXT("Asset is still cold at actual spawn submission"), AssetPath.ResolveObject())) { return; }
            auto Parent = FLifetime::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Parent, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});

            for (int32 Index = 0; Index < 3; ++Index)
            {
                FCk_HexGridObject_SpawnParams Params;
                Params.Set_Spec(TSoftObjectPtr<UCk_HexGridObject_AuthoringSpec>{Index == 1 ? MissingExport : AssetPath});
                Scenario->Bodies[Index] = FLifetime::Request_CreateEntity(Parent);
                auto Pending = UCk_Utils_EntityScript_UE::Add(Scenario->Bodies[Index], UCk_HexGridObject_EntityScript::StaticClass(),
                    FInstancedStruct::Make(Params), [this, Scenario, Index](FCk_Handle Body)
                    {
                        ++Scenario->RequestsObserved;
                        TestTrue(TEXT("Real SpawnEntity processor returned Continue before asset resolution"),
                            Body.Has<ck::FTag_EntityScript_ContinueConstruction>() && !FObject::Has(Body));
                        if (Index == 2) { FLifetime::Request_DestroyEntity(Body); }
                    });
                if (!TestTrue(TEXT("Cold spawn enqueued"), UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))) { return; }
                Scenario->Captures[Index].Reset(NewObject<UCk_AutoTest_BodyCapturer_UE>());
                FCk_Delegate_EntityScript_Constructed Callback;
                Callback.BindDynamic(Scenario->Captures[Index].Get(), &UCk_AutoTest_BodyCapturer_UE::OnBodyConstructed);
                UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, Callback);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Scenario]()
        {
            return Scenario->RequestsObserved == 3 && Scenario->Captures[0].IsValid() && Scenario->Captures[1].IsValid()
                && Scenario->Captures[0]->_CapturedBodies.Num() == 1 && Scenario->Captures[1]->_CapturedBodies.Num() == 1
                && ck::Is_NOT_Valid(Scenario->Bodies[2]);
        }), 15.f, TEXT("Both streamed paths settle and the retired target goes stale")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld*)
        {
            const auto* Success = Cast<UCk_HexGridObject_EntityScript>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(Scenario->Bodies[0]));
            const auto* Failure = Cast<UCk_HexGridObject_EntityScript>(
                UCk_Utils_EntityScript_UE::TryGet_EntityScript(Scenario->Bodies[1]));
            TestTrue(TEXT("Cold disk asset composed the saved footprint"), Success
                && Success->Get_ObjectFailure() == ECk_HexGridObject_Failure::None && FObject::Has(Scenario->Bodies[0])
                && FObject::Get_Spec(FObject::Cast(Scenario->Bodies[0])).Get_FootprintOffsets() == TArray<FCk_HexCoord>{{1,0}});
            TestTrue(TEXT("Missing asset export completed with explicit failure and no feature"), Failure
                && Failure->Get_ObjectFailure() == ECk_HexGridObject_Failure::InvalidObject && !FObject::Has(Scenario->Bodies[1]));
            TestTrue(TEXT("Retirement during loading suppressed construction and composition"),
                Scenario->Captures[2].IsValid() && Scenario->Captures[2]->_CapturedBodies.IsEmpty()
                && ck::Is_NOT_Valid(Scenario->Bodies[2]) && !FObject::Has(Scenario->Bodies[2]));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

namespace ck_test_hex_recipe_streaming
{
    using Probe = UCkHexRecipeStreamingProbe;
    using Grid = UCk_Utils_HexGrid_UE;
    using Life = UCk_Utils_EntityLifetime_UE;
    const FString PackageName = TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_PersistenceBoard");
    const FSoftObjectPath AssetPath{ck::tests::hex_persistence::BoardAssetPath};
    const FSoftObjectPath MissingExport{PackageName + TEXT(".MissingBoardRecipe")};

    auto MakeBlueprint(FAutomationTestBase& Test) -> UBlueprint*
    {
        auto* BP = FKismetEditorUtilities::CreateBlueprint(Probe::StaticClass(), GetTransientPackage(),
            FName(*FString::Printf(TEXT("HexColdRecipe_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (!BP) { return nullptr; }
        const auto Link = [&Test](UEdGraph& Graph, UEdGraphPin* A, UEdGraphPin* B)
        { return Test.TestTrue(TEXT("Cold recipe K2 graph pins connect"), A && B && Graph.GetSchema()->TryCreateConnection(A,B)); };
        const auto Call = [](UEdGraph& Graph, FName Name)
        {
            FGraphNodeCreator<UK2Node_CallFunction> Creator(Graph);
            auto* Node = Creator.CreateNode();
            Node->SetFromFunction(Probe::StaticClass()->FindFunctionByName(Name));
            Creator.Finalize();
            return Node;
        };

        auto* Signature = UCk_GenericEntityScript_UE::StaticClass()->FindFunctionByName(TEXT("DoConstruct"));
        if (!Test.TestNotNull(TEXT("Native construction override signature"), Signature)) { return nullptr; }
        auto* Construct = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("DoConstruct"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        // UFunction creates a new user-defined signature. UClass creates an actual override and retains
        // the native reference parameter flags; the former cannot override DoConstruct's ref handle.
        FBlueprintEditorUtils::AddFunctionGraph(BP, Construct, false, Signature->GetOwnerClass());
        TArray<UK2Node_FunctionEntry*> Entries;
        TArray<UK2Node_FunctionResult*> Results;
        Construct->GetNodesOfClass(Entries);
        Construct->GetNodesOfClass(Results);
        if (Entries.Num() != 1 || Results.Num() != 1) { return nullptr; }
        auto* Record = Call(*Construct, TEXT("RecordConstruct"));
        auto* Then = Entries[0]->FindPin(UEdGraphSchema_K2::PN_Then);
        auto* ReturnExec = Results[0]->FindPin(UEdGraphSchema_K2::PN_Execute);
        if (Then && ReturnExec) { Then->BreakLinkTo(ReturnExec); }
        if (!Link(*Construct, Then, Record->GetExecPin()) ||
            !Link(*Construct, Record->GetThenPin(), ReturnExec) ||
            !Link(*Construct, Entries[0]->FindPin(TEXT("InHandle")), Record->FindPin(TEXT("InHandle"))) ||
            !Link(*Construct, Record->FindPin(UEdGraphSchema_K2::PN_ReturnValue), Results[0]->FindPin(UEdGraphSchema_K2::PN_ReturnValue)))
        { return nullptr; }

        auto* Events = FBlueprintEditorUtils::CreateNewGraph(BP, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(BP, Events);
        for (const auto Pair : {TPair<FName,FName>{TEXT("DoContinueConstruction"),TEXT("RecordContinue")},
            TPair<FName,FName>{TEXT("DoBeginPlay"),TEXT("RecordBegin")}, TPair<FName,FName>{TEXT("DoEndPlay"),TEXT("RecordEnd")}})
        {
            FGraphNodeCreator<UK2Node_Event> Creator(*Events);
            auto* Event = Creator.CreateNode();
            Event->EventReference.SetExternalMember(Pair.Key, UCk_GenericEntityScript_UE::StaticClass());
            Event->bOverrideFunction = true;
            Creator.Finalize();
            auto* Receiver = Call(*Events, Pair.Value);
            if (!Link(*Events, Event->FindPin(UEdGraphSchema_K2::PN_Then), Receiver->GetExecPin()) ||
                !Link(*Events, Event->FindPin(TEXT("InHandle")), Receiver->FindPin(TEXT("InHandle"))))
            { return nullptr; }
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status != BS_UpToDate || !BP->GeneratedClass) { return nullptr; }
        for (const FName Name : {FName(TEXT("DoConstruct")), FName(TEXT("DoContinueConstruction")),
            FName(TEXT("DoBeginPlay")), FName(TEXT("DoEndPlay"))})
        {
            const auto* Function = BP->GeneratedClass->FindFunctionByName(Name);
            if (!Test.TestTrue(Name.ToString() + TEXT(" has actual K2 bytecode"), Function && !Function->Script.IsEmpty()))
            { return nullptr; }
        }
        return BP;
    }

    struct FScenario
    {
        TStrongObjectPtr<UBlueprint> Blueprint;
        TStrongObjectPtr<UCk_HexGrid_AuthoringSpec> InvalidAsset;
        TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE> Captures[4];
        TStrongObjectPtr<Probe> Scripts[4];
        FCk_Handle Bodies[4];
        int32 RequestsObserved = 0;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexBoardColdRecipe, "Ck.HexGrid.Persistence.ColdRecipe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexBoardColdRecipe::RunTest(const FString&)
{
    using namespace ck_test_hex_recipe_streaming;
    if (!TestTrue(TEXT("Cold test uses the real asynchronous resource-loading policy"),
        UCk_Utils_ResourceLoader_Settings_UE::Get_LoadingPolicyForConsumer(TEXT("HexGrid.Construct")) == ECk_ResourceLoader_LoadingPolicy::Async))
    { return false; }
    if (!TestTrue(TEXT("Saved board fixture exists"), FPackageName::DoesPackageExist(PackageName))) { return false; }
    if (auto* Package = FindPackage(nullptr, *PackageName))
    {
        if (!TestFalse(TEXT("Only unload the clean owned recipe fixture"), Package->IsDirty())) { return false; }
        FText Error;
        if (!UPackageTools::UnloadPackages({Package}, Error)) { AddError(Error.ToString()); return false; }
    }
    if (!TestNull(TEXT("Recipe is genuinely unloaded before PIE"), AssetPath.ResolveObject())) { return false; }
    auto S = MakeShared<FScenario>();
    S->Blueprint.Reset(MakeBlueprint(*this));
    if (!TestTrue(TEXT("Cold recipe lifecycle Blueprint compiled"), S->Blueprint.IsValid())) { return false; }
    S->InvalidAsset.Reset(NewObject<UCk_HexGrid_AuthoringSpec>());
    FCk_HexGrid_Spec Invalid;
    FCk_HexDomain BadDomain;
    BadDomain.Set_RingRadius(-1);
    Invalid.Set_Domain(BadDomain);
    S->InvalidAsset->Set_Spec(Invalid);

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld* World)
    {
        if (!TestNull(TEXT("Recipe is still unloaded at actual spawn submission"), AssetPath.ResolveObject())) { return; }
        const TSubclassOf<UCk_EntityScript_UE> ScriptClass{S->Blueprint->GeneratedClass.Get()};
        if (!TestNotNull(TEXT("Compiled lifecycle Blueprint derives from EntityScript"), ScriptClass.Get())) { return; }
        auto Owner = Life::Request_CreateEntity_TransientOwner(World);
        UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
        for (int32 Index = 0; Index < 4; ++Index)
        {
            FCk_HexGrid_SpawnParams Params;
            Params.Set_Spec(Index == 2 ? TSoftObjectPtr<UCk_HexGrid_AuthoringSpec>{S->InvalidAsset.Get()}
                : TSoftObjectPtr<UCk_HexGrid_AuthoringSpec>{Index == 1 ? MissingExport : AssetPath});
            S->Bodies[Index] = Life::Request_CreateEntity(Owner);
            auto Pending = UCk_Utils_EntityScript_UE::Add(S->Bodies[Index], ScriptClass,
                FInstancedStruct::Make(Params), [this,S,Index](FCk_Handle Body)
                {
                    ++S->RequestsObserved;
                    S->Scripts[Index].Reset(Cast<Probe>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Body)));
                    TestTrue(TEXT("Construction has not published a board or invoked K2 before resource resolution"),
                        Body.Has<ck::FTag_EntityScript_ContinueConstruction>() && !Grid::Has(Body) &&
                        S->Scripts[Index].IsValid() && S->Scripts[Index]->ConstructCalls == 0);
                    if (Index == 3) { Life::Request_DestroyEntity(Body); }
                });
            if (!TestTrue(TEXT("Cold native spawn accepted"), UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))) { return; }
            S->Captures[Index].Reset(NewObject<UCk_AutoTest_BodyCapturer_UE>());
            FCk_Delegate_EntityScript_Constructed Done;
            Done.BindDynamic(S->Captures[Index].Get(), &UCk_AutoTest_BodyCapturer_UE::OnBodyConstructed);
            UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, Done);
        }
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([S]()
    {
        return S->RequestsObserved == 4 && S->Captures[0].IsValid() && S->Captures[1].IsValid() && S->Captures[2].IsValid()
            && S->Captures[0]->_CapturedBodies.Num() == 1 && S->Captures[1]->_CapturedBodies.Num() == 1
            && S->Captures[2]->_CapturedBodies.Num() == 1 && S->Scripts[0].IsValid() && S->Scripts[0]->BeginCalls == 1
            && !ck::IsValid(S->Bodies[3]);
    }), 20.f, TEXT("Cold success, missing export, invalid recipe and retired target settle")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this,S](UWorld*)
    {
        if (!TestTrue(TEXT("Successful cold K2 lifecycle executes exactly once on a ready board"),
            S->Scripts[0].IsValid() && S->Scripts[0]->ConstructCalls == 1 && S->Scripts[0]->ContinueCalls == 1
            && S->Scripts[0]->BeginCalls == 1 && S->Scripts[0]->EndCalls == 0 && S->Scripts[0]->ReadyInsideConstruct
            && Grid::Get_Cells(Grid::Cast(S->Bodies[0])).Num() == 19)) { return; }
        const auto* Handler = FCk_PersistenceHandlerRegistry::Find(FCk_RepData_HexGridState::StaticStruct());
        const auto Produced = Handler && Handler->Produce ? Handler->Produce(S->Bodies[0]) : TOptional<FInstancedStruct>{};
        const auto* Data = Produced.IsSet() ? Produced->GetPtr<FCk_RepData_HexGridState>() : nullptr;
        TestTrue(TEXT("Cold saved recipe remains durable and uses its saved asset identity"),
            Data && Data->Get_Recipe().Get_AssetPath() == AssetPath.ToString());
        for (int32 Index : {1,2,3})
        {
            TestTrue(TEXT("Failed or retired recipe never calls user K2 lifecycle"), S->Scripts[Index].IsValid()
                && S->Scripts[Index]->ConstructCalls == 0 && S->Scripts[Index]->ContinueCalls == 0
                && S->Scripts[Index]->BeginCalls == 0 && S->Scripts[Index]->EndCalls == 0 && !Grid::Has(S->Bodies[Index]));
            if (Index != 3)
            { TestTrue(TEXT("Definitive missing/invalid recipe reports failure"), S->Scripts[Index]->Get_BoardFailure() == ECk_HexBoardFailure::InvalidSpec); }
        }
        TestTrue(TEXT("Retired streaming target never publishes construction completion"), S->Captures[3]->_CapturedBodies.IsEmpty());
        for (int32 Index : {0,1,2}) { Life::Request_DestroyEntity(S->Bodies[Index]); }
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda([S]()
    { return !ck::IsValid(S->Bodies[0]) && !ck::IsValid(S->Bodies[1]) && !ck::IsValid(S->Bodies[2]); }),
        10.f, TEXT("Lifecycle subjects physically retire")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda([this,S]()
    {
        return TestTrue(TEXT("Only successfully composed subject executes user K2 EndPlay"), S->Scripts[0].IsValid()
            && S->Scripts[0]->EndCalls == 1 && S->Scripts[1].IsValid() && S->Scripts[1]->EndCalls == 0
            && S->Scripts[2].IsValid() && S->Scripts[2]->EndCalls == 0 && S->Scripts[3].IsValid() && S->Scripts[3]->EndCalls == 0);
    }), TEXT("Failure lifecycle remains private through destruction")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

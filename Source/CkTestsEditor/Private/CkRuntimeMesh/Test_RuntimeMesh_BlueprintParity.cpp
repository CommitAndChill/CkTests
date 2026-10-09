#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_RuntimeMeshBpHarness.h"

#include "CkCore/Time/CkTime.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsEditor_Subsystem.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkJolt/Settings/CkJolt_ProjectSettings.h"
#include "CkJolt/Subsystem/CkJolt_Subsystem.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Processor.h"
#include "CkRuntimeMesh/Display/CkRuntimeMeshDisplay_Utils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>

namespace ck_test_runtime_mesh_bp
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext
        | EAutomationTestFlags::EngineFilter;

    auto Connect(UEdGraph* InGraph, UEdGraphPin* InOutput, UEdGraphPin* InInput)
        -> bool
    {
        return InOutput != nullptr && InInput != nullptr
            && InGraph->GetSchema()->TryCreateConnection(InOutput, InInput);
    }
}

void UCk_Test_RuntimeMeshBpHarness_UE::OnSliceResolved(
    FCk_RuntimeMesh_SliceResult InResult)
{
    Results.Add(InResult);
}

void UCk_Test_RuntimeMeshBpHarness_UE::OnRequestCompleted(
    FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
{
    Completions.Add(InResult);
}

void UCk_Test_RuntimeMeshBpHarness_UE::OnJoltResolved(
    FCk_Handle_JoltBody InBody, ECk_JoltBody_SetupState InState,
    ECk_JoltBody_SetupFailure InFailure)
{
    ++JoltCallbacks;
    JoltCallbackState = InState;
    JoltCallbackFailure = InFailure;
}

void UCk_Test_RuntimeMeshBpHarness_UE::CaptureComposition(
    ECk_RuntimeMesh_SetupState InDisplayState,
    ECk_RuntimeMeshDisplay_SetupFailure InDisplayFailure,
    ECk_JoltBody_SetupState InBodyState,
    ECk_JoltBody_SetupFailure InBodyFailure,
    const FString& InDiagnostic)
{
    CompositionObserved = true;
    DisplayState = InDisplayState;
    DisplayFailure = InDisplayFailure;
    BodyState = InBodyState;
    BodyFailure = InBodyFailure;
    Diagnostic = InDiagnostic;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_BlueprintParity,
    "Ck.RuntimeMesh.Blueprint.CompiledSliceEvent",
    ck_test_runtime_mesh_bp::TestFlags)

bool FCkTest_RuntimeMesh_BlueprintParity::RunTest(const FString& Parameters)
{
    using namespace ck_test_runtime_mesh_bp;
    constexpr auto InformEngineOfWorld = false;
    auto* Settings = GetMutableDefault<UCk_Jolt_ProjectSettings_UE>();
    if (NOT TestNotNull(TEXT("Jolt editor settings"), Settings))
    { return false; }
    const auto RestoreMode = Settings->Get_EditorStaticWorldMode();
    ON_SCOPE_EXIT { Settings->TestOnly_Set_EditorStaticWorldMode(RestoreMode); };
    Settings->TestOnly_Set_EditorStaticWorldMode(ECk_Jolt_EditorStaticWorldMode::LiveExtract);
    auto* World = UWorld::CreateWorld(EWorldType::Editor, InformEngineOfWorld,
        TEXT("RuntimeMeshBlueprintParity"));
    if (NOT TestNotNull(TEXT("test world"), World))
    { return false; }
    World->AddToRoot();
    ON_SCOPE_EXIT
    {
        ck::runtimemesh::CancelWorld(World);
        World->RemoveFromRoot();
        World->DestroyWorld(InformEngineOfWorld);
    };
    auto* Ecs = World->GetSubsystem<UCk_EditorEcsWorld_Subsystem_UE>();
    auto* Jolt = World->GetSubsystem<UCk_Jolt_Subsystem>();
    if (NOT TestNotNull(TEXT("editor ECS"), Ecs)
        || NOT TestNotNull(TEXT("editor Jolt"), Jolt)
        || NOT TestTrue(TEXT("editor Jolt PhysicsSystem"),
            Jolt->Get_PhysicsSystem().Pin().IsValid()))
    { return false; }
    const auto WorldEntity = Ecs->Get_TransientEntity();
    auto SourceOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
    const auto ResultOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
    auto* Asset = LoadObject<UStaticMesh>(nullptr,
        TEXT("/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU"));
    if (NOT TestNotNull(TEXT("cooked CPU mesh"), Asset))
    { return false; }
    auto Spec = FCk_RuntimeMesh_Spec{};
    Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{Asset});
    auto Source = UCk_Utils_RuntimeMesh_UE::Add(SourceOwner, Spec);
    if (NOT TestTrue(TEXT("source handle"), ck::IsValid(Source)))
    { return false; }
    ck::FProcessor_RuntimeMesh_Setup{WorldEntity.Get_RegistryView()}.ForEachEntity(
        FCk_Time{}, Source, Source.Get<ck::FFragment_RuntimeMesh>(),
        Source.Get<ck::FFragment_RuntimeMesh_PendingImport>());
    if (NOT TestEqual(TEXT("source imported"),
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source),
        ECk_RuntimeMesh_SetupState::Ready))
    { return false; }

    auto Blueprint = TStrongObjectPtr<UBlueprint>{FKismetEditorUtilities::CreateBlueprint(
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass(), GetTransientPackage(),
        FName(*FString::Printf(TEXT("RuntimeMeshBP_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass())};
    if (NOT TestNotNull(TEXT("transient Blueprint"), Blueprint.Get()))
    { return false; }
    auto* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint.Get());
    if (NOT TestNotNull(TEXT("event graph"), Graph))
    { return false; }
    auto* Event = NewObject<UK2Node_Event>(Graph);
    Event->EventReference.SetExternalMember(
        GET_FUNCTION_NAME_CHECKED(UCk_Test_RuntimeMeshBpHarness_UE, DispatchSlice),
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass());
    Event->bOverrideFunction = true;
    Graph->AddNode(Event, false, false);
    Event->CreateNewGuid();
    Event->PostPlacedNewNode();
    Event->AllocateDefaultPins();

    auto* Function = UCk_Utils_RuntimeMesh_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_RuntimeMesh_UE, Request_Slice));
    if (NOT TestNotNull(TEXT("public BP call function"), Function))
    { return false; }
    auto* Call = NewObject<UK2Node_CallFunction>(Graph);
    Call->SetFromFunction(Function);
    Graph->AddNode(Call, false, false);
    Call->CreateNewGuid();
    Call->PostPlacedNewNode();
    Call->AllocateDefaultPins();

    auto* Typed = NewObject<UK2Node_CreateDelegate>(Graph);
    Graph->AddNode(Typed, false, false);
    Typed->CreateNewGuid();
    Typed->PostPlacedNewNode();
    Typed->AllocateDefaultPins();
    Typed->SetFunction(GET_FUNCTION_NAME_CHECKED(
        UCk_Test_RuntimeMeshBpHarness_UE, OnSliceResolved));
    auto* Generic = NewObject<UK2Node_CreateDelegate>(Graph);
    Graph->AddNode(Generic, false, false);
    Generic->CreateNewGuid();
    Generic->PostPlacedNewNode();
    Generic->AllocateDefaultPins();
    Generic->SetFunction(GET_FUNCTION_NAME_CHECKED(
        UCk_Test_RuntimeMeshBpHarness_UE, OnRequestCompleted));

    const auto Exec = Connect(Graph, Event->FindPin(UEdGraphSchema_K2::PN_Then),
        Call->FindPin(UEdGraphSchema_K2::PN_Execute));
    const auto SourcePin = Connect(Graph, Event->FindPin(TEXT("InSource")),
        Call->FindPin(TEXT("InSource")));
    const auto RequestPin = Connect(Graph, Event->FindPin(TEXT("InRequest")),
        Call->FindPin(TEXT("InRequest")));
    const auto TypedPin = Connect(Graph, Typed->GetDelegateOutPin(),
        Call->FindPin(TEXT("InReceiver")));
    const auto GenericPin = Connect(Graph, Generic->GetDelegateOutPin(),
        Call->FindPin(TEXT("InDelegate")));
    if (NOT TestTrue(TEXT("BP execution and four data pins connect"),
        Exec && SourcePin && RequestPin && TypedPin && GenericPin))
    { return false; }
    Typed->HandleAnyChangeWithoutNotifying();
    Generic->HandleAnyChangeWithoutNotifying();

    const auto AddEvent = [&](FName InName) -> UK2Node_Event*
    {
        auto* Node = NewObject<UK2Node_Event>(Graph);
        Node->EventReference.SetExternalMember(InName,
            UCk_Test_RuntimeMeshBpHarness_UE::StaticClass());
        Node->bOverrideFunction = true;
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        return Node;
    };
    const auto AddCall = [&](UFunction* InFunction) -> UK2Node_CallFunction*
    {
        auto* Node = NewObject<UK2Node_CallFunction>(Graph);
        Node->SetFromFunction(InFunction);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        return Node;
    };
    const auto AddDelegate = [&](FName InName) -> UK2Node_CreateDelegate*
    {
        auto* Node = NewObject<UK2Node_CreateDelegate>(Graph);
        Graph->AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        Node->SetFunction(InName);
        return Node;
    };
    const auto AddFunction = [](UClass* InClass, FName InName) -> UFunction*
    { return InClass->FindFunctionByName(InName); };

    auto* ComposeEvent = AddEvent(GET_FUNCTION_NAME_CHECKED(
        UCk_Test_RuntimeMeshBpHarness_UE, ComposeResult));
    auto* DisplayAdd = AddCall(AddFunction(UCk_Utils_RuntimeMeshDisplay_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_RuntimeMeshDisplay_UE, Add)));
    auto* BodyAdd = AddCall(AddFunction(UCk_Utils_JoltBody_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_JoltBody_UE, Add)));
    auto* Promise = AddCall(AddFunction(UCk_Utils_JoltBody_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_JoltBody_UE, TryPromise_OnSetupResolved)));
    auto* JoltDelegate = AddDelegate(GET_FUNCTION_NAME_CHECKED(
        UCk_Test_RuntimeMeshBpHarness_UE, OnJoltResolved));
    const auto ComposeConnected =
        Connect(Graph, ComposeEvent->FindPin(UEdGraphSchema_K2::PN_Then),
            DisplayAdd->FindPin(UEdGraphSchema_K2::PN_Execute))
        && Connect(Graph, DisplayAdd->FindPin(UEdGraphSchema_K2::PN_Then),
            BodyAdd->FindPin(UEdGraphSchema_K2::PN_Execute))
        && Connect(Graph, BodyAdd->FindPin(UEdGraphSchema_K2::PN_Then),
            Promise->FindPin(UEdGraphSchema_K2::PN_Execute))
        && Connect(Graph, ComposeEvent->FindPin(TEXT("InDisplayOwner")),
            DisplayAdd->FindPin(TEXT("InHandle")))
        && Connect(Graph, ComposeEvent->FindPin(TEXT("InEntity")),
            BodyAdd->FindPin(TEXT("InHandle")))
        && Connect(Graph, ComposeEvent->FindPin(TEXT("InDisplay")),
            DisplayAdd->FindPin(TEXT("InSpec")))
        && Connect(Graph, ComposeEvent->FindPin(TEXT("InBody")),
            BodyAdd->FindPin(TEXT("InParams")))
        && Connect(Graph, BodyAdd->GetReturnValuePin(),
            Promise->FindPin(TEXT("InJoltBody")))
        && Connect(Graph, JoltDelegate->GetDelegateOutPin(),
            Promise->FindPin(TEXT("InDelegate")));
    JoltDelegate->HandleAnyChangeWithoutNotifying();
    if (NOT TestTrue(TEXT("compiled BP composition pins connect"), ComposeConnected))
    { return false; }

    auto* InspectEvent = AddEvent(GET_FUNCTION_NAME_CHECKED(
        UCk_Test_RuntimeMeshBpHarness_UE, InspectComposition));
    auto* DisplayState = AddCall(AddFunction(UCk_Utils_RuntimeMeshDisplay_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_RuntimeMeshDisplay_UE, Get_SetupState)));
    auto* DisplayFailure = AddCall(AddFunction(UCk_Utils_RuntimeMeshDisplay_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_RuntimeMeshDisplay_UE, Get_SetupFailure)));
    auto* BodyState = AddCall(AddFunction(UCk_Utils_JoltBody_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_JoltBody_UE, Get_SetupState)));
    auto* BodyFailure = AddCall(AddFunction(UCk_Utils_JoltBody_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_JoltBody_UE, Get_SetupFailure)));
    auto* BodyDiagnostic = AddCall(AddFunction(UCk_Utils_JoltBody_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_JoltBody_UE, Get_SetupDiagnostic)));
    auto* Capture = AddCall(AddFunction(UCk_Test_RuntimeMeshBpHarness_UE::StaticClass(),
        GET_FUNCTION_NAME_CHECKED(UCk_Test_RuntimeMeshBpHarness_UE, CaptureComposition)));
    const auto InspectConnected =
        Connect(Graph, InspectEvent->FindPin(UEdGraphSchema_K2::PN_Then),
            Capture->FindPin(UEdGraphSchema_K2::PN_Execute))
        && Connect(Graph, InspectEvent->FindPin(TEXT("InDisplay")),
            DisplayState->FindPin(TEXT("InHandle")))
        && Connect(Graph, InspectEvent->FindPin(TEXT("InDisplay")),
            DisplayFailure->FindPin(TEXT("InHandle")))
        && Connect(Graph, InspectEvent->FindPin(TEXT("InBody")),
            BodyState->FindPin(TEXT("InJoltBody")))
        && Connect(Graph, InspectEvent->FindPin(TEXT("InBody")),
            BodyFailure->FindPin(TEXT("InJoltBody")))
        && Connect(Graph, InspectEvent->FindPin(TEXT("InBody")),
            BodyDiagnostic->FindPin(TEXT("InJoltBody")))
        && Connect(Graph, DisplayState->GetReturnValuePin(),
            Capture->FindPin(TEXT("InDisplayState")))
        && Connect(Graph, DisplayFailure->GetReturnValuePin(),
            Capture->FindPin(TEXT("InDisplayFailure")))
        && Connect(Graph, BodyState->GetReturnValuePin(),
            Capture->FindPin(TEXT("InBodyState")))
        && Connect(Graph, BodyFailure->GetReturnValuePin(),
            Capture->FindPin(TEXT("InBodyFailure")))
        && Connect(Graph, BodyDiagnostic->GetReturnValuePin(),
            Capture->FindPin(TEXT("InDiagnostic")));
    if (NOT TestTrue(TEXT("compiled BP getter pins connect"), InspectConnected))
    { return false; }
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (NOT TestTrue(TEXT("Blueprint compiled"),
        (Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)
        && Blueprint->GeneratedClass != nullptr))
    { return false; }

    auto Harness = TStrongObjectPtr<UCk_Test_RuntimeMeshBpHarness_UE>{
        NewObject<UCk_Test_RuntimeMeshBpHarness_UE>(GetTransientPackage(),
            Blueprint->GeneratedClass)};
    auto Request = FCk_Request_RuntimeMesh_Slice{};
    const auto OperationID = FGuid::NewGuid();
    Request.Set_OperationID(OperationID);
    Request.Set_ResultOwner(ResultOwner);
    auto Plane = FCk_RuntimeMesh_PlaneLocal{};
    Plane.Set_PositionCm(FVector{5, 0, 0});
    Plane.Set_Normal(FVector::ForwardVector);
    Plane.Set_Tangent(FVector::RightVector);
    Request.Set_Plane(Plane);
    Harness->DispatchSlice(Source, Request);
    TestEqual(TEXT("BP node enqueues without synchronous callback"),
        Harness->Results.Num(), 0);
    ck::FProcessor_RuntimeMesh_Drain{WorldEntity.Get_RegistryView()}.DoTick(
        FCk_Time{1.0f / 60.0f});
    if (NOT TestEqual(TEXT("typed BP delegate fired"), Harness->Results.Num(), 1))
    { return false; }
    TestEqual(TEXT("generic BP delegate fired"), Harness->Completions.Num(), 1);
    TestEqual(TEXT("operation ID preserved"),
        Harness->Results[0].Get_OperationID(), OperationID);
    TestEqual(TEXT("BP slice succeeded"), Harness->Results[0].Get_Outcome(),
        ECk_RuntimeMesh_SliceOutcome::Succeeded);
    TestTrue(TEXT("BP result owner independent of source"),
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(
            Harness->Results[0].Get_Positive()) == ResultOwner);

    auto PositiveEntity = FCk_Handle{Harness->Results[0].Get_Positive()};
    auto PositiveTransform = UCk_Utils_Transform_UE::Add(PositiveEntity,
        FTransform{FRotator::ZeroRotator, FVector{0, 0, 100}},
        ECk_Replication::DoesNotReplicate);
    auto Visuals = FCk_RuntimeMeshDisplay_Visuals{};
    const auto Material = TSoftObjectPtr<UMaterialInterface>{FSoftObjectPath{
        TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")}};
    Visuals.Set_Materials({Material, Material});
    auto DisplaySpec = FCk_RuntimeMeshDisplay_Spec{};
    DisplaySpec.Set_Geometry(Harness->Results[0].Get_Positive());
    DisplaySpec.Set_Visuals(Visuals);
    auto Convex = FCk_JoltBody_RuntimeConvexSpec{};
    Convex.Set_PointsCm(UCk_Utils_RuntimeMesh_UE::Copy_LocalVerticesCm(
        Harness->Results[0].Get_Positive()));
    auto BodySpec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::RuntimeConvex};
    BodySpec.Set_RuntimeConvex(Convex);
    BodySpec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
    BodySpec.Set_MassKg(1.0f);
    Harness->ComposeResult(PositiveTransform, PositiveEntity, DisplaySpec, BodySpec);
    if (NOT TestTrue(TEXT("BP Display Add composed"),
        UCk_Utils_RuntimeMeshDisplay_UE::Has(PositiveEntity))
        || NOT TestTrue(TEXT("BP RuntimeConvex Add composed"),
            UCk_Utils_JoltBody_UE::Has(PositiveEntity)))
    { return false; }
    const auto DisplayHandle = UCk_Utils_RuntimeMeshDisplay_UE::CastChecked(PositiveEntity);
    const auto BodyHandle = UCk_Utils_JoltBody_UE::CastChecked(PositiveEntity);
    FlushAsyncLoading();
    for (auto Attempt = 0; Attempt < 4
        && (UCk_Utils_RuntimeMeshDisplay_UE::Get_SetupState(DisplayHandle)
            == ECk_RuntimeMesh_SetupState::Pending
            || UCk_Utils_JoltBody_UE::Get_SetupState(BodyHandle)
                == ECk_JoltBody_SetupState::Pending); ++Attempt)
    { Ecs->Tick(0.016f); }
    Harness->InspectComposition(DisplayHandle, BodyHandle);
    TestTrue(TEXT("BP getter bytecode captured status"), Harness->CompositionObserved);
    TestEqual(TEXT("BP Display setup Ready"), Harness->DisplayState,
        ECk_RuntimeMesh_SetupState::Ready);
    TestEqual(TEXT("BP Display no failure"), Harness->DisplayFailure,
        ECk_RuntimeMeshDisplay_SetupFailure::None);
    TestEqual(TEXT("BP RuntimeConvex setup Ready"), Harness->BodyState,
        ECk_JoltBody_SetupState::Ready);
    TestEqual(TEXT("BP RuntimeConvex no failure"), Harness->BodyFailure,
        ECk_JoltBody_SetupFailure::None);
    TestTrue(TEXT("BP RuntimeConvex diagnostic empty"), Harness->Diagnostic.IsEmpty());
    TestEqual(TEXT("BP Jolt promise callback exactly once"), Harness->JoltCallbacks, 1);
    TestEqual(TEXT("BP Jolt promise Ready"), Harness->JoltCallbackState,
        ECk_JoltBody_SetupState::Ready);
    TestEqual(TEXT("BP Jolt promise no failure"), Harness->JoltCallbackFailure,
        ECk_JoltBody_SetupFailure::None);
    return true;
}
#endif

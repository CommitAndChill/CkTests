#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkRuntimeMesh/CkTest_RuntimeMeshBpHarness.h"

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Receipt.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionBuilder/CkProceduralActionBuilder_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/Debug/CkProceduralAnimation_Debug.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace ck_test_procedural_action_blueprint
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
        DoAddCall(
            UEdGraph& InGraph,
            UFunction& InFunction)
        -> UK2Node_CallFunction*
    {
        auto* Node = NewObject<UK2Node_CallFunction>(&InGraph);
        Node->SetFromFunction(&InFunction);
        InGraph.AddNode(Node, false, false);
        Node->CreateNewGuid();
        Node->PostPlacedNewNode();
        Node->AllocateDefaultPins();
        return Node;
    }

    auto
        DoConnect(
            UEdGraph& InGraph,
            UEdGraphPin* InOutput,
            UEdGraphPin* InInput)
        -> bool
    {
        return InOutput != nullptr && InInput != nullptr
            && InGraph.GetSchema()->TryCreateConnection(InOutput, InInput);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_BlueprintParity_ArticulationNodes,
    "Ck.ProceduralAnimation.Action.Editor.BlueprintParity.ArticulationNodes",
    ck_test_procedural_action_blueprint::TestFlags)

auto
    FCkTest_ProceduralAction_BlueprintParity_ArticulationNodes::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_blueprint;

    auto Blueprint = TStrongObjectPtr<UBlueprint>{FKismetEditorUtilities::CreateBlueprint(
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass(), GetTransientPackage(),
        FName(*FString::Printf(TEXT("ProceduralArticulationBP_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass())};
    if (NOT TestNotNull(TEXT("transient Blueprint"), Blueprint.Get()))
    { return false; }

    auto* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint.Get());
    if (NOT TestNotNull(TEXT("event graph"), Graph))
    { return false; }

    auto ReceiptType = FEdGraphPinType{};
    ReceiptType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    ReceiptType.PinSubCategoryObject = FCk_ProceduralAction_Receipt::StaticStruct();
    const auto ReceiptName = FName(TEXT("ObservedReceipt"));
    auto PageType = FEdGraphPinType{};
    PageType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    PageType.PinSubCategoryObject = FCk_ProceduralAction_ReadPage::StaticStruct();
    const auto PageName = FName(TEXT("ObservedPage"));
    auto PlayerType = FEdGraphPinType{};
    PlayerType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    PlayerType.PinSubCategoryObject = FCk_Handle_ProceduralActionPlayer::StaticStruct();
    const auto PlayerName = FName(TEXT("LoadedClipPlayer"));
    if (NOT TestTrue(TEXT("Blueprint owns a default receipt value"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), ReceiptName, ReceiptType))
        || NOT TestTrue(TEXT("Blueprint owns a default read page value"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), PageName, PageType))
        || NOT TestTrue(TEXT("Blueprint owns a typed player handle"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), PlayerName, PlayerType)))
    { return false; }

    auto* Event = NewObject<UK2Node_Event>(Graph);
    Event->EventReference.SetExternalMember(
        GET_FUNCTION_NAME_CHECKED(UCk_Test_RuntimeMeshBpHarness_UE, ComposeResult),
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass());
    Event->bOverrideFunction = true;
    Graph->AddNode(Event, false, false);
    Event->CreateNewGuid();
    Event->PostPlacedNewNode();
    Event->AllocateDefaultPins();

    auto* Utils = UCk_Utils_ProceduralArticulation_UE::StaticClass();
    auto* AddFunction = Utils->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralArticulation_UE, Add));
    auto* LimbFunction = Utils->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralArticulation_UE, Get_Limb));
    auto* RevisionFunction = Utils->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralArticulation_UE, Get_SettledRevision));
    auto* CreateFunction = UCk_Utils_ProceduralAction_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAction_UE, Create_FromClip));
    auto* ValidateFunction = UCk_Utils_ProceduralAction_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAction_UE, Validate_Data));
    if (NOT TestNotNull(TEXT("public Articulation Add node"), AddFunction)
        || NOT TestNotNull(TEXT("public Articulation Get_Limb node"), LimbFunction)
        || NOT TestNotNull(TEXT("public Articulation Get_SettledRevision node"), RevisionFunction)
        || NOT TestNotNull(TEXT("public clip create node"), CreateFunction)
        || NOT TestNotNull(TEXT("public clip validation node"), ValidateFunction))
    { return false; }

    auto* Add = DoAddCall(*Graph, *AddFunction);
    auto* Limb = DoAddCall(*Graph, *LimbFunction);
    auto* Revision = DoAddCall(*Graph, *RevisionFunction);
    auto* Create = DoAddCall(*Graph, *CreateFunction);
    auto* Validate = DoAddCall(*Graph, *ValidateFunction);

    auto* MakePlaySpec = NewObject<UK2Node_MakeStruct>(Graph);
    MakePlaySpec->StructType = FCk_ProceduralAction_PlaySpec::StaticStruct();
    Graph->AddNode(MakePlaySpec, false, false);
    MakePlaySpec->CreateNewGuid();
    MakePlaySpec->PostPlacedNewNode();
    MakePlaySpec->AllocateDefaultPins();

    auto* ValidationBreak = NewObject<UK2Node_BreakStruct>(Graph);
    ValidationBreak->StructType = FCk_ProceduralAction_ValidationReport::StaticStruct();
    Graph->AddNode(ValidationBreak, false, false);
    ValidationBreak->CreateNewGuid();
    ValidationBreak->PostPlacedNewNode();
    ValidationBreak->AllocateDefaultPins();

    auto* PlayerGet = NewObject<UK2Node_VariableGet>(Graph);
    PlayerGet->VariableReference.SetSelfMember(PlayerName);
    Graph->AddNode(PlayerGet, false, false);
    PlayerGet->CreateNewGuid();
    PlayerGet->PostPlacedNewNode();
    PlayerGet->AllocateDefaultPins();

    constexpr auto BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    auto* BodyPulse = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
    auto* ClipPin = MakePlaySpec->FindPin(TEXT("_Clip"));
    if (NOT TestNotNull(TEXT("persisted BodyPulse clip"), BodyPulse)
        || NOT TestNotNull(TEXT("play spec Clip input"), ClipPin))
    { return false; }
    Graph->GetSchema()->TrySetDefaultValue(*ClipPin, BodyPulse->GetPathName());

    auto* MakeSpec = NewObject<UK2Node_MakeStruct>(Graph);
    MakeSpec->StructType = FCk_ProceduralArticulation_Spec::StaticStruct();
    Graph->AddNode(MakeSpec, false, false);
    MakeSpec->CreateNewGuid();
    MakeSpec->PostPlacedNewNode();
    MakeSpec->AllocateDefaultPins();

    auto* ReceiptGet = NewObject<UK2Node_VariableGet>(Graph);
    ReceiptGet->VariableReference.SetSelfMember(ReceiptName);
    Graph->AddNode(ReceiptGet, false, false);
    ReceiptGet->CreateNewGuid();
    ReceiptGet->PostPlacedNewNode();
    ReceiptGet->AllocateDefaultPins();
    auto* ReceiptBreak = NewObject<UK2Node_BreakStruct>(Graph);
    ReceiptBreak->StructType = FCk_ProceduralAction_Receipt::StaticStruct();
    Graph->AddNode(ReceiptBreak, false, false);
    ReceiptBreak->CreateNewGuid();
    ReceiptBreak->PostPlacedNewNode();
    ReceiptBreak->AllocateDefaultPins();

    auto* PageGet = NewObject<UK2Node_VariableGet>(Graph);
    PageGet->VariableReference.SetSelfMember(PageName);
    Graph->AddNode(PageGet, false, false);
    PageGet->CreateNewGuid();
    PageGet->PostPlacedNewNode();
    PageGet->AllocateDefaultPins();
    auto* PageBreak = NewObject<UK2Node_BreakStruct>(Graph);
    PageBreak->StructType = FCk_ProceduralAction_ReadPage::StaticStruct();
    Graph->AddNode(PageBreak, false, false);
    PageBreak->CreateNewGuid();
    PageBreak->PostPlacedNewNode();
    PageBreak->AllocateDefaultPins();

    auto* ReceiptBranch = NewObject<UK2Node_IfThenElse>(Graph);
    Graph->AddNode(ReceiptBranch, false, false);
    ReceiptBranch->CreateNewGuid();
    ReceiptBranch->PostPlacedNewNode();
    ReceiptBranch->AllocateDefaultPins();
    auto* PageBranch = NewObject<UK2Node_IfThenElse>(Graph);
    Graph->AddNode(PageBranch, false, false);
    PageBranch->CreateNewGuid();
    PageBranch->PostPlacedNewNode();
    PageBranch->AllocateDefaultPins();

    const auto Connected =
        DoConnect(*Graph, Event->FindPin(UEdGraphSchema_K2::PN_Then),
            Validate->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Validate->FindPin(UEdGraphSchema_K2::PN_Then),
            Create->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Create->FindPin(UEdGraphSchema_K2::PN_Then),
            Add->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Validate->GetReturnValuePin(),
            ValidationBreak->FindPin(FCk_ProceduralAction_ValidationReport::StaticStruct()->GetFName()))
        && DoConnect(*Graph, Event->FindPin(TEXT("InEntity")),
            Create->FindPin(TEXT("InScope")))
        && DoConnect(*Graph, PlayerGet->GetValuePin(),
            Create->FindPin(TEXT("InPlayer")))
        && DoConnect(*Graph,
            MakePlaySpec->FindPin(FCk_ProceduralAction_PlaySpec::StaticStruct()->GetFName()),
            Create->FindPin(TEXT("InSpec")))
        && DoConnect(*Graph, Event->FindPin(TEXT("InDisplayOwner")),
            Add->FindPin(TEXT("InRoot")))
        && DoConnect(*Graph, MakeSpec->FindPin(FCk_ProceduralArticulation_Spec::StaticStruct()->GetFName()),
            Add->FindPin(TEXT("InSpec")))
        && DoConnect(*Graph, Add->GetReturnValuePin(),
            Limb->FindPin(TEXT("InArticulation")))
        && DoConnect(*Graph, Add->GetReturnValuePin(),
            Revision->FindPin(TEXT("InArticulation")))
        && DoConnect(*Graph, ReceiptGet->GetValuePin(),
            ReceiptBreak->FindPin(FCk_ProceduralAction_Receipt::StaticStruct()->GetFName()))
        && DoConnect(*Graph, PageGet->GetValuePin(),
            PageBreak->FindPin(FCk_ProceduralAction_ReadPage::StaticStruct()->GetFName()))
        && DoConnect(*Graph, Add->FindPin(UEdGraphSchema_K2::PN_Then),
            ReceiptBranch->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, ReceiptBreak->FindPin(TEXT("_HasAcceptedPose")),
            ReceiptBranch->GetConditionPin())
        && DoConnect(*Graph, ReceiptBranch->FindPin(UEdGraphSchema_K2::PN_Then),
            PageBranch->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, PageBreak->FindPin(TEXT("_Terminal")),
            PageBranch->GetConditionPin());
    if (NOT TestTrue(TEXT("Articulation and observation pins connect"), Connected)
        || NOT TestNotNull(TEXT("Articulation spec pin"), Add->FindPin(TEXT("InSpec")))
        || NOT TestNotNull(TEXT("Get_Limb ID pin"), Limb->FindPin(TEXT("InId")))
        || NOT TestNotNull(TEXT("Get_Limb typed return pin"), Limb->GetReturnValuePin())
        || NOT TestNotNull(TEXT("Get_SettledRevision return pin"), Revision->GetReturnValuePin())
        || NOT TestNotNull(TEXT("receipt accepted-pose read pin"), ReceiptBreak->FindPin(TEXT("_HasAcceptedPose")))
        || NOT TestNotNull(TEXT("page result read pin"), PageBreak->FindPin(TEXT("_Result")))
        || NOT TestNotNull(TEXT("page records read pin"), PageBreak->FindPin(TEXT("_Records")))
        || NOT TestNotNull(TEXT("validation status read pin"), ValidationBreak->FindPin(TEXT("_Status")))
        || NOT TestNotNull(TEXT("validation diagnostics read pin"), ValidationBreak->FindPin(TEXT("_Diagnostics")))
        || NOT TestNotNull(TEXT("play spec delivery input"), MakePlaySpec->FindPin(TEXT("_Delivery")))
        || NOT TestNotNull(TEXT("play spec slot bindings input"), MakePlaySpec->FindPin(TEXT("_SlotBindings"))))
    { return false; }
    if (NOT TestTrue(TEXT("play spec carries the persisted clip as a soft reference"),
            ClipPin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
            && ClipPin->DefaultValue.Contains(BodyPulse->GetName()))
        || NOT TestTrue(TEXT("Create_FromClip accepts the reflected play spec"),
            Create->FindPin(TEXT("InSpec"))->PinType.PinSubCategoryObject.Get()
                == FCk_ProceduralAction_PlaySpec::StaticStruct()))
    { return false; }
    if (NOT TestNotNull(TEXT("Validate_Data clip input"), Validate->FindPin(TEXT("InData"))))
    { return false; }
    Graph->GetSchema()->TrySetDefaultObject(*Validate->FindPin(TEXT("InData")), BodyPulse);
    if (NOT TestTrue(TEXT("Articulation Add accepts its reflected spec"),
            Add->FindPin(TEXT("InSpec"))->PinType.PinSubCategoryObject.Get()
                == FCk_ProceduralArticulation_Spec::StaticStruct())
        || NOT TestTrue(TEXT("Get_Limb returns the typed limb handle"),
            Limb->GetReturnValuePin()->PinType.PinSubCategoryObject.Get()
                == FCk_Handle_ProceduralLimb::StaticStruct())
        || NOT TestTrue(TEXT("Get_SettledRevision returns int64"),
            Revision->GetReturnValuePin()->PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
        || NOT TestTrue(TEXT("receipt accepted-pose field is bool"),
            ReceiptBreak->FindPin(TEXT("_HasAcceptedPose"))->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
        || NOT TestTrue(TEXT("page result field is the reflected read-result enum"),
            PageBreak->FindPin(TEXT("_Result"))->PinType.PinSubCategoryObject.Get()
                == StaticEnum<ECk_ProceduralAction_ReadResult>())
        || NOT TestTrue(TEXT("validation status field is the reflected report enum"),
            ValidationBreak->FindPin(TEXT("_Status"))->PinType.PinSubCategoryObject.Get()
                == StaticEnum<ECk_ProceduralAction_ReportStatus>())
        || NOT TestTrue(TEXT("validation diagnostics field is a reflected array"),
            ValidationBreak->FindPin(TEXT("_Diagnostics"))->PinType.ContainerType
                == EPinContainerType::Array)
        || NOT TestTrue(TEXT("play spec delivery field is the reflected delivery enum"),
            MakePlaySpec->FindPin(TEXT("_Delivery"))->PinType.PinSubCategoryObject.Get()
                == StaticEnum<ECk_ProceduralAction_Delivery>()))
    { return false; }

    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    return TestTrue(TEXT("Articulation Blueprint nodes compile"),
        (Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)
        && Blueprint->GeneratedClass != nullptr);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_BlueprintParity_LoadedClipVm,
    "Ck.ProceduralAnimation.Action.Editor.BlueprintParity.LoadedClipVm",
    ck_test_procedural_action_blueprint::TestFlags)

auto
    FCkTest_ProceduralAction_BlueprintParity_LoadedClipVm::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_blueprint;

    auto Blueprint = TStrongObjectPtr<UBlueprint>{FKismetEditorUtilities::CreateBlueprint(
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass(), GetTransientPackage(),
        FName(*FString::Printf(TEXT("ProceduralActionLoadedVm_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass())};
    auto* Graph = Blueprint.IsValid() ? FBlueprintEditorUtils::FindEventGraph(Blueprint.Get()) : nullptr;
    if (NOT TestNotNull(TEXT("transient Blueprint event graph"), Graph))
    { return false; }

    auto PlayerType = FEdGraphPinType{};
    PlayerType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    PlayerType.PinSubCategoryObject = FCk_Handle_ProceduralActionPlayer::StaticStruct();
    auto ActionType = FEdGraphPinType{};
    ActionType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    ActionType.PinSubCategoryObject = FCk_Handle_ProceduralAction::StaticStruct();
    auto HandleType = FEdGraphPinType{};
    HandleType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    HandleType.PinSubCategoryObject = FCk_Handle::StaticStruct();
    auto ActionSnapshotType = FEdGraphPinType{};
    ActionSnapshotType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    ActionSnapshotType.PinSubCategoryObject = FCk_ProceduralAction_DebugInstance::StaticStruct();
    auto SubjectSnapshotType = FEdGraphPinType{};
    SubjectSnapshotType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    SubjectSnapshotType.PinSubCategoryObject = FCk_ProceduralAction_DebugSubject::StaticStruct();
    const FName PlayerName{TEXT("VmPlayer")};
    const FName ActionName{TEXT("VmCreatedAction")};
    const FName ActionBaseName{TEXT("VmActionBase")};
    const FName SubjectBaseName{TEXT("VmSubjectBase")};
    const FName ActionSnapshotName{TEXT("VmActionSnapshot")};
    const FName SubjectSnapshotName{TEXT("VmSubjectSnapshot")};
    if (NOT TestTrue(TEXT("VM Blueprint owns typed player variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), PlayerName, PlayerType))
        || NOT TestTrue(TEXT("VM Blueprint owns typed result variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), ActionName, ActionType))
        || NOT TestTrue(TEXT("VM Blueprint owns action debug input"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), ActionBaseName, HandleType))
        || NOT TestTrue(TEXT("VM Blueprint owns subject debug input"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), SubjectBaseName, HandleType))
        || NOT TestTrue(TEXT("VM Blueprint owns action debug output"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), ActionSnapshotName, ActionSnapshotType))
        || NOT TestTrue(TEXT("VM Blueprint owns subject debug output"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), SubjectSnapshotName, SubjectSnapshotType)))
    { return false; }

    auto* Event = NewObject<UK2Node_Event>(Graph);
    Event->EventReference.SetExternalMember(
        GET_FUNCTION_NAME_CHECKED(UCk_Test_RuntimeMeshBpHarness_UE, ComposeResult),
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass());
    Event->bOverrideFunction = true;
    Graph->AddNode(Event, false, false);
    Event->CreateNewGuid();
    Event->PostPlacedNewNode();
    Event->AllocateDefaultPins();

    auto* Function = UCk_Utils_ProceduralAction_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAction_UE, Create_FromClip));
    if (NOT TestNotNull(TEXT("clip create public UFunction"), Function))
    { return false; }
    auto* Loaded = DoAddCall(*Graph, *Function);
    auto* MakeSpec = NewObject<UK2Node_MakeStruct>(Graph);
    MakeSpec->StructType = FCk_ProceduralAction_PlaySpec::StaticStruct();
    Graph->AddNode(MakeSpec, false, false);
    MakeSpec->CreateNewGuid();
    MakeSpec->PostPlacedNewNode();
    MakeSpec->AllocateDefaultPins();
    auto* PlayerGet = NewObject<UK2Node_VariableGet>(Graph);
    PlayerGet->VariableReference.SetSelfMember(PlayerName);
    Graph->AddNode(PlayerGet, false, false);
    PlayerGet->CreateNewGuid();
    PlayerGet->PostPlacedNewNode();
    PlayerGet->AllocateDefaultPins();
    auto* ActionSet = NewObject<UK2Node_VariableSet>(Graph);
    ActionSet->VariableReference.SetSelfMember(ActionName);
    Graph->AddNode(ActionSet, false, false);
    ActionSet->CreateNewGuid();
    ActionSet->PostPlacedNewNode();
    ActionSet->AllocateDefaultPins();

    constexpr auto BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    auto* BodyPulse = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
    auto* ClipPin = MakeSpec->FindPin(TEXT("_Clip"));
    auto* DeliveryPin = MakeSpec->FindPin(TEXT("_Delivery"));
    if (NOT TestNotNull(TEXT("saved BodyPulse clip"), BodyPulse)
        || NOT TestNotNull(TEXT("play spec clip pin"), ClipPin)
        || NOT TestNotNull(TEXT("play spec delivery pin"), DeliveryPin))
    { return false; }
    Graph->GetSchema()->TrySetDefaultValue(*ClipPin, BodyPulse->GetPathName());
    Graph->GetSchema()->TrySetDefaultValue(*DeliveryPin, TEXT("Explicit"));
    const auto Connected =
        DoConnect(*Graph, Event->FindPin(UEdGraphSchema_K2::PN_Then),
            Loaded->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Loaded->FindPin(UEdGraphSchema_K2::PN_Then),
            ActionSet->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Event->FindPin(TEXT("InEntity")),
            Loaded->FindPin(TEXT("InScope")))
        && DoConnect(*Graph, PlayerGet->GetValuePin(), Loaded->FindPin(TEXT("InPlayer")))
        && DoConnect(*Graph,
            MakeSpec->FindPin(FCk_ProceduralAction_PlaySpec::StaticStruct()->GetFName()),
            Loaded->FindPin(TEXT("InSpec")))
        && DoConnect(*Graph, Loaded->GetReturnValuePin(),
            ActionSet->FindPin(ActionName, EGPD_Input));
    if (NOT TestTrue(TEXT("VM loaded clip graph pins connect"), Connected))
    { return false; }
    auto* InspectEvent = NewObject<UK2Node_Event>(Graph);
    InspectEvent->EventReference.SetExternalMember(
        GET_FUNCTION_NAME_CHECKED(UCk_Test_RuntimeMeshBpHarness_UE, InspectComposition),
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass());
    InspectEvent->bOverrideFunction = true;
    Graph->AddNode(InspectEvent, false, false);
    InspectEvent->CreateNewGuid();
    InspectEvent->PostPlacedNewNode();
    InspectEvent->AllocateDefaultPins();
    auto* DebugClass = UCk_Utils_ProceduralAnimation_Debug_UE::StaticClass();
    auto* ActionSnapshotFunction = DebugClass->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAnimation_Debug_UE, Get_ActionSnapshot));
    auto* SubjectSnapshotFunction = DebugClass->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAnimation_Debug_UE, Get_ControlSnapshot));
    if (NOT TestNotNull(TEXT("action snapshot UFunction"), ActionSnapshotFunction)
        || NOT TestNotNull(TEXT("subject snapshot UFunction"), SubjectSnapshotFunction))
    { return false; }
    auto* ActionSnapshotCall = DoAddCall(*Graph, *ActionSnapshotFunction);
    auto* SubjectSnapshotCall = DoAddCall(*Graph, *SubjectSnapshotFunction);
    auto* ActionBaseGet = NewObject<UK2Node_VariableGet>(Graph);
    ActionBaseGet->VariableReference.SetSelfMember(ActionBaseName);
    Graph->AddNode(ActionBaseGet, false, false);
    ActionBaseGet->CreateNewGuid();
    ActionBaseGet->PostPlacedNewNode();
    ActionBaseGet->AllocateDefaultPins();
    auto* SubjectBaseGet = NewObject<UK2Node_VariableGet>(Graph);
    SubjectBaseGet->VariableReference.SetSelfMember(SubjectBaseName);
    Graph->AddNode(SubjectBaseGet, false, false);
    SubjectBaseGet->CreateNewGuid();
    SubjectBaseGet->PostPlacedNewNode();
    SubjectBaseGet->AllocateDefaultPins();
    auto* ActionSnapshotSet = NewObject<UK2Node_VariableSet>(Graph);
    ActionSnapshotSet->VariableReference.SetSelfMember(ActionSnapshotName);
    Graph->AddNode(ActionSnapshotSet, false, false);
    ActionSnapshotSet->CreateNewGuid();
    ActionSnapshotSet->PostPlacedNewNode();
    ActionSnapshotSet->AllocateDefaultPins();
    auto* SubjectSnapshotSet = NewObject<UK2Node_VariableSet>(Graph);
    SubjectSnapshotSet->VariableReference.SetSelfMember(SubjectSnapshotName);
    Graph->AddNode(SubjectSnapshotSet, false, false);
    SubjectSnapshotSet->CreateNewGuid();
    SubjectSnapshotSet->PostPlacedNewNode();
    SubjectSnapshotSet->AllocateDefaultPins();
    const auto SnapshotsConnected =
        DoConnect(*Graph, InspectEvent->FindPin(UEdGraphSchema_K2::PN_Then),
            ActionSnapshotSet->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, ActionSnapshotSet->FindPin(UEdGraphSchema_K2::PN_Then),
            SubjectSnapshotSet->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, ActionBaseGet->GetValuePin(),
            ActionSnapshotCall->FindPin(TEXT("InAction")))
        && DoConnect(*Graph, SubjectBaseGet->GetValuePin(),
            SubjectSnapshotCall->FindPin(TEXT("InSubject")))
        && DoConnect(*Graph, ActionSnapshotCall->GetReturnValuePin(),
            ActionSnapshotSet->FindPin(ActionSnapshotName, EGPD_Input))
        && DoConnect(*Graph, SubjectSnapshotCall->GetReturnValuePin(),
            SubjectSnapshotSet->FindPin(SubjectSnapshotName, EGPD_Input));
    if (NOT TestTrue(TEXT("snapshot Blueprint VM pins connect"), SnapshotsConnected))
    { return false; }
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (NOT TestTrue(TEXT("VM loaded clip graph compiles"),
        (Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)
        && Blueprint->GeneratedClass != nullptr))
    { return false; }

    auto Fixture = FTestWorldWrapper{};
    if (NOT TestTrue(TEXT("production Game world created"),
            Fixture.CreateTestWorld(EWorldType::Game))
        || NOT TestTrue(TEXT("production Game world began play"),
            Fixture.BeginPlayInTestWorld()))
    {
        Fixture.ForwardErrorMessages(this);
        return false;
    }
    auto* World = Fixture.GetTestWorld();
    auto Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
    auto RootTransform = UCk_Utils_Transform_UE::Add(Root, FTransform::Identity,
        ECk_Replication::DoesNotReplicate);
    auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity, FTransform::Identity,
        ECk_Replication::DoesNotReplicate);
    auto Layout = FCk_ProceduralArticulation_Layout{};
    Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
    Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
    auto SubjectSpec = FCk_ProceduralArticulation_Spec{
        MoveTemp(Layout), TArray<FCk_ProceduralLimb_Binding>{}};
    SubjectSpec.Set_Presentation(Presentation);
    auto Subject = UCk_Utils_ProceduralArticulation_UE::Add(RootTransform, SubjectSpec);
    auto Player = UCk_Utils_ProceduralActionPlayer_UE::Add(Subject,
        FCk_ProceduralActionPlayer_Spec{});
    auto Scope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
    auto Harness = TStrongObjectPtr<UCk_Test_RuntimeMeshBpHarness_UE>{
        NewObject<UCk_Test_RuntimeMeshBpHarness_UE>(GetTransientPackage(),
            Blueprint->GeneratedClass)};
    auto* PlayerProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, PlayerName);
    auto* ActionProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, ActionName);
    auto* ActionBaseProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, ActionBaseName);
    auto* SubjectBaseProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, SubjectBaseName);
    auto* ActionSnapshotProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, ActionSnapshotName);
    auto* SubjectSnapshotProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, SubjectSnapshotName);
    if (NOT TestTrue(TEXT("production body-only subject, player, and scope composed"),
            ck::IsValid(Subject) && ck::IsValid(Player) && ck::IsValid(Scope))
        || NOT TestNotNull(TEXT("VM player reflected member"), PlayerProperty)
        || NOT TestNotNull(TEXT("VM action reflected member"), ActionProperty)
        || NOT TestNotNull(TEXT("VM action debug input member"), ActionBaseProperty)
        || NOT TestNotNull(TEXT("VM subject debug input member"), SubjectBaseProperty)
        || NOT TestNotNull(TEXT("VM action debug output member"), ActionSnapshotProperty)
        || NOT TestNotNull(TEXT("VM subject debug output member"), SubjectSnapshotProperty))
    {
        Fixture.DestroyTestWorld(true);
        Fixture.ForwardErrorMessages(this);
        return false;
    }
    PlayerProperty->CopyCompleteValue(PlayerProperty->ContainerPtrToValuePtr<void>(Harness.Get()),
        &Player);
    Harness->ComposeResult(RootTransform, Scope,
        FCk_RuntimeMeshDisplay_Spec{}, FCk_JoltBody_Spec{});
    auto Action = FCk_Handle_ProceduralAction{};
    ActionProperty->CopyCompleteValue(&Action,
        ActionProperty->ContainerPtrToValuePtr<void>(Harness.Get()));
    if (NOT TestTrue(TEXT("Blueprint VM created loaded action through public node"),
        ck::IsValid(Action)))
    {
        Fixture.DestroyTestWorld(true);
        Fixture.ForwardErrorMessages(this);
        return false;
    }
    const auto Identity = UCk_Utils_ProceduralAction_UE::Get_Identity(Action);
    auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(Player, Identity, 0, 64);
    for (auto TickIndex = 0; TickIndex < 600 && NOT Page.Get_Terminal(); ++TickIndex)
    {
        if (NOT Fixture.TickTestWorld(1.0f / 60.0f))
        { break; }
        Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(Player, Identity, 0, 64);
    }
    TestTrue(TEXT("VM-loaded clip reached terminal production receipt"), Page.Get_Terminal());
    auto AcceptedBody = false;
    for (const auto& Record : Page.Get_Records())
    {
        AcceptedBody |= Record.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose
            && Record.Get_Receipt().Get_HasAcceptedPose()
            && Record.Get_Receipt().Get_CurrentPose().Get_HasBodyPresentation();
    }
    TestTrue(TEXT("VM-loaded clip published accepted body pose"), AcceptedBody);
    TestEqual(TEXT("VM-loaded clip completed"),
        UCk_Utils_ProceduralAction_UE::Get_Result(Action).Get_Outcome(),
        ECk_ProceduralAction_Outcome::Completed);
    const auto Recovery = UCk_Utils_ProceduralAction_UE::Get_Result(Action).Get_Recovery();
    TestTrue(TEXT("VM body-only recovery is complete or not required"),
        Recovery == ECk_ProceduralAction_Recovery::Succeeded
            || Recovery == ECk_ProceduralAction_Recovery::NotRequired);
    const auto ActionBase = Action.ConvertToHandle();
    const auto SubjectBase = Subject.ConvertToHandle();
    ActionBaseProperty->CopyCompleteValue(
        ActionBaseProperty->ContainerPtrToValuePtr<void>(Harness.Get()), &ActionBase);
    SubjectBaseProperty->CopyCompleteValue(
        SubjectBaseProperty->ContainerPtrToValuePtr<void>(Harness.Get()), &SubjectBase);
    Harness->InspectComposition(FCk_Handle_RuntimeMeshDisplay{}, FCk_Handle_JoltBody{});
    auto ActionSnapshot = FCk_ProceduralAction_DebugInstance{};
    auto SubjectSnapshot = FCk_ProceduralAction_DebugSubject{};
    ActionSnapshotProperty->CopyCompleteValue(&ActionSnapshot,
        ActionSnapshotProperty->ContainerPtrToValuePtr<void>(Harness.Get()));
    SubjectSnapshotProperty->CopyCompleteValue(&SubjectSnapshot,
        SubjectSnapshotProperty->ContainerPtrToValuePtr<void>(Harness.Get()));
    TestTrue(TEXT("Blueprint VM action snapshot available"), ActionSnapshot.Get_Available());
    TestEqual(TEXT("Blueprint VM action snapshot identity pinned"),
        ActionSnapshot.Get_Identity().Get_Value(), Identity.Get_Value());
    TestEqual(TEXT("Blueprint VM action snapshot reports finished production action"),
        ActionSnapshot.Get_State(), ECk_ProceduralAction_State::Finished);
    TestTrue(TEXT("Blueprint VM subject snapshot available"), SubjectSnapshot.Get_Available());
    TestTrue(TEXT("Blueprint VM subject snapshot settled"),
        SubjectSnapshot.Get_SettledRevision() > 0);
    const auto StaleAction = Action;
    const auto Destroyed = Fixture.DestroyTestWorld(true);
    Fixture.ForwardErrorMessages(this);
    TestTrue(TEXT("VM world was destroyed"), Destroyed);
    TestFalse(TEXT("VM-created action handle invalid after teardown"), ck::IsValid(StaleAction));
    return NOT HasAnyErrors();
}

#endif

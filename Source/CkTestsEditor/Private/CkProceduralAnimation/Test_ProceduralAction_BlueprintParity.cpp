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
    auto* LoadedFunction = UCk_Utils_ProceduralAction_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAction_UE, Create_FromLoadedClip));
    auto* ValidateFunction = UCk_Utils_ProceduralAction_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAction_UE, Validate_Data));
    auto* AdmissionFunction = UCk_Utils_ProceduralActionPlayer_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralActionPlayer_UE, Get_AdmissionReport));
    if (NOT TestNotNull(TEXT("public Articulation Add node"), AddFunction)
        || NOT TestNotNull(TEXT("public Articulation Get_Limb node"), LimbFunction)
        || NOT TestNotNull(TEXT("public Articulation Get_SettledRevision node"), RevisionFunction)
        || NOT TestNotNull(TEXT("public loaded clip create node"), LoadedFunction)
        || NOT TestNotNull(TEXT("public clip validation node"), ValidateFunction)
        || NOT TestNotNull(TEXT("public player admission report node"), AdmissionFunction))
    { return false; }

    auto* Add = DoAddCall(*Graph, *AddFunction);
    auto* Limb = DoAddCall(*Graph, *LimbFunction);
    auto* Revision = DoAddCall(*Graph, *RevisionFunction);
    auto* Loaded = DoAddCall(*Graph, *LoadedFunction);
    auto* Validate = DoAddCall(*Graph, *ValidateFunction);
    auto* Admission = DoAddCall(*Graph, *AdmissionFunction);

    auto* MakeLoadedSpec = NewObject<UK2Node_MakeStruct>(Graph);
    MakeLoadedSpec->StructType = FCk_ProceduralAction_LoadedPlaySpec::StaticStruct();
    Graph->AddNode(MakeLoadedSpec, false, false);
    MakeLoadedSpec->CreateNewGuid();
    MakeLoadedSpec->PostPlacedNewNode();
    MakeLoadedSpec->AllocateDefaultPins();

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
    auto* AdmissionBreak = NewObject<UK2Node_BreakStruct>(Graph);
    AdmissionBreak->StructType = FCk_ProceduralAction_ValidationReport::StaticStruct();
    Graph->AddNode(AdmissionBreak, false, false);
    AdmissionBreak->CreateNewGuid();
    AdmissionBreak->PostPlacedNewNode();
    AdmissionBreak->AllocateDefaultPins();

    auto* PlayerGet = NewObject<UK2Node_VariableGet>(Graph);
    PlayerGet->VariableReference.SetSelfMember(PlayerName);
    Graph->AddNode(PlayerGet, false, false);
    PlayerGet->CreateNewGuid();
    PlayerGet->PostPlacedNewNode();
    PlayerGet->AllocateDefaultPins();

    constexpr auto BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    auto* BodyPulse = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
    auto* ClipPin = MakeLoadedSpec->FindPin(TEXT("_Clip"));
    if (NOT TestNotNull(TEXT("persisted BodyPulse clip"), BodyPulse)
        || NOT TestNotNull(TEXT("loaded play spec Clip input"), ClipPin))
    { return false; }
    Graph->GetSchema()->TrySetDefaultObject(*ClipPin, BodyPulse);

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
    auto* ReportBranch = NewObject<UK2Node_IfThenElse>(Graph);
    Graph->AddNode(ReportBranch, false, false);
    ReportBranch->CreateNewGuid();
    ReportBranch->PostPlacedNewNode();
    ReportBranch->AllocateDefaultPins();

    const auto Connected =
        DoConnect(*Graph, Event->FindPin(UEdGraphSchema_K2::PN_Then),
            Validate->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Validate->FindPin(UEdGraphSchema_K2::PN_Then),
            Admission->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Admission->FindPin(UEdGraphSchema_K2::PN_Then),
            Loaded->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Loaded->FindPin(UEdGraphSchema_K2::PN_Then),
            Add->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Validate->GetReturnValuePin(),
            ValidationBreak->FindPin(FCk_ProceduralAction_ValidationReport::StaticStruct()->GetFName()))
        && DoConnect(*Graph, Admission->GetReturnValuePin(),
            AdmissionBreak->FindPin(FCk_ProceduralAction_ValidationReport::StaticStruct()->GetFName()))
        && DoConnect(*Graph, PlayerGet->GetValuePin(),
            Admission->FindPin(TEXT("InPlayer")))
        && DoConnect(*Graph,
            MakePlaySpec->FindPin(FCk_ProceduralAction_PlaySpec::StaticStruct()->GetFName()),
            Admission->FindPin(TEXT("InSpec")))
        && DoConnect(*Graph, Event->FindPin(TEXT("InEntity")),
            Loaded->FindPin(TEXT("InScope")))
        && DoConnect(*Graph, PlayerGet->GetValuePin(),
            Loaded->FindPin(TEXT("InPlayer")))
        && DoConnect(*Graph,
            MakeLoadedSpec->FindPin(FCk_ProceduralAction_LoadedPlaySpec::StaticStruct()->GetFName()),
            Loaded->FindPin(TEXT("InSpec")))
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
            PageBranch->GetConditionPin())
        && DoConnect(*Graph, PageBranch->FindPin(UEdGraphSchema_K2::PN_Then),
            ReportBranch->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, ValidationBreak->FindPin(TEXT("_Advisory")),
            ReportBranch->GetConditionPin());
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
        || NOT TestNotNull(TEXT("admission status read pin"), AdmissionBreak->FindPin(TEXT("_Status")))
        || NOT TestNotNull(TEXT("admission advisory read pin"), AdmissionBreak->FindPin(TEXT("_Advisory")))
        || NOT TestNotNull(TEXT("admission diagnostics read pin"), AdmissionBreak->FindPin(TEXT("_Diagnostics"))))
    { return false; }
    if (NOT TestTrue(TEXT("loaded play spec carries the persisted clip object"),
            ClipPin->DefaultObject == BodyPulse)
        || NOT TestTrue(TEXT("loaded create accepts reflected loaded spec"),
            Loaded->FindPin(TEXT("InSpec"))->PinType.PinSubCategoryObject.Get()
                == FCk_ProceduralAction_LoadedPlaySpec::StaticStruct()))
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
        || NOT TestTrue(TEXT("admission diagnostics field is a reflected array"),
            AdmissionBreak->FindPin(TEXT("_Diagnostics"))->PinType.ContainerType
                == EPinContainerType::Array))
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
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAction_UE, Create_FromLoadedClip));
    if (NOT TestNotNull(TEXT("loaded clip public UFunction"), Function))
    { return false; }
    auto* Loaded = DoAddCall(*Graph, *Function);
    auto* MakeSpec = NewObject<UK2Node_MakeStruct>(Graph);
    MakeSpec->StructType = FCk_ProceduralAction_LoadedPlaySpec::StaticStruct();
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
    if (NOT TestNotNull(TEXT("saved BodyPulse clip"), BodyPulse)
        || NOT TestNotNull(TEXT("loaded spec clip pin"), ClipPin))
    { return false; }
    Graph->GetSchema()->TrySetDefaultObject(*ClipPin, BodyPulse);
    const auto Connected =
        DoConnect(*Graph, Event->FindPin(UEdGraphSchema_K2::PN_Then),
            Loaded->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Loaded->FindPin(UEdGraphSchema_K2::PN_Then),
            ActionSet->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Event->FindPin(TEXT("InEntity")),
            Loaded->FindPin(TEXT("InScope")))
        && DoConnect(*Graph, PlayerGet->GetValuePin(), Loaded->FindPin(TEXT("InPlayer")))
        && DoConnect(*Graph,
            MakeSpec->FindPin(FCk_ProceduralAction_LoadedPlaySpec::StaticStruct()->GetFName()),
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_BlueprintParity_ManagedPlayLoadedVm,
    "Ck.ProceduralAnimation.Action.Editor.BlueprintParity.ManagedPlayLoadedVm",
    ck_test_procedural_action_blueprint::TestFlags)

auto
    FCkTest_ProceduralAction_BlueprintParity_ManagedPlayLoadedVm::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_blueprint;

    auto Blueprint = TStrongObjectPtr<UBlueprint>{FKismetEditorUtilities::CreateBlueprint(
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass(), GetTransientPackage(),
        FName(*FString::Printf(TEXT("ProceduralActionManagedVm_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass())};
    auto* Graph = Blueprint.IsValid() ? FBlueprintEditorUtils::FindEventGraph(Blueprint.Get()) : nullptr;
    if (NOT TestNotNull(TEXT("transient Blueprint event graph"), Graph))
    { return false; }

    auto PlayerType = FEdGraphPinType{};
    PlayerType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    PlayerType.PinSubCategoryObject = FCk_Handle_ProceduralActionPlayer::StaticStruct();
    auto StartType = FEdGraphPinType{};
    StartType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    StartType.PinSubCategoryObject = FCk_ProceduralAction_ManagedStart::StaticStruct();
    auto CompletionType = FEdGraphPinType{};
    CompletionType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    CompletionType.PinSubCategoryObject = FCk_ProceduralAction_ManagedCompletion::StaticStruct();
    const FName PlayerName{TEXT("VmManagedPlayer")};
    const FName StartName{TEXT("VmManagedStart")};
    const FName CompletionName{TEXT("VmManagedCompletion")};
    if (NOT TestTrue(TEXT("VM Blueprint owns typed player variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), PlayerName, PlayerType))
        || NOT TestTrue(TEXT("VM Blueprint owns managed start variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), StartName, StartType))
        || NOT TestTrue(TEXT("VM Blueprint owns managed completion variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), CompletionName, CompletionType)))
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
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralAction_UE, Play_Loaded));
    if (NOT TestNotNull(TEXT("managed loaded play public UFunction"), Function))
    { return false; }
    const auto* CompletionProperty = CastField<FDelegateProperty>(Function->FindPropertyByName(TEXT("InOnCompleted")));
    if (NOT TestNotNull(TEXT("managed completion is a dynamic delegate parameter"), CompletionProperty)
        || NOT TestNotNull(TEXT("managed completion signature"), CompletionProperty->SignatureFunction.Get()))
    { return false; }
    auto* Play = DoAddCall(*Graph, *Function);
    auto* MakeSpec = NewObject<UK2Node_MakeStruct>(Graph);
    MakeSpec->StructType = FCk_ProceduralAction_LoadedPlaySpec::StaticStruct();
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
    auto* StartSet = NewObject<UK2Node_VariableSet>(Graph);
    StartSet->VariableReference.SetSelfMember(StartName);
    Graph->AddNode(StartSet, false, false);
    StartSet->CreateNewGuid();
    StartSet->PostPlacedNewNode();
    StartSet->AllocateDefaultPins();
    constexpr auto SelectNewNode = false;
    auto* Completed = UK2Node_CustomEvent::CreateFromFunction(FVector2D::ZeroVector, Graph,
        TEXT("OnVmManagedCompleted"), CompletionProperty->SignatureFunction.Get(), SelectNewNode);
    auto* CompletionSet = NewObject<UK2Node_VariableSet>(Graph);
    CompletionSet->VariableReference.SetSelfMember(CompletionName);
    Graph->AddNode(CompletionSet, false, false);
    CompletionSet->CreateNewGuid();
    CompletionSet->PostPlacedNewNode();
    CompletionSet->AllocateDefaultPins();

    constexpr auto BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/"
        "Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    auto* BodyPulse = LoadObject<UCk_ProceduralAction_Data>(nullptr, BodyPulsePath);
    auto* ClipPin = MakeSpec->FindPin(TEXT("_Clip"));
    if (NOT TestNotNull(TEXT("saved BodyPulse clip"), BodyPulse)
        || NOT TestNotNull(TEXT("loaded spec clip pin"), ClipPin)
        || NOT TestNotNull(TEXT("completion custom event"), Completed))
    { return false; }
    Graph->GetSchema()->TrySetDefaultObject(*ClipPin, BodyPulse);
    const auto Connected =
        DoConnect(*Graph, Event->FindPin(UEdGraphSchema_K2::PN_Then),
            Play->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Play->FindPin(UEdGraphSchema_K2::PN_Then),
            StartSet->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Event->FindPin(TEXT("InEntity")), Play->FindPin(TEXT("InScope")))
        && DoConnect(*Graph, PlayerGet->GetValuePin(), Play->FindPin(TEXT("InPlayer")))
        && DoConnect(*Graph,
            MakeSpec->FindPin(FCk_ProceduralAction_LoadedPlaySpec::StaticStruct()->GetFName()),
            Play->FindPin(TEXT("InSpec")))
        && DoConnect(*Graph, Completed->FindPin(UK2Node_Event::DelegateOutputName),
            Play->FindPin(TEXT("InOnCompleted")))
        && DoConnect(*Graph, Play->GetReturnValuePin(), StartSet->FindPin(StartName, EGPD_Input))
        && DoConnect(*Graph, Completed->FindPin(UEdGraphSchema_K2::PN_Then),
            CompletionSet->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Completed->FindPin(TEXT("InCompletion")),
            CompletionSet->FindPin(CompletionName, EGPD_Input));
    if (NOT TestTrue(TEXT("VM managed play graph pins connect"), Connected))
    { return false; }
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (NOT TestTrue(TEXT("VM managed play graph compiles"),
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
    auto* StartProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, StartName);
    auto* CompletionMember = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, CompletionName);
    if (NOT TestTrue(TEXT("production body-only subject, player, and scope composed"),
            ck::IsValid(Subject) && ck::IsValid(Player) && ck::IsValid(Scope))
        || NOT TestNotNull(TEXT("VM player reflected member"), PlayerProperty)
        || NOT TestNotNull(TEXT("VM start reflected member"), StartProperty)
        || NOT TestNotNull(TEXT("VM completion reflected member"), CompletionMember))
    {
        Fixture.DestroyTestWorld(true);
        Fixture.ForwardErrorMessages(this);
        return false;
    }
    PlayerProperty->CopyCompleteValue(PlayerProperty->ContainerPtrToValuePtr<void>(Harness.Get()),
        &Player);
    Harness->ComposeResult(RootTransform, Scope,
        FCk_RuntimeMeshDisplay_Spec{}, FCk_JoltBody_Spec{});
    auto Start = FCk_ProceduralAction_ManagedStart{};
    StartProperty->CopyCompleteValue(&Start, StartProperty->ContainerPtrToValuePtr<void>(Harness.Get()));
    auto Completion = FCk_ProceduralAction_ManagedCompletion{};
    CompletionMember->CopyCompleteValue(&Completion,
        CompletionMember->ContainerPtrToValuePtr<void>(Harness.Get()));
    TestTrue(TEXT("Blueprint VM started a managed action through the public node"),
        ck::IsValid(Start.Get_Action()) && Start.Get_Identity().Get_Value() > 0);
    TestEqual(TEXT("Blueprint VM completion does not fire inside the node"),
        Completion.Get_Identity().Get_Value(), int64{0});
    for (auto TickIndex = 0; TickIndex < 600 && Completion.Get_Identity().Get_Value() == 0; ++TickIndex)
    {
        if (NOT Fixture.TickTestWorld(1.0f / 60.0f))
        { break; }
        CompletionMember->CopyCompleteValue(&Completion,
            CompletionMember->ContainerPtrToValuePtr<void>(Harness.Get()));
    }
    TestEqual(TEXT("Blueprint custom event received the managed completion"),
        Completion.Get_Identity().Get_Value(), Start.Get_Identity().Get_Value());
    TestEqual(TEXT("Blueprint VM managed BodyPulse completed"),
        Completion.Get_Result().Get_Outcome(), ECk_ProceduralAction_Outcome::Completed);
    TestEqual(TEXT("Blueprint VM managed delivery is complete"),
        Completion.Get_Delivery(), ECk_ProceduralAction_DeliveryStatus::Complete);
    const auto Destroyed = Fixture.DestroyTestWorld(true);
    Fixture.ForwardErrorMessages(this);
    TestTrue(TEXT("VM world was destroyed"), Destroyed);
    return NOT HasAnyErrors();
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_BlueprintParity_MirrorVm,
    "Ck.ProceduralAnimation.Action.Editor.BlueprintParity.MirrorVm",
    ck_test_procedural_action_blueprint::TestFlags)

auto
    FCkTest_ProceduralAction_BlueprintParity_MirrorVm::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_blueprint;

    auto Blueprint = TStrongObjectPtr<UBlueprint>{FKismetEditorUtilities::CreateBlueprint(
        UCk_Test_RuntimeMeshBpHarness_UE::StaticClass(), GetTransientPackage(),
        FName(*FString::Printf(TEXT("ProceduralActionMirrorVm_%s"),
            *FGuid::NewGuid().ToString(EGuidFormats::Digits))),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass())};
    auto* Graph = Blueprint.IsValid() ? FBlueprintEditorUtils::FindEventGraph(Blueprint.Get()) : nullptr;
    if (NOT TestNotNull(TEXT("transient Blueprint event graph"), Graph))
    { return false; }

    auto DraftType = FEdGraphPinType{};
    DraftType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    DraftType.PinSubCategoryObject = FCk_ProceduralAction_ClipDraft::StaticStruct();
    auto ReportType = FEdGraphPinType{};
    ReportType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    ReportType.PinSubCategoryObject = FCk_ProceduralAction_ValidationReport::StaticStruct();
    const FName DraftName{TEXT("VmMirrorDraft")};
    const FName ReportName{TEXT("VmMirrorReport")};
    if (NOT TestTrue(TEXT("VM Blueprint owns a draft variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), DraftName, DraftType))
        || NOT TestTrue(TEXT("VM Blueprint owns a report variable"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), ReportName, ReportType)))
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

    auto* Function = UCk_Utils_ProceduralActionBuilder_UE::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(UCk_Utils_ProceduralActionBuilder_UE, Mirror_Slot));
    if (NOT TestNotNull(TEXT("draft mirror public UFunction"), Function))
    { return false; }
    auto* Mirror = DoAddCall(*Graph, *Function);
    auto* MakeMirror = NewObject<UK2Node_MakeStruct>(Graph);
    MakeMirror->StructType = FCk_ProceduralAction_MirrorSpec::StaticStruct();
    Graph->AddNode(MakeMirror, false, false);
    MakeMirror->CreateNewGuid();
    MakeMirror->PostPlacedNewNode();
    MakeMirror->AllocateDefaultPins();
    auto* DraftGet = NewObject<UK2Node_VariableGet>(Graph);
    DraftGet->VariableReference.SetSelfMember(DraftName);
    Graph->AddNode(DraftGet, false, false);
    DraftGet->CreateNewGuid();
    DraftGet->PostPlacedNewNode();
    DraftGet->AllocateDefaultPins();
    auto* ReportSet = NewObject<UK2Node_VariableSet>(Graph);
    ReportSet->VariableReference.SetSelfMember(ReportName);
    Graph->AddNode(ReportSet, false, false);
    ReportSet->CreateNewGuid();
    ReportSet->PostPlacedNewNode();
    ReportSet->AllocateDefaultPins();

    auto* SourcePin = MakeMirror->FindPin(TEXT("_SourceSlot"));
    auto* DestinationPin = MakeMirror->FindPin(TEXT("_DestinationSlot"));
    if (NOT TestNotNull(TEXT("mirror source slot pin"), SourcePin)
        || NOT TestNotNull(TEXT("mirror destination slot pin"), DestinationPin))
    { return false; }
    Graph->GetSchema()->TrySetDefaultValue(*SourcePin, TEXT("Left"));
    Graph->GetSchema()->TrySetDefaultValue(*DestinationPin, TEXT("Right"));
    const auto Connected =
        DoConnect(*Graph, Event->FindPin(UEdGraphSchema_K2::PN_Then),
            Mirror->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, Mirror->FindPin(UEdGraphSchema_K2::PN_Then),
            ReportSet->FindPin(UEdGraphSchema_K2::PN_Execute))
        && DoConnect(*Graph, DraftGet->GetValuePin(), Mirror->FindPin(TEXT("InDraft")))
        && DoConnect(*Graph,
            MakeMirror->FindPin(FCk_ProceduralAction_MirrorSpec::StaticStruct()->GetFName()),
            Mirror->FindPin(TEXT("InMirror")))
        && DoConnect(*Graph, Mirror->GetReturnValuePin(), ReportSet->FindPin(ReportName, EGPD_Input));
    if (NOT TestTrue(TEXT("VM mirror graph pins connect"), Connected))
    { return false; }

    auto* MakeLoaded = NewObject<UK2Node_MakeStruct>(Graph);
    MakeLoaded->StructType = FCk_ProceduralAction_LoadedPlaySpec::StaticStruct();
    Graph->AddNode(MakeLoaded, false, false);
    MakeLoaded->CreateNewGuid();
    MakeLoaded->PostPlacedNewNode();
    MakeLoaded->AllocateDefaultPins();
    TestNotNull(TEXT("A loaded play spec exposes the runtime mirror list to Blueprint"),
        MakeLoaded->FindPin(TEXT("_Mirrors")));
    Graph->RemoveNode(MakeLoaded);

    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (NOT TestTrue(TEXT("VM mirror graph compiles"),
        (Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)
        && Blueprint->GeneratedClass != nullptr))
    { return false; }

    auto Draft = FCk_ProceduralAction_ClipDraft{};
    Draft.Set_Slots(TArray<FCk_ProceduralAction_Slot>{FCk_ProceduralAction_Slot{FName{TEXT("Left")}},
        FCk_ProceduralAction_Slot{FName{TEXT("Right")}}});
    auto Track = FCk_ProceduralAction_LimbTrack{FName{TEXT("Left")}};
    Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, FVector{10.0, 20.0, 30.0}},
        FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, FVector{40.0, 50.0, 60.0}}});
    auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Reach")}, FCk_Time{0.5}};
    Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
    Draft.Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});

    auto Harness = TStrongObjectPtr<UCk_Test_RuntimeMeshBpHarness_UE>{
        NewObject<UCk_Test_RuntimeMeshBpHarness_UE>(GetTransientPackage(), Blueprint->GeneratedClass)};
    auto* DraftProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, DraftName);
    auto* ReportProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, ReportName);
    if (NOT TestNotNull(TEXT("VM draft reflected member"), DraftProperty)
        || NOT TestNotNull(TEXT("VM report reflected member"), ReportProperty))
    { return false; }
    DraftProperty->CopyCompleteValue(DraftProperty->ContainerPtrToValuePtr<void>(Harness.Get()), &Draft);
    auto DisplayOwner = FCk_Handle_Transform{};
    auto Entity = FCk_Handle{};
    Harness->ComposeResult(DisplayOwner, Entity, FCk_RuntimeMeshDisplay_Spec{}, FCk_JoltBody_Spec{});

    auto Report = FCk_ProceduralAction_ValidationReport{};
    ReportProperty->CopyCompleteValue(&Report, ReportProperty->ContainerPtrToValuePtr<void>(Harness.Get()));
    auto Mirrored = FCk_ProceduralAction_ClipDraft{};
    DraftProperty->CopyCompleteValue(&Mirrored, DraftProperty->ContainerPtrToValuePtr<void>(Harness.Get()));
    TestEqual(TEXT("Blueprint VM mirror reports Valid"), Report.Get_Status(), ECk_ProceduralAction_ReportStatus::Valid);
    const auto Tracks = Mirrored.Get_Phases().IsEmpty()
        ? TArray<FCk_ProceduralAction_LimbTrack>{} : Mirrored.Get_Phases()[0].Get_LimbTracks();
    if (TestEqual(TEXT("Blueprint VM mirror writes the destination track into the referenced draft"), Tracks.Num(), 2))
    {
        TestEqual(TEXT("The written track is the destination slot"), Tracks[1].Get_Slot(), FName{TEXT("Right")});
        TestTrue(TEXT("The written keys are reflected across XZ"), Tracks[1].Get_PositionKeys().Num() == 2
            && Tracks[1].Get_PositionKeys()[0].Get_Value().Equals(FVector{10.0, -20.0, 30.0})
            && Tracks[1].Get_PositionKeys()[1].Get_Value().Equals(FVector{40.0, -50.0, 60.0}));
    }
    return NOT HasAnyErrors();
}

#endif

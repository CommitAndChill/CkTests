#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_HexBlueprintGraph.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MakeStruct.h"
#include "ImageUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Tests/AutomationCommon.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"

namespace ck_test_hex_board_bp
{
    constexpr auto AssetPackage = TEXT("/CkTests/CkGrid/HexGrid/BP_HexGrid_BoardFixture");

    auto FactoryGraph(UBlueprint& BP) -> UEdGraph*
    {
        const FName FactoryName = ck_test_hex_geometry_blueprint::GraphName(TEXT("TryCreate"));
        for (UEdGraph* Graph : BP.FunctionGraphs)
        { if (Graph && Graph->GetFName() == FactoryName) { return Graph; } }
        return nullptr;
    }

    auto AuthorFactory(FAutomationTestBase& Test, UEdGraph& Graph) -> bool
    {
        TArray<UK2Node_FunctionEntry*> Entries;
        TArray<UK2Node_CallFunction*> Calls;
        Graph.GetNodesOfClass(Entries);
        Graph.GetNodesOfClass(Calls);
        if (!Test.TestTrue(TEXT("factory has one entry and one native call"), Entries.Num() == 1 && Calls.Num() == 1))
        { return false; }
        auto* Input = Calls[0]->FindPin(TEXT("InSpec"));
        if (!Test.TestNotNull(TEXT("factory spec input"), Input)) { return false; }
        Input->BreakAllPinLinks();
        Entries[0]->RemoveUserDefinedPinByName(TEXT("InSpec"));
        auto Make = [&](UScriptStruct* Type, int32 X, int32 Y)
        {
            FGraphNodeCreator<UK2Node_MakeStruct> Creator(Graph);
            auto* Node = Creator.CreateNode();
            Node->StructType = Type;
            Node->NodePosX = X;
            Node->NodePosY = Y;
            Creator.Finalize();
            return Node;
        };
        auto* Domain = Make(FCk_HexDomain::StaticStruct(), -640, 400);
        auto* Layout = Make(FCk_HexLayout::StaticStruct(), -640, 120);
        auto* Spec = Make(FCk_HexGrid_Spec::StaticStruct(), -165, 250);
        auto* RingRadius = Domain->FindPin(TEXT("_RingRadius"));
        auto* Orientation = Layout->FindPin(TEXT("_Orientation"));
        if (!Test.TestNotNull(TEXT("ring radius input"), RingRadius)
            || !Test.TestNotNull(TEXT("orientation enum input"), Orientation)) { return false; }
        const auto* Schema = Graph.GetSchema();
        Schema->TrySetDefaultValue(*RingRadius, TEXT("1"));
        Schema->TrySetDefaultValue(*Orientation, TEXT("Flat"));
        auto Connect = [&](UK2Node_MakeStruct& From, UEdGraphPin* To)
        {
            auto* Source = From.FindPin(From.StructType->GetFName(), EGPD_Output);
            return Test.TestTrue(From.StructType->GetName() + TEXT(" output connected"),
                Source && To && Schema->TryCreateConnection(Source, To));
        };
        return Connect(*Domain, Spec->FindPin(TEXT("_Domain")))
            && Connect(*Layout, Spec->FindPin(TEXT("_Layout"))) && Connect(*Spec, Input);
    }

    auto ValidateFactory(FAutomationTestBase& Test, UBlueprint& BP) -> bool
    {
        auto* Graph = FactoryGraph(BP);
        if (!Test.TestNotNull(TEXT("saved TryCreate graph"), Graph)) { return false; }
        TArray<UK2Node_CallFunction*> Calls;
        Graph->GetNodesOfClass(Calls);
        if (!Test.TestTrue(TEXT("TryCreate graph calls its native factory"),
            Calls.Num() == 1 && Calls[0]->FunctionReference.GetMemberName() == TEXT("TryCreate")))
        { return false; }
        auto Source = [](UEdGraphNode& Node, FName Pin, UScriptStruct* Expected) -> UK2Node_MakeStruct*
        {
            const auto* Input = Node.FindPin(Pin, EGPD_Input);
            auto* Make = Input && Input->LinkedTo.Num() == 1
                ? Cast<UK2Node_MakeStruct>(Input->LinkedTo[0]->GetOwningNode()) : nullptr;
            return Make && Make->StructType == Expected ? Make : nullptr;
        };
        auto* Spec = Source(*Calls[0], TEXT("InSpec"), FCk_HexGrid_Spec::StaticStruct());
        auto* Layout = Spec ? Source(*Spec, TEXT("_Layout"), FCk_HexLayout::StaticStruct()) : nullptr;
        auto* Domain = Spec ? Source(*Spec, TEXT("_Domain"), FCk_HexDomain::StaticStruct()) : nullptr;
        auto* Radius = Domain ? Domain->FindPin(TEXT("_RingRadius")) : nullptr;
        auto* Orientation = Layout ? Layout->FindPin(TEXT("_Orientation")) : nullptr;
        return Test.TestTrue(TEXT("saved factory uses nested domain and layout nodes with authored defaults"),
            Radius && Orientation && Radius->DefaultValue == TEXT("1") && Orientation->DefaultValue == TEXT("Flat"));
    }

    auto GetFixture(FAutomationTestBase& Test) -> UBlueprint*
    {
        const auto Name = FPackageName::GetShortName(AssetPackage);
        if (FPackageName::DoesPackageExist(AssetPackage))
        {
            auto* Saved = LoadObject<UBlueprint>(nullptr, *(FString(AssetPackage) + TEXT(".") + Name));
            if (!Saved || !ValidateFactory(Test, *Saved)) { return nullptr; }
            FKismetEditorUtilities::CompileBlueprint(Saved);
            return Saved->Status == BS_UpToDate ? Saved : nullptr;
        }
        auto* Package = CreatePackage(AssetPackage);
        auto* BP = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), Package, FName(*Name),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (!BP) { return nullptr; }
        for (const auto* FunctionName : {TEXT("TryCreate"), TEXT("Get_Cells"), TEXT("TryGet_CellFromWorldPoint"),
            TEXT("TryGet_CellFromWorldRay"), TEXT("TryGet_CellWorldCorners"), TEXT("Request_DestroyEntity")})
        {
            const auto* Library = FStringView(FunctionName) == TEXT("Request_DestroyEntity")
                ? UCk_Utils_EntityLifetime_UE::StaticClass() : UCk_Utils_HexGrid_UE::StaticClass();
            auto* Function = Library->FindFunctionByName(FunctionName);
            if (!Function || !ck_test_hex_geometry_blueprint::AddGraph(Test, *BP, *Function)) { return nullptr; }
        }
        auto* Factory = FactoryGraph(*BP);
        if (!Factory || !AuthorFactory(Test, *Factory) || !ValidateFactory(Test, *BP)) { return nullptr; }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status != BS_UpToDate) { return nullptr; }
        const auto File = FPackageName::LongPackageNameToFilename(AssetPackage, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        BP->SetFlags(RF_Public | RF_Standalone);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Package, BP, *File, Args) ? BP : nullptr;
    }

    struct FArgument
    {
        FName Name;
        void* Value;
    };

    auto Call(FAutomationTestBase& Test, UObject& Instance, FName Name,
        std::initializer_list<FArgument> Inputs, std::initializer_list<FArgument> Outputs) -> bool
    {
        auto* Fn = Instance.FindFunction(ck_test_hex_geometry_blueprint::GraphName(Name));
        if (!Test.TestTrue(Name.ToString() + TEXT(" has Blueprint bytecode"), Fn && !Fn->Script.IsEmpty()))
        { return false; }
        FStructOnScope Params(Fn);
        const auto* Native = Name == TEXT("Request_DestroyEntity") ? UCk_Utils_EntityLifetime_UE::StaticClass()
            : UCk_Utils_HexGrid_UE::StaticClass();
        auto* Signature = Native->FindFunctionByName(Name);
        if (!Test.TestNotNull(Name.ToString() + TEXT(" native signature"), Signature))
        { return false; }
        auto Copy = [&](const FArgument& Arg, bool Read)
        {
            auto* Prop = FindFProperty<FProperty>(Fn, Arg.Name);
            auto* Original = FindFProperty<FProperty>(Signature, Arg.Name);
            if (!Test.TestTrue(Arg.Name.ToString(), Prop && Original && Prop->SameType(Original))) { return false; }
            auto* Pin = Prop->ContainerPtrToValuePtr<void>(Params.GetStructMemory());
            Prop->CopyCompleteValue(Read ? Arg.Value : Pin, Read ? Pin : Arg.Value);
            return true;
        };
        for (const auto& Arg : Inputs) { if (!Copy(Arg, false)) { return false; } }
        Instance.ProcessEvent(Fn, Params.GetStructMemory());
        for (const auto& Arg : Outputs) { if (!Copy(Arg, true)) { return false; } }
        if (Name.ToString().StartsWith(TEXT("Try")))
        {
            auto* Result = CastField<FBoolProperty>(Fn->GetReturnProperty());
            return Test.TestNotNull(Name.ToString() + TEXT(" boolean result pin"), Result)
                && Test.TestTrue(Name.ToString(), Result->GetPropertyValue_InContainer(Params.GetStructMemory()));
        }
        return true;
    }

    auto Exercise(FAutomationTestBase& Test, UBlueprint& BP, UWorld* World, FCk_Handle_HexCell& Retained) -> void
    {
        TStrongObjectPtr<UObject> Instance{NewObject<UObject>(GetTransientPackage(), BP.GeneratedClass)};
        auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
        UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
        FCk_Handle_HexGrid Board;
        FTransform Pivot{FVector{120, -40, 20}};
        if (!Call(Test, *Instance, TEXT("TryCreate"), {{TEXT("InOwner"), &Owner}, {TEXT("InInitialTransform"), &Pivot}},
            {{TEXT("OutGrid"), &Board}}) || !Test.TestTrue(TEXT("created board"), ck::IsValid(Board)))
        { return; }
        TArray<FCk_Handle_HexCell> Cells;
        Call(Test, *Instance, TEXT("Get_Cells"), {{TEXT("InGrid"), &Board}}, {{TEXT("ReturnValue"), &Cells}});
        Test.TestEqual(TEXT("seven actual cells"), Cells.Num(), 7);
        auto Center = Pivot.GetLocation();
        FCk_Handle_HexCell ByRay;
        Call(Test, *Instance, TEXT("TryGet_CellFromWorldPoint"), {{TEXT("InGrid"), &Board}, {TEXT("InPoint"), &Center}},
            {{TEXT("OutCell"), &Retained}});
        auto Start = Center + FVector(0,0,25);
        auto Direction = FVector(0,0,-2);
        FVector Hit = Start;
        Call(Test, *Instance, TEXT("TryGet_CellFromWorldRay"), {{TEXT("InGrid"), &Board}, {TEXT("InOrigin"), &Start},
            {TEXT("InDirection"), &Direction}}, {{TEXT("OutCell"), &ByRay}, {TEXT("OutWorldHit"), &Hit}});
        Test.TestTrue(TEXT("both Blueprint picks return the center cell and correct hit"),
            ck::IsValid(Retained) && Cells.Contains(Retained) && ByRay == Retained && Hit.Equals(Center));
        FCk_HexCoord Coord;
        TArray<FVector> Corners;
        Call(Test, *Instance, TEXT("TryGet_CellWorldCorners"), {{TEXT("InGrid"), &Board}, {TEXT("InCoordinate"), &Coord}},
            {{TEXT("OutCorners"), &Corners}});
        if (Test.TestEqual(TEXT("six boundary vertices"), Corners.Num(), 6))
        {
            const auto H = 50.0 * FMath::Sqrt(3.0);
            const FVector Expected[] = {{100,0,0}, {50,H,0}, {-50,H,0}, {-100,0,0}, {-50,-H,0}, {50,-H,0}};
            for (int32 Index = 0; Index < 6; ++Index)
            { Test.TestTrue(TEXT("Blueprint-authored flat vertex"), Corners[Index].Equals(Center + Expected[Index], 1e-7)); }
        }
        auto Base = FCk_Handle{Board};
        Call(Test, *Instance, TEXT("Request_DestroyEntity"), {{TEXT("InHandle"), &Base}}, {});
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCk_HexBoardBlueprint, "Ck.HexGrid.Board.BlueprintAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCk_HexBoardBlueprint::RunTest(const FString&)
{
    auto* BP = ck_test_hex_board_bp::GetFixture(*this);
    if (!TestTrue(TEXT("saved board Blueprint is available"), BP && BP->GeneratedClass && !BP->FunctionGraphs.IsEmpty()))
    { return false; }
    auto Held = MakeShared<TStrongObjectPtr<UBlueprint>>(BP);
    auto Graph = SNew(SGraphEditor).GraphToEdit(ck_test_hex_board_bp::FactoryGraph(*BP));
    auto Window = SNew(SWindow).Title(FText::FromString(TEXT("Hex board factory"))).ClientSize(FVector2D(1440,740))[Graph];
    FSlateApplication::Get().AddWindow(Window);
    Graph->ZoomToFit(false);
    ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this, FCk_NetAutoTest_Assertion::CreateLambda(
        [Graph,Window]()
        {
            TArray<FColor> Pixels;
            FIntVector Size;
            const bool Captured = FSlateApplication::Get().TakeScreenshot(Graph, Pixels, Size);
            Window->RequestDestroyWindow();
            if (!Captured || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() != static_cast<int64>(Size.X) * Size.Y)
            { return false; }
            for (auto& Pixel : Pixels) { Pixel.A = 255; }
            const auto Dir = FPaths::ProjectSavedDir() / TEXT("Automation/HexBoard");
            IFileManager::Get().MakeDirectory(*Dir, true);
            return FImageUtils::SaveImageByExtension(*(Dir / TEXT("Blueprint.png")),
                FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8));
        }), TEXT("Blueprint graph screenshot")));
    auto Retained = MakeShared<FCk_Handle_HexCell>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1,30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,Held,Retained](UWorld* World){ ck_test_hex_board_bp::Exercise(*this, *Held->Get(), World, *Retained); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Retained](){ return !ck::IsValid(*Retained); }), 10.f, TEXT("Blueprint teardown retired the picked cell")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

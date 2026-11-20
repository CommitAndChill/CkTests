#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AssetManagerEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "CkSnapshot/Inspection/CkSnapshot_Inspection_Sha256.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditor.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ReferenceViewer/EdGraph_ReferenceViewer.h"
#include "ReferenceViewer/EdGraphNode_Reference.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"

namespace ck_test_square_reference_viewer
{
    const FName Root{TEXT("/CkFoundation/CkGrid/Utils_CkGrid_ML")};

    auto FindWidget(const TSharedRef<SWidget>& Widget, const FName Type) -> TSharedPtr<SWidget>
    {
        if (!Widget->GetVisibility().IsVisible()) { return {}; }
        if (Widget->GetType() == Type) { return Widget; }
        auto* Children = Widget->GetChildren();
        for (int32 I = 0; Children && I < Children->Num(); ++I)
        {
            if (auto Match = FindWidget(Children->GetChildAt(I), Type)) { return Match; }
        }
        return {};
    }

    auto ReadPins(const UBlueprint& BP, FString& Out) -> bool
    {
        Out.Reset();
        const auto* PinStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Engine.EdGraphPinType"));
        if (!PinStruct) { return false; }
        bool Found2dMacro = false;
        bool Found3dMacro = false;
        for (const UEdGraph* Graph : BP.MacroGraphs)
        {
            if (!Graph) { return false; }
            int32 Inputs = 0, Outputs = 0;
            for (const UEdGraphNode* Node : Graph->Nodes)
            {
                const auto* Tunnel = Cast<UK2Node_Tunnel>(Node);
                if (!Tunnel || Tunnel->GetClass() != UK2Node_Tunnel::StaticClass()) { continue; }
                for (const auto* Pin : Tunnel->Pins)
                {
                    if (!Pin || Pin->bOrphanedPin) { return false; }
                    // Entry tunnels output their external inputs into the graph; exit tunnels receive external outputs.
                    const bool IsInput = Pin->Direction == EGPD_Output;
                    if (IsInput) { ++Inputs; } else { ++Outputs; }
                    FString Type;
                    if (Pin->PinType.PinCategory.IsNone()) { return false; }
                    PinStruct->ExportText(Type, &Pin->PinType, nullptr, nullptr, PPF_None, nullptr);
                    Out += FString::Printf(TEXT("%s\t%s\t%s\t%s\t%s\n"), *Graph->GetName(),
                        *Tunnel->GetName(), IsInput ? TEXT("input") : TEXT("output"), *Pin->PinName.ToString(), *Type);
                }
            }
            const bool HasInterface = Inputs > 0 && Outputs > 0;
            if (Graph->GetFName() == FName(TEXT("ForEach_GridCell (2D)"))) { Found2dMacro = HasInterface; }
            if (Graph->GetFName() == FName(TEXT("ForEach_GridCell (3D)"))) { Found3dMacro = HasInterface; }
        }
        return Found2dMacro && Found3dMacro;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkSquareReferenceViewerGate, "Ck.HexGrid.Release.SquareReferenceViewer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FCkSquareReferenceViewerGate::RunTest(const FString&)
{
    using namespace ck_test_square_reference_viewer;
    const FString File = FPackageName::LongPackageNameToFilename(Root.ToString(), FPackageName::GetAssetPackageExtension());
    TArray<uint8> Bytes;
    if (!TestTrue(TEXT("Original macro package readable"), FFileHelper::LoadFileToArray(Bytes, *File))) { return false; }
    const auto Hash = ck::snapshot::Get_Sha256Hex(Bytes);
    auto* BP = LoadObject<UBlueprint>(nullptr, TEXT("/CkFoundation/CkGrid/Utils_CkGrid_ML.Utils_CkGrid_ML"));
    if (!TestNotNull(TEXT("Original square macro library"), BP)) { return false; }
    for (const UEdGraph* Graph : BP->MacroGraphs)
    {
        AddInfo(FString::Printf(TEXT("Serialized square macro graph %s"), *GetNameSafe(Graph)));
    }
    auto Held = MakeShared<TStrongObjectPtr<UBlueprint>>(BP);
    FString Pins;
    if (!TestTrue(TEXT("Serialized iteration macro has typed public inputs and outputs"), ReadPins(*BP, Pins))) { return false; }
    FKismetEditorUtilities::CompileBlueprint(BP);
    FString CompiledPins;
    if (!TestTrue(TEXT("In-memory compile preserves typed tunnels"), BP->Status == BS_UpToDate &&
        ReadPins(*BP, CompiledPins) && CompiledPins == Pins)) { return false; }

    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FName> References, Dependencies;
    Registry.GetReferencers(Root, References);
    Registry.GetDependencies(Root, Dependencies);
    References.Sort(FNameLexicalLess{});
    Dependencies.Sort(FNameLexicalLess{});
    FString Report = FString::Printf(TEXT("package\t%s\nsha256\t%s\ncompile\tBS_UpToDate\nreferencer_count\t%d\ndependency_count\t%d\n"),
        *Root.ToString(), *Hash, References.Num(), Dependencies.Num());
    for (const auto Name : References) { Report += TEXT("referencer\t") + Name.ToString() + TEXT("\n"); }
    for (const auto Name : Dependencies) { Report += TEXT("dependency\t") + Name.ToString() + TEXT("\n"); }
    Report += TEXT("graph\ttunnel\tmacro_direction\tpin_name\tserialized_pin_type\n") + Pins;
    if (!TestTrue(TEXT("Editor has Slate and can register the Reference Viewer tab"),
        GIsEditor && !IsRunningCommandlet() && FSlateApplication::IsInitialized())) { return false; }
    IAssetManagerEditorModule::Get().OpenReferenceViewerUI(TArray<FName>{Root});
    const auto Directory = FPaths::ProjectSavedDir() / TEXT("Automation/HexPhase8/ReferenceViewer") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [this, Held, File, Hash, Directory, Report, FitFrames = 0]() mutable
        {
            TArray<TSharedRef<SWindow>> Windows;
            auto& Slate = FSlateApplication::Get();
            Slate.GetAllVisibleWindowsOrdered(Windows);
            for (const auto& Window : Windows)
            {
                auto Viewer = FindWidget(Window, TEXT("SReferenceViewer"));
                if (!Viewer) { continue; }
                auto Widget = FindWidget(Viewer.ToSharedRef(), TEXT("SGraphEditor"));
                if (!Widget) { continue; }
                const auto* Graph = Cast<UEdGraph_ReferenceViewer>(StaticCastSharedPtr<SGraphEditor>(Widget)->GetCurrentGraph());
                if (!Graph || Graph->GetCurrentGraphRootIdentifiers() != TArray<FAssetIdentifier>{FAssetIdentifier(Root)}) { continue; }
                TArray<FString> Displayed;
                bool HasRootNode = false;
                for (const UEdGraphNode* Node : Graph->Nodes)
                {
                    const auto* Ref = Cast<UEdGraphNode_Reference>(Node);
                    if (!Ref) { continue; }
                    TArray<FAssetIdentifier> Ids;
                    Ref->GetAllIdentifiers(Ids);
                    for (const auto& Id : Ids)
                    {
                        HasRootNode |= Id == FAssetIdentifier(Root);
                        Displayed.Add(Id.ToString());
                    }
                }
                if (!HasRootNode || Viewer->GetCachedGeometry().GetLocalSize().X < 100) { continue; }
                if (FitFrames++ == 0)
                {
                    StaticCastSharedPtr<SGraphEditor>(Widget)->ZoomToFit(false);
                    return false;
                }
                if (FitFrames < 8) { return false; }
                TArray<FColor> Pixels;
                FIntVector Size;
                if (!Slate.TakeScreenshot(Window, Pixels, Size) || Size.X < 100 || Size.Y < 100 ||
                    Pixels.Num() != int64(Size.X) * Size.Y) { continue; }
                if (!Pixels.ContainsByPredicate([First = Pixels[0]](const FColor& P)
                    { return P.R != First.R || P.G != First.G || P.B != First.B; })) { continue; }
                for (auto& Pixel : Pixels) { Pixel.A = 255; }
                TArray<uint8> After;
                if (!TestTrue(TEXT("Fresh on-disk digest still matches before open/compile"),
                    FFileHelper::LoadFileToArray(After, *File) && ck::snapshot::Get_Sha256Hex(After) == Hash)) { return true; }
                Displayed.Sort();
                FString Evidence = Report + TEXT("viewer_root\t") + Root.ToString() + TEXT("\n");
                for (const auto& Id : Displayed) { Evidence += TEXT("viewer_node\t") + Id + TEXT("\n"); }
                IFileManager::Get().MakeDirectory(*Directory, true);
                TestTrue(TEXT("Real Reference Viewer window saved"), FImageUtils::SaveImageByExtension(
                    *(Directory / TEXT("ReferenceViewer.png")), FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8)));
                TestTrue(TEXT("Registry, serialized pins and rendered root report saved"),
                    FFileHelper::SaveStringToFile(Evidence, *(Directory / TEXT("SquareReferenceViewer.tsv"))));
                AddInfo(Directory);
                return true;
            }
            return false;
        }), 30.0, TEXT("Rendered Reference Viewer widget, exact original root, graph node and native window capture")));
    return true;
}
#endif

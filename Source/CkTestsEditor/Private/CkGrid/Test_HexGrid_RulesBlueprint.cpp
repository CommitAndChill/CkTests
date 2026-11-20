#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_HexBlueprintGraph.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGrid_Spec.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridObject_Spec.h"
#include "CkGrid/HexGridSystem/Blocker/CkHexGridBlocker_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_test_hex_rules_blueprint
{
    using FObject = UCk_Utils_HexGridObject_UE;
    using FBlocker = UCk_Utils_HexGridBlocker_UE;
    using FLife = UCk_Utils_EntityLifetime_UE;
    const FString Directory = TEXT("/CkTests/CkGrid/HexGrid/");

    bool Save(UObject& Asset)
    {
        const auto File = FPackageName::LongPackageNameToFilename(Asset.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        Asset.SetFlags(RF_Public | RF_Standalone);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset.GetOutermost(), &Asset, *File, Args);
    }

    template <typename T, typename TSetup>
    auto Author(const FString& Name, const TSetup& Setup) -> T*
    {
        const auto Path = Directory + Name;
        if (FPackageName::DoesPackageExist(Path))
        { return LoadObject<T>(nullptr, *(Path + TEXT(".") + Name)); }
        auto* Asset = NewObject<T>(CreatePackage(*Path), FName(*Name), RF_Public | RF_Standalone);
        Setup(*Asset);
        return Save(*Asset) ? Asset : nullptr;
    }

    auto RuleBlueprint(FAutomationTestBase& Test) -> UBlueprint*
    {
        const FString Name = TEXT("BP_HexGrid_RulesFixture");
        const auto Path = Directory + Name;
        const bool Exists = FPackageName::DoesPackageExist(Path);
        auto* BP = Exists ? LoadObject<UBlueprint>(nullptr, *(Path + TEXT(".") + Name))
            : FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), CreatePackage(*Path), FName(*Name),
                BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (!BP) { return nullptr; }
        if (!Exists)
        {
            for (const FName FnName : {FName(TEXT("TryAdd")), FName(TEXT("TryResolve_Cells")),
                    FName(TEXT("Get_Blockers")), FName(TEXT("Request_SetActive")), FName(TEXT("Get_IsActive"))})
            {
                auto* Owner = FnName == TEXT("TryAdd") || FnName == TEXT("TryResolve_Cells")
                    ? FObject::StaticClass() : FBlocker::StaticClass();
                auto* Fn = Owner->FindFunctionByName(FnName);
                if (!Fn || !ck_test_hex_geometry_blueprint::AddGraph(Test,*BP,*Fn)) { return nullptr; }
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        }
        FKismetEditorUtilities::CompileBlueprint(BP);
        return BP->Status == BS_UpToDate && (Exists || Save(*BP)) ? BP : nullptr;
    }

    struct FPin
    {
        FName Name;
        void* Value;
    };

    bool Call(FAutomationTestBase& Test, UObject& Instance, UClass* Library, FName Name,
        std::initializer_list<FPin> Inputs, std::initializer_list<FPin> Outputs)
    {
        auto* Fn = Instance.FindFunction(ck_test_hex_geometry_blueprint::GraphName(Name));
        auto* Signature = Library->FindFunctionByName(Name);
        if (!Test.TestTrue(Name.ToString() + TEXT(" has compiled graph"), Fn && !Fn->Script.IsEmpty() && Signature))
        { return false; }
        FStructOnScope Stack(Fn);
        const auto Transfer = [&](FPin Arg, bool Read)
        {
            auto* P = FindFProperty<FProperty>(Fn, Arg.Name);
            auto* Native = FindFProperty<FProperty>(Signature, Arg.Name);
            if (!Test.TestTrue(Arg.Name.ToString(), P && Native && P->SameType(Native))) { return false; }
            auto* Pin = P->ContainerPtrToValuePtr<void>(Stack.GetStructMemory());
            if (const auto* Bool = CastField<FBoolProperty>(P))
            {
                if (Read) { *static_cast<bool*>(Arg.Value) = Bool->GetPropertyValue(Pin); }
                else { Bool->SetPropertyValue(Pin, *static_cast<bool*>(Arg.Value)); }
            }
            else { P->CopyCompleteValue(Read ? Arg.Value : Pin, Read ? Pin : Arg.Value); }
            return true;
        };
        for (const auto& Arg : Inputs) { if (!Transfer(Arg,false)) { return false; } }
        Instance.ProcessEvent(Fn, Stack.GetStructMemory());
        for (const auto& Arg : Outputs) { if (!Transfer(Arg,true)) { return false; } }
        return true;
    }

    struct FScenario
    {
        TStrongObjectPtr<UCk_HexGridObject_AuthoringSpec> ObjectAsset;
        TStrongObjectPtr<UCk_HexGrid_AuthoringSpec> BoardAsset;
        TStrongObjectPtr<UBlueprint> Blueprint;
        TStrongObjectPtr<UObject> Instance;
        FCk_Handle_HexGridBlocker Blocker;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexRulesBlueprint, "Ck.HexGrid.Rules.Blueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexRulesBlueprint::RunTest(const FString&)
{
    using namespace ck_test_hex_rules_blueprint;
    auto S = MakeShared<FScenario>();
    S->ObjectAsset.Reset(Author<UCk_HexGridObject_AuthoringSpec>(TEXT("DA_HexGrid_RulesObject"), [](auto& Asset)
    {
        FCk_HexGridObject_Spec Spec;
        Spec.Set_FootprintOffsets({{1,0}});
        Asset.Set_ObjectSpec(Spec);
    }));
    S->BoardAsset.Reset(Author<UCk_HexGrid_AuthoringSpec>(TEXT("DA_HexGrid_RulesBoard"), [](auto& Asset)
    {
        FCk_HexGrid_Spec Spec;
        FCk_HexGridAuthoredBlocker Blocker;
        Blocker.Set_Cells({FCk_HexCoord{}});
        Blocker.Set_InitiallyActive(false);
        Spec.Set_Blockers({Blocker});
        Asset.Set_Spec(Spec);
    }));
    auto* BP = RuleBlueprint(*this);
    if (!TestTrue(TEXT("Both authored data assets and compiled rules Blueprint available"),
        S->ObjectAsset.IsValid() && S->BoardAsset.IsValid() && BP && BP->GeneratedClass))
    { return false; }
    S->Blueprint.Reset(BP);
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,S](UWorld* World)
        {
            // Starting PIE may recompile the fixture and replace its generated class.
            S->Instance.Reset(NewObject<UObject>(GetTransientPackage(), S->Blueprint->GeneratedClass));
            auto Owner = FLife::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
            auto Spec = S->ObjectAsset->Get_ObjectSpec();
            FCk_Handle_HexGridObject Object;
            ECk_HexGridObject_Failure Reason{};
            bool Added = false;
            auto Compose = [&]()
            {
                return Call(*this, *S->Instance, FObject::StaticClass(), TEXT("TryAdd"),
                    {{TEXT("InHandle"),&Owner},{TEXT("InSpec"),&Spec}},
                    {{TEXT("OutObject"),&Object},{TEXT("OutFailure"),&Reason},{TEXT("ReturnValue"),&Added}});
            };
            if (!Compose() || !TestTrue(TEXT("Blueprint composes authored footprint"), Added && ck::IsValid(Object)))
            { return; }
            const FCk_HexCoord Expected[] = {{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}};
            FCk_HexCoord Anchor;
            auto Connectivity = ECk_HexGridConnectivity::RequireConnected;
            for (int32 I = 0; I < 6; ++I)
            {
                auto Turn = static_cast<ECk_HexRotation>(I);
                FCk_HexGridObject_ResolveResult Result;
                if (!Call(*this,*S->Instance,FObject::StaticClass(),TEXT("TryResolve_Cells"),
                    {{TEXT("InObject"),&Object},{TEXT("InAnchor"),&Anchor},{TEXT("InRotation"),&Turn},
                        {TEXT("InConnectivity"),&Connectivity}}, {{TEXT("ReturnValue"),&Result}})) { return; }
                TestTrue(FString::Printf(TEXT("Blueprint turn %d"),I), Result.Get_Succeeded()
                    && Result.Get_Cells().Num() == 1 && Result.Get_Cells()[0] == Expected[I]);
            }
            if (!Compose() || !TestTrue(TEXT("Rejection exposes typed failure and invalid object"), !Added
                && Reason == ECk_HexGridObject_Failure::AlreadyComposed && ck::Is_NOT_Valid(Object))) { return; }
            FCk_Handle_HexGrid Board;
            ECk_HexBoardFailure BoardFailure{};
            if (!TestTrue(TEXT("Authored board composes"), UCk_Utils_HexGrid_UE::TryCreate(
                Owner, FTransform::Identity, S->BoardAsset->Get_Spec(), Board, BoardFailure))) { return; }
            TArray<FCk_Handle_HexGridBlocker> Blockers;
            if (!Call(*this,*S->Instance,FBlocker::StaticClass(),TEXT("Get_Blockers"),{{TEXT("InGrid"),&Board}},
                {{TEXT("ReturnValue"),&Blockers}}) || !TestEqual(TEXT("one authored blocker"), Blockers.Num(), 1)) { return; }
            S->Blocker = Blockers[0];
            bool Active = true;
            auto Input = FCk_Handle_HexGridBlocker{S->Blocker};
            FCk_Handle_HexGridBlocker Submitted;
            if (Call(*this,*S->Instance,FBlocker::StaticClass(),TEXT("Request_SetActive"),
                {{TEXT("InBlocker"),&Input},{TEXT("InActive"),&Active}}, {{TEXT("ReturnValue"),&Submitted}}))
            { TestTrue(TEXT("Blueprint toggle returns the submitted blocker"), Submitted == S->Blocker); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [this,S](){ return HasAnyErrors() || (ck::IsValid(S->Blocker) && FBlocker::Get_IsActive(S->Blocker)); }), 5.f, TEXT("BP toggle commits")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this,S](UWorld*)
        {
            if (HasAnyErrors()) { return; }
            bool Active = false;
            TestTrue(TEXT("Blueprint sees committed vote"), Call(*this,*S->Instance,FBlocker::StaticClass(),TEXT("Get_IsActive"),
                {{TEXT("InBlocker"),&S->Blocker}}, {{TEXT("ReturnValue"),&Active}}) && Active);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

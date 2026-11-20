#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTest_HexBlueprintGraph.h"
#include "CkTest_HexPersistenceAssets.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGridOccupancy_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include <Engine/Blueprint.h>
#include <Engine/BlueprintGeneratedClass.h>
#include <Engine/World.h>
#include <HAL/FileManager.h>
#include <Kismet2/BlueprintEditorUtils.h>
#include <Kismet2/KismetEditorUtilities.h>
#include <Misc/PackageName.h>
#include <Misc/Paths.h>
#include <UObject/SavePackage.h>
#include <UObject/StrongObjectPtr.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>
#include <type_traits>

namespace ck_test_hex_persistence_blueprint
{
    using FGrid = UCk_Utils_HexGrid_UE;

    auto Author(FAutomationTestBase& InTest, FName AssetName, const TArray<UFunction*>& Functions) -> UBlueprint*
    {
        const FString PackageName = TEXT("/CkTests/CkGrid/HexGrid/") + AssetName.ToString();
        const auto Exists = FPackageName::DoesPackageExist(PackageName);
        auto* Blueprint = Exists ? LoadObject<UBlueprint>(nullptr, *(PackageName + TEXT(".") + AssetName.ToString()))
            : FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), CreatePackage(*PackageName), AssetName,
                BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        if (!Blueprint) { return nullptr; }
        if (!Exists)
        {
            for (auto* Function : Functions)
            {
                if (!Function || !ck_test_hex_geometry_blueprint::AddGraph(InTest, *Blueprint, *Function))
                { return nullptr; }
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (Blueprint->Status != BS_UpToDate) { return nullptr; }
        if (!Exists)
        {
            const auto File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            Blueprint->SetFlags(RF_Public | RF_Standalone);
            FSavePackageArgs Args;
            Args.TopLevelFlags = RF_Public | RF_Standalone;
            if (!UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *File, Args)) { return nullptr; }
        }
        return Blueprint;
    }

    template<typename T>
    auto Call(FAutomationTestBase& InTest, UObject& InInstance, FName InName,
        const FCk_Handle_HexGrid& InBoard, T& OutValue) -> bool
    {
        auto* Function = InInstance.FindFunction(ck_test_hex_geometry_blueprint::GraphName(InName));
        auto* Native = FGrid::StaticClass()->FindFunctionByName(InName);
        const auto* Input = Function ? FindFProperty<FStructProperty>(Function, TEXT("InGrid")) : nullptr;
        const auto* Output = Function ? Function->GetReturnProperty() : nullptr;
        if (!InTest.TestTrue(InName.ToString() + TEXT(" executes compiled readiness graph"),
            Function && !Function->Script.IsEmpty() && Native && Input && Output &&
            Input->Struct == FCk_Handle_HexGrid::StaticStruct() && Output->SameType(Native->GetReturnProperty())))
        { return false; }
        FStructOnScope Frame(Function);
        Input->CopyCompleteValue(Input->ContainerPtrToValuePtr<void>(Frame.GetStructMemory()), &InBoard);
        InInstance.ProcessEvent(Function, Frame.GetStructMemory());
        if constexpr (std::is_same_v<T, bool>)
        {
            const auto* Boolean = CastField<FBoolProperty>(Output);
            if (!Boolean) { return false; }
            OutValue = Boolean->GetPropertyValue_InContainer(Frame.GetStructMemory());
        }
        else
        {
            Output->CopyCompleteValue(&OutValue, Output->ContainerPtrToValuePtr<void>(Frame.GetStructMemory()));
        }
        return true;
    }

    struct FScenario
    {
        TStrongObjectPtr<UBlueprint> Blueprint;
        TStrongObjectPtr<UObject> Instance;
    };
}

auto ck::tests::hex_persistence::EnsureNetConsumer(FAutomationTestBase& InTest) -> bool
{
    return ck_test_hex_persistence_blueprint::Author(InTest, TEXT("BP_HexGrid_NetProjectionFixture"), {
        UCk_Utils_HexGrid_UE::StaticClass()->FindFunctionByName(TEXT("Get_Readiness")),
        UCk_Utils_HexGridOccupancy_UE::StaticClass()->FindFunctionByName(TEXT("TryGet_Placement"))}) != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexPersistenceBlueprint, "Ck.HexGrid.Persistence.Blueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkHexPersistenceBlueprint::RunTest(const FString&)
{
    using namespace ck_test_hex_persistence_blueprint;
    auto Scenario = MakeShared<FScenario>();
    Scenario->Blueprint.Reset(Author(*this, TEXT("BP_HexGrid_PersistenceFixture"), {
        FGrid::StaticClass()->FindFunctionByName(TEXT("Get_Readiness")),
        FGrid::StaticClass()->FindFunctionByName(TEXT("Get_IsReady")),
        FGrid::StaticClass()->FindFunctionByName(TEXT("Get_IsPending"))}));
    if (!TestTrue(TEXT("Saved compiled persistence readiness Blueprint available"),
        Scenario->Blueprint.IsValid() && Scenario->Blueprint->GeneratedClass))
    { return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda([this, Scenario](UWorld* World)
    {
        Scenario->Instance.Reset(NewObject<UObject>(GetTransientPackage(), Scenario->Blueprint->GeneratedClass));
        auto Verify = [&](const FCk_Handle_HexGrid& Board, ECk_HexGridReadiness State, ECk_HexGridFailure Failure)
        {
            FCk_HexGridReadiness_Result Result;
            bool Ready = false;
            bool Pending = true;
            if (!Call(*this, *Scenario->Instance, TEXT("Get_Readiness"), Board, Result)
                || !Call(*this, *Scenario->Instance, TEXT("Get_IsReady"), Board, Ready)
                || !Call(*this, *Scenario->Instance, TEXT("Get_IsPending"), Board, Pending))
            { return; }
            TestTrue(TEXT("Blueprint readiness result matches public native result"), Result.Get_State() == State
                && Result.Get_Failure() == Failure && Ready == (State == ECk_HexGridReadiness::Ready) && !Pending);
        };
        Verify({}, ECk_HexGridReadiness::Unavailable, ECk_HexGridFailure::InvalidGrid);
        auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
        UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
            ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
        FCk_Handle_HexGrid Board;
        ECk_HexBoardFailure Failure{};
        if (!TestTrue(TEXT("Fresh board composes for Blueprint readiness query"),
            FGrid::TryCreate(Owner, FTransform::Identity, FCk_HexGrid_Spec{}, Board, Failure)))
        { return; }
        Verify(Board, ECk_HexGridReadiness::Ready, ECk_HexGridFailure::None);
    })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

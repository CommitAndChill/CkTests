#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkRuntimeMesh/CkRuntimeMesh_Utils.h"
#include "CkRuntimeMesh/CkRuntimeMesh_Processor.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkCore/Time/CkTime.h"
#include "CkCore/Validation/CkIsValid.h"
#include "Engine/World.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/ScopeExit.h"
#include "../CkUnitTest_Common.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_RuntimeMesh_PendingImportGC,
    "RuntimeMeshCooked.PendingImportGC",
    ck::tests::kCkUnitTestFlags)

bool FCkTest_RuntimeMesh_PendingImportGC::RunTest(const FString& Parameters)
{
    constexpr TCHAR SourcePath[] =
        TEXT("/CkTests/CkRuntimeMesh/Cooked/SM_Import_CPU.SM_Import_CPU");
    if (NOT TestNull(TEXT("fresh-process source must not be resident"),
        FindObject<UStaticMesh>(nullptr, SourcePath)))
    { return false; }

    constexpr auto InformEngineOfWorld = false;
    auto* World = UWorld::CreateWorld(EWorldType::Game, InformEngineOfWorld,
        TEXT("RuntimeMeshPendingImportGC"));
    if (NOT TestNotNull(TEXT("world created"), World))
    { return false; }
    World->AddToRoot();
    ON_SCOPE_EXIT
    {
        ck::runtimemesh::CancelWorld(World);
        World->RemoveFromRoot();
        World->DestroyWorld(InformEngineOfWorld);
    };

    const auto WorldEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(World);
    if (NOT TestTrue(TEXT("world entity exists"), ck::IsValid(WorldEntity)))
    { return false; }
    auto Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(WorldEntity);
    auto Spec = FCk_RuntimeMesh_Spec{};
    Spec.Set_SourceMesh(TSoftObjectPtr<UStaticMesh>{FSoftObjectPath{SourcePath}});
    auto Source = UCk_Utils_RuntimeMesh_UE::Add(Owner, Spec);
    if (NOT TestTrue(TEXT("source composed"), ck::IsValid(Source)))
    { return false; }
    auto& Pending = Source.Get<ck::FFragment_RuntimeMesh_PendingImport>();
    ck::FProcessor_RuntimeMesh_Setup{WorldEntity.Get_RegistryView()}.ForEachEntity(
        FCk_Time{}, Source, Source.Get<ck::FFragment_RuntimeMesh>(), Pending);
    TestEqual(TEXT("first setup remains pending"),
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source),
        ECk_RuntimeMesh_SetupState::Pending);
    TestTrue(TEXT("rooted batch was requested"), Pending.Get_Batch().Get_IsRequested());
    CollectGarbage(RF_NoFlags);
    FlushAsyncLoading();
    for (auto Attempt = 0; Attempt < 4
        && UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source)
            == ECk_RuntimeMesh_SetupState::Pending; ++Attempt)
    {
        ck::FProcessor_RuntimeMesh_Setup{WorldEntity.Get_RegistryView()}.ForEachEntity(
            FCk_Time{}, Source, Source.Get<ck::FFragment_RuntimeMesh>(),
            Source.Get<ck::FFragment_RuntimeMesh_PendingImport>());
    }
    TestEqual(TEXT("GC-safe import reaches Ready"),
        UCk_Utils_RuntimeMesh_UE::Get_SetupState(Source),
        ECk_RuntimeMesh_SetupState::Ready);
    TestFalse(TEXT("terminal import releases rooted batch fragment"),
        Source.Has<ck::FFragment_RuntimeMesh_PendingImport>());
    return true;
}
#endif

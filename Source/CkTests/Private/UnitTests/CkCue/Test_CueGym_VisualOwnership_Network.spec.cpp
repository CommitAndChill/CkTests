#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkCue/CkCueSubsystem_Base.h"
#include "CkCue/CkCue_EntityScript.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Registry/CkRegistry_SlotTable.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "CkPmg/CkPmg_Fragment_BasicShapes.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"
#include "UObject/StrongObjectPtr.h"

#include <utility>
#include <limits>

namespace ck_cue_visual_ownership_test
{
    constexpr const TCHAR* PersistentTag = TEXT("CueGym.Lifetime.Persistent");
    constexpr const TCHAR* PersistentClassPath = TEXT("/Script/Angelscript.Ck_CueGym_Cue_Persistent");
    constexpr const TCHAR* TempOwnerClassPath = TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_TempOwner");
    constexpr const TCHAR* CueParamsPath = TEXT("/Script/Angelscript.CkCueGym_SpawnParams");
    constexpr const TCHAR* TempParamsPath = TEXT("/Script/Angelscript.Ck_Gym_TransformSpawnParams");
    constexpr const TCHAR* ProbeClassPath = TEXT("/Script/Angelscript.CkCueGym_VisualOwnershipProbe");

    struct FRun
    {
        UClass* PersistentClass = nullptr;
        UClass* TempOwnerClass = nullptr;
        UClass* ProbeClass = nullptr;
        UScriptStruct* CueParams = nullptr;
        UScriptStruct* TempParams = nullptr;
        FInstancedStruct CueSpawnParams;
        FInstancedStruct TempSpawnParams;
        UFunction* PersistentVisual = nullptr;
        UFunction* TempVisual = nullptr;
        UFunction* InvalidProbe = nullptr;
        TStrongObjectPtr<UObject> MaterialProbe;
        FCk_Handle Root;
        FCk_Handle Owner;
        FCk_Handle Cue;
        FCk_Handle TempOwner;
        FCk_Handle ForeignOwner;
        FCk_Handle Sphere;
        FCk_Handle Box;
        FCk_Handle MaterialSphere;
        FCk_Handle MaterialBox;
        TWeakObjectPtr<UProceduralMeshComponent> MaterialSphereMesh;
        TWeakObjectPtr<UProceduralMeshComponent> MaterialBoxMesh;
        bool MaterialMeshesCaptured = false;
        bool Spawned = false;
        bool Drew = false;
        bool Captured = false;
        bool SphereDirect = false;
        bool BoxDirect = false;
    };

    auto Allocated(const FCk_Handle& Handle) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    auto SameRegistry(const FCk_Handle& Left, const FCk_Handle& Right) -> bool
    {
        return Allocated(Left) && Allocated(Right) &&
            Left.Get_RegistryView().Get_RegistryHandle() ==
                Right.Get_RegistryView().Get_RegistryHandle();
    }

    auto DirectParent(const FCk_Handle& Child, const FCk_Handle& Parent) -> bool
    {
        return SameRegistry(Child, Parent) &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == Parent;
    }

    // The authored getter returns a raw receipt, including a shape pending destruction.
    // Validate the function's exact return type before ProcessEvent.
    auto TryVisualGetter(UClass* Class, UFunction*& Out) -> bool
    {
        Out = IsValid(Class) ? Class->FindFunctionByName(TEXT("Get_VisualShape")) : nullptr;
        const auto* Return = IsValid(Out) ? FindFProperty<FStructProperty>(Out, TEXT("ReturnValue")) : nullptr;
        return Return && Return->Struct == FCk_Handle::StaticStruct() &&
            Out->NumParms == 1;
    }

    auto ReadVisual(const FCk_Handle& Entity, UClass* ExpectedClass,
        UFunction* Getter) -> FCk_Handle
    {
        if (!Allocated(Entity) || !IsValid(ExpectedClass) || !IsValid(Getter)) { return {}; }
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity);
        if (!IsValid(Script) || Script->GetClass() != ExpectedClass) { return {}; }
        FStructOnScope Args(Getter);
        auto* Return = FindFProperty<FStructProperty>(Getter, TEXT("ReturnValue"));
        if (!Return || Return->Struct != FCk_Handle::StaticStruct() ||
            !Args.GetStructMemory()) { return {}; }
        Script->ProcessEvent(Getter, Args.GetStructMemory());
        return *Return->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory());
    }

    auto ParamsWithDefaultTransform(UScriptStruct* Type) -> FInstancedStruct
    {
        FInstancedStruct Result;
        if (!IsValid(Type)) { return Result; }
        Result.InitializeAs(Type);
        auto* Transform = FindFProperty<FStructProperty>(Type, TEXT("InitialTransform"));
        if (!Transform || Transform->Struct != TBaseStructure<FTransform>::Get() ||
            !Result.GetMutableMemory()) { Result.Reset(); return Result; }
        *Transform->ContainerPtrToValuePtr<FTransform>(Result.GetMutableMemory()) =
            FTransform::Identity;
        return Result;
    }

    auto InvalidProbeSignature(UFunction* Function) -> bool
    {
        if (!IsValid(Function)) { return false; }
        auto* SourceArg = FindFProperty<FStructProperty>(Function, TEXT("InSource"));
        auto* ForeignArg = FindFProperty<FStructProperty>(Function, TEXT("InForeignSource"));
        auto* ColorArg = FindFProperty<FStructProperty>(Function, TEXT("InBadColor"));
        auto* RadiusArg = FindFProperty<FFloatProperty>(Function, TEXT("InBadRadius"));
        auto* ExtentArg = FindFProperty<FStructProperty>(Function, TEXT("InBadExtent"));
        auto* Return = FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue"));
        return Function->NumParms == 6 && SourceArg && ForeignArg && ColorArg &&
            RadiusArg && ExtentArg && Return &&
            SourceArg->Struct == FCk_Handle::StaticStruct() &&
            ForeignArg->Struct == FCk_Handle::StaticStruct() &&
            ColorArg->Struct == TBaseStructure<FLinearColor>::Get() &&
            ExtentArg->Struct == TBaseStructure<FVector>::Get() &&
            Return->HasAnyPropertyFlags(CPF_ReturnParm);
    }

    auto TryInvalidProbe(UObject* Probe, UFunction* Function,
        const FCk_Handle& Source, const FCk_Handle& ForeignSource) -> bool
    {
        if (!IsValid(Probe) || !InvalidProbeSignature(Function) ||
            Probe->GetClass() != Function->GetOuterUClass()) { return false; }
        auto* SourceArg = FindFProperty<FStructProperty>(Function, TEXT("InSource"));
        auto* ForeignArg = FindFProperty<FStructProperty>(Function, TEXT("InForeignSource"));
        auto* ColorArg = FindFProperty<FStructProperty>(Function, TEXT("InBadColor"));
        auto* RadiusArg = FindFProperty<FFloatProperty>(Function, TEXT("InBadRadius"));
        auto* ExtentArg = FindFProperty<FStructProperty>(Function, TEXT("InBadExtent"));
        auto* Return = FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue"));
        if (!SourceArg || !ForeignArg || !ColorArg ||
            !RadiusArg || !ExtentArg || !Return ||
            !Allocated(Source) || !Allocated(ForeignSource)) { return false; }
        FStructOnScope Args(Function);
        if (!Args.GetStructMemory()) { return false; }
        SourceArg->Struct->CopyScriptStruct(
            SourceArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &Source);
        ForeignArg->Struct->CopyScriptStruct(
            ForeignArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &ForeignSource);
        const float NaN = std::numeric_limits<float>::quiet_NaN();
        const FLinearColor BadColor(NaN, 1.0f, 1.0f, 0.3f);
        const FVector BadExtent(20.0f, NaN, 20.0f);
        ColorArg->Struct->CopyScriptStruct(
            ColorArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &BadColor);
        RadiusArg->SetPropertyValue_InContainer(Args.GetStructMemory(), NaN);
        ExtentArg->Struct->CopyScriptStruct(
            ExtentArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &BadExtent);
        Probe->ProcessEvent(Function, Args.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    auto TryReadAllocatedEntities(UWorld* World, TArray<uint32>& Out) -> bool
    {
        if (!IsValid(World)) { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!Allocated(Root)) { return false; }
        const auto Registry = Root.Get_RegistryView();
        const auto* Native = ck::registry_table::TryResolve(Registry.Get_RegistryHandle());
        const auto* Entities = Native ? std::as_const(*Native).storage<FCk_Entity::IdType>() : nullptr;
        if (!Entities) { return false; }
        TArray<uint32> IDs;
        for (const auto& [Entity] : Entities->each()) { IDs.Add(static_cast<uint32>(Entity)); }
        IDs.Sort();
        Out = MoveTemp(IDs);
        return true;
    }

    // This fixture preflight is side-effect free. Rejected input never reaches
    // either production request; this does not test production rejection callbacks.
    auto CanIssue(UWorld* World, const FCk_Handle& Owner, const FRun& Run) -> bool
    {
        if (!IsValid(World) || World->GetNetMode() == NM_Client || !IsValid(GEngine) ||
            !Allocated(Owner) || !UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Owner) ||
            !IsValid(Run.PersistentClass) || !IsValid(Run.TempOwnerClass) ||
            !IsValid(Run.CueParams) || !IsValid(Run.TempParams) ||
            !Run.CueSpawnParams.IsValid() || !Run.TempSpawnParams.IsValid() ||
            Run.CueSpawnParams.GetScriptStruct() != Run.CueParams ||
            Run.TempSpawnParams.GetScriptStruct() != Run.TempParams ||
            !IsValid(Run.PersistentVisual) || !IsValid(Run.TempVisual) ||
            !IsValid(Run.ProbeClass) || !IsValid(Run.InvalidProbe)) { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        if (!SameRegistry(Owner, Root)) { return false; }
        const auto* Discovery = GEngine->GetEngineSubsystem<UCk_GenericCueSubsystem_UE>();
        const auto* Executor = World->GetSubsystem<UCk_GenericCueExecutor_Subsystem_UE>();
        if (!IsValid(Discovery) || !IsValid(Executor) || Executor->GetWorld() != World)
        { return false; }
        const auto Tag = FGameplayTag::RequestGameplayTag(FName(PersistentTag), false);
        const auto* CueClass = Tag.IsValid() ? Discovery->Get_DiscoveredCues().Find(Tag) : nullptr;
        return CueClass && *CueClass == Run.PersistentClass;
    }

    auto Bind(FRun& Run) -> bool
    {
        Run.PersistentClass = FSoftClassPath(PersistentClassPath).TryLoadClass<UCk_EntityScript_UE>();
        Run.TempOwnerClass = FSoftClassPath(TempOwnerClassPath).TryLoadClass<UCk_EntityScript_UE>();
        Run.ProbeClass = FSoftClassPath(ProbeClassPath).TryLoadClass<UObject>();
        Run.InvalidProbe = IsValid(Run.ProbeClass) ?
            Run.ProbeClass->FindFunctionByName(TEXT("InvokeInvalidHelpers")) : nullptr;
        Run.CueParams = FindObject<UScriptStruct>(nullptr, CueParamsPath);
        Run.TempParams = FindObject<UScriptStruct>(nullptr, TempParamsPath);
        Run.CueSpawnParams = ParamsWithDefaultTransform(Run.CueParams);
        Run.TempSpawnParams = ParamsWithDefaultTransform(Run.TempParams);
        return TryVisualGetter(Run.PersistentClass, Run.PersistentVisual) &&
            TryVisualGetter(Run.TempOwnerClass, Run.TempVisual) &&
            InvalidProbeSignature(Run.InvalidProbe) &&
            Run.CueSpawnParams.IsValid() && Run.TempSpawnParams.IsValid() &&
            Run.CueSpawnParams.GetScriptStruct() == Run.CueParams &&
            Run.TempSpawnParams.GetScriptStruct() == Run.TempParams;
    }

    auto IsSphere(const FCk_Handle& Shape, const FCk_Handle& WorldRoot) -> bool
    {
        if (!SameRegistry(Shape, WorldRoot) ||
            !Shape.Has_All<ck::FFragment_Pmg_Sphere_Params,
                ck::FFragment_Pmg_DebugShape_Common,
                ck::FFragment_Pmg_DebugShape,
                ck::FFragment_LifetimeOwner>()) { return false; }
        const auto& Params = Shape.Get<ck::FFragment_Pmg_Sphere_Params,
            ck::IsValid_Policy_IncludePendingKill>();
        const auto& Common = Shape.Get<ck::FFragment_Pmg_DebugShape_Common,
            ck::IsValid_Policy_IncludePendingKill>();
        return FMath::IsNearlyEqual(Params.Get_Radius(), 20.0f) &&
            Params.Get_Segments() == 12 && Params.Get_Rings() == 12 &&
            Common.Get_Color().Equals(FLinearColor(0.0f, 1.0f, 0.0f, 0.3f));
    }

    auto IsBox(const FCk_Handle& Shape, const FCk_Handle& WorldRoot) -> bool
    {
        if (!SameRegistry(Shape, WorldRoot) ||
            !Shape.Has_All<ck::FFragment_Pmg_Box_Params,
                ck::FFragment_Pmg_DebugShape_Common,
                ck::FFragment_Pmg_DebugShape,
                ck::FFragment_LifetimeOwner>()) { return false; }
        const auto& Params = Shape.Get<ck::FFragment_Pmg_Box_Params,
            ck::IsValid_Policy_IncludePendingKill>();
        const auto& Common = Shape.Get<ck::FFragment_Pmg_DebugShape_Common,
            ck::IsValid_Policy_IncludePendingKill>();
        const FVector Extent = Params.Get_Extent();
        const FLinearColor Color = Common.Get_Color();
        return Extent.X >= 5.0f && Extent.X <= 30.0f &&
            FMath::IsNearlyEqual(Extent.X, Extent.Y) &&
            FMath::IsNearlyEqual(Extent.X, Extent.Z) &&
            FMath::IsNearlyEqual(Color.R, Color.G) &&
            FMath::IsNearlyEqual(Color.R, Color.B) &&
            Color.R >= 0.3f && Color.R <= 1.0f &&
            FMath::IsNearlyEqual(Color.A, 0.3f);
    }

    auto Drawn(UWorld* World, const FRun& Run) -> bool
    {
        if (!Run.Spawned || !IsValid(World) || !Allocated(Run.Cue) ||
            !Allocated(Run.TempOwner)) { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        const auto Sphere = ReadVisual(Run.Cue, Run.PersistentClass, Run.PersistentVisual);
        const auto Box = ReadVisual(Run.TempOwner, Run.TempOwnerClass, Run.TempVisual);
        return Sphere != Box && IsSphere(Sphere, Root) && IsBox(Box, Root);
    }

    auto InvokeMaterialization(UObject* Probe, const FCk_Handle& SphereSource,
        const FCk_Handle& BoxSource, FCk_Handle& OutSphere, FCk_Handle& OutBox) -> bool
    {
        if (!IsValid(Probe) || !Allocated(SphereSource) || !Allocated(BoxSource))
        { return false; }
        auto* Draw = Probe->FindFunction(FName(TEXT("DrawForMaterialization")));
        auto* SphereGetter = Probe->FindFunction(FName(TEXT("Get_MaterializedSphere")));
        auto* BoxGetter = Probe->FindFunction(FName(TEXT("Get_MaterializedBox")));
        auto* SphereArg = IsValid(Draw) ? FindFProperty<FStructProperty>(Draw, TEXT("InSphereSource")) : nullptr;
        auto* BoxArg = IsValid(Draw) ? FindFProperty<FStructProperty>(Draw, TEXT("InBoxSource")) : nullptr;
        auto* DrawReturn = IsValid(Draw) ? FindFProperty<FBoolProperty>(Draw, TEXT("ReturnValue")) : nullptr;
        auto ReadGetter = [Probe](UFunction* Getter) -> FCk_Handle
        {
            const auto* Return = IsValid(Getter) ?
                FindFProperty<FStructProperty>(Getter, TEXT("ReturnValue")) : nullptr;
            if (!Return || Return->Struct != FCk_Handle::StaticStruct() ||
                Getter->NumParms != 1) { return {}; }
            FStructOnScope Args(Getter);
            if (!Args.GetStructMemory()) { return {}; }
            Probe->ProcessEvent(Getter, Args.GetStructMemory());
            return *Return->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory());
        };
        if (!SphereArg || !BoxArg || !DrawReturn || Draw->NumParms != 3 ||
            SphereArg->Struct != FCk_Handle::StaticStruct() ||
            BoxArg->Struct != FCk_Handle::StaticStruct() ||
            !IsValid(SphereGetter) || !IsValid(BoxGetter)) { return false; }
        FStructOnScope Args(Draw);
        if (!Args.GetStructMemory()) { return false; }
        SphereArg->Struct->CopyScriptStruct(
            SphereArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &SphereSource);
        BoxArg->Struct->CopyScriptStruct(
            BoxArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &BoxSource);
        Probe->ProcessEvent(Draw, Args.GetStructMemory());
        if (!DrawReturn->GetPropertyValue_InContainer(Args.GetStructMemory())) { return false; }
        OutSphere = ReadGetter(SphereGetter);
        OutBox = ReadGetter(BoxGetter);
        return Allocated(OutSphere) && Allocated(OutBox) && OutSphere != OutBox;
    }

    auto ReadMaterializedReceipt(UObject* Probe, const TCHAR* Name) -> FCk_Handle
    {
        if (!IsValid(Probe)) { return {}; }
        auto* Getter = Probe->FindFunction(FName(Name));
        auto* Return = IsValid(Getter) ?
            FindFProperty<FStructProperty>(Getter, TEXT("ReturnValue")) : nullptr;
        if (!Return || Return->Struct != FCk_Handle::StaticStruct() ||
            Getter->NumParms != 1) { return {}; }
        FStructOnScope Args(Getter);
        if (!Args.GetStructMemory()) { return {}; }
        Probe->ProcessEvent(Getter, Args.GetStructMemory());
        return *Return->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory());
    }

    auto InvokeInitiatedAncestorProbe(UObject* Probe,
        const FCk_Handle& SphereSource, const FCk_Handle& BoxSource) -> bool
    {
        if (!IsValid(Probe) || !Allocated(SphereSource) || !Allocated(BoxSource))
        { return false; }
        auto* Function = Probe->FindFunction(FName(TEXT("RejectAfterAncestorInitiated")));
        auto* SphereArg = IsValid(Function) ?
            FindFProperty<FStructProperty>(Function, TEXT("InSphereSource")) : nullptr;
        auto* BoxArg = IsValid(Function) ?
            FindFProperty<FStructProperty>(Function, TEXT("InBoxSource")) : nullptr;
        auto* Return = IsValid(Function) ?
            FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!SphereArg || !BoxArg || !Return || Function->NumParms != 3 ||
            SphereArg->Struct != FCk_Handle::StaticStruct() ||
            BoxArg->Struct != FCk_Handle::StaticStruct() ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm)) { return false; }
        FStructOnScope Args(Function);
        if (!Args.GetStructMemory()) { return false; }
        SphereArg->Struct->CopyScriptStruct(
            SphereArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &SphereSource);
        BoxArg->Struct->CopyScriptStruct(
            BoxArg->ContainerPtrToValuePtr<void>(Args.GetStructMemory()), &BoxSource);
        Probe->ProcessEvent(Function, Args.GetStructMemory());
        return Return->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    auto IsMaterialized(UWorld* World, const FCk_Handle& Shape,
        const FCk_Handle& Source, const bool Sphere) -> bool
    {
        if (!IsValid(World) || !Allocated(Source) || !Allocated(Shape) || !DirectParent(Shape, Source) ||
            !Source.Has<ck::FFragment_Transform>() ||
            UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Shape, ECk_EntityLifetime_DestructionPhase::BeginDestroy) ||
            Shape.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>() ||
            !Shape.Has_All<ck::FFragment_Pmg_DebugShape_Common,
                ck::FFragment_Pmg_DebugShape, ck::FFragment_Transform>())
        { return false; }
        const auto& Common = Shape.Get<ck::FFragment_Pmg_DebugShape_Common>();
        const auto& Physical = Shape.Get<ck::FFragment_Pmg_DebugShape>();
        auto* Mesh = Physical.Get_MeshComponent().Get();
        const auto SourceTransform = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(Source));
        const FVector ExpectedOffset = Sphere ?
            FVector(17.0f, 23.0f, 31.0f) : FVector(-11.0f, 5.0f, 19.0f);
        if (!IsValid(Mesh) || !Mesh->IsRegistered() || Mesh->GetWorld() != World ||
            Mesh->GetProcMeshSection(0) == nullptr || Mesh->GetMaterial(0) == nullptr ||
            !Mesh->GetComponentTransform().Equals(
                Shape.Get<ck::FFragment_Transform>().Get_Transform(), 0.01f) ||
            !Mesh->GetComponentLocation().Equals(
                SourceTransform.GetLocation() + ExpectedOffset, 0.01f) ||
            !Mesh->IsVisible() || Mesh->bHiddenInGame)
        { return false; }
        if (Sphere)
        {
            return Shape.Has<ck::FFragment_Pmg_Sphere_Params>() &&
                !Shape.Has<ck::FFragment_Pmg_Box_Params>() &&
                FMath::IsNearlyEqual(Shape.Get<ck::FFragment_Pmg_Sphere_Params>().Get_Radius(), 31.0f) &&
                Common.Get_Color().Equals(FLinearColor(0.2f, 0.7f, 0.4f, 0.6f));
        }
        return Shape.Has<ck::FFragment_Pmg_Box_Params>() &&
            !Shape.Has<ck::FFragment_Pmg_Sphere_Params>() &&
            Shape.Get<ck::FFragment_Pmg_Box_Params>().Get_Extent().Equals(FVector(11.0f, 13.0f, 17.0f)) &&
            Common.Get_Color().Equals(FLinearColor(0.8f, 0.3f, 0.6f, 0.5f));
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCueGym_VisualOwnership,
    "Ck.Cue.Gym.VisualOwnership.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCueGym_VisualOwnership::RunTest(const FString& Parameters)
{
    using namespace ck_cue_visual_ownership_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Cue visual ownership network topology")); return false; }
    const auto Run = MakeShared<FRun>();
    // The harness count is total PIE worlds: server plus two clients.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World) || !Bind(*Run)) { return false; }
            const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            if (!Allocated(Root)) { return false; }
            return CanIssue(World, Root, *Run);
        }), 60.0, TEXT("authored Persistent cue, TempOwner, getter, and spawn params ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            TArray<uint32> Before;
            TArray<uint32> After;
            const bool ReadBefore = TryReadAllocatedEntities(World, Before);
            const bool Rejected = !CanIssue(World, FCk_Handle{}, *Run);
            const bool ReadAfter = TryReadAllocatedEntities(World, After);
            if (!TestTrue(TEXT("default fixture owner rejected before request dispatch with unchanged entity IDs"),
                ReadBefore && Rejected && ReadAfter && Before == After)) { return; }
            Run->Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            if (!TestTrue(TEXT("real server root passes pure request preflight"),
                CanIssue(World, Run->Root, *Run))) { return; }
            Run->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->Root);
            if (!TestTrue(TEXT("fixture created a known live owner"),
                CanIssue(World, Run->Owner, *Run) && DirectParent(Run->Owner, Run->Root)))
            { return; }
            const auto Tag = FGameplayTag::RequestGameplayTag(FName(PersistentTag), false);
            const auto CuePending = World->GetSubsystem<UCk_GenericCueExecutor_Subsystem_UE>()
                ->Request_ExecuteCue(Run->Owner, Tag, Run->CueSpawnParams,
                    ECk_Cue_ReliabilityPolicy::Unreliable,
                    ECk_Cue_MulticastPolicy::LocalOnly);
            Run->Cue = CuePending.Get_EntityUnderConstruction();
            auto TempPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Run->Owner, Run->TempOwnerClass,
                Run->TempSpawnParams, {});
            Run->TempOwner = TempPending.Get_EntityUnderConstruction();
            Run->Spawned = Allocated(Run->Cue) && Allocated(Run->TempOwner) &&
                DirectParent(Run->Cue, Run->Owner) &&
                DirectParent(Run->TempOwner, Run->Owner);
            TestTrue(TEXT("real authored LocalOnly Persistent cue and TempOwner admitted under known owner"),
                Run->Spawned);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            const auto Root = IsValid(World) ?
                UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) : FCk_Handle{};
            if (!TestTrue(TEXT("client world root available for cross-world helper probe"),
                Allocated(Root) && UCk_Utils_EntityLifetime_UE::Get_CanCreateEntity(Root)))
            { return; }
            Run->ForeignOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Root);
            if (!TestTrue(TEXT("client probe owner allocated under its world root"),
                DirectParent(Run->ForeignOwner, Root))) { return; }
            UCk_Utils_Transform_UE::Add(Run->ForeignOwner, FTransform::Identity,
                ECk_Replication::DoesNotReplicate);
            TestTrue(TEXT("cross-world probe owner has a real transform"),
                Run->ForeignOwner.Has<ck::FFragment_Transform>());
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            Run->Drew = Drawn(ck::auto_test::net::Get_ServerWorld(), *Run);
            return Run->Drew;
        }), 60.0, TEXT("authored DrawTick produced exact Persistent sphere and TempOwner box")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            TArray<uint32> ServerBefore, ServerAfter, ClientBefore, ClientAfter;
            if (!TestTrue(TEXT("invalid-helper probe has a live local source and foreign transformed source"),
                Run->Drew && Allocated(Run->Cue) && Allocated(Run->ForeignOwner) &&
                IsValid(Client) && !SameRegistry(Run->Cue, Run->ForeignOwner) &&
                TryReadAllocatedEntities(World, ServerBefore) &&
                TryReadAllocatedEntities(Client, ClientBefore))) { return; }
            auto* Probe = NewObject<UObject>(World, Run->ProbeClass);
            if (!TestTrue(TEXT("authored invalid-helper probe is world-outer"),
                IsValid(Probe) && Probe->GetOuter() == World)) { return; }
            const bool Rejected = TryInvalidProbe(Probe, Run->InvalidProbe,
                Run->Cue, Run->ForeignOwner);
            const bool Stable = TryReadAllocatedEntities(World, ServerAfter) &&
                TryReadAllocatedEntities(Client, ClientAfter) &&
                ServerBefore == ServerAfter && ClientBefore == ClientAfter;
            TestTrue(TEXT("actual invalid sphere and box calls return invalid without same-call allocations in either world"),
                Rejected && Stable);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("authored visual draw reached live capture"),
                Run->Drew && Drawn(World, *Run))) { return; }
            Run->Sphere = ReadVisual(Run->Cue, Run->PersistentClass, Run->PersistentVisual);
            Run->Box = ReadVisual(Run->TempOwner, Run->TempOwnerClass, Run->TempVisual);
            Run->Captured = IsSphere(Run->Sphere, Run->Root) &&
                IsBox(Run->Box, Run->Root) && Run->Sphere != Run->Box;
            if (!TestTrue(TEXT("captured exact allocated sphere and box receipts before owner destruction"),
                Run->Captured)) { return; }
            Run->SphereDirect = DirectParent(Run->Sphere, Run->Cue);
            Run->BoxDirect = DirectParent(Run->Box, Run->TempOwner);
            TestTrue(TEXT("Persistent DrawTick sphere has cue entity as direct lifetime parent"),
                Run->SphereDirect);
            TestTrue(TEXT("TempOwner DrawTick box has TempOwner entity as direct lifetime parent"),
                Run->BoxDirect);
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->Owner);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return Run->Captured && !Allocated(Run->Owner) && !Allocated(Run->Cue) &&
                !Allocated(Run->TempOwner) && !Allocated(Run->Sphere) &&
                !Allocated(Run->Box);
        }), 30.0, TEXT("known owner cascade physically deallocated cue, TempOwner, and captured PMG shapes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([Run](UWorld* World)
        {
            if (IsValid(World) && Allocated(Run->ForeignOwner))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->ForeignOwner); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        { return TestTrue(TEXT("Cue visual ownership PIE worlds drained"),
            ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()); }),
        TEXT("Cue visual ownership PIE cleanup"), true));
    return true;
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCueGym_VisualMaterialization,
    "Ck.Cue.Gym.VisualMaterialization.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkCueGym_VisualMaterialization::RunTest(const FString& Parameters)
{
    using namespace ck_cue_visual_ownership_test;
    bool Dedicated = false;
    if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, Dedicated))
    { AddError(TEXT("unsupported Cue visual materialization network topology")); return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            auto* World = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(World) || !Bind(*Run)) { return false; }
            const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            return Allocated(Root) && CanIssue(World, Root, *Run);
        }), 60.0, TEXT("authored cue, TempOwner, and materialization probe ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            Run->Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
            Run->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Run->Root);
            if (!TestTrue(TEXT("known source owner admitted in the authority world"),
                CanIssue(World, Run->Owner, *Run) && DirectParent(Run->Owner, Run->Root)))
            { return; }
            const auto Tag = FGameplayTag::RequestGameplayTag(FName(PersistentTag), false);
            const auto CuePending = World->GetSubsystem<UCk_GenericCueExecutor_Subsystem_UE>()
                ->Request_ExecuteCue(Run->Owner, Tag, Run->CueSpawnParams,
                    ECk_Cue_ReliabilityPolicy::Unreliable,
                    ECk_Cue_MulticastPolicy::LocalOnly);
            Run->Cue = CuePending.Get_EntityUnderConstruction();
            auto TempPending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(
                Run->Owner, Run->TempOwnerClass, Run->TempSpawnParams, {});
            Run->TempOwner = TempPending.Get_EntityUnderConstruction();
            Run->Spawned = Allocated(Run->Cue) && Allocated(Run->TempOwner) &&
                DirectParent(Run->Cue, Run->Owner) &&
                DirectParent(Run->TempOwner, Run->Owner);
            TestTrue(TEXT("actual authored cue and TempOwner spawned under owner"), Run->Spawned);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            Run->Drew = Drawn(ck::auto_test::net::Get_ServerWorld(), *Run);
            return Run->Drew;
        }), 60.0, TEXT("authored source scripts reached their real drawing ticks")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            if (!TestTrue(TEXT("materialization probe has two live transformed authored sources"),
                IsValid(World) && Run->Drew && Allocated(Run->Cue) &&
                Allocated(Run->TempOwner) &&
                Run->Cue.Has<ck::FFragment_Transform>() &&
                Run->TempOwner.Has<ck::FFragment_Transform>())) { return; }
            Run->MaterialProbe.Reset(NewObject<UObject>(World, Run->ProbeClass));
            auto* Probe = Run->MaterialProbe.Get();
            TestTrue(TEXT("one-shot authored helpers returned distinct source-owned shapes"),
                InvokeMaterialization(Probe, Run->Cue, Run->TempOwner,
                    Run->MaterialSphere, Run->MaterialBox) &&
                DirectParent(Run->MaterialSphere, Run->Cue) &&
                DirectParent(Run->MaterialBox, Run->TempOwner));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(2));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            const bool BothMaterialized =
                IsMaterialized(World, Run->MaterialSphere, Run->Cue, true) &&
                IsMaterialized(World, Run->MaterialBox, Run->TempOwner, false);
            if (TestTrue(TEXT("authored sphere and box materialize as registered meshes after PMG setup"),
                BothMaterialized))
            {
                Run->MaterialSphereMesh = Run->MaterialSphere.Get<ck::FFragment_Pmg_DebugShape>()
                    .Get_MeshComponent();
                Run->MaterialBoxMesh = Run->MaterialBox.Get<ck::FFragment_Pmg_DebugShape>()
                    .Get_MeshComponent();
                Run->MaterialMeshesCaptured = Run->MaterialSphereMesh.IsValid() &&
                    Run->MaterialBoxMesh.IsValid();
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            TestTrue(TEXT("one-shot authored sphere and box remain live and materialized across later ticks"),
                IsMaterialized(World, Run->MaterialSphere, Run->Cue, true) &&
                IsMaterialized(World, Run->MaterialBox, Run->TempOwner, false));
            if (!Allocated(Run->Owner)) { return; }
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Run->Owner);
            TArray<uint32> Before, After;
            const bool CascadeStarted =
                Allocated(Run->Owner) && Allocated(Run->Cue) &&
                Allocated(Run->TempOwner) && Allocated(Run->MaterialSphere) &&
                Allocated(Run->MaterialBox) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Run->Owner, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Run->Cue, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Run->TempOwner, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Run->MaterialSphere, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Run->MaterialBox, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                DirectParent(Run->Cue, Run->Owner) &&
                DirectParent(Run->TempOwner, Run->Owner) &&
                DirectParent(Run->MaterialSphere, Run->Cue) &&
                DirectParent(Run->MaterialBox, Run->TempOwner) &&
                TryReadAllocatedEntities(World, Before);
            auto* Probe = Run->MaterialProbe.Get();
            const bool Rejected = CascadeStarted &&
                InvokeInitiatedAncestorProbe(Probe, Run->Cue, Run->TempOwner);
            const bool Unchanged = TryReadAllocatedEntities(World, After) &&
                Before == After &&
                ReadMaterializedReceipt(Probe, TEXT("Get_MaterializedSphere")) ==
                    Run->MaterialSphere &&
                ReadMaterializedReceipt(Probe, TEXT("Get_MaterializedBox")) ==
                    Run->MaterialBox &&
                Allocated(Run->MaterialSphere) && Allocated(Run->MaterialBox) &&
                DirectParent(Run->MaterialSphere, Run->Cue) &&
                DirectParent(Run->MaterialBox, Run->TempOwner) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Run->MaterialSphere, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Run->MaterialBox, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
            TestTrue(TEXT("initiated owner cascade rejects both retire and draw calls without changing receipts or entity census"),
                CascadeStarted && Rejected && Unchanged);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([Run]() -> bool
        {
            return !Allocated(Run->Owner) && !Allocated(Run->Cue) &&
                !Allocated(Run->TempOwner) && !Allocated(Run->MaterialSphere) &&
                !Allocated(Run->MaterialBox);
        }), 30.0, TEXT("owner cascade deallocated both one-shot PMG shapes")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld*)
        {
            const auto* SphereMesh = Run->MaterialSphereMesh.Get();
            const auto* BoxMesh = Run->MaterialBoxMesh.Get();
            TestTrue(TEXT("owner cascade unregistered both source mesh components"),
                Run->MaterialMeshesCaptured &&
                (!IsValid(SphereMesh) || !SphereMesh->IsRegistered()) &&
                (!IsValid(BoxMesh) || !BoxMesh->IsRegistered()));
            Run->MaterialProbe.Reset();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this]() -> bool
        { return TestTrue(TEXT("Cue visual materialization PIE worlds drained"),
            ck::auto_test::net::Get_AllPIEWorlds().IsEmpty()); }),
        TEXT("Cue visual materialization PIE cleanup"), true));
    return true;
}

#endif

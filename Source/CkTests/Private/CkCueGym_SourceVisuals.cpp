// Read-only authored Cue visual observer.
#include "CkCueGym_SourceVisuals.h"
#include "CkCueGym_OutlineRecipe.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkCue/CkCueSubsystem_Base.h"
#include "CkCue/CkCueRelay_Actor.h"
#include "CkCore/Diagnostics/CkDiagnosticVisibility.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment_Data.h"
#include "CkEcs/EntityScript/CkEntityScript_SpawnRecipe.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "CkPmg/CkPmg_Fragment_BasicShapes.h"
#include "CkPmg/CkPmg_Utils_DebugShapes.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_cue_source_visuals
{
    constexpr int32 RoleCount = 6;
    constexpr int32 TransientRole = 4;

    // Exact authored source classes. Role 4 is checked but never traversed.
    constexpr const TCHAR* RoleClasses[RoleCount] = {
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Lifetime"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Concurrency"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_OwnerValidation"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Restart"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_Transient"),
        TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_OwnerDestruction")
    };

    enum class EScriptKind : uint8 { Unknown, NoVisual, VisualSphere, VisualBox };
    struct FClassRule { const TCHAR* Path; int32 Role; EScriptKind Kind; };
    constexpr FClassRule ChildClasses[] = {
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_AfterOneFrame"), 0, EScriptKind::NoVisual},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_Persistent"), 0, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_Timed"), 0, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_Custom"), 0, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_Multiple"), 1, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_Restart"), 1, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_OwnerSkip"), 2, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_OwnerRequire"), 2, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_TempOwner"), 2, EScriptKind::VisualBox},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_Restartable"), 3, EScriptKind::VisualSphere},
        {TEXT("/Script/Angelscript.Ck_EntityScript_CueGym_DestructionOwner"), 5, EScriptKind::VisualBox},
        {TEXT("/Script/Angelscript.Ck_CueGym_Cue_OwnerDestruction"), 5, EScriptKind::VisualSphere}
    };

    struct FVisit
    {
        FCk_Handle Handle;
        int32 Role = -1;
        EScriptKind ParentScriptKind = EScriptKind::Unknown;
        bool RetiredLineage = false;
        FCk_Handle Parent;
    };
    struct FReceipt { FCk_Handle Source; FCk_Handle Shape; int32 Role = -1; EScriptKind Kind = EScriptKind::Unknown; };

    auto Allocated(const FCk_Handle& Handle) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Handle); }

    auto Live(const FCk_Handle& Handle) -> bool
    {
        return Allocated(Handle) && ck::IsValid(Handle) &&
            !Handle.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay,
                ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await,
                ck::FTag_DestroyEntity_Finalize>();
    }

    auto SameWorld(const FCk_Handle& Handle, const FCk_Handle& WorldRoot) -> bool
    {
        return Allocated(Handle) && Allocated(WorldRoot) &&
            Handle.Get_RegistryView().Get_RegistryHandle() ==
                WorldRoot.Get_RegistryView().Get_RegistryHandle();
    }

    auto DirectParent(const FCk_Handle& Child, const FCk_Handle& Parent,
        const FCk_Handle& WorldRoot) -> bool
    {
        if (!SameWorld(Child, WorldRoot) || !SameWorld(Parent, WorldRoot) ||
            !Child.Has<ck::FFragment_LifetimeOwner>()) { return false; }
        return Child.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == Parent;
    }

    auto GetScript(const FCk_Handle& Entity) -> UCk_EntityScript_UE*
    {
        if (!Live(Entity) || !Entity.Has<ck::FFragment_EntityScript>())
        { return nullptr; }
        return UCk_Utils_EntityScript_UE::TryGet_EntityScript(Entity);
    }

    auto ClassIs(const UClass* Actual, const TCHAR* Path) -> bool
    {
        const auto* Expected = FindObject<UClass>(nullptr, Path);
        return IsValid(Actual) && IsValid(Expected) && Actual == Expected;
    }

    auto Classify(const UClass* Actual, int32 Role) -> EScriptKind
    {
        for (const auto& Rule : ChildClasses)
        { if (Rule.Role == Role && ClassIs(Actual, Rule.Path)) { return Rule.Kind; } }
        return EScriptKind::Unknown;
    }

    auto GetAuthoredClass(const FCk_Handle& Entity) -> UClass*
    {
        if (!Allocated(Entity)) { return nullptr; }
        if (Entity.Has<ck::FFragment_SpawnRecipe>())
        { return Entity.Get<ck::FFragment_SpawnRecipe,
            ck::IsValid_Policy_IncludePendingKill>().Get_ScriptClass().Get(); }
        return nullptr;
    }

    // Add() creates a request carrier *under* the still-scriptless target.
    // The carrier is evidence only when it names this exact target and owner.
    auto TryGetPendingTargetClass(const FCk_Handle& Target,
        const FCk_Handle& Owner, const FCk_Handle& WorldRoot,
        UClass*& OutClass) -> bool
    {
        OutClass = nullptr;
        if (!Allocated(Target) || !DirectParent(Target, Owner, WorldRoot))
        { return false; }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Target, WorldRoot, Children) || Children.Num() != 1)
        { return false; }
        const auto& Carrier = Children[0];
        if (!Live(Carrier) || !DirectParent(Carrier, Target, WorldRoot) ||
            !Carrier.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
        { return false; }
        TArray<FCk_Handle> CarrierChildren;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Carrier, WorldRoot, CarrierChildren) || !CarrierChildren.IsEmpty())
        { return false; }
        const auto& Request = Carrier.Get<
            ck::FFragment_EntityScript_RequestSpawnEntity>();
        auto* Archetype = Request.Get_EntityScriptClassArchetype().Get();
        if (Request.Get_NewEntity() != Target || Request.Get_Owner() != Owner ||
            Request.Get_ContextOwner() != Owner || !IsValid(Archetype) ||
            Archetype != Archetype->GetClass()->GetDefaultObject(false))
        { return false; }
        OutClass = Archetype->GetClass();
        return true;
    }

    // Contract: every visual author returns its current raw FCk_Handle; no
    // reflected shape struct, dynamic cast or stale cached handle is accepted.
    auto ReadCurrentShape(UCk_EntityScript_UE* Script, FCk_Handle& Out) -> bool
    {
        Out = {};
        if (!IsValid(Script)) { return false; }
        auto* Function = Script->GetClass()->FindFunctionByName(TEXT("Get_VisualShape"));
        const auto* Return = IsValid(Function) ?
            FindFProperty<FStructProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!Return || Function->NumParms != 1 ||
            Return->Struct != FCk_Handle::StaticStruct()) { return false; }
        FStructOnScope Args(Function);
        if (!Args.GetStructMemory()) { return false; }
        Script->ProcessEvent(Function, Args.GetStructMemory());
        Out = *Return->ContainerPtrToValuePtr<FCk_Handle>(Args.GetStructMemory());
        return true;
    }

    auto ReadDrawAttempted(UCk_EntityScript_UE* Script, bool& Out) -> bool
    {
        Out = false;
        if (!IsValid(Script)) { return false; }
        auto* Function = Script->GetClass()->FindFunctionByName(
            TEXT("Get_HasVisualDrawAttempted"));
        const auto* Return = IsValid(Function) ?
            FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue")) : nullptr;
        if (!Return || Function->NumParms != 1) { return false; }
        FStructOnScope Args(Function);
        if (!Args.GetStructMemory()) { return false; }
        Script->ProcessEvent(Function, Args.GetStructMemory());
        Out = Return->GetPropertyValue_InContainer(Args.GetStructMemory());
        return true;
    }

    auto Finite(const FLinearColor& Color) -> bool
    {
        return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) &&
            FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
    }

    auto ReadMaterialized(const FReceipt& Receipt, UWorld* World,
        const FCk_Handle& WorldRoot, FCkCueGym_SourceVisualRow& Out,
        UProceduralMeshComponent*& OutMesh) ->
        ECkCueGym_SourceVisualOutcome
    {
        OutMesh = nullptr;
        const auto Shape = Receipt.Shape;
        if (!SameWorld(Shape, WorldRoot) ||
            !DirectParent(Shape, Receipt.Source, WorldRoot))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        if (!Live(Shape)) { return ECkCueGym_SourceVisualOutcome::Invalid; }
        if (!Shape.Has_All<ck::FFragment_Pmg_DebugShape_Common,
                ck::FFragment_Transform>() ||
            Shape.Has_Any<ck::FTag_Pmg_DebugShape_Composite,
                ck::FTag_Pmg_DebugShape_LineSet>())
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const bool Sphere = Receipt.Kind == EScriptKind::VisualSphere;
        const bool Box = Receipt.Kind == EScriptKind::VisualBox;
        if ((!Sphere && !Box) ||
            (Sphere && (!Shape.Has<ck::FFragment_Pmg_Sphere_Params>() ||
                        Shape.Has<ck::FFragment_Pmg_Box_Params>())) ||
            (Box && (!Shape.Has<ck::FFragment_Pmg_Box_Params>() ||
                     Shape.Has<ck::FFragment_Pmg_Sphere_Params>())))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        // AddSphere/AddBox can create the physical fragment before the setup
        // processor runs. The typed, owned shape is still legitimately pending.
        if (Shape.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>())
        { return ECkCueGym_SourceVisualOutcome::Pending; }
        if (!Shape.Has<ck::FFragment_Pmg_DebugShape>())
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const auto& Common = Shape.Get<ck::FFragment_Pmg_DebugShape_Common>();
        const auto& Physical = Shape.Get<ck::FFragment_Pmg_DebugShape>();
        auto* Mesh = Physical.Get_MeshComponent().Get();
        const auto TypedShape = ck::StaticCast<FCk_Handle_Pmg_DebugShape>(Shape);
        const auto* ExpectedOuter =
            UCk_Utils_Pmg_DebugShape_UE::Get_MeshComponentOuter(World, TypedShape);
        if (!IsValid(Mesh) || !IsValid(ExpectedOuter) ||
            Mesh->GetOuter() != ExpectedOuter ||
            !Mesh->IsRegistered() || Mesh->GetWorld() != World ||
            Mesh->GetProcMeshSection(0) == nullptr || Mesh->GetMaterial(0) == nullptr)
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const auto Pose = Mesh->GetComponentTransform();
        if (!Pose.IsValid() || Pose.ContainsNaN() ||
            !Pose.Equals(Shape.Get<ck::FFragment_Transform>().Get_Transform(), 0.01f))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const auto Mode = Common.Get_RenderMode();
        if (Mode != ECk_Pmg_RenderMode::SingleSided &&
            Mode != ECk_Pmg_RenderMode::DoubleSided &&
            Mode != ECk_Pmg_RenderMode::Hidden)
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const bool PhysicalVisible = Mode != ECk_Pmg_RenderMode::Hidden &&
            !ck::diagnostic_visibility::Is_HiddenForStreamerMode();
        if (Mesh->IsVisible() != PhysicalVisible ||
            Mesh->bHiddenInGame == PhysicalVisible ||
            Mesh->GetCollisionEnabled() !=
                (Common.Get_EnableCollision() ? ECollisionEnabled::QueryAndPhysics :
                    ECollisionEnabled::NoCollision))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const auto Color = Common.Get_Color();
        const auto Thickness = Common.Get_LineThickness();
        const auto Duration = Common.Get_Duration().Get_Seconds();
        if (!Finite(Color) || !FMath::IsFinite(Thickness) || Thickness < 0.0f ||
            !FMath::IsFinite(Duration))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        Out.Key = static_cast<int64>(static_cast<uint32>(
            Receipt.Source.Get_Entity().Get_ID())) + 1;
        Out.Role = Receipt.Role;
        Out.Kind = Sphere ? ECkCueGym_SourceVisualKind::Sphere :
            ECkCueGym_SourceVisualKind::Box;
        Out.Pose = Pose;
        Out.Color = Color;
        Out.DrawLines = Common.Get_DrawLines();
        Out.RenderMode = Mode;
        Out.VisibleByRenderMode = Mode != ECk_Pmg_RenderMode::Hidden;
        Out.EnableCollision = Common.Get_EnableCollision();
        Out.LineThickness = Thickness;
        Out.Duration = Duration;
        if (Sphere)
        {
            const auto& Params = Shape.Get<ck::FFragment_Pmg_Sphere_Params>();
            Out.Radius = Params.Get_Radius();
            Out.Segments = Params.Get_Segments();
            Out.Rings = Params.Get_Rings();
            Out.Axis = Params.Get_Axis();
            if (!FMath::IsFinite(Out.Radius) || Out.Radius <= 0.0f ||
                Out.Segments < 3 || Out.Rings < 2)
            { return ECkCueGym_SourceVisualOutcome::Invalid; }
        }
        else
        {
            const auto& Params = Shape.Get<ck::FFragment_Pmg_Box_Params>();
            Out.Extent = Params.Get_Extent();
            Out.Axis = Params.Get_Axis();
            if (Out.Extent.ContainsNaN() || Out.Extent.X <= 0.0 ||
                Out.Extent.Y <= 0.0 || Out.Extent.Z <= 0.0)
            { return ECkCueGym_SourceVisualOutcome::Invalid; }
        }
        if (Out.Axis != ECk_Plane_Axis::XY && Out.Axis != ECk_Plane_Axis::XZ &&
            Out.Axis != ECk_Plane_Axis::YZ)
        { return ECkCueGym_SourceVisualOutcome::Invalid; }

        // Verify the material and section counts actually produced by PMG.
        // Counts establish that the configured sphere/box and baked lines have
        // physical sections; they do not prove every triangle coordinate.
        auto* FillMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
        FLinearColor ActualFill;
        const auto* Filled = Mesh->GetProcMeshSection(0);
        if (!IsValid(FillMID) || FillMID->GetOuter() != Mesh ||
            !IsValid(FillMID->Parent.Get()) ||
            !FillMID->GetVectorParameterValue(
                FHashedMaterialParameterInfo{FName(TEXT("Color"))}, ActualFill) ||
            !Finite(ActualFill) ||
            !ActualFill.Equals(ck::pmg_debug_shape::Get_FillColor(Color), 0.001f) ||
            Filled == nullptr)
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        if (Sphere && (static_cast<int64>(Out.Rings) + 1 >
                MAX_int32 / (static_cast<int64>(Out.Segments) + 1) ||
            static_cast<int64>(Out.Rings) >
                MAX_int32 / (static_cast<int64>(Out.Segments) * 6)))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const int64 ExpectedFillVertices = Sphere ?
            (static_cast<int64>(Out.Rings) + 1) *
                (static_cast<int64>(Out.Segments) + 1) : 24;
        const int64 ExpectedFillIndices = Sphere ?
            static_cast<int64>(Out.Rings) * Out.Segments * 6 : 36;
        if (ExpectedFillVertices > MAX_int32 || ExpectedFillIndices > MAX_int32 ||
            Filled->ProcVertexBuffer.Num() != ExpectedFillVertices ||
            Filled->ProcIndexBuffer.Num() != ExpectedFillIndices)
        { return ECkCueGym_SourceVisualOutcome::Invalid; }

        if (Shape.Has<ck::FTag_Pmg_DebugShape_LinesNeedBaking>())
        { return ECkCueGym_SourceVisualOutcome::Pending; }
        if (Shape.Has<ck::FFragment_Pmg_DebugShape_Lines>())
        {
            const auto& SourceLines = Shape.Get<
                ck::FFragment_Pmg_DebugShape_Lines>().Get_Lines();
            if (SourceLines.Num() > MAX_int32 / 36)
            { return ECkCueGym_SourceVisualOutcome::Invalid; }
            Out.Lines.Reserve(SourceLines.Num());
            for (const auto& Line : SourceLines)
            {
                const auto Span = Line._End - Line._Start;
                if (Line._Start.ContainsNaN() || Line._End.ContainsNaN() ||
                    Span.ContainsNaN() || Span.IsNearlyZero() ||
                    !Finite(Line._Color) || !FMath::IsFinite(Line._Thickness) ||
                    Line._Thickness <= 0.0f)
                { return ECkCueGym_SourceVisualOutcome::Invalid; }
                FCkCueGym_SourceVisualLine RowLine;
                RowLine.Start = Line._Start;
                RowLine.End = Line._End;
                RowLine.Color = Line._Color;
                RowLine.Thickness = Line._Thickness;
                Out.Lines.Add(MoveTemp(RowLine));
            }
        }
        if (Common.Get_DrawLines() && Out.Lines.IsEmpty())
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        const auto* Wire = Mesh->GetProcMeshSection(1);
        if (Common.Get_DrawLines())
        {
            auto* WireMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(1));
            FLinearColor ActualWire;
            auto ExpectedWire = Color;
            ExpectedWire.A = 1.0f;
            if (Wire == nullptr || !IsValid(WireMID) ||
                WireMID->GetOuter() != Mesh || WireMID == FillMID ||
                WireMID->Parent != FillMID->Parent ||
                !WireMID->GetVectorParameterValue(
                    FHashedMaterialParameterInfo{FName(TEXT("Color"))}, ActualWire) ||
                !Finite(ActualWire) ||
                !ActualWire.Equals(ExpectedWire, 0.001f) ||
                Wire->ProcVertexBuffer.Num() !=
                    static_cast<int64>(Out.Lines.Num()) * 8 ||
                Wire->ProcIndexBuffer.Num() !=
                    static_cast<int64>(Out.Lines.Num()) * 36)
            { return ECkCueGym_SourceVisualOutcome::Invalid; }
        }
        else if (Wire != nullptr &&
            (!Wire->ProcVertexBuffer.IsEmpty() ||
             !Wire->ProcIndexBuffer.IsEmpty()))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        Out.OutlineRecipeVersion = 1;
        if (!ck_cue_outline_recipe::Get_MatchesActualLines(Out, Out.Lines))
        { return ECkCueGym_SourceVisualOutcome::Invalid; }
        OutMesh = Mesh;
        return ECkCueGym_SourceVisualOutcome::Complete;
    }
}

ECkCueGym_RelayReadiness UCkCueGym_SourceVisuals::Get_RelayReadiness(
    UWorld* InWorld, AActor* InSourceState)
{
    // An absent executor is ordinary startup; a mismatched caller is not.
    if (!IsInGameThread() || !IsValid(InWorld) || !InWorld->IsGameWorld() ||
        InWorld->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        InWorld->IsNetMode(NM_Client) || !IsValid(InSourceState) ||
        InSourceState->IsActorBeingDestroyed() ||
        InSourceState->GetWorld() != InWorld ||
        InWorld->GetGameState() != InSourceState ||
        !InSourceState->HasAuthority() ||
        InSourceState->GetClass()->GetPathName() !=
            TEXT("/Script/Angelscript.Ck_CueGym_GameState"))
    { return ECkCueGym_RelayReadiness::Invalid; }

    const auto WorldRoot =
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld);
    if (!ck_cue_source_visuals::Live(WorldRoot))
    { return ECkCueGym_RelayReadiness::Pending; }

    auto* Executor = InWorld->GetSubsystem<UCk_GenericCueExecutor_Subsystem_UE>();
    if (!IsValid(Executor)) { return ECkCueGym_RelayReadiness::Pending; }
    if (Executor->GetWorld() != InWorld ||
        Executor->GetClass() != UCk_GenericCueExecutor_Subsystem_UE::StaticClass())
    { return ECkCueGym_RelayReadiness::Invalid; }
    if (Executor->Get_ChannelCount_Active() <= 0)
    { return ECkCueGym_RelayReadiness::Pending; }

    // The relay binds this exact group only after pool registration. This
    // observation does not acquire a channel or advance round-robin state.
    for (TActorIterator<ACk_CueRelay_UE> It(InWorld); It; ++It)
    {
        const auto* Relay = *It;
        if (!IsValid(Relay) || Relay->IsActorBeingDestroyed() ||
            Relay->GetWorld() != InWorld ||
            Relay->GetClass() != ACk_CueRelay_UE::StaticClass() ||
            Relay->Get_GroupSubsystem().Get() != Executor ||
            !UCk_Utils_OwningActor_UE::Get_IsActorEcsReady(Relay))
        { continue; }
        const auto Channel =
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Relay);
        if (!ck_cue_source_visuals::Live(Channel)) { continue; }
        if (!ck_cue_source_visuals::SameWorld(Channel, WorldRoot))
        { return ECkCueGym_RelayReadiness::Invalid; }
        if (UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Channel) == Relay)
        { return ECkCueGym_RelayReadiness::Ready; }
    }
    return ECkCueGym_RelayReadiness::Pending;
}

double UCkCueGym_SourceVisuals::Get_RelayWallClockSeconds()
{
    return FPlatformTime::Seconds();
}

auto UCkCueGym_SourceVisuals::TryGet_SourceVisuals(
    UWorld* InWorld, const FCk_Handle& InSourceOwner,
    const TArray<FCk_Handle>& InRoleRoots,
    TArray<FCkCueGym_SourceVisualRow>& OutRows,
    FString& OutFailureReason) -> ECkCueGym_SourceVisualOutcome
{
    using namespace ck_cue_source_visuals;
    OutRows.Reset();
    OutFailureReason.Reset();
    const auto Fail = [&OutFailureReason](const TCHAR* Why)
    {
        OutFailureReason = Why;
        return ECkCueGym_SourceVisualOutcome::Invalid;
    };
    if (!IsInGameThread() || !IsValid(InWorld) || !InWorld->IsGameWorld() ||
        InWorld->GetNetMode() == NM_Client ||
        InRoleRoots.Num() != RoleCount)
    { return Fail(TEXT("invalid authority world, thread or six-role input")); }
    const auto WorldRoot = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*InWorld);
    if (!Live(WorldRoot) || !Live(InSourceOwner) ||
        !DirectParent(InSourceOwner, WorldRoot, WorldRoot))
    { return Fail(TEXT("source owner is not a live direct child of this world")); }
    TArray<FCk_Handle> OwnerChildren;
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
            InSourceOwner, WorldRoot, OwnerChildren) || OwnerChildren.Num() != RoleCount)
    { return Fail(TEXT("source owner does not have exactly six allocated roles")); }
    for (int32 Role = 0; Role < RoleCount; ++Role)
    {
        const auto& Root = InRoleRoots[Role];
        if (!Live(Root) || !DirectParent(Root, InSourceOwner, WorldRoot) ||
            !OwnerChildren.Contains(Root))
        { return Fail(TEXT("role root missing, duplicated or wrongly owned")); }
        auto* Script = GetScript(Root);
        if (!IsValid(Script) || !ClassIs(Script->GetClass(), RoleClasses[Role]))
        { return Fail(TEXT("role root has wrong authored script class")); }
        for (int32 Earlier = 0; Earlier < Role; ++Earlier)
        { if (Root == InRoleRoots[Earlier]) { return Fail(TEXT("duplicate role root")); } }
    }
    TArray<FVisit> Queue;
    for (int32 Role = 0; Role < RoleCount; ++Role)
    { if (Role != TransientRole) { Queue.Add(FVisit{InRoleRoots[Role], Role}); } }
    TArray<FCk_Handle> Seen;
    TArray<FReceipt> Receipts;
    TArray<FCkCueGym_SourceVisualRow> Candidate;
    bool Pending = false;
    for (int32 Index = 0; Index < Queue.Num(); ++Index)
    {
        const auto Current = Queue[Index];
        if (!SameWorld(Current.Handle, WorldRoot) || Seen.Contains(Current.Handle))
        { return Fail(TEXT("lifetime walk has a cycle, duplicate or foreign world")); }
        Seen.Add(Current.Handle);
        const bool Retired = Current.RetiredLineage || !Live(Current.Handle);
        auto* Script = Retired ? nullptr : GetScript(Current.Handle);
        EScriptKind ScriptKind = EScriptKind::Unknown;
        if (Current.Handle != InRoleRoots[Current.Role])
        {
            UClass* PendingClass = nullptr;
            const bool KnownPending = !Retired && !IsValid(Script) &&
                TryGetPendingTargetClass(Current.Handle, Current.Parent,
                    WorldRoot, PendingClass);
            if (Current.Handle.Has<ck::FFragment_EntityScript_RequestSpawnEntity>())
            {
                const auto& Request = Current.Handle.Get<
                    ck::FFragment_EntityScript_RequestSpawnEntity,
                    ck::IsValid_Policy_IncludePendingKill>();
                auto* Archetype = Request.Get_EntityScriptClassArchetype().Get();
                TArray<FCk_Handle> CarrierChildren;
                if (Current.ParentScriptKind == EScriptKind::Unknown ||
                    Request.Get_NewEntity() != Current.Parent ||
                    !DirectParent(Current.Parent, Request.Get_Owner(), WorldRoot) ||
                    Request.Get_ContextOwner() != Request.Get_Owner() ||
                    !IsValid(Archetype) ||
                    Archetype != Archetype->GetClass()->GetDefaultObject(false) ||
                    Classify(Archetype->GetClass(), Current.Role) !=
                        Current.ParentScriptKind ||
                    !UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                        Current.Handle, WorldRoot, CarrierChildren) ||
                    !CarrierChildren.IsEmpty())
                { return Fail(TEXT("spawn carrier target/owner lineage is invalid")); }
            }
            else if (Current.Handle.Has_Any<ck::FFragment_EntityScript,
                    ck::FFragment_SpawnRecipe>() || KnownPending)
            {
                if (!Retired &&
                    Current.Handle.Has<ck::FFragment_EntityScript>() &&
                    !IsValid(Script))
                { return Fail(TEXT("live script fragment has no valid script")); }
                auto* AuthoredClass = IsValid(Script) ? Script->GetClass() :
                    GetAuthoredClass(Current.Handle);
                if (!IsValid(AuthoredClass) &&
                    !Current.Handle.Has<ck::FFragment_SpawnRecipe>())
                { AuthoredClass = PendingClass; }
                ScriptKind = Classify(AuthoredClass, Current.Role);
                if (ScriptKind == EScriptKind::Unknown ||
                    Current.ParentScriptKind == EScriptKind::Unknown)
                { return Fail(TEXT("unexpected script under authored role")); }
                if (!Retired && !IsValid(Script))
                {
                    if (!KnownPending || PendingClass != AuthoredClass)
                    { return Fail(TEXT("scriptless target lacks unique matching spawn carrier")); }
                    Pending = true;
                }
                if (!Retired && IsValid(Script) &&
                    (ScriptKind == EScriptKind::VisualSphere ||
                     ScriptKind == EScriptKind::VisualBox))
                {
                    FCk_Handle Shape;
                    bool Attempted = false;
                    if (!ReadCurrentShape(Script, Shape) ||
                        !ReadDrawAttempted(Script, Attempted))
                    { return Fail(TEXT("visual script lacks exact receipt/attempt getters")); }
                    if (!Allocated(Shape))
                    {
                        if (Attempted)
                        { return Fail(TEXT("draw-attempted source lost current receipt")); }
                        Pending = true;
                    }
                    else
                    {
                        if (!Attempted)
                        { return Fail(TEXT("current receipt predates draw attempt")); }
                        Receipts.Add(FReceipt{Current.Handle, Shape,
                            Current.Role, ScriptKind});
                    }
                }
            }
            else if (Current.Handle.Has<ck::FFragment_Timer>())
            {
                if (Current.ParentScriptKind == EScriptKind::Unknown)
                { return Fail(TEXT("timer is not a direct known script child")); }
            }
            else if (Current.Handle.Has_Any<ck::FFragment_Pmg_Sphere_Params,
                    ck::FFragment_Pmg_Box_Params>())
            {
                if (Current.ParentScriptKind != EScriptKind::VisualSphere &&
                    Current.ParentScriptKind != EScriptKind::VisualBox)
                { return Fail(TEXT("shape is not a direct visual script child")); }
                if ((Current.ParentScriptKind == EScriptKind::VisualSphere &&
                     !Current.Handle.Has<ck::FFragment_Pmg_Sphere_Params>()) ||
                    (Current.ParentScriptKind == EScriptKind::VisualBox &&
                     !Current.Handle.Has<ck::FFragment_Pmg_Box_Params>()))
                { return Fail(TEXT("shape kind differs from parent script")); }
            }
            else if (Retired &&
                Current.Parent != InRoleRoots[Current.Role] &&
                Current.ParentScriptKind != EScriptKind::Unknown &&
                Current.Handle.Has_Any<ck::FTag_DestroyEntity_Initiate,
                    ck::FTag_DestroyEntity_EndPlay,
                    ck::FTag_DestroyEntity_Teardown,
                    ck::FTag_DestroyEntity_Await,
                    ck::FTag_DestroyEntity_Finalize>())
            {
                // Spawn request processing removes its request fragment after
                // requesting carrier destruction. This narrow terminal shell
                // can remain allocated briefly under its known script target.
            }
            else
            { return Fail(TEXT("unclassified live allocation in role lifetime tree")); }
        }
        else { ScriptKind = EScriptKind::NoVisual; }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Current.Handle, WorldRoot, Children))
        { return Fail(TEXT("cannot read allocated lifetime dependents")); }
        if (Current.Handle.Has<ck::FFragment_Timer>() ||
            Current.Handle.Has_Any<ck::FFragment_Pmg_Sphere_Params,
                ck::FFragment_Pmg_Box_Params>() ||
            (Retired && Current.Parent != InRoleRoots[Current.Role] &&
                !Current.Handle.Has_Any<ck::FFragment_EntityScript,
                    ck::FFragment_SpawnRecipe,
                    ck::FFragment_EntityScript_RequestSpawnEntity,
                    ck::FFragment_Timer,
                    ck::FFragment_Pmg_Sphere_Params,
                    ck::FFragment_Pmg_Box_Params>()))
        { if (!Children.IsEmpty())
          { return Fail(TEXT("timer or shape unexpectedly owns descendants")); } }
        for (const auto& Child : Children)
        {
            if (!DirectParent(Child, Current.Handle, WorldRoot))
            { return Fail(TEXT("allocated child has wrong parent or registry")); }
            Queue.Add(FVisit{Child, Current.Role, ScriptKind,
                Retired, Current.Handle});
        }
    }
    // Every active typed shape must be the exact current getter receipt.
    // Retired prior receipts are inspected for lineage but never published or
    // treated as unsettled work; replacement may retire one every frame.
    for (const auto& Visit : Queue)
    {
        if (!Allocated(Visit.Handle) ||
            !Visit.Handle.Has_Any<ck::FFragment_Pmg_Sphere_Params,
                ck::FFragment_Pmg_Box_Params>()) { continue; }
        bool IsCurrent = false;
        for (const auto& Receipt : Receipts)
        { IsCurrent |= Receipt.Shape == Visit.Handle; }
        if (!IsCurrent)
        {
            if (Visit.RetiredLineage || !Live(Visit.Handle)) { continue; }
            return Fail(TEXT("live PMG shape is not a current authored receipt"));
        }
    }
    TArray<UProceduralMeshComponent*> Meshes;
    for (const auto& Receipt : Receipts)
    {
        if (!Seen.Contains(Receipt.Shape))
        { return Fail(TEXT("current receipt missing from exact lifetime tree")); }
        FCkCueGym_SourceVisualRow Row;
        UProceduralMeshComponent* Mesh = nullptr;
        const auto Outcome = ReadMaterialized(
            Receipt, InWorld, WorldRoot, Row, Mesh);
        if (Outcome == ECkCueGym_SourceVisualOutcome::Invalid)
        { return Fail(TEXT("current PMG receipt failed physical/style proof")); }
        if (Outcome == ECkCueGym_SourceVisualOutcome::Pending)
        { Pending = true; continue; }
        if (!IsValid(Mesh) || Meshes.Contains(Mesh))
        { return Fail(TEXT("physical mesh is absent or shared by receipts")); }
        Meshes.Add(Mesh);
        for (const auto& Prior : Candidate)
        { if (Prior.Key == Row.Key) { return Fail(TEXT("duplicate source visual key")); } }
        Candidate.Add(Row);
    }
    if (Pending) { return ECkCueGym_SourceVisualOutcome::Pending; }
    Candidate.Sort([](const FCkCueGym_SourceVisualRow& A,
        const FCkCueGym_SourceVisualRow& B)
    { return A.Role == B.Role ? A.Key < B.Key : A.Role < B.Role; });
    OutRows = MoveTemp(Candidate);
    return ECkCueGym_SourceVisualOutcome::Complete;
}

bool UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(
    const TArray<FCkCueGym_SourceVisualRow>& InRows,
    TArray<FCkCueGym_SourceVisualRow>& OutRows)
{
    if (&InRows == &OutRows) { OutRows.Reset(); return false; }
    OutRows.Reset();
    TArray<FCkCueGym_SourceVisualRow> Candidate;
    Candidate.Reserve(InRows.Num());
    TSet<int64> Keys;
    for (const auto& Row : InRows)
    {
        if (Row.OutlineRecipeVersion != 1 || Row.Key <= 0 ||
            Row.Role < 0 || Row.Role >= 6 || Row.Role == 4 ||
            Keys.Contains(Row.Key) || !Row.Pose.IsValid() ||
            Row.Pose.ContainsNaN() ||
            !FMath::IsFinite(Row.Color.R) || !FMath::IsFinite(Row.Color.G) ||
            !FMath::IsFinite(Row.Color.B) || !FMath::IsFinite(Row.Color.A) ||
            !FMath::IsFinite(Row.LineThickness) || Row.LineThickness < 0.0f ||
            (Row.DrawLines && Row.LineThickness <= 0.0f) ||
            !FMath::IsFinite(Row.Duration) ||
            (Row.RenderMode != ECk_Pmg_RenderMode::SingleSided &&
             Row.RenderMode != ECk_Pmg_RenderMode::DoubleSided &&
             Row.RenderMode != ECk_Pmg_RenderMode::Hidden) ||
            Row.VisibleByRenderMode !=
                (Row.RenderMode != ECk_Pmg_RenderMode::Hidden) ||
            (Row.Axis != ECk_Plane_Axis::XY && Row.Axis != ECk_Plane_Axis::XZ &&
             Row.Axis != ECk_Plane_Axis::YZ))
        { return false; }
        if (Row.Kind == ECkCueGym_SourceVisualKind::Sphere)
        {
            if (!FMath::IsFinite(Row.Radius) || Row.Radius <= 0.0f ||
                Row.Radius > 1000000.0f || Row.Segments != 12 ||
                Row.Rings != 12 || Row.Extent != FVector::ZeroVector)
            { return false; }
        }
        else if (Row.Kind == ECkCueGym_SourceVisualKind::Box)
        {
            if (Row.Radius != 0.0f || Row.Segments != 0 || Row.Rings != 0 ||
                !FMath::IsFinite(Row.Extent.X) || !FMath::IsFinite(Row.Extent.Y) ||
                !FMath::IsFinite(Row.Extent.Z) || Row.Extent.X <= 0.0 ||
                Row.Extent.Y <= 0.0 || Row.Extent.Z <= 0.0 ||
                Row.Extent.X > 1000000.0 || Row.Extent.Y > 1000000.0 ||
                Row.Extent.Z > 1000000.0)
            { return false; }
        }
        else { return false; }
        TArray<FCkCueGym_SourceVisualLine> Expected;
        if (!ck_cue_outline_recipe::TryGenerate(Row, Expected) ||
            (!Row.Lines.IsEmpty() &&
             !ck_cue_outline_recipe::Get_MatchesActualLines(Row, Row.Lines)))
        { return false; }
        auto& Expanded = Candidate.Add_GetRef(Row);
        Expanded.Lines = MoveTemp(Expected);
        Keys.Add(Row.Key);
    }
    OutRows = MoveTemp(Candidate);
    return true;
}

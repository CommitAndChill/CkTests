#include "CkCompassGym_Mirror.h"
#include "CkCompassGym_Mirror_Private.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkGym_AdmissionRuntime.h"
#include "CkCompass/CkCompass_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkPoi/CkPoi_Fragment.h"
#include "CkVisibleRange/CkVisibleRange_Fragment.h"
#include "CkPoiDisplayDefinition/CkPoiDisplayDefinition_Utils.h"
#include "CkPoiDisplayDefinition/CkPoiDisplayDefinition_Fragment.h"
#include "CkPmg/CkPmg_Utils_BasicShapes.h"
#include "CkPmg/CkPmg_Utils_DebugShapes.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "CkPmg/CkPmg_Fragment_BasicShapes.h"
#include "CkCore/Diagnostics/CkDiagnosticVisibility.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UnrealType.h"

CK_REGISTER_PROCESSOR(ck::FProcessor_CompassGym_Mirror);

namespace ck_compass_mirror
{
    using FEntry = ck::FCompassGym_MirrorEntry;
    using FState = ck::FFragment_CompassGym_Mirror;
    using EPhase = ck::ECompassGym_MirrorPhase;
    constexpr double PendingTimeoutSeconds = 30.0;

#if WITH_DEV_AUTOMATION_TESTS
    struct FScopedMirrorProfile
    {
        ck::FCompassGym_MirrorProfileCounter& Counter;
        const double Started = FPlatformTime::Seconds();

        explicit FScopedMirrorProfile(
            ck::FCompassGym_MirrorProfileCounter& InCounter)
            : Counter(InCounter) {}

        ~FScopedMirrorProfile()
        {
            ++Counter.Calls;
            Counter.Seconds += FPlatformTime::Seconds() - Started;
        }
    };
#endif

    bool Allocated(const FCk_Handle& H)
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    bool Live(const FCk_Handle& H)
    {
        return Allocated(H) && ck::IsValid(H) &&
            !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    bool Direct(const FCk_Handle& Child, const FCk_Handle& Parent,
        const FCk_Handle& WorldRoot)
    {
        if (!Allocated(Child) || !Allocated(Parent) || !Allocated(WorldRoot) ||
            Child.Get_RegistryView().Get_RegistryHandle() !=
                WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            Parent.Get_RegistryView().Get_RegistryHandle() !=
                WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            !Child.Has<ck::FFragment_LifetimeOwner>())
        { return false; }
        return Child.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == Parent;
    }

    bool Context(UWorld* World, AGameStateBase* GS,
        const FCk_Handle& MirrorRoot, FCk_Handle& OutWorldRoot)
    {
        OutWorldRoot = {};
        if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
            !World->IsNetMode(NM_Client) ||
            World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            World->bIsTearingDown || World->IsBeingCleanedUp() ||
            !IsValid(GS) || GS->IsActorBeingDestroyed() ||
            GS->GetWorld() != World || World->GetGameState() != GS)
        { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Live(Root) || !Root.Has<TWeakObjectPtr<UWorld>>() ||
            Root.Get<TWeakObjectPtr<UWorld>>().Get() != World ||
            !Live(MirrorRoot) || !Direct(MirrorRoot, Root, Root))
        { return false; }
        OutWorldRoot = Root;
        return true;
    }

    bool Open(const FCk_Handle& WorldRoot)
    {
        bool AdmissionClosed = true;
        return UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(
                WorldRoot, AdmissionClosed) && !AdmissionClosed &&
            !UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(WorldRoot);
    }

    bool CapturedWorldRoot(const FState& State, FCk_Handle& OutRoot)
    {
        OutRoot = {};
        auto* World = State.World.Get();
        if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
            !World->IsNetMode(NM_Client) ||
            World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            World->bIsTearingDown || World->IsBeingCleanedUp() ||
            !Live(State.WorldRoot) ||
            !State.WorldRoot.Has<TWeakObjectPtr<UWorld>>() ||
            State.WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get() != World)
        { return false; }
        const auto Exact = UCk_Utils_EcsWorld_Subsystem_UE::
            TryGet_TransientEntityForWorld(*World);
        if (!Live(Exact) || Exact != State.WorldRoot)
        { return false; }
        OutRoot = Exact;
        return true;
    }

    bool BoundOpen(const FState& State,
        const FCk_Handle& MirrorRoot, const FCk_Handle& WorldRoot)
    {
        FCk_Handle Exact;
        return Context(State.World.Get(), State.GameState.Get(),
                MirrorRoot, Exact) && Exact == WorldRoot &&
            State.WorldRoot == Exact && !State.ClosedUnproven && Open(Exact);
    }

    void Fail(FState& State, const FCk_Handle& MirrorRoot,
        const FCk_Handle& WorldRoot, const TCHAR* Why)
    {
        (void)MirrorRoot;
        State.ClosedUnproven = true;
        State.Phase = EPhase::Failed;
        State.Failure = Why;
        // Only the captured exact client-world root is eligible for closure.
        FCk_Handle ExactRoot;
        if (CapturedWorldRoot(State, ExactRoot) &&
            (!Allocated(WorldRoot) || WorldRoot == ExactRoot))
        {
            const bool Projection = UCk_Utils_Compass_UE::
                TryCloseWorldProjection(ExactRoot);
            const bool Admission = UCkGym_AdmissionRuntime_UE::
                TryCloseWorldUnproven(ExactRoot);
            if (!Projection || !Admission)
            { State.Failure += TEXT("; exact-world closure unproven"); }
        }
        else { State.Failure += TEXT("; exact world root unavailable"); }
    }

    bool SameRow(const FCkCompassGym_SourceRow& A,
        const FCkCompassGym_SourceRow& B)
    {
        return FCkCompassGym_SourceRow::StaticStruct()->
            CompareScriptStruct(&A, &B, PPF_None);
    }

    const FEntry* Find(const TArray<FEntry>& Entries, int64 Id)
    {
        for (const auto& E : Entries)
        { if (E.Row.CatalogId == Id) { return &E; } }
        return nullptr;
    }

    bool CompleteCensus(const FCk_Handle& MirrorRoot,
        const FCk_Handle& WorldRoot, const FState& State)
    {
#if WITH_DEV_AUTOMATION_TESTS
        FScopedMirrorProfile Profile(State.Profile.CompleteCensus);
#endif
        TArray<FCk_Handle> ReverseHosts;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(
                MirrorRoot, WorldRoot, ReverseHosts))
        { return false; }
        TSet<FCk_Handle> KnownHosts;
        TSet<FCk_Handle> KnownMarkers;
        TMap<FCk_Handle, FCk_Handle> AllowedMarkerByHost;
        TSet<FCk_Handle> MarkerParents;
        const auto Add = [&](const TArray<FEntry>& Entries) {
            for (const auto& E : Entries)
            {
                // Include deallocated receipt identities: the current-owner
                // census below must still reject a child of an old Host/Marker.
                if (const auto* Existing = AllowedMarkerByHost.Find(E.Host))
                {
                    if (*Existing != E.Marker) { return false; }
                }
                else { AllowedMarkerByHost.Add(E.Host, E.Marker); }
                MarkerParents.Add(E.Marker);
                if (!Allocated(E.Host)) { continue; }
                if (KnownHosts.Contains(E.Host)) { return false; }
                KnownHosts.Add(E.Host);
                if (Allocated(E.Marker))
                {
                    if (KnownMarkers.Contains(E.Marker)) { return false; }
                    KnownMarkers.Add(E.Marker);
                }
            }
            return true;
        };
        if (!Add(State.Installed) || !Add(State.Staged) ||
            !Add(State.Retiring) || ReverseHosts.Num() != KnownHosts.Num())
        { return false; }
        TSet<FCk_Handle> ReverseHostSet;
        ReverseHostSet.Reserve(ReverseHosts.Num());
        for (const auto& H : ReverseHosts) { ReverseHostSet.Add(H); }
        for (const auto& H : KnownHosts)
        {
            if (!ReverseHostSet.Contains(H) || !Direct(H, MirrorRoot, WorldRoot))
            { return false; }
        }
        if (MirrorRoot.Has<ck::FFragment_LifetimeDependents>())
        {
            for (const auto& Raw : MirrorRoot.Get<
                ck::FFragment_LifetimeDependents>().Get_Entities())
            {
                if (Allocated(Raw) && !KnownHosts.Contains(Raw))
                { return false; }
            }
        }
        for (const auto& E : State.Installed)
        {
            if (!Live(E.Host) || (E.Row.HasMarker && !Live(E.Marker)))
            { return false; }
        }
        for (const auto& E : State.Staged)
        { if (!Live(E.Host) || (E.Row.HasMarker && !Live(E.Marker))) { return false; } }
        for (const auto& E : State.Retiring)
        {
            if (Allocated(E.Host) && !Direct(E.Host, MirrorRoot, WorldRoot))
            { return false; }
            if (Allocated(E.Marker) &&
                (!Allocated(E.Host) || !Direct(E.Marker, E.Host, WorldRoot)))
            { return false; }
        }
        bool Complete = true;
        TSet<FCk_Handle> ActualRootHosts;
        const auto Registry = WorldRoot.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Owner) {
                const FCk_Handle Child{Entity, RegistryHandle};
                if (!Allocated(Child)) { return; }
                const auto Parent = Owner.Get_Entity();
                if (Parent == MirrorRoot) { ActualRootHosts.Add(Child); }
                if (const auto* Allowed = AllowedMarkerByHost.Find(Parent))
                { if (Child != *Allowed) { Complete = false; } }
                if (MarkerParents.Contains(Parent)) { Complete = false; }
            });
        if (!Complete || ActualRootHosts.Num() != KnownHosts.Num())
        { return false; }
        for (const auto& H : ActualRootHosts)
        { if (!KnownHosts.Contains(H)) { return false; } }
        for (const auto& E : State.Installed)
        {
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryGet_AllocatedDirectLifetimeDependents(E.Host, WorldRoot, Children) ||
                Children.Num() != int32(E.Row.HasMarker) ||
                (E.Row.HasMarker && (!Children.Contains(E.Marker) ||
                    !Direct(E.Marker, E.Host, WorldRoot))))
            { return false; }
        }
        for (const auto& E : State.Staged)
        {
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryGet_AllocatedDirectLifetimeDependents(E.Host, WorldRoot, Children) ||
                Children.Num() != int32(E.Row.HasMarker) ||
                (E.Row.HasMarker && (!Children.Contains(E.Marker) ||
                    !Direct(E.Marker, E.Host, WorldRoot))))
            { return false; }
        }
        for (const auto& E : State.Retiring)
        {
            if (!Allocated(E.Host)) { continue; }
            TArray<FCk_Handle> Children;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
                TryGet_AllocatedDirectLifetimeDependents(E.Host, WorldRoot,
                    Children) || Children.Num() != int32(Allocated(E.Marker)) ||
                (Allocated(E.Marker) && !Children.Contains(E.Marker)))
            { return false; }
        }
        // The native proof also reads raw, unfiltered lifetime dependents.
        // A transferred child lingering only in an old reverse read-set is
        // unsafe even when the current-owner census looks complete.
        TArray<FCk_Handle> AllKnown;
        const auto AddKnown = [&](const TArray<FEntry>& Entries) {
            for (const auto& E : Entries)
            {
                AllKnown.Add(E.Host);
                if (E.Marker != FCk_Handle{}) { AllKnown.Add(E.Marker); }
            }
        };
        AddKnown(State.Installed);
        AddKnown(State.Staged);
        AddKnown(State.Retiring);
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryProve_FrozenRetirementLedger(AllKnown, AllKnown, WorldRoot))
        { return false; }
        // Native proof includes both the unfiltered destruction read-set and
        // allocated current-owner census, even for pending-kill members.
        TArray<FCk_Handle> Retiring;
        for (const auto& E : State.Retiring)
        {
            Retiring.Add(E.Host);
            if (E.Marker != FCk_Handle{}) { Retiring.Add(E.Marker); }
        }
        return UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryProve_FrozenRetirementLedger(Retiring, Retiring, WorldRoot);
    }

    bool StageRow(const FCkCompassGym_SourceRow& Row,
        const FCk_Handle& MirrorRoot, const FCk_Handle& WorldRoot,
        FState& State)
    {
        FEntry Entry;
        Entry.Row = Row;
        Entry.Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(MirrorRoot);
        State.Staged.Add(Entry); // retain first return before validity/composition
        if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
            !Live(Entry.Host) || !Direct(Entry.Host, MirrorRoot, WorldRoot))
        { return false; }
        const int32 Index = State.Staged.Num() - 1;
        auto MutableHost = State.Staged[Index].Host;
        const auto Transform = UCk_Utils_Transform_UE::Add(MutableHost,
            FTransform(FRotator::ZeroRotator, Row.Location),
            ECk_Replication::DoesNotReplicate);
        if (!Live(Transform) || !BoundOpen(State, MirrorRoot, WorldRoot) ||
            !Direct(Entry.Host, MirrorRoot, WorldRoot))
        { return false; }
        const auto Tagged = UCk_Utils_EntityTag_UE::
            Add_UsingGameplayTag(MutableHost, Row.Category);
        if (!Live(Tagged) || !BoundOpen(State, MirrorRoot, WorldRoot) ||
            !Direct(Entry.Host, MirrorRoot, WorldRoot) ||
            State.Staged[Index].Host.Has<ck::FTag_Poi>())
        { return false; }
        if (!Row.HasDisplay) { return !Row.HasMarker; }
        auto Spec = FCk_PoiDisplayDefinition_Spec(Row.Consumer);
        Spec.Set_Icon(Row.Icon).Set_Priority(Row.Priority)
            .Set_OffscreenPolicy(Row.OffscreenPolicy)
            .Set_Tint(Row.Tint).Set_SizeHint(Row.SizeHint);
        const auto Display = UCk_Utils_PoiDisplayDefinition_UE::Add(MutableHost, Spec);
        if (!Live(Display) || !BoundOpen(State, MirrorRoot, WorldRoot) ||
            !Direct(Entry.Host, MirrorRoot, WorldRoot))
        { return false; }
        if (Row.HasMarker)
        {
            const auto Marker = UCk_Utils_Pmg_BasicShapes::Create_Sphere(
                MutableHost, Row.Marker.Pose, Row.Marker.Radius,
                Row.Marker.Segments, Row.Marker.Rings, Row.Marker.Axis,
                Row.Marker.Color, Row.Marker.DrawLines,
                Row.Marker.LineThickness, Row.Marker.Duration);
            State.Staged[Index].Marker = Marker;
            if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
                !Live(Marker) ||
                !Direct(Marker, State.Staged[Index].Host, WorldRoot))
            { return false; }
        }
        return true;
    }

    bool Finite(const FLinearColor& C)
    { return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) &&
        FMath::IsFinite(C.B) && FMath::IsFinite(C.A); }

    ECkCompassGym_CatalogOutcome Physical(UWorld* World,
        const FEntry& E, const FCk_Handle& MirrorRoot,
        const FCk_Handle& WorldRoot, bool Exposed)
    {
        const auto& R = E.Row;
        if (!Live(E.Host) || !Direct(E.Host, MirrorRoot, WorldRoot) ||
            E.Host.Has<ck::FTag_Poi>() != Exposed ||
            E.Host.Has_Any<ck::FFragment_VisibleRange_Params,
                ck::FFragment_VisibleRange, ck::FTag_VisibleRange_Hidden>() ||
            !E.Host.Has<ck::FFragment_Transform>())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto HostPose = E.Host.Get<ck::FFragment_Transform>().Get_Transform();
        if (!HostPose.IsValid() || HostPose.ContainsNaN() ||
            !HostPose.GetLocation().Equals(R.Location, 0.01) ||
            !HostPose.GetRotation().Equals(FQuat::Identity, 0.0001) ||
            !HostPose.GetScale3D().Equals(FVector::OneVector, 0.0001))
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto CategoryRoot = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Poi.Category")), false);
        const auto Disabled = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Poi.Disabled")), false);
        if (!CategoryRoot.IsValid() || !Disabled.IsValid())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto Tags = UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(E.Host);
        const auto Categories = Tags.Filter(FGameplayTagContainer(CategoryRoot));
        if (Tags.HasTagExact(Disabled) || Categories.Num() > 1 ||
            (Categories.Num() == 1 &&
                !Categories.HasTagExact(R.Category)))
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        // EntityTag Add_UsingGameplayTag queues its request; staged Hosts can
        // legitimately wait one ordered pump for the category to materialize.
        if (Categories.IsEmpty())
        { return Exposed ? ECkCompassGym_CatalogOutcome::Invalid :
            ECkCompassGym_CatalogOutcome::Pending; }
        const bool Params = E.Host.Has<ck::FFragment_PoiDisplayDefinition_Params>();
        const bool Current = E.Host.Has<ck::FFragment_PoiDisplayDefinition>();
        if (!R.HasDisplay)
        {
            if (Params || Current || E.Marker != FCk_Handle{})
            { return ECkCompassGym_CatalogOutcome::Invalid; }
            return ECkCompassGym_CatalogOutcome::Complete;
        }
        if (Params)
        {
            const auto& D = E.Host.Get<ck::FFragment_PoiDisplayDefinition_Params>();
            if (D.Get_Consumer() != R.Consumer || D.Get_Icon() != R.Icon ||
                D.Get_Priority() != R.Priority ||
                D.Get_OffscreenPolicy() != R.OffscreenPolicy)
            { return ECkCompassGym_CatalogOutcome::Invalid; }
        }
        if (Current)
        {
            const auto& D = E.Host.Get<ck::FFragment_PoiDisplayDefinition>();
            if (!Finite(D.Get_Tint()) || D.Get_Tint() != R.Tint ||
                D.Get_SizeHint() != R.SizeHint)
            { return ECkCompassGym_CatalogOutcome::Invalid; }
        }
        if (!Params || !Current)
        { return ECkCompassGym_CatalogOutcome::Pending; }
        if (!R.HasMarker || !Live(E.Marker) ||
            !Direct(E.Marker, E.Host, WorldRoot) ||
            E.Marker.Has_Any<ck::FTag_Pmg_DebugShape_Composite,
                ck::FTag_Pmg_DebugShape_LineSet>() ||
            E.Marker.Has<ck::FFragment_Pmg_Box_Params>())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        if (!E.Marker.Has_All<ck::FFragment_Pmg_Sphere_Params,
                ck::FFragment_Pmg_DebugShape_Common, ck::FFragment_Transform>())
        { return ECkCompassGym_CatalogOutcome::Pending; }
        const auto& Sphere = E.Marker.Get<ck::FFragment_Pmg_Sphere_Params>();
        const auto& Common = E.Marker.Get<ck::FFragment_Pmg_DebugShape_Common>();
        const auto Pose = E.Marker.Get<ck::FFragment_Transform>().Get_Transform();
        const auto& M = R.Marker;
        if (Sphere.Get_Radius() != M.Radius ||
            Sphere.Get_Segments() != M.Segments ||
            Sphere.Get_Rings() != M.Rings || Sphere.Get_Axis() != M.Axis ||
            !Pose.IsValid() || Pose.ContainsNaN() || !Pose.Equals(M.Pose, 0.01) ||
            !Finite(Common.Get_Color()) ||
            !Common.Get_Color().Equals(M.Color, 0.0001) ||
            Common.Get_DrawLines() != M.DrawLines ||
            Common.Get_LineThickness() != M.LineThickness ||
            Common.Get_Duration().Get_Seconds() != M.Duration ||
            Common.Get_EnableCollision() != M.EnableCollision ||
            Common.Get_RenderMode() != M.RenderMode ||
            !E.Marker.Has<ck::FTag_Pmg_DebugShape_PersistentDuration>())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        if (E.Marker.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>() ||
            E.Marker.Has<ck::FTag_Pmg_DebugShape_LinesNeedBaking>())
        { return ECkCompassGym_CatalogOutcome::Pending; }
        if (!E.Marker.Has_All<ck::FFragment_Pmg_DebugShape,
                ck::FFragment_Pmg_DebugShape_Lines>())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto& ActualLines = E.Marker.Get<
            ck::FFragment_Pmg_DebugShape_Lines>().Get_Lines();
        if (ActualLines.Num() != M.Lines.Num() || ActualLines.Num() != 96)
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        for (int32 I = 0; I < ActualLines.Num(); ++I)
        {
            const auto& A = ActualLines[I];
            const auto& B = M.Lines[I];
            if (A._Start.ContainsNaN() || A._End.ContainsNaN() ||
                !Finite(A._Color) || !FMath::IsFinite(A._Thickness) ||
                !A._Start.Equals(B.Start, 0.001) ||
                !A._End.Equals(B.End, 0.001) ||
                !A._Color.Equals(B.Color, 0.001) ||
                A._Thickness != B.Thickness)
            { return ECkCompassGym_CatalogOutcome::Invalid; }
        }
        const auto& Physical = E.Marker.Get<ck::FFragment_Pmg_DebugShape>();
        auto* Mesh = Physical.Get_MeshComponent().Get();
        const auto Typed = ck::StaticCast<FCk_Handle_Pmg_DebugShape>(E.Marker);
        const auto* Outer = UCk_Utils_Pmg_DebugShape_UE::
            Get_MeshComponentOuter(World, Typed);
        if (!IsValid(Mesh) || !IsValid(Outer) || Mesh->GetOuter() != Outer ||
            !Mesh->IsRegistered() || Mesh->GetWorld() != World ||
            !Mesh->GetComponentTransform().Equals(M.Pose, 0.01) ||
            Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision ||
            Mesh->IsVisible() != !ck::diagnostic_visibility::Is_HiddenForStreamerMode() ||
            Mesh->bHiddenInGame == !ck::diagnostic_visibility::Is_HiddenForStreamerMode())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto* FillSection = Mesh->GetProcMeshSection(0);
        const auto* WireSection = Mesh->GetProcMeshSection(1);
        auto* FillMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
        auto* WireMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(1));
        FLinearColor Fill, Wire;
        auto Opaque = M.Color;
        Opaque.A = 1.0f;
        if (!FillSection || !WireSection || !IsValid(FillMID) ||
            !IsValid(WireMID) || FillMID == WireMID ||
            FillMID->GetOuter() != Mesh || WireMID->GetOuter() != Mesh ||
            !IsValid(FillMID->Parent.Get()) ||
            WireMID->Parent != FillMID->Parent ||
            !FillMID->GetVectorParameterValue(
                FHashedMaterialParameterInfo{FName(TEXT("Color"))}, Fill) ||
            !WireMID->GetVectorParameterValue(
                FHashedMaterialParameterInfo{FName(TEXT("Color"))}, Wire) ||
            !Finite(Fill) || !Finite(Wire) ||
            !Fill.Equals(ck::pmg_debug_shape::Get_FillColor(M.Color), 0.001) ||
            !Wire.Equals(Opaque, 0.001) ||
            FillSection->ProcVertexBuffer.Num() != 169 ||
            FillSection->ProcIndexBuffer.Num() != 864 ||
            WireSection->ProcVertexBuffer.Num() != 96 * 8 ||
            WireSection->ProcIndexBuffer.Num() != 96 * 36)
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        return ECkCompassGym_CatalogOutcome::Complete;
    }

    bool HasId(const TArray<FCkCompassGym_SourceRow>& Rows, int64 Id)
    {
        for (const auto& Row : Rows)
        { if (Row.CatalogId == Id) { return true; } }
        return false;
    }

    bool SafeLeaf(const FCk_Handle& Target, const FCk_Handle& WorldRoot)
    {
        if (!Live(Target)) { return false; }
        TArray<FCk_Handle> Children;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(Target, WorldRoot,
                Children) || !Children.IsEmpty())
        { return false; }
        return UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryProve_FrozenRetirementLedger({Target}, {Target}, WorldRoot);
    }

    bool RetiringOrGone(const FCk_Handle& H)
    { return !Allocated(H) || !Live(H); }

    bool InstalledPhysical(UWorld* World, const FCk_Handle& MirrorRoot,
        const FCk_Handle& WorldRoot, const FState& State)
    {
#if WITH_DEV_AUTOMATION_TESTS
        FScopedMirrorProfile Profile(State.Profile.InstalledPhysical);
#endif
        for (const auto& E : State.Installed)
        {
            if (Physical(World, E, MirrorRoot, WorldRoot, true) !=
                ECkCompassGym_CatalogOutcome::Complete)
            { return false; }
        }
        return true;
    }

    bool CommitInstalledPhysical(UWorld* World,
        const FCk_Handle& MirrorRoot, const FCk_Handle& WorldRoot,
        const FState& State, const TSet<FCk_Handle>& RemovedSet)
    {
#if WITH_DEV_AUTOMATION_TESTS
        FScopedMirrorProfile Profile(State.Profile.InstalledPhysical);
#endif
        for (const auto& E : State.Installed)
        {
            if (Physical(World, E, MirrorRoot, WorldRoot,
                    !RemovedSet.Contains(E.Host)) !=
                ECkCompassGym_CatalogOutcome::Complete)
            { return false; }
        }
        return true;
    }

    bool ValidateAllPhysical(UWorld* World, const FCk_Handle& MirrorRoot,
        const FCk_Handle& WorldRoot, const FState& State, bool& OutPending)
    {
        OutPending = false;
        if (!InstalledPhysical(World, MirrorRoot, WorldRoot, State))
        { return false; }
        for (const auto& E : State.Staged)
        {
            const auto Outcome = Physical(World, E, MirrorRoot, WorldRoot, false);
            if (Outcome == ECkCompassGym_CatalogOutcome::Invalid)
            { return false; }
            OutPending |= Outcome == ECkCompassGym_CatalogOutcome::Pending;
        }
        return true;
    }

    bool Commit(UWorld* World, const FCk_Handle& MirrorRoot,
        const FCk_Handle& WorldRoot, FState& State)
    {
#if WITH_DEV_AUTOMATION_TESTS
        FScopedMirrorProfile Profile(State.Profile.Commit);
#endif
        if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
            !CompleteCensus(MirrorRoot, WorldRoot, State))
        { return false; }
        bool Pending = false;
        if (!ValidateAllPhysical(World, MirrorRoot, WorldRoot, State, Pending) ||
            Pending)
        { return false; }
        TGuardValue<bool> MutationGuard(State.MutationInProgress, true);
        TArray<FCk_Handle> Removed;
        TSet<FCk_Handle> RemovedSet;
        TArray<FCk_Handle> Added;
        bool Mutated = false;
        const auto Restore = [&]() {
            for (const auto& H : Added)
            {
                if (BoundOpen(State, MirrorRoot, WorldRoot) &&
                    CompleteCensus(MirrorRoot, WorldRoot, State) &&
                    Live(H) && Direct(H, MirrorRoot, WorldRoot) &&
                    H.Has<ck::FTag_Poi>())
                { auto M = H; M.Try_Remove<ck::FTag_Poi>(); }
            }
            for (const auto& H : Removed)
            {
                if (BoundOpen(State, MirrorRoot, WorldRoot) &&
                    CompleteCensus(MirrorRoot, WorldRoot, State) &&
                    Live(H) && Direct(H, MirrorRoot, WorldRoot) &&
                    !H.Has<ck::FTag_Poi>())
                { auto M = H; M.Add<ck::FTag_Poi>(); }
            }
        };
        for (const auto& E : State.Installed)
        {
            if (HasId(State.PendingRows, E.Row.CatalogId)) { continue; }
            if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
                !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                !CommitInstalledPhysical(World, MirrorRoot,
                    WorldRoot, State, RemovedSet) ||
                !Live(E.Host) || !E.Host.Has<ck::FTag_Poi>())
            { Restore(); return false; }
            auto H = E.Host;
            const bool RemovedTag = H.Try_Remove<ck::FTag_Poi>();
            if (RemovedTag) { Removed.Add(E.Host); RemovedSet.Add(E.Host); }
            if (!RemovedTag || !BoundOpen(State, MirrorRoot, WorldRoot) ||
                !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                !CommitInstalledPhysical(World, MirrorRoot,
                    WorldRoot, State, RemovedSet) ||
                E.Host.Has<ck::FTag_Poi>())
            { Restore(); return false; }
            Mutated = true;
        }
        for (const auto& E : State.Staged)
        {
            if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
                !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                !CommitInstalledPhysical(World, MirrorRoot,
                    WorldRoot, State, RemovedSet) ||
                !Live(E.Host) || E.Host.Has<ck::FTag_Poi>())
            { Restore(); return false; }
            auto H = E.Host;
            H.Add<ck::FTag_Poi>();
            if (Live(E.Host) && E.Host.Has<ck::FTag_Poi>())
            { Added.Add(E.Host); }
            if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
                !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                !CommitInstalledPhysical(World, MirrorRoot,
                    WorldRoot, State, RemovedSet) ||
                !E.Host.Has<ck::FTag_Poi>())
            { Restore(); return false; }
            Mutated = true;
        }
        // The membership change is one ordered barrier; no pending or partial
        // frame is made visible to TryGet_Installed.
        if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
            !CompleteCensus(MirrorRoot, WorldRoot, State))
        { Restore(); return false; }
        TArray<FEntry> Next;
        Next.Reserve(State.PendingRows.Num());
        TArray<FEntry> Departing;
        for (const auto& E : State.Installed)
        {
            if (HasId(State.PendingRows, E.Row.CatalogId)) { Next.Add(E); }
            else { Departing.Add(E); }
        }
        Next.Append(State.Staged);
        Next.Sort([](const FEntry& A, const FEntry& B)
            { return A.Row.CatalogId < B.Row.CatalogId; });
        if (Next.Num() != State.PendingRows.Num())
        { Restore(); return false; }
        for (const auto& E : Next)
        {
            if (Physical(World, E, MirrorRoot, WorldRoot, true) !=
                ECkCompassGym_CatalogOutcome::Complete)
            { Restore(); return false; }
        }
        for (const auto& E : Departing)
        {
            if (E.Host.Has<ck::FTag_Poi>())
            { Restore(); return false; }
        }
        FState Prospective = State;
        Prospective.Retiring.Append(Departing);
        Prospective.Installed = Next;
        Prospective.Staged.Reset();
        if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
            !CompleteCensus(MirrorRoot, WorldRoot, Prospective))
        { Restore(); return false; }
        State.Retiring.Append(Departing);
        State.Installed = MoveTemp(Next);
        State.Staged.Reset();
        for (const auto& Row : State.PendingRows)
        {
            if (!HasId(State.HistoryRows, Row.CatalogId))
            { State.HistoryRows.Add(Row); }
        }
        State.InstalledRows = State.PendingRows;
        State.InstalledFrame = State.PendingFrame;
        State.PendingRows.Reset();
        State.PendingFrame = {};
        State.HasInstalled = true;
        State.Phase = State.Retiring.IsEmpty() ? EPhase::Idle :
            EPhase::Retire;
        State.RetirementDeadlineSeconds = State.Retiring.IsEmpty() ? 0.0 :
            FPlatformTime::Seconds() + 60.0;
        (void)Mutated;
        return true;
    }

    bool RetireBatch(const FCk_Handle& MirrorRoot,
        const FCk_Handle& WorldRoot, FState& State)
    {
#if WITH_DEV_AUTOMATION_TESTS
        FScopedMirrorProfile Profile(State.Profile.RetireBatch);
#endif
        if (State.Phase != EPhase::Retire || State.Retiring.IsEmpty() ||
            !BoundOpen(State, MirrorRoot, WorldRoot) ||
            !CompleteCensus(MirrorRoot, WorldRoot, State))
        { return false; }
        int32 Requested = 0;
        for (int32 I = 0; I < State.Retiring.Num(); ++I)
        {
            auto& Entry = State.Retiring[I];
            if (Entry.RetirePhase ==
                ck::ECompassGym_MirrorLeafPhase::WaitMarker)
            {
                if (Allocated(Entry.Marker)) { continue; }
                Entry.RetirePhase = ck::ECompassGym_MirrorLeafPhase::Host;
            }
            if (Entry.RetirePhase ==
                ck::ECompassGym_MirrorLeafPhase::WaitHost)
            {
                if (Allocated(Entry.Host)) { continue; }
                State.Retiring.RemoveAt(I--);
                if (!CompleteCensus(MirrorRoot, WorldRoot, State))
                { return false; }
                continue;
            }
            if (Entry.RetirePhase ==
                ck::ECompassGym_MirrorLeafPhase::Marker &&
                !Entry.Row.HasMarker)
            { Entry.RetirePhase = ck::ECompassGym_MirrorLeafPhase::Host; }
            if (Requested == 8) { continue; }
            bool PendingPhysical = false;
            if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
                !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                !ValidateAllPhysical(State.World.Get(), MirrorRoot,
                    WorldRoot, State, PendingPhysical) || PendingPhysical)
            { return false; }
            if (Entry.RetirePhase ==
                ck::ECompassGym_MirrorLeafPhase::Marker)
            {
                const auto Marker = Entry.Marker;
                const auto Host = Entry.Host;
                if (!Live(Marker) || !Direct(Marker, Host, WorldRoot) ||
                    !SafeLeaf(Marker, WorldRoot))
                { return false; }
                auto Mutable = Marker;
                {
                    TGuardValue<bool> MutationGuard(State.MutationInProgress,
                        true);
                    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Mutable);
                }
                ++Requested;
                if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
                    !RetiringOrGone(Marker) ||
                    !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                    !ValidateAllPhysical(State.World.Get(), MirrorRoot,
                        WorldRoot, State, PendingPhysical) || PendingPhysical)
                { return false; }
                Entry.RetirePhase =
                    ck::ECompassGym_MirrorLeafPhase::WaitMarker;
            }
            else if (Entry.RetirePhase ==
                ck::ECompassGym_MirrorLeafPhase::Host)
            {
                const auto Host = Entry.Host;
                if ((Entry.Row.HasMarker && Allocated(Entry.Marker)) ||
                    !Live(Host) || !Direct(Host, MirrorRoot, WorldRoot) ||
                    Host.Has<ck::FTag_Poi>() || !SafeLeaf(Host, WorldRoot))
                { return false; }
                auto Mutable = Host;
                {
                    TGuardValue<bool> MutationGuard(State.MutationInProgress,
                        true);
                    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Mutable);
                }
                ++Requested;
                if (!BoundOpen(State, MirrorRoot, WorldRoot) ||
                    !RetiringOrGone(Host) ||
                    !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                    !ValidateAllPhysical(State.World.Get(), MirrorRoot,
                        WorldRoot, State, PendingPhysical) || PendingPhysical)
                { return false; }
                Entry.RetirePhase = ck::ECompassGym_MirrorLeafPhase::WaitHost;
            }
            else { return false; }
        }
        if (State.Retiring.IsEmpty())
        {
            State.Phase = EPhase::Idle;
            State.RetirementDeadlineSeconds = 0.0;
        }
        return true;
    }
}

void ck::FProcessor_CompassGym_Mirror::ForEachEntity(
    TimeType DeltaT, HandleType MirrorRoot, FFragment_CompassGym_Mirror& State)
{
    using namespace ck_compass_mirror;
    (void)DeltaT;
    if (State.Phase == EPhase::Failed) { return; }
    FCk_Handle WorldRoot;
    auto* World = State.World.Get();
    if (State.Phase == EPhase::Idle)
    {
        if (!ck_compass_mirror::Context(World, State.GameState.Get(), MirrorRoot, WorldRoot) ||
            !Open(WorldRoot) || State.ClosedUnproven ||
            !CompleteCensus(MirrorRoot, WorldRoot, State) ||
            !InstalledPhysical(World, MirrorRoot, WorldRoot, State))
        { Fail(State, MirrorRoot, WorldRoot,
            TEXT("Installed mirror physical or lifetime proof lost")); }
        return;
    }
    const double Deadline = State.Phase == EPhase::Retire ?
        State.RetirementDeadlineSeconds : State.DeadlineSeconds;
    if (!ck_compass_mirror::Context(World, State.GameState.Get(), MirrorRoot, WorldRoot) ||
        !Open(WorldRoot) || State.ClosedUnproven || State.MutationInProgress ||
        FPlatformTime::Seconds() > Deadline)
    { Fail(State, MirrorRoot, WorldRoot, TEXT("Mirror context or deadline lost")); return; }
    if (!CompleteCensus(MirrorRoot, WorldRoot, State) ||
        !InstalledPhysical(World, MirrorRoot, WorldRoot, State))
    { Fail(State, MirrorRoot, WorldRoot, TEXT("Mirror lifetime ledger unproven")); return; }
    if (State.Phase == EPhase::Stage)
    {
#if WITH_DEV_AUTOMATION_TESTS
        FScopedMirrorProfile Profile(State.Profile.StageLoop);
#endif
        int32 Budget = 16;
        while (State.StageCursor < State.PendingRows.Num() && Budget-- > 0)
        {
            const auto& Row = State.PendingRows[State.StageCursor++];
            if (Find(State.Installed, Row.CatalogId)) { continue; }
            if (!StageRow(Row, MirrorRoot, WorldRoot, State) ||
                !BoundOpen(State, MirrorRoot, WorldRoot) ||
                !CompleteCensus(MirrorRoot, WorldRoot, State) ||
                !InstalledPhysical(World, MirrorRoot, WorldRoot, State))
            { Fail(State, MirrorRoot, WorldRoot, TEXT("Mirror staging failed")); return; }
        }
        if (State.StageCursor == State.PendingRows.Num())
        { State.Phase = EPhase::Materialize; }
    }
    if (State.Phase == EPhase::Materialize)
    {
        bool Pending = false;
        if (!ValidateAllPhysical(World, MirrorRoot, WorldRoot, State, Pending))
        { Fail(State, MirrorRoot, WorldRoot, TEXT("Mirror materialization invalid")); return; }
        if (!Pending) { State.Phase = EPhase::Commit; }
    }
    if (State.Phase == EPhase::Commit)
    {
        if (!Commit(World, MirrorRoot, WorldRoot, State))
        { Fail(State, MirrorRoot, WorldRoot, TEXT("Mirror frame commit unproven")); return; }
    }
    if (State.Phase == EPhase::Retire)
    {
        if (!RetireBatch(MirrorRoot, WorldRoot, State))
        { Fail(State, MirrorRoot, WorldRoot, TEXT("Mirror leaf retirement unproven")); }
    }
}

bool UCkCompassGym_Mirror::TryQueue_Snapshot(UWorld* World,
    AGameStateBase* ExactGameState, const FCk_Handle& MirrorRoot,
    const FCkCompassGym_WireSnapshot& Snapshot, FString& OutFailureReason)
{
    using namespace ck_compass_mirror;
    OutFailureReason.Reset();
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, MirrorRoot, WorldRoot) ||
        !Open(WorldRoot))
    { OutFailureReason = TEXT("Exact live client world unavailable"); return false; }
    TArray<FCkCompassGym_SourceRow> Expanded;
    const auto Registry = WorldRoot.Get_RegistryView();
    const auto RegistryHandle = Registry.Get_RegistryHandle();
    bool Conflicting = false;
    Registry.View<ck::FFragment_CompassGym_Mirror>().ForEach(
        [&](FCk_Entity Entity, const ck::FFragment_CompassGym_Mirror& Other) {
            (void)Other;
            const FCk_Handle OtherRoot{Entity, RegistryHandle};
            if (Allocated(OtherRoot) && OtherRoot != MirrorRoot)
            { Conflicting = true; }
        });
    if (Conflicting)
    {
        OutFailureReason = TEXT("Client world already has a mirror root");
        return false;
    }
    if (MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
    {
        const auto& Existing = MirrorRoot.Get<ck::FFragment_CompassGym_Mirror>();
        if (Existing.World.Get() != World ||
            Existing.GameState.Get() != ExactGameState ||
            Existing.WorldRoot != WorldRoot ||
            Existing.MutationInProgress || Existing.ClosedUnproven ||
            Existing.Phase == EPhase::Failed)
        {
            OutFailureReason = TEXT("Mirror binding or mutation unavailable");
            return false;
        }
    }
    if (!UCkCompassGym_Wire::TryValidateAndExpand(Snapshot, Expanded) ||
        Snapshot.Lifecycle == ECkCompassGym_WireLifecycle::Failed)
    {
        auto Mutable = MirrorRoot;
        auto& State = Mutable.AddOrGet<ck::FFragment_CompassGym_Mirror>();
        State.World = World;
        State.GameState = ExactGameState;
        State.WorldRoot = WorldRoot;
        Fail(State, MirrorRoot, WorldRoot,
            TEXT("Malformed or failed Compass wire"));
        OutFailureReason = State.Failure;
        return false;
    }
    if (MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
    {
        auto Mutable = MirrorRoot;
        auto& State = Mutable.Get<ck::FFragment_CompassGym_Mirror>();
        if (State.World.Get() != World ||
            State.GameState.Get() != ExactGameState ||
            State.WorldRoot != WorldRoot ||
            State.ClosedUnproven || State.Phase == EPhase::Failed)
        { OutFailureReason = TEXT("Mirror root bound elsewhere or failed"); return false; }
        if (State.MutationInProgress)
        { OutFailureReason = TEXT("Mirror mutation in progress"); return false; }
        if (!CompleteCensus(MirrorRoot, WorldRoot, State) ||
            !InstalledPhysical(World, MirrorRoot, WorldRoot, State))
        {
            Fail(State, MirrorRoot, WorldRoot, TEXT("Mirror ledger changed"));
            OutFailureReason = State.Failure;
            return false;
        }
        if (State.Phase != EPhase::Idle)
        {
            const bool HasPendingFrame = State.Phase == EPhase::Stage ||
                State.Phase == EPhase::Materialize ||
                State.Phase == EPhase::Commit;
            if (HasPendingFrame && UCkCompassGym_Wire::Get_IsSameFrame(
                    Snapshot, State.PendingFrame))
            { return true; }
            if (HasPendingFrame && Snapshot.SourceGeneration ==
                    State.PendingFrame.SourceGeneration &&
                Snapshot.Sequence == State.PendingFrame.Sequence)
            {
                Fail(State, MirrorRoot, WorldRoot,
                    TEXT("Same pending identity changed content"));
                OutFailureReason = State.Failure;
                return false;
            }
            OutFailureReason = TEXT("Mirror frame pending; retry after install");
            return false;
        }
        if (State.HasInstalled)
        {
            if (UCkCompassGym_Wire::Get_IsSameFrame(
                    Snapshot, State.InstalledFrame))
            { return true; }
            if (!UCkCompassGym_Wire::Get_IsAdvance(
                    State.InstalledFrame, Snapshot))
            {
                Fail(State, MirrorRoot, WorldRoot,
                    TEXT("Mirror wire identity stale or changed"));
                OutFailureReason = State.Failure;
                return false;
            }
            for (const auto& Old : State.HistoryRows)
            {
                for (const auto& New : Expanded)
                {
                    if (Old.CatalogId == New.CatalogId &&
                        !SameRow(Old, New))
                    {
                        Fail(State, MirrorRoot, WorldRoot,
                            TEXT("Retained CatalogId changed canonical value"));
                        OutFailureReason = State.Failure;
                        return false;
                    }
                }
            }
        }
        State.PendingFrame = Snapshot;
        State.PendingRows = MoveTemp(Expanded);
        State.StageCursor = 0;
        State.DeadlineSeconds = FPlatformTime::Seconds() +
            PendingTimeoutSeconds;
        State.Phase = EPhase::Stage;
        return true;
    }
    TArray<FCk_Handle> Children;
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::
        TryGet_AllocatedDirectLifetimeDependents(MirrorRoot, WorldRoot,
            Children) || !Children.IsEmpty())
    {
        OutFailureReason = TEXT("Fresh mirror root has unknown child");
        return false;
    }
    auto Mutable = MirrorRoot;
    auto& State = Mutable.AddOrGet<ck::FFragment_CompassGym_Mirror>();
    State.World = World;
    State.GameState = ExactGameState;
    State.WorldRoot = WorldRoot;
    State.PendingFrame = Snapshot;
    State.PendingRows = MoveTemp(Expanded);
    State.DeadlineSeconds = FPlatformTime::Seconds() + PendingTimeoutSeconds;
    State.Phase = EPhase::Stage;
    return true;
}

bool UCkCompassGym_Mirror::TryGet_Installed(UWorld* World,
    AGameStateBase* ExactGameState, const FCk_Handle& MirrorRoot,
    int64& OutGeneration, int64& OutSequence,
    TArray<FCkCompassGym_SourceRow>& OutRows, FString& OutFailureReason)
{
    using namespace ck_compass_mirror;
    OutGeneration = 0;
    OutSequence = 0;
    OutRows.Reset();
    OutFailureReason.Reset();
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, MirrorRoot, WorldRoot) ||
        !Open(WorldRoot) ||
        !MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
    { OutFailureReason = TEXT("Exact client mirror unavailable"); return false; }
    const auto& State = MirrorRoot.Get<ck::FFragment_CompassGym_Mirror>();
    if (State.World.Get() != World ||
        State.GameState.Get() != ExactGameState ||
        State.WorldRoot != WorldRoot ||
        State.ClosedUnproven || State.Phase == EPhase::Failed ||
        State.MutationInProgress || !State.HasInstalled)
    { OutFailureReason = TEXT("Installed mirror frame unavailable"); return false; }
    bool InstalledPhysical = true;
    for (const auto& E : State.Installed)
    {
        if (Physical(World, E, MirrorRoot, WorldRoot, true) !=
            ECkCompassGym_CatalogOutcome::Complete)
        { InstalledPhysical = false; break; }
    }
    if (!CompleteCensus(MirrorRoot, WorldRoot, State) || !InstalledPhysical)
    { OutFailureReason = TEXT("Installed mirror frame unproven"); return false; }
    OutGeneration = State.InstalledFrame.SourceGeneration;
    OutSequence = State.InstalledFrame.Sequence;
    OutRows = State.InstalledRows;
    return true;
}

bool UCkCompassGym_Mirror::TryGet_Status(UWorld* World,
    AGameStateBase* ExactGameState, const FCk_Handle& MirrorRoot,
    bool& OutBusy, bool& OutFailed, FString& OutFailureReason)
{
    using namespace ck_compass_mirror;
    OutBusy = false;
    OutFailed = false;
    OutFailureReason.Reset();
    FCk_Handle WorldRoot;
    if (!Context(World, ExactGameState, MirrorRoot, WorldRoot) ||
        !MirrorRoot.Has<ck::FFragment_CompassGym_Mirror>())
    { OutFailureReason = TEXT("Exact captured client mirror unavailable"); return false; }
    const auto& State = MirrorRoot.Get<ck::FFragment_CompassGym_Mirror>();
    if (State.World.Get() != World ||
        State.GameState.Get() != ExactGameState ||
        State.WorldRoot != WorldRoot)
    { OutFailureReason = TEXT("Mirror bound to another exact tuple"); return false; }
    bool AdmissionClosed = true;
    if (!UCkGym_AdmissionRuntime_UE::TryGet_IsClosedUnproven(
            WorldRoot, AdmissionClosed))
    { OutFailureReason = TEXT("World admission status unavailable"); return false; }
    OutFailed = State.ClosedUnproven || State.Phase == EPhase::Failed ||
        AdmissionClosed ||
        UCk_Utils_Compass_UE::Get_IsWorldProjectionClosed(WorldRoot);
    OutBusy = !OutFailed && State.Phase != EPhase::Idle;
    if (OutFailed) { OutFailureReason = State.Failure.IsEmpty() ?
        TEXT("Client mirror or world projection terminally closed") :
        State.Failure; }
    return true;
}

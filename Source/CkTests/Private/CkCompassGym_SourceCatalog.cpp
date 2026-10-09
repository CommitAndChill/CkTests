#include "CkCompassGym_SourceCatalog.h"
#include "CkCueGym_OutlineRecipe.h"

#include "CkEntityScriptGym_Runtime.h"
#include "CkCore/Diagnostics/CkDiagnosticVisibility.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkPoi/CkPoi_Fragment.h"
#include "CkPoi/CkPoi_Utils.h"
#include "CkPoiDisplayDefinition/CkPoiDisplayDefinition_Fragment.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "CkPmg/CkPmg_Fragment_BasicShapes.h"
#include "CkPmg/CkPmg_Utils_DebugShapes.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "CkVisibleRange/CkVisibleRange_Fragment.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace ck_compass_catalog
{
    auto Allocated(const FCk_Handle& H) -> bool
    { return UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H); }

    auto Live(const FCk_Handle& H) -> bool
    {
        return Allocated(H) && ck::IsValid(H) &&
            !H.Has_Any<ck::FTag_DestroyEntity_Initiate,
                ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    auto SameWorld(const FCk_Handle& H, const FCk_Handle& WorldRoot) -> bool
    {
        return Allocated(H) && Allocated(WorldRoot) &&
            H.Get_RegistryView().Get_RegistryHandle() ==
                WorldRoot.Get_RegistryView().Get_RegistryHandle();
    }

    auto Direct(const FCk_Handle& Child, const FCk_Handle& Parent,
        const FCk_Handle& WorldRoot) -> bool
    {
        return SameWorld(Child, WorldRoot) && SameWorld(Parent, WorldRoot) &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == Parent;
    }

    auto Children(const FCk_Handle& Parent, const FCk_Handle& WorldRoot,
        TArray<FCk_Handle>& Out) -> bool
    {
        Out.Reset();
        return UCk_EntityScriptGym_Spawn_Runtime_UE::
            TryGet_AllocatedDirectLifetimeDependents(Parent, WorldRoot, Out);
    }

    // Reverse lifetime-dependents alone can miss an allocated child whose
    // current owner points at Parent but whose reverse entry was lost.
    auto ProveOwnerCensus(const FCk_Handle& SourceRoot,
        const FCk_Handle& WorldRoot, const TArray<FCk_Handle>& ReverseHosts,
        const TArray<FCkCompassGym_SourceReceipt>& ActiveReceipts,
        const TArray<FCk_Handle>& InactiveParents) -> bool
    {
        TArray<FCk_Handle> ActualRootChildren;
        bool InactiveHasChild = false;
        bool ActiveLineageInvalid = false;
        const auto Registry = WorldRoot.Get_RegistryView();
        const auto RegistryHandle = Registry.Get_RegistryHandle();
        Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Owner)
            {
                const FCk_Handle Child{Entity, RegistryHandle};
                if (!Allocated(Child)) { return; }
                const auto Parent = Owner.Get_Entity();
                if (Parent == SourceRoot) { ActualRootChildren.AddUnique(Child); }
                if (InactiveParents.Contains(Parent)) { InactiveHasChild = true; }
                for (const auto& R : ActiveReceipts)
                {
                    if (Parent == R.Host && Child != R.Marker && Child != R.Timer)
                    { ActiveLineageInvalid = true; }
                    if ((R.Marker != FCk_Handle{} && Parent == R.Marker) ||
                        (R.Timer != FCk_Handle{} && Parent == R.Timer))
                    { ActiveLineageInvalid = true; }
                }
            });
        if (InactiveHasChild || ActiveLineageInvalid ||
            ActualRootChildren.Num() != ReverseHosts.Num())
        { return false; }
        for (const auto& Child : ActualRootChildren)
        { if (!ReverseHosts.Contains(Child)) { return false; } }
        return true;
    }

    auto Finite(const FLinearColor& C) -> bool
    {
        return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) &&
            FMath::IsFinite(C.B) && FMath::IsFinite(C.A);
    }

    auto MarkerKind(ECkCompassGym_SourceKind Kind) -> bool
    { return Kind != ECkCompassGym_SourceKind::Stress; }

    auto KnownKind(ECkCompassGym_SourceKind Kind) -> bool
    {
        return Kind == ECkCompassGym_SourceKind::Ring ||
            Kind == ECkCompassGym_SourceKind::Waypoint ||
            Kind == ECkCompassGym_SourceKind::Stress ||
            Kind == ECkCompassGym_SourceKind::Ping;
    }

    auto ExpectedRadius(ECkCompassGym_SourceKind Kind) -> float
    {
        if (Kind == ECkCompassGym_SourceKind::Ring) { return 40.0f; }
        if (Kind == ECkCompassGym_SourceKind::Waypoint) { return 55.0f; }
        if (Kind == ECkCompassGym_SourceKind::Ping) { return 45.0f; }
        return 0.0f;
    }

    auto ExpectedColor(ECkCompassGym_SourceKind Kind,
        const FGameplayTag& Category, FLinearColor& Out) -> bool
    {
        const auto Name = Category.GetTagName();
        if (Kind == ECkCompassGym_SourceKind::Waypoint &&
            Name == TEXT("Poi.Category.Waypoint"))
        { Out = FLinearColor(0.7f, 0.35f, 0.95f); return true; }
        if (Kind == ECkCompassGym_SourceKind::Ping &&
            Name == TEXT("Poi.Category.Ping"))
        { Out = FLinearColor(0.3f, 0.9f, 0.95f); return true; }
        if (Kind != ECkCompassGym_SourceKind::Ring) { return false; }
        if (Name == TEXT("Poi.Category.Quest"))
        { Out = FLinearColor(1.0f, 0.78f, 0.25f); return true; }
        if (Name == TEXT("Poi.Category.Shop"))
        { Out = FLinearColor(0.95f, 0.5f, 0.2f); return true; }
        if (Name == TEXT("Poi.Category.Danger"))
        { Out = FLinearColor(0.9f, 0.25f, 0.25f); return true; }
        if (Name == TEXT("Poi.Category.Info"))
        { Out = FLinearColor(0.35f, 0.6f, 0.95f); return true; }
        return false;
    }

    // Physical proof is intentionally narrower than generic PMG: this gym
    // authors a persistent, 12x12, filled+outlined sphere at host+Z60.
    auto ReadMarker(UWorld* World, const FCk_Handle& Host,
        const FCk_Handle& Marker, const FCk_Handle& WorldRoot,
        ECkCompassGym_SourceKind Kind, const FGameplayTag& Category,
        const FVector* HostLocation, bool CompositionComplete,
        FCkCompassGym_SourceMarker& Out) ->
        ECkCompassGym_CatalogOutcome
    {
        if (!Live(Marker) || !Direct(Marker, Host, WorldRoot) ||
            Marker.Has_Any<ck::FTag_Pmg_DebugShape_Composite,
                ck::FTag_Pmg_DebugShape_LineSet>() ||
            Marker.Has<ck::FFragment_Pmg_Box_Params>())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        if (Marker.Has<ck::FFragment_Pmg_DebugShape_Lines>())
        {
            FLinearColor Expected;
            if (!ExpectedColor(Kind, Category, Expected))
            { return ECkCompassGym_CatalogOutcome::Invalid; }
            Expected.A = 1.0f;
            for (const auto& Line : Marker.Get<
                ck::FFragment_Pmg_DebugShape_Lines>().Get_Lines())
            {
                if (Line._Start.ContainsNaN() || Line._End.ContainsNaN() ||
                    !Finite(Line._Color) ||
                    !Line._Color.Equals(Expected, 0.001f) ||
                    !FMath::IsFinite(Line._Thickness) ||
                    Line._Thickness != 2.0f)
                { return ECkCompassGym_CatalogOutcome::Invalid; }
            }
        }
        const bool NeedsSetup = Marker.Has<ck::FTag_Pmg_DebugShape_NeedsSetup>();
        if (Marker.Has<ck::FFragment_Pmg_Sphere_Params>())
        {
            const auto& Known = Marker.Get<ck::FFragment_Pmg_Sphere_Params>();
            if (Known.Get_Radius() != ExpectedRadius(Kind) ||
                Known.Get_Segments() != 12 || Known.Get_Rings() != 12 ||
                Known.Get_Axis() != ECk_Plane_Axis::XY)
            { return ECkCompassGym_CatalogOutcome::Invalid; }
        }
        if (Marker.Has<ck::FFragment_Pmg_DebugShape_Common>())
        {
            const auto& Known = Marker.Get<ck::FFragment_Pmg_DebugShape_Common>();
            FLinearColor Expected;
            const auto Duration = Known.Get_Duration().Get_Seconds();
            if (!ExpectedColor(Kind, Category, Expected) ||
                !Finite(Known.Get_Color()) ||
                !Known.Get_Color().Equals(Expected, 0.0001f) ||
                !Known.Get_DrawLines() || Known.Get_LineThickness() != 2.0f ||
                !FMath::IsFinite(Duration) || Duration != -1.0 ||
                !Marker.Has<ck::FTag_Pmg_DebugShape_PersistentDuration>() ||
                Known.Get_EnableCollision() ||
                Known.Get_RenderMode() != ECk_Pmg_RenderMode::DoubleSided)
            { return ECkCompassGym_CatalogOutcome::Invalid; }
        }
        if (Marker.Has<ck::FFragment_Transform>())
        {
            const auto Known = Marker.Get<ck::FFragment_Transform>().Get_Transform();
            if (!Known.IsValid() || Known.ContainsNaN() ||
                (HostLocation != nullptr && !Known.GetLocation().Equals(
                    *HostLocation + FVector(0, 0, 60), 0.01f)) ||
                !Known.GetRotation().Equals(FQuat::Identity, 0.0001f) ||
                !Known.GetScale3D().Equals(FVector::OneVector, 0.0001f))
            { return ECkCompassGym_CatalogOutcome::Invalid; }
        }
        if (!Marker.Has_All<ck::FFragment_Pmg_Sphere_Params,
                ck::FFragment_Pmg_DebugShape_Common, ck::FFragment_Transform>())
        { return CompositionComplete ? ECkCompassGym_CatalogOutcome::Invalid :
            ECkCompassGym_CatalogOutcome::Pending; }
        const auto& Params = Marker.Get<ck::FFragment_Pmg_Sphere_Params>();
        const auto& Common = Marker.Get<ck::FFragment_Pmg_DebugShape_Common>();
        const auto SourcePose = Marker.Get<ck::FFragment_Transform>().Get_Transform();
        if (!SourcePose.IsValid() || SourcePose.ContainsNaN() ||
            (HostLocation != nullptr && !SourcePose.GetLocation().Equals(
                *HostLocation + FVector(0, 0, 60), 0.01f)) ||
            !SourcePose.GetRotation().Equals(FQuat::Identity, 0.0001f) ||
            !SourcePose.GetScale3D().Equals(FVector::OneVector, 0.0001f))
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        FLinearColor AuthoredColor;
        const auto Color = Common.Get_Color();
        const auto Duration = Common.Get_Duration().Get_Seconds();
        const auto Mode = Common.Get_RenderMode();
        if (!ExpectedColor(Kind, Category, AuthoredColor) ||
            !Finite(Color) || !Color.Equals(AuthoredColor, 0.0001f) ||
            Params.Get_Radius() != ExpectedRadius(Kind) ||
            Params.Get_Segments() != 12 || Params.Get_Rings() != 12 ||
            Params.Get_Axis() != ECk_Plane_Axis::XY ||
            !Common.Get_DrawLines() || Common.Get_LineThickness() != 2.0f ||
            !FMath::IsFinite(Duration) || Duration != -1.0 ||
            !Marker.Has<ck::FTag_Pmg_DebugShape_PersistentDuration>() ||
            Common.Get_EnableCollision() ||
            Mode != ECk_Pmg_RenderMode::DoubleSided)
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        if (NeedsSetup) { return ECkCompassGym_CatalogOutcome::Pending; }
        if (!Marker.Has<ck::FFragment_Pmg_DebugShape>())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto& Physical = Marker.Get<ck::FFragment_Pmg_DebugShape>();
        auto* Mesh = Physical.Get_MeshComponent().Get();
        const auto Typed = ck::StaticCast<FCk_Handle_Pmg_DebugShape>(Marker);
        const auto* ExpectedOuter =
            UCk_Utils_Pmg_DebugShape_UE::Get_MeshComponentOuter(World, Typed);
        if (!IsValid(Mesh) || !IsValid(ExpectedOuter) ||
            Mesh->GetOuter() != ExpectedOuter || !Mesh->IsRegistered() ||
            Mesh->GetWorld() != World || !Mesh->GetProcMeshSection(0))
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto Pose = Mesh->GetComponentTransform();
        if (!Pose.IsValid() || Pose.ContainsNaN() ||
            !Pose.Equals(SourcePose, 0.01f))
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const bool Visible = !ck::diagnostic_visibility::Is_HiddenForStreamerMode();
        if (Mesh->IsVisible() != Visible || Mesh->bHiddenInGame == Visible ||
            Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        auto* FillMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
        FLinearColor Fill;
        const auto* FillSection = Mesh->GetProcMeshSection(0);
        if (!IsValid(FillMID) || FillMID->GetOuter() != Mesh ||
            !IsValid(FillMID->Parent.Get()) ||
            !FillMID->GetVectorParameterValue(
                FHashedMaterialParameterInfo{FName(TEXT("Color"))}, Fill) ||
            !Finite(Fill) || !Fill.Equals(ck::pmg_debug_shape::Get_FillColor(Color), 0.001f) ||
            FillSection->ProcVertexBuffer.Num() != 169 ||
            FillSection->ProcIndexBuffer.Num() != 864)
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        if (Marker.Has<ck::FTag_Pmg_DebugShape_LinesNeedBaking>())
        {
            if (Marker.Has<ck::FFragment_Pmg_DebugShape_Lines>())
            {
                for (const auto& Line : Marker.Get<
                    ck::FFragment_Pmg_DebugShape_Lines>().Get_Lines())
                {
                    if (Line._Start.ContainsNaN() || Line._End.ContainsNaN() ||
                        !Finite(Line._Color) || !FMath::IsFinite(Line._Thickness) ||
                        Line._Thickness <= 0.0f)
                    { return ECkCompassGym_CatalogOutcome::Invalid; }
                }
            }
            return ECkCompassGym_CatalogOutcome::Pending;
        }
        if (!Marker.Has<ck::FFragment_Pmg_DebugShape_Lines>())
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        const auto& Lines = Marker.Get<ck::FFragment_Pmg_DebugShape_Lines>().Get_Lines();
        const auto* WireSection = Mesh->GetProcMeshSection(1);
        auto* WireMID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(1));
        FLinearColor WireColor;
        auto OpaqueColor = Color;
        OpaqueColor.A = 1.0f;
        if (Lines.Num() != 96 || !WireSection || !IsValid(WireMID) ||
            WireMID->GetOuter() != Mesh || WireMID == FillMID ||
            WireMID->Parent != FillMID->Parent ||
            !WireMID->GetVectorParameterValue(
                FHashedMaterialParameterInfo{FName(TEXT("Color"))}, WireColor) ||
            !Finite(WireColor) || !WireColor.Equals(OpaqueColor, 0.001f) ||
            WireSection->ProcVertexBuffer.Num() != 96 * 8 ||
            WireSection->ProcIndexBuffer.Num() != 96 * 36)
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        Out.Pose = Pose;
        Out.Color = Color;
        Out.Radius = Params.Get_Radius();
        Out.Segments = Params.Get_Segments();
        Out.Rings = Params.Get_Rings();
        Out.Axis = Params.Get_Axis();
        Out.DrawLines = Common.Get_DrawLines();
        Out.LineThickness = Common.Get_LineThickness();
        Out.Duration = Duration;
        Out.RenderMode = Mode;
        Out.VisibleByRenderMode = true;
        Out.EnableCollision = Common.Get_EnableCollision();
        Out.Lines.Reserve(Lines.Num());
        for (const auto& Line : Lines)
        {
            if (Line._Start.ContainsNaN() || Line._End.ContainsNaN() ||
                (Line._End - Line._Start).IsNearlyZero() ||
                !Finite(Line._Color) || !FMath::IsFinite(Line._Thickness) ||
                Line._Thickness <= 0.0f ||
                !Line._Color.Equals(OpaqueColor, 0.001f) ||
                Line._Thickness != Common.Get_LineThickness())
            { return ECkCompassGym_CatalogOutcome::Invalid; }
            FCkCompassGym_SourceLine Value;
            Value.Start = Line._Start;
            Value.End = Line._End;
            Value.Color = Line._Color;
            Value.Thickness = Line._Thickness;
            Out.Lines.Add(Value);
        }
        FCkCueGym_SourceVisualRow Recipe;
        Recipe.Kind = ECkCueGym_SourceVisualKind::Sphere;
        Recipe.Pose = Out.Pose;
        Recipe.Color = Out.Color;
        Recipe.Radius = Out.Radius;
        Recipe.Segments = Out.Segments;
        Recipe.Rings = Out.Rings;
        Recipe.Axis = Out.Axis;
        Recipe.DrawLines = Out.DrawLines;
        Recipe.LineThickness = Out.LineThickness;
        TArray<FCkCueGym_SourceVisualLine> Actual;
        Actual.Reserve(Out.Lines.Num());
        for (const auto& Line : Out.Lines)
        {
            FCkCueGym_SourceVisualLine Copy;
            Copy.Start = Line.Start;
            Copy.End = Line.End;
            Copy.Color = Line.Color;
            Copy.Thickness = Line.Thickness;
            Actual.Add(Copy);
        }
        if (!ck_cue_outline_recipe::Get_MatchesActualLines(Recipe, Actual))
        { return ECkCompassGym_CatalogOutcome::Invalid; }
        return ECkCompassGym_CatalogOutcome::Complete;
    }
}

static ECkCompassGym_CatalogOutcome TryGet_SourceCatalogInternal(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    bool CompositionComplete, bool PhaseAware,
    const TArray<FCkCompassGym_SourceReceipt>& Receipts,
    const TArray<FCkCompassGym_SourceReceipt>& StagedStressReceipts,
    const TArray<FCkCompassGym_SourceReceipt>& FrozenRetiringStressReceipts,
    TArray<FCkCompassGym_SourceRow>& OutRows,
    TArray<FCkCompassGym_SourceRow>& OutStagedRows,
    FString& OutFailureReason)
{
    using namespace ck_compass_catalog;
    OutRows.Reset();
    OutStagedRows.Reset();
    OutFailureReason.Reset();
    auto Invalid = [&OutFailureReason](const TCHAR* Why) {
        OutFailureReason = Why;
        return ECkCompassGym_CatalogOutcome::Invalid;
    };
    if (PhaseAware && &OutRows == &OutStagedRows)
    { return Invalid(TEXT("Active and staged outputs alias")); }
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() ||
        World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        World->bIsTearingDown || World->IsBeingCleanedUp() ||
        World->IsNetMode(NM_Client) || !IsValid(ExactGameState) ||
        ExactGameState->IsActorBeingDestroyed() ||
        World->GetGameState() != ExactGameState ||
        ExactGameState->GetWorld() != World)
    { return Invalid(TEXT("Authority GameState/world preflight failed")); }
    const auto WorldRoot =
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!Live(WorldRoot) || !Live(SourceRoot) ||
        !Direct(SourceRoot, WorldRoot, WorldRoot))
    { return Invalid(TEXT("Source root not a live direct world child")); }
    TArray<FCk_Handle> Hosts;
    if (!Children(SourceRoot, WorldRoot, Hosts))
    { return Invalid(TEXT("Source root lifetime census failed")); }
    bool InactivePending = false;
    TArray<FCkCompassGym_SourceRow> StagedRows;
    TArray<FCk_Handle> InactiveParentHandles;
    if (!PhaseAware)
    {
        if (Hosts.Num() != Receipts.Num())
        { return Invalid(TEXT("Unclaimed or missing source host")); }
    }
    else
    {
        if ((!StagedStressReceipts.IsEmpty() && StagedStressReceipts.Num() != 500) ||
            (!FrozenRetiringStressReceipts.IsEmpty() &&
             FrozenRetiringStressReceipts.Num() != 500) ||
            (!StagedStressReceipts.IsEmpty() &&
             !FrozenRetiringStressReceipts.IsEmpty()))
        { return Invalid(TEXT("Inactive stress ledger must be empty or 500")); }
        if (!StagedStressReceipts.IsEmpty() || !FrozenRetiringStressReceipts.IsEmpty())
        {
            for (const auto& R : Receipts)
            {
                if (R.Kind == ECkCompassGym_SourceKind::Stress)
                { return Invalid(TEXT("Active stress overlaps inactive stress phase")); }
            }
        }
        TArray<int64> AllIds;
        TArray<FCk_Handle> AllHandles;
        for (const auto& R : Receipts)
        {
            if (R.CatalogId <= 0 || R.Host == FCk_Handle{} ||
                AllIds.Contains(R.CatalogId) || AllHandles.Contains(R.Host))
            { return Invalid(TEXT("Active receipt identity invalid")); }
            AllIds.Add(R.CatalogId);
            AllHandles.Add(R.Host);
        }
        StagedRows.Reserve(StagedStressReceipts.Num());
        TArray<FCk_Handle> AllocatedRetiring;
        const auto StressTag = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Poi.Category.Stress")), false);
        const auto CategoryRootForStage = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Poi.Category")), false);
        const auto DisabledForStage = FGameplayTag::RequestGameplayTag(
            FName(TEXT("Poi.Disabled")), false);
        if ((!StagedStressReceipts.IsEmpty() ||
             !FrozenRetiringStressReceipts.IsEmpty()) &&
            (!StressTag.IsValid() || !CategoryRootForStage.IsValid() ||
             !DisabledForStage.IsValid()))
        { return Invalid(TEXT("Inactive POI tags not registered")); }
        for (const auto& R : StagedStressReceipts)
        {
            if (R.CatalogId <= 0 || R.Kind != ECkCompassGym_SourceKind::Stress ||
                R.Host == FCk_Handle{} || R.ExpectedCategory != StressTag ||
                R.Marker != FCk_Handle{} || R.Timer != FCk_Handle{} ||
                AllIds.Contains(R.CatalogId) || AllHandles.Contains(R.Host) ||
                !Live(R.Host) || !Direct(R.Host, SourceRoot, WorldRoot) ||
                !Hosts.Contains(R.Host) || R.Host.Has<ck::FTag_Poi>() ||
                R.Host.Has<ck::FFragment_Transform_RootComponent>() ||
                R.Host.Has_Any<ck::FFragment_VisibleRange_Params,
                    ck::FFragment_VisibleRange, ck::FTag_VisibleRange_Hidden>() ||
                R.Host.Has_Any<ck::FFragment_PoiDisplayDefinition_Params,
                    ck::FFragment_PoiDisplayDefinition>())
            { return Invalid(TEXT("Staged stress receipt or exposure invalid")); }
            AllIds.Add(R.CatalogId);
            AllHandles.Add(R.Host);
            InactiveParentHandles.Add(R.Host);
            TArray<FCk_Handle> ChildSet;
            if (!Children(R.Host, WorldRoot, ChildSet) || !ChildSet.IsEmpty())
            { return Invalid(TEXT("Staged stress host has dependent")); }
            const bool HasTransform = R.Host.Has<ck::FFragment_Transform>();
            if (!HasTransform)
            { return Invalid(TEXT("Composed staged stress transform absent")); }
            const auto Pose = R.Host.Get<ck::FFragment_Transform>().Get_Transform();
            if (!Pose.IsValid() || Pose.ContainsNaN() ||
                !Pose.GetRotation().Equals(FQuat::Identity, 0.0001f) ||
                !Pose.GetScale3D().Equals(FVector::OneVector, 0.0001f))
            { return Invalid(TEXT("Staged stress transform invalid")); }
            const auto Tags = UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(R.Host);
            const auto Categories = Tags.Filter(
                FGameplayTagContainer(CategoryRootForStage));
            if (Tags.HasTagExact(DisabledForStage) || Categories.Num() > 1 ||
                (Categories.Num() == 1 && !Categories.HasTagExact(StressTag)) ||
                (Categories.IsEmpty() && R.Admitted))
            { return Invalid(TEXT("Staged stress category invalid")); }
            if (Categories.IsEmpty()) { InactivePending = true; }
            FCkCompassGym_SourceRow Row;
            Row.CatalogId = R.CatalogId;
            Row.Kind = ECkCompassGym_SourceKind::Stress;
            Row.Category = StressTag;
            Row.Location = Pose.GetLocation();
            StagedRows.Add(MoveTemp(Row));
        }
        TArray<FCk_Handle> FrozenHosts;
        FrozenHosts.Reserve(FrozenRetiringStressReceipts.Num());
        for (const auto& R : FrozenRetiringStressReceipts)
        {
            if (R.CatalogId <= 0 || R.Kind != ECkCompassGym_SourceKind::Stress ||
                R.Host == FCk_Handle{} || R.ExpectedCategory != StressTag ||
                R.Marker != FCk_Handle{} || R.Timer != FCk_Handle{} ||
                AllIds.Contains(R.CatalogId) || AllHandles.Contains(R.Host) ||
                !R.Host.IsRegistryValid() ||
                R.Host.Get_RegistryView().Get_RegistryHandle() !=
                    WorldRoot.Get_RegistryView().Get_RegistryHandle())
            { return Invalid(TEXT("Frozen retiring stress identity invalid")); }
            AllIds.Add(R.CatalogId);
            AllHandles.Add(R.Host);
            InactiveParentHandles.Add(R.Host);
            FrozenHosts.Add(R.Host);
            if (!Allocated(R.Host)) { continue; }
            if (!Direct(R.Host, SourceRoot, WorldRoot) ||
                !Hosts.Contains(R.Host) || R.Host.Has<ck::FTag_Poi>())
            { return Invalid(TEXT("Allocated retiring stress host exposed or foreign")); }
            TArray<FCk_Handle> ChildSet;
            if (!Children(R.Host, WorldRoot, ChildSet) || !ChildSet.IsEmpty())
            { return Invalid(TEXT("Retiring stress host has dependent")); }
            AllocatedRetiring.Add(R.Host);
        }
        if (!FrozenHosts.IsEmpty() &&
            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryProve_FrozenRetirementLedger(
                FrozenHosts, FrozenHosts, WorldRoot))
        { return Invalid(TEXT("Frozen retiring stress membership changed")); }
        if (Hosts.Num() != Receipts.Num() + StagedStressReceipts.Num() +
                AllocatedRetiring.Num())
        { return Invalid(TEXT("Source root allocated cohort count mismatch")); }
        for (const auto& Host : Hosts)
        {
            if (!AllHandles.Contains(Host))
            { return Invalid(TEXT("Unclaimed source root child")); }
        }
    }
    TArray<int64> PreflightIds;
    TArray<FCk_Handle> PreflightHosts;
    for (const auto& R : Receipts)
    {
        const bool NeedsMarker = MarkerKind(R.Kind);
        const bool NeedsTimer = R.Kind == ECkCompassGym_SourceKind::Ping;
        const auto CategoryName = R.ExpectedCategory.GetTagName();
        const bool KindCategoryValid =
            (R.Kind == ECkCompassGym_SourceKind::Stress &&
                CategoryName == TEXT("Poi.Category.Stress")) ||
            (R.Kind == ECkCompassGym_SourceKind::Ring &&
                (CategoryName == TEXT("Poi.Category.Quest") ||
                 CategoryName == TEXT("Poi.Category.Shop") ||
                 CategoryName == TEXT("Poi.Category.Danger") ||
                 CategoryName == TEXT("Poi.Category.Info"))) ||
            (R.Kind == ECkCompassGym_SourceKind::Waypoint &&
                CategoryName == TEXT("Poi.Category.Waypoint")) ||
            (R.Kind == ECkCompassGym_SourceKind::Ping &&
                CategoryName == TEXT("Poi.Category.Ping"));
        if (R.CatalogId <= 0 || PreflightIds.Contains(R.CatalogId) ||
            !KnownKind(R.Kind) || !R.ExpectedCategory.IsValid() ||
            !KindCategoryValid ||
            PreflightHosts.Contains(R.Host) || !Live(R.Host) ||
            !Direct(R.Host, SourceRoot, WorldRoot) || !Hosts.Contains(R.Host) ||
            (NeedsMarker && R.Marker == R.Timer) ||
            (NeedsMarker && (!Live(R.Marker) || !Direct(R.Marker, R.Host, WorldRoot))) ||
            (!NeedsMarker && R.Marker != FCk_Handle{}) ||
            (NeedsTimer && (!Live(R.Timer) || !Direct(R.Timer, R.Host, WorldRoot))) ||
            (!NeedsTimer && R.Timer != FCk_Handle{}))
        { return Invalid(TEXT("Malformed or foreign source receipt")); }
        if (R.Host.Has<ck::FFragment_Transform_RootComponent>())
        { return Invalid(TEXT("RootComponent-backed POI host unsupported")); }
        PreflightIds.Add(R.CatalogId);
        PreflightHosts.Add(R.Host);
        TArray<FCk_Handle> HostChildren;
        if (!Children(R.Host, WorldRoot, HostChildren) ||
            HostChildren.Num() != int32(NeedsMarker) + int32(NeedsTimer) ||
            (NeedsMarker && !HostChildren.Contains(R.Marker)) ||
            (NeedsTimer && !HostChildren.Contains(R.Timer)))
        { return Invalid(TEXT("Unclaimed or missing host dependent")); }
        if (NeedsMarker)
        {
            TArray<FCk_Handle> MarkerChildren;
            if (!Children(R.Marker, WorldRoot, MarkerChildren) ||
                !MarkerChildren.IsEmpty())
            { return Invalid(TEXT("Marker has unclaimed dependent")); }
        }
        if (NeedsTimer)
        {
            TArray<FCk_Handle> TimerChildren;
            if (!Children(R.Timer, WorldRoot, TimerChildren) ||
                !TimerChildren.IsEmpty())
            { return Invalid(TEXT("Timer has unclaimed dependent")); }
        }
    }
    int32 PreflightRings = 0, PreflightWaypoints = 0, PreflightStress = 0;
    for (const auto& R : Receipts)
    {
        if (R.Kind == ECkCompassGym_SourceKind::Ring) { ++PreflightRings; }
        if (R.Kind == ECkCompassGym_SourceKind::Waypoint) { ++PreflightWaypoints; }
        if (R.Kind == ECkCompassGym_SourceKind::Stress) { ++PreflightStress; }
    }
    if (CompositionComplete &&
        (PreflightRings != 8 || PreflightWaypoints != 1 ||
         (PreflightStress != 0 && PreflightStress != 500)))
    { return Invalid(TEXT("Complete source kind counts invalid")); }
    if (!ProveOwnerCensus(SourceRoot, WorldRoot, Hosts, Receipts,
            InactiveParentHandles))
    { return Invalid(TEXT("Source owner census differs from reverse membership")); }
    bool AnyPending = !CompositionComplete || InactivePending;
    TArray<FCkCompassGym_SourceRow> Stage;
    Stage.Reserve(Receipts.Num());
    const auto CategoryRoot = FGameplayTag::RequestGameplayTag(
        FName(TEXT("Poi.Category")), false);
    const auto Disabled = FGameplayTag::RequestGameplayTag(
        FName(TEXT("Poi.Disabled")), false);
    const auto Consumer = FGameplayTag::RequestGameplayTag(
        FName(TEXT("Poi.Consumer.Compass")), false);
    if (!CategoryRoot.IsValid() || !Disabled.IsValid() || !Consumer.IsValid())
    { return Invalid(TEXT("Required POI tags not registered")); }
    for (const auto& R : Receipts)
    {
        if (!R.ExpectedCategory.MatchesTag(CategoryRoot) ||
            R.Host.Has_Any<ck::FFragment_VisibleRange_Params,
                ck::FFragment_VisibleRange, ck::FTag_VisibleRange_Hidden>())
        { return Invalid(TEXT("POI receipt, ownership, or feature invalid")); }
        const bool HasPoi = R.Host.Has<ck::FTag_Poi>();
        const bool HasTransform = R.Host.Has<ck::FFragment_Transform>();
        if ((!HasPoi || !HasTransform) && (CompositionComplete || R.Admitted))
        { return Invalid(TEXT("Composed POI lost feature or transform")); }
        if (!HasPoi || !HasTransform) { AnyPending = true; }
        FVector HostLocation = FVector::ZeroVector;
        if (HasTransform)
        {
            const auto Pose = R.Host.Get<ck::FFragment_Transform>().Get_Transform();
            if (!Pose.IsValid() || Pose.ContainsNaN() ||
                !Pose.GetRotation().Equals(FQuat::Identity, 0.0001f) ||
                !Pose.GetScale3D().Equals(FVector::OneVector, 0.0001f))
            { return Invalid(TEXT("Unsupported POI source transform")); }
            HostLocation = Pose.GetLocation();
        }
        const auto Tags = UCk_Utils_EntityTag_UE::Get_AllTagsAsContainer(R.Host);
        const auto Categories = Tags.Filter(FGameplayTagContainer(CategoryRoot));
        if (Tags.HasTagExact(Disabled) || Categories.Num() > 1 ||
            (Categories.Num() == 1 &&
             !Categories.HasTagExact(R.ExpectedCategory)) ||
            (Categories.IsEmpty() && R.Admitted))
        { return Invalid(TEXT("Materialized POI category or disabled tag invalid")); }
        if (Categories.IsEmpty()) { AnyPending = true; }
        FCkCompassGym_SourceRow Row;
        Row.CatalogId = R.CatalogId;
        Row.Kind = R.Kind;
        Row.Location = HostLocation;
        Row.Category = R.ExpectedCategory;
        const bool NeedsMarker = MarkerKind(R.Kind);
        const bool NeedsTimer = R.Kind == ECkCompassGym_SourceKind::Ping;
        if ((NeedsMarker && (!Live(R.Marker) ||
                !Direct(R.Marker, R.Host, WorldRoot))) ||
            (!NeedsMarker && R.Marker != FCk_Handle{}) ||
            (NeedsTimer && (!Live(R.Timer) ||
                !Direct(R.Timer, R.Host, WorldRoot))) ||
            (!NeedsTimer && R.Timer != FCk_Handle{}))
        { return Invalid(TEXT("Marker/timer first return missing or foreign")); }
        TArray<FCk_Handle> ChildrenOfHost;
        if (!Children(R.Host, WorldRoot, ChildrenOfHost) ||
            ChildrenOfHost.Num() != int32(NeedsMarker) + int32(NeedsTimer) ||
            (NeedsMarker && !ChildrenOfHost.Contains(R.Marker)) ||
            (NeedsTimer && !ChildrenOfHost.Contains(R.Timer)))
        { return Invalid(TEXT("Host has missing or unclaimed dependent")); }
        if (NeedsTimer)
        {
            const bool NeedsSetup = R.Timer.Has<ck::FTag_Timer_NeedsSetup>();
            const bool HasParams = R.Timer.Has<ck::FFragment_Timer_Params>();
            const bool HasCurrent = R.Timer.Has<ck::FFragment_Timer>();
            if (R.Timer.Has<ck::FTag_Timer_Countdown>() ||
                (HasParams && R.Timer.Get<ck::FFragment_Timer_Params>().
                    Get_Behavior() != ECk_Timer_Behavior::StopOnDone) ||
                (HasCurrent && R.Timer.Get<ck::FFragment_Timer>().Get_Chrono().
                    Get_GoalValue().Get_Seconds() != 5.0))
            { return Invalid(TEXT("Ping timer known values invalid")); }
            if (!HasParams && (CompositionComplete || R.Admitted))
            { return Invalid(TEXT("Composed ping timer params absent")); }
            if (!HasParams || !HasCurrent || NeedsSetup)
            {
                if (R.Admitted)
                { return Invalid(TEXT("Ping timer materialization lost")); }
                AnyPending = true;
            }
            else if (!R.Timer.Has<ck::FTag_Timer_NeedsUpdate>())
            { return Invalid(TEXT("Settled ping timer is not running")); }
            TArray<FCk_Handle> TimerChildren;
            if (!Children(R.Timer, WorldRoot, TimerChildren) || !TimerChildren.IsEmpty())
            { return Invalid(TEXT("Ping timer has unexpected child")); }
        }
        const bool HasDisplayParams =
            R.Host.Has<ck::FFragment_PoiDisplayDefinition_Params>();
        const bool HasDisplayCurrent =
            R.Host.Has<ck::FFragment_PoiDisplayDefinition>();
        if (!NeedsMarker && (HasDisplayParams || HasDisplayCurrent))
        { return Invalid(TEXT("Stress POI has unexpected display")); }
        if (NeedsMarker && (!HasDisplayParams || !HasDisplayCurrent))
        {
            if (CompositionComplete || R.Admitted ||
                HasDisplayParams != HasDisplayCurrent)
            { return Invalid(TEXT("Direct POI display presence invalid")); }
            AnyPending = true;
        }
        if (HasDisplayParams)
        {
            const auto& Params =
                R.Host.Get<ck::FFragment_PoiDisplayDefinition_Params>();
            if (Params.Get_Consumer() != Consumer ||
                (Params.Get_OffscreenPolicy() != ECk_Poi_OffscreenPolicy::Hide &&
                 Params.Get_OffscreenPolicy() != ECk_Poi_OffscreenPolicy::ClampToEdge))
            { return Invalid(TEXT("POI display params invalid")); }
            Row.Consumer = Params.Get_Consumer();
            Row.Icon = Params.Get_Icon();
            Row.Priority = Params.Get_Priority();
            Row.OffscreenPolicy = Params.Get_OffscreenPolicy();
        }
        if (HasDisplayCurrent)
        {
            const auto& Current =
                R.Host.Get<ck::FFragment_PoiDisplayDefinition>();
            if (!Finite(Current.Get_Tint()) ||
                Current.Get_SizeHint().ContainsNaN() ||
                Current.Get_SizeHint().X <= 0.0 ||
                Current.Get_SizeHint().Y <= 0.0)
            { return Invalid(TEXT("POI display current invalid")); }
            Row.Tint = Current.Get_Tint();
            Row.SizeHint = Current.Get_SizeHint();
        }
        Row.HasDisplay = HasDisplayParams && HasDisplayCurrent;
        if (NeedsMarker)
        {
            Row.HasMarker = true;
            const auto Outcome = ReadMarker(World, R.Host, R.Marker, WorldRoot,
                R.Kind, R.ExpectedCategory,
                HasTransform ? &HostLocation : nullptr,
                CompositionComplete, Row.Marker);
            if (Outcome == ECkCompassGym_CatalogOutcome::Pending && !R.Admitted)
            { AnyPending = true; }
            else if (Outcome != ECkCompassGym_CatalogOutcome::Complete)
            { return Invalid(TEXT("Source marker physical proof failed")); }
        }
        Stage.Add(MoveTemp(Row));
    }
    if (AnyPending) { return ECkCompassGym_CatalogOutcome::Pending; }
    Stage.Sort([](const auto& A, const auto& B) {
        return A.CatalogId < B.CatalogId;
    });
    OutRows = MoveTemp(Stage);
    StagedRows.Sort([](const auto& A, const auto& B) {
        return A.CatalogId < B.CatalogId;
    });
    OutStagedRows = MoveTemp(StagedRows);
    return ECkCompassGym_CatalogOutcome::Complete;
}

ECkCompassGym_CatalogOutcome UCkCompassGym_SourceCatalog::TryGet_SourceCatalog(
    UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
    bool CompositionComplete, const TArray<FCkCompassGym_SourceReceipt>& Receipts,
    TArray<FCkCompassGym_SourceRow>& OutRows, FString& OutFailureReason)
{
    const TArray<FCkCompassGym_SourceReceipt> EmptyReceipts;
    TArray<FCkCompassGym_SourceRow> UnusedStagedRows;
    return TryGet_SourceCatalogInternal(World, ExactGameState, SourceRoot,
        CompositionComplete, false, Receipts, EmptyReceipts, EmptyReceipts,
        OutRows, UnusedStagedRows,
        OutFailureReason);
}

ECkCompassGym_CatalogOutcome UCkCompassGym_SourceCatalog::
    TryGet_SourceCatalogTransaction(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        bool CompositionComplete,
        const TArray<FCkCompassGym_SourceReceipt>& ActiveReceipts,
        const TArray<FCkCompassGym_SourceReceipt>& StagedStressReceipts,
        const TArray<FCkCompassGym_SourceReceipt>& FrozenRetiringStressReceipts,
        TArray<FCkCompassGym_SourceRow>& OutActiveRows,
        TArray<FCkCompassGym_SourceRow>& OutStagedRows,
        FString& OutFailureReason)
{
    return TryGet_SourceCatalogInternal(World, ExactGameState, SourceRoot,
        CompositionComplete, true, ActiveReceipts, StagedStressReceipts,
        FrozenRetiringStressReceipts, OutActiveRows, OutStagedRows,
        OutFailureReason);
}

#include "CkCueGym_Presenter.h"

#include "CkCore/Diagnostics/CkDiagnosticVisibility.h"
#include "CkCore/Math/Vector/CkVector_Utils.h"
#include "CkPmg/CkPmg_Fragment.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

namespace ck_cue_passive
{
    struct FGeometry
    {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
    };
    struct FPrepared
    {
        FCkCueGym_SourceVisualRow Row;
        FGeometry Fill;
        FGeometry Lines;
    };

    bool Finite(const FVector& V)
    { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
    bool Finite(const FLinearColor& C)
    { return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) && FMath::IsFinite(C.B) && FMath::IsFinite(C.A); }
    bool ValidRow(const FCkCueGym_SourceVisualRow& R)
    {
        if (R.Key <= 0 || R.Role < 0 || R.Role > 5 || R.Role == 4 ||
            (R.Kind != ECkCueGym_SourceVisualKind::Sphere &&
             R.Kind != ECkCueGym_SourceVisualKind::Box) ||
            R.Pose.ContainsNaN() || !R.Pose.GetRotation().IsNormalized() ||
            !Finite(R.Pose.GetLocation()) || !Finite(R.Pose.GetScale3D()) ||
            !Finite(R.Color) || !FMath::IsFinite(R.LineThickness) ||
            R.LineThickness < 0.0f || !FMath::IsFinite(R.Duration) ||
            (R.Axis != ECk_Plane_Axis::XY && R.Axis != ECk_Plane_Axis::XZ &&
             R.Axis != ECk_Plane_Axis::YZ) ||
            (R.RenderMode != ECk_Pmg_RenderMode::SingleSided &&
             R.RenderMode != ECk_Pmg_RenderMode::DoubleSided &&
             R.RenderMode != ECk_Pmg_RenderMode::Hidden) ||
            R.VisibleByRenderMode != (R.RenderMode != ECk_Pmg_RenderMode::Hidden))
        { return false; }
        if (R.Lines.Num() > MAX_int32 / 36 ||
            (R.DrawLines && R.Lines.IsEmpty())) { return false; }
        for (const auto& Line : R.Lines)
        {
            if (!Finite(Line.Start) || !Finite(Line.End) ||
                !Finite(Line.Color) || !FMath::IsFinite(Line.Thickness) ||
                Line.Thickness <= 0.0f ||
                (Line.End - Line.Start).IsNearlyZero())
            { return false; }
        }
        if (R.Kind == ECkCueGym_SourceVisualKind::Sphere)
        {
            // Bounds are arithmetic/index bounds, not a presentation row cap.
            return FMath::IsFinite(R.Radius) && R.Radius > 0.0f &&
                R.Segments >= 3 && R.Rings >= 2 &&
                int64(R.Segments) + 1 <= int64(MAX_int32) / (int64(R.Rings) + 1) &&
                int64(R.Segments) <= int64(MAX_int32) / (int64(R.Rings) * 6);
        }
        return Finite(R.Extent) && R.Extent.X > 0.0 &&
            R.Extent.Y > 0.0 && R.Extent.Z > 0.0;
    }

    void BuildSphere(const FCkCueGym_SourceVisualRow& R, FGeometry& G)
    {
        // Exact loop/winding from CkPmg_Processor_BasicShapes.cpp,
        // ck_pmg_processor_basic_shapes::GenerateDebugShape_Sphere.
        for (int32 Ring = 0; Ring <= R.Rings; ++Ring)
        {
            const auto Phi = PI * Ring / R.Rings;
            const auto RR = R.Radius * FMath::Sin(Phi);
            const auto H = R.Radius * FMath::Cos(Phi);
            for (int32 Segment = 0; Segment <= R.Segments; ++Segment)
            {
                const auto Theta = 2.0f * PI * Segment / R.Segments;
                const FVector V(RR * FMath::Cos(Theta), RR * FMath::Sin(Theta), H);
                G.Vertices.Add(V);
                G.Normals.Add(V.GetSafeNormal());
                G.UVs.Add(FVector2D(float(Segment) / R.Segments, float(Ring) / R.Rings));
            }
        }
        for (int32 Ring = 0; Ring < R.Rings; ++Ring)
        for (int32 Segment = 0; Segment < R.Segments; ++Segment)
        {
            const int32 Current = Ring * (R.Segments + 1) + Segment;
            const int32 Next = Current + R.Segments + 1;
            G.Triangles.Append({Current, Current + 1, Next,
                Current + 1, Next + 1, Next});
        }
        UCk_Utils_Vector3_UE::ApplyPlaneAxisRotation(G.Vertices, G.Normals, R.Axis);
    }

    void BuildBox(const FCkCueGym_SourceVisualRow& R, FGeometry& G)
    {
        // Exact face vertices/winding from GenerateDebugShape_Box in the same file.
        const FVector E = R.Extent;
        const FVector Corners[8] = {
            {-E.X,-E.Y,-E.Z}, {E.X,-E.Y,-E.Z}, {E.X,E.Y,-E.Z}, {-E.X,E.Y,-E.Z},
            {-E.X,-E.Y,E.Z}, {E.X,-E.Y,E.Z}, {E.X,E.Y,E.Z}, {-E.X,E.Y,E.Z}};
        const int32 Faces[6][4] = {
            {0,1,2,3}, {4,7,6,5}, {0,4,5,1},
            {2,6,7,3}, {0,3,7,4}, {1,5,6,2}};
        const FVector Normals[6] = {
            {0,0,-1}, {0,0,1}, {0,-1,0}, {0,1,0}, {-1,0,0}, {1,0,0}};
        for (int32 Face = 0; Face < 6; ++Face)
        {
            const int32 Base = G.Vertices.Num();
            for (int32 I = 0; I < 4; ++I)
            { G.Vertices.Add(Corners[Faces[Face][I]]); G.Normals.Add(Normals[Face]); }
            G.UVs.Append({{0,0},{1,0},{1,1},{0,1}});
            G.Triangles.Append({Base,Base+1,Base+2,Base,Base+2,Base+3});
        }
        UCk_Utils_Vector3_UE::ApplyPlaneAxisRotation(G.Vertices, G.Normals, R.Axis);
    }

    void BakeLine(const FCkCueGym_SourceVisualLine& L, FGeometry& G)
    {
        // CkPmg_Processor_DebugShapes.cpp pmg_bake_lines_helpers::BuildBoxForLine.
        const FVector Dir = (L.End - L.Start).GetSafeNormal();
        if (Dir.IsNearlyZero()) { return; }
        FVector P1 = FVector::CrossProduct(Dir, FVector::UpVector).GetSafeNormal();
        if (P1.IsNearlyZero()) { P1 = FVector::ForwardVector; }
        const FVector P2 = FVector::CrossProduct(Dir, P1).GetSafeNormal();
        const FVector Mid = (L.Start + L.End) * 0.5f;
        const FVector Bias = Mid.GetSafeNormal() * (L.Thickness * 0.5f);
        P1 *= L.Thickness * 0.5f;
        const FVector Q2 = P2 * (L.Thickness * 0.5f);
        const int32 Base = G.Vertices.Num();
        G.Vertices.Append({L.Start-P1-Q2+Bias,L.Start+P1-Q2+Bias,
            L.Start+P1+Q2+Bias,L.Start-P1+Q2+Bias,
            L.End-P1-Q2+Bias,L.End+P1-Q2+Bias,
            L.End+P1+Q2+Bias,L.End-P1+Q2+Bias});
        for (int32 I = 0; I < 8; ++I)
        { G.Normals.Add(FVector::UpVector); G.UVs.Add(FVector2D::ZeroVector); }
        const auto Quad = [&G,Base](int32 A,int32 B,int32 C,int32 D)
        { G.Triangles.Append({Base+A,Base+B,Base+C,Base+A,Base+C,Base+D}); };
        Quad(0,3,2,1); Quad(4,5,6,7); Quad(0,1,5,4);
        Quad(3,7,6,2); Quad(0,4,7,3); Quad(1,2,6,5);
    }

    bool Prepare(const FCkCueGym_SourceVisualRow& R, FPrepared& Out)
    {
        if (!ValidRow(R)) { return false; }
        Out.Row = R;
        if (R.Kind == ECkCueGym_SourceVisualKind::Sphere)
        { BuildSphere(R, Out.Fill); }
        else { BuildBox(R, Out.Fill); }
        if (R.DrawLines)
        {
            for (const auto& Line : R.Lines) { BakeLine(Line, Out.Lines); }
        }
        for (const FVector& V : Out.Fill.Vertices)
        { if (!Finite(V)) { return false; } }
        for (const FVector& V : Out.Lines.Vertices)
        { if (!Finite(V)) { return false; } }
        return Out.Fill.Vertices.Num() > 0 && Out.Fill.Triangles.Num() > 0;
    }

    bool SameSection(const FProcMeshSection* Section, const FGeometry& Expected)
    {
        if (!Section || Section->ProcVertexBuffer.Num() != Expected.Vertices.Num() ||
            Section->ProcIndexBuffer.Num() != Expected.Triangles.Num())
        { return false; }
        for (int32 I = 0; I < Expected.Vertices.Num(); ++I)
        {
            const auto& Vertex = Section->ProcVertexBuffer[I];
            if (!Vertex.Position.Equals(Expected.Vertices[I], KINDA_SMALL_NUMBER) ||
                !Vertex.Normal.Equals(Expected.Normals[I], KINDA_SMALL_NUMBER) ||
                !Vertex.UV0.Equals(Expected.UVs[I], KINDA_SMALL_NUMBER))
            { return false; }
        }
        for (int32 I = 0; I < Expected.Triangles.Num(); ++I)
        { if (Section->ProcIndexBuffer[I] != Expected.Triangles[I]) { return false; } }
        return true;
    }
}

ACk_CueGym_Presenter_UE::ACk_CueGym_Presenter_UE()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    bAlwaysRelevant = false;
    SetActorHiddenInGame(true);
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
    SceneRoot->SetMobility(EComponentMobility::Movable);
}

void ACk_CueGym_Presenter_UE::BeginPlay()
{
    Super::BeginPlay();
    CapturedWorld = GetWorld();
    CapturedController = Cast<APlayerController>(GetOwner());
    if (!HasLocalContext(GetWorld()) || !IsValid(SceneRoot.Get()) ||
        !SceneRoot->IsRegistered())
    { FailClosed(); }
}

void ACk_CueGym_Presenter_UE::EndPlay(const EEndPlayReason::Type Reason)
{ FailClosed(); Super::EndPlay(Reason); }

bool ACk_CueGym_Presenter_UE::HasLocalContext(UWorld* CallerWorld) const
{
    const UWorld* W = CapturedWorld.Get();
    const APlayerController* PC = CapturedController.Get();
    return IsInGameThread() && IsValid(W) && IsValid(PC) &&
        CallerWorld == W && GetWorld() == W && PC->GetWorld() == W &&
        GetOwner() == PC && W->IsGameWorld() && W->GetNetMode() == NM_Client &&
        PC->IsLocalController() && !IsActorBeingDestroyed();
}

bool ACk_CueGym_Presenter_UE::IsKnownOwnedMesh(const UProceduralMeshComponent* M) const
{
    if (!IsValid(M) || M->GetOuter() != this ||
        M->GetClass() != UProceduralMeshComponent::StaticClass())
    { return false; }
    for (const auto& Owned : OwnedMeshes)
    { if (Owned.Get() == M) { return true; } }
    return false;
}

void ACk_CueGym_Presenter_UE::HideKnownOwned()
{
    SetActorHiddenInGame(true);
    for (UProceduralMeshComponent* M : OwnedMeshes)
    {
        if (!IsKnownOwnedMesh(M)) { continue; }
        M->SetVisibility(false);
        if (!IsKnownOwnedMesh(M)) { continue; }
        M->SetHiddenInGame(true);
        if (!IsKnownOwnedMesh(M)) { continue; }
        M->SetMeshSectionVisible(0, false);
        if (M->GetProcMeshSection(1)) { M->SetMeshSectionVisible(1, false); }
        if (!IsKnownOwnedMesh(M)) { continue; }
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
}

void ACk_CueGym_Presenter_UE::DestroyKnownOwned()
{
    HideKnownOwned();
    for (UProceduralMeshComponent* M : OwnedMeshes)
    {
        if (!IsKnownOwnedMesh(M)) { continue; }
        M->ClearAllMeshSections();
        if (!IsKnownOwnedMesh(M)) { continue; }
        M->UnregisterComponent();
        if (!IsKnownOwnedMesh(M)) { continue; }
        M->DestroyComponent();
    }
    OwnedMeshes.Reset();
    InstalledRows.Reset();
    Receipts.Reset();
}

void ACk_CueGym_Presenter_UE::FailClosed()
{
    bFailed = true;
    bSuspended = true;
    const bool WasOperating = bInOperation;
    bInOperation = true;
    DestroyKnownOwned();
    bInOperation = WasOperating;
}

bool ACk_CueGym_Presenter_UE::Request_CloseFailure()
{
    const bool WasOpen = !bFailed;
    if (bInOperation) { bFailed = true; return WasOpen; }
    FailClosed();
    return WasOpen;
}

bool ACk_CueGym_Presenter_UE::TryGet_Receipt(
    int64 Key, FCkCueGym_PassiveReceipt& OutReceipt)
{
    OutReceipt = {};
    if (Key <= 0 || !TryCheckHealth()) { return false; }
    for (const auto& Receipt : Receipts)
    {
        if (Receipt.Key != Key) { continue; }
        OutReceipt = Receipt;
        return true;
    }
    return false;
}

bool ACk_CueGym_Presenter_UE::CheckMesh(
    UProceduralMeshComponent* M, const FCkCueGym_SourceVisualRow& R,
    const FCkCueGym_PassiveReceipt& Receipt, bool bExpectVisible,
    bool bExpectSuspended) const
{
    if (!IsKnownOwnedMesh(M) || M->GetOwner() != this ||
        !M->IsRegistered() ||
        M->GetWorld() != CapturedWorld.Get() || M->GetAttachParent() != SceneRoot ||
        !M->GetComponentTransform().Equals(R.Pose, KINDA_SMALL_NUMBER) ||
        M->IsVisible() != bExpectVisible || M->bHiddenInGame == bExpectVisible)
    { return false; }
    const auto* Fill = M->GetProcMeshSection(0);
    const auto* Lines = M->GetProcMeshSection(1);
    const bool HasLines = Receipt.LineVertices > 0;
    ck_cue_passive::FPrepared Expected;
    if (!ck_cue_passive::Prepare(R, Expected) ||
        !ck_cue_passive::SameSection(Fill, Expected.Fill) ||
        HasLines != !Expected.Lines.Vertices.IsEmpty() ||
        (HasLines && !ck_cue_passive::SameSection(Lines, Expected.Lines)))
    { return false; }
    const auto* FillMID = Cast<UMaterialInstanceDynamic>(M->GetMaterial(0));
    const auto* LineMID = HasLines ? Cast<UMaterialInstanceDynamic>(M->GetMaterial(1)) : nullptr;
    if (!Fill || Fill->ProcVertexBuffer.Num() != Receipt.FillVertices ||
        Fill->ProcIndexBuffer.Num() != Receipt.FillTriangles * 3 ||
        (Lines != nullptr) != HasLines ||
        (HasLines && (Lines->ProcVertexBuffer.Num() != Receipt.LineVertices ||
            Lines->ProcIndexBuffer.Num() != Receipt.LineTriangles * 3)) ||
        !IsValid(FillMID) || (HasLines && !IsValid(LineMID)) ||
        FillMID->GetOuter() != M || !IsValid(FillMID->Parent.Get()) ||
        (HasLines && (LineMID->GetOuter() != M || !IsValid(LineMID->Parent.Get()))) ||
        !Receipt.bRegistered || !Receipt.bHasFillMaterial ||
        (HasLines && !Receipt.bHasLineMaterial))
    { return false; }
    const FString BasePath = TEXT(
        "/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent");
    if (FillMID->Parent->GetPathName() != BasePath ||
        (HasLines && LineMID->Parent->GetPathName() != BasePath))
    { return false; }
    FLinearColor ActualFill;
    if (!FillMID->GetVectorParameterValue(FHashedMaterialParameterInfo{FName(TEXT("Color"))},
            ActualFill) || !ActualFill.Equals(Receipt.FillColor, KINDA_SMALL_NUMBER))
    { return false; }
    if (HasLines)
    {
        FLinearColor ActualLine;
        if (!LineMID->GetVectorParameterValue(FHashedMaterialParameterInfo{FName(TEXT("Color"))},
                ActualLine) || !ActualLine.Equals(Receipt.LineColor, KINDA_SMALL_NUMBER))
        { return false; }
    }
    // PMG's source collision bit is independent of render/streamer visibility.
    // Explicit suspension is a local presentation pause and disables queries.
    const ECollisionEnabled::Type ExpectedCollision = !bExpectSuspended && R.EnableCollision
        ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision;
    return M->GetCollisionEnabled() == ExpectedCollision &&
        M->IsMeshSectionVisible(0) == bExpectVisible &&
        (!HasLines || M->IsMeshSectionVisible(1) == bExpectVisible);
}

bool ACk_CueGym_Presenter_UE::CheckCohort(bool bExpectSuspended) const
{
    if (!HasLocalContext(GetWorld()) || !IsValid(SceneRoot.Get()) ||
        GetRootComponent() != SceneRoot || !SceneRoot->IsRegistered() ||
        SceneRoot->GetOwner() != this || SceneRoot->GetOuter() != this ||
        SceneRoot->GetClass() != USceneComponent::StaticClass() ||
        !SceneRoot->HasAnyFlags(RF_DefaultSubObject) ||
        IsHidden() != bExpectSuspended ||
        InstalledRows.Num() != Receipts.Num() ||
        OwnedMeshes.Num() != Receipts.Num())
    { return false; }
    TArray<USceneComponent*> Components;
    GetComponents<USceneComponent>(Components);
    if (Components.Num() != OwnedMeshes.Num() + 1) { return false; }
    for (USceneComponent* Component : Components)
    {
        if (Component == SceneRoot) { continue; }
        if (!IsKnownOwnedMesh(Cast<UProceduralMeshComponent>(Component)))
        { return false; }
    }
    for (int32 I = 0; I < Receipts.Num(); ++I)
    {
        const auto& R = InstalledRows[I];
        const auto& Receipt = Receipts[I];
        UProceduralMeshComponent* M = Receipt.Mesh.Get();
        const bool Visible = !bExpectSuspended && R.VisibleByRenderMode &&
            !ck::diagnostic_visibility::Is_HiddenForStreamerMode();
        if (R.Key != Receipt.Key || R.Role != Receipt.Role ||
            R.Kind != Receipt.Kind || M != OwnedMeshes[I] ||
            !CheckMesh(M, R, Receipt, Visible, bExpectSuspended))
        { return false; }
        for (int32 J = 0; J < I; ++J)
        { if (R.Key == InstalledRows[J].Key || M == OwnedMeshes[J]) { return false; } }
    }
    return true;
}

bool ACk_CueGym_Presenter_UE::TryCheckHealth()
{
    if (bInOperation) { bFailed = true; return false; }
    if (bFailed) { FailClosed(); return false; }
    bInOperation = true;
    if (InstalledRows.Num() != Receipts.Num() ||
        OwnedMeshes.Num() != Receipts.Num())
    { FailClosed(); bInOperation = false; return false; }
    // Streamer policy belongs to this client. Apply a change behind the actor
    // barrier without treating a local policy toggle as a corrupt source frame.
    if (!bSuspended && HasLocalContext(GetWorld()))
    {
        bool PolicyChanged = false;
        for (int32 I = 0; I < Receipts.Num(); ++I)
        {
            const bool Wanted = InstalledRows[I].VisibleByRenderMode &&
                !ck::diagnostic_visibility::Is_HiddenForStreamerMode();
            PolicyChanged |= Wanted != Receipts[I].bVisibleByPolicy;
        }
        if (PolicyChanged)
        {
            for (int32 I = 0; I < Receipts.Num(); ++I)
            {
                if (!CheckMesh(OwnedMeshes[I], InstalledRows[I], Receipts[I],
                        Receipts[I].bVisibleByPolicy, false))
                { FailClosed(); bInOperation = false; return false; }
            }
            SetActorHiddenInGame(true);
            for (int32 I = 0; I < Receipts.Num(); ++I)
            {
                UProceduralMeshComponent* M = OwnedMeshes[I];
                if (!IsKnownOwnedMesh(M))
                { FailClosed(); bInOperation = false; return false; }
                const bool Wanted = InstalledRows[I].VisibleByRenderMode &&
                    !ck::diagnostic_visibility::Is_HiddenForStreamerMode();
                M->SetMeshSectionVisible(0, Wanted);
                if (Receipts[I].LineVertices > 0)
                { M->SetMeshSectionVisible(1, Wanted); }
                M->SetCollisionEnabled(InstalledRows[I].EnableCollision
                    ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
                M->SetHiddenInGame(!Wanted);
                M->SetVisibility(Wanted);
                Receipts[I].bVisibleByPolicy = Wanted;
                if (bFailed || !CheckMesh(M, InstalledRows[I], Receipts[I], Wanted, false))
                { FailClosed(); bInOperation = false; return false; }
            }
            SetActorHiddenInGame(false);
        }
    }
    const bool Healthy = CheckCohort(bSuspended) &&
        (bSuspended || InstalledSequence > 0);
    if (!Healthy || bFailed) { FailClosed(); }
    bInOperation = false;
    return Healthy && !bFailed;
}

bool ACk_CueGym_Presenter_UE::Request_Suspend()
{
    if (bInOperation) { bFailed = true; return false; }
    if (bFailed) { FailClosed(); return false; }
    bInOperation = true;
    if (!CheckCohort(bSuspended)) { FailClosed(); bInOperation = false; return false; }
    HideKnownOwned();
    bSuspended = true;
    for (auto& Receipt : Receipts) { Receipt.bVisibleByPolicy = false; }
    if (bFailed || !CheckCohort(true))
    { FailClosed(); bInOperation = false; return false; }
    bInOperation = false;
    return true;
}

bool ACk_CueGym_Presenter_UE::TryInstall(
    UWorld* CallerWorld, int32 SourceGeneration, int64 Sequence,
    const TArray<FCkCueGym_SourceVisualRow>& CompleteRows)
{
    if (bInOperation) { bFailed = true; return false; }
    if (bFailed) { FailClosed(); return false; }
    bInOperation = true;
    const auto Abort = [this]() -> bool
    { FailClosed(); bInOperation = false; return false; };
    if (!HasLocalContext(CallerWorld) || SourceGeneration <= 0 ||
        SourceGeneration < InstalledGeneration || Sequence <= InstalledSequence ||
        Sequence <= 0 || !CheckCohort(bSuspended))
    { return Abort(); }

    TArray<FCkCueGym_SourceVisualRow> NormalizedRows;
    if (!UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(CompleteRows, NormalizedRows))
    { return Abort(); }

    // Preflight complete set and all geometry before mutating a component.
    TSet<int64> Keys;
    TArray<ck_cue_passive::FPrepared> Prepared;
    Prepared.Reserve(NormalizedRows.Num());
    for (const auto& Row : NormalizedRows)
    {
        if (Keys.Contains(Row.Key)) { return Abort(); }
        Keys.Add(Row.Key);
        auto& Item = Prepared.AddDefaulted_GetRef();
        if (!ck_cue_passive::Prepare(Row, Item)) { return Abort(); }
    }
    UMaterial* Base = LoadObject<UMaterial>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent.M_SimpleUnlitTranslucent"));
    if (!IsValid(Base)) { return Abort(); }

    HideKnownOwned();
    if (bFailed || !IsHidden()) { return Abort(); }
    TArray<TObjectPtr<UProceduralMeshComponent>> NewMeshes;
    TArray<FCkCueGym_PassiveReceipt> NewReceipts;
    TSet<int32> ReusedOldIndices;
    for (const auto& Item : Prepared)
    {
        UProceduralMeshComponent* M = nullptr;
        for (int32 Old = 0; Old < InstalledRows.Num(); ++Old)
        {
            if (InstalledRows[Old].Key == Item.Row.Key &&
                InstalledRows[Old].Kind == Item.Row.Kind)
            { M = OwnedMeshes[Old]; ReusedOldIndices.Add(Old); break; }
        }
        const bool Reused = M != nullptr;
        if (!Reused)
        {
            M = NewObject<UProceduralMeshComponent>(this);
            if (!IsValid(M)) { return Abort(); }
            // Retain the first returned pointer before fallible setup.
            OwnedMeshes.Add(M);
        }
        NewMeshes.Add(M);
        if (!IsKnownOwnedMesh(M)) { return Abort(); }
        if (!Reused)
        {
            AddInstanceComponent(M);
            if (bFailed || !IsKnownOwnedMesh(M) || M->GetOwner() != this)
            { return Abort(); }
        }
        M->SetVisibility(false);
        M->SetHiddenInGame(true);
        M->SetCastShadow(false);
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (!Reused)
        {
            M->SetupAttachment(SceneRoot);
            M->RegisterComponentWithWorld(CallerWorld);
        }
        if (bFailed || !IsKnownOwnedMesh(M) || !M->IsRegistered() ||
            M->GetAttachParent() != SceneRoot) { return Abort(); }
        if (!ck_cue_passive::SameSection(M->GetProcMeshSection(0), Item.Fill))
        {
            M->CreateMeshSection_LinearColor(0, Item.Fill.Vertices,
                Item.Fill.Triangles, Item.Fill.Normals, Item.Fill.UVs,
                TArray<FLinearColor>{}, TArray<FProcMeshTangent>{}, true);
        }
        if (bFailed || !IsKnownOwnedMesh(M) || !M->GetProcMeshSection(0))
        { return Abort(); }
        M->SetMeshSectionVisible(0, false);
        if (!Item.Lines.Vertices.IsEmpty())
        {
            if (!ck_cue_passive::SameSection(M->GetProcMeshSection(1), Item.Lines))
            {
                M->CreateMeshSection_LinearColor(1, Item.Lines.Vertices,
                    Item.Lines.Triangles, Item.Lines.Normals, Item.Lines.UVs,
                    TArray<FLinearColor>{}, TArray<FProcMeshTangent>{}, false);
            }
            if (bFailed || !IsKnownOwnedMesh(M) || !M->GetProcMeshSection(1))
            { return Abort(); }
        }
        else if (M->GetProcMeshSection(1)) { M->ClearMeshSection(1); }
        UMaterialInstanceDynamic* FillMID = Reused
            ? Cast<UMaterialInstanceDynamic>(M->GetMaterial(0)) : nullptr;
        if (!IsValid(FillMID)) { FillMID = UMaterialInstanceDynamic::Create(Base, M); }
        if (!IsValid(FillMID)) { return Abort(); }
        FLinearColor FillColor = Item.Row.Color;
        FillColor.A = ck::pmg_debug_shape::FillOpacity;
        FillMID->SetVectorParameterValue(TEXT("Color"), FillColor);
        M->SetMaterial(0, FillMID);
        UMaterialInstanceDynamic* LineMID = nullptr;
        FLinearColor LineColor = Item.Row.Color;
        LineColor.A = 1.0f;
        if (!Item.Lines.Vertices.IsEmpty())
        {
            LineMID = Reused ? Cast<UMaterialInstanceDynamic>(M->GetMaterial(1)) : nullptr;
            if (!IsValid(LineMID)) { LineMID = UMaterialInstanceDynamic::Create(Base, M); }
            if (!IsValid(LineMID)) { return Abort(); }
            LineMID->SetVectorParameterValue(TEXT("Color"), LineColor);
            M->SetMaterial(1, LineMID);
            M->SetMeshSectionVisible(1, false);
        }
        else { M->SetMaterial(1, nullptr); }
        M->SetWorldTransform(Item.Row.Pose, false, nullptr,
            ETeleportType::TeleportPhysics);
        if (bFailed || !IsKnownOwnedMesh(M) ||
            !M->GetComponentTransform().Equals(Item.Row.Pose, KINDA_SMALL_NUMBER))
        { return Abort(); }
        FCkCueGym_PassiveReceipt Receipt;
        Receipt.Key = Item.Row.Key;
        Receipt.Role = Item.Row.Role;
        Receipt.Kind = Item.Row.Kind;
        Receipt.FillVertices = Item.Fill.Vertices.Num();
        Receipt.FillTriangles = Item.Fill.Triangles.Num() / 3;
        Receipt.LineVertices = Item.Lines.Vertices.Num();
        Receipt.LineTriangles = Item.Lines.Triangles.Num() / 3;
        Receipt.bRegistered = M->IsRegistered();
        Receipt.bHasFillMaterial = M->GetMaterial(0) == FillMID;
        Receipt.bHasLineMaterial = LineMID && M->GetMaterial(1) == LineMID;
        Receipt.Pose = Item.Row.Pose;
        Receipt.FillColor = FillColor;
        Receipt.LineColor = LineColor;
        Receipt.Mesh = M;
        if (!CheckMesh(M, Item.Row, Receipt, false, true) || bFailed)
        { return Abort(); }
        NewReceipts.Add(Receipt);
    }
    if (NewReceipts.Num() != NormalizedRows.Num() || bFailed)
    { return Abort(); }

    // Retire only removed/replaced rows; stable keys/kinds keep component and MID.
    const int32 OldCount = InstalledRows.Num();
    for (int32 I = 0; I < OldCount; ++I)
    {
        if (ReusedOldIndices.Contains(I)) { continue; }
        UProceduralMeshComponent* M = OwnedMeshes[I];
        if (!IsKnownOwnedMesh(M)) { return Abort(); }
        M->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        M->ClearAllMeshSections();
        if (!IsKnownOwnedMesh(M)) { return Abort(); }
        M->UnregisterComponent();
        if (!IsKnownOwnedMesh(M)) { return Abort(); }
        M->DestroyComponent();
    }
    OwnedMeshes = MoveTemp(NewMeshes);
    InstalledRows = MoveTemp(NormalizedRows);
    Receipts = MoveTemp(NewReceipts);
    if (!CheckCohort(true) || bFailed) { return Abort(); }

    // Configure every local visibility/collision bit behind the actor barrier.
    for (int32 I = 0; I < InstalledRows.Num(); ++I)
    {
        UProceduralMeshComponent* M = OwnedMeshes[I];
        const auto& R = InstalledRows[I];
        if (!IsKnownOwnedMesh(M)) { return Abort(); }
        const bool Visible = R.VisibleByRenderMode &&
            !ck::diagnostic_visibility::Is_HiddenForStreamerMode();
        M->SetMeshSectionVisible(0, Visible);
        if (Receipts[I].LineVertices > 0) { M->SetMeshSectionVisible(1, Visible); }
        M->SetCollisionEnabled(R.EnableCollision
            ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
        M->SetHiddenInGame(!Visible);
        M->SetVisibility(Visible);
        Receipts[I].bVisibleByPolicy = Visible;
        if (bFailed || !CheckMesh(M, R, Receipts[I], Visible, false) || !IsHidden())
        { return Abort(); }
    }
    SetActorHiddenInGame(false);
    bSuspended = false;
    InstalledGeneration = SourceGeneration;
    InstalledSequence = Sequence;
    if (bFailed || !CheckCohort(false)) { return Abort(); }
    bInOperation = false;
    return true;
}

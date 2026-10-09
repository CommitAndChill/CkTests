#include "CkCompassGym_Wire.h"

#include "CkCueGym_OutlineRecipe.h"
#include "UObject/UnrealType.h"

namespace
{
    bool Finite(const FLinearColor& C)
    {
        return FMath::IsFinite(C.R) && FMath::IsFinite(C.G) &&
            FMath::IsFinite(C.B) && FMath::IsFinite(C.A);
    }

    bool Finite(const FVector& V)
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) &&
            FMath::IsFinite(V.Z);
    }

    bool ExpectedColor(ECkCompassGym_SourceKind Kind,
        const FGameplayTag& Category, FLinearColor& Out)
    {
        const FName Name = Category.GetTagName();
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

    bool ValidateAndExpandRow(const FCkCompassGym_SourceRow& Input,
        FCkCompassGym_SourceRow& Expanded)
    {
        if (Input.CatalogId <= 0 || !Finite(Input.Location) ||
            !Input.Category.IsValid())
        { return false; }
        const bool Stress = Input.Kind == ECkCompassGym_SourceKind::Stress;
        FLinearColor Color;
        if (Stress)
        {
            const FCkCompassGym_SourceMarker DefaultMarker;
            if (Input.Category.GetTagName() != TEXT("Poi.Category.Stress") ||
                Input.HasDisplay || Input.HasMarker || Input.Consumer.IsValid() ||
                !Input.Icon.IsNull() || Input.Priority != 0 ||
                Input.OffscreenPolicy != ECk_Poi_OffscreenPolicy::Hide ||
                Input.Tint != FLinearColor::White ||
                Input.SizeHint != FVector2D(32.0, 32.0) ||
                !FCkCompassGym_SourceMarker::StaticStruct()->CompareScriptStruct(
                    &Input.Marker, &DefaultMarker, PPF_None))
            { return false; }
            Expanded = Input;
            return true;
        }
        if (!ExpectedColor(Input.Kind, Input.Category, Color) ||
            !Input.HasDisplay || !Input.HasMarker ||
            Input.Consumer.GetTagName() != TEXT("Poi.Consumer.Compass") ||
            (Input.OffscreenPolicy != ECk_Poi_OffscreenPolicy::Hide &&
             Input.OffscreenPolicy != ECk_Poi_OffscreenPolicy::ClampToEdge) ||
            !Finite(Input.Tint) ||
            !FMath::IsFinite(Input.SizeHint.X) ||
            !FMath::IsFinite(Input.SizeHint.Y) ||
            Input.SizeHint.X <= 0.0 || Input.SizeHint.Y <= 0.0)
        { return false; }
        const auto& M = Input.Marker;
        const float Radius = Input.Kind == ECkCompassGym_SourceKind::Ring ?
            40.0f : Input.Kind == ECkCompassGym_SourceKind::Waypoint ?
            55.0f : 45.0f;
        if (!M.Pose.IsValid() || M.Pose.ContainsNaN() ||
            !Finite(M.Pose.GetLocation()) ||
            !Finite(M.Pose.GetScale3D()) ||
            !M.Pose.GetLocation().Equals(Input.Location + FVector(0, 0, 60), 0.02) ||
            !M.Pose.GetRotation().Equals(FQuat::Identity, 0.0001) ||
            !M.Pose.GetScale3D().Equals(FVector::OneVector, 0.0001) ||
            !Finite(M.Color) || !M.Color.Equals(Color, 0.0001f) ||
            M.Radius != Radius || M.Segments != 12 || M.Rings != 12 ||
            M.Axis != ECk_Plane_Axis::XY || !M.DrawLines ||
            !FMath::IsFinite(M.LineThickness) || M.LineThickness != 2.0f ||
            !FMath::IsFinite(M.Duration) || M.Duration != -1.0 ||
            M.RenderMode != ECk_Pmg_RenderMode::DoubleSided ||
            !M.VisibleByRenderMode || M.EnableCollision)
        { return false; }
        FCkCueGym_SourceVisualRow Recipe;
        Recipe.Kind = ECkCueGym_SourceVisualKind::Sphere;
        Recipe.Pose = M.Pose;
        Recipe.Color = M.Color;
        Recipe.Radius = M.Radius;
        Recipe.Segments = M.Segments;
        Recipe.Rings = M.Rings;
        Recipe.Axis = M.Axis;
        Recipe.DrawLines = M.DrawLines;
        Recipe.LineThickness = M.LineThickness;
        TArray<FCkCueGym_SourceVisualLine> Lines;
        if (!ck_cue_outline_recipe::TryGenerate(Recipe, Lines) || Lines.Num() != 96)
        { return false; }
        if (!M.Lines.IsEmpty())
        {
            if (M.Lines.Num() != Lines.Num()) { return false; }
            for (int32 I = 0; I < Lines.Num(); ++I)
            {
                const auto& A = M.Lines[I];
                const auto& B = Lines[I];
                if (A.Start != B.Start || A.End != B.End ||
                    A.Color != B.Color || A.Thickness != B.Thickness)
                { return false; }
            }
        }
        Expanded = Input;
        Expanded.Marker.Lines.Reset();
        Expanded.Marker.Lines.Reserve(Lines.Num());
        for (const auto& Line : Lines)
        {
            FCkCompassGym_SourceLine Copy;
            Copy.Start = Line.Start;
            Copy.End = Line.End;
            Copy.Color = Line.Color;
            Copy.Thickness = Line.Thickness;
            Expanded.Marker.Lines.Add(Copy);
        }
        return true;
    }

    bool ValidEnvelope(const FCkCompassGym_WireSnapshot& S)
    {
        if (S.SchemaVersion != 1 || S.SourceGeneration < 0 ||
            S.Sequence < 0 || S.DeclaredRowCount < 0 ||
            S.DeclaredRowCount != S.Rows.Num())
        { return false; }
        if ((S.SourceGeneration == 0) != (S.Sequence == 0))
        { return false; }
        if (S.SourceGeneration == 0 &&
            S.Lifecycle != ECkCompassGym_WireLifecycle::Initializing)
        { return false; }
        switch (S.Lifecycle)
        {
        case ECkCompassGym_WireLifecycle::Initializing:
        case ECkCompassGym_WireLifecycle::Failed:
            return S.Rows.IsEmpty();
        case ECkCompassGym_WireLifecycle::Active:
            return S.SourceGeneration > 0 && S.Sequence > 0;
        default:
            return false;
        }
    }
}

bool UCkCompassGym_Wire::TryValidateAndExpand(
    const FCkCompassGym_WireSnapshot& Snapshot,
    TArray<FCkCompassGym_SourceRow>& OutExpandedRows)
{
    if (&Snapshot.Rows == &OutExpandedRows) { return false; }
    OutExpandedRows.Reset();
    if (!ValidEnvelope(Snapshot)) { return false; }
    if (Snapshot.Lifecycle != ECkCompassGym_WireLifecycle::Active)
    { return true; }
    TArray<FCkCompassGym_SourceRow> Stage;
    Stage.Reserve(Snapshot.Rows.Num());
    int32 Rings = 0;
    int32 Waypoints = 0;
    int32 Stress = 0;
    int64 PreviousId = 0;
    for (const auto& Row : Snapshot.Rows)
    {
        if (Row.CatalogId <= PreviousId) { return false; }
        PreviousId = Row.CatalogId;
        FCkCompassGym_SourceRow Expanded;
        if (!ValidateAndExpandRow(Row, Expanded)) { return false; }
        switch (Row.Kind)
        {
        case ECkCompassGym_SourceKind::Ring: ++Rings; break;
        case ECkCompassGym_SourceKind::Waypoint: ++Waypoints; break;
        case ECkCompassGym_SourceKind::Stress: ++Stress; break;
        case ECkCompassGym_SourceKind::Ping: break;
        default: return false;
        }
        Stage.Add(MoveTemp(Expanded));
    }
    if (Rings != 8 || Waypoints != 1 || (Stress != 0 && Stress != 500))
    { return false; }
    OutExpandedRows = MoveTemp(Stage);
    return true;
}

bool UCkCompassGym_Wire::Get_IsSameFrame(
    const FCkCompassGym_WireSnapshot& A,
    const FCkCompassGym_WireSnapshot& B)
{
    TArray<FCkCompassGym_SourceRow> AE;
    TArray<FCkCompassGym_SourceRow> BE;
    if (!TryValidateAndExpand(A, AE) || !TryValidateAndExpand(B, BE) ||
        A.SchemaVersion != B.SchemaVersion ||
        A.SourceGeneration != B.SourceGeneration || A.Sequence != B.Sequence ||
        A.Lifecycle != B.Lifecycle ||
        A.DeclaredRowCount != B.DeclaredRowCount || AE.Num() != BE.Num())
    { return false; }
    const auto* Struct = FCkCompassGym_SourceRow::StaticStruct();
    for (int32 I = 0; I < AE.Num(); ++I)
    {
        if (!Struct->CompareScriptStruct(&AE[I], &BE[I], PPF_None))
        { return false; }
    }
    return true;
}

bool UCkCompassGym_Wire::Get_IsAdvance(
    const FCkCompassGym_WireSnapshot& Previous,
    const FCkCompassGym_WireSnapshot& Next)
{
    TArray<FCkCompassGym_SourceRow> Discard;
    if (!TryValidateAndExpand(Previous, Discard) ||
        !TryValidateAndExpand(Next, Discard))
    { return false; }
    if (Next.SourceGeneration > Previous.SourceGeneration)
    { return Next.SourceGeneration > 0; }
    if (Next.SourceGeneration != Previous.SourceGeneration ||
        Next.Sequence <= Previous.Sequence)
    { return false; }
    if (Previous.Lifecycle == ECkCompassGym_WireLifecycle::Failed)
    { return Next.Lifecycle == ECkCompassGym_WireLifecycle::Failed; }
    if (Previous.Lifecycle == ECkCompassGym_WireLifecycle::Active &&
        Next.Lifecycle == ECkCompassGym_WireLifecycle::Initializing)
    { return false; }
    return true;
}

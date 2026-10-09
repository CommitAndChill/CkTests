#include "CkCueGym_OutlineRecipe.h"

#include "CkCore/Math/Vector/CkVector_Utils.h"

namespace ck_cue_outline_recipe
{
    namespace
    {
        // Mirrors ck::pmg::Append_DebugLine_World: the stored line is local to
        // the shape transform, not the world-space point passed to Append.
        void AddWorldLine(const FTransform& Pose, const FVector& WorldStart,
            const FVector& WorldEnd, const FLinearColor& Color, float Thickness,
            TArray<FCkCueGym_SourceVisualLine>& Lines)
        {
            FCkCueGym_SourceVisualLine Line;
            Line.Start = Pose.InverseTransformPosition(WorldStart);
            Line.End = Pose.InverseTransformPosition(WorldEnd);
            Line.Color = Color;
            Line.Thickness = Thickness;
            Lines.Add(Line);
        }

        bool Finite(const FLinearColor& Color)
        {
            return FMath::IsFinite(Color.R) && FMath::IsFinite(Color.G) &&
                FMath::IsFinite(Color.B) && FMath::IsFinite(Color.A);
        }

        bool Same(const FCkCueGym_SourceVisualLine& A,
            const FCkCueGym_SourceVisualLine& B)
        {
            return A.Start.X == B.Start.X && A.Start.Y == B.Start.Y &&
                A.Start.Z == B.Start.Z && A.End.X == B.End.X &&
                A.End.Y == B.End.Y && A.End.Z == B.End.Z &&
                A.Color.R == B.Color.R && A.Color.G == B.Color.G &&
                A.Color.B == B.Color.B && A.Color.A == B.Color.A &&
                A.Thickness == B.Thickness;
        }
    }

    bool TryGenerate(const FCkCueGym_SourceVisualRow& Row,
        TArray<FCkCueGym_SourceVisualLine>& OutLines)
    {
        OutLines.Reset();
        if ((Row.Kind != ECkCueGym_SourceVisualKind::Sphere &&
             Row.Kind != ECkCueGym_SourceVisualKind::Box) ||
            !Row.Pose.IsValid() || Row.Pose.ContainsNaN() ||
            !FMath::IsFinite(Row.Pose.GetScale3D().X) ||
            !FMath::IsFinite(Row.Pose.GetScale3D().Y) ||
            !FMath::IsFinite(Row.Pose.GetScale3D().Z) ||
            Row.Pose.GetScale3D().X <= 0.0 ||
            Row.Pose.GetScale3D().Y <= 0.0 ||
            Row.Pose.GetScale3D().Z <= 0.0 ||
            !Finite(Row.Color) || !FMath::IsFinite(Row.LineThickness) ||
            Row.LineThickness < 0.0f ||
            (Row.Axis != ECk_Plane_Axis::XY && Row.Axis != ECk_Plane_Axis::XZ &&
             Row.Axis != ECk_Plane_Axis::YZ))
        { return false; }
        if (!Row.DrawLines) { return true; }
        if (Row.LineThickness <= 0.0f) { return false; }

        auto LineColor = Row.Color;
        LineColor.A = 1.0f;
        const auto Center = Row.Pose.GetLocation();
        const auto AxisRot = UCk_Utils_Vector3_UE::Get_PlaneAxisRotation(Row.Axis);
        if (Row.Kind == ECkCueGym_SourceVisualKind::Sphere)
        {
            if (!FMath::IsFinite(Row.Radius) || Row.Radius <= 0.0f ||
                Row.Segments < 3 || Row.Rings < 2) { return false; }
            const auto Rot = Row.Pose.GetRotation();
            const auto CombinedRot = Rot * AxisRot;
            const auto Radius = Row.Radius;
            const auto ArcSegments = 32;
            OutLines.Reserve(3 * ArcSegments);

            // Preserve PMG's three loops, order and floating-point expressions.
            for (auto j = 0; j < ArcSegments; ++j)
            {
                const auto Phi1 = 2.0f * PI * j / ArcSegments;
                const auto Phi2 = 2.0f * PI * (j + 1) / ArcSegments;
                const auto P1 = Center + CombinedRot.RotateVector(FVector(
                    Radius * FMath::Cos(Phi1), 0.0f, Radius * FMath::Sin(Phi1)));
                const auto P2 = Center + CombinedRot.RotateVector(FVector(
                    Radius * FMath::Cos(Phi2), 0.0f, Radius * FMath::Sin(Phi2)));
                AddWorldLine(Row.Pose, P1, P2, LineColor, Row.LineThickness, OutLines);
            }
            for (auto j = 0; j < ArcSegments; ++j)
            {
                const auto Phi1 = 2.0f * PI * j / ArcSegments;
                const auto Phi2 = 2.0f * PI * (j + 1) / ArcSegments;
                const auto P1 = Center + CombinedRot.RotateVector(FVector(
                    0.0f, Radius * FMath::Cos(Phi1), Radius * FMath::Sin(Phi1)));
                const auto P2 = Center + CombinedRot.RotateVector(FVector(
                    0.0f, Radius * FMath::Cos(Phi2), Radius * FMath::Sin(Phi2)));
                AddWorldLine(Row.Pose, P1, P2, LineColor, Row.LineThickness, OutLines);
            }
            for (auto j = 0; j < ArcSegments; ++j)
            {
                const auto Phi1 = 2.0f * PI * j / ArcSegments;
                const auto Phi2 = 2.0f * PI * (j + 1) / ArcSegments;
                const auto P1 = Center + CombinedRot.RotateVector(FVector(
                    Radius * FMath::Cos(Phi1), Radius * FMath::Sin(Phi1), 0.0f));
                const auto P2 = Center + CombinedRot.RotateVector(FVector(
                    Radius * FMath::Cos(Phi2), Radius * FMath::Sin(Phi2), 0.0f));
                AddWorldLine(Row.Pose, P1, P2, LineColor, Row.LineThickness, OutLines);
            }
        }
        else
        {
            if (Row.Extent.ContainsNaN() || Row.Extent.X <= 0.0 ||
                Row.Extent.Y <= 0.0 || Row.Extent.Z <= 0.0) { return false; }
            // PMG passes CombinedRot.Rotator() to Append_DebugBox_World,
            // which converts that rotator back to a quaternion. Keep both steps.
            const auto Rotation = Row.Pose.GetRotation();
            const auto CombinedRot = Rotation * AxisRot;
            const auto Quat = CombinedRot.Rotator().Quaternion();
            const auto Extent = Row.Extent;
            const FVector Corners[8] = {
                Center + Quat.RotateVector(FVector(-Extent.X, -Extent.Y, -Extent.Z)),
                Center + Quat.RotateVector(FVector( Extent.X, -Extent.Y, -Extent.Z)),
                Center + Quat.RotateVector(FVector( Extent.X,  Extent.Y, -Extent.Z)),
                Center + Quat.RotateVector(FVector(-Extent.X,  Extent.Y, -Extent.Z)),
                Center + Quat.RotateVector(FVector(-Extent.X, -Extent.Y,  Extent.Z)),
                Center + Quat.RotateVector(FVector( Extent.X, -Extent.Y,  Extent.Z)),
                Center + Quat.RotateVector(FVector( Extent.X,  Extent.Y,  Extent.Z)),
                Center + Quat.RotateVector(FVector(-Extent.X,  Extent.Y,  Extent.Z))};
            const int32 Edges[12][2] = {
                {0, 1}, {1, 2}, {2, 3}, {3, 0},
                {4, 5}, {5, 6}, {6, 7}, {7, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            OutLines.Reserve(12);
            for (const auto& Edge : Edges)
            {
                AddWorldLine(Row.Pose, Corners[Edge[0]], Corners[Edge[1]],
                    LineColor, Row.LineThickness, OutLines);
            }
        }

        for (const auto& Line : OutLines)
        {
            if (Line.Start.ContainsNaN() || Line.End.ContainsNaN() ||
                !Finite(Line.Color) || !FMath::IsFinite(Line.Thickness) ||
                Line.Thickness <= 0.0f)
            { OutLines.Reset(); return false; }
        }
        return true;
    }

    bool Get_MatchesActualLines(const FCkCueGym_SourceVisualRow& Row,
        const TArray<FCkCueGym_SourceVisualLine>& ActualLines)
    {
        TArray<FCkCueGym_SourceVisualLine> Expected;
        if (!TryGenerate(Row, Expected) || Expected.Num() != ActualLines.Num())
        { return false; }
        for (int32 Index = 0; Index < Expected.Num(); ++Index)
        { if (!Same(Expected[Index], ActualLines[Index])) { return false; } }
        return true;
    }
}

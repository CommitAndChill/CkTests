// Candidate for Plugins/CkTests/Script/CkRotateTowards/CkRotateTowardsGym_HUD.as.
// The owning PC supplies an already-installed, same-generation and same-sequence sample.
class ACk_RotateTowardsGym_HUD : ACkGym_ControlPanelHUD
{
    UFUNCTION(BlueprintOverride)
    void DrawHUD(int32 SizeX, int32 SizeY)
    {
        Super::DrawHUD(SizeX, SizeY);
        if (SizeX <= 0 || SizeY <= 0 || UCk_Utils_GymRegistry_UE::Get_SuppressHUDDuringStartup())
        { return; }

        auto PC = Cast<ACk_RotateTowardsGym_PlayerController>(GetOwningPlayerController());
        if (ck::Is_NOT_Valid(PC) || !PC.IsLocalController() || !PC.Get_DrawTargetsForPlayer())
        { return; }
        auto LocalWorld = GetWorld();
        if (ck::Is_NOT_Valid(LocalWorld) || PC.GetWorld() != LocalWorld)
        { return; }
        auto State = CkRotateTowardsShared::Get_State(LocalWorld);
        if (ck::Is_NOT_Valid(State) || State.GetWorld() != LocalWorld)
        { return; }

        FCkRotateTowardsGym_Snapshot Sample;
        if (!PC.TryGet_TargetOverlaySample(Sample) || !Sample.Ready || Sample.Failed ||
            Sample.Turrets.Num() != 7 || !State.Snapshot.Ready || State.Snapshot.Failed ||
            State.Snapshot.Generation != Sample.Generation || Sample.Sequence != State.Snapshot.Sequence)
        { return; }

        for (int32 i=0;i<Sample.Turrets.Num();++i)
        {
            auto Row=Sample.Turrets[i]; FTransform Pose; FVector Target, Rest;
            if (!PC.TryGet_TurretPresentation(Sample.Generation,i,Pose,Target,Rest)) { continue; }
            const FVector Location = Pose.GetTranslation();
            const FLinearColor Color = !Row.Enabled ? FLinearColor::Red :
                Row.AtTarget ? FLinearColor::Green : FLinearColor::Yellow;
            DrawProjectedArrow(PC, Location, Location + Pose.Rotator().Vector() * 60.0, Color);
            DrawProjectedSegment(PC, Location, Target, Color);
            DrawProjectedArrow(PC, Location, Location + Row.Desired.Vector() * 60.0, FLinearColor::White);

            if (Row.Clamped)
            {
                const FVector Direction = (Rest - Location).GetSafeNormal();
                const float RestYaw = Direction.Rotation().Yaw;
                DrawProjectedSegment(PC, Location, Location + Direction * 80.0, FLinearColor::Blue);
                DrawProjectedSegment(PC, Location,
                    Location + FRotator(0.0, RestYaw - 45.0, 0.0).Vector() * 80.0,
                    FLinearColor(0.0, 1.0, 1.0, 1.0));
                DrawProjectedSegment(PC, Location,
                    Location + FRotator(0.0, RestYaw + 45.0, 0.0).Vector() * 80.0,
                    FLinearColor(0.0, 1.0, 1.0, 1.0));
            }

            FVector2D Label;
            if (TryProject(PC, Location + FVector(0.0, 0.0, 20.0), Label))
            {
                DrawText(f"Des ({Row.Desired.Pitch :.1},{Row.Desired.Yaw :.1},{Row.Desired.Roll :.1}) Rem ({Row.Remaining.Pitch :.1},{Row.Remaining.Yaw :.1},{Row.Remaining.Roll :.1})",
                    Color, Label.X, Label.Y, nullptr, 0.85, false);
            }
        }
    }

    private bool TryProject(ACk_RotateTowardsGym_PlayerController PC, FVector WorldPoint, FVector2D&out ScreenPoint)
    {
        ScreenPoint = FVector2D();
        if (ck::Is_NOT_Valid(PC) || !PC.ProjectWorldLocationToScreen(WorldPoint, ScreenPoint, true))
        { return false; }
        return Math::IsFinite(ScreenPoint.X) && Math::IsFinite(ScreenPoint.Y);
    }

    private void DrawProjectedSegment(ACk_RotateTowardsGym_PlayerController PC,
        FVector From, FVector To, FLinearColor Color)
    {
        FVector2D A;
        FVector2D B;
        if (!TryProject(PC, From, A) || !TryProject(PC, To, B))
        { return; }
        DrawLine(A.X, A.Y, B.X, B.Y, Color, 1.0);
    }

    private void DrawProjectedArrow(ACk_RotateTowardsGym_PlayerController PC,
        FVector From, FVector To, FLinearColor Color)
    {
        FVector2D A;
        FVector2D B;
        if (!TryProject(PC, From, A) || !TryProject(PC, To, B))
        { return; }
        const float DX = B.X - A.X;
        const float DY = B.Y - A.Y;
        const float LengthSquared = DX * DX + DY * DY;
        if (!Math::IsFinite(LengthSquared) || LengthSquared <= 0.0001)
        { return; }
        const float InvLength = 1.0 / Math::Sqrt(LengthSquared);
        const float UnitX = DX * InvLength;
        const float UnitY = DY * InvLength;
        DrawLine(A.X, A.Y, B.X, B.Y, Color, 2.0);
        DrawLine(B.X, B.Y, B.X - UnitX * 10.0 + UnitY * 5.0,
            B.Y - UnitY * 10.0 - UnitX * 5.0, Color, 2.0);
        DrawLine(B.X, B.Y, B.X - UnitX * 10.0 - UnitY * 5.0,
            B.Y - UnitY * 10.0 + UnitX * 5.0, Color, 2.0);
    }
}

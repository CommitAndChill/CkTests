// Language=angelscript
// Value-only presentation wire. Native visual rows contain only sampled presentation
// values, including actual entity-local outline lines; no handles or UObjects.
UENUM()
enum ECkCueGym_Lifecycle
{
    Initializing,
    Active,
    Retiring,
    Failed
}

UENUM()
enum ECkCueGym_EventAdmission
{
    Pending,
    Accepted,
    Rejected
}

USTRUCT()
struct FCkCueGym_Snapshot
{
    UPROPERTY() int32 CatalogVersion = 1;
    UPROPERTY() ECkCueGym_Lifecycle Lifecycle = ECkCueGym_Lifecycle::Initializing;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() TArray<int32> RoleEpochs;
    UPROPERTY() int64 Sequence = 0;
    UPROPERTY() int64 FrameSerial = 0;
    // -2 means no retirement; -1 all roles; 0..5 selected retiring role.
    UPROPERTY() int32 ResetRole = -2;
    UPROPERTY() int64 TransientRequestedSerial = 0;
    UPROPERTY() int32 TransientEpochStartGeneration = 0;
    UPROPERTY() TArray<FString> Bodies;
    UPROPERTY() TArray<FString> Instructions;
    // The complete currently live authority visual set; never a capped prefix.
    UPROPERTY() int32 VisualRowCount = 0;
    UPROPERTY() TArray<FCkCueGym_SourceVisualRow> VisualRows;
    UPROPERTY() FString FailureReason;
}

namespace CkCueGymWire
{
    // Role indices must match CkCueGymCatalog::Get_Tag exactly:
    // Lifetime, Concurrency, OwnerValidation, Restart, Transient,
    // OwnerDestruction. Transient has CueRelay events, never passive rows.
    const int32 CatalogVersion = 1;
    const int32 RoleCount = 6;
    const int32 TransientRole = 4;
    const int32 AllRoles = -1;

    bool Get_IsValidVisualRow(FCkCueGym_SourceVisualRow Row)
    {
        if (Row.Key <= 0 || Row.Role < 0 || Row.Role >= RoleCount ||
            Row.Role == TransientRole ||
            (Row.Kind != ECkCueGym_SourceVisualKind::Sphere &&
             Row.Kind != ECkCueGym_SourceVisualKind::Box) ||
            !Row.Pose.IsValid() || Row.Pose.ContainsNaN() ||
            !Math::IsFinite(Row.Color.R) || !Math::IsFinite(Row.Color.G) ||
            !Math::IsFinite(Row.Color.B) || !Math::IsFinite(Row.Color.A) ||
            !Math::IsFinite(Row.LineThickness) || Row.LineThickness < 0.0f ||
            !Math::IsFinite(Row.Duration) ||
            (Row.RenderMode != ECk_Pmg_RenderMode::SingleSided &&
             Row.RenderMode != ECk_Pmg_RenderMode::DoubleSided &&
             Row.RenderMode != ECk_Pmg_RenderMode::Hidden) ||
            Row.VisibleByRenderMode !=
                (Row.RenderMode != ECk_Pmg_RenderMode::Hidden) ||
            (Row.Axis != ECk_Plane_Axis::XY &&
             Row.Axis != ECk_Plane_Axis::XZ &&
             Row.Axis != ECk_Plane_Axis::YZ))
        { return false; }
        for (auto Line : Row.Lines)
        {
            if (Line.Start.ContainsNaN() || Line.End.ContainsNaN() ||
                (Line.End - Line.Start).SizeSquared() <= 0.000001 ||
                !Math::IsFinite(Line.Color.R) || !Math::IsFinite(Line.Color.G) ||
                !Math::IsFinite(Line.Color.B) || !Math::IsFinite(Line.Color.A) ||
                !Math::IsFinite(Line.Thickness) || Line.Thickness <= 0.0f)
            { return false; }
        }
        if (Row.Kind == ECkCueGym_SourceVisualKind::Sphere)
        {
            return Math::IsFinite(Row.Radius) && Row.Radius > 0.0f &&
                Row.Segments >= 3 && Row.Rings >= 2 &&
                Row.Extent.X == 0.0 && Row.Extent.Y == 0.0 &&
                Row.Extent.Z == 0.0;
        }
        return Row.Radius == 0.0f && Row.Segments == 0 && Row.Rings == 0 &&
            !Row.Extent.ContainsNaN() && Row.Extent.X > 0.0 &&
            Row.Extent.Y > 0.0 && Row.Extent.Z > 0.0;
    }

    bool Get_IsValidSnapshot(FCkCueGym_Snapshot Value)
    {
        if (Value.CatalogVersion != CatalogVersion ||
            Value.Generation < 0 || Value.Sequence < 0 ||
            Value.FrameSerial < 0 || Value.TransientRequestedSerial < 0 ||
            Value.VisualRowCount < 0) { return false; }
        if (Value.Lifecycle == ECkCueGym_Lifecycle::Retiring)
        {
            if (Value.ResetRole != -1 && (Value.ResetRole < 0 || Value.ResetRole >= RoleCount))
            { return false; }
        }
        else if (Value.ResetRole != -2) { return false; }
        const bool IsDefault = Value.Lifecycle == ECkCueGym_Lifecycle::Initializing &&
            Value.Generation == 0 && Value.Sequence == 0;
        if (IsDefault)
        {
            return Value.RoleEpochs.IsEmpty() && Value.FrameSerial == 0 &&
                Value.TransientRequestedSerial == 0 && Value.TransientEpochStartGeneration == 0 &&
                Value.Bodies.IsEmpty() && Value.Instructions.IsEmpty() &&
                Value.VisualRowCount == 0 && Value.VisualRows.IsEmpty() &&
                Value.FailureReason.IsEmpty();
        }
        if (Value.Generation <= 0 || Value.Sequence <= 0 ||
            Value.TransientEpochStartGeneration <= 0 ||
            Value.TransientEpochStartGeneration > Value.Generation ||
            Value.RoleEpochs.Num() != RoleCount) { return false; }
        for (int32 Epoch : Value.RoleEpochs)
        { if (Epoch <= 0) { return false; } }
        if (Value.Lifecycle == ECkCueGym_Lifecycle::Active)
        {
            if (Value.FrameSerial <= 0 || !Value.FailureReason.IsEmpty() ||
                Value.Bodies.Num() != RoleCount ||
                Value.Instructions.Num() != RoleCount ||
                Value.VisualRowCount <= 0 ||
                Value.VisualRowCount != Value.VisualRows.Num()) { return false; }
            TArray<FCkCueGym_SourceVisualRow> ExpandedRows;
            if (!UCkCueGym_SourceVisuals::TryExpand_SourceVisualRows(
                Value.VisualRows, ExpandedRows)) { return false; }
            for (int32 I = 0; I < ExpandedRows.Num(); ++I)
            {
                if (!Get_IsValidVisualRow(ExpandedRows[I])) { return false; }
                for (int32 J = 0; J < I; ++J)
                {
                    if (ExpandedRows[I].Key == ExpandedRows[J].Key)
                    { return false; }
                }
            }
            return true;
        }
        if (Value.FrameSerial != 0 || !Value.Bodies.IsEmpty() ||
            !Value.Instructions.IsEmpty() || Value.VisualRowCount != 0 ||
            !Value.VisualRows.IsEmpty())
        { return false; }
        if (Value.Lifecycle == ECkCueGym_Lifecycle::Initializing ||
            Value.Lifecycle == ECkCueGym_Lifecycle::Retiring)
        { return Value.FailureReason.IsEmpty(); }
        if (Value.Lifecycle == ECkCueGym_Lifecycle::Failed)
        { return !Value.FailureReason.IsEmpty(); }
        return false;
    }

    // Only the authority uses this adjacent-publication check. ResetRole is
    // AllRoles for R, otherwise the selected station role 0..5.
    bool Get_IsValidPublicationAdvance(FCkCueGym_Snapshot Previous,
        FCkCueGym_Snapshot Next, int32 ResetRole = AllRoles)
    {
        if (!Get_IsValidSnapshot(Previous) || !Get_IsValidSnapshot(Next) ||
            Previous.Lifecycle == ECkCueGym_Lifecycle::Failed ||
            Previous.Sequence >= 9223372036854775807 ||
            Next.Sequence != Previous.Sequence + 1) { return false; }
        if (Previous.Generation == 0)
        {
            if (Next.Generation != 1 ||
                Next.Lifecycle != ECkCueGym_Lifecycle::Initializing)
            { return false; }
            for (int32 Epoch : Next.RoleEpochs)
            { if (Epoch != 1) { return false; } }
            return Next.TransientEpochStartGeneration == 1 && Next.TransientRequestedSerial == 0;
        }
        if (Previous.Lifecycle == ECkCueGym_Lifecycle::Retiring &&
            Next.Lifecycle == ECkCueGym_Lifecycle::Initializing)
        {
            if (Previous.Generation >= 2147483647 ||
                Next.Generation != Previous.Generation + 1 ||
                (ResetRole != AllRoles &&
                 (ResetRole < 0 || ResetRole >= RoleCount)))
            { return false; }
            for (int32 I = 0; I < RoleCount; ++I)
            {
                const bool Increment = ResetRole == AllRoles || ResetRole == I;
                if (Increment && Previous.RoleEpochs[I] >= 2147483647)
                { return false; }
                if (Next.RoleEpochs[I] != Previous.RoleEpochs[I] +
                    (Increment ? 1 : 0)) { return false; }
            }
            const bool TransientReset = ResetRole == AllRoles || ResetRole == TransientRole;
            return Next.TransientEpochStartGeneration == (TransientReset ?
                Next.Generation : Previous.TransientEpochStartGeneration) &&
                (TransientReset ? Next.TransientRequestedSerial == 0 :
                 Next.TransientRequestedSerial >= Previous.TransientRequestedSerial);
        }
        if (Next.RoleEpochs[TransientRole] == Previous.RoleEpochs[TransientRole] &&
            Next.TransientRequestedSerial < Previous.TransientRequestedSerial)
        { return false; }
        if (Next.Generation != Previous.Generation) { return false; }
        if (Next.TransientEpochStartGeneration != Previous.TransientEpochStartGeneration)
        { return false; }
        for (int32 I = 0; I < RoleCount; ++I)
        { if (Next.RoleEpochs[I] != Previous.RoleEpochs[I]) { return false; } }
        if (Next.Lifecycle == ECkCueGym_Lifecycle::Failed) { return true; }
        if (Previous.Lifecycle == ECkCueGym_Lifecycle::Initializing)
        { return Next.Lifecycle == ECkCueGym_Lifecycle::Active; }
        if (Previous.Lifecycle == ECkCueGym_Lifecycle::Active)
        {
            return Next.Lifecycle == ECkCueGym_Lifecycle::Retiring ||
                (Next.Lifecycle == ECkCueGym_Lifecycle::Active &&
                 Next.FrameSerial > Previous.FrameSerial);
        }
        return false;
    }

    // Clients can skip publications and first encounter an Active late-join
    // snapshot. They still reject stale sequence/generation/role epochs.
    bool Get_IsValidClientAdvance(FCkCueGym_Snapshot Previous,
        FCkCueGym_Snapshot Next)
    {
        if (!Get_IsValidSnapshot(Previous) || !Get_IsValidSnapshot(Next) ||
            Previous.Lifecycle == ECkCueGym_Lifecycle::Failed ||
            Next.Generation == 0) { return false; }
        if (Previous.Generation == 0) { return true; }
        if ((Next.RoleEpochs[TransientRole] == Previous.RoleEpochs[TransientRole] &&
             Next.TransientEpochStartGeneration != Previous.TransientEpochStartGeneration) ||
            (Next.RoleEpochs[TransientRole] > Previous.RoleEpochs[TransientRole] &&
             Next.TransientEpochStartGeneration <= Previous.Generation))
        { return false; }
        if (Next.RoleEpochs[TransientRole] == Previous.RoleEpochs[TransientRole] &&
            Next.TransientRequestedSerial < Previous.TransientRequestedSerial)
        { return false; }
        if (Next.Sequence <= Previous.Sequence ||
            Next.Generation < Previous.Generation) { return false; }
        if (Next.Generation == Previous.Generation)
        {
            for (int32 I = 0; I < RoleCount; ++I)
            { if (Next.RoleEpochs[I] != Previous.RoleEpochs[I]) { return false; } }
            if (Previous.Lifecycle == ECkCueGym_Lifecycle::Initializing &&
                Next.Lifecycle == ECkCueGym_Lifecycle::Initializing)
            { return false; }
            if (Previous.Lifecycle == ECkCueGym_Lifecycle::Retiring &&
                Next.Lifecycle != ECkCueGym_Lifecycle::Failed) { return false; }
            if (Previous.Lifecycle == ECkCueGym_Lifecycle::Active &&
                Next.Lifecycle == ECkCueGym_Lifecycle::Initializing)
            { return false; }
            if (Previous.Lifecycle == ECkCueGym_Lifecycle::Active &&
                Next.Lifecycle == ECkCueGym_Lifecycle::Active &&
                Next.FrameSerial <= Previous.FrameSerial) { return false; }
            return true;
        }
        bool AnyAdvanced = false;
        for (int32 I = 0; I < RoleCount; ++I)
        {
            if (Next.RoleEpochs[I] < Previous.RoleEpochs[I]) { return false; }
            AnyAdvanced = AnyAdvanced ||
                Next.RoleEpochs[I] > Previous.RoleEpochs[I];
        }
        return AnyAdvanced;
    }
}

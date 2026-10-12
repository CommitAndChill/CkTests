// Language=angelscript
// Value wire schema, independent of source ECS and visual UObjects.
UENUM()
enum ECkUnrealComponentGym_Lifecycle
{
    Initializing,
    Active,
    Retiring,
    Failed
}

USTRUCT()
struct FCkUnrealComponentGym_Snapshot
{
    UPROPERTY() int32 CatalogVersion = 1;
    UPROPERTY() ECkUnrealComponentGym_Lifecycle Lifecycle =
        ECkUnrealComponentGym_Lifecycle::Initializing;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int64 Sequence = 0;
    UPROPERTY() int64 FrameSerial = 0;
    UPROPERTY() TArray<FTransform> ComponentWorldPoses;
    UPROPERTY() TArray<FString> Bodies;
    UPROPERTY() TArray<FString> Instructions;
    UPROPERTY() FString FailureReason;
}

namespace CkUnrealComponentGymShared
{
    const int32 CatalogVersion = 1;
    const int32 RoleCount = 8;

    bool Get_IsSameSnapshot(FCkUnrealComponentGym_Snapshot A,
        FCkUnrealComponentGym_Snapshot B)
    {
        if (!Get_IsValidSnapshot(A) || !Get_IsValidSnapshot(B) ||
            A.CatalogVersion != B.CatalogVersion || A.Lifecycle != B.Lifecycle ||
            A.Generation != B.Generation || A.Sequence != B.Sequence ||
            A.FrameSerial != B.FrameSerial || A.FailureReason != B.FailureReason ||
            A.ComponentWorldPoses.Num() != B.ComponentWorldPoses.Num() ||
            A.Bodies.Num() != B.Bodies.Num() || A.Instructions.Num() != B.Instructions.Num())
        { return false; }
        for (int32 I = 0; I < A.ComponentWorldPoses.Num(); ++I)
        { if (!A.ComponentWorldPoses[I].Equals(B.ComponentWorldPoses[I], 0.0)) { return false; } }
        for (int32 I = 0; I < A.Bodies.Num(); ++I)
        { if (A.Bodies[I] != B.Bodies[I]) { return false; } }
        for (int32 I = 0; I < A.Instructions.Num(); ++I)
        { if (A.Instructions[I] != B.Instructions[I]) { return false; } }
        return true;
    }

    // Only the authority validates adjacent publications. Clients may receive
    // coalesced sequences and a late-join Active frame.
    bool Get_IsValidPublicationAdvance(FCkUnrealComponentGym_Snapshot Previous,
        FCkUnrealComponentGym_Snapshot Next)
    {
        if (!Get_IsValidSnapshot(Previous) || !Get_IsValidSnapshot(Next) ||
            Previous.Lifecycle == ECkUnrealComponentGym_Lifecycle::Failed ||
            Previous.Sequence >= 9223372036854775807 ||
            Next.Sequence != Previous.Sequence + 1) { return false; }
        if (Previous.Generation == 0)
        {
            return Next.Generation == 1 &&
                Next.Lifecycle == ECkUnrealComponentGym_Lifecycle::Initializing;
        }
        if (Next.Lifecycle == ECkUnrealComponentGym_Lifecycle::Failed)
        { return Next.Generation == Previous.Generation; }
        if (Next.Lifecycle == ECkUnrealComponentGym_Lifecycle::Initializing)
        {
            return Previous.Lifecycle == ECkUnrealComponentGym_Lifecycle::Active &&
                Previous.Generation < 2147483647 &&
                Next.Generation == Previous.Generation + 1;
        }
        if (Next.Lifecycle != ECkUnrealComponentGym_Lifecycle::Active ||
            Next.Generation != Previous.Generation) { return false; }
        return (Previous.Lifecycle == ECkUnrealComponentGym_Lifecycle::Initializing &&
                Next.FrameSerial > 0) ||
            (Previous.Lifecycle == ECkUnrealComponentGym_Lifecycle::Active &&
                Next.FrameSerial > Previous.FrameSerial);
    }

    // Validates a single received value, including a late-join or coalesced one.
    bool Get_IsValidSnapshot(FCkUnrealComponentGym_Snapshot Value)
    {
        if (Value.CatalogVersion != CatalogVersion ||
            Value.Generation < 0 || Value.Sequence < 0 || Value.FrameSerial < 0)
        { return false; }
        if (Value.Lifecycle == ECkUnrealComponentGym_Lifecycle::Active)
        {
            if (Value.Generation <= 0 || Value.Sequence <= 0 ||
                Value.FrameSerial <= 0 || !Value.FailureReason.IsEmpty() ||
                Value.ComponentWorldPoses.Num() != RoleCount ||
                Value.Bodies.Num() != RoleCount ||
                Value.Instructions.Num() != RoleCount)
            { return false; }
            for (auto Pose : Value.ComponentWorldPoses)
            { if (!Pose.IsValid() || Pose.ContainsNaN()) { return false; } }
            return true;
        }
        if (Value.FrameSerial != 0 || !Value.ComponentWorldPoses.IsEmpty() ||
            !Value.Bodies.IsEmpty() || !Value.Instructions.IsEmpty())
        { return false; }
        if (Value.Lifecycle == ECkUnrealComponentGym_Lifecycle::Initializing)
        {
            return Value.FailureReason.IsEmpty() &&
                ((Value.Generation == 0 && Value.Sequence == 0) ||
                 (Value.Generation > 0 && Value.Sequence > 0));
        }
        if (Value.Generation <= 0 || Value.Sequence <= 0) { return false; }
        if (Value.Lifecycle == ECkUnrealComponentGym_Lifecycle::Retiring)
        { return Value.FailureReason.IsEmpty(); }
        if (Value.Lifecycle == ECkUnrealComponentGym_Lifecycle::Failed)
        { return !Value.FailureReason.IsEmpty(); }
        return false;
    }
}

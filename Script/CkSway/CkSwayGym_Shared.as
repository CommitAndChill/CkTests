// Language=angelscript

UENUM()
enum ECkSwayGym_Lifecycle
{
    Initializing,
    Active,
    Retiring,
    Failed
}

// Lifecycle and remote diagnostic telemetry only. Visible poses use Transform replication.
// Handles, request receipts, and component pointers never cross the network.
USTRUCT()
struct FCkSwayGym_Snapshot
{
    UPROPERTY() int32 CatalogVersion = 1;
    UPROPERTY() ECkSwayGym_Lifecycle Lifecycle = ECkSwayGym_Lifecycle::Initializing;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int64 Sequence = 0;
    UPROPERTY() int64 FrameSerial = 0;
    UPROPERTY() TArray<float32> SwayYawDegrees;
    UPROPERTY() TArray<float32> SwayXCm;
    UPROPERTY() TArray<bool> SwaySettled;
    UPROPERTY() FString FailureReason;
}

namespace CkSwayGymShared
{
    const int32 HeadCount = 3;
    const int32 CubeCount = 7;
    const int32 SwayCount = 5;

    bool Get_IsValidSnapshot(FCkSwayGym_Snapshot Value)
    {
        if (Value.CatalogVersion != 1 || Value.Generation < 0 ||
            Value.Sequence < 0 || Value.FrameSerial < 0) { return false; }
        if (Value.Lifecycle == ECkSwayGym_Lifecycle::Active)
        {
            if (Value.Generation <= 0 || Value.Sequence <= 0 ||
                Value.FrameSerial <= 0 || !Value.FailureReason.IsEmpty() ||
                Value.SwayYawDegrees.Num() != SwayCount ||
                Value.SwayXCm.Num() != SwayCount ||
                Value.SwaySettled.Num() != SwayCount) { return false; }
            for (int32 I = 0; I < SwayCount; ++I)
            {
                if (!Math::IsFinite(Value.SwayYawDegrees[I]) ||
                    !Math::IsFinite(Value.SwayXCm[I])) { return false; }
            }
            return true;
        }
        if (Value.FrameSerial != 0 || !Value.SwayYawDegrees.IsEmpty() ||
            !Value.SwayXCm.IsEmpty() || !Value.SwaySettled.IsEmpty())
        { return false; }
        if (Value.Lifecycle == ECkSwayGym_Lifecycle::Initializing)
        {
            return Value.FailureReason.IsEmpty() &&
                ((Value.Generation == 0 && Value.Sequence == 0) ||
                 (Value.Generation > 0 && Value.Sequence > 0));
        }
        if (Value.Generation <= 0 || Value.Sequence <= 0) { return false; }
        if (Value.Lifecycle == ECkSwayGym_Lifecycle::Retiring)
        { return Value.FailureReason.IsEmpty(); }
        if (Value.Lifecycle == ECkSwayGym_Lifecycle::Failed)
        { return !Value.FailureReason.IsEmpty(); }
        return false;
    }

    // Source publisher only. Client updates can coalesce or begin at Active.
    bool Get_IsValidPublicationAdvance(FCkSwayGym_Snapshot Previous,
        FCkSwayGym_Snapshot Next)
    {
        if (!Get_IsValidSnapshot(Previous) || !Get_IsValidSnapshot(Next) ||
            Next.Sequence <= Previous.Sequence ||
            Next.Sequence - Previous.Sequence != 1 ||
            Previous.Lifecycle == ECkSwayGym_Lifecycle::Failed)
        { return false; }
        if (Previous.Generation == 0)
        {
            return Previous.Sequence == 0 &&
                Previous.Lifecycle == ECkSwayGym_Lifecycle::Initializing &&
                Next.Generation == 1 &&
                Next.Lifecycle == ECkSwayGym_Lifecycle::Initializing;
        }
        if (Previous.Generation < 2147483647 &&
            Next.Generation == Previous.Generation + 1)
        {
            return Previous.Lifecycle == ECkSwayGym_Lifecycle::Retiring &&
                Next.Lifecycle == ECkSwayGym_Lifecycle::Initializing;
        }
        if (Next.Generation != Previous.Generation) { return false; }
        if (Previous.Lifecycle == ECkSwayGym_Lifecycle::Initializing)
        {
            return Next.Lifecycle == ECkSwayGym_Lifecycle::Initializing ||
                Next.Lifecycle == ECkSwayGym_Lifecycle::Active ||
                Next.Lifecycle == ECkSwayGym_Lifecycle::Failed;
        }
        if (Previous.Lifecycle == ECkSwayGym_Lifecycle::Active)
        {
            if (Next.Lifecycle == ECkSwayGym_Lifecycle::Active)
            { return Next.FrameSerial > Previous.FrameSerial; }
            return Next.Lifecycle == ECkSwayGym_Lifecycle::Retiring ||
                Next.Lifecycle == ECkSwayGym_Lifecycle::Failed;
        }
        if (Previous.Lifecycle == ECkSwayGym_Lifecycle::Retiring)
        {
            return Next.Lifecycle == ECkSwayGym_Lifecycle::Retiring ||
                Next.Lifecycle == ECkSwayGym_Lifecycle::Failed;
        }
        return false;
    }
}

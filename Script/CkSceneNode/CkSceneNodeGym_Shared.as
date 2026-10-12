// Value-only wire contract for one shared SceneNode authority generation.
// Root transforms and local offsets are source-only formatter inputs, never wire fields.
UENUM()
enum ECkSceneNodeGym_Lifecycle
{
    Initializing,
    Active,
    Retiring,
    Failed
}

USTRUCT()
struct FCkSceneNodeGym_Snapshot
{
    UPROPERTY() int32 CatalogVersion = 1;
    UPROPERTY() ECkSceneNodeGym_Lifecycle Lifecycle = ECkSceneNodeGym_Lifecycle::Initializing;
    UPROPERTY() int32 Generation = 0;
    UPROPERTY() int64 Sequence = 0;
    UPROPERTY() int64 FrameSerial = 0;

    UPROPERTY() TArray<FString> BaseTitles;
    UPROPERTY() TArray<FString> Bodies;
    UPROPERTY() TArray<FString> Instructions;
    UPROPERTY() FString FailureReason;
}

namespace CkSceneNodeGymShared
{
    const int32 CatalogVersion = 1;
    const int32 VisualCount = 17;
    const int32 StationCount = 6;
    const int32 NodeCount = 11;
    const float32 OrbitRadius = 150.0f;
    const float32 OrbitSpeed = 1.5f;
    const float32 ParentRotationSpeed = 30.0f;

    FString Get_BaseTitle(int32 InSlot)
    {
        if (InSlot == 0) { return "ParentChild"; }
        if (InSlot == 1) { return "OffsetUpdates"; }
        if (InSlot == 2) { return "MultipleChildren"; }
        if (InSlot == 3) { return "Hierarchy"; }
        if (InSlot == 4) { return "PropagateOnly"; }
        if (InSlot == 5) { return "AnchorOffset"; }
        return "";
    }

    bool Get_IsValidSnapshot(FCkSceneNodeGym_Snapshot InSnapshot)
    {
        if (InSnapshot.CatalogVersion != CatalogVersion ||
            InSnapshot.Generation < 0 || InSnapshot.Sequence < 0 ||
            InSnapshot.FrameSerial < 0) { return false; }

        if (InSnapshot.Lifecycle == ECkSceneNodeGym_Lifecycle::Active)
        {
            if (InSnapshot.Generation <= 0 || InSnapshot.Sequence <= 0 ||
                InSnapshot.FrameSerial <= 0 || !InSnapshot.FailureReason.IsEmpty() ||

                InSnapshot.BaseTitles.Num() != StationCount ||
                InSnapshot.Bodies.Num() != StationCount ||
                InSnapshot.Instructions.Num() != StationCount) { return false; }
            for (int32 Slot = 0; Slot < StationCount; ++Slot)
            {
                if (InSnapshot.BaseTitles[Slot] != Get_BaseTitle(Slot)) { return false; }
                if (Slot < 4 && (InSnapshot.Bodies[Slot].IsEmpty() ||
                    InSnapshot.Instructions[Slot].IsEmpty())) { return false; }
                if (Slot >= 4 &&
                    (InSnapshot.Bodies[Slot] !=
                        CkSceneNodeGymCatalog::Get_StaticBody(Slot) ||
                     !InSnapshot.Instructions[Slot].IsEmpty())) { return false; }
            }
            return true;
        }

        if (InSnapshot.FrameSerial != 0 ||
            !InSnapshot.BaseTitles.IsEmpty() || !InSnapshot.Bodies.IsEmpty() ||
            !InSnapshot.Instructions.IsEmpty()) { return false; }
        if (InSnapshot.Lifecycle == ECkSceneNodeGym_Lifecycle::Initializing)
        {
            return InSnapshot.FailureReason.IsEmpty() &&
                ((InSnapshot.Generation == 0 && InSnapshot.Sequence == 0) ||
                 (InSnapshot.Generation > 0 && InSnapshot.Sequence > 0));
        }
        if (InSnapshot.Generation <= 0 || InSnapshot.Sequence <= 0) { return false; }
        if (InSnapshot.Lifecycle == ECkSceneNodeGym_Lifecycle::Retiring)
        { return InSnapshot.FailureReason.IsEmpty(); }
        if (InSnapshot.Lifecycle == ECkSceneNodeGym_Lifecycle::Failed)
        { return !InSnapshot.FailureReason.IsEmpty(); }
        return false;
    }

    // Source publisher only: replication may skip states, and late join may first
    // observe Active generation >1. Clients validate each received value instead.
    bool Get_IsValidPublicationAdvance(FCkSceneNodeGym_Snapshot InPrevious,
        FCkSceneNodeGym_Snapshot InNext)
    {
        if (!Get_IsValidSnapshot(InPrevious) || !Get_IsValidSnapshot(InNext) ||
            InNext.Sequence <= InPrevious.Sequence) { return false; }
        // Both sequences are nonnegative and ordered, so subtraction is representable.
        if (InNext.Sequence - InPrevious.Sequence != 1) { return false; }
        if (InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Failed) { return false; }
        if (InPrevious.Generation == 0)
        {
            return InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Initializing &&
                InPrevious.Sequence == 0 && InNext.Generation == 1 &&
                InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Initializing;
        }
        if (InPrevious.Generation < 2147483647 &&
            InNext.Generation == InPrevious.Generation + 1)
        {
            return InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Retiring &&
                InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Initializing;
        }
        if (InNext.Generation != InPrevious.Generation) { return false; }
        if (InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Initializing)
        {
            return InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Initializing ||
                InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Active ||
                InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Failed;
        }
        if (InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Active)
        {
            if (InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Active)
            { return InNext.FrameSerial > InPrevious.FrameSerial; }
            return InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Retiring ||
                InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Failed;
        }
        if (InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Retiring)
        {
            return InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Retiring ||
                InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Failed;
        }
        return false;
    }

    // Formats four live displays and retains the other two authored static
    // descriptions from the same catalog used to construct their panels.
    // The caller appends its own local network-role suffix to BaseTitles.
    bool TryFormatDisplayRows(FCkSceneNodeGym_Frame InFrame,
        FCkSceneNodeGym_SourceDisplayData InSource,
        TArray<FString>&out OutBaseTitles, TArray<FString>&out OutBodies,
        TArray<FString>&out OutInstructions)
    {
        OutBaseTitles.Empty();
        OutBodies.Empty();
        OutInstructions.Empty();
        if (InFrame.Generation <= 0 || InFrame.FrameSerial <= 0 ||
            InFrame.WorldPoses.Num() != VisualCount ||
            InSource.Generation != InFrame.Generation ||
            InSource.FrameSerial != InFrame.FrameSerial ||
            InSource.RootWorldTransforms.Num() != StationCount ||
            InSource.NodeLocalOffsets.Num() != NodeCount) { return false; }
        for (auto Pose : InFrame.WorldPoses)
        { if (!Pose.IsValid() || Pose.ContainsNaN()) { return false; } }
        for (auto Pose : InSource.RootWorldTransforms)
        { if (!Pose.IsValid() || Pose.ContainsNaN()) { return false; } }
        for (auto Offset : InSource.NodeLocalOffsets)
        { if (!Offset.IsValid() || Offset.ContainsNaN()) { return false; } }

        auto Titles = TArray<FString>();
        auto Bodies = TArray<FString>();
        auto Instructions = TArray<FString>();
        for (int32 Slot = 0; Slot < StationCount; ++Slot)
        {
            Titles.Add(Get_BaseTitle(Slot));
            Bodies.Add("");
            Instructions.Add("");
        }

        auto ParentLoc = InSource.RootWorldTransforms[0].GetLocation();
        auto ChildOffsetLoc = InSource.NodeLocalOffsets[0].GetLocation();
        auto Body = f"Parent Location:\n";
        Body = f"{Body}  X: {ParentLoc.X}  Y: {ParentLoc.Y}  Z: {ParentLoc.Z}\n\n";
        Body = f"{Body}Child Offset:\n";
        Body = f"{Body}  X: {ChildOffsetLoc.X}  Y: {ChildOffsetLoc.Y}  Z: {ChildOffsetLoc.Z}\n\n";
        Body = f"{Body}Orbit Radius: {OrbitRadius}\n";
        Bodies[0] = f"{Body}Orbit Speed: {OrbitSpeed}";
        Instructions[0] = "Child scene node orbits around\n"
            + "parent entity.\n"
            + "Uses utils_scene_node::Create()\n"
            + "and Request_UpdateOffset_Location().\n"
            + "Offset updated each frame with\n"
            + "sin/cos circular path.";

        auto Offset = InSource.NodeLocalOffsets[1];
        auto OffsetLoc = Offset.GetLocation();
        auto OffsetRot = Offset.GetRotation().Rotator();
        auto OffsetScale = Offset.GetScale3D();
        Body = f"Offset Location:\n";
        Body = f"{Body}  X: {OffsetLoc.X}  Y: {OffsetLoc.Y}  Z: {OffsetLoc.Z}\n\n";
        Body = f"{Body}Offset Rotation:\n";
        Body = f"{Body}  P: {OffsetRot.Pitch}  Y: {OffsetRot.Yaw}  R: {OffsetRot.Roll}\n\n";
        Body = f"{Body}Offset Scale:\n";
        Bodies[1] = f"{Body}  X: {OffsetScale.X}  Y: {OffsetScale.Y}  Z: {OffsetScale.Z}";
        Instructions[1] = "Animates location, rotation,\n"
            + "and scale offsets simultaneously.\n"
            + "Uses Request_UpdateOffset_Location\n"
            + "/Rotation/Scale().\n"
            + "All three components oscillate\n"
            + "via sine wave.";

        auto ParentRot = InSource.RootWorldTransforms[2].GetRotation().Rotator();
        Body = f"Parent Rotation:\n";
        Body = f"{Body}  Yaw: {ParentRot.Yaw}\n\n";
        Body = f"{Body}Children: 4\n\n";
        for (int32 Index = 0; Index < 4; ++Index)
        {
            auto ChildLoc = InSource.NodeLocalOffsets[Index + 2].GetLocation();
            Body = f"{Body}Child {Index + 1}: ({ChildLoc.X}, {ChildLoc.Y}, {ChildLoc.Z})\n";
        }
        Bodies[2] = Body;
        Instructions[2] = "Parent rotates with 4 children\n"
            + "at cardinal offsets.\n"
            + "Children follow parent\n"
            + "automatically via hierarchy.\n"
            + "Rotation speed: " + f"{ParentRotationSpeed}" + " deg/s.";

        auto RootRot = InSource.RootWorldTransforms[3].GetRotation().Rotator();
        auto HierarchyChildOffset = InSource.NodeLocalOffsets[6];
        auto HierarchyChildRot = HierarchyChildOffset.GetRotation().Rotator();
        auto HierarchyGrandchildOffset = InSource.NodeLocalOffsets[7];
        auto HierarchyGrandchildRot = HierarchyGrandchildOffset.GetRotation().Rotator();
        Body = f"Root Yaw: {RootRot.Yaw}\n\n";
        Body = f"{Body}Child Offset Yaw: {HierarchyChildRot.Yaw}\n";
        Body = f"{Body}Child Offset X: {HierarchyChildOffset.GetLocation().X}\n\n";
        Body = f"{Body}Grandchild Offset Yaw: {HierarchyGrandchildRot.Yaw}\n";
        Bodies[3] = f"{Body}Grandchild Offset X: {HierarchyGrandchildOffset.GetLocation().X}";
        Instructions[3] = "Nested chain:\n"
            + "root -> child -> grandchild (3 levels).\n"
            + "Root rotates, child adds its own rotation,\n"
            + "grandchild counter-rotates.\n"
            + "Shows compounding transforms\n"
            + "through hierarchy.";

        Bodies[4] = CkSceneNodeGymCatalog::Get_StaticBody(4);
        Bodies[5] = CkSceneNodeGymCatalog::Get_StaticBody(5);
        if (Bodies[4].IsEmpty() || Bodies[5].IsEmpty()) { return false; }

        OutBaseTitles = Titles;
        OutBodies = Bodies;
        OutInstructions = Instructions;
        return true;
    }

    // Candidate addition to CkSceneNodeGymShared. Source publication is strict;
    // receipt validation permits replication to omit intermediate publications.
    bool Get_IsValidReceivedAdvance(FCkSceneNodeGym_Snapshot InPrevious,
        FCkSceneNodeGym_Snapshot InNext)
    {
        if (!Get_IsValidSnapshot(InPrevious) || !Get_IsValidSnapshot(InNext) ||
            InNext.Generation <= 0 || InNext.Sequence <= InPrevious.Sequence)
        { return false; }
        // The default value has not yet observed a positive source publication.
        if (InPrevious.Generation == 0) { return true; }
        if (InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Failed ||
            InNext.Generation < InPrevious.Generation) { return false; }
        // A late update can skip Retiring, Initializing and complete generations.
        if (InNext.Generation > InPrevious.Generation) { return true; }
        if (InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Retiring)
        {
            return InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Retiring ||
                InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Failed;
        }
        if (InPrevious.Lifecycle == ECkSceneNodeGym_Lifecycle::Active)
        {
            if (InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Initializing)
            { return false; }
            if (InNext.Lifecycle == ECkSceneNodeGym_Lifecycle::Active)
            { return InNext.FrameSerial > InPrevious.FrameSerial; }
        }
        return true;
    }

    // Duplicate transport observations are no-ops only for identical values.
    // Transform equality here compares payload components, not geometric tolerance.
    bool Get_IsExactSnapshotValue(FCkSceneNodeGym_Snapshot A, FCkSceneNodeGym_Snapshot B)
    {
        if (!Get_IsValidSnapshot(A) || !Get_IsValidSnapshot(B) ||
            A.CatalogVersion != B.CatalogVersion || A.Lifecycle != B.Lifecycle ||
            A.Generation != B.Generation || A.Sequence != B.Sequence ||
            A.FrameSerial != B.FrameSerial || A.FailureReason != B.FailureReason ||

            A.BaseTitles.Num() != B.BaseTitles.Num() || A.Bodies.Num() != B.Bodies.Num() ||
            A.Instructions.Num() != B.Instructions.Num()) { return false; }
        for (int32 I = 0; I < A.BaseTitles.Num(); ++I)
        { if (A.BaseTitles[I] != B.BaseTitles[I]) { return false; } }
        for (int32 I = 0; I < A.Bodies.Num(); ++I)
        { if (A.Bodies[I] != B.Bodies[I]) { return false; } }
        for (int32 I = 0; I < A.Instructions.Num(); ++I)
        { if (A.Instructions[I] != B.Instructions[I]) { return false; } }
        return true;
    }

}

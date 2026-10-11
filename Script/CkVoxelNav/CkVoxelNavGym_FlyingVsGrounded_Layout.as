// Language=angelscript

namespace CkVoxelNavGym
{
    FString Get_BaseTag(int32 Index)
    { return Index == 0 ? "VoxelNavFlyingVsGrounded" : ""; }

    FTransform Get_StationTransform(int32 Index)
    {
        return Index == 0
            ? FTransform(FRotator(0.0, 180.0, 0.0), FVector(2600.0, 0.0, 0.0), FVector::OneVector)
            : FTransform::Identity;
    }

    FString Get_Title(int32 Index)
    { return Index == 0 ? "VoxelNav — Flying vs Grounded" : ""; }

    FString Get_Description(int32 Index)
    {
        if (Index != 0) { return ""; }
        return "Two agents use the same baked voxel volume. The FLYING one (cyan, Y=-400) starts 1000cm up and climbs over the 1200cm wall in its lane with a visible nose-up/nose-down pitch and holds its altitude to the goal.\n"
            + "The GROUNDED one (orange, Y=+400) starts on the level's navmesh and walks along the floor to its ground-level goal.\n"
            + "Fail signatures: flyer frozen at spawn = its displacement drain is missing; flyer sinking to the floor = it is still inside ConstrainToNavmesh's view; flyer rolling or banking = its rotation is being composed as a delta instead of written absolutely.\n"
            + "Panel [R] restart both agents\nPanel [T] send them back (return trip)";
    }

    bool IsValidFrame(const FCkVoxelNavGym_WireSnapshot&in Frame)
    { return UCk_VoxelNavGym_Runtime_UE::Get_IsValidFrame(Frame); }
}

#pragma once

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"

#include "Engine/World.h"

// --------------------------------------------------------------------------------------------------------------------

// A four-legged walker standing on a static Jolt floor. Only the named legs are bound as articulation limbs, so a
// test controls exactly which walking limbs an action can borrow while all four keep supporting the body.
namespace ck_test_procedural_walker_fixture
{
    const auto BodyLocation = FVector{0.0, 47000.0, 5060.0};
    const auto FloorLocation = FVector{0.0, 47000.0, 4975.0};

    struct FFloor
    {
        FCk_Handle Entity;
        FCk_Handle_JoltBody Body;
    };

    struct FWalker
    {
        FCk_Handle Root;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_ProceduralArticulation Subject;
    };

    inline auto
        LegId(
            int32 InIndex)
        -> FName
    {
        return FName{*FString::Printf(TEXT("Leg%d"), InIndex)};
    }

    inline auto
        AddFloor(
            UWorld* InWorld)
        -> FFloor
    {
        auto Floor = FFloor{};
        Floor.Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        UCk_Utils_Transform_UE::Add(Floor.Entity, FTransform{FloorLocation}, ECk_Replication::DoesNotReplicate);
        auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
        Shape.Set_HalfExtents(FVector{800.0, 800.0, 25.0});
        auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(ECk_MotionType::Static);
        Spec.Set_CollisionProfileName(TEXT("BlockAll"));
        Floor.Body = UCk_Utils_JoltBody_UE::Add(Floor.Entity, Spec);
        return Floor;
    }

    inline auto
        Get_IsFloorReady(
            const FFloor& InFloor)
        -> bool
    {
        return ck::IsValid(InFloor.Body) && UCk_Utils_JoltBody_UE::Get_IsBodyAdded(InFloor.Body);
    }

    inline auto
        AddWalker(
            UWorld* InWorld,
            const TArray<FName>& InBoundLegIds)
        -> FWalker
    {
        auto Walker = FWalker{};
        Walker.Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
        Walker.Body = UCk_Utils_Transform_UE::Add(Walker.Root, FTransform{BodyLocation},
            ECk_Replication::DoesNotReplicate);
        auto Legs = TArray<FCk_ProceduralLeg_Spec>{};
        const auto Corners = TArray<FVector2D>{{40.0, 30.0}, {40.0, -30.0}, {-40.0, 30.0}, {-40.0, -30.0}};
        for (auto Index = 0; Index < Corners.Num(); ++Index)
        {
            const auto& Corner = Corners[Index];
            auto Placement = FCk_ProceduralLeg_Placement{
                FVector{Corner.X, Corner.Y, 0.0}, FVector{Corner.X, Corner.Y * 2.0, -60.0}};
            Placement.Set_PhaseOffset(Index == 0 || Index == 3 ? 0.0f : 0.5f);
            Legs.Emplace(LegId(Index), Placement, FCk_ProceduralLeg_ChainGeometry{TArray<float>{60.0f, 80.0f}});
        }
        auto* Rig = NewObject<UCk_ProceduralRig_Data>();
        Rig->Set_Legs(Legs);
        auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
        for (const auto Id : InBoundLegIds)
        {
            auto Segments = TArray<FCk_Handle_Transform>{};
            for (auto Index = 0; Index < 2; ++Index)
            {
                auto Entity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Walker.Body);
                Segments.Add(UCk_Utils_Transform_UE::Add(Entity, FTransform{BodyLocation},
                    ECk_Replication::DoesNotReplicate));
            }
            auto FootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Walker.Body);
            const auto Foot = UCk_Utils_Transform_UE::Add(FootEntity, FTransform{BodyLocation},
                ECk_Replication::DoesNotReplicate);
            auto RigSpec = FCk_ProceduralRig_Spec{Segments};
            RigSpec.Set_Foot(Foot);
            Chains.Emplace(Id, RigSpec);
        }
        const auto Composed = UCk_Utils_ProceduralAnimation_UE::Add_Walker(Walker.Body, FCk_ProceduralWalker_Spec{Rig, NewObject<UCk_ProceduralGait_Data>()}.Set_Chains(Chains));
        Walker.Gait = Composed.Get_Gait();
        Walker.Subject = UCk_Utils_ProceduralArticulation_UE::Cast(Walker.Root);
        return Walker;
    }

    inline auto
        Get_IsWalkerSettled(
            const FWalker& InWalker)
        -> bool
    {
        return ck::IsValid(InWalker.Subject)
            && UCk_Utils_ProceduralArticulation_UE::Get_SettledRevision(InWalker.Subject) > 0
            && UCk_Utils_ProceduralGait_UE::Get_PlantedCount(InWalker.Gait) == 4;
    }
}

// --------------------------------------------------------------------------------------------------------------------

// Language=angelscript

//============================================================================
// CK JOLT - shared builders for the ConnectedBodiesCollide rows
//============================================================================
//
// Boxes on their own entities, point joints between them, contact counting by
// pair and the joint gap (the distance between the two bodies' copies of the
// pivot, each carried in its own body's frame from the frame the joint was added).
//============================================================================

namespace ck_jolt_connected_bodies_test
{
    FCk_Handle Make_Box(FCk_Handle InOwner, FVector InCentre, FVector InHalfExtents, ECk_MotionType InMotion,
                        float InGravityFactor, FName InProfile)
    {
        auto Entity = utils_entity_lifetime::Request_CreateEntity(InOwner);
        Entity.Request_OverrideToSelf();
        utils_transform::Add(Entity, FTransform(FRotator::ZeroRotator, InCentre), ECk_Replication::DoesNotReplicate);

        auto Shape = FCk_Jolt_ShapeDimensions(ECk_Jolt_ShapeType::Box);
        Shape.Set_HalfExtents(InHalfExtents);
        auto Spec = FCk_JoltBody_Spec(ECk_JoltBody_ShapeSource::ExplicitShape);
        Spec.Set_ShapeDimensions(Shape);
        Spec.Set_MotionType(InMotion);
        Spec.Set_CollisionProfileName(InProfile);
        if (InMotion == ECk_MotionType::Dynamic)
        {
            Spec.Set_MassSource(ECk_JoltBody_MassSource::Explicit);
            Spec.Set_MassKg(1.0f);
            Spec.Set_GravityFactor(InGravityFactor);
        }
        utils_jolt_body::Add(Entity, Spec);
        return Entity;
    }

    FCk_Handle_JoltConstraint Make_PointJoint(FCk_Handle InBodyA, FCk_Handle InBodyB, FVector InPivot,
                                               ECk_EnableDisable InConnectedBodiesCollide)
    {
        auto Spec = FCk_JoltConstraint_Spec(ECk_JoltConstraint_Type::Point);
        Spec.Set_OtherBody(InBodyB);
        Spec.Set_WorldAnchorA(InPivot);
        Spec.Set_WorldAnchorB(InPivot);
        Spec.Set_ConnectedBodiesCollide(InConnectedBodiesCollide);
        auto BodyA = InBodyA.As_JoltBody();
        return utils_jolt_constraint::Create(BodyA, Spec);
    }

    // The pivot in InBody's own frame, now.
    FVector Get_LocalPivot(FCk_Handle InBody, FVector InWorldPivot)
    {
        return utils_transform::Get_EntityCurrentTransform(InBody.As_Transform()).InverseTransformPosition(InWorldPivot);
    }

    float64 Get_JointGap(FCk_Handle InBodyA, FVector InLocalA, FCk_Handle InBodyB, FVector InLocalB)
    {
        const auto OnA = utils_transform::Get_EntityCurrentTransform(InBodyA.As_Transform()).TransformPosition(InLocalA);
        const auto OnB = utils_transform::Get_EntityCurrentTransform(InBodyB.As_Transform()).TransformPosition(InLocalB);
        return (OnA - OnB).Size();
    }
}

// Contacts reported between entities, as unordered pairs. Each side of a contact reports it, so a pair seen from both
// bodies counts twice; the rows only ask whether a count is zero.
struct FCk_JoltConnectedBodies_Contacts
{
    TArray<FCk_Handle> _A;
    TArray<FCk_Handle> _B;
    TArray<int32> _Counts;

    void Record(FCk_Handle InSelf, FCk_Handle InOther)
    {
        for (int32 Index = 0; Index < _Counts.Num(); ++Index)
        {
            if ((_A[Index] == InSelf && _B[Index] == InOther) || (_A[Index] == InOther && _B[Index] == InSelf))
            {
                _Counts[Index] += 1;
                return;
            }
        }

        _A.Add(InSelf);
        _B.Add(InOther);
        _Counts.Add(1);
    }

    int32 Get_Count(FCk_Handle InX, FCk_Handle InY) const
    {
        for (int32 Index = 0; Index < _Counts.Num(); ++Index)
        {
            if ((_A[Index] == InX && _B[Index] == InY) || (_A[Index] == InY && _B[Index] == InX))
            { return _Counts[Index]; }
        }

        return 0;
    }
}

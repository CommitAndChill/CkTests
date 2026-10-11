// Language=angelscript

// Value-only source collector. The GameState owns this struct and calls Initialize
// once before fixture creation, then TryCapture_Fixtures after each creator step.
// It does not inspect ECS state, issue requests, or authorize cleanup.
struct FCkRotateTowardsGym_CreatorLedger
{
    TArray<FCk_Handle> RoleValues;     // Owner + the 41 native graph positions.
    TArray<FCk_Handle> ExpectedParents;
    TArray<FCk_Handle> Evidence;       // Append-only actual nondefault returns.
    TArray<FCk_Handle> EvidenceParents;
    TArray<int32> EvidenceRoles;       // -1 means an extra or alias conflict.
    bool Broken = false;

    bool Initialize()
    {
        if (RoleValues.Num() != 0 || ExpectedParents.Num() != 0 ||
            Evidence.Num() != 0 || EvidenceParents.Num() != 0 ||
            EvidenceRoles.Num() != 0 || Broken) { return false; }
        RoleValues.SetNum(42);
        ExpectedParents.SetNum(42);
        return RoleValues.Num() == 42 && ExpectedParents.Num() == 42;
    }

    private int32 Get_ParentRole(int32 Role) const
    {
        if (Role == 0) { return -1; }
        if (Role <= 3) { return 0; }
        if (Role <= 8)
        {
            if (Role == 4) { return 1; }
            if (Role == 5 || Role == 6) { return 2; }
            return Role == 7 ? 3 : 7;
        }
        if (Role <= 15)
        {
            if (Role == 9 || Role == 10) { return 1; }
            return Role == 11 ? 2 : 3;
        }
        if (Role <= 19)
        {
            if (Role == 16) { return 4; }
            if (Role == 17) { return 5; }
            return Role == 18 ? 7 : 8;
        }
        if (Role <= 30) { return 0; }
        return 20 + Role - 31;
    }

    private void RecordReturn(int32 Role, FCk_Handle Value, FCk_Handle Parent)
    {
        if (Value == FCk_Handle()) { return; }
        if (Role != 0 && Parent == FCk_Handle()) { Broken = true; }
        bool AlreadyRecorded = false;
        for (int32 i = 0; i < Evidence.Num(); ++i)
        {
            if (Evidence[i] == Value && EvidenceRoles[i] != Role)
            { Broken = true; }
            if (EvidenceRoles[i] == Role && Evidence[i] == Value &&
                EvidenceParents[i] == Parent)
            { AlreadyRecorded = true; }
        }
        if (AlreadyRecorded) { return; }
        Evidence.Add(Value);
        EvidenceParents.Add(Parent);
        EvidenceRoles.Add(Role);
    }

    private void CaptureRole(int32 Role, FCk_Handle Value, FCk_Handle Parent)
    {
        const auto Prior = RoleValues[Role];
        if (Value == FCk_Handle())
        {
            if (Prior != FCk_Handle()) { Broken = true; }
            return;
        }
        if (Prior == FCk_Handle())
        {
            RoleValues[Role] = Value;
            ExpectedParents[Role] = Parent;
        }
        else if (Prior != Value || ExpectedParents[Role] != Parent)
        { Broken = true; }
        RecordReturn(Role, Value, Parent);
    }

    // `&` is the repository's established mutable AS parameter form.
    // Caller pre-fills exact fixed arrays and keeps InFixtures stable across callbacks.
    bool TryCapture_Fixtures(const TArray<FCkRotateTowardsGym_Fixture>& InFixtures,
        FCkRotateTowardsGym_MotionAdmission& InOutFrame,
        FCk_Handle InSourceOwner, int32 InSourceGeneration, FCk_Handle InWorldBridge)
    {
        if (RoleValues.Num() != 42 || ExpectedParents.Num() != 42 ||
            Evidence.Num() != EvidenceParents.Num() ||
            Evidence.Num() != EvidenceRoles.Num() || InFixtures.Num() != 3 ||
            InOutFrame.Roots.Num() != 3 || InOutFrame.Motion.Num() != 5 ||
            InOutFrame.Turrets.Num() != 7 || InOutFrame.Tweens.Num() != 4 ||
            InOutFrame.CubeNodes.Num() != 11 ||
            InOutFrame.CubeComponents.Num() != 11 ||
            InOutFrame.ComponentAdded.Num() != 11)
        {
            Broken = true;
            return false; // Preserve the stable fixtures and every existing row.
        }
        if (InSourceGeneration <= 0 || InOutFrame.Generation != InSourceGeneration ||
            InSourceOwner == FCk_Handle() || InOutFrame.Owner != InSourceOwner)
        { Broken = true; }
        const auto EvidenceBefore = Evidence.Num();
        CaptureRole(0, InOutFrame.Owner, InWorldBridge);
        CaptureRole(0, InSourceOwner, InWorldBridge);

        TArray<FCk_Handle> Current;
        Current.SetNum(42);
        Current[0] = InSourceOwner;
        Current[1] = FCk_Handle(InFixtures[0].Root);
        Current[2] = FCk_Handle(InFixtures[1].Root);
        Current[3] = FCk_Handle(InFixtures[2].Root);
        Current[4] = FCk_Handle(InFixtures[0].Target);
        Current[5] = FCk_Handle(InFixtures[1].Target);
        Current[6] = FCk_Handle(InFixtures[1].RestPoint);
        Current[7] = FCk_Handle(InFixtures[2].Carrier);
        Current[8] = FCk_Handle(InFixtures[2].TargetNode);

        for (int32 s = 0; s < 3; ++s)
        {
            const auto& F = InFixtures[s];
            const int32 TurretOffset = s == 0 ? 0 : (s == 1 ? 2 : 3);
            const int32 TurretCapacity = s == 0 ? 2 : (s == 1 ? 1 : 4);
            const int32 CubeOffset = s == 0 ? 0 : (s == 1 ? 3 : 6);
            const int32 CubeCapacity = s == 2 ? 5 : 3;
            const int32 TweenOffset = s == 0 ? 0 : (s == 1 ? 1 : 2);
            const int32 TweenCapacity = s == 2 ? 2 : 1;
            if (!F.StaticStarted && (F.Root != FCk_Handle_Transform() ||
                F.Target != FCk_Handle_Transform() ||
                F.RestPoint != FCk_Handle_Transform() ||
                F.Carrier != FCk_Handle_Transform() ||
                F.TargetNode != FCk_Handle_SceneNode() ||
                F.TurretRoots.Num() != 0 || F.Turrets.Num() != 0 ||
                F.Tweens.Num() != 0 || F.CubeNodes.Num() != 0 ||
                F.CubeComponents.Num() != 0))
            { Broken = true; } // Never accept identities from the legacy path.
            if (F.StagingFailed) { Broken = true; }
            if (F.StaticStarted && (F.Generation != InSourceGeneration ||
                F.Owner != InSourceOwner || F.Station != s))
            { Broken = true; }
            // These fields have no role in this station's authored recipe. Keep
            // every unexpected creator return even though no native slot exists.
            if ((s == 0 || s == 2) && F.RestPoint != FCk_Handle_Transform())
            {
                Broken = true;
                RecordReturn(-1, FCk_Handle(F.RestPoint), FCk_Handle(F.Root));
            }
            if ((s == 0 || s == 1) && F.Carrier != FCk_Handle_Transform())
            {
                Broken = true;
                RecordReturn(-1, FCk_Handle(F.Carrier), FCk_Handle(F.Root));
            }
            if ((s == 0 || s == 1) && F.TargetNode != FCk_Handle_SceneNode())
            {
                Broken = true;
                const auto Parent = F.Carrier != FCk_Handle_Transform() ?
                    FCk_Handle(F.Carrier) : FCk_Handle(F.Root);
                RecordReturn(-1, FCk_Handle(F.TargetNode), Parent);
            }
            if (F.TurretRoots.Num() > TurretCapacity ||
                F.Turrets.Num() > TurretCapacity ||
                F.CubeNodes.Num() > CubeCapacity ||
                F.CubeComponents.Num() > CubeCapacity ||
                F.Tweens.Num() > TweenCapacity)
            { Broken = true; }
            for (int32 j = 0; j < F.TurretRoots.Num(); ++j)
            {
                const auto H = FCk_Handle(F.TurretRoots[j]);
                if (j < TurretCapacity)
                { Current[9 + TurretOffset + j] = H; }
                else { RecordReturn(-1, H, FCk_Handle(F.Root)); }
            }
            for (int32 j = 0; j < F.Turrets.Num(); ++j)
            {
                const auto H = FCk_Handle(F.Turrets[j]);
                if (j >= TurretCapacity || j >= F.TurretRoots.Num() ||
                    H != FCk_Handle(F.TurretRoots[j]))
                {
                    Broken = true;
                    RecordReturn(-1, H, FCk_Handle(F.Root));
                }
            }
            for (int32 j = 0; j < F.Tweens.Num(); ++j)
            {
                const auto H = FCk_Handle(F.Tweens[j]);
                if (j < TweenCapacity)
                { Current[16 + TweenOffset + j] = H; }
                else { RecordReturn(-1, H, FCk_Handle()); }
            }
            for (int32 j = 0; j < F.CubeTargets.Num(); ++j)
            {
                const auto H = F.CubeTargets[j];
                if (j < CubeCapacity)
                {
                    Current[20 + CubeOffset + j] = H;
                    if (j >= F.CubeSpawnCarriers.Num() || InOutFrame.CubeSpawnCarriers.Num() != 11)
                    { Broken = true; }
                    else
                    {
                        auto PriorCarrier = InOutFrame.CubeSpawnCarriers[CubeOffset + j];
                        auto CapturedCarrier = F.CubeSpawnCarriers[j];
                        if (CapturedCarrier == FCk_Handle() ||
                            (PriorCarrier != FCk_Handle() && PriorCarrier != CapturedCarrier))
                        { Broken = true; }
                        else { InOutFrame.CubeSpawnCarriers[CubeOffset + j] = CapturedCarrier; }
                    }
                }
                else { RecordReturn(-1, H, FCk_Handle()); }
            }
            for (int32 j = 0; j < F.CubeComponents.Num(); ++j)
            {
                const auto H = FCk_Handle(F.CubeComponents[j]);
                if (j < CubeCapacity)
                { Current[31 + CubeOffset + j] = H; }
                else
                {
                    const auto Parent = j < F.CubeNodes.Num() ?
                        FCk_Handle(F.CubeNodes[j]) : FCk_Handle();
                    RecordReturn(-1, H, Parent);
                }
            }
        }
        if (InFixtures[2].Target != FCk_Handle_Transform() &&
            FCk_Handle(InFixtures[2].Target) != Current[8])
        {
            Broken = true;
            RecordReturn(-1, FCk_Handle(InFixtures[2].Target), Current[7]);
        }

        for (int32 Role = 1; Role < 42; ++Role)
        {
            const auto ParentRole = Get_ParentRole(Role);
            auto Parent = Current[ParentRole];
            if (Parent == FCk_Handle()) { Parent = RoleValues[ParentRole]; }
            CaptureRole(Role, Current[Role], Parent);
        }
        if ((InOutFrame.Closed || InOutFrame.Activated) &&
            Evidence.Num() != EvidenceBefore)
        { Broken = true; }
        if (InOutFrame.Closed || InOutFrame.Activated)
        { return !Broken; } // No frame publication after closure or activation.

        for (int32 i = 0; i < 3; ++i)
        {
            const auto H = InFixtures[i].Root;
            if (H == FCk_Handle_Transform())
            { if (InOutFrame.Roots[i] != FCk_Handle_Transform()) { Broken = true; } }
            else if (InOutFrame.Roots[i] == FCk_Handle_Transform())
            { InOutFrame.Roots[i] = H; }
            else if (InOutFrame.Roots[i] != H) { Broken = true; }
        }
        for (int32 i = 0; i < 5; ++i)
        {
            const auto H = Current[4 + i];
            if (H == FCk_Handle())
            { if (InOutFrame.Motion[i] != FCk_Handle()) { Broken = true; } }
            else if (InOutFrame.Motion[i] == FCk_Handle())
            { InOutFrame.Motion[i] = H; }
            else if (InOutFrame.Motion[i] != H) { Broken = true; }
        }
        for (int32 s = 0; s < 3; ++s)
        {
            const auto& F = InFixtures[s];
            const int32 TurretOffset = s == 0 ? 0 : (s == 1 ? 2 : 3);
            const int32 TurretCapacity = s == 0 ? 2 : (s == 1 ? 1 : 4);
            const int32 CubeOffset = s == 0 ? 0 : (s == 1 ? 3 : 6);
            const int32 CubeCapacity = s == 2 ? 5 : 3;
            const int32 TweenOffset = s == 0 ? 0 : (s == 1 ? 1 : 2);
            const int32 TweenCapacity = s == 2 ? 2 : 1;
            for (int32 j = 0; j < TurretCapacity; ++j)
            {
                const auto i = TurretOffset + j;
                const bool HasTyped = j < F.Turrets.Num();
                const auto H = HasTyped ? F.Turrets[j] : FCk_Handle_RotateTowards();
                if (H == FCk_Handle_RotateTowards())
                { if (InOutFrame.Turrets[i] != FCk_Handle_RotateTowards()) { Broken = true; } }
                else if (j >= F.TurretRoots.Num() ||
                    FCk_Handle(H) != FCk_Handle(F.TurretRoots[j]))
                { Broken = true; }
                else if (InOutFrame.Turrets[i] == FCk_Handle_RotateTowards())
                { InOutFrame.Turrets[i] = H; }
                else if (InOutFrame.Turrets[i] != H) { Broken = true; }
            }
            for (int32 j = 0; j < TweenCapacity; ++j)
            {
                const auto i = TweenOffset + j;
                const auto H = j < F.Tweens.Num() ? F.Tweens[j] : FCk_Handle_Tween();
                if (H == FCk_Handle_Tween())
                { if (InOutFrame.Tweens[i] != FCk_Handle_Tween()) { Broken = true; } }
                else if (InOutFrame.Tweens[i] == FCk_Handle_Tween())
                { InOutFrame.Tweens[i] = H; }
                else if (InOutFrame.Tweens[i] != H) { Broken = true; }
            }
            for (int32 j = 0; j < CubeCapacity; ++j)
            {
                const auto i = CubeOffset + j;
                const auto N = j < F.CubeNodes.Num() ? F.CubeNodes[j] : FCk_Handle_SceneNode();
                const auto C = j < F.CubeComponents.Num() ?
                    F.CubeComponents[j] : FCk_Handle_UnrealComponent();
                if (N == FCk_Handle_SceneNode())
                { if (InOutFrame.CubeNodes[i] != FCk_Handle_SceneNode()) { Broken = true; } }
                else if (InOutFrame.CubeNodes[i] == FCk_Handle_SceneNode())
                { InOutFrame.CubeNodes[i] = N; }
                else if (InOutFrame.CubeNodes[i] != N) { Broken = true; }
                if (C == FCk_Handle_UnrealComponent())
                { if (InOutFrame.CubeComponents[i] != FCk_Handle_UnrealComponent()) { Broken = true; } }
                else if (InOutFrame.CubeComponents[i] == FCk_Handle_UnrealComponent())
                { InOutFrame.CubeComponents[i] = C; }
                else if (InOutFrame.CubeComponents[i] != C) { Broken = true; }
            }
        }
        // Deliberately leave Generation, Owner, ComponentAdded, Activated and Closed unchanged.
        return !Broken;
    }
}

// Language=angelscript

namespace CkGoapGym
{
    FString Get_BaseTag(int32 Index)
    {
        if (Index == 0) { return "Gym.Goap.Station.OpenDoor"; }
        if (Index == 1) { return "Gym.Goap.Station.MakeTea"; }
        if (Index == 2) { return "Gym.Goap.Station.CrossRiver"; }
        if (Index == 3) { return "Gym.Goap.Station.Patrol"; }
        if (Index == 4) { return "Gym.Goap.Station.Survival"; }
        if (Index == 5) { return "Gym.Goap.Station.CombatBrain"; }
        if (Index == 6) { return "Gym.Goap.Station.OptOutDemo"; }
        return "";
    }

    TSubclassOf<UCk_EntityScript_UE> Get_SourceClass(int32 Index)
    {
        if (Index == 0) { return UCk_EntityScript_GoapGym_OpenDoor_Station; }
        if (Index == 1) { return UCk_EntityScript_GoapGym_MakeTea_Station; }
        if (Index == 2) { return UCk_EntityScript_GoapGym_CrossRiver_Station; }
        if (Index == 3) { return UCk_EntityScript_GoapGym_Patrol_Station; }
        if (Index == 4) { return UCk_EntityScript_GoapGym_Survival_Station; }
        if (Index == 5) { return UCk_EntityScript_GoapGym_CombatBrain_Station; }
        if (Index == 6) { return UCk_EntityScript_GoapGym_OptOutDemo_Station; }
        return nullptr;
    }

    FTransform Get_StationTransform(int32 Index)
    {
        if (Index < 0 || Index >= 7) { return FTransform::Identity; }
        return FTransform(FRotator(0.0, 180.0, 0.0),
            FVector(500.0, -2400.0 + float(Index) * 800.0, 0.0));
    }

    FTransform Get_SourceTransform(int32 Index)
    {
        auto Station = Get_StationTransform(Index);
        if (Index < 0 || Index >= 7) { return Station; }
        return FTransform(Station.Rotator(),
            Station.GetLocation() + FVector(0.0, 0.0, 100.0),
            FVector(1.0, 1.0, 1.0));
    }

    FString Get_Title(int32 Index)
    {
        if (Index == 0) { return "STATION 1 / OPEN DOOR"; }
        if (Index == 1) { return "STATION 2 / MAKE TEA"; }
        if (Index == 2) { return "STATION 3 / CROSS RIVER"; }
        if (Index == 3) { return "STATION 4 / PATROL ROUTE"; }
        if (Index == 4) { return "STATION 5 / SURVIVAL DECISION"; }
        if (Index == 5) { return "STATION 6 / COMBAT BRAIN"; }
        if (Index == 6) { return "STATION 7 / OPT-OUT DEMO"; }
        return "";
    }

    FString Get_Description(int32 Index)
    {
        if (Index == 0) { return "Atomic plan.\nGoal: Door.IsOpen=true."; }
        if (Index == 1) { return "4-step dependency chain.\nGoal: TeaServed=true."; }
        if (Index == 2) { return "Branching cost-sensitive plan.\nGoal: Crossed=true."; }
        if (Index == 3) { return "Multi-tier Planner (U11.6).\nGoToWaypoint + Observe promoted to Planners.\nChain: [Root, Composite, Leaf]."; }
        if (Index == 4) { return "Two independent ActionSets.\nGoals: Hungry=false AND SafeFromThreat=true."; }
        if (Index == 5) { return "4-tier canonical Planner (spec 2.2).\nAlive -> Engage -> Light/Heavy -> leaves.\nSibling promotions at tier 3."; }
        if (Index == 6) { return "Intentional _AllowPlanFailed=true.\nGoal is structurally unreachable.\nDebugger should show OPT-OUT indicator."; }
        return "";
    }

    int32 Get_KeyCount(int32 Index)
    {
        if (Index == 0) { return 1; }
        if (Index == 1) { return 8; }
        if (Index == 2 || Index == 3) { return 3; }
        if (Index == 4) { return 5; }
        if (Index == 5) { return 6; }
        if (Index == 6) { return 2; }
        return 0;
    }

    FName Get_KeyName(int32 Index, int32 Bit)
    {
        if (Index == 0 && Bit == 0) { return n"Gym.Goap.WS.Door.IsOpen"; }
        if (Index == 1)
        {
            if (Bit == 0) { return n"Gym.Goap.WS.Tea.HasKettle"; }
            if (Bit == 1) { return n"Gym.Goap.WS.Tea.HasWater"; }
            if (Bit == 2) { return n"Gym.Goap.WS.Tea.HasTeaLeaves"; }
            if (Bit == 3) { return n"Gym.Goap.WS.Tea.HasCup"; }
            if (Bit == 4) { return n"Gym.Goap.WS.Tea.WaterBoiled"; }
            if (Bit == 5) { return n"Gym.Goap.WS.Tea.TeaSteeped"; }
            if (Bit == 6) { return n"Gym.Goap.WS.Tea.TeaPoured"; }
            if (Bit == 7) { return n"Gym.Goap.WS.Tea.TeaServed"; }
        }
        if (Index == 2)
        {
            if (Bit == 0) { return n"Gym.Goap.WS.CrossRiver.BridgeIsOpen"; }
            if (Bit == 1) { return n"Gym.Goap.WS.CrossRiver.HasCoin"; }
            if (Bit == 2) { return n"Gym.Goap.WS.CrossRiver.Crossed"; }
        }
        if (Index == 3)
        {
            if (Bit == 0) { return n"Gym.Goap.WS.Patrol.AtWaypoint"; }
            if (Bit == 1) { return n"Gym.Goap.WS.Patrol.AreaScanned"; }
            if (Bit == 2) { return n"Gym.Goap.WS.Patrol.AreaPatrolled"; }
        }
        if (Index == 4)
        {
            if (Bit == 0) { return n"Gym.Goap.WS.Survival.Hungry"; }
            if (Bit == 1) { return n"Gym.Goap.WS.Survival.HasFood"; }
            if (Bit == 2) { return n"Gym.Goap.WS.Survival.ThreatActive"; }
            if (Bit == 3) { return n"Gym.Goap.WS.Survival.HasWeapon"; }
            if (Bit == 4) { return n"Gym.Goap.WS.Survival.SafeFromThreat"; }
        }
        if (Index == 5)
        {
            if (Bit == 0) { return n"Gym.Goap.WS.CombatBrain.EnemyVisible"; }
            if (Bit == 1) { return n"Gym.Goap.WS.CombatBrain.WeaponEquipped"; }
            if (Bit == 2) { return n"Gym.Goap.WS.CombatBrain.StaminaHigh"; }
            if (Bit == 3) { return n"Gym.Goap.WS.CombatBrain.EnemyHit"; }
            if (Bit == 4) { return n"Gym.Goap.WS.CombatBrain.EnemyAttacked"; }
            if (Bit == 5) { return n"Gym.Goap.WS.CombatBrain.EnemyDead"; }
        }
        if (Index == 6)
        {
            if (Bit == 0) { return n"Gym.Goap.WS.OptOutDemo.Unreachable"; }
            if (Bit == 1) { return n"Gym.Goap.WS.OptOutDemo.Touch"; }
        }
        return FName();
    }

    FGameplayTag Get_Key(int32 Index, int32 Bit)
    {
        auto Name = Get_KeyName(Index, Bit);
        if (Name == n"") { return FGameplayTag(); }
        return utils_gameplay_tag::ResolveGameplayTag(Name);
    }

    int32 Get_InitialMask(int32 Index)
    {
        if (Index == 1) { return 15; }
        if (Index == 2) { return 3; }
        if (Index == 4) { return 13; }
        return 0;
    }

    int32 Get_ActionStation(int32 Action)
    {
        if (Action == 0) { return 0; }
        if (Action >= 1 && Action <= 5) { return 1; }
        if (Action >= 6 && Action <= 8) { return 2; }
        if (Action >= 9 && Action <= 12) { return 3; }
        if (Action >= 13 && Action <= 17) { return 4; }
        if (Action >= 18 && Action <= 22) { return 5; }
        return -1;
    }

    bool TryGet_ControlMask(int32 Index, int32 Action, int32 Before, int32&out Expected)
    {
        Expected = 0;
        auto KeyCount = Get_KeyCount(Index);
        if (KeyCount <= 0 || Get_ActionStation(Action) != Index ||
            Before < 0 || Before >= (1 << KeyCount)) { return false; }
        Expected = Before;
        if (Action == 0) { Expected ^= 1; }
        else if (Action >= 1 && Action <= 4) { Expected ^= 1 << (Action - 1); }
        else if (Action == 5) { Expected = 15; }
        else if (Action == 6) { Expected ^= 1; }
        else if (Action == 7) { Expected &= ~2; }
        else if (Action == 8) { Expected = 3; }
        else if (Action >= 9 && Action <= 11) { Expected |= 1 << (Action - 9); }
        else if (Action == 12) { Expected = 0; }
        else if (Action >= 13 && Action <= 16) { Expected ^= 1 << (Action - 13); }
        else if (Action == 17) { Expected = 13; }
        else if (Action >= 18 && Action <= 20) { Expected ^= 1 << (Action - 18); }
        else if (Action == 21) { Expected = 63; }
        else if (Action == 22) { Expected = 0; }
        return true;
    }

    int32 Get_PlannerCount(int32 Index)
    {
        if (Index == 3) { return 3; }
        if (Index == 4) { return 2; }
        if (Index == 5) { return 4; }
        return Index >= 0 && Index < 7 ? 1 : 0;
    }

    int32 Get_ActionCount(int32 Index)
    {
        if (Index == 0) { return 2; }
        if (Index == 1) { return 4; }
        if (Index == 2) { return 3; }
        if (Index == 3) { return 8; }
        if (Index == 4) { return 6; }
        if (Index == 5) { return 11; }
        if (Index == 6) { return 1; }
        return 0;
    }

    // Codes follow each source's AddAction order, including promoted actions.
    int32 Get_PlannerParentAction(int32 Index, int32 Planner)
    {
        if (Index < 0 || Index >= 7 || Planner < 0 ||
            Planner >= Get_PlannerCount(Index)) { return -2; }
        if (Index == 3)
        {
            if (Planner == 1) { return 0; } // GoToWaypoint
            if (Planner == 2) { return 3; } // Observe
        }
        if (Index == 5)
        {
            if (Planner == 1) { return 0; } // Engage
            if (Planner == 2) { return 3; } // LightAttacks
            if (Planner == 3) { return 4; } // HeavyAttacks
        }
        return -1;
    }

    int32 Get_ActionPlanner(int32 Index, int32 Code)
    {
        if (Code < 0 || Code >= Get_ActionCount(Index)) { return -1; }
        if (Index == 3)
        {
            if (Code == 1 || Code == 2) { return 1; }
            if (Code == 4 || Code == 5) { return 2; }
        }
        if (Index == 4) { return Code <= 2 ? 0 : 1; }
        if (Index == 5)
        {
            if (Code >= 3 && Code <= 5) { return 1; }
            if (Code >= 6 && Code <= 8) { return 2; }
            if (Code >= 9) { return 3; }
        }
        return 0;
    }

    bool IsValidFrame(const FCkGoapGym_WireSnapshot&in Frame)
    {
        if (Frame.SourceGeneration != 1 || Frame.ControlGeneration <= 0 ||
            Frame.Sequence <= 0 || Frame.Replacing != -1) { return false; }
        if (Frame.Phase == 4) { return true; }
        if ((Frame.Phase != 2 && Frame.Phase != 3) ||
            Frame.Stations.Num() != 7) { return false; }
        for (int32 I = 0; I < 7; ++I)
        {
            auto Station = Frame.Stations[I];
            if (Station.Index != I || Station.Generation != 1 ||
                Station.Title.Len() > 128 || Station.Detail.Len() > 8192 ||
                Station.Footer.Len() > 1024 ||
                Station.WorldStateMask < 0 ||
                Station.WorldStateMask >= (1 << Get_KeyCount(I)) ||
                Station.ControlCount < 0 || Station.ControlCount > 2147483647 ||
                Station.Planners.Num() != Get_PlannerCount(I))
            { return false; }
            for (int32 J = 0; J < Station.Planners.Num(); ++J)
            {
                auto Planner = Station.Planners[J];
                if (Planner.Index != J ||
                    Planner.ParentAction != Get_PlannerParentAction(I, J) ||
                    Planner.PlanCount < 0 || Planner.PlanCount > 2147483647 ||
                    Planner.FailureCount < 0 || Planner.FailureCount > 2147483647 ||
                    Planner.PlanStatus < 0 || Planner.PlanStatus > 4 ||
                    Planner.PlanActions.Num() > Get_ActionCount(I) ||
                    Planner.ActiveChain.Num() > 4 ||
                    !Math::IsFinite(Planner.PlanCost) || Planner.PlanCost < 0.0f)
                { return false; }
                for (auto Code : Planner.PlanActions)
                { if (Get_ActionPlanner(I, Code) != J) { return false; } }
                for (auto Code : Planner.ActiveChain)
                { if (Code < 0 || Code >= Get_ActionCount(I)) { return false; } }
            }
        }
        return true;
    }
}

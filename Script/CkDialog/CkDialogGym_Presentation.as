// Language=angelscript

// This is a local shell under the owning controller's Graph station. It only
// renders copied wire values; the Dialog registry and emitters stay on authority.
struct FCkDialogGym_GraphPresentation
{
    FCk_Handle Owner;
    TArray<FCk_Handle_Pmg_DebugShape> Boxes;
    TArray<FCk_Handle_Pmg_DebugShape> Labels;
    TArray<FCk_Handle_Pmg_DebugShape> Edges;
    TArray<int32> AppliedStates;
    FName AppliedActive;

    bool IsDirect(FCk_Handle Child, FCk_Handle Parent) const
    {
        return ck::IsValid(Child) && ck::IsValid(Parent) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                Child, ECk_EntityLifetime_DestructionPhase::BeginDestroy) &&
            utils_entity_lifetime::Has_LifetimeOwner(Child) &&
            utils_entity_lifetime::Get_LifetimeOwner(Child) == Parent;
    }

    FVector Get_NodePosition(int32 Index) const
    {
        auto Anchor = CkDialogGym::Get_SourceTransform(4).GetLocation();
        if (Index == 0) { return Anchor + FVector(0.0, 0.0, 340.0); }
        if (Index == 1) { return Anchor + FVector(0.0, -260.0, 180.0); }
        if (Index == 2) { return Anchor + FVector(0.0, 260.0, 180.0); }
        return Anchor + FVector(0.0, -260.0, 20.0);
    }

    FName Get_NodeID(int32 Index) const
    {
        if (Index == 0) { return n"Graph.Greeting"; }
        if (Index == 1) { return n"Graph.Friendly"; }
        if (Index == 2) { return n"Graph.Hostile"; }
        return n"Graph.Farewell";
    }

    FString Get_NodeLabel(int32 Index) const
    {
        if (Index == 0) { return "Greeting"; }
        if (Index == 1) { return "Friendly"; }
        if (Index == 2) { return "Hostile"; }
        return "Farewell";
    }

    FLinearColor Get_NodeColor(int32 State, bool Active) const
    {
        if (Active) { return FLinearColor(1.0f, 0.9f, 0.15f, 1.0f); }
        if (State == 1) { return FLinearColor(0.15f, 0.85f, 0.3f, 1.0f); }
        if (State == 2) { return FLinearColor(0.9f, 0.2f, 0.2f, 1.0f); }
        return FLinearColor(0.4f, 0.4f, 0.4f, 1.0f);
    }

    bool Request_Create(FCk_Handle Station)
    {
        if (ck::IsValid(Owner) || ck::Is_NOT_Valid(Station) ||
            !utils_entity_lifetime::Get_CanCreateEntity(Station)) { return false; }
        Owner = utils_entity_lifetime::Request_CreateEntity(Station);
        if (!IsDirect(Owner, Station)) { return false; }
        if (!utils_entity_lifetime::Get_CanCreateEntity(Owner))
        { Request_Close(Station); return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            auto Position = Get_NodePosition(I);
            Boxes.Add(utils_pmg_basic_shapes::Create_Box(
                Owner, FTransform(Position), FVector(12.0, 95.0, 45.0),
                ECk_Plane_Axis::YZ, FLinearColor(0.4f, 0.4f, 0.4f, 1.0f),
                true, 2.0f, -1.0f));
            Labels.Add(utils_pmg_text_shapes::Create_Text(
                Owner, FTransform(Position + FVector(0.0, 0.0, 65.0)),
                Get_NodeLabel(I), 34.0f, FLinearColor::White, true, true,
                2.0f, ECk_Pmg_TextAlign::Center, ECk_Plane_Axis::YZ,
                nullptr, -1.0f));
            AppliedStates.Add(-1);
        }
        for (int32 I = 0; I < 3; ++I)
        {
            int32 FromIndex = I == 2 ? 1 : 0;
            int32 ToIndex = I + 1;
            auto Start = Get_NodePosition(FromIndex);
            auto End = Get_NodePosition(ToIndex);
            auto Direction = End - Start;
            Edges.Add(utils_pmg_directional_shapes::Create_DashedLine(
                Owner, FTransform(Direction.GetSafeNormal().Rotation(), Start,
                    FVector(1.0, 1.0, 1.0)), Direction.Size(),
                18.0f, 10.0f, 3.0f,
                FLinearColor(0.4f, 0.4f, 0.4f, 1.0f),
                ECk_Plane_Axis::YZ, -1.0f));
        }
        if (Boxes.Num() != 4 || Labels.Num() != 4 || Edges.Num() != 3)
        { Request_Close(Station); return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            if (ck::Is_NOT_Valid(Boxes[I]) || ck::Is_NOT_Valid(Labels[I]))
            { Request_Close(Station); return false; }
        }
        for (auto Edge : Edges)
        { if (ck::Is_NOT_Valid(Edge)) { Request_Close(Station); return false; } }
        return true;
    }

    bool IsValid(FCk_Handle Station) const
    {
        if (!IsDirect(Owner, Station) || Boxes.Num() != 4 || Labels.Num() != 4 ||
            Edges.Num() != 3 ||
            AppliedStates.Num() != 4)
        { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            if (!IsDirect(FCk_Handle(Boxes[I]), Owner) ||
                !IsDirect(FCk_Handle(Labels[I]), Owner) ||
                !utils_pmg_debug_shape::Has(FCk_Handle(Boxes[I])) ||
                !utils_pmg_debug_shape::Has(FCk_Handle(Labels[I])))
            { return false; }
        }
        for (int32 I = 0; I < 3; ++I)
        {
            if (!IsDirect(FCk_Handle(Edges[I]), Owner) ||
                !utils_pmg_debug_shape::Has(FCk_Handle(Edges[I])))
            { return false; }
        }
        return true;
    }

    bool Request_Draw(FCk_Handle Station, FCkDialogGym_StationFrame Frame)
    {
        if (!IsValid(Station) || Frame.Index != 4 ||
            Frame.GraphNodeStates.Num() != 4) { return false; }
        for (int32 I = 0; I < 4; ++I)
        {
            if (AppliedStates[I] != Frame.GraphNodeStates[I] ||
                AppliedActive != Frame.ActiveLineID)
            {
                auto Color = Get_NodeColor(Frame.GraphNodeStates[I],
                    Frame.ActiveLineID == Get_NodeID(I));
                utils_pmg_debug_shape::Request_SetColor(Boxes[I],
                    FCk_Request_Pmg_DebugShape_SetColor(Color));
                if (I > 0)
                { utils_pmg_debug_shape::Request_SetColor(Edges[I - 1],
                    FCk_Request_Pmg_DebugShape_SetColor(Color)); }
            }
            AppliedStates[I] = Frame.GraphNodeStates[I];
        }
        AppliedActive = Frame.ActiveLineID;
        return true;
    }

    void Request_Close(FCk_Handle Station)
    {
        if (IsDirect(Owner, Station))
        { utils_entity_lifetime::Request_DestroyEntity(Owner); }
        Owner = FCk_Handle();
        Boxes.Empty();
        Labels.Empty();
        Edges.Empty();
        AppliedStates.Empty();
        AppliedActive = FName();
    }
}

class UCk_AutoTest_HexGrid_Rules : UCk_AutoTest_Base
{
    private FCk_Handle_HexGridBlocker _Blocker;
    private FCk_Handle_HexCell _Cell;
    private int _Completions = 0;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto Zero = FCk_HexCoord();
        auto Named = utils_gameplay_tag::ResolveGameplayTag(n"Grid.Zone.Produce");
        auto Decl = FCk_HexGridAuthoredBlocker();
        TArray<FCk_HexCoord> Cells;
        Cells.Add(Zero);
        Decl.Set_Cells(Cells);
        Decl.Set_Name(Named);
        TArray<FCk_HexGridAuthoredBlocker> Blockers;
        Blockers.Add(Decl);
        auto Input = FCk_HexGrid_Spec();
        Input.Set_Blockers(Blockers);
        auto Tags = FGameplayTagContainer();
        Tags.AddTag(Named);
        Input.Set_DefaultCellTags(Tags);
        auto Asset = Cast<UCk_HexGrid_AuthoringSpec>(NewObject(this, UCk_HexGrid_AuthoringSpec));
        Asset.Set_Spec(Input);
        auto Spec = FCk_HexGrid_Spec();
        auto BoardFailure = ECk_HexBoardFailure::None;
        FCk_Handle_HexGrid Board;
        if (!Asset.TryResolve(Spec,BoardFailure)
            || !utils_hex_grid::TryCreate(InHandle,FTransform::Identity,Spec,Board,BoardFailure)
            || !Board.TryGet_CellAt(Zero,_Cell,BoardFailure))
        { FinishFailure("Authored board did not compose"); return; }
        _Blocker = utils_hex_grid_blocker::Get_BlockerWithTag(Board,Named);
        bool Disabled = false;
        FGameplayTagContainer Effective;
        if (!ck::IsValid(_Blocker) || !utils_hex_grid_blocker::Has(_Blocker.H()) || !_Blocker.Get_IsActive()
            || !_Cell.TryGet_IsDisabled(Disabled) || !Disabled || !_Cell.TryGet_Tags(Effective) || !Effective.HasTagExact(Named))
        { FinishFailure("Authored blocker votes and effective tags were not present at publication"); return; }
        auto Offset = FCk_HexCoord();
        Offset.Set_Q(1);
        TArray<FCk_HexCoord> Offsets;
        Offsets.Add(Offset);
        auto ObjectSpec = FCk_HexGridObject_Spec();
        ObjectSpec.Set_FootprintOffsets(Offsets);
        auto ObjectEntity = utils_entity_lifetime::Request_CreateEntity(InHandle);
        FCk_Handle_HexGridObject Object;
        auto Reason = ECk_HexGridObject_Failure::None;
        if (!utils_hex_grid_object::TryAdd(ObjectEntity,ObjectSpec,Object,Reason)
            || !ck::IsValid(Object) || !utils_hex_grid_object::Has(Object.H()))
        { FinishFailure("Typed object failed to compose"); return; }
        auto R = Object.TryResolve_Cells(Zero,ECk_HexRotation::Turn1,ECk_HexGridConnectivity::RequireConnected);
        if (!R._Succeeded || R._Cells.Num()!=1 || R._Cells[0]._Q!=0 || R._Cells[0]._R!=1)
        { FinishFailure("Offset was not rotated around the hole anchor"); return; }
        _Blocker.Request_SetActive(false,FCk_Delegate_Request_OnCompleted(this,n"OnToggle"));
    }

    UFUNCTION()
    void OnToggle(FCk_Handle InRequestOwner, ECk_Request_OperationResult InResult)
    {
        bool Disabled = true;
        ++_Completions;
        if (_Completions != 1 || InRequestOwner != _Blocker.H() || InResult != ECk_Request_OperationResult::Succeeded
            || _Blocker.Get_IsActive() || !_Cell.TryGet_IsDisabled(Disabled) || Disabled)
        { FinishFailure("Toggle completion did not observe committed blocker and cell state"); return; }
        FinishSuccess();
    }
}

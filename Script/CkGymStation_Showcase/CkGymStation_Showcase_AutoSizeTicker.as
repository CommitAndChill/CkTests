// One authoritative reveal clock; the GameState publishes its result to local shells.
USTRUCT()
struct FCk_GymStation_Showcase_AutoSizeTickerParams
{
    UPROPERTY() FName StationTag;
    UPROPERTY() FText Title = FText::FromString("AUTO-SIZE RUNTIME");
    UPROPERTY() TArray<FText> DescriptionLines;
    UPROPERTY() float LineRevealInterval = 1.0f;
    UPROPERTY() int32 Generation = 0;
}

class UCk_EntityScript_GymStation_Showcase_AutoSizeTicker : UCk_GenericEntityScript_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;

    UPROPERTY(ExposeOnSpawn) FName StationTag;
    UPROPERTY(ExposeOnSpawn) FText Title = FText::FromString("AUTO-SIZE RUNTIME");
    UPROPERTY(ExposeOnSpawn) TArray<FText> DescriptionLines;
    UPROPERTY(ExposeOnSpawn) float LineRevealInterval = 1.0f;
    UPROPERTY(ExposeOnSpawn) int32 Generation = 0;

    private float _Elapsed = 0.0f;
    private int32 _TickCount = 0;
    private FCk_Handle_Timer _Clock;

    bool Get_HasClock() const
    { return CkStationShowcase::IsLive(FCk_Handle(_Clock)) &&
        utils_timer::DoCast(FCk_Handle(_Clock)).IsSet(); }

    FCk_Handle_Timer Get_Clock() const
    { return _Clock; }

    UFUNCTION(BlueprintOverride)
    ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
    {
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (!CkStationShowcase::IsLive(InHandle) || ck::Is_NOT_Valid(State) ||
            !State.Get_HasWorldIdentity() || Generation <= 0 || Generation != State.Snapshot.Generation ||
            StationTag != CkStationShowcase::Get_StationTag(2) ||
            !Math::IsFinite(LineRevealInterval) || LineRevealInterval <= 0.0f ||
            DescriptionLines.Num() != 9)
        { return ECk_EntityScript_ConstructionFlow::Finished; }
        _Clock = utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"OnTick"));
        return ECk_EntityScript_ConstructionFlow::Finished;
    }

    UFUNCTION()
    private void OnTick(FCk_Handle_Timer InTimer, FCk_Chrono InChrono, FCk_Time InDeltaT)
    {
        auto State = CkStationShowcase::Get_State(GetWorld());
        if (ck::Is_NOT_Valid(State) || !State.Get_HasWorldIdentity() ||
            !Get_HasClock() || !CkStationShowcase::IsLive(FCk_Handle(InTimer)) ||
            !utils_timer::DoCast(FCk_Handle(InTimer)).IsSet() || FCk_Handle(InTimer) != FCk_Handle(_Clock) ||
            !Math::IsFinite(InDeltaT.Get_Seconds()) || InDeltaT.Get_Seconds() < 0.0) { return; }
        if (_TickCount >= 2147483646) { return; }
        float NextElapsed = _Elapsed + InDeltaT.Get_Seconds();
        if (!Math::IsFinite(NextElapsed) || !Math::IsFinite(LineRevealInterval) ||
            LineRevealInterval <= 0.0f || DescriptionLines.Num() != 9) { return; }
        _Elapsed = NextElapsed;
        _TickCount++;
        auto Interval = Math::Max(0.01f, LineRevealInterval);
        int32 LinesToReveal = DescriptionLines.Num();
        // Preserve the authored +1 reveal formula while bounding conversion for extreme finite deltas.
        if (_Elapsed < float(DescriptionLines.Num()) * Interval)
        { LinesToReveal = int32(_Elapsed / Interval) + 1; }
        auto Text = "";
        for (int i = 0; i < LinesToReveal; ++i)
        { Text = f"{Text}{DescriptionLines[i].ToString()}\n"; }
        Text = f"{Text}Tick: {_TickCount}";
        State.Request_RecordReveal(ck::ToEntity(this), Generation, Title.ToString(), Text,
            LinesToReveal, _TickCount);
    }
}

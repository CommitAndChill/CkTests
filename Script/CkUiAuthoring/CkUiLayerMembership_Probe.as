// Language=angelscript

class UCk_LayerMembership_Probe : UObject
{
    UPROPERTY()
    private UCk_UI_LayerMembership_UE _Membership;

    private int32 _RetirementCount = 0;
    private bool _CallbacksRetired = true;

    UFUNCTION()
    bool TryStart(UCommonActivatableWidget InOwner, FGameplayTag InLayerTag)
    {
        if (ck::IsValid(_Membership)) { return false; }
        _Membership = Cast<UCk_UI_LayerMembership_UE>(NewObject(this, UCk_UI_LayerMembership_UE));
        if (!ck::IsValid(_Membership)) { return false; }
        auto Errors = TArray<FString>();
        if (!_Membership.TryObserve(InOwner, InLayerTag, Errors))
        {
            _Membership.Request_Stop();
            _Membership = nullptr;
            return false;
        }
        _Membership.OnRetired.AddUFunction(this, n"DoOnRetired");
        return true;
    }

    UFUNCTION(BlueprintPure)
    bool Get_Current() const
    { return ck::IsValid(_Membership) && _Membership.Get_IsCurrent(); }

    UFUNCTION(BlueprintPure)
    int32 Get_RetirementCount() const { return _RetirementCount; }

    UFUNCTION(BlueprintPure)
    bool Get_CallbacksRetired() const { return _CallbacksRetired; }

    UFUNCTION()
    void Request_Stop()
    { if (ck::IsValid(_Membership)) { _Membership.Request_Stop(); } }

    UFUNCTION()
    private void DoOnRetired(UCk_UI_LayerMembership_UE InMembership)
    {
        ++_RetirementCount;
        _CallbacksRetired = _CallbacksRetired && ck::IsValid(InMembership)
            && InMembership == _Membership && !InMembership.Get_IsCurrent();
    }
}

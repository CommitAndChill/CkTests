// Language=angelscript

// TryGet_EntityScript on an entity spawned through the ordinary (pooled) path returns that entity's own
// script: the cast succeeds and the ExposeOnSpawn value is the one this spawn passed.
class UCk_AutoTest_EntityScript_TryGetReturnsSpawnedScript : UCk_AutoTest_Base
{
    private const int32 _ExpectedInt = 42;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto SpawnParams = UCk_EntityScript_EntityScriptGym_Spawn::Params();
        SpawnParams.InitialTransform = FTransform::Identity;
        SpawnParams.TestInt = _ExpectedInt;

        auto SpawnRequest = utils_entity_script::Request_SpawnEntity(
            InHandle,
            UCk_EntityScript_EntityScriptGym_Spawn,
            SpawnParams);

        utils_pending_entity_script::Promise_OnConstructed(
            SpawnRequest,
            FCk_Delegate_EntityScript_Constructed(this, n"OnConstructed"));
    }

    UFUNCTION()
    private void OnConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
    {
        if (IsFinished()) { return; }

        auto Script = Cast<UCk_EntityScript_EntityScriptGym_Spawn>(
            utils_entity_script::TryGet_EntityScript(InEntityScriptHandle));

        if (ck::Is_NOT_Valid(Script))
        {
            FinishFailure("TryGet_EntityScript returned no script of the spawned class");
            return;
        }

        Assert_Equals_Int(Script.TestInt, _ExpectedInt, "the returned script carries this spawn's ExposeOnSpawn value");
        FinishSuccess();
    }
}

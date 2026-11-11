// Language=angelscript

//============================================================================
// CK PMG - AUTOMATION TEST: Pmg_Polyline CREATE MAKES A DISTINCT CHILD
//============================================================================
//
// Verifies the child-making Create verb (counterpart to the stamp-self Add):
// Create(owner, ...) spawns a NEW child entity carrying the feature - the
// returned handle is valid, Has(child) is true, Has(owner) is FALSE - and a
// freshly created polyline draws nothing until it is given points.
//============================================================================

class UCk_AutoTest_Pmg_Polyline_Create_MakesDistinctChild : UCk_AutoTest_Base
{
    default _TimeoutSeconds = 3.0f;

    UFUNCTION(BlueprintOverride)
    void DoBeginPlay(FCk_Handle InHandle)
    {
        auto _CkPerfScope = ck::ScopedStat();
        auto Owner = utils_entity_lifetime::Request_CreateEntity(InHandle);

        auto Spec = FCk_Pmg_Polyline_Spec();
        Spec.Set_Material(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
            "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));

        auto Child = utils_pmg_polyline::Create(Owner, Spec);
        auto ChildEntity = FCk_Handle(Child);

        Assert_True(ck::IsValid(Child),
            "Create should return a valid FCk_Handle_Pmg_Polyline");
        Assert_True(ChildEntity != Owner,
            "Create should return a new entity, not the owner");
        Assert_True(utils_pmg_polyline::Has(ChildEntity),
            "The created child entity should carry the Pmg_Polyline feature");
        Assert_True(!utils_pmg_polyline::Has(Owner),
            "The owner must NOT carry the feature - Create is child-making, not stamp-self");
        Assert_True(utils_pmg_polyline::Get_NumDrawnPoints(Child) == 0,
            "A polyline that has not been given points draws nothing");

        FinishSuccess();
    }
}

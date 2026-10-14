// Language=angelscript

// Asset literals live in a namespace so the test file can name them (a global-scope asset is file-local).
namespace ck_procedural_action_fixture_assets
{
    asset Ck_ProceduralAction_AssetRouteFixture of UCk_ProceduralAction_Data
    {
        FCk_ProceduralAction_BodyChannel Body;
        Body.Set_Enabled(true);
        Body.Set_InitialSource(ECk_ProceduralAction_InitialSource::IdentityOffset);
        Set_BodyChannel(Body);

        TArray<FCk_ProceduralAction_Slot> Slots;
        Slots.Add(FCk_ProceduralAction_Slot(n"Gesture"));
        Set_Slots(Slots);

        TArray<FCk_ProceduralAction_TransformKey> BodyKeys;
        BodyKeys.Add(FCk_ProceduralAction_TransformKey(FCk_FloatRange_0to1(0.0f), FVector::ZeroVector, FQuat::Identity));
        BodyKeys.Add(FCk_ProceduralAction_TransformKey(FCk_FloatRange_0to1(1.0f), FVector(12.0, 0.0, 0.0), FQuat::Identity));
        FCk_ProceduralAction_Phase Lean = FCk_ProceduralAction_Phase(n"Lean", FCk_Time(0.3));
        Lean.Set_BodyKeys(BodyKeys);
        TArray<FCk_ProceduralAction_Phase> Phases;
        Phases.Add(Lean);
        Set_Phases(Phases);
    }
}

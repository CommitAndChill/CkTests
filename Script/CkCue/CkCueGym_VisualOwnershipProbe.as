// Language=angelscript
// Test-only, world-outer bridge to the authored PMG helper functions.
UCLASS()
class UCkCueGym_VisualOwnershipProbe : UObject
{
	FCk_Handle_Pmg_DebugShape MaterializedSphere;
	FCk_Handle_Pmg_DebugShape MaterializedBox;

	// Test-only one-shot calls into the authored production helpers. The sources
	// are the actual transformed cue and TempOwner created by the native fixture.
	UFUNCTION()
	bool DrawForMaterialization(FCk_Handle InSphereSource, FCk_Handle InBoxSource)
	{
		if (ck::Is_NOT_Valid(InSphereSource) || ck::Is_NOT_Valid(InBoxSource) ||
			ck::IsValid(MaterializedSphere) || ck::IsValid(MaterializedBox))
		{ return false; }
		MaterializedSphere = CueGym_Pmg::DrawSphere(InSphereSource,
			FVector(17.0f, 23.0f, 31.0f), FLinearColor(0.2f, 0.7f, 0.4f, 0.6f), 31.0f);
		MaterializedBox = CueGym_Pmg::DrawBox(InBoxSource,
			FVector(-11.0f, 5.0f, 19.0f), FLinearColor(0.8f, 0.3f, 0.6f, 0.5f),
			FVector(11.0f, 13.0f, 17.0f));
		return ck::IsValid(MaterializedSphere) && ck::IsValid(MaterializedBox);
	}

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_MaterializedSphere() const
	{ return FCk_Handle(MaterializedSphere); }

	UFUNCTION(BlueprintPure)
	FCk_Handle Get_MaterializedBox() const
	{ return FCk_Handle(MaterializedBox); }

	UFUNCTION()
	bool InvokeInvalidHelpers(FCk_Handle InSource, FCk_Handle InForeignSource,
		FLinearColor InBadColor, float32 InBadRadius, FVector InBadExtent)
	{
		bool AllRejected = true;
		auto Offset = FVector(0.0f, 0.0f, 100.0f);
		auto Color = FLinearColor(0.0f, 1.0f, 0.0f, 0.3f);
		auto Extent = FVector(20.0f, 20.0f, 20.0f);

		// Keep the helper call on the left of && so every case executes.
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawSphere(FCk_Handle(), Offset, Color, 20.0f)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawBox(FCk_Handle(), Offset, Color, Extent)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawSphere(InSource, Offset, Color, -1.0f)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawBox(InSource, Offset, Color, FVector(-1.0f, 20.0f, 20.0f))) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawSphere(InSource, Offset, Color, InBadRadius)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawBox(InSource, Offset, Color, InBadExtent)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawSphere(InSource, Offset, InBadColor, 20.0f)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawBox(InSource, Offset, InBadColor, Extent)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawSphere(InForeignSource, Offset, Color, 20.0f)) && AllRejected;
		AllRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawBox(InForeignSource, Offset, Color, Extent)) && AllRejected;
		return AllRejected;
	}

	UFUNCTION()
	bool RejectAfterAncestorInitiated(FCk_Handle InSphereSource, FCk_Handle InBoxSource)
	{
		// The fixture initiates the real owner cascade before calling this method.
		// Every operation must execute: a failure must not hide later mutation.
		auto Root = ck::TransientEntity();
		const bool SphereContextRejected = !CueGym_Pmg::Get_HasDrawingContext(InSphereSource, Root);
		const bool BoxContextRejected = !CueGym_Pmg::Get_HasDrawingContext(InBoxSource, Root);
		const bool SphereRetireRejected = !CueGym_Pmg::TryRetirePreviousShape(
			InSphereSource, MaterializedSphere, true);
		const bool BoxRetireRejected = !CueGym_Pmg::TryRetirePreviousShape(
			InBoxSource, MaterializedBox, true);
		const bool SphereDrawRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawSphere(
			InSphereSource, FVector(17.0f, 23.0f, 31.0f),
			FLinearColor(0.2f, 0.7f, 0.4f, 0.6f), 31.0f));
		const bool BoxDrawRejected = ck::Is_NOT_Valid(CueGym_Pmg::DrawBox(
			InBoxSource, FVector(-11.0f, 5.0f, 19.0f),
			FLinearColor(0.8f, 0.3f, 0.6f, 0.5f),
			FVector(11.0f, 13.0f, 17.0f)));
		return SphereContextRejected && BoxContextRejected &&
			SphereRetireRejected && BoxRetireRejected &&
			SphereDrawRejected && BoxDrawRejected;
	}
}

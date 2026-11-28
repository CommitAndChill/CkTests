// Four aging presets share one live-authored IndustrialPaint master.
class ACk_UsfGym_IndustrialShowcase : ACk_UsfGym_StudioShowcase
{
    private FString _PresetTitle = "Industrial paint";

    FString Get_PresetTitle() const
    { return _PresetTitle; }

    void Request_ConfigurePreset(int32 InPreset)
    {
        if (ck::EnsureIfNot(InPreset >= 0 && InPreset < 4, "Invalid industrial material preset"))
        { return; }
        const auto Slot = Get_SpecimenSlot();
        if (ck::EnsureIfNot(Slot >= 0, "The industrial specimen material slot is missing"))
        { return; }
        auto MID = Cast<UMaterialInstanceDynamic>(Mesh.GetMaterial(Slot));
        if (ck::EnsureIfNot(ck::IsValid(MID), "Set the industrial look before applying its preset"))
        { return; }

        auto Paint = FLinearColor(0.025, 0.22, 0.19, 1.0);
        auto Wear = 0.0;
        auto Rust = 0.02;
        auto Dust = 0.02;
        auto Scratches = 0.08;
        auto Wetness = 0.0;
        auto Blur = 0.30;
        auto Offset = 0.0;
        auto MaskTiling = 1.2;
        auto Title = "Factory teal - clean";
        if (InPreset == 1)
        {
            Paint = FLinearColor(0.48, 0.23, 0.035, 1.0);
            Wear = 0.45; Rust = 0.30; Dust = 0.10; Scratches = 0.40; Blur = 0.42;
            Offset = 0.29;
            MaskTiling = 2.2;
            Title = "Workshop ochre - worn";
        }
        else if (InPreset == 2)
        {
            Paint = FLinearColor(0.035, 0.12, 0.30, 1.0);
            Wear = 0.68; Rust = 0.70; Dust = 0.18; Scratches = 0.26; Blur = 0.55;
            Wetness = 0.32;
            Offset = 0.61;
            MaskTiling = 1.8;
            Title = "Coastal blue - corroded";
        }
        else if (InPreset == 3)
        {
            Paint = FLinearColor(0.12, 0.15, 0.045, 1.0);
            Wear = 0.88; Rust = 0.95; Dust = 0.52; Scratches = 0.15; Blur = 0.62;
            Offset = 0.87;
            Title = "Abandoned olive - aged";
        }

        MID.SetVectorParameterValue(n"PaintColor", Paint);
        MID.SetVectorParameterValue(n"PrimerColor", FLinearColor(0.24, 0.065, 0.035, 1.0));
        MID.SetScalarParameterValue(n"Wear", Wear);
        MID.SetScalarParameterValue(n"RustAmount", Rust);
        MID.SetScalarParameterValue(n"DustAmount", Dust);
        MID.SetScalarParameterValue(n"ScratchAmount", Scratches);
        MID.SetScalarParameterValue(n"Wetness", Wetness);
        MID.SetScalarParameterValue(n"PaintBlur", Blur);
        MID.SetScalarParameterValue(n"SurfaceTiling", 3.0);
        MID.SetScalarParameterValue(n"MaskTiling", MaskTiling);
        MID.SetScalarParameterValue(n"MaskOffset", Offset);
        MID.SetScalarParameterValue(n"NormalStrength", 0.65);
        MID.SetScalarParameterValue(n"DebugView", 0.0);
        _PresetTitle = Title;
    }
}

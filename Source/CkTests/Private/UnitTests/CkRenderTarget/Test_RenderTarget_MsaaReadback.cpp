#include "CkCore/Macros/CkMacros.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS

#include "ClearQuad.h"
#include "DynamicRHI.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

// --------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext |
        EAutomationTestFlags::ProductFilter | EAutomationTestFlags::NonNullRHI;
    constexpr auto TextureWidth = 12;
    constexpr auto TextureHeight = 10;
    constexpr auto RequestedSamples = 4;
    constexpr auto LinearToGammaDisabled = false;
    constexpr auto LinearToGammaEnabled = true;
    constexpr auto SrgbDisabled = false;
    constexpr auto SrgbEnabled = true;
    const auto FullRect = FIntRect{0, 0, TextureWidth, TextureHeight};
    const auto CropRect = FIntRect{2, 1, 9, 8};
    const FLinearColor Pattern[] =
    {
        {0.12f, 0.28f, 0.46f, 1.0f}, {0.31f, 0.57f, 0.19f, 1.0f}, {0.63f, 0.22f, 0.38f, 1.0f},
        {0.42f, 0.17f, 0.69f, 1.0f}, {0.24f, 0.48f, 0.61f, 1.0f}, {0.72f, 0.36f, 0.14f, 1.0f}
    };

    enum class EResourceKind : uint8
    {
        Ordinary,
        Shared
    };

    struct FFixture
    {
        FTextureRHIRef Baseline;
        FTextureRHIRef Subject;
    };

    struct FReadback
    {
        TArray<FColor> Baseline;
        TArray<FColor> Full[2];
        TArray<FColor> Crop[2];
        TArray<FLinearColor> LinearFull;
    };

    auto
        RequireD3D12(
            FAutomationTestBase& InTest)
        -> bool
    {
        if (NOT FApp::CanEverRender() || GDynamicRHI == nullptr)
        {
            InTest.AddError(TEXT("MSAA readback requires an initialized rendering RHI."));
            return false;
        }
        return InTest.TestTrue(TEXT("MSAA readback exercises the D3D12 public readback contract"),
            RHIGetInterfaceType() == ERHIInterfaceType::D3D12);
    }

    auto
        DrawPattern(
            FRHICommandListImmediate& InCommands,
            const FTextureRHIRef& InTexture)
        -> void
    {
        InCommands.BeginRenderPass(FRHIRenderPassInfo{InTexture, ERenderTargetActions::Clear_Store},
            TEXT("CkTests.MsaaReadback.Pattern"));
        for (auto Row = 0; Row < 2; ++Row)
        {
            for (auto Column = 0; Column < 3; ++Column)
            {
                InCommands.SetViewport(Column * 4, Row * 5, 0.0f, (Column + 1) * 4, (Row + 1) * 5, 1.0f);
                DrawClearQuad(InCommands, Pattern[Row * 3 + Column]);
            }
        }
        InCommands.EndRenderPass();
    }

    auto
        ColorsMatch(
            const FColor& InActual,
            const FColor& InExpected)
        -> bool
    {
        return FMath::Abs(int32{InActual.R} - int32{InExpected.R}) <= 1 &&
            FMath::Abs(int32{InActual.G} - int32{InExpected.G}) <= 1 &&
            FMath::Abs(int32{InActual.B} - int32{InExpected.B}) <= 1 &&
            FMath::Abs(int32{InActual.A} - int32{InExpected.A}) <= 1;
    }

    auto
        CheckPixels(
            FAutomationTestBase& InTest,
            const FString& InLabel,
            const TArray<FColor>& InPixels,
            const TArray<FColor>& InBaseline,
            const FIntRect& InRect,
            int32 InSamples)
        -> void
    {
        if (NOT InTest.TestEqual(InLabel + TEXT(" pixel count includes every sample"),
            InPixels.Num(), InRect.Width() * InRect.Height() * InSamples))
        {
            return;
        }
        auto Mismatches = 0;
        auto FirstMismatch = FString{};
        for (auto Y = 0; Y < InRect.Height(); ++Y)
        {
            for (auto X = 0; X < InRect.Width(); ++X)
            {
                const auto Expected = InBaseline[(Y + InRect.Min.Y) * TextureWidth + X + InRect.Min.X];
                for (auto Sample = 0; Sample < InSamples; ++Sample)
                {
                    const auto Actual = InPixels[(Y * InRect.Width() + X) * InSamples + Sample];
                    if (NOT ColorsMatch(Actual, Expected))
                    {
                        if (Mismatches == 0)
                        {
                            FirstMismatch = FString::Printf(TEXT("local=(%d,%d) sample=%d actual=%s expected=%s"),
                                X, Y, Sample, *Actual.ToString(), *Expected.ToString());
                        }
                        ++Mismatches;
                    }
                }
            }
        }
        if (Mismatches != 0)
        {
            InTest.AddError(FString::Printf(TEXT("%s: %d sample mismatches; %s"),
                *InLabel, Mismatches, *FirstMismatch));
        }
    }

    auto
        RunReadback(
            FAutomationTestBase& InTest,
            EResourceKind InKind)
        -> bool
    {
        if (NOT RequireD3D12(InTest))
        {
            return false;
        }
        TArray<FColor> LinearBaseline;
        for (const auto Srgb : {SrgbDisabled, SrgbEnabled})
        {
            const auto Fixture = MakeShared<FFixture, ESPMode::ThreadSafe>();
            ENQUEUE_RENDER_COMMAND(CkTestsCreateMsaaReadback)([Fixture, InKind, Srgb](FRHICommandListImmediate& Commands)
            {
                const auto Flags = ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource |
                    (Srgb ? ETextureCreateFlags::SRGB : ETextureCreateFlags::None);
                auto Desc = FRHITextureCreateDesc::Create2D(TEXT("CkTests.MsaaReadback.Baseline"))
                    .SetExtent(TextureWidth, TextureHeight).SetFormat(PF_B8G8R8A8).SetNumMips(1)
                    .SetNumSamples(1).SetFlags(Flags).SetClearValue(FClearValueBinding::Black)
                    .SetInitialState(ERHIAccess::RTV);
                Fixture->Baseline = Commands.CreateTexture(Desc);
                Desc.SetNumSamples(RequestedSamples);
                if (InKind == EResourceKind::Shared)
                {
                    Desc.AddFlags(ETextureCreateFlags::Shared);
                }
                Fixture->Subject = Commands.CreateTexture(Desc);
                if (Fixture->Baseline.IsValid() && Fixture->Subject.IsValid())
                {
                    DrawPattern(Commands, Fixture->Baseline);
                    DrawPattern(Commands, Fixture->Subject);
                }
            });
            FlushRenderingCommands();
            if (NOT InTest.TestTrue(TEXT("RHI creates both real render targets"),
                Fixture->Baseline.IsValid() && Fixture->Subject.IsValid()))
            {
                return false;
            }
            const auto ExpectedSamples = InKind == EResourceKind::Shared ? 1 : RequestedSamples;
            InTest.TestEqual(TEXT("Single-sample oracle has one effective sample"), Fixture->Baseline->GetNumSamples(), uint32{1});
            if (NOT InTest.TestEqual(TEXT("Requested four samples have the expected effective native count"),
                Fixture->Subject->GetNumSamples(), uint32(ExpectedSamples)))
            {
                return false;
            }
            const auto Readback = MakeShared<FReadback, ESPMode::ThreadSafe>();
            const auto ReadLinearColors = InKind == EResourceKind::Shared && Srgb;
            {
                const auto BaselineOwner = Fixture->Baseline;
                const auto SubjectOwner = Fixture->Subject;
                ENQUEUE_RENDER_COMMAND(CkTestsReadMsaaReadback)([Readback, BaselineOwner, SubjectOwner, ReadLinearColors](FRHICommandListImmediate& Commands)
                {
                    auto Flags = FReadSurfaceDataFlags{RCM_MinMax};
                    Flags.SetLinearToGamma(LinearToGammaDisabled);
                    Commands.ReadSurfaceData(BaselineOwner, FullRect, Readback->Baseline, Flags);
                    for (auto Pass = 0; Pass < 2; ++Pass)
                    {
                        const auto LinearToGamma = Pass == 1 ? LinearToGammaEnabled : LinearToGammaDisabled;
                        Flags.SetLinearToGamma(LinearToGamma);
                        Commands.ReadSurfaceData(SubjectOwner, FullRect, Readback->Full[Pass], Flags);
                        Commands.ReadSurfaceData(SubjectOwner, CropRect, Readback->Crop[Pass], Flags);
                    }
                    if (ReadLinearColors)
                    {
                        Commands.ReadSurfaceData(SubjectOwner, FullRect, Readback->LinearFull, Flags);
                    }
                });
            }
            // Queued reads retain their own resource references after the fixture releases ownership.
            Fixture->Baseline.SafeRelease();
            Fixture->Subject.SafeRelease();
            FlushRenderingCommands();
            const auto Label = FString::Printf(TEXT("%s %s"),
                InKind == EResourceKind::Shared ? TEXT("Shared fallback") : TEXT("MSAA4"),
                Srgb ? TEXT("SRGB") : TEXT("Linear"));
            if (NOT InTest.TestEqual(Label + TEXT(" oracle pixel count"), Readback->Baseline.Num(), TextureWidth * TextureHeight))
            {
                return false;
            }
            for (auto Y = 0; Y < TextureHeight; ++Y)
            {
                for (auto X = 0; X < TextureWidth; ++X)
                {
                    const auto Expected = Pattern[(Y / 5) * 3 + X / 4].ToFColor(Srgb);
                    if (NOT ColorsMatch(Readback->Baseline[Y * TextureWidth + X], Expected))
                    {
                        InTest.AddError(Label + TEXT(" single-sample baseline does not contain the authored midtone pattern."));
                        return false;
                    }
                }
            }
            if (Srgb)
            {
                InTest.TestFalse(TEXT("Linear and SRGB midtones produce distinct stored bytes"),
                    ColorsMatch(Readback->Baseline[0], LinearBaseline[0]));
            }
            else
            {
                LinearBaseline = Readback->Baseline;
            }
            for (auto Pass = 0; Pass < 2; ++Pass)
            {
                CheckPixels(InTest, Label + FString::Printf(TEXT(" full read %d"), Pass),
                    Readback->Full[Pass], Readback->Baseline, FullRect, ExpectedSamples);
                CheckPixels(InTest, Label + FString::Printf(TEXT(" offset crop read %d"), Pass),
                    Readback->Crop[Pass], Readback->Baseline, CropRect, ExpectedSamples);
            }
            InTest.TestTrue(Label + TEXT(" repeated RGBA8 full reads ignore LinearToGamma"),
                Readback->Full[0] == Readback->Full[1]);
            InTest.TestTrue(Label + TEXT(" repeated RGBA8 cropped reads ignore LinearToGamma"),
                Readback->Crop[0] == Readback->Crop[1]);
            if (ReadLinearColors)
            {
                if (NOT InTest.TestEqual(TEXT("Shared SRGB linear-color read has exactly one value per pixel"),
                    Readback->LinearFull.Num(), TextureWidth * TextureHeight))
                {
                    return false;
                }
                constexpr auto LinearConversionTolerance = 1.0e-6f;
                for (auto Pixel = 0; Pixel < Readback->LinearFull.Num(); ++Pixel)
                {
                    const auto& Actual = Readback->LinearFull[Pixel];
                    const auto Expected = FLinearColor{Readback->Baseline[Pixel]};
                    InTest.TestTrue(FString::Printf(TEXT("Shared SRGB linear-color pixel %d is finite"), Pixel),
                        FMath::IsFinite(Actual.R) && FMath::IsFinite(Actual.G) &&
                        FMath::IsFinite(Actual.B) && FMath::IsFinite(Actual.A));
                    InTest.TestTrue(FString::Printf(TEXT("Shared SRGB linear-color pixel %d decodes baseline bytes"), Pixel),
                        Actual.Equals(Expected, LinearConversionTolerance));
                }
            }
        }
        return NOT InTest.HasAnyErrors();
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RenderTarget_MsaaReadback,
    "CkTests.UnitTests.CkRenderTarget.MsaaReadback.FourSamples", TestFlags)

auto
    FCkTest_RenderTarget_MsaaReadback::
    RunTest(
        const FString& InParameters)
    -> bool
{
    return RunReadback(*this, EResourceKind::Ordinary);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RenderTarget_MsaaReadback_SharedFallback,
    "CkTests.UnitTests.CkRenderTarget.MsaaReadback.SharedFallback", TestFlags)

auto
    FCkTest_RenderTarget_MsaaReadback_SharedFallback::
    RunTest(
        const FString& InParameters)
    -> bool
{
    return RunReadback(*this, EResourceKind::Shared);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RenderTarget_MsaaReadback_ArrayRejected,
    "CkTests.UnitTests.CkRenderTarget.MsaaReadback.ArrayRejected", TestFlags)

auto
    FCkTest_RenderTarget_MsaaReadback_ArrayRejected::
    RunTest(
        const FString& InParameters)
    -> bool
{
    if (NOT RequireD3D12(*this))
    {
        return false;
    }
    const auto Fixture = MakeShared<FFixture, ESPMode::ThreadSafe>();
    ENQUEUE_RENDER_COMMAND(CkTestsCreateMsaaArray)([Fixture](FRHICommandListImmediate& Commands)
    {
        const auto Desc = FRHITextureCreateDesc::Create2DArray(TEXT("CkTests.MsaaReadback.Array"))
            .SetExtent(TextureWidth, TextureHeight).SetArraySize(2).SetFormat(PF_B8G8R8A8)
            .SetNumMips(1).SetNumSamples(RequestedSamples)
            .SetFlags(ETextureCreateFlags::RenderTargetable | ETextureCreateFlags::ShaderResource)
            .SetClearValue(FClearValueBinding::Black).SetInitialState(ERHIAccess::RTV);
        Fixture->Subject = Commands.CreateTexture(Desc);
    });
    FlushRenderingCommands();
    if (NOT TestTrue(TEXT("RHI creates a legitimate multisampled array"), Fixture->Subject.IsValid()))
    {
        return false;
    }
    if (NOT TestEqual(TEXT("Array has four effective samples"), Fixture->Subject->GetNumSamples(), uint32{4}))
    {
        return false;
    }
    TestEqual(TEXT("Array has two slices"), Fixture->Subject->GetDesc().ArraySize, uint16{2});
    AddExpectedError(TEXT("D3D12 multisampled array readback is unsupported; returning no pixels."),
        EAutomationExpectedErrorFlags::Contains, 2);
    struct FArrayResult
    {
        TArray<FColor> Colors{FColor::Magenta};
        TArray<FLinearColor> LinearColors{FLinearColor::Red};
        bool ResourceUnchanged = false;
    };
    const auto Result = MakeShared<FArrayResult, ESPMode::ThreadSafe>();
    {
        const auto Owner = Fixture->Subject;
        ENQUEUE_RENDER_COMMAND(CkTestsRejectMsaaArray)([Result, Owner](FRHICommandListImmediate& Commands)
        {
            const auto Before = Owner->GetDesc();
            Commands.ReadSurfaceData(Owner, FullRect, Result->Colors, FReadSurfaceDataFlags{RCM_MinMax});
            Commands.ReadSurfaceData(Owner, FullRect, Result->LinearColors, FReadSurfaceDataFlags{RCM_MinMax});
            Result->ResourceUnchanged = Owner.IsValid() && Owner->GetDesc().NumSamples == Before.NumSamples &&
                Owner->GetDesc().ArraySize == Before.ArraySize && Owner->GetDesc().Flags == Before.Flags &&
                Owner->GetDesc().Extent == Before.Extent && Owner->GetDesc().Format == Before.Format;
        });
    }
    Fixture->Subject.SafeRelease();
    FlushRenderingCommands();
    TestTrue(TEXT("FColor unsupported read discards sentinel output"), Result->Colors.IsEmpty());
    TestTrue(TEXT("FLinearColor unsupported read discards sentinel output"), Result->LinearColors.IsEmpty());
    TestTrue(TEXT("Rejected reads preserve array resource validity and descriptor"), Result->ResourceUnchanged);
    return NOT HasAnyErrors();
}

#endif

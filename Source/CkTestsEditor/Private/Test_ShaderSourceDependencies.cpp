#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && __has_include("ShaderSourceDependencies.h")
#include "ShaderSourceDependencies.h"

#if defined(UE_SHADER_SOURCE_DEPENDENCIES_API_VERSION) && UE_SHADER_SOURCE_DEPENDENCIES_API_VERSION >= 1

#include "CkCore/Macros/CkMacros.h"
#include "CkUsfEditor/Reload/CkUsf_ShaderReload.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "RHIGlobals.h"
#include "ShaderCompilerCore.h"

namespace ck_test_shader_source_dependencies
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    struct FFixture
    {
        FString _Directory;
        FString _VirtualRoot;

        auto
            Initialize(
                FAutomationTestBase& InTest)
            -> bool
        {
            const auto Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            _Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
                / TEXT("Automation/ShaderSourceDependencies") / Id);
            _VirtualRoot = TEXT("/CkDependencyTest_") + Id;
            constexpr auto CreateTree = true;
            if (NOT InTest.TestTrue(TEXT("Create owned shader source fixture directory"),
                IFileManager::Get().MakeDirectory(*_Directory, CreateTree)))
            { return false; }
            // Shader mappings have no individual removal API; retain this unique mapping and its Saved sources.
            AddShaderSourceDirectoryMapping(_VirtualRoot, _Directory);
            return true;
        }

        auto
            Path(
                const TCHAR* InName) const
            -> FString
        {
            return _VirtualRoot / InName;
        }

        auto
            Write(
                FAutomationTestBase& InTest,
                const TCHAR* InName,
                const FString& InSource) const
            -> bool
        {
            const auto DiskPath = _Directory / InName;
            constexpr auto CreateTree = true;
            if (NOT InTest.TestTrue(TEXT("Create owned source parent directory"),
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(DiskPath), CreateTree)))
            { return false; }
            return InTest.TestTrue(TEXT("Write owned shader source"), FFileHelper::SaveStringToFile(
                InSource, *DiskPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
        }
    };

    auto
        CheckPlatform(
            FAutomationTestBase& InTest)
        -> bool
    {
        if (NOT InTest.TestTrue(TEXT("Active shader platform is in bounds"),
            static_cast<uint32>(GMaxRHIShaderPlatform) < static_cast<uint32>(SP_NumPlatforms)))
        { return false; }
        return InTest.TestTrue(TEXT("Active shader platform metadata is initialized"),
            FDataDrivenShaderPlatformInfo::IsValid(GMaxRHIShaderPlatform));
    }

    auto
        CheckEdges(
            FAutomationTestBase& InTest,
            const FShaderSourceDependencies& InResult,
            const FString& InParent,
            const TArray<FString>& InExpected)
        -> void
    {
        const auto* Edges = InResult.DirectIncludes.Find(InParent);
        if (NOT InTest.TestNotNull(*FString::Printf(TEXT("Readable parent has direct edges: %s"), *InParent), Edges))
        { return; }
        InTest.TestTrue(TEXT("Direct edges preserve source order and exclude transitive edges"), *Edges == InExpected);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkShaderDependenciesMissingSources,
    "CkTests.UnitTests.CkUsf.ShaderSourceDependencies.MissingSourcesPreserveDirectEdges",
    ck_test_shader_source_dependencies::TestFlags)

auto
    FCkShaderDependenciesMissingSources::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_shader_source_dependencies;
    if (NOT TestTrue(TEXT("Foundation enables shader reload when the compiled engine exposes dependency API v1"),
        ck::usf_editor::FShaderReload::Get_IsSupported()))
    { return false; }
    auto Fixture = FFixture{};
    if (NOT CheckPlatform(*this) || NOT Fixture.Initialize(*this))
    { return false; }
    if (NOT Fixture.Write(*this, TEXT("Root.ush"), TEXT("#include \"Child.ush\"\n#include \"Absent.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("Child.ush"), TEXT("#include \"AbsentGrandchild.ush\"\n#include \"Empty.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("Empty.ush"), FString{}))
    { return false; }
    auto Result = FShaderSourceDependencies{};
    auto Error = FString{};
    TestFalse(TEXT("Missing child and grandchild make traversal incomplete"),
        TryGetShaderSourceDependencies(*Fixture.Path(TEXT("Root.ush")), Result, GMaxRHIShaderPlatform, &Error));
    TestEqual(TEXT("Root identity is retained"), Result.RootVirtualFilePath, Fixture.Path(TEXT("Root.ush")));
    CheckEdges(*this, Result, Fixture.Path(TEXT("Root.ush")), {Fixture.Path(TEXT("Child.ush")), Fixture.Path(TEXT("Absent.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("Child.ush")), {Fixture.Path(TEXT("AbsentGrandchild.ush")), Fixture.Path(TEXT("Empty.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("Empty.ush")), {});
    TestEqual(TEXT("Only readable sources have edge arrays"), Result.DirectIncludes.Num(), 3);
    TestTrue(TEXT("Unavailable identities include direct and transitive missing sources exactly once"),
        Result.UnavailableSources.Num() == 2 && Result.UnavailableSources.Contains(Fixture.Path(TEXT("Absent.ush")))
            && Result.UnavailableSources.Contains(Fixture.Path(TEXT("AbsentGrandchild.ush"))));
    TestTrue(TEXT("Missing children leave readable parent edges complete"), Result.IncompleteSources.IsEmpty());
    TestTrue(TEXT("Incomplete traversal reports an ordinary error"), NOT Error.IsEmpty());
    TestFalse(TEXT("Absent root is rejected"), TryGetShaderSourceDependencies(
        *Fixture.Path(TEXT("MissingRoot.ush")), Result, GMaxRHIShaderPlatform));
    TestTrue(TEXT("Absent root has no readable graph and replaces previous unavailable identities"),
        Result.DirectIncludes.IsEmpty() && Result.UnavailableSources == TArray<FString>{Fixture.Path(TEXT("MissingRoot.ush"))});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkShaderDependenciesInvalidInputs,
    "CkTests.UnitTests.CkUsf.ShaderSourceDependencies.InvalidInputsResetAndAliasSafely",
    ck_test_shader_source_dependencies::TestFlags)

auto
    FCkShaderDependenciesInvalidInputs::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_shader_source_dependencies;
    auto Fixture = FFixture{};
    if (NOT CheckPlatform(*this) || NOT Fixture.Initialize(*this)
        || NOT Fixture.Write(*this, TEXT("Empty.ush"), FString{}))
    { return false; }
    const TCHAR* InvalidRoots[] = {nullptr, TEXT(""), TEXT("Relative.ush"), TEXT("/Bad/../Root.ush"), TEXT("/Bad/Root.txt")};
    for (const auto* Root : InvalidRoots)
    {
        auto Result = FShaderSourceDependencies{};
        Result.RootVirtualFilePath = TEXT("OldRoot");
        Result.DirectIncludes.Add(TEXT("OldParent"), TArray<FString>{TEXT("OldChild")});
        Result.UnavailableSources.Add(TEXT("OldMissing"));
        Result.IncompleteSources.Add(TEXT("OldPartial"));
        Result.bDepthLimitReached = true;
        Result.bSearchLimitReached = true;
        auto Error = FString{TEXT("OldError")};
        TestFalse(TEXT("Malformed root is rejected without diagnostics"),
            TryGetShaderSourceDependencies(Root, Result, GMaxRHIShaderPlatform, &Error));
        TestTrue(TEXT("Invalid input resets every output field"), Result.RootVirtualFilePath.IsEmpty()
            && Result.DirectIncludes.IsEmpty() && Result.UnavailableSources.IsEmpty() && Result.IncompleteSources.IsEmpty()
            && NOT Result.bDepthLimitReached && NOT Result.bSearchLimitReached);
        TestTrue(TEXT("Invalid input replaces the old error"), NOT Error.IsEmpty() && Error != TEXT("OldError"));
    }
    const EShaderPlatform InvalidPlatforms[] = {SP_NumPlatforms, static_cast<EShaderPlatform>(-1)};
    for (const auto Platform : InvalidPlatforms)
    {
        auto Result = FShaderSourceDependencies{};
        auto Error = FString{};
        TestFalse(TEXT("Out-of-bounds platform is rejected before table access"),
            TryGetShaderSourceDependencies(*Fixture.Path(TEXT("Empty.ush")), Result, Platform, &Error));
        TestTrue(TEXT("Invalid platform does not publish a root or graph"),
            Result.RootVirtualFilePath.IsEmpty() && Result.DirectIncludes.IsEmpty());
        TestTrue(TEXT("Invalid platform has an explicit error"), Error.Contains(TEXT("out of range")));
    }
    for (auto Index = 0u; Index < static_cast<uint32>(SP_NumPlatforms); ++Index)
    {
        const auto Platform = static_cast<EShaderPlatform>(Index);
        if (FDataDrivenShaderPlatformInfo::IsValid(Platform))
        { continue; }
        auto UninitializedResult = FShaderSourceDependencies{};
        auto UninitializedError = FString{};
        TestFalse(TEXT("In-range platform without metadata is rejected before path work"),
            TryGetShaderSourceDependencies(*Fixture.Path(TEXT("Empty.ush")), UninitializedResult,
                Platform, &UninitializedError));
        TestTrue(TEXT("Uninitialized platform produces no graph and names the metadata prerequisite"),
            UninitializedResult.RootVirtualFilePath.IsEmpty() && UninitializedResult.DirectIncludes.IsEmpty()
                && UninitializedError.Contains(TEXT("no initialized metadata")));
    }
    auto Result = FShaderSourceDependencies{};
    Result.RootVirtualFilePath = Fixture.Path(TEXT("Empty.ush"));
    const auto* AliasedRoot = *Result.RootVirtualFilePath;
    auto Error = FString{TEXT("OldError")};
    TestTrue(TEXT("Root pointer may alias the output root storage"),
        TryGetShaderSourceDependencies(AliasedRoot, Result, GMaxRHIShaderPlatform, &Error));
    TestEqual(TEXT("Alias scan keeps the original root"), Result.RootVirtualFilePath, Fixture.Path(TEXT("Empty.ush")));
    CheckEdges(*this, Result, Fixture.Path(TEXT("Empty.ush")), {});
    TestTrue(TEXT("Successful scan resets stale errors"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkShaderDependenciesRelativeGraph,
    "CkTests.UnitTests.CkUsf.ShaderSourceDependencies.RelativeDiamondCycleAndFilters",
    ck_test_shader_source_dependencies::TestFlags)

auto
    FCkShaderDependenciesRelativeGraph::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_shader_source_dependencies;
    auto Fixture = FFixture{};
    if (NOT CheckPlatform(*this) || NOT Fixture.Initialize(*this)
        || NOT Fixture.Write(*this, TEXT("Root.ush"), TEXT("#include \"Sub/A.ush\"\n#include \"B.ush\"\n#include \"Sub/A.ush\"\n#include \"/Engine/Generated/CkAbsentGenerated.ush\"\n#include \"/Engine/Private/Platform/CkAbsentPlatform/Absent.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("Sub/A.ush"), TEXT("#include \"../Shared.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("B.ush"), TEXT("#include \"Shared.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("Shared.ush"), TEXT("#include \"Root.ush\"\n")))
    { return false; }
    auto Result = FShaderSourceDependencies{};
    auto Error = FString{};
    TestTrue(TEXT("Relative diamond with a cycle completes"),
        TryGetShaderSourceDependencies(*Fixture.Path(TEXT("Root.ush")), Result, GMaxRHIShaderPlatform, &Error));
    CheckEdges(*this, Result, Fixture.Path(TEXT("Root.ush")), {Fixture.Path(TEXT("Sub/A.ush")), Fixture.Path(TEXT("B.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("Sub/A.ush")), {Fixture.Path(TEXT("Shared.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("B.ush")), {Fixture.Path(TEXT("Shared.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("Shared.ush")), {Fixture.Path(TEXT("Root.ush"))});
    TestEqual(TEXT("Each readable identity is scanned once"), Result.DirectIncludes.Num(), 4);
    TestTrue(TEXT("Ignored generated and nonmatching platform includes are not missing sources"),
        Result.UnavailableSources.IsEmpty() && Result.IncompleteSources.IsEmpty() && Error.IsEmpty());
    if (NOT Fixture.Write(*this, TEXT("InvalidChild.ush"), TEXT("#include \"Bad.txt\"\n#include \"B.ush\"\n")))
    { return false; }
    TestFalse(TEXT("Invalid child path makes its readable parent incomplete"), TryGetShaderSourceDependencies(
        *Fixture.Path(TEXT("InvalidChild.ush")), Result, GMaxRHIShaderPlatform, &Error));
    CheckEdges(*this, Result, Fixture.Path(TEXT("InvalidChild.ush")), {Fixture.Path(TEXT("B.ush"))});
    TestTrue(TEXT("Only the invalid-path parent has partial edges"),
        Result.IncompleteSources == TArray<FString>{Fixture.Path(TEXT("InvalidChild.ush"))});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkShaderDependenciesLimits,
    "CkTests.UnitTests.CkUsf.ShaderSourceDependencies.DepthAndSearchLimits",
    ck_test_shader_source_dependencies::TestFlags)

auto
    FCkShaderDependenciesLimits::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_shader_source_dependencies;
    auto Fixture = FFixture{};
    if (NOT CheckPlatform(*this) || NOT Fixture.Initialize(*this)
        || NOT Fixture.Write(*this, TEXT("Root.ush"), TEXT("#include \"Child.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("Child.ush"), TEXT("#include \"Empty.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("Empty.ush"), FString{}))
    { return false; }
    auto Result = FShaderSourceDependencies{};
    for (const auto Depth : {0u, 1u})
    {
        TestFalse(TEXT("Depth limit returns an incomplete traversal"), TryGetShaderSourceDependencies(
            *Fixture.Path(TEXT("Root.ush")), Result, GMaxRHIShaderPlatform, nullptr, Depth));
        const auto PartialParent = Fixture.Path(Depth == 0 ? TEXT("Root.ush") : TEXT("Child.ush"));
        TestTrue(TEXT("Depth flag identifies exactly the parent whose includes were not traversed"),
            Result.bDepthLimitReached && NOT Result.bSearchLimitReached
                && Result.IncompleteSources == TArray<FString>{PartialParent} && Result.UnavailableSources.IsEmpty());
        CheckEdges(*this, Result, PartialParent, {});
        if (Depth == 1)
        { CheckEdges(*this, Result, Fixture.Path(TEXT("Root.ush")), {Fixture.Path(TEXT("Child.ush"))}); }
    }
    constexpr auto ZeroDepth = 0u;
    TestTrue(TEXT("Zero depth accepts a readable empty source without undiscovered includes"),
        TryGetShaderSourceDependencies(*Fixture.Path(TEXT("Empty.ush")), Result,
            GMaxRHIShaderPlatform, nullptr, ZeroDepth));
    TestFalse(TEXT("Empty source at zero depth is not a truncated traversal"), Result.bDepthLimitReached);
    auto Source = FString{};
    for (auto Index = 0; Index < 200; ++Index)
    {
        Source += TEXT("#include \"Empty.ush\"\n");
    }
    if (NOT Fixture.Write(*this, TEXT("Exact200.ush"), Source.LeftChop(1))
        || NOT Fixture.Write(*this, TEXT("Over200.ush"), Source + TEXT("#include \"Unvisited.ush\"\n")))
    { return false; }
    TestTrue(TEXT("Exactly 200 includes ending at EOF are complete"), TryGetShaderSourceDependencies(
        *Fixture.Path(TEXT("Exact200.ush")), Result, GMaxRHIShaderPlatform));
    CheckEdges(*this, Result, Fixture.Path(TEXT("Exact200.ush")), {Fixture.Path(TEXT("Empty.ush"))});
    TestFalse(TEXT("Exact EOF does not report a search limit"), Result.bSearchLimitReached);
    TestFalse(TEXT("A remaining 201st directive reports an incomplete traversal"), TryGetShaderSourceDependencies(
        *Fixture.Path(TEXT("Over200.ush")), Result, GMaxRHIShaderPlatform));
    CheckEdges(*this, Result, Fixture.Path(TEXT("Over200.ush")), {Fixture.Path(TEXT("Empty.ush"))});
    TestTrue(TEXT("Search limit does not pretend the unvisited child was read or missing"),
        Result.bSearchLimitReached && NOT Result.bDepthLimitReached && Result.UnavailableSources.IsEmpty()
            && Result.IncompleteSources == TArray<FString>{Fixture.Path(TEXT("Over200.ush"))});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkShaderDependenciesLegacyCompatibility,
    "CkTests.UnitTests.CkUsf.ShaderSourceDependencies.LegacyIncludesAndHashRemainStable",
    ck_test_shader_source_dependencies::TestFlags)

auto
    FCkShaderDependenciesLegacyCompatibility::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_shader_source_dependencies;
    auto Fixture = FFixture{};
    if (NOT CheckPlatform(*this) || NOT Fixture.Initialize(*this)
        || NOT Fixture.Write(*this, TEXT("Root.ush"), TEXT("#include \"A.ush\"\n#include \"B.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("A.ush"), TEXT("#include \"Shared.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("B.ush"), TEXT("#include \"Shared.ush\"\n"))
        || NOT Fixture.Write(*this, TEXT("Shared.ush"), TEXT("float CkDependencyFixtureValue = 0.5;\n")))
    { return false; }
    const auto Root = Fixture.Path(TEXT("Root.ush"));
    auto IncludesBefore = TArray<FString>{};
    GetShaderIncludes(*Root, *Root, IncludesBefore, GMaxRHIShaderPlatform);
    TestTrue(TEXT("Legacy scanner visits the full acyclic diamond before the new scan"),
        IncludesBefore == TArray<FString>{Fixture.Path(TEXT("A.ush")), Fixture.Path(TEXT("Shared.ush")), Fixture.Path(TEXT("B.ush"))});
    auto Error = FString{};
    const auto* HashBeforePointer = TryGetShaderFileHash(*Root, GMaxRHIShaderPlatform, &Error);
    if (NOT TestNotNull(TEXT("Legacy shader hash exists before the new scan"), HashBeforePointer))
    { return false; }
    const auto HashBefore = *HashBeforePointer;
    auto Result = FShaderSourceDependencies{};
    TestTrue(TEXT("New direct-edge scan completes on the same acyclic fixture"),
        TryGetShaderSourceDependencies(*Root, Result, GMaxRHIShaderPlatform, &Error));
    CheckEdges(*this, Result, Root, {Fixture.Path(TEXT("A.ush")), Fixture.Path(TEXT("B.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("A.ush")), {Fixture.Path(TEXT("Shared.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("B.ush")), {Fixture.Path(TEXT("Shared.ush"))});
    CheckEdges(*this, Result, Fixture.Path(TEXT("Shared.ush")), {});
    auto IncludesAfter = TArray<FString>{};
    GetShaderIncludes(*Root, *Root, IncludesAfter, GMaxRHIShaderPlatform);
    TestTrue(TEXT("New scan preserves the legacy include identities and ordering"), IncludesAfter == IncludesBefore);
    const auto* HashAfter = TryGetShaderFileHash(*Root, GMaxRHIShaderPlatform, &Error);
    if (NOT TestNotNull(TEXT("Legacy shader hash exists after the new scan"), HashAfter))
    { return false; }
    TestTrue(TEXT("New scan preserves the legacy shader hash value"), HashBefore == *HashAfter);
    TestTrue(TEXT("Compatible scans produce no error"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkShaderSourceCommentStripBoundary,
    "CkTests.UnitTests.CkUsf.ShaderSourceDependencies.CommentStripEmptyAndSimdBoundary",
    ck_test_shader_source_dependencies::TestFlags)

auto
    FCkShaderSourceCommentStripBoundary::
    RunTest(
        const FString&)
    -> bool
{
    auto Sources = TArray<FString>{
        FString{}, FString{}, TEXT("x"), TEXT("123456789012345"),
        TEXT("1234567890123456"), TEXT("12345678901234567")};
    Sources[1] = TEXT("Previous shader source");
    Sources[1].Reset();
    auto Stripped = TArray<ANSICHAR>{};
    for (const auto Flags : {EConvertAndStripFlags::None, EConvertAndStripFlags::NoSimdPadding})
    {
        const auto Padding = Flags == EConvertAndStripFlags::NoSimdPadding ? 1 : 16;
        for (auto SourceIndex = 0; SourceIndex < Sources.Num(); ++SourceIndex)
        {
            const auto& Source = Sources[SourceIndex];
            Stripped.Init(static_cast<ANSICHAR>('x'), 64);
            ShaderConvertAndStripComments(Source, Stripped, Flags);
            const auto Context = FString::Printf(TEXT("Source %d length %d padding %d"),
                SourceIndex, Source.Len(), Padding);
            if (NOT TestEqual(*(Context + TEXT(" resets the reused output to its exact size")),
                Stripped.Num(), Source.Len() + Padding))
            { continue; }
            for (auto Index = 0; Index < Source.Len(); ++Index)
            {
                TestEqual(*(Context + TEXT(" preserves each ASCII character")),
                    Stripped[Index], static_cast<ANSICHAR>(Source[Index]));
            }
            for (auto Index = Source.Len(); Index < Stripped.Num(); ++Index)
            {
                TestEqual(*(Context + TEXT(" zeroes every terminator and SIMD padding byte")),
                    Stripped[Index], static_cast<ANSICHAR>(0));
            }
        }
    }
    return true;
}

#endif
#endif

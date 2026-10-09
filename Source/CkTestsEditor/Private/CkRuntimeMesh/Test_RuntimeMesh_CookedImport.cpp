#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_RuntimeMesh_Cooked_Import,
    "RuntimeMeshCooked.Import",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCkTest_RuntimeMesh_Cooked_Import::RunTest(const FString&)
{
    FString Executable;
    if (NOT TestTrue(TEXT("staged executable argument is required"),
        FParse::Value(FCommandLine::Get(), TEXT("CkRuntimeMeshCookedExecutable="), Executable)
            && NOT Executable.IsEmpty()))
    { return false; }
    Executable = FPaths::ConvertRelativePathToFull(Executable);
    if (NOT TestTrue(TEXT("staged executable exists"), FPaths::FileExists(Executable)))
    { return false; }

    const auto ProbeLogPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("RuntimeMeshCooked") /
        (FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".log")));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ProbeLogPath), true);
    // A cooked game that can never render drops static-mesh render data on load (StaticMesh.cpp, the
    // CanEverRender gate in Serialize), so the probe needs a real RHI; off-screen keeps it headless.
    const auto Arguments = FString::Printf(
        TEXT("-CkRuntimeMeshCookProbe -RenderOffScreen -nosound -unattended -nopause -abslog=\"%s\""), *ProbeLogPath);
    uint32 ProcessID = 0;
    auto Process = FPlatformProcess::CreateProc(*Executable, *Arguments, true, true, true,
        &ProcessID, 0, nullptr, nullptr);
    if (NOT TestTrue(TEXT("staged process launched"), Process.IsValid()))
    { return false; }

    const double Deadline = FPlatformTime::Seconds() + 240.0;
    while (FPlatformProcess::IsProcRunning(Process) && FPlatformTime::Seconds() < Deadline)
    { FPlatformProcess::Sleep(0.1f); }
    const bool TimedOut = FPlatformProcess::IsProcRunning(Process);
    if (TimedOut)
    { FPlatformProcess::TerminateProc(Process, true); }
    int32 ExitCode = -1;
    const bool HasExitCode = FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
    FPlatformProcess::CloseProc(Process);

    FString Log;
    const bool HasLog = FFileHelper::LoadFileToString(Log, *ProbeLogPath);
    TestFalse(TEXT("packaged probe completed before timeout"), TimedOut);
    TestTrue(TEXT("packaged probe wrote an absolute log"), HasLog);
    TestTrue(TEXT("packaged probe emitted its unique PASS verdict"),
        HasLog && Log.Contains(TEXT("CK_RUNTIME_MESH_COOK_PROBE PASS")));
    TestFalse(TEXT("packaged probe did not emit FAIL"),
        HasLog && Log.Contains(TEXT("CK_RUNTIME_MESH_COOK_PROBE FAIL")));
    TestTrue(TEXT("packaged process exposed an exit code"), HasExitCode);
    TestEqual(TEXT("packaged process exited successfully"), ExitCode, 0);
    return NOT TimedOut && HasLog && HasExitCode && ExitCode == 0
        && Log.Contains(TEXT("CK_RUNTIME_MESH_COOK_PROBE PASS"))
        && NOT Log.Contains(TEXT("CK_RUNTIME_MESH_COOK_PROBE FAIL"));
}

#endif

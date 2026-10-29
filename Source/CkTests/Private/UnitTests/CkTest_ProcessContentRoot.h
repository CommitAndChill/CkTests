#pragma once

// A content root only THIS editor process mounts, for tests that must save real packages.
//
// The toolbox runs a gate on several editors of one project at once, and every one of them watches every
// content directory it has mounted (/Game, each plugin's Content/). A package a test saves there is seen by
// the other editors' asset-registry directory watchers: they gather it after the test has deleted it
// ("LogAssetRegistry OpenFile failed", raised as an error in whatever functional test they are running),
// or load it and react to it (EntityScript blueprints get a SpawnParams struct saved by the OTHER editor,
// which then holds the file while the owning test deletes it: Error Code 32).
//
// A root mounted at runtime by one process is in no other editor's watch list, so none of that can happen.
// The directory sits under Saved/Automation, beside the other mounted test fixtures, and not under
// Saved/Automation/Tmp, which an editor starting its test session deletes.
//
// The mount stays for the process: tests that generate into it keep the in-memory packages alive across
// runs in one editor session, and those must stay resolvable.

#if WITH_EDITOR

#include "CoreMinimal.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

#include "CkCore/Macros/CkMacros.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test
{
    // Returns "/<InName>_P<pid>" (no trailing slash), mounted on first use.
    inline auto Get_ProcessContentRoot(const FString& InName) -> FString
    {
        const auto Root = FString::Printf(TEXT("/%s_P%u"), *InName, FPlatformProcess::GetCurrentProcessId());
        const auto MountRoot = Root + TEXT("/");

        if (NOT FPackageName::MountPointExists(MountRoot))
        {
            const auto Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
                / TEXT("Automation") / InName / FString::Printf(TEXT("P%u/"), FPlatformProcess::GetCurrentProcessId()));
            IFileManager::Get().MakeDirectory(*Directory, true);
            FPackageName::RegisterMountPoint(MountRoot, Directory);
        }

        return Root;
    }
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR

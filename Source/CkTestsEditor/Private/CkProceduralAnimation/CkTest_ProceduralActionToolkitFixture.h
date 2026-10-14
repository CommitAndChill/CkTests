#pragma once

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorData.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_EditorToolkit.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "SEditorViewport.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Text/STextBlock.h"

// --------------------------------------------------------------------------------------------------------------------

// A disposable copy of a shipped example clip, opened in the clip editor in its own content root with a private
// transaction buffer, so the example on disk and the editor's undo history are never touched.
namespace ck_test_procedural_action_toolkit
{
    inline const TCHAR* BodyPulsePath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA");
    inline const TCHAR* BodyPulsePreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_Preview_DA.Ck_ProceduralAction_BodyPulse_Preview_DA");
    inline const TCHAR* LimbNodPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_DA.Ck_ProceduralAction_LimbNod_DA");
    inline const TCHAR* LimbNodPreviewPath = TEXT("/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_LimbNod_Preview_DA.Ck_ProceduralAction_LimbNod_Preview_DA");

    inline auto
        FindTagged(
            const TSharedRef<SWidget>& InWidget,
            FName InTag,
            int32 InDepth = 0)
        -> TSharedPtr<SWidget>
    {
        if (InDepth > 64)
        { return {}; }
        if (InWidget->GetTag() == InTag)
        { return InWidget; }
        auto* Children = InWidget->GetChildren();
        for (auto Index = 0; Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindTagged(Children->GetChildAt(Index), InTag, InDepth + 1))
            { return Found; }
        }
        return {};
    }

    inline auto
        FindInToolkit(
            FCk_ProceduralAction_EditorToolkit& InToolkit,
            FName InTag)
        -> TSharedPtr<SWidget>
    {
        const auto TabManager = InToolkit.GetAssociatedTabManager();
        if (NOT TabManager.IsValid())
        { return {}; }
        for (const auto TabId : {FCk_ProceduralAction_EditorToolkit::ViewportTabId, FCk_ProceduralAction_EditorToolkit::ChannelsTabId,
            FCk_ProceduralAction_EditorToolkit::DetailsTabId, FCk_ProceduralAction_EditorToolkit::TimelineTabId,
            FCk_ProceduralAction_EditorToolkit::CurvesTabId})
        {
            const auto Tab = TabManager->FindExistingLiveTab(FTabId{TabId});
            if (NOT Tab.IsValid())
            { continue; }
            if (const auto Found = FindTagged(Tab->GetContent(), InTag))
            { return Found; }
        }
        return {};
    }

    inline auto
        TextOf(
            FCk_ProceduralAction_EditorToolkit& InToolkit,
            FName InTag)
        -> FString
    {
        const auto Widget = FindInToolkit(InToolkit, InTag);
        return Widget.IsValid() && Widget->GetType() == FName{TEXT("STextBlock")}
            ? StaticCastSharedPtr<STextBlock>(Widget)->GetText().ToString() : FString{};
    }

    inline auto
        FindViewportClient(
            FCk_ProceduralAction_EditorToolkit& InToolkit)
        -> TSharedPtr<FEditorViewportClient>
    {
        const auto Widget = FindInToolkit(InToolkit, FName{TEXT("CkActionEditor.Viewport")});
        return Widget.IsValid() && Widget->GetType() == FName{TEXT("SCkProceduralAction_Viewport")}
            ? StaticCastSharedPtr<SEditorViewport>(Widget)->GetViewportClient() : TSharedPtr<FEditorViewportClient>{};
    }

    struct FDisposableClipToolkit
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UAssetEditorSubsystem> Editors;
        TStrongObjectPtr<UCk_ProceduralAction_Data> Clip;
        TStrongObjectPtr<UTransactor> PreviousTransactions;
        TStrongObjectPtr<UTransBuffer> PrivateTransactions;
        FString MountRoot;
        FString MountDirectory;
        FString PackageName;
        FString LastWaitReason;
        double DeadlineSeconds = 0.0;
        int32 Stage = 0;
        int32 Frames = 0;
        bool Mounted = false;

        auto Toolkit() const -> FCk_ProceduralAction_EditorToolkit*
        {
            auto* Instance = Editors.IsValid() && Clip.IsValid() ? Editors->FindEditorForAsset(Clip.Get(), false) : nullptr;
            if (Instance == nullptr || Instance->GetEditorName() != FCk_ProceduralAction_EditorToolkit::ToolkitName)
            { return nullptr; }
            return static_cast<FCk_ProceduralAction_EditorToolkit*>(Instance);
        }

        // Waits for InCondition; after InMaxFrames editor frames it gives up and the caller asserts.
        auto Settle(bool InCondition, const FString& InReason, int32 InMaxFrames = 120) -> bool
        {
            if (InCondition)
            { Frames = 0; return true; }
            LastWaitReason = InReason;
            if (++Frames < InMaxFrames)
            { return false; }
            Frames = 0;
            return true;
        }

        auto Fail(const FString& InMessage) -> bool
        {
            const auto* Editor = Toolkit();
            Test->AddError(FString::Printf(TEXT("%s (stage %d; last wait: %s; message: %s)"), *InMessage, Stage, *LastWaitReason,
                Editor != nullptr ? *Editor->Get_Message() : TEXT("")));
            Close();
            return true;
        }

        auto Open(const TCHAR* InSourcePath, const TCHAR* InPreviewPath, const FString& InName) -> bool
        {
            if (GEditor == nullptr || GEditor->Trans == nullptr || GEditor->Trans->IsActive())
            { return false; }
            const auto Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            MountRoot = FString::Printf(TEXT("/CkActionToolkitFixture_%s/"), *Unique);
            MountDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("ProceduralAction"),
                TEXT("ToolkitFixture"), Unique) + TEXT("/");
            if (NOT IFileManager::Get().MakeDirectory(*MountDirectory, true))
            { return false; }
            FPackageName::RegisterMountPoint(MountRoot, MountDirectory);
            Mounted = true;
            PreviousTransactions.Reset(GEditor->Trans.Get());
            PrivateTransactions.Reset(NewObject<UTransBuffer>());
            PrivateTransactions->Initialize(8 * 1024 * 1024);
            GEditor->Trans = PrivateTransactions.Get();

            PackageName = MountRoot + InName;
            auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, InSourcePath);
            auto* Package = CreatePackage(*PackageName);
            if (Source == nullptr || Package == nullptr)
            { return false; }
            Clip.Reset(DuplicateObject<UCk_ProceduralAction_Data>(Source, Package, FName{*InName}));
            if (NOT Clip.IsValid())
            { return false; }
            Clip->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
            auto Subject = FCk_ProceduralActionEditor_Subject{};
            Subject.Set_Scenario(TSoftObjectPtr<UCk_ProceduralActionPreview_Config>{FSoftObjectPath{InPreviewPath}});
            UCk_Utils_ProceduralActionEditor_UE::Get_EditorData(Clip.Get())->Set_Subject(Subject);
            Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
            return Editors.IsValid() && Editors->OpenEditorForAsset(Clip.Get(), EToolkitMode::Standalone);
        }

        auto Close() -> void
        {
            if (Editors.IsValid() && Clip.IsValid())
            { Editors->CloseAllEditorsForAsset(Clip.Get()); }
            Clip.Reset();
            if (PrivateTransactions.IsValid())
            { PrivateTransactions->Reset(FText::FromString(TEXT("Discard disposable clip editor transactions"))); }
            if (GEditor != nullptr && PrivateTransactions.IsValid() && GEditor->Trans.Get() == PrivateTransactions.Get())
            { GEditor->Trans = PreviousTransactions.Get(); }
            PrivateTransactions.Reset();
            PreviousTransactions.Reset();
            if (auto* Package = PackageName.IsEmpty() ? nullptr : FindPackage(nullptr, *PackageName))
            {
                auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
                Unload.bResetTransBuffer = false;
                UPackageTools::UnloadPackages(Unload);
            }
            PackageName.Reset();
            CollectGarbage(RF_NoFlags);
            if (Mounted)
            {
                FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory);
                IFileManager::Get().DeleteDirectory(*MountDirectory, false, true);
                Mounted = false;
            }
        }

        ~FDisposableClipToolkit() { Close(); }
    };
}

// --------------------------------------------------------------------------------------------------------------------

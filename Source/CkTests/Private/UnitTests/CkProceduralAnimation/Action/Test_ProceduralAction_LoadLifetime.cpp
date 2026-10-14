#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCore/Validation/CkIsValid.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Fragment.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Utils.h"
#include "CkProceduralAnimation/ActionPlayer/CkProceduralActionPlayer_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace ck_test_procedural_action_load
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    constexpr auto WaitSeconds = 10.0;
    const auto SourcePath = FSoftObjectPath{TEXT(
        "/CkFoundation/ProceduralAnimation/Examples/Ck_ProceduralAction_BodyPulse_DA.Ck_ProceduralAction_BodyPulse_DA")};

    struct FState
    {
        FString MountRoot;
        FString MountDirectory;
        FString PackageFile;
        FSoftObjectPath ClipPath;
        bool Mounted = false;
        bool ObservedPending = false;
        bool MissedPending = false;
        FDelegateHandle PendingMonitor;
        FCk_Handle_Transform Root;
        FCk_Handle_ProceduralActionPlayer Player;
        FCk_Handle CancelScope;
        FCk_Handle LostScope;
        FCk_Handle_ProceduralAction CancelAction;
        FCk_Handle_ProceduralAction LostAction;
        FCk_ProceduralAction_Identity CancelIdentity;
        FCk_ProceduralAction_Identity LostIdentity;

        ~FState()
        {
            if (PendingMonitor.IsValid())
            { FWorldDelegates::OnWorldPostActorTick.Remove(PendingMonitor); }
            // A failed latent assertion can skip the explicit completion step below.
            // Drain before unmounting its package path.
            if (NOT ClipPath.IsNull())
            { FlushAsyncLoading(); }
            if (Mounted)
            { FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory); }
            if (NOT PackageFile.IsEmpty())
            { IFileManager::Get().Delete(*PackageFile); }
            if (NOT MountDirectory.IsEmpty())
            { IFileManager::Get().DeleteDirectory(*MountDirectory); }
        }
    };

    auto MakeUniqueDiskClip(FState& InState) -> bool
    {
        const auto Unique = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        InState.MountRoot = FString::Printf(TEXT("/CkActionV19_%s/"), *Unique);
        InState.MountDirectory = FPaths::Combine(FPaths::ProjectSavedDir(),
            TEXT("CkActionV19"), Unique) + TEXT("/");
        if (NOT IFileManager::Get().MakeDirectory(*InState.MountDirectory, true))
        { return false; }
        FPackageName::RegisterMountPoint(InState.MountRoot, InState.MountDirectory);
        InState.Mounted = true;
        const auto PackageName = InState.MountRoot + TEXT("Clip");
        auto* Source = LoadObject<UCk_ProceduralAction_Data>(nullptr, *SourcePath.ToString());
        auto* Package = CreatePackage(*PackageName);
        if (Source == nullptr || Package == nullptr) { return false; }
        auto* Clip = DuplicateObject<UCk_ProceduralAction_Data>(Source, Package, TEXT("Clip"));
        if (Clip == nullptr) { return false; }
        Clip->SetFlags(RF_Public | RF_Standalone);
        Package->MarkPackageDirty();
        InState.PackageFile = FPackageName::LongPackageNameToFilename(
            PackageName, FPackageName::GetAssetPackageExtension());
        auto SaveArgs = FSavePackageArgs{};
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.Error = GLog;
        if (NOT UPackage::SavePackage(Package, Clip, *InState.PackageFile, SaveArgs))
        { return false; }
        auto Unload = UPackageTools::FUnloadPackageParams{TArray<UPackage*>{Package}};
        Unload.bResetTransBuffer = false;
        if (NOT UPackageTools::UnloadPackages(Unload)) { return false; }
        CollectGarbage(RF_NoFlags);
        InState.ClipPath = FSoftObjectPath{PackageName + TEXT(".Clip")};
        return InState.ClipPath.ResolveObject() == nullptr;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkProceduralActionInFlightLoadLifetimeTest,
    "Ck.ProceduralAnimation.Action.Clip.InFlightCancellationAndOwnerLoss",
    ck_test_procedural_action_load::TestFlags)

auto
FCkProceduralActionInFlightLoadLifetimeTest::RunTest(
    const FString&)
    -> bool
{
    using namespace ck_test_procedural_action_load;
    const auto State = MakeShared<FState>();
    if (NOT TestTrue(TEXT("Unique saved clip unloads before async request"),
        MakeUniqueDiskClip(*State)))
    { return false; }

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            auto RootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(InWorld);
            State->Root = UCk_Utils_Transform_UE::Add(RootEntity,
                FTransform{FVector{100.0, 200.0, 2000.0}}, ECk_Replication::DoesNotReplicate);
            auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity,
                FTransform::Identity, ECk_Replication::DoesNotReplicate);
            auto Layout = FCk_ProceduralArticulation_Layout{TArray<FCk_ProceduralLimb_Layout>{}};
            Layout.Set_BodyEnvelopeMin(FVector{-2.0, -2.0, -2.0});
            Layout.Set_BodyEnvelopeMax(FVector{2.0, 2.0, 2.0});
            auto Spec = FCk_ProceduralArticulation_Spec{Layout,
                TArray<FCk_ProceduralLimb_Binding>{}};
            Spec.Set_Presentation(Presentation);
            auto Subject = UCk_Utils_ProceduralArticulation_UE::Add(State->Root, Spec);
            State->Player = UCk_Utils_ProceduralActionPlayer_UE::Add(
                Subject, FCk_ProceduralActionPlayer_Spec{});
            TestTrue(TEXT("In-flight test player attaches"), ck::IsValid(State->Player));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        { return ck::IsValid(State->Player); }), WaitSeconds, TEXT("In-flight player ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld* InWorld)
        {
            if (NOT TestFalse(TEXT("Unique clip remains nonresident"),
                State->ClipPath.ResolveObject() != nullptr))
            { return; }
            const auto WeakState = TWeakPtr<FState>{State};
            const auto WeakWorld = TWeakObjectPtr<UWorld>{InWorld};
            State->PendingMonitor = FWorldDelegates::OnWorldPostActorTick.AddLambda(
                [WeakState, WeakWorld](UWorld* InTickingWorld, ELevelTick, float)
                {
                    if (InTickingWorld != WeakWorld.Get()) { return; }
                    const auto Pinned = WeakState.Pin();
                    if (NOT Pinned.IsValid() || Pinned->ObservedPending
                        || Pinned->MissedPending) { return; }
                    const auto IsPending = [](const FCk_Handle_ProceduralAction& InAction)
                    {
                        if (ck::Is_NOT_Valid(InAction)
                            || NOT InAction.Has<ck::FFragment_ProceduralAction_Loading>())
                        { return false; }
                        const auto& Assets = InAction.Get<ck::FFragment_ProceduralAction_Loading>()
                            .Get_Assets();
                        return Assets.Get_IsRequested() && NOT Assets.Get_IsReady()
                            && NOT Assets.Get_HasFailed();
                    };
                    if (IsPending(Pinned->CancelAction) && IsPending(Pinned->LostAction))
                    {
                        Pinned->ObservedPending = true;
                        UCk_Utils_ProceduralAction_UE::Request_Cancel(Pinned->CancelAction,
                            FCk_Request_ProceduralAction_Cancel{FName{TEXT("CancelDuringLoad")}}, {});
                        UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Pinned->LostScope);
                    }
                    else if (ck::IsValid(Pinned->CancelAction)
                        && UCk_Utils_ProceduralAction_UE::Get_State(Pinned->CancelAction)
                            != ECk_ProceduralAction_State::PendingLoad)
                    { Pinned->MissedPending = true; }
                });
            State->CancelScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            State->LostScope = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(State->Root);
            const auto Play = FCk_ProceduralAction_PlaySpec{
                TSoftObjectPtr<UCk_ProceduralAction_Data>{State->ClipPath}};
            State->CancelAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(
                State->CancelScope, State->Player, Play);
            State->LostAction = UCk_Utils_ProceduralAction_UE::Create_FromClip(
                State->LostScope, State->Player, Play);
            State->CancelIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->CancelAction);
            State->LostIdentity = UCk_Utils_ProceduralAction_UE::Get_Identity(State->LostAction);
            TestTrue(TEXT("Both soft clips reserve before first load tick"),
                ck::IsValid(State->CancelAction) && ck::IsValid(State->LostAction));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            return State->ObservedPending || State->MissedPending;
        }), WaitSeconds, TEXT("Post-actor observer classified real rooted load state")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            TestTrue(TEXT("Cancellation and owner loss queued while real IO was pending"),
                State->ObservedPending && NOT State->MissedPending);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            if (ck::Is_NOT_Valid(State->Player)) { return false; }
            return UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->CancelIdentity, 0, 8).Get_Terminal()
                && UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, State->LostIdentity, 0, 8).Get_Terminal();
        }), WaitSeconds, TEXT("Cancelled and dead-owner streams terminate after pending IO")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            const auto CancelPage = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->CancelIdentity, 0, 8);
            const auto LostPage = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                State->Player, State->LostIdentity, 0, 8);
            TestTrue(TEXT("No accepted pose after in-flight cancellation"),
                CancelPage.Get_Records().ContainsByPredicate([](const auto& InRecord)
                { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose; }) == false
                    && LostPage.Get_Records().ContainsByPredicate([](const auto& InRecord)
                    { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose; }) == false);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(8));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, State](UWorld*)
        {
            FlushAsyncLoading();
            CollectGarbage(RF_NoFlags);
            for (const auto Identity : {State->CancelIdentity, State->LostIdentity})
            {
                const auto Page = UCk_Utils_ProceduralActionPlayer_UE::Read_Receipts(
                    State->Player, Identity, 0, 8);
                TestTrue(TEXT("Late completion cannot append accepted geometry"),
                    Page.Get_Terminal() && NOT Page.Get_Records().ContainsByPredicate([](const auto& InRecord)
                    { return InRecord.Get_Kind() == ECk_ProceduralAction_RecordKind::AcceptedPose; }));
            }
            auto Root = State->Root.ConvertToHandle();
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Root);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

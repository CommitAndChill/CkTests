#include "CameraLayer_AdmissionProbe.h"

#include "CkCamera/Camera/CameraLayer/CkCameraLayer_Fragment.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"

UCk_CameraLayer_LifecycleProbe::UCk_CameraLayer_LifecycleProbe()
{
    _TickMode = ECk_Camera_TickMode::Tick;
}

void UCk_CameraLayer_LifecycleProbe::Tick(FCk_Handle_CameraLayer InHandle, FCk_Time InDeltaT)
{
    ++ObservedTicks;
    if (NOT Armed || Fired)
    { return; }
    Fired = true;

    if (Mutation == EMutation::DisconnectSibling)
    {
        auto Camera = Get_OwningCamera();
        auto Entry = Sibling;
        MutationSucceeded = ck::IsValid(Camera) && ck::IsValid(Entry) &&
            ck::FUtils_RecordOfCameraLayers::Get_ContainsEntry(Camera, Entry) &&
            ck::FUtils_RecordOfCameraLayers::Request_Disconnect(Camera, Entry);
    }
    else if (Mutation == EMutation::RetireOwner)
    {
        auto Owned = Owner;
        if (ck::IsValid(Owned))
        {
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Owned);
            MutationSucceeded = UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Owned, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
        }
    }
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkCamera/Camera/CkCamera_Component.h"
#include "CkCamera/Camera/CkCamera_Fragment.h"
#include "CkCamera/Camera/CkCamera_Utils.h"
#include "CkCamera/Camera/CameraLayer/CkCameraLayer_Fragment.h"
#include "CkCamera/Camera/CameraLayer/CkCameraLayer_Utils.h"
#include "CkCamera/Camera/CameraLayer/EntityScripts/CkCameraLayer_EntityScript.h"
#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace ck_tests_camera_layer_admission
{
    struct FMutationCase
    {
        FCk_Handle Owner;
        FCk_Handle_Camera Camera;
        FCk_Handle_CameraLayer Mutator;
        FCk_Handle_CameraLayer Observer;
        TStrongObjectPtr<UCk_CameraComponent> Component;
        TStrongObjectPtr<UCk_CameraLayer_LifecycleProbe> MutatorScript;
        TStrongObjectPtr<UCk_CameraLayer_LifecycleObserver> ObserverScript;
        FCk_CameraProfile ProfileBefore;
        TSubclassOf<UCk_CameraLayer_EntityScript> DominantBefore;
        int32 ObserverTicksBefore = 0;
        bool Ready = false;
    };

    struct FScenario
    {
        FMutationCase Disconnect;
        FMutationCase Retire;
        FCk_Handle Owner;
        FCk_Handle FailedOwner;
        FCk_Handle_Transform ObservedAnchor;
        FCk_Handle_Camera Camera;
        FCk_Handle_CameraLayer Sibling;
        TArray<FCk_Handle_CameraLayer> RecordBeforeRejectedRequest;
        TStrongObjectPtr<UCk_CameraComponent> Component;
        TStrongObjectPtr<UCk_CameraLayer_AdmissionProbe> Probe;
        bool Ready = false;
    };
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkTest_CameraLayerAdmission,
    "Ck.Camera.LayerAdmission.Network",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkTest_CameraLayerAdmission::RunTest(const FString& Parameters)
{
    using namespace ck_tests_camera_layer_admission;
    auto Scenario = MakeShared<FScenario>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld* World)
        {
            AddExpectedError(TEXT("Create CameraLayer: invalid camera"),
                EAutomationExpectedErrorFlags::Contains, 0);
            auto Default = FCk_Handle_Camera{};
            TestFalse(TEXT("default camera rejects without mutation"), ck::IsValid(
                UCk_Utils_CameraLayer_UE::Create(Default, UCk_CameraLayer_Default_EntityScript::StaticClass())));

            Scenario->Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            if (NOT TestTrue(TEXT("real world owner exists"), ck::IsValid(Scenario->Owner)))
            { return; }
            auto Transform = UCk_Utils_Transform_UE::Add(
                Scenario->Owner, FTransform::Identity, ECk_Replication::DoesNotReplicate);
            Scenario->Component.Reset(NewObject<UCk_CameraComponent>(World));
            if (NOT TestTrue(TEXT("real transform and camera component exist"),
                ck::IsValid(Transform) && Scenario->Component.IsValid()))
            { return; }
            Scenario->Camera = UCk_Utils_Camera_UE::Add(
                Transform, FCk_Camera_Spec{Scenario->Component.Get()});
            if (NOT TestTrue(TEXT("real camera composed"), ck::IsValid(Scenario->Camera)))
            { return; }
            const auto OriginalDirector = Scenario->Component->Get_DirectorEntity();
            const auto OriginalRows = ck::FUtils_RecordOfCameraLayers::Get_Entries(Scenario->Camera);
            const auto OriginalProfile = UCk_Utils_Camera_UE::Get_ComposedProfile(Scenario->Camera);
            AddExpectedError(TEXT("Camera Add requires a fresh unclosed director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            const auto Duplicate = UCk_Utils_Camera_UE::Add(
                Transform, FCk_Camera_Spec{Scenario->Component.Get()});
            TestFalse(TEXT("duplicate Camera Add rejects"), ck::IsValid(Duplicate));
            TestTrue(TEXT("duplicate Add preserves exact director binding"),
                Scenario->Component->Get_DirectorEntity() == OriginalDirector);
            TestTrue(TEXT("duplicate Add preserves exact layer record"),
                ck::FUtils_RecordOfCameraLayers::Get_Entries(Scenario->Camera) == OriginalRows);
            const auto AfterDuplicateProfile = UCk_Utils_Camera_UE::Get_ComposedProfile(Scenario->Camera);
            TestTrue(TEXT("duplicate Add preserves the composed profile"),
                FCk_CameraProfile::StaticStruct()->CompareScriptStruct(
                    &OriginalProfile, &AfterDuplicateProfile, PPF_None));
            AddExpectedError(TEXT("Create CameraLayer: invalid layer class"),
                EAutomationExpectedErrorFlags::Contains, 0);
            TestFalse(TEXT("missing layer class rejects without admitting a child"), ck::IsValid(
                UCk_Utils_CameraLayer_UE::Create(
                    Scenario->Camera, TSubclassOf<UCk_CameraLayer_EntityScript>{})));
            Scenario->Sibling = UCk_Utils_CameraLayer_UE::Create(
                Scenario->Camera, UCk_CameraLayer_Default_EntityScript::StaticClass());
            if (NOT TestTrue(TEXT("real existing sibling admitted"), ck::IsValid(Scenario->Sibling)))
            { return; }
            Scenario->Sibling.Get<ck::FFragment_CameraLayer_Params>().Set_Priority(7);
            auto& Blend = Scenario->Sibling.Get<ck::FFragment_CameraLayer_Blend>();
            Blend.Set_Alpha(1.0f);
            Blend.Set_TargetAlpha(1.0f);

            // Preserve a real, otherwise-complete camera while corrupting only its
            // owner link for this same-frame call. Registry/world remain healthy.
            const auto OriginalOwner = Scenario->Camera.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
            const auto RowsBefore = ck::FUtils_RecordOfCameraLayers::Get_Entries(Scenario->Camera);
            if (NOT TestTrue(TEXT("camera owner can be faulted in place"),
                Scenario->Camera.Try_Remove<ck::FFragment_LifetimeOwner>()))
            { return; }
            Scenario->Camera.Add<ck::FFragment_LifetimeOwner>(FCk_Handle{Scenario->Camera});
            AddExpectedError(TEXT("has unsafe owner world"), EAutomationExpectedErrorFlags::Contains, 0);
            auto RejectedForCycle = UCk_Utils_CameraLayer_UE::Create(
                Scenario->Camera, UCk_CameraLayer_Default_EntityScript::StaticClass());
            Scenario->Camera.Try_Remove<ck::FFragment_LifetimeOwner>();
            Scenario->Camera.Add<ck::FFragment_LifetimeOwner>(OriginalOwner);
            TestFalse(TEXT("cyclic owner rejects before child allocation"), ck::IsValid(RejectedForCycle));
            TestTrue(TEXT("cyclic rejection retains exact camera record membership"),
                ck::FUtils_RecordOfCameraLayers::Get_Entries(Scenario->Camera) == RowsBefore);
            if (ck::IsValid(RejectedForCycle))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(RejectedForCycle); }

            Scenario->Probe.Reset(NewObject<UCk_CameraLayer_AdmissionProbe>(World));
            if (NOT TestTrue(TEXT("completion receiver exists"), Scenario->Probe.IsValid()))
            { return; }
            Scenario->Ready = true;

            auto SetterCompletion = FCk_Delegate_Request_OnCompleted{};
            SetterCompletion.BindDynamic(Scenario->Probe.Get(),
                &UCk_CameraLayer_AdmissionProbe::OnCompleted);
            auto DefaultCamera = FCk_Handle_Camera{};
            AddExpectedError(TEXT("Camera setter requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            TestFalse(TEXT("default Camera setter rejects"), ck::IsValid(
                UCk_Utils_Camera_UE::Request_Set_UseFixedBoomRotation(
                    DefaultCamera, true, SetterCompletion)));
            TestEqual(TEXT("default setter reports failure, never success"), Scenario->Probe->Rejected, 1);
            TestEqual(TEXT("default setter has no success callback"), Scenario->Probe->Succeeded, 0);

            // ExpectedMessages is keyed by pattern. Register each rejection
            // once before both the default and terminal-closed getter checks.
            AddExpectedError(TEXT("Camera layer count requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("Camera layer query requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("Camera dominant layer requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("Camera composed profile requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("Camera view info requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("Camera view anchor requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("Camera boom rotation requires an admitted live director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("Camera tuner profile requires a complete admitted director"),
                EAutomationExpectedErrorFlags::Contains, 0);
            const auto CheckRejectedGetters = [&](const FCk_Handle_Camera& Candidate,
                const TCHAR* Stage)
            {
                TestEqual(*FString::Printf(TEXT("%s layer count rejects"), Stage),
                    UCk_Utils_Camera_UE::Get_LayerCount(Candidate), 0);
                TestFalse(*FString::Printf(TEXT("%s Has_Layer rejects"), Stage),
                    UCk_Utils_Camera_UE::Has_Layer(
                        Candidate, UCk_CameraLayer_Default_EntityScript::StaticClass()));
                TestTrue(*FString::Printf(TEXT("%s dominant class is empty"), Stage),
                    UCk_Utils_Camera_UE::Get_DominantLayerClass(Candidate).Get() == nullptr);
                const auto Profile = UCk_Utils_Camera_UE::Get_ComposedProfile(Candidate);
                const auto EmptyProfile = FCk_CameraProfile{};
                TestTrue(*FString::Printf(TEXT("%s profile is default value"), Stage),
                    FCk_CameraProfile::StaticStruct()->CompareScriptStruct(
                        &Profile, &EmptyProfile, PPF_None));
                const auto View = UCk_Utils_Camera_UE::Get_ViewInfo(Candidate);
                const auto EmptyView = FMinimalViewInfo{};
                TestTrue(*FString::Printf(TEXT("%s view is default value"), Stage),
                    FMinimalViewInfo::StaticStruct()->CompareScriptStruct(
                        &View, &EmptyView, PPF_None));
                TestFalse(*FString::Printf(TEXT("%s anchor is empty"), Stage),
                    ck::IsValid(UCk_Utils_Camera_UE::Get_ViewAnchor(Candidate)));
                TestTrue(*FString::Printf(TEXT("%s boom rotation is default value"), Stage),
                    UCk_Utils_Camera_UE::Get_BoomRotation(Candidate).IsNearlyZero());
                const auto Tuners = UCk_Utils_Camera_UE::Get_Profile(Candidate);
                TestTrue(*FString::Printf(TEXT("%s tuner profile is default value"), Stage),
                    FCk_CameraProfile::StaticStruct()->CompareScriptStruct(
                        &Tuners, &EmptyProfile, PPF_None));
            };
            CheckRejectedGetters(DefaultCamera, TEXT("default"));

            auto FailedOwner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
            Scenario->FailedOwner = FailedOwner;
            if (NOT TestTrue(TEXT("second real owner exists"), ck::IsValid(FailedOwner))) { return; }
            auto FailedTransform = UCk_Utils_Transform_UE::Add(
                FailedOwner, FTransform::Identity, ECk_Replication::DoesNotReplicate);
            TStrongObjectPtr<UCk_CameraComponent> FailedComponent(NewObject<UCk_CameraComponent>(World));
            if (NOT TestTrue(TEXT("fresh failing camera input exists"),
                ck::IsValid(FailedTransform) && FailedComponent.IsValid()))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(FailedOwner); return; }
            auto ObservedAnchor = FCk_Handle_Transform{};
            auto CapturedFailedCamera = FCk_Handle_Camera{};
            const auto Observer = UCk_Utils_Transform_UE::Get_OnAdded().AddLambda(
                [&](const FCk_Handle_Transform& Added)
                {
                    if (NOT ck::IsValid(Added) || Added == FailedTransform ||
                        NOT Added.Has<ck::FFragment_LifetimeOwner>() ||
                        Added.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != FailedOwner)
                    { return; }
                    ObservedAnchor = Added;
                    Scenario->ObservedAnchor = Added;
                    CapturedFailedCamera = UCk_Utils_Camera_UE::Cast(FailedTransform);
                    auto MutableAnchor = FCk_Handle{Added};
                    UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(MutableAnchor);
                });
            AddExpectedError(TEXT("Camera Add could not create its direct view anchor"),
                EAutomationExpectedErrorFlags::Contains, 0);
            const auto FailedAdd = UCk_Utils_Camera_UE::Add(
                FailedTransform, FCk_Camera_Spec{FailedComponent.Get()});
            UCk_Utils_Transform_UE::Get_OnAdded().Remove(Observer);
            TestTrue(TEXT("real TransformAdded observer initiated the new view anchor"),
                ObservedAnchor.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    ObservedAnchor, ECk_EntityLifetime_DestructionPhase::BeginDestroy));
            TestFalse(TEXT("retiring view anchor makes Camera Add reject"), ck::IsValid(FailedAdd));
            TestFalse(TEXT("failed Add did not bind the output component"),
                ck::IsValid(FailedComponent->Get_DirectorEntity()));
            TestFalse(TEXT("failed Add leaves no runnable Params or Camera holder"),
                FailedTransform.Has_Any<ck::FFragment_Camera_Params, ck::FFragment_Camera>());
            TestFalse(TEXT("failed Add leaves no queued Camera requests"),
                FailedTransform.Has<ck::FFragment_Camera_Requests>());
            TestFalse(TEXT("terminal failed Camera Add cannot retry"), ck::IsValid(
                UCk_Utils_Camera_UE::Add(
                    FailedTransform, FCk_Camera_Spec{FailedComponent.Get()})));
            TestFalse(TEXT("closed Camera setter rejects without reviving holders"), ck::IsValid(
                UCk_Utils_Camera_UE::Request_Set_UseFixedBoomRotation(
                    CapturedFailedCamera, true, SetterCompletion)));
            TestEqual(TEXT("closed and default setters report only failure"),
                Scenario->Probe->Rejected, 2);
            TestEqual(TEXT("closed and default setters report no success"),
                Scenario->Probe->Succeeded, 0);
            TestFalse(TEXT("closed setter cannot recreate Camera holders"),
                FailedTransform.Has_Any<ck::FFragment_Camera_Params, ck::FFragment_Camera,
                    ck::FFragment_Camera_Requests>());
            CheckRejectedGetters(CapturedFailedCamera, TEXT("closed"));
            TestFalse(TEXT("closed getters cannot revive Camera holders"),
                FailedTransform.Has_Any<ck::FFragment_Camera_Params, ck::FFragment_Camera,
                    ck::FFragment_Camera_Requests>());
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(FailedOwner);
            FailedComponent.Reset();
            Scenario->Probe->Rejected = 0;

            // A real OneOnly request reaches the normal processor with a class-valid
            // but uninstantiable layer. Rejection must precede sibling fading.
            Scenario->RecordBeforeRejectedRequest =
                ck::FUtils_RecordOfCameraLayers::Get_Entries(Scenario->Camera);
            auto Request = FCk_Request_Camera_AddLayer(UCk_CameraLayer_EntityScript::StaticClass());
            Request.Set_StackingBehavior(ECk_Camera_StackingBehavior::OneOnly);
            Request.Set_Priority(7);
            auto Completed = FCk_Delegate_Request_OnCompleted{};
            Completed.BindDynamic(Scenario->Probe.Get(), &UCk_CameraLayer_AdmissionProbe::OnCompleted);
            AddExpectedError(TEXT("Create CameraLayer: abstract or retiring layer class"),
                EAutomationExpectedErrorFlags::Contains, 0);
            AddExpectedError(TEXT("AddLayer: failed to admit new layer"),
                EAutomationExpectedErrorFlags::Contains, 0);
            UCk_Utils_Camera_UE::Request_AddLayer(Scenario->Camera, Request, Completed);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(2));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld*)
        {
            if (NOT Scenario->Ready)
            { return; }
            if (NOT TestTrue(TEXT("existing sibling remains live"), ck::IsValid(Scenario->Sibling)))
            { return; }
            TestEqual(TEXT("rejected OneOnly completion reports no success"), Scenario->Probe->Succeeded, 0);
            TestEqual(TEXT("rejected OneOnly completion reports failure once"), Scenario->Probe->Rejected, 1);
            TestEqual(TEXT("rejected OneOnly preserves sibling target alpha"),
                Scenario->Sibling.Get<ck::FFragment_CameraLayer_Blend>().Get_TargetAlpha(), 1.0f);
            TestEqual(TEXT("rejected OneOnly adds no new gameplay layer"),
                UCk_Utils_Camera_UE::Get_LayerCount(Scenario->Camera), 1);
            TestTrue(TEXT("rejected OneOnly leaves sibling in camera record"),
                ck::FUtils_RecordOfCameraLayers::Get_ContainsEntry(Scenario->Camera, Scenario->Sibling));
            TestTrue(TEXT("rejected OneOnly keeps the entire camera record unchanged"),
                ck::FUtils_RecordOfCameraLayers::Get_Entries(Scenario->Camera) ==
                    Scenario->RecordBeforeRejectedRequest);

            auto Request = FCk_Request_Camera_AddLayer(UCk_CameraLayer_Default_EntityScript::StaticClass());
            Request.Set_StackingBehavior(ECk_Camera_StackingBehavior::OneOnly);
            Request.Set_Priority(7);
            auto Completed = FCk_Delegate_Request_OnCompleted{};
            Completed.BindDynamic(Scenario->Probe.Get(), &UCk_CameraLayer_AdmissionProbe::OnCompleted);
            UCk_Utils_Camera_UE::Request_AddLayer(Scenario->Camera, Request, Completed);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(2));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld*)
        {
            if (NOT Scenario->Ready)
            { return; }
            TestEqual(TEXT("compatible OneOnly reports one accepted request"), Scenario->Probe->Succeeded, 1);
            auto FoundReplacement = false;
            ck::FUtils_RecordOfCameraLayers::ForEach_ValidEntry(Scenario->Camera,
                [Scenario, &FoundReplacement](FCk_Handle_CameraLayer Layer)
                {
                    if (Layer != Scenario->Sibling &&
                        NOT Layer.Get<ck::FFragment_CameraLayer_Params>().Get_IsDefault() &&
                        Layer.Get<ck::FFragment_CameraLayer_Params>().Get_Priority() == 7)
                    { FoundReplacement = true; }
                });
            TestTrue(TEXT("compatible OneOnly admits a distinct same-priority layer"), FoundReplacement);
            const auto OldSiblingAllocated = Scenario->Sibling.IsValid(
                ck::IsValid_Policy_IncludePendingKill{});
            if (OldSiblingAllocated && ck::IsValid(Scenario->Sibling) &&
                Scenario->Sibling.Has<ck::FFragment_CameraLayer_Blend>())
            {
                TestEqual(TEXT("compatible OneOnly fades prior sibling"),
                    Scenario->Sibling.Get<ck::FFragment_CameraLayer_Blend>().Get_TargetAlpha(), 0.0f);
            }
            else
            {
                TestFalse(TEXT("compatible OneOnly physically retired prior sibling"), OldSiblingAllocated);
            }

            auto Stale = Scenario->Camera;
            UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Scenario->Owner);
            TestFalse(TEXT("retiring camera cannot create another layer"), ck::IsValid(
                UCk_Utils_CameraLayer_UE::Create(Stale, UCk_CameraLayer_Default_EntityScript::StaticClass())));
            const auto RejectionsBeforeStale = Scenario->Probe->Rejected;
            auto SetterCompletion = FCk_Delegate_Request_OnCompleted{};
            SetterCompletion.BindDynamic(Scenario->Probe.Get(),
                &UCk_CameraLayer_AdmissionProbe::OnCompleted);
            TestFalse(TEXT("retiring Camera setter rejects"), ck::IsValid(
                UCk_Utils_Camera_UE::Request_Set_UseFixedBoomRotation(
                    Stale, true, SetterCompletion)));
            TestEqual(TEXT("retiring setter reports one failure"),
                Scenario->Probe->Rejected, RejectionsBeforeStale + 1);
            TestEqual(TEXT("retiring setter never reports success"), Scenario->Probe->Succeeded, 1);
        })));

    // Two distinct cameras exercise callbacks inside the ordinary scheduled
    // lifecycle pass. Create admits real EntityScripts; no processor is called
    // by the fixture and no Active/Blend state is fabricated.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld* World)
        {
            if (NOT Scenario->Ready) { return; }
            const auto BuildCase = [this, World](FMutationCase& Case, const TCHAR* Label)
            {
                Case.Owner = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World);
                if (NOT TestTrue(*FString::Printf(TEXT("%s owner admitted"), Label), ck::IsValid(Case.Owner)))
                { return; }
                auto Transform = UCk_Utils_Transform_UE::Add(
                    Case.Owner, FTransform::Identity, ECk_Replication::DoesNotReplicate);
                Case.Component.Reset(NewObject<UCk_CameraComponent>(World));
                if (NOT TestTrue(*FString::Printf(TEXT("%s input complete"), Label),
                    ck::IsValid(Transform) && Case.Component.IsValid()))
                { return; }
                Case.Camera = UCk_Utils_Camera_UE::Add(Transform, FCk_Camera_Spec{Case.Component.Get()});
                if (NOT TestTrue(*FString::Printf(TEXT("%s director admitted"), Label),
                    ck::IsValid(Case.Camera)))
                { return; }
                Case.Mutator = UCk_Utils_CameraLayer_UE::Create(
                    Case.Camera, UCk_CameraLayer_LifecycleProbe::StaticClass());
                Case.Observer = UCk_Utils_CameraLayer_UE::Create(
                    Case.Camera, UCk_CameraLayer_LifecycleObserver::StaticClass());
                if (NOT TestTrue(*FString::Printf(TEXT("%s two real layer requests admitted"), Label),
                    ck::IsValid(Case.Mutator) && ck::IsValid(Case.Observer)))
                { return; }
                Case.Mutator.Get<ck::FFragment_CameraLayer_Params>().Set_Priority(7);
                Case.Observer.Get<ck::FFragment_CameraLayer_Params>().Set_Priority(9);
            };
            BuildCase(Scenario->Disconnect, TEXT("disconnect"));
            BuildCase(Scenario->Retire, TEXT("retire"));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(3));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld*)
        {
            const auto ArmCase = [this](FMutationCase& Case, const TCHAR* Label,
                UCk_CameraLayer_LifecycleProbe::EMutation Mutation)
            {
                if (NOT TestTrue(*FString::Printf(TEXT("%s layers naturally live"), Label),
                    ck::IsValid(Case.Camera) && ck::IsValid(Case.Mutator) &&
                    ck::IsValid(Case.Observer) &&
                    Case.Camera.Has<ck::FFragment_Camera>() &&
                    Case.Mutator.Has<ck::FFragment_CameraLayer_Blend>() &&
                    Case.Observer.Has<ck::FFragment_CameraLayer_Blend>()))
                { return; }
                if (NOT TestTrue(*FString::Printf(TEXT("%s scripts constructed"), Label),
                    Case.Mutator.Has<ck::FFragment_EntityScript>() &&
                    Case.Observer.Has<ck::FFragment_EntityScript>()))
                { return; }
                Case.MutatorScript.Reset(Cast<UCk_CameraLayer_LifecycleProbe>(
                    Case.Mutator.Get<ck::FFragment_EntityScript>().Get_Script().Get()));
                Case.ObserverScript.Reset(Cast<UCk_CameraLayer_LifecycleObserver>(
                    Case.Observer.Get<ck::FFragment_EntityScript>().Get_Script().Get()));
                if (NOT TestTrue(*FString::Printf(TEXT("%s concrete instances retained"), Label),
                    Case.MutatorScript.IsValid() && Case.ObserverScript.IsValid()))
                { return; }
                if (NOT TestTrue(*FString::Printf(TEXT("%s scheduled Tick observed"), Label),
                    Case.MutatorScript->ObservedTicks > 0 && Case.ObserverScript->ObservedTicks > 0 &&
                    Case.Mutator.Get<ck::FFragment_CameraLayer_Blend>().Get_Alpha() > 0.0f &&
                    Case.Observer.Get<ck::FFragment_CameraLayer_Blend>().Get_Alpha() > 0.0f))
                { return; }
                Case.ProfileBefore = Case.Camera.Get<ck::FFragment_Camera>().Get_ComposedProfile();
                Case.DominantBefore = Case.Camera.Get<ck::FFragment_Camera>().Get_DominantLayerClass();
                Case.ObserverTicksBefore = Case.ObserverScript->ObservedTicks;
                Case.MutatorScript->Sibling = Case.Observer;
                Case.MutatorScript->Owner = Case.Owner;
                Case.MutatorScript->Mutation = Mutation;
                Case.MutatorScript->Armed = true;
                Case.Ready = true;
            };
            ArmCase(Scenario->Disconnect, TEXT("disconnect"),
                UCk_CameraLayer_LifecycleProbe::EMutation::DisconnectSibling);
            ArmCase(Scenario->Retire, TEXT("retire"),
                UCk_CameraLayer_LifecycleProbe::EMutation::RetireOwner);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(2));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld*)
        {
            const auto& Disconnected = Scenario->Disconnect;
            if (Disconnected.Ready)
            {
                TestTrue(TEXT("scheduled Tick disconnected the exact later sibling"),
                    Disconnected.MutatorScript->Fired && Disconnected.MutatorScript->MutationSucceeded);
                TestEqual(TEXT("disconnected sibling received no post-disconnect Tick"),
                    Disconnected.ObserverScript->ObservedTicks, Disconnected.ObserverTicksBefore);
                if (TestTrue(TEXT("director survives sibling disconnect"),
                    ck::IsValid(Disconnected.Camera) &&
                    Disconnected.Camera.Has<ck::FFragment_Camera>()))
                {
                    TestFalse(TEXT("disconnected sibling absent from live record"),
                        ck::FUtils_RecordOfCameraLayers::Get_ContainsEntry(
                            Disconnected.Camera, Disconnected.Observer));
                    TestTrue(TEXT("disconnected sibling cannot remain dominant"),
                        Disconnected.Camera.Get<ck::FFragment_Camera>().Get_DominantLayerClass() !=
                            UCk_CameraLayer_LifecycleObserver::StaticClass());
                    const auto After = Disconnected.Camera.Get<ck::FFragment_Camera>().Get_ComposedProfile();
                    TestTrue(TEXT("disconnect leaves composed tuner profile unchanged"),
                        FCk_CameraProfile::StaticStruct()->CompareScriptStruct(
                            &Disconnected.ProfileBefore, &After, PPF_None));
                }
            }
            const auto& Retired = Scenario->Retire;
            if (Retired.Ready)
            {
                TestTrue(TEXT("scheduled Tick initiated director owner retirement"),
                    Retired.MutatorScript->Fired && Retired.MutatorScript->MutationSucceeded);
                TestEqual(TEXT("later layer did not Tick after director retirement"),
                    Retired.ObserverScript->ObservedTicks, Retired.ObserverTicksBefore);
                if (Retired.Camera.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                    Retired.Camera.Has<ck::FFragment_Camera>())
                {
                    const auto& CameraState = Retired.Camera.Get<ck::FFragment_Camera>();
                    const auto After = CameraState.Get_ComposedProfile();
                    TestTrue(TEXT("retired director did not publish another composed profile"),
                        FCk_CameraProfile::StaticStruct()->CompareScriptStruct(
                            &Retired.ProfileBefore, &After, PPF_None));
                    TestTrue(TEXT("retired director did not publish another dominant layer"),
                        CameraState.Get_DominantLayerClass() == Retired.DominantBefore);
                }
            }
        })));
    // This command runs even if a prior network action failed. Strong UObject
    // references must be dropped before EndPIE destroys their World/GameInstance.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Scenario]()
        {
            if (ck::IsValid(Scenario->Component.Get()))
            { Scenario->Component->Set_DirectorEntity(FCk_Handle_Camera{}); }
            if (ck::IsValid(Scenario->Owner) &&
                NOT UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Scenario->Owner, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Scenario->Owner); }
            if (ck::IsValid(Scenario->FailedOwner) &&
                NOT UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                    Scenario->FailedOwner, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
            { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Scenario->FailedOwner); }
            for (auto* Case : {&Scenario->Disconnect, &Scenario->Retire})
            {
                if (ck::IsValid(Case->Component.Get()))
                { Case->Component->Set_DirectorEntity(FCk_Handle_Camera{}); }
                if (ck::IsValid(Case->Owner) &&
                    NOT UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                        Case->Owner, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Case->Owner); }
                Case->MutatorScript.Reset();
                Case->ObserverScript.Reset();
                Case->Component.Reset();
            }
            Scenario->Probe.Reset();
            Scenario->Component.Reset();
            return NOT Scenario->Probe.IsValid() && NOT Scenario->Component.IsValid() &&
                NOT Scenario->Disconnect.Component.IsValid() &&
                NOT Scenario->Retire.Component.IsValid() &&
                NOT Scenario->Disconnect.MutatorScript.IsValid() &&
                NOT Scenario->Disconnect.ObserverScript.IsValid() &&
                NOT Scenario->Retire.MutatorScript.IsValid() &&
                NOT Scenario->Retire.ObserverScript.IsValid();
        }), TEXT("Camera test drops world-owned references before EndPIE"), true));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([Scenario]()
        {
            return NOT Scenario->Owner.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->FailedOwner.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->ObservedAnchor.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Camera.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Sibling.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Disconnect.Owner.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Disconnect.Camera.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Disconnect.Mutator.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Disconnect.Observer.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Retire.Owner.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Retire.Camera.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Retire.Mutator.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Retire.Observer.IsValid(ck::IsValid_Policy_IncludePendingKill{}) &&
                NOT Scenario->Probe.IsValid() && NOT Scenario->Component.IsValid();
        }), TEXT("Camera test receipts and strong references are gone after EndPIE"), true));
    return true;
}

#endif

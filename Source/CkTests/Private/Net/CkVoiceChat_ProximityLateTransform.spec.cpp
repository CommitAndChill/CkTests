// A talker that joins a Positional3D channel before it has a Transform gets its routing probes as soon as a
// Transform arrives. Until then it has none (proximity routing fails closed for it) and the reconcile keeps its
// dirty tag - the retry - instead of dropping it on the first pass.
//
// Surface in Session Frontend: Ck.VoiceChat.Net.ProximityProbes_ComposeWhenTransformArrives

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"

#include "CkEcsExt/Transform/CkTransform_Utils.h"

#include "CkTests/Net/CkNetAutomation_Common.h"

#include "CkVoiceChat/VoiceChannel/CkVoiceChannel_Utils.h"
#include "CkVoiceChat/VoiceTalker/CkVoiceTalker_Utils.h"

#include "Engine/World.h"

#include <NativeGameplayTags.h>

// --------------------------------------------------------------------------------------------------------------------

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VoiceChat_Channel_AutoTest_LateTransform, TEXT("VoiceChat.Channel.AutoTest.LateTransform"));

// --------------------------------------------------------------------------------------------------------------------

namespace ck_voice_proximity_late_transform_spec
{
    constexpr auto WaitSeconds = 15.0;

    struct FLateTransformSpecState
    {
        FCk_Handle Host;
        FCk_Handle_VoiceChannel Channel;
        FCk_Handle TalkerEntity;
        FCk_Handle_VoiceTalker Talker;
    };

    auto Get_HasProbes(const FCk_Handle_VoiceTalker& InTalker) -> bool
    {
        return InTalker.Has<ck::FFragment_VoiceTalker_Proximity>() &&
            ck::IsValid(InTalker.Get<ck::FFragment_VoiceTalker_Proximity>().Get_RangeProbe());
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkVoiceChatNet_ProximityProbes_ComposeWhenTransformArrives,
    "Ck.VoiceChat.Net.ProximityProbes_ComposeWhenTransformArrives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkVoiceChatNet_ProximityProbes_ComposeWhenTransformArrives::RunTest(const FString& Parameters)
{
    using namespace ck_voice_proximity_late_transform_spec;

    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};
    const auto State = MakeShared<FLateTransformSpecState, ESPMode::ThreadSafe>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            auto TransientEntity = UCk_Utils_EcsWorld_Subsystem_UE::Get_TransientEntity(InServer);

            State->Host = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
            State->Channel = UCk_Utils_VoiceChannel_UE::Add(State->Host,
                FCk_VoiceChannel_Spec{TAG_VoiceChat_Channel_AutoTest_LateTransform}
                    .Set_SpatializationPolicy(ECk_VoiceChat_SpatializationPolicy::Positional3D));

            // No Transform on the talker's entity yet.
            State->TalkerEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(TransientEntity);
            State->Talker = UCk_Utils_VoiceTalker_UE::Add(State->TalkerEntity, FCk_VoiceTalker_Spec{});

            UCk_Utils_VoiceChannel_UE::Request_Join(State->Channel, FCk_Request_VoiceChannel_Join{State->Talker}, {});
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Channel) && UCk_Utils_VoiceChannel_UE::Get_IsMember(State->Channel, State->Talker) &&
                State->Talker.Has<ck::FTag_VoiceTalker_ProbesAwaitTransform>();
        }), WaitSeconds, TEXT("the transform-less talker joined and the reconcile saw that it awaits a Transform")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(10));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestFalse(TEXT("no probes without a Transform (proximity routing fails closed)"), Get_HasProbes(State->Talker));
            TestTrue(TEXT("the reconcile kept its dirty tag - it is still waiting, not finished"),
                State->Talker.Has<ck::FTag_VoiceTalker_ProximityDirty>());
            return true;
        }), TEXT("waiting for a Transform")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld* InServer) -> void
        {
            UCk_Utils_Transform_UE::Add(State->TalkerEntity, FTransform::Identity, ECk_Replication::DoesNotReplicate);
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]() -> bool
        {
            return ck::IsValid(State->Talker) && Get_HasProbes(State->Talker);
        }), WaitSeconds, TEXT("the talker's probes composed once its Transform arrived")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]() -> bool
        {
            TestFalse(TEXT("the reconcile is done: dirty tag cleared"), State->Talker.Has<ck::FTag_VoiceTalker_ProximityDirty>());
            TestFalse(TEXT("and no longer awaiting a Transform"), State->Talker.Has<ck::FTag_VoiceTalker_ProbesAwaitTransform>());
            return true;
        }), TEXT("probes composed")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
        {
            FAutomationTestBase::bSuppressLogErrors = false;
            FAutomationTestBase::bSuppressLogWarnings = false;
            return true;
        }),
        TEXT("restore log suppression statics")));

    return true;
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

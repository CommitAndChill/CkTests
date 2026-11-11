#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && UE_WITH_IRIS

#include "CkCore/Macros/CkMacros.h"
#include "CkAutoTest_Utils.h"
#include "CkTests/Net/CkAutoTest_VoiceSpike.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/Channel.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Templates/Atomic.h"
#include "UObject/UnrealType.h"

namespace ck_iris_backpressure_spec
{
    constexpr auto ControlCount = 21;
    constexpr auto PressureBytes = 60000;

    struct FState final : FOutputDevice
    {
        TWeakObjectPtr<APlayerController> _Controller;
        TWeakObjectPtr<UNetConnection> _Connection;
        TWeakObjectPtr<ACk_AutoTest_VoiceSpikeChannel_UE> _Channel;
        TWeakObjectPtr<ACk_AutoTest_VoiceSpikePressure_UE> _Pressure;
        TArray<uint8> _ExpectedPayload;
        TAtomic<bool> _TickObservedNotReady{false};
        bool _PublicObservedNotReady = false;
        bool _Observing = false;
        bool _RestoreSpeed = false;
        int32 _PreviousSpeed = 0;
        bool _ThrottleOverridden = false;
        FString _PreviousThrottleValue;
        EConsoleVariableFlags _PreviousThrottlePriority = ECVF_SetByConstructor;

        auto
            RestoreThrottle(
                FAutomationTestBase* InTest = nullptr)
            -> bool
        {
            if (NOT _ThrottleOverridden)
            { return true; }
            UCk_Utils_AutoTest_UE::Request_PopCVarOverride(TEXT("net.DisableBandwithThrottling"));
            _ThrottleOverridden = false;
            if (InTest == nullptr)
            { return true; }
            const auto* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("net.DisableBandwithThrottling"));
            if (NOT InTest->TestNotNull(TEXT("Bandwidth override CVar remains registered after fixture teardown"), Variable))
            { return false; }
            const auto RestoredValue = InTest->TestEqual(TEXT("Fixture restores the exact prior bandwidth override value"),
                Variable->GetString(), _PreviousThrottleValue);
            const auto RestoredPriority = InTest->TestEqual(TEXT("Fixture restores the exact prior bandwidth override priority"),
                static_cast<EConsoleVariableFlags>(Variable->GetFlags() & ECVF_SetByMask), _PreviousThrottlePriority);
            return RestoredValue && RestoredPriority;
        }

        auto
            Serialize(
                const TCHAR* InText,
                ELogVerbosity::Type,
                const FName& InCategory)
            -> void override
        {
            if (InCategory == FName{TEXT("LogIris")}
                && FString{InText}.Contains(TEXT("Disallowed to write first packet in batch, with Iris this is not good!")))
            {
                constexpr auto TickEnteredBackpressure = true;
                _TickObservedNotReady.Store(TickEnteredBackpressure);
            }
        }

        auto
            Retire()
            -> void
        {
            if (_Observing)
            {
                GLog->RemoveOutputDevice(this);
                _Observing = false;
            }
            if (_RestoreSpeed && _Controller.IsValid())
            {
                _Controller->SetNetSpeed(_PreviousSpeed);
                _RestoreSpeed = false;
            }
            if (_Channel.IsValid())
            { _Channel->Destroy(); }
            if (_Pressure.IsValid())
            { _Pressure->Destroy(); }
        }

        ~FState()
        {
            Retire();
            RestoreThrottle();
        }
    };

    auto
        FindDataStream(
            const UNetConnection* InConnection)
        -> UChannel*
    {
        if (InConnection == nullptr)
        { return nullptr; }
        if (InConnection->GetConnectionState() != USOCK_Open)
        { return nullptr; }
        for (const auto& Channel : InConnection->OpenChannels)
        {
            if (Channel != nullptr && Channel->Connection == InConnection
                && Channel->ChName == FName{TEXT("DataStream")})
            { return Channel.Get(); }
        }
        return nullptr;
    }

    auto
        ReadPayload(
            const ACk_AutoTest_VoiceSpikePressure_UE* InActor,
            TArray<uint8>& OutBytes)
        -> bool
    {
        OutBytes.Reset();
        if (InActor == nullptr)
        { return false; }
        const auto* Property = FindFProperty<FArrayProperty>(InActor->GetClass(), TEXT("_Payload"));
        if (Property == nullptr)
        { return false; }
        if (CastField<FByteProperty>(Property->Inner) == nullptr)
        { return false; }
        auto Array = FScriptArrayHelper{Property, Property->ContainerPtrToValuePtr<void>(InActor)};
        if (Array.Num() > 0)
        { OutBytes.Append(Array.GetRawPtr(), Array.Num()); }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkIrisBackpressureRecovery,
    "Ck.Iris.Net.Backpressure.ConnectedStreamRecoversAndDrains",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

auto
    FCkIrisBackpressureRecovery::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_iris_backpressure_spec;
    const auto State = MakeShared<FState>();
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
    auto* Throttle = IConsoleManager::Get().FindConsoleVariable(TEXT("net.DisableBandwithThrottling"));
    if (NOT TestNotNull(TEXT("Public engine bandwidth bypass CVar exists"), Throttle))
    { return false; }
    State->_PreviousThrottleValue = Throttle->GetString();
    State->_PreviousThrottlePriority = static_cast<EConsoleVariableFlags>(Throttle->GetFlags() & ECVF_SetByMask);
    UCk_Utils_AutoTest_UE::Request_PushCVarOverride(TEXT("net.DisableBandwithThrottling"), TEXT("0"));
    State->_ThrottleOverridden = true;
    if (NOT TestEqual(TEXT("Real bandwidth readiness remains enabled before PIE starts"), Throttle->GetInt(), 0))
    { return false; }
#endif
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(2, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(2, 30.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld* InWorld)
        {
            auto* Driver = InWorld->GetNetDriver();
            if (NOT TestNotNull(TEXT("Real PIE net driver exists"), Driver))
            { return; }
            if (NOT TestTrue(TEXT("Real connection uses Iris replication"), Driver->IsUsingIrisReplication()))
            { return; }
            auto* Controller = ck::auto_test::net::Get_RemoteClientPlayerController(InWorld, 0);
            if (NOT TestNotNull(TEXT("Remote client controller exists"), Controller))
            { return; }
            if (NOT TestNotNull(TEXT("Remote client PlayerState exists"), Controller->PlayerState.Get()))
            { return; }
            State->_Controller = Controller;
            State->_Connection = Controller->GetNetConnection();
            if (NOT TestTrue(TEXT("Remote client owns an open production connection"),
                State->_Connection.IsValid() && State->_Connection->GetConnectionState() == USOCK_Open))
            { return; }
            auto Spawn = FActorSpawnParameters{};
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            State->_Channel = InWorld->SpawnActor<ACk_AutoTest_VoiceSpikeChannel_UE>(
                ACk_AutoTest_VoiceSpikeChannel_UE::StaticClass(), FTransform::Identity, Spawn);
            State->_Pressure = InWorld->SpawnActor<ACk_AutoTest_VoiceSpikePressure_UE>(
                ACk_AutoTest_VoiceSpikePressure_UE::StaticClass(), FTransform::Identity, Spawn);
            if (NOT TestTrue(TEXT("Complete production transport vehicles spawn"),
                State->_Channel.IsValid() && State->_Pressure.IsValid()))
            { return; }
            State->_Channel->SetOwner(Controller->PlayerState);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            return ACk_AutoTest_VoiceSpikeChannel_UE::Find(Client) != nullptr
                && ACk_AutoTest_VoiceSpikePressure_UE::Find(Client) != nullptr;
        }), 15.0, TEXT("Remote client receives both real replicated actors")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld*)
        {
            if (State->_Channel.IsValid())
            { State->_Channel->Client_ReceiveControl(0); }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]
        {
            const auto* Channel = ACk_AutoTest_VoiceSpikeChannel_UE::Find(ck::auto_test::net::Get_ClientWorld(0));
            return Channel != nullptr && Channel->_ReceivedControls == 1;
        }), 15.0, TEXT("Reliable RPC receipt proves the stream handshake and routing work before pressure")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
        {
            if (NOT TestTrue(TEXT("Connected vehicles remain live before pressure"),
                State->_Controller.IsValid() && State->_Connection.IsValid()
                    && State->_Channel.IsValid() && State->_Pressure.IsValid()))
            { return; }
            State->_PreviousSpeed = State->_Connection->CurrentNetSpeed;
            State->_RestoreSpeed = true;
            constexpr auto ConstrainedBytesPerSecond = 1800;
            State->_Controller->SetNetSpeed(ConstrainedBytesPerSecond);
            TestEqual(TEXT("Public controller API constrains the real connection bandwidth"),
                State->_Connection->CurrentNetSpeed, ConstrainedBytesPerSecond);
            GLog->AddOutputDevice(&State.Get());
            State->_Observing = true;
            State->_Pressure->_ChurnBytesPerTick = PressureBytes;
            State->_Channel->_BundlesPerTick = 8;
            State->_Channel->_RemainingBundleBudget = 240;
            for (auto Index = 1; Index < ControlCount; ++Index)
            {
                State->_Channel->Client_ReceiveControl(Index);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            auto* Stream = FindDataStream(State->_Connection.Get());
            if (Stream != nullptr && NOT Stream->IsNetReady())
            { State->_PublicObservedNotReady = true; }
            return State->_PublicObservedNotReady && State->_TickObservedNotReady.Load()
                && State->_Pressure.IsValid() && State->_Pressure->_ChurnCounter > 0;
        }), 10.0, TEXT("Public DataStream readiness and the real handshaken Tick branch both observe backpressure")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, State](UWorld*)
        {
            TestTrue(TEXT("Production Tick encountered ordinary connected-stream backpressure"), State->_TickObservedNotReady.Load());
            TestTrue(TEXT("Public DataStream channel readiness actually became false"), State->_PublicObservedNotReady);
            if (State->_Pressure.IsValid())
            {
                State->_Pressure->_ChurnBytesPerTick = 0;
                TestTrue(TEXT("Final replicated pressure state is observable without mutating it"),
                    ReadPayload(State->_Pressure.Get(), State->_ExpectedPayload));
                TestEqual(TEXT("Pressure really authored a full state payload"), State->_ExpectedPayload.Num(), PressureBytes);
            }
            if (State->_Channel.IsValid())
            { State->_Channel->_RemainingBundleBudget = 0; }
            if (State->_Controller.IsValid())
            {
                State->_Controller->SetNetSpeed(State->_PreviousSpeed);
                if (State->_Connection.IsValid())
                {
                    TestEqual(TEXT("Public controller API restores the original connection bandwidth"),
                        State->_Connection->CurrentNetSpeed, State->_PreviousSpeed);
                }
                State->_RestoreSpeed = false;
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            const auto* Channel = ACk_AutoTest_VoiceSpikeChannel_UE::Find(Client);
            auto Payload = TArray<uint8>{};
            auto* Stream = FindDataStream(State->_Connection.Get());
            return State->_Connection.IsValid() && State->_Connection->GetConnectionState() == USOCK_Open
                && Stream != nullptr && Stream->IsNetReady() && Channel != nullptr
                && Channel->_ReceivedControls == ControlCount && NOT State->_ExpectedPayload.IsEmpty()
                && ReadPayload(ACk_AutoTest_VoiceSpikePressure_UE::Find(Client), Payload)
                && Payload == State->_ExpectedPayload;
        }), 30.0, TEXT("Same connection recovers readiness and delivers every reliable control and exact final state")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([State](UWorld*)
        {
            State->Retire();
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([State]
        {
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);
            return NOT State->_Channel.IsValid() && NOT State->_Pressure.IsValid()
                && ACk_AutoTest_VoiceSpikeChannel_UE::Find(Client) == nullptr
                && ACk_AutoTest_VoiceSpikePressure_UE::Find(Client) == nullptr;
        }), 15.0, TEXT("Owned transport actors retire on both peers before PIE ends")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, State]
        {
            return State->RestoreThrottle(this);
        }), TEXT("Restore the prior public bandwidth override value and priority after PIE")));
    return true;
}

#endif

#include "Test_CrowdRemoteDebug_ProviderProbe.h"
#include "CkCrowd/Debug/CkCrowd_RemoteDebug_Utils.h"

#include <Engine/LocalPlayer.h>
#include <Engine/World.h>
#include <GameFramework/PlayerController.h>
#include <UObject/StructOnScope.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/UnrealType.h>
#include <UObject/UObjectGlobals.h>
#include <limits>

namespace ck_crowd_remote_debug_provider_probe
{
    constexpr int32 ProbeRows = 80; // exercises full arrays beyond obsolete 32/64 proposals

    auto
        MakeRow(
            int64 WorldGeneration, int64 Serial
        )
        -> FCkCrowdRemoteAgentRow
    {
        FCkCrowdRemoteAgentRow Row;
        Row.Id.WorldGeneration = WorldGeneration;
        Row.Id.AgentSerial = Serial;
        Row.SampleSequence = 7;
        Row.ConfigurationRevision = 2;
        Row.bHasDebugOverride = Serial % 2 == 1;
        Row.DisplayId = FString::Printf(TEXT("%lld:%lld"), WorldGeneration, Serial);
        Row.DebugName = FString::Printf(TEXT("Probe%lld"), Serial);
        Row.OwnerName = TEXT("PIE owner");
        Row.PrimaryTag = TEXT("Crowd.Test");
        Row.Position = FVector{static_cast<double>(Serial), 0.0, 0.0};
        Row.WorldTransform = FTransform{Row.Position};
        Row.NeighborCount = ProbeRows;
        Row.PlannedPathPointCount = ProbeRows;
        Row.MaxSpeedCmPerSec = 100.0f;
        Row.MaxTurnRateRadPerSec = 1.0f;
        Row.MaxAccelerationCmPerSecSq = 200.0f;
        Row.ArrivalRadiusCm = 20.0f;
        Row.SeparationRadiusCm = 50.0f;
        return Row;
    }

    auto
        MakeValidCommand(
            const FCkCrowdRemoteAgentId& Id, int64 Serial
        )
        -> FCkCrowdRemoteCommandRequest
    {
        FCkCrowdRemoteCommandRequest Request;
        Request.Id = Id;
        Request.ClientSerial = Serial;
        Request.ObservedConfigurationRevision = 2;
        Request.Command = ECkCrowdRemoteCommand::SetTunables;
        Request.MaxSpeedCmPerSec = 100.0f;
        Request.MaxTurnRateRadPerSec = 1.0f;
        Request.MaxAccelerationCmPerSecSq = 200.0f;
        Request.ArrivalRadiusCm = 20.0f;
        Request.SeparationRadiusCm = 50.0f;
        return Request;
    }
}

using namespace ck_crowd_remote_debug_provider_probe;

auto
    UCkTest_CrowdRemoteDebug_ProviderProbe::
    GetWorld()
    const -> UWorld*
{
    return BoundWorld.Get();
}

auto
    UCkTest_CrowdRemoteDebug_ProviderProbe::
    IsForWorld(
        const UWorld& World
    )
    const -> bool
{
    return BoundWorld.Get() == &World;
}

auto
    UCkTest_CrowdRemoteDebug_ProviderProbe::
    CopyRows(
        const UWorld& World, const APlayerController&, const ULocalPlayer&,
        TArray<FCkCrowdRemoteAgentRow>& Out
    )
    const -> bool
{
    ++RowCalls;
    if (!IsForWorld(World))
    { return false; }
    Out.Reset();
    for (int32 Index = 0; Index < ProbeRows; ++Index)
    {
        Out.Add(MakeRow(WorldGeneration, Index + 1));
    }
    switch (Mode)
    {
    case 1: Out[0].Id = {}; break;
    case 2: Out[0].Position.X = std::numeric_limits<double>::quiet_NaN(); break;
    case 3: Out[0].Status = static_cast<ECkCrowdRemoteAgentStatus>(255); break;
    case 4: ++Out[1].Id.WorldGeneration; break;
    case 5: Out[1].Id = Out[0].Id; break;
    case 6: Out[0].WorldTransform.SetRotation(FQuat{0.0, 0.0, 0.0, 0.0}); break;
    case 7: Out[0].WorldTransform.SetRotation(FQuat{2.0, 0.0, 0.0, 0.0}); break;
    case 8: Out[0].WorldTransform.SetRotation(FQuat{FVector::UpVector, 0.5}); break;
    case 9: ++Out[1].SampleSequence; break;
    default: break;
    }
    return true;
}

auto
    UCkTest_CrowdRemoteDebug_ProviderProbe::
    RequestSelectionFromOwningLocalController(
        const UWorld& World, APlayerController& RequestingPC,
        const ULocalPlayer& RequestingLocalPlayer, const FCkCrowdRemoteAgentId& Id,
        int64 SelectionRequestSerial, bool bClear)
    -> bool
{
    ++SelectionCalls;
    const TWeakObjectPtr<const APlayerController> Key{&RequestingPC};
    if (!IsForWorld(World) || RequestingPC.GetWorld() != &World ||
        RequestingPC.GetLocalPlayer() != &RequestingLocalPlayer ||
        SelectionRequestSerial <= 0 ||
        SelectionRequestSerial <= SelectionSerialByPC.FindRef(Key) ||
        (bClear ? (Id.WorldGeneration != 0 || Id.AgentSerial != 0) :
            (Id.WorldGeneration != WorldGeneration || Id.AgentSerial != 1)))
    { return false; }
    SelectionSerialByPC.Add(Key, SelectionRequestSerial);
    SelectedIdByPC.Add(Key, Id);
    LastSelectionPC = &RequestingPC;
    LastSelectionLocalPlayer = const_cast<ULocalPlayer*>(&RequestingLocalPlayer);
    ++AcceptedSelections;
    return true;
}

auto
    UCkTest_CrowdRemoteDebug_ProviderProbe::
    CopySelectedDetail(
        const UWorld& World, const APlayerController& RequestingPC, const ULocalPlayer&,
        const FCkCrowdRemoteAgentId& Id, int64 SelectionRequestSerial,
        FCkCrowdRemoteAgentDetail& Out
    )
    const -> bool
{
    ++DetailCalls;
    const TWeakObjectPtr<const APlayerController> Key{&RequestingPC};
    if (!IsForWorld(World) || Id.WorldGeneration != WorldGeneration || Id.AgentSerial != 1 ||
        SelectionSerialByPC.FindRef(Key) != SelectionRequestSerial ||
        !(SelectedIdByPC.FindRef(Key) == Id))
    {
        return false;
    }
    Out = {};
    Out.Id = Id;
    Out.SampleSequence = 7;
    Out.ConfigurationRevision = 2;
    Out.SelectionRequestSerial = SelectionRequestSerial;
    Out.Sample = MakeRow(WorldGeneration, 1);
    for (int32 Index = 0; Index < ProbeRows; ++Index)
    {
        FCkCrowdRemoteNeighbor Neighbor;
        Neighbor.bIdResolved = true;
        Neighbor.Id.WorldGeneration = WorldGeneration;
        Neighbor.Id.AgentSerial = Index + 1;
        Neighbor.DistanceCm = static_cast<float>(Index);
        Neighbor.RelativeOffset = FVector{static_cast<double>(Index), 0.0, 0.0};
        Out.Neighbors.Add(Neighbor);
        Out.PlannedPath.Add(FVector{static_cast<double>(Index), 100.0, 0.0});
    }
    if (DetailMode == 1)
    { ++Out.SelectionRequestSerial; }
    if (DetailMode == 2)
    { ++Out.Id.AgentSerial; }
    if (DetailMode == 3)
    { ++Out.Sample.Id.AgentSerial; }
    return true;
}

auto
    UCkTest_CrowdRemoteDebug_ProviderProbe::
    SubmitFromOwningLocalController(
        const UWorld& World, APlayerController& RequestingPC,
        const ULocalPlayer& RequestingLocalPlayer,
        const FCkCrowdRemoteCommandRequest& Request
    )
    -> bool
{
    ++SubmitCalls;
    if (!IsForWorld(World))
    { return false; }
    LastSubmitPC = &RequestingPC;
    LastSubmitLocalPlayer = const_cast<ULocalPlayer*>(&RequestingLocalPlayer);
    LastResult = {};
    LastResult.Id = Request.Id;
    LastResult.ClientSerial = Request.ClientSerial;
    LastResult.AppliedConfigurationRevision = 3;
    LastResult.ResultSampleSequence = 8;
    LastResult.Outcome = ECkCrowdRemoteCommandOutcome::Accepted;
    return true;
}

auto
    UCkTest_CrowdRemoteDebug_ProviderProbe::
    CopyCommandResult(
        const UWorld& World, const APlayerController&, const ULocalPlayer&,
        int64 ClientSerial, FCkCrowdRemoteCommandResult& Out
    )
    const -> bool
{
    ++ResultCalls;
    if (!IsForWorld(World) || LastResult.ClientSerial != ClientSerial)
    { return false; }
    Out = LastResult;
    return true;
}

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkTests/Net/CkNetAutomation_Common.h"
#include <Misc/AutomationTest.h>
#include <UObject/StrongObjectPtr.h>

namespace ck_crowd_remote_debug_provider_test
{
    struct FClient
    {
        TStrongObjectPtr<UCkTest_CrowdRemoteDebug_ProviderProbe> Provider;
        TStrongObjectPtr<UCkTest_CrowdRemoteDebug_ProviderProbe> Other;
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<APlayerController> PC;
        TWeakObjectPtr<ULocalPlayer> LocalPlayer;
        FCkCrowdRemoteProviderToken Token;
        bool bExercised = false;
    };

    struct FRun
    {
        FClient Clients[2];
    };

    auto
        TryLocalContext(
            UWorld* World, APlayerController*& OutPC, ULocalPlayer*& OutLocalPlayer
        )
        -> bool
    {
        OutPC = nullptr;
        OutLocalPlayer = nullptr;
        if (!IsValid(World))
        { return false; }
        for (auto It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            ULocalPlayer* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
            if (IsValid(LP) && PC->IsLocalController() && PC->GetWorld() == World &&
                LP->GetWorld() == World && LP->GetPlayerController(World) == PC)
            {
                OutPC = PC;
                OutLocalPlayer = LP;
                return true;
            }
        }
        return false;
    }

    auto
        ExerciseReflectedRows(
            FAutomationTestBase& Test, APlayerController* InRequestingPC,
            bool ExpectedSuccess, int32 ExpectedRows)
        -> void
    {
        auto* Function = UCk_Utils_CrowdRemoteDebug_UE::StaticClass()->FindFunctionByName(
            GET_FUNCTION_NAME_CHECKED(UCk_Utils_CrowdRemoteDebug_UE, TryCopyRows));
        if (!Test.TestNotNull(TEXT("reflected Crowd consumer rows function"), Function))
        { return; }
        auto* PCProperty = FindFProperty<FObjectProperty>(Function, TEXT("InRequestingPC"));
        auto* RowsProperty = FindFProperty<FArrayProperty>(Function, TEXT("OutRows"));
        auto* ReturnProperty = FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue"));
        const bool Schema = PCProperty && RowsProperty && ReturnProperty &&
            Function->HasAllFunctionFlags(FUNC_Static | FUNC_Native) &&
            Function->NumParms == 3 &&
            PCProperty->HasAnyPropertyFlags(CPF_Parm) &&
            PCProperty->PropertyClass == APlayerController::StaticClass() &&
            RowsProperty->HasAllPropertyFlags(CPF_Parm | CPF_OutParm) &&
            ReturnProperty->HasAnyPropertyFlags(CPF_ReturnParm) &&
            CastField<FStructProperty>(RowsProperty->Inner) &&
            CastField<FStructProperty>(RowsProperty->Inner)->Struct ==
                FCkCrowdRemoteAgentRow::StaticStruct();
        if (!Test.TestTrue(TEXT("reflected Crowd rows parameter types and flags"), Schema))
        { return; }
        FStructOnScope Parameters(Function);
        auto* Memory = Parameters.GetStructMemory();
        PCProperty->SetObjectPropertyValue_InContainer(Memory, InRequestingPC);
        FScriptArrayHelper Rows(RowsProperty, RowsProperty->ContainerPtrToValuePtr<void>(Memory));
        Rows.AddValue(); // rejected calls must clear a pre-existing output row
        GetMutableDefault<UCk_Utils_CrowdRemoteDebug_UE>()->ProcessEvent(Function, Memory);
        Test.TestEqual(TEXT("reflected Crowd rows return"),
            ReturnProperty->GetPropertyValue_InContainer(Memory), ExpectedSuccess);
        Test.TestEqual(TEXT("reflected Crowd rows atomic count"), Rows.Num(), ExpectedRows);
    }

    auto
        ExerciseReflectedSelection(
            FAutomationTestBase& Test, APlayerController* PC,
            const FCkCrowdRemoteAgentId& Id, int64 Serial, bool bClear,
            bool Expected)
        -> bool
    {
        auto* Function = UCk_Utils_CrowdRemoteDebug_UE::StaticClass()->FindFunctionByName(
            GET_FUNCTION_NAME_CHECKED(UCk_Utils_CrowdRemoteDebug_UE, TryRequestSelection));
        if (!Test.TestNotNull(TEXT("reflected Crowd selection request function"), Function))
        { return false; }
        auto* PCProperty = FindFProperty<FObjectProperty>(Function, TEXT("InRequestingPC"));
        auto* IdProperty = FindFProperty<FStructProperty>(Function, TEXT("InId"));
        auto* SerialProperty = FindFProperty<FInt64Property>(Function,
            TEXT("InSelectionRequestSerial"));
        auto* ClearProperty = FindFProperty<FBoolProperty>(Function, TEXT("bClear"));
        auto* ReturnProperty = FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue"));
        const bool Schema = PCProperty && IdProperty && SerialProperty && ClearProperty &&
            ReturnProperty && Function->NumParms == 5 &&
            Function->HasAllFunctionFlags(FUNC_Static | FUNC_Native) &&
            PCProperty->PropertyClass == APlayerController::StaticClass() &&
            IdProperty->Struct == FCkCrowdRemoteAgentId::StaticStruct() &&
            ReturnProperty->HasAnyPropertyFlags(CPF_ReturnParm);
        if (!Test.TestTrue(TEXT("reflected selection exact schema"), Schema))
        { return false; }
        FStructOnScope Params(Function);
        auto* Memory = Params.GetStructMemory();
        PCProperty->SetObjectPropertyValue_InContainer(Memory, PC);
        IdProperty->Struct->CopyScriptStruct(
            IdProperty->ContainerPtrToValuePtr<void>(Memory), &Id);
        SerialProperty->SetPropertyValue_InContainer(Memory, Serial);
        ClearProperty->SetPropertyValue_InContainer(Memory, bClear);
        GetMutableDefault<UCk_Utils_CrowdRemoteDebug_UE>()->ProcessEvent(Function, Memory);
        return Test.TestEqual(TEXT("reflected selection returns expected admission"),
            ReturnProperty->GetPropertyValue_InContainer(Memory), Expected);
    }

    auto
        InvokeASProbe(
            FAutomationTestBase& Test, UWorld* World, UClass* ProbeClass,
            APlayerController* PC, const FCkCrowdRemoteCommandRequest& Request,
            const FName Method)
        -> bool
    {
        if (!Test.TestTrue(TEXT("AS Crowd probe class and PIE world remain live"),
            IsValid(World) && IsValid(ProbeClass)))
        { return false; }
        auto* Function = ProbeClass->FindFunctionByName(Method);
        if (!Test.TestNotNull(TEXT("AS Crowd probe method reflects"), Function))
        { return false; }
        auto* PCProperty = FindFProperty<FObjectProperty>(Function, TEXT("InPC"));
        auto* RequestProperty = FindFProperty<FStructProperty>(Function, TEXT("InRequest"));
        auto* ReturnProperty = FindFProperty<FBoolProperty>(Function, TEXT("ReturnValue"));
        const bool Schema = PCProperty && RequestProperty && ReturnProperty &&
            Function->NumParms == 3 &&
            PCProperty->PropertyClass == APlayerController::StaticClass() &&
            PCProperty->HasAnyPropertyFlags(CPF_Parm) &&
            RequestProperty->Struct == FCkCrowdRemoteCommandRequest::StaticStruct() &&
            RequestProperty->HasAnyPropertyFlags(CPF_Parm) &&
            ReturnProperty->HasAnyPropertyFlags(CPF_ReturnParm);
        if (!Test.TestTrue(TEXT("AS Crowd method has exact reflected PC/request/return schema"), Schema))
        { return false; }
        auto* Object = NewObject<UObject>(World, ProbeClass);
        if (!Test.TestNotNull(TEXT("world-owned AS Crowd probe instance"), Object))
        { return false; }
        FStructOnScope Parameters(Function);
        auto* Memory = Parameters.GetStructMemory();
        PCProperty->SetObjectPropertyValue_InContainer(Memory, PC);
        RequestProperty->Struct->CopyScriptStruct(
            RequestProperty->ContainerPtrToValuePtr<void>(Memory), &Request);
        Object->ProcessEvent(Function, Memory);
        return ReturnProperty->GetPropertyValue_InContainer(Memory);
    }

    auto
        ExerciseClient(
            FAutomationTestBase& Test, UWorld* World, int32 Index, FClient& Client,
            UClass* ASProbeClass
        )
        -> void
    {
        APlayerController* PC = nullptr;
        ULocalPlayer* LP = nullptr;
        if (!Test.TestTrue(TEXT("real client PC and LocalPlayer are correlated"),
                TryLocalContext(World, PC, LP)))
        { return; }
        Client.World = World;
        Client.PC = PC;
        Client.LocalPlayer = LP;
        Client.Provider = TStrongObjectPtr<UCkTest_CrowdRemoteDebug_ProviderProbe>{
            NewObject<UCkTest_CrowdRemoteDebug_ProviderProbe>(World)};
        Client.Other = TStrongObjectPtr<UCkTest_CrowdRemoteDebug_ProviderProbe>{
            NewObject<UCkTest_CrowdRemoteDebug_ProviderProbe>(World)};
        if (!Test.TestNotNull(TEXT("world-owned probe provider"), Client.Provider.Get()) ||
            !Test.TestNotNull(TEXT("independent duplicate owner"), Client.Other.Get()))
        { return; }
        Client.Provider->BoundWorld = World;
        Client.Other->BoundWorld = World;
        Client.Provider->WorldGeneration = 101 + Index;
        Client.Other->WorldGeneration = 101 + Index;
        if (!Test.TestTrue(TEXT("first world provider registers"),
                FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Client.Provider, Client.Token)) ||
            !Test.TestTrue(TEXT("registration returns exact token"), Client.Token.IsValid()))
        { return; }

        FCkCrowdRemoteProviderToken DuplicateToken = Client.Token;
        Test.TestFalse(TEXT("duplicate owner rejected"),
            FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Client.Other, DuplicateToken));
        Test.TestTrue(TEXT("duplicate rejection preserves caller's original token"),
            DuplicateToken.Value == Client.Token.Value);
        Test.TestFalse(TEXT("same provider cannot register twice"),
            FCkCrowdRemoteDebugRegistry::TryRegister(*World, *Client.Provider, DuplicateToken));
        Test.TestTrue(TEXT("same-owner rejection preserves exact token"),
            DuplicateToken.Value == Client.Token.Value);
        FCkCrowdRemoteProviderToken WrongToken{Client.Token.Value + 1};
        Test.TestFalse(TEXT("wrong token cannot evict"),
            FCkCrowdRemoteDebugRegistry::TryUnregister(*World, *Client.Provider, WrongToken));
        Test.TestFalse(TEXT("wrong owner cannot evict"),
            FCkCrowdRemoteDebugRegistry::TryUnregister(*World, *Client.Other, Client.Token));

        TArray<FCkCrowdRemoteAgentRow> Rows;
        Test.TestTrue(TEXT("valid complete roster accepted"),
            FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
        Test.TestEqual(TEXT("authored 80 rows are untruncated"), Rows.Num(), ProbeRows);
        Test.TestTrue(TEXT("first remote override state copied"), Rows.Num() == ProbeRows && Rows[0].bHasDebugOverride);
        Test.TestTrue(TEXT("second remote override state copied"), Rows.Num() == ProbeRows && !Rows[1].bHasDebugOverride);
        Test.TestTrue(TEXT("native consumer facade returns complete roster"),
            UCk_Utils_CrowdRemoteDebug_UE::TryCopyRows(PC, Rows));
        Test.TestEqual(TEXT("native consumer roster has no cap"), Rows.Num(), ProbeRows);
        Rows.Add(MakeRow(Client.Provider->WorldGeneration, 999));
        Test.TestFalse(TEXT("native facade rejects null PC"),
            UCk_Utils_CrowdRemoteDebug_UE::TryCopyRows(nullptr, Rows));
        Test.TestEqual(TEXT("null PC clears prior roster"), Rows.Num(), 0);
        auto* AuthorityPC = ck::auto_test::net::TryGet_RemoteClientPlayerController(
            ck::auto_test::net::Get_ServerWorld(), Index);
        if (Test.TestNotNull(TEXT("real non-local authority PC for consumer rejection"), AuthorityPC))
        {
            Rows.Add(MakeRow(Client.Provider->WorldGeneration, 999));
            Test.TestFalse(TEXT("native facade rejects non-local authority PC"),
                UCk_Utils_CrowdRemoteDebug_UE::TryCopyRows(AuthorityPC, Rows));
            Test.TestEqual(TEXT("wrong PC context clears prior roster"), Rows.Num(), 0);
        }
        ExerciseReflectedRows(Test, PC, true, ProbeRows);
        const int32 CallsAfterValidReflectedRows = Client.Provider->RowCalls;
        ExerciseReflectedRows(Test, nullptr, false, 0);
        if (AuthorityPC)
        { ExerciseReflectedRows(Test, AuthorityPC, false, 0); }
        Test.TestEqual(TEXT("invalid reflected contexts never call provider"),
            Client.Provider->RowCalls, CallsAfterValidReflectedRows);
        const FCkCrowdRemoteAgentId SelectedId = MakeRow(Client.Provider->WorldGeneration, 1).Id;
        for (int32 Mode = 1; Mode <= 7; ++Mode)
        {
            Client.Provider->Mode = Mode;
            Rows.Add(MakeRow(Client.Provider->WorldGeneration, 999));
            Test.TestFalse(*FString::Printf(TEXT("invalid roster mode %d rejects"), Mode),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
            Test.TestEqual(*FString::Printf(TEXT("invalid roster mode %d publishes no subset"), Mode),
                Rows.Num(), 0);
        }
        auto FinalStatus = MakeRow(Client.Provider->WorldGeneration, 1);
        FinalStatus.Status = ECkCrowdRemoteAgentStatus::PlayerProxy;
        Test.TestTrue(TEXT("authored final row status accepted"),
            FCkCrowdRemoteDebugValidation::IsValid(FinalStatus));
        const auto* StatusEnum = StaticEnum<ECkCrowdRemoteAgentStatus>();
        const auto StatusMax = StatusEnum->GetMaxEnumValue();
        Test.TestTrue(TEXT("row status autogenerated MAX is reflected"),
            StatusEnum->IsValidEnumValue(StatusMax) &&
            StatusEnum->GetNameByValue(StatusMax).ToString().EndsWith(TEXT("_MAX")));
        FinalStatus.Status = static_cast<ECkCrowdRemoteAgentStatus>(StatusMax);
        Test.TestFalse(TEXT("autogenerated row status MAX rejected"),
            FCkCrowdRemoteDebugValidation::IsValid(FinalStatus));
        Client.Provider->Mode = 8;
        Test.TestTrue(TEXT("finite normalized nonidentity rotation is accepted"),
            FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
        Test.TestTrue(TEXT("accepted rotation is normalized and nonidentity"),
            Rows.Num() == ProbeRows && Rows[0].WorldTransform.GetRotation().IsNormalized() &&
            Rows[0].WorldTransform.GetRotation().Z > 0.0);
        Client.Provider->Mode = 9;
        Rows.Add(MakeRow(Client.Provider->WorldGeneration, 999));
        Test.TestFalse(TEXT("mixed sample sequence rejects whole roster"),
            FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *PC, *LP, Rows));
        Test.TestEqual(TEXT("mixed sequence clears prior candidate rows"), Rows.Num(), 0);
        Client.Provider->Mode = 0;

        FCkCrowdRemoteAgentDetail Detail;
        const int32 SelectionBefore = Client.Provider->SelectionCalls;
        Test.TestFalse(TEXT("zero selection serial rejects before provider"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 0));
        Test.TestFalse(TEXT("default selection ID rejects before provider"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, {}, 1));
        FCkCrowdRemoteAgentId PartialId;
        PartialId.WorldGeneration = Client.Provider->WorldGeneration;
        Test.TestFalse(TEXT("partial clear ID rejects before provider"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, PartialId, 1, true));
        Test.TestFalse(TEXT("nondefault clear ID rejects before provider"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 1, true));
        Test.TestFalse(TEXT("null PC facade selection rejects"),
            UCk_Utils_CrowdRemoteDebug_UE::TryRequestSelection(nullptr, SelectedId, 1));
        Test.TestEqual(TEXT("malformed selection never reaches provider"),
            Client.Provider->SelectionCalls, SelectionBefore);
        ExerciseReflectedSelection(Test, nullptr, SelectedId, 16, false, false);
        if (AuthorityPC)
        { ExerciseReflectedSelection(Test, AuthorityPC, SelectedId, 16, false, false); }
        Test.TestEqual(TEXT("invalid reflected selection never reaches provider"),
            Client.Provider->SelectionCalls, SelectionBefore);
        ExerciseReflectedSelection(Test, PC, SelectedId, 16, false, true);
        Test.TestEqual(TEXT("reflected selection reached exact provider"),
            Client.Provider->SelectionSerialByPC.FindRef(
                TWeakObjectPtr<const APlayerController>{PC}), int64{16});
        const int32 RequestsBeforePureCopy = Client.Provider->SelectionCalls;
        Test.TestFalse(TEXT("copy alone cannot issue an unrequested selection"),
            FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*World, *PC, *LP, SelectedId, 17, Detail));
        Test.TestEqual(TEXT("pure detail copy never issues selection"),
            Client.Provider->SelectionCalls, RequestsBeforePureCopy);
        Test.TestTrue(TEXT("selection request reaches exact local provider"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 17));
        Test.TestTrue(TEXT("selection provider receives exact PC and LocalPlayer"),
            Client.Provider->LastSelectionPC.Get() == PC &&
            Client.Provider->LastSelectionLocalPlayer.Get() == LP);
        Test.TestTrue(TEXT("selected detail exact request admitted"),
            FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*World, *PC, *LP, SelectedId, 17, Detail));
        Test.TestEqual(TEXT("selected neighbors untruncated"), Detail.Neighbors.Num(), ProbeRows);
        Test.TestEqual(TEXT("selected path untruncated"), Detail.PlannedPath.Num(), ProbeRows);
        Test.TestTrue(TEXT("selected frozen override state copied"), Detail.Sample.bHasDebugOverride);
        Test.TestEqual(TEXT("selected serial echoed"), Detail.SelectionRequestSerial, int64{17});
        Test.TestTrue(TEXT("native consumer facade copies selected frozen detail"),
            UCk_Utils_CrowdRemoteDebug_UE::TryCopySelectedDetail(PC, SelectedId, 17, Detail));
        Test.TestEqual(TEXT("native selected path remains complete"), Detail.PlannedPath.Num(), ProbeRows);
        Test.TestFalse(TEXT("duplicate selection serial rejected"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 17));
        Test.TestFalse(TEXT("stale selection serial rejected"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 16));
        Test.TestEqual(TEXT("replay leaves accepted selection watermark unchanged"),
            Client.Provider->SelectionSerialByPC.FindRef(
                TWeakObjectPtr<const APlayerController>{PC}), int64{17});
        Test.TestTrue(TEXT("next selection serial admitted after replay"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 18));
        Client.Provider->DetailMode = 1;
        Test.TestFalse(TEXT("mismatched selected serial rejected"),
            FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*World, *PC, *LP, SelectedId, 18, Detail));
        Test.TestFalse(TEXT("rejected detail cleared"), Detail.Id.IsValid());
        Test.TestTrue(TEXT("next detail request serial admitted"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 19));
        Client.Provider->DetailMode = 2;
        Test.TestFalse(TEXT("mismatched selected ID rejected"),
            FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*World, *PC, *LP, SelectedId, 19, Detail));
        Test.TestTrue(TEXT("third detail request serial admitted"),
            FCkCrowdRemoteDebugRegistry::TryRequestSelection(*World, *PC, *LP, SelectedId, 20));
        Client.Provider->DetailMode = 3;
        Test.TestFalse(TEXT("sample identity mismatch rejected"),
            FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*World, *PC, *LP, SelectedId, 20, Detail));
        Client.Provider->DetailMode = 0;
        Test.TestTrue(TEXT("explicit selection clear admitted"),
            UCk_Utils_CrowdRemoteDebug_UE::TryRequestSelection(PC, {}, 21, true));
        Test.TestTrue(TEXT("clear retains exact owner serial and default selection"),
            Client.Provider->SelectionSerialByPC.FindRef(
                TWeakObjectPtr<const APlayerController>{PC}) == 21 &&
            !Client.Provider->SelectedIdByPC.FindRef(
                TWeakObjectPtr<const APlayerController>{PC}).IsValid());
        Test.TestFalse(TEXT("cleared selection cannot copy stale detail"),
            FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*World, *PC, *LP, SelectedId, 20, Detail));
        Test.TestTrue(TEXT("selection can resume after clear"),
            UCk_Utils_CrowdRemoteDebug_UE::TryRequestSelection(PC, SelectedId, 22));
        Test.TestTrue(TEXT("native facade copies resumed selected detail"),
            UCk_Utils_CrowdRemoteDebug_UE::TryCopySelectedDetail(PC, SelectedId, 22, Detail));

        auto Request = MakeValidCommand(SelectedId, 21);
        const int32 SubmitBefore = Client.Provider->SubmitCalls;
        auto Invalid = Request;
        Invalid.Id = {};
        Test.TestFalse(TEXT("default command ID rejected"),
            FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Invalid));
        Invalid = Request;
        Invalid.MaxSpeedCmPerSec = std::numeric_limits<float>::quiet_NaN();
        Test.TestFalse(TEXT("nonfinite tunable rejected"),
            FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Invalid));
        Invalid = Request;
        Invalid.Command = static_cast<ECkCrowdRemoteCommand>(255);
        Test.TestFalse(TEXT("unsupported command enum rejected"),
            FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Invalid));
        const auto* CommandEnum = StaticEnum<ECkCrowdRemoteCommand>();
        const auto CommandMax = CommandEnum->GetMaxEnumValue();
        Test.TestTrue(TEXT("command autogenerated MAX is reflected"),
            CommandEnum->IsValidEnumValue(CommandMax) &&
            CommandEnum->GetNameByValue(CommandMax).ToString().EndsWith(TEXT("_MAX")));
        Invalid = Request;
        Invalid.Command = static_cast<ECkCrowdRemoteCommand>(CommandMax);
        Test.TestFalse(TEXT("autogenerated command MAX rejected before provider"),
            FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Invalid));
        Test.TestEqual(TEXT("invalid commands never invoke provider"), Client.Provider->SubmitCalls, SubmitBefore);
        auto AuthoredFinalCommand = Request;
        AuthoredFinalCommand.Command = ECkCrowdRemoteCommand::ManualMove;
        Test.TestTrue(TEXT("authored final command remains valid"),
            FCkCrowdRemoteDebugValidation::IsValid(AuthoredFinalCommand));
        Test.TestTrue(TEXT("valid command reaches exact local owner"),
            FCkCrowdRemoteDebugRegistry::TrySubmit(*World, *PC, *LP, Request));
        Test.TestEqual(TEXT("one valid provider command"), Client.Provider->SubmitCalls, SubmitBefore + 1);
        Test.TestTrue(TEXT("provider received exact PC and local player"),
            Client.Provider->LastSubmitPC.Get() == PC && Client.Provider->LastSubmitLocalPlayer.Get() == LP);
        FCkCrowdRemoteCommandResult Result;
        Test.TestTrue(TEXT("correlated command result accepted"),
            FCkCrowdRemoteDebugRegistry::TryCopyCommandResult(*World, *PC, *LP, 21, Result));
        Test.TestEqual(TEXT("correlated result serial"), Result.ClientSerial, int64{21});
        const auto ValidAcceptedResult = Client.Provider->LastResult;
        Client.Provider->LastResult.ResultSampleSequence = 0;
        Test.TestFalse(TEXT("accepted result requires a published sample"),
            FCkCrowdRemoteDebugValidation::IsValid(Client.Provider->LastResult));
        auto RejectedWithoutSample = Client.Provider->LastResult;
        RejectedWithoutSample.Outcome = ECkCrowdRemoteCommandOutcome::Failed;
        Test.TestTrue(TEXT("failed result may precede any published sample"),
            FCkCrowdRemoteDebugValidation::IsValid(RejectedWithoutSample));
        const int32 SubmitsBeforeMalformedResult = Client.Provider->SubmitCalls;
        Test.TestFalse(TEXT("accepted command without a published sample rejected"),
            FCkCrowdRemoteDebugRegistry::TryCopyCommandResult(*World, *PC, *LP, 21, Result));
        Test.TestFalse(TEXT("malformed accepted result clears entire output"),
            Result.Id.IsValid() || Result.ClientSerial != 0 ||
            Result.AppliedConfigurationRevision != 0 || Result.ResultSampleSequence != 0 ||
            Result.Outcome != ECkCrowdRemoteCommandOutcome::Failed ||
            Result.bHasAppliedDestination || !Result.AppliedDestination.IsZero() ||
            !Result.Diagnostic.IsEmpty());
        Test.TestEqual(TEXT("invalid result does not submit another command"),
            Client.Provider->SubmitCalls, SubmitsBeforeMalformedResult);
        Client.Provider->LastResult = ValidAcceptedResult;
        Test.TestTrue(TEXT("restored valid accepted result remains copyable"),
            FCkCrowdRemoteDebugRegistry::TryCopyCommandResult(*World, *PC, *LP, 21, Result));
        Test.TestEqual(TEXT("restored result sample sequence"), Result.ResultSampleSequence,
            ValidAcceptedResult.ResultSampleSequence);
        auto AuthoredFinalOutcome = Result;
        AuthoredFinalOutcome.Outcome = ECkCrowdRemoteCommandOutcome::Failed;
        Test.TestTrue(TEXT("authored final outcome remains valid"),
            FCkCrowdRemoteDebugValidation::IsValid(AuthoredFinalOutcome));
        const auto* OutcomeEnum = StaticEnum<ECkCrowdRemoteCommandOutcome>();
        const auto OutcomeMax = OutcomeEnum->GetMaxEnumValue();
        Test.TestTrue(TEXT("outcome autogenerated MAX is reflected"),
            OutcomeEnum->IsValidEnumValue(OutcomeMax) &&
            OutcomeEnum->GetNameByValue(OutcomeMax).ToString().EndsWith(TEXT("_MAX")));
        AuthoredFinalOutcome.Outcome = static_cast<ECkCrowdRemoteCommandOutcome>(OutcomeMax);
        Test.TestFalse(TEXT("autogenerated outcome MAX rejected"),
            FCkCrowdRemoteDebugValidation::IsValid(AuthoredFinalOutcome));
        Test.TestFalse(TEXT("wrong result serial publishes nothing"),
            FCkCrowdRemoteDebugRegistry::TryCopyCommandResult(*World, *PC, *LP, 22, Result));
        Test.TestFalse(TEXT("wrong result cleared"), Result.Id.IsValid());
        auto ConsumerRequest = MakeValidCommand(SelectedId, 23);
        Test.TestTrue(TEXT("native consumer facade submits owning command"),
            UCk_Utils_CrowdRemoteDebug_UE::TrySubmit(PC, ConsumerRequest));
        Test.TestEqual(TEXT("consumer command invoked same provider once"),
            Client.Provider->SubmitCalls, SubmitBefore + 2);
        Test.TestTrue(TEXT("native consumer facade copies correlated result"),
            UCk_Utils_CrowdRemoteDebug_UE::TryCopyCommandResult(PC, 23, Result));
        Test.TestEqual(TEXT("consumer result retains exact serial"), Result.ClientSerial, int64{23});
        const auto ASRequest = MakeValidCommand(SelectedId, 25);
        const int32 ASRowsBefore = Client.Provider->RowCalls;
        const int32 ASSelectionBefore = Client.Provider->AcceptedSelections;
        const int32 ASDetailBefore = Client.Provider->DetailCalls;
        const int32 ASSubmitBefore = Client.Provider->SubmitCalls;
        const int32 ASResultBefore = Client.Provider->ResultCalls;
        Test.TestTrue(TEXT("AngelScript calls all four Crowd consumer functions with real payload"),
            InvokeASProbe(Test, World, ASProbeClass, PC, ASRequest, FName{TEXT("ProbeValid")}));
        Test.TestEqual(TEXT("AS rows reached exact provider once"),
            Client.Provider->RowCalls, ASRowsBefore + 1);
        Test.TestEqual(TEXT("AS detail reached exact provider once"),
            Client.Provider->DetailCalls, ASDetailBefore + 1);
        Test.TestEqual(TEXT("AS selection request admitted by exact provider"),
            Client.Provider->AcceptedSelections, ASSelectionBefore + 1);
        Test.TestEqual(TEXT("AS command reached exact provider once"),
            Client.Provider->SubmitCalls, ASSubmitBefore + 1);
        Test.TestEqual(TEXT("AS result reached exact provider once"),
            Client.Provider->ResultCalls, ASResultBefore + 1);
        Test.TestTrue(TEXT("AS command preserved exact owning PC/local player"),
            Client.Provider->LastSubmitPC.Get() == PC &&
            Client.Provider->LastSubmitLocalPlayer.Get() == LP &&
            Client.Provider->LastResult.ClientSerial == ASRequest.ClientSerial);
        const int32 InvalidRowsBefore = Client.Provider->RowCalls;
        const int32 InvalidSelectionBefore = Client.Provider->SelectionCalls;
        const int32 InvalidDetailBefore = Client.Provider->DetailCalls;
        const int32 InvalidSubmitBefore = Client.Provider->SubmitCalls;
        const int32 InvalidResultBefore = Client.Provider->ResultCalls;
        Test.TestTrue(TEXT("AngelScript rejects null PC across all four Crowd consumers"),
            InvokeASProbe(Test, World, ASProbeClass, nullptr, ASRequest,
                FName{TEXT("ProbeInvalidPC")}));
        Test.TestEqual(TEXT("AS null PC invokes no rows provider"),
            Client.Provider->RowCalls, InvalidRowsBefore);
        Test.TestEqual(TEXT("AS null PC invokes no detail provider"),
            Client.Provider->DetailCalls, InvalidDetailBefore);
        Test.TestEqual(TEXT("AS null PC invokes no selection provider"),
            Client.Provider->SelectionCalls, InvalidSelectionBefore);
        Test.TestEqual(TEXT("AS null PC invokes no command provider"),
            Client.Provider->SubmitCalls, InvalidSubmitBefore);
        Test.TestEqual(TEXT("AS null PC invokes no result provider"),
            Client.Provider->ResultCalls, InvalidResultBefore);
        Client.bExercised = true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkCrowdRemoteDebug_ProviderContract,
    "Ck.Crowd.RemoteDebug.ProviderContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkCrowdRemoteDebug_ProviderContract::
    RunTest(
        const FString& Parameters
    )
    -> bool
{
    using namespace ck_crowd_remote_debug_provider_test;
    UClass* ASProbeClass = FindFirstObject<UClass>(
        TEXT("Ck_CrowdRemoteDebug_ASProbe"), EFindFirstObjectOptions::None);
    if (!IsValid(ASProbeClass))
    {
        ASProbeClass = FSoftClassPath{
            TEXT("/Script/Angelscript.Ck_CrowdRemoteDebug_ASProbe")}.TryLoadClass<UObject>();
    }
    if (!TestNotNull(TEXT("AS Crowd consumer probe class"), ASProbeClass))
    { return false; }
    if (!TestEqual(TEXT("AS Crowd consumer exact class path"), ASProbeClass->GetPathName(),
        FString{TEXT("/Script/Angelscript.Ck_CrowdRemoteDebug_ASProbe")}) ||
        !TestNotNull(TEXT("AS Crowd ProbeValid method"),
            ASProbeClass->FindFunctionByName(FName{TEXT("ProbeValid")})) ||
        !TestNotNull(TEXT("AS Crowd ProbeInvalidPC method"),
            ASProbeClass->FindFunctionByName(FName{TEXT("ProbeInvalidPC")})))
    { return false; }
    const auto Run = MakeShared<FRun>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(
        3, TEXT("/Engine/Maps/Entry"), Parameters));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(3, 60.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this,
        FCk_NetAutoTest_Condition::CreateLambda([]() -> bool
        {
            for (int32 Index = 0; Index < 2; ++Index)
            {
                APlayerController* PC = nullptr;
                ULocalPlayer* LP = nullptr;
                if (!TryLocalContext(ck::auto_test::net::Get_ClientWorld(Index), PC, LP))
                { return false; }
                if (!IsValid(ck::auto_test::net::TryGet_RemoteClientPlayerController(
                    ck::auto_test::net::Get_ServerWorld(), Index)))
                { return false; }
            }
            return true;
        }), 60.0, TEXT("two remote local-player contexts and correlated authority controllers become ready")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, ASProbeClass](UWorld* World)
        { ExerciseClient(*this, World, 0, Run->Clients[0], ASProbeClass); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run, ASProbeClass](UWorld* World)
        { ExerciseClient(*this, World, 1, Run->Clients[1], ASProbeClass); })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, Run]() -> bool
        {
            FClient& A = Run->Clients[0];
            FClient& B = Run->Clients[1];
            if (!TestTrue(TEXT("both remote client probes exercised"), A.bExercised && B.bExercised) ||
                !TestTrue(TEXT("two distinct client PIE worlds"), A.World.IsValid() && B.World.IsValid() &&
                    A.World.Get() != B.World.Get()))
            { return false; }
            TArray<FCkCrowdRemoteAgentRow> Rows;
            FCkCrowdRemoteAgentDetail Detail;
            auto Request = MakeValidCommand(MakeRow(A.Provider->WorldGeneration, 1).Id, 31);
            const int32 CallsA = A.Provider->RowCalls;
            const int32 CallsB = B.Provider->RowCalls;
            const int32 DetailCallsA = A.Provider->DetailCalls;
            const int32 SelectionCallsA = A.Provider->SelectionCalls;
            const int32 SubmitsA = A.Provider->SubmitCalls;
            TestFalse(TEXT("crossed local player rejects rows"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*A.World.Get(), *A.PC.Get(),
                    *B.LocalPlayer.Get(), Rows));
            TestEqual(TEXT("crossed rows publish nothing"), Rows.Num(), 0);
            TestFalse(TEXT("crossed world rejects rows"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*B.World.Get(), *A.PC.Get(),
                    *A.LocalPlayer.Get(), Rows));
            TestFalse(TEXT("crossed local player rejects selection request"),
                FCkCrowdRemoteDebugRegistry::TryRequestSelection(*A.World.Get(),
                    *A.PC.Get(), *B.LocalPlayer.Get(), Request.Id, 45));
            TestFalse(TEXT("crossed world rejects selection request"),
                FCkCrowdRemoteDebugRegistry::TryRequestSelection(*B.World.Get(),
                    *A.PC.Get(), *A.LocalPlayer.Get(), Request.Id, 45));
            TestFalse(TEXT("crossed local player rejects selected detail"),
                FCkCrowdRemoteDebugRegistry::TryCopySelectedDetail(*A.World.Get(),
                    *A.PC.Get(), *B.LocalPlayer.Get(), Request.Id, 32, Detail));
            TestFalse(TEXT("crossed local player rejects command"),
                FCkCrowdRemoteDebugRegistry::TrySubmit(*A.World.Get(),
                    *A.PC.Get(), *B.LocalPlayer.Get(), Request));
            TestFalse(TEXT("other world cannot unregister first provider"),
                FCkCrowdRemoteDebugRegistry::TryUnregister(*B.World.Get(), *A.Provider, A.Token));
            TestEqual(TEXT("crossed context does not call first provider rows"), A.Provider->RowCalls, CallsA);
            TestEqual(TEXT("crossed context does not call second provider rows"), B.Provider->RowCalls, CallsB);
            TestEqual(TEXT("crossed context does not call selected provider"), A.Provider->DetailCalls, DetailCallsA);
            TestEqual(TEXT("crossed context does not call selection provider"),
                A.Provider->SelectionCalls, SelectionCallsA);
            TestEqual(TEXT("crossed context does not submit"), A.Provider->SubmitCalls, SubmitsA);
            TestTrue(TEXT("wrong-world unregister preserves first provider"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*A.World.Get(),
                    *A.PC.Get(), *A.LocalPlayer.Get(), Rows));
            TestEqual(TEXT("first provider still returns complete roster"), Rows.Num(), ProbeRows);
            return true;
        }), TEXT("cross-world local context and registry identity")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(0,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            FClient& A = Run->Clients[0];
            if (!TestTrue(TEXT("first client context remains valid before stale-provider probe"),
                IsValid(World) && A.Provider.IsValid() && A.PC.IsValid() &&
                A.LocalPlayer.IsValid()))
            { return; }
            const int32 Before = A.Provider->RowCalls;
            A.Provider->MarkAsGarbage();
            TArray<FCkCrowdRemoteAgentRow> Rows;
            TestFalse(TEXT("garbage provider is not called"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *A.PC.Get(),
                    *A.LocalPlayer.Get(), Rows));
            TestEqual(TEXT("stale provider did not receive callback"), A.Provider->RowCalls, Before);
            TestEqual(TEXT("stale provider publishes no rows"), Rows.Num(), 0);
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnClient(1,
        FCk_NetAutoTest_ServerAction::CreateLambda([this, Run](UWorld* World)
        {
            FClient& B = Run->Clients[1];
            if (!TestTrue(TEXT("second client context remains valid before exact unregister"),
                IsValid(World) && B.Provider.IsValid() && B.PC.IsValid() &&
                B.LocalPlayer.IsValid()))
            { return; }
            TestTrue(TEXT("exact owner and token unregister"),
                FCkCrowdRemoteDebugRegistry::TryUnregister(*World, *B.Provider, B.Token));
            TArray<FCkCrowdRemoteAgentRow> Rows;
            const int32 Before = B.Provider->RowCalls;
            TestFalse(TEXT("unregistered provider is not called"),
                FCkCrowdRemoteDebugRegistry::TryCopyRows(*World, *B.PC.Get(),
                    *B.LocalPlayer.Get(), Rows));
            TestEqual(TEXT("unregistered provider callback count stable"), B.Provider->RowCalls, Before);
            TestTrue(TEXT("replacement owner registers after exact removal"),
                FCkCrowdRemoteDebugRegistry::TryRegister(*World, *B.Other, B.Token));
            TestTrue(TEXT("replacement owner exact removal"),
                FCkCrowdRemoteDebugRegistry::TryUnregister(*World, *B.Other, B.Token));
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

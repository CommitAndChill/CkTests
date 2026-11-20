#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkGrid/HexGridSystem/Authoring/CkHexGridObject_EntityScript.h"
#include "CkTests/Net/CkAutoTest_NetSubject_Utils.h"
#include "UObject/StrongObjectPtr.h"
#include "CkGrid/HexGridSystem/Object/CkHexGridObject_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "Engine/World.h"

namespace ck_hex_footprint_test
{
    using FObject = UCk_Utils_HexGridObject_UE;
    using FFailure = ECk_HexGridObject_Failure;
    using FCoord = FCk_HexCoord;
    using FLife = UCk_Utils_EntityLifetime_UE;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    const FCoord Directions[] = {{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}};

    void Validate(FAutomationTestBase& Test, FCk_Handle& Owner)
    {
        auto Children = FLife::Get_LifetimeDependents(Owner);
        FCk_HexGridObject_Spec Spec;
        FCk_Handle_HexGridObject Object;
        FFailure Failure{};
        auto Reject = [&](TArray<FCoord> Offsets, FFailure Expected)
        {
            Spec.Set_FootprintOffsets(Offsets);
            Test.TestTrue(TEXT("Invalid Add clears output and leaves owner uncomposed"),
                !FObject::TryAdd(Owner, Spec, Object, Failure) && Failure == Expected
                && !FObject::Has(Owner) && ck::Is_NOT_Valid(Object));
            Test.TestTrue(TEXT("Invalid Create rejects before allocating a child"),
                !FObject::TryCreate(Owner, Spec, Object, Failure) && Failure == Expected
                && ck::Is_NOT_Valid(Object) && FLife::Get_LifetimeDependents(Owner) == Children);
        };
        Reject({}, FFailure::EmptyFootprint);
        Reject({{1,0},{-1,1},{1,0}}, FFailure::DuplicateOffset);
        TArray<FCoord> Oversized;
        Oversized.SetNum(65537);
        Reject(MoveTemp(Oversized), FFailure::TooManyCells);
        Spec.Set_FootprintOffsets({{2,0},{1,0}});
        if (!Test.TestTrue(TEXT("Valid shape composes"), FObject::TryAdd(Owner, Spec, Object, Failure)))
        { return; }
        Spec.Set_FootprintOffsets({});
        Test.TestEqual(TEXT("Object owns immutable offsets"), FObject::Get_Spec(Object).Get_FootprintOffsets().Num(), 2);
        const FCoord Anchor{-17,6};
        for (int32 Turn = 0; Turn < 6; ++Turn)
        {
            auto Direction = Directions[Turn];
            auto Expected = TArray<FCoord>{
                {Anchor.Get_Q()+Direction.Get_Q(), Anchor.Get_R()+Direction.Get_R()},
                {Anchor.Get_Q()+2*Direction.Get_Q(), Anchor.Get_R()+2*Direction.Get_R()}};
            if (Direction.Get_R() < 0 || (Direction.Get_R() == 0 && Direction.Get_Q() < 0))
            { Expected.Swap(0,1); }
            auto Cells = FObject::TryResolve_Cells(Object, Anchor, static_cast<ECk_HexRotation>(Turn), ECk_HexGridConnectivity::RequireConnected);
            Test.TestTrue(FString::Printf(TEXT("Turn %d preserves authored hole and sorted offsets"),Turn),
                Cells.Get_Succeeded() && Cells.Get_Failure() == FFailure::None && Cells.Get_Cells() == Expected);
        }
        const auto BadTurn = FObject::TryResolve_Cells(Object, {}, static_cast<ECk_HexRotation>(77), ECk_HexGridConnectivity::Ignore);
        Test.TestTrue(TEXT("Invalid rotation fails explicitly"), !BadTurn.Get_Succeeded()
            && BadTurn.Get_Failure() == FFailure::InvalidRotation && BadTurn.Get_Cells().IsEmpty());
        const auto Overflow = FObject::TryResolve_Cells(Object, {MAX_int32-1,0}, ECk_HexRotation::Turn0, ECk_HexGridConnectivity::Ignore);
        Test.TestTrue(TEXT("Out-of-range translation has no partial cells"), !Overflow.Get_Succeeded()
            && Overflow.Get_Failure() == FFailure::Overflow && Overflow.Get_Cells().IsEmpty());
        const auto Empty = FObject::TryResolve_Cells({}, Anchor, ECk_HexRotation::Turn0, ECk_HexGridConnectivity::Ignore);
        Test.TestTrue(TEXT("Invalid object cannot yield empty success"), !Empty.Get_Succeeded()
            && Empty.Get_Failure() == FFailure::InvalidObject && Empty.Get_Cells().IsEmpty());
    }

    void Connectivity(FAutomationTestBase& Test, FCk_Handle& Owner)
    {
        auto Check = [&](TArray<FCoord> Offsets, bool Connected)
        {
            FCk_HexGridObject_Spec Spec;
            Spec.Set_FootprintOffsets(Offsets);
            FCk_Handle_HexGridObject Object;
            FFailure Failure{};
            if (!Test.TestTrue(TEXT("Distinct offsets create a child"), FObject::TryCreate(Owner, Spec, Object, Failure)))
            { return; }
            const auto Free = FObject::TryResolve_Cells(Object, {}, ECk_HexRotation::Turn0, ECk_HexGridConnectivity::Ignore);
            Test.TestTrue(TEXT("Ignore admits all valid islands"), Free.Get_Succeeded() && Free.Get_Cells().Num() == Offsets.Num());
            const auto Strict = FObject::TryResolve_Cells(Object, {}, ECk_HexRotation::Turn0, ECk_HexGridConnectivity::RequireConnected);
            Test.TestTrue(TEXT("Six-edge connectivity"), Strict.Get_Succeeded() == Connected
                && Strict.Get_Failure() == (Connected ? FFailure::None : FFailure::Disconnected)
                && Strict.Get_Cells().Num() == (Connected ? Offsets.Num() : 0));
            const auto Bad = FObject::TryResolve_Cells(Object, {}, ECk_HexRotation::Turn0, static_cast<ECk_HexGridConnectivity>(77));
            Test.TestTrue(TEXT("Malformed connectivity rejects"), !Bad.Get_Succeeded()
                && Bad.Get_Failure() == FFailure::InvalidConnectivity && Bad.Get_Cells().IsEmpty());
        };
        for (const auto Direction : Directions) { Check({{},Direction}, true); }
        Check({{},{1,0},{3,0},{4,0}}, false);
        Check({{MAX_int32,0},{MAX_int32-1,0}}, true);
        Check({{MAX_int32,0},{MIN_int32,0}}, false);
    }

    void Queue(FAutomationTestBase& Test, void (*Check)(FAutomationTestBase&, FCk_Handle&))
    {
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [&Test,Check](UWorld* World)
            {
                auto Parent = FLife::Request_CreateEntity_TransientOwner(World);
                UCk_Utils_Net_UE::Add(Parent, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                    ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
                Check(Test, Parent);
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexFootprints, "Ck.HexGrid.Rules.FootprintValidation", ck_hex_footprint_test::Flags)
bool FCkHexFootprints::RunTest(const FString&)
{
    ck_hex_footprint_test::Queue(*this, ck_hex_footprint_test::Validate);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexConnectivity, "Ck.HexGrid.Rules.Connectivity", ck_hex_footprint_test::Flags)
bool FCkHexConnectivity::RunTest(const FString&)
{
    ck_hex_footprint_test::Queue(*this, ck_hex_footprint_test::Connectivity);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkHexObjectAuthoring, "Ck.HexGrid.Rules.ObjectAuthoring", ck_hex_footprint_test::Flags)
bool FCkHexObjectAuthoring::RunTest(const FString&)
{
    using namespace ck_hex_footprint_test;
    struct FScenario
    {
        TStrongObjectPtr<UCk_HexGridObject_AuthoringSpec> Assets[2];
        TStrongObjectPtr<UCk_AutoTest_BodyCapturer_UE> Captures[3];
    };
    auto Scenario = MakeShared<FScenario>();
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.f));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld* World)
        {
            auto Owner = FLife::Request_CreateEntity_TransientOwner(World);
            UCk_Utils_Net_UE::Add(Owner, FCk_Net_ConnectionSettings{ECk_Replication::DoesNotReplicate,
                ECk_Net_NetModeType::ClientAndHost, ECk_Net_EntityNetRole::Authority});
            for (int32 Index = 0; Index < 3; ++Index)
            {
                auto Payload = FCk_HexGridObject_SpawnParams{};
                if (Index < 2)
                {
                    Scenario->Assets[Index].Reset(NewObject<UCk_HexGridObject_AuthoringSpec>());
                    FCk_HexGridObject_Spec Spec;
                    Spec.Set_FootprintOffsets(Index == 0 ? TArray<FCoord>{{1,0},{2,0}} : TArray<FCoord>{{},{ }});
                    Scenario->Assets[Index]->Set_ObjectSpec(Spec);
                    Payload.Set_Spec(TSoftObjectPtr<UCk_HexGridObject_AuthoringSpec>{Scenario->Assets[Index].Get()});
                }
                auto Pending = UCk_Utils_EntityScript_UE::Request_SpawnEntity(Owner, UCk_HexGridObject_EntityScript::StaticClass(),
                    FInstancedStruct::Make(Payload), {});
                if (!TestTrue(TEXT("EntityScript spawn request admitted"),
                    UCk_Utils_PendingEntityScript_UE::Get_IsValid(Pending))) { return; }
                Scenario->Captures[Index].Reset(NewObject<UCk_AutoTest_BodyCapturer_UE>());
                FCk_Delegate_EntityScript_Constructed Callback;
                Callback.BindDynamic(Scenario->Captures[Index].Get(), &UCk_AutoTest_BodyCapturer_UE::OnBodyConstructed);
                UCk_Utils_PendingEntityScript_UE::Promise_OnConstructed(Pending, Callback);
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(this, FCk_NetAutoTest_Condition::CreateLambda(
        [Scenario]()
        {
            for (const auto& Capture : Scenario->Captures)
            {
                if (!Capture.IsValid() || Capture->_CapturedBodies.Num() != 1) { return false; }
            }
            return true;
        }), 10.f, TEXT("All three object EntityScripts reported their constructed handles")));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
        [this, Scenario](UWorld*)
        {
            const FFailure Expected[] = {FFailure::None, FFailure::DuplicateOffset, FFailure::InvalidObject};
            for (int32 Index = 0; Index < 3; ++Index)
            {
                const auto& Capture = Scenario->Captures[Index];
                if (!Capture.IsValid() || Capture->_CapturedBodies.Num() != 1) { return; }
                auto Body = Capture->_CapturedBodies[0];
                const auto* Script = Cast<UCk_HexGridObject_EntityScript>(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Body));
                TestTrue(TEXT("Object composition reports exact per-instance outcome"), Script && Script->Get_ObjectFailure() == Expected[Index]);
                TestEqual(TEXT("Only the valid asset installed a feature"), FObject::Has(Body), Index == 0);
                if (Index == 0 && FObject::Has(Body))
                {
                    TestTrue(TEXT("Authored offsets survived spawn injection"),
                        FObject::Get_Spec(FObject::Cast(Body)).Get_FootprintOffsets() == TArray<FCoord>{{1,0},{2,0}});
                }
            }
        })));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    return true;
}

#endif

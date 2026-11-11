#include "CkProceduralAnimation/Core/CkProceduralSurfaceMotion.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Fragment_Data.h"
#include "../../CkUnitTest_Common.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_test_procedural_support_return
{
    auto
        Hit()
        -> ck::FProceduralSurfaceHit
    {
        return ck::FProceduralSurfaceHit{}.Set_Hit(true).Set_Position(FVector::ZeroVector)
            .Set_Normal(FVector::UpVector).Set_Fraction(0.5f);
    }

    auto
        State()
        -> ck::FProceduralSurfaceMotionState
    {
        return ck::FProceduralSurfaceMotionState{}.Set_Grounded(true).Set_ContactTrusted(true)
            .Set_SupportNormal(FVector::ForwardVector).Set_StepForward(FVector::RightVector)
            .Set_TravelTangent(FVector::RightVector).Set_SupportPoint(FVector{2.0, 3.0, 4.0})
            .Set_ContactSource(ck::EProceduralSurfaceContactSource::Fan).Set_Velocity(FVector{1.0, 2.0, 3.0})
            .Set_MissingContact(FCk_Time{0.1}).Set_CandidateSeen(FCk_Time{0.2})
            .Set_CandidateNormal(FVector::ForwardVector).Set_CandidatePoint(FVector{4.0, 5.0, 6.0})
            .Set_Obstruction(ck::EProceduralSurfaceObstruction::Wall).Set_ObstructionPoint(FVector{7.0, 8.0, 9.0})
            .Set_ObstructionNormal(FVector::ForwardVector).Set_ObstructionStandoff(12.0f)
            .Set_ClimbAcquisition(Hit());
    }

    auto
        Body()
        -> FTransform
    {
        return FTransform{FRotationMatrix::MakeFromZX(FVector::ForwardVector, FVector::RightVector).ToQuat(),
            FVector{0.0, 0.0, 100.0}};
    }

    auto
        Feet()
        -> ck::FProceduralSurfaceFeetSupport
    {
        return ck::FProceduralSurfaceFeetSupport{}.Set_Point(FVector::ZeroVector).Set_Normal(FVector::UpVector)
            .Set_Basis(FQuat::Identity).Set_Origin(FVector::ZeroVector)
            .Set_FootprintMin(FVector2D{-20.0, -20.0}).Set_FootprintMax(FVector2D{20.0, 20.0});
    }

    auto
        SameState(const ck::FProceduralSurfaceMotionState& A, const ck::FProceduralSurfaceMotionState& B)
        -> bool
    {
        const auto SameAcquisition = A.Get_ClimbAcquisition().IsSet() == B.Get_ClimbAcquisition().IsSet()
            && (NOT A.Get_ClimbAcquisition().IsSet()
                || (A.Get_ClimbAcquisition()->Get_Hit() == B.Get_ClimbAcquisition()->Get_Hit()
                    && A.Get_ClimbAcquisition()->Get_Position() == B.Get_ClimbAcquisition()->Get_Position()
                    && A.Get_ClimbAcquisition()->Get_Normal() == B.Get_ClimbAcquisition()->Get_Normal()
                    && A.Get_ClimbAcquisition()->Get_Fraction() == B.Get_ClimbAcquisition()->Get_Fraction()));
        return A.Get_SupportNormal() == B.Get_SupportNormal() && A.Get_SupportPoint() == B.Get_SupportPoint()
            && A.Get_TravelTangent() == B.Get_TravelTangent() && A.Get_StepForward() == B.Get_StepForward()
            && A.Get_Velocity() == B.Get_Velocity() && A.Get_MissingContact().Get_Seconds() == B.Get_MissingContact().Get_Seconds()
            && A.Get_Grounded() == B.Get_Grounded() && A.Get_ContactTrusted() == B.Get_ContactTrusted()
            && A.Get_ContactSource() == B.Get_ContactSource() && A.Get_CandidateNormal() == B.Get_CandidateNormal()
            && A.Get_CandidatePoint() == B.Get_CandidatePoint() && A.Get_CandidateSeen().Get_Seconds() == B.Get_CandidateSeen().Get_Seconds()
            && A.Get_Obstruction() == B.Get_Obstruction() && A.Get_ObstructionPoint() == B.Get_ObstructionPoint()
            && A.Get_ObstructionNormal() == B.Get_ObstructionNormal() && A.Get_ObstructionStandoff() == B.Get_ObstructionStandoff()
            && SameAcquisition;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportReturnContactTest,
    "Ck.ProceduralAnimation.Core.SupportReturn.ContactEligibility", ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralSupportReturnContactTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_support_return;
    const auto Settings = ck::FProceduralSurfaceMotionSettings{};
    const auto Plants = TArray<ck::FProceduralSurfacePlantedContact>{{FVector::ZeroVector, FVector::UpVector}};
    const auto Eligible = [&](const auto& InSettings, const TOptional<ck::FProceduralSurfaceFeetSupport>& InFeet,
        TArrayView<const ck::FProceduralSurfacePlantedContact> InPlants, const auto& InBody, const auto& InState, const auto& InHit)
    {
        return ck::Get_IsProceduralSupportReturnContactEligible(InSettings, InFeet, InPlants, InBody, InState, InHit);
    };
    TestTrue(TEXT("Gravity-facing contact, planted witness and aligned feet footprint qualify"),
        Eligible(Settings, Feet(), Plants, Body(), State(), Hit()));
    TestFalse(TEXT("Missing feet cannot supply a support plane"), Eligible(Settings, {}, Plants, Body(), State(), Hit()));
    TestFalse(TEXT("No trusted planted witness cannot qualify"), Eligible(Settings, Feet(), {}, Body(), State(), Hit()));
    const auto WrongPlant = TArray<ck::FProceduralSurfacePlantedContact>{{FVector::ZeroVector, FVector::ForwardVector}};
    TestFalse(TEXT("Wrong-normal plant cannot witness the returned face"),
        Eligible(Settings, Feet(), WrongPlant, Body(), State(), Hit()));
    const auto OffPlane = TArray<ck::FProceduralSurfacePlantedContact>{{FVector{0.0, 0.0, 100.0}, FVector::UpVector}};
    TestFalse(TEXT("A distant plant cannot witness this contact plane"), Eligible(Settings, Feet(), OffPlane, Body(), State(), Hit()));
    auto FarBody = Body();
    FarBody.SetLocation(FVector{1000.0, 0.0, 100.0});
    TestFalse(TEXT("Body outside the existing expanded footprint rejects"), Eligible(Settings, Feet(), Plants, FarBody, State(), Hit()));
    TestFalse(TEXT("Feet aligned to the old wall do not prove improved support"),
        Eligible(Settings, Feet().Set_Normal(FVector::ForwardVector), Plants, Body(), State(), Hit()));
    TestFalse(TEXT("Inverted footprint rejects"), Eligible(Settings, Feet().Set_FootprintMin(FVector2D{30.0, 30.0}), Plants, Body(), State(), Hit()));
    TestFalse(TEXT("No support-normal change is not a support return"),
        Eligible(Settings, Feet(), Plants, Body(), State().Set_SupportNormal(FVector::UpVector), Hit()));
    TestFalse(TEXT("Untrusted or airborne state rejects"), Eligible(Settings, Feet(), Plants, Body(), State().Set_ContactTrusted(false), Hit()));
    TestFalse(TEXT("Airborne state rejects independently"), Eligible(Settings, Feet(), Plants, Body(), State().Set_Grounded(false), Hit()));
    TestFalse(TEXT("Wrong gravity-facing normal rejects"), Eligible(Settings, Feet(), Plants, Body(), State(), Hit().Set_Normal(FVector::ForwardVector)));
    TestFalse(TEXT("Default contact normal rejects before alignment arithmetic"),
        Eligible(Settings, Feet(), Plants, Body(), State(), Hit().Set_Normal(FVector::ZeroVector)));
    TestFalse(TEXT("Zero gravity rejects"), Eligible(ck::FProceduralSurfaceMotionSettings{}.Set_Gravity(FVector::ZeroVector), Feet(), Plants, Body(), State(), Hit()));
    TestFalse(TEXT("Authored height plane retains authority"), Eligible(ck::FProceduralSurfaceMotionSettings{}.Set_HeightPlane(FVector::ZeroVector), Feet(), Plants, Body(), State(), Hit()));
    for (const auto Fraction : {0.0f, -1.0f, 1.1f, std::numeric_limits<float>::quiet_NaN()})
    {
        TestFalse(TEXT("Only finite positive native contact fractions at most one qualify"),
            Eligible(Settings, Feet(), Plants, Body(), State(), Hit().Set_Fraction(Fraction)));
    }
    auto TooMany = TArray<ck::FProceduralSurfacePlantedContact>{};
    TooMany.Init(Plants[0], 65);
    TestFalse(TEXT("Plant bound is enforced before an unbounded witness scan"), Eligible(Settings, Feet(), TooMany, Body(), State(), Hit()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportReturnRoomTest,
    "Ck.ProceduralAnimation.Core.SupportReturn.RoomAndConfirmation", ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralSupportReturnRoomTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_support_return;
    const auto Settings = ck::FProceduralSurfaceMotionSettings{};
    auto Calls = 0;
    const auto Clear = [&](const FVector& Start, const FVector& End)
    {
        ++Calls;
        TestTrue(TEXT("Room query retains the existing one-centimetre start and clearance endpoint"),
            Start.Equals(FVector{0.0, 0.0, 1.0}) && End.Equals(FVector{0.0, 0.0, Settings.Get_Clearance()}));
        return ck::FProceduralSurfaceHit{};
    };
    TestTrue(TEXT("Clear room needs exactly the existing single callback"),
        ck::Get_HasProceduralSupportReturnRoom(Settings, Clear, Hit(), FVector::ForwardVector));
    TestEqual(TEXT("One room query, no hidden repeat"), Calls, 1);
    for (const auto Fraction : {0.0f, -1.0f, 1.1f, std::numeric_limits<float>::quiet_NaN()})
    {
        Calls = 0;
        TestFalse(TEXT("Invalid contact fraction rejects before querying room"),
            ck::Get_HasProceduralSupportReturnRoom(Settings, Clear, Hit().Set_Fraction(Fraction), FVector::ForwardVector));
        TestEqual(TEXT("Rejected input does not invoke callback"), Calls, 0);
    }
    Calls = 0;
    TestFalse(TEXT("Missing contact rejects without room work"),
        ck::Get_HasProceduralSupportReturnRoom(Settings, Clear, Hit().Set_Hit(false), FVector::ForwardVector));
    TestFalse(TEXT("Nonunit old Up rejects without room work"),
        ck::Get_HasProceduralSupportReturnRoom(Settings, Clear, Hit(), FVector::ZeroVector));
    TestFalse(TEXT("Nonunit contact normal rejects without room work"),
        ck::Get_HasProceduralSupportReturnRoom(Settings, Clear, Hit().Set_Normal(FVector::ZeroVector), FVector::ForwardVector));
    TestFalse(TEXT("Invalid clearance rejects without room work"), ck::Get_HasProceduralSupportReturnRoom(
        ck::FProceduralSurfaceMotionSettings{}.Set_Clearance(std::numeric_limits<float>::quiet_NaN()), Clear, Hit(), FVector::ForwardVector));
    TestEqual(TEXT("All invalid preflights remain query-free"), Calls, 0);
    const auto Blocked = [&](const FVector&, const FVector&)
    {
        ++Calls;
        return Hit().Set_Position(FVector{0.0, 0.0, 5.0}).Set_Normal(FVector::DownVector);
    };
    TestFalse(TEXT("Actual opposing room blocker rejects"),
        ck::Get_HasProceduralSupportReturnRoom(Settings, Blocked, Hit(), FVector::ForwardVector));
    TestEqual(TEXT("Blocked room still spends just one callback"), Calls, 1);
    Calls = 0;
    const auto MalformedBlocker = [&](const FVector&, const FVector&)
    {
        ++Calls;
        return Hit().Set_Fraction(std::numeric_limits<float>::quiet_NaN());
    };
    TestFalse(TEXT("Malformed returned room fraction cannot certify clearance"),
        ck::Get_HasProceduralSupportReturnRoom(Settings, MalformedBlocker, Hit(), FVector::ForwardVector));
    TestEqual(TEXT("Malformed returned observation is not retried"), Calls, 1);
    TestTrue(TEXT("Identical actual normals remain consistent"),
        ck::Get_IsProceduralSupportReturnNormalConsistent(FVector::UpVector, FVector::UpVector));
    TestFalse(TEXT("A changed support normal does not inherit confirmation time"),
        ck::Get_IsProceduralSupportReturnNormalConsistent(FVector::UpVector, FVector::ForwardVector));
    TestFalse(TEXT("Default normal cannot preserve confirmation"),
        ck::Get_IsProceduralSupportReturnNormalConsistent(FVector::ZeroVector, FVector::UpVector));
    TestFalse(TEXT("No elapsed confirmation cannot satisfy the default interval"), ck::Get_IsProceduralSupportReturnConfirmed(Settings, 0.0));
    TestTrue(TEXT("Actual full confirmation time qualifies"),
        ck::Get_IsProceduralSupportReturnConfirmed(Settings, Settings.Get_ConfirmTime().Get_Seconds()));
    for (const auto Seen : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        TestFalse(TEXT("Malformed accumulated time never confirms"), ck::Get_IsProceduralSupportReturnConfirmed(Settings, Seen));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportReturnAdoptionTest,
    "Ck.ProceduralAnimation.Core.SupportReturn.AtomicAdoptionAndRates", ck::tests::kCkUnitTestFlags)

auto
    FCkProceduralSupportReturnAdoptionTest::
    RunTest(const FString&)
    -> bool
{
    using namespace ck_test_procedural_support_return;
    const auto Settings = ck::FProceduralSurfaceMotionSettings{};
    const auto InputBody = Body();
    const auto InputState = State();
    constexpr auto Seconds = 0.01;
    auto OutBody = FTransform::Identity;
    auto OutState = ck::FProceduralSurfaceMotionState{};
    TestTrue(TEXT("Qualified proposal adopts through existing Core rates"), ck::TryMake_ProceduralSupportReturn(
        Settings, Hit(), FCk_Time{Seconds}, InputBody, InputState, OutBody, OutState));
    TestTrue(TEXT("Clearance correction is capped at old speed times dt and adds no horizontal steering"),
        OutBody.GetLocation().Equals(FVector{0.0, 0.0, 98.0}, 1.0e-9));
    TestTrue(TEXT("Body rotation uses the existing angular rate, not an immediate support-frame snap"),
        FMath::IsNearlyEqual(InputBody.GetRotation().AngularDistance(OutBody.GetRotation()),
            FMath::DegreesToRadians(Settings.Get_SurfaceTurnRateDegrees()) * Seconds, 1.0e-7));
    TestTrue(TEXT("Accepted support, heading and velocity agree with the one proposal"),
        OutState.Get_Grounded() && OutState.Get_ContactTrusted()
            && OutState.Get_SupportNormal() == Hit().Get_Normal() && OutState.Get_SupportPoint() == Hit().Get_Position()
            && OutState.Get_TravelTangent().Equals(FVector::RightVector) && OutState.Get_StepForward() == InputState.Get_StepForward()
            && OutState.Get_Velocity().Equals((OutBody.GetLocation() - InputBody.GetLocation()) / Seconds)
            && OutState.Get_ContactSource() == ck::EProceduralSurfaceContactSource::NativeEnvelope);
    TestTrue(TEXT("Support return clears every coupled obsolete contact/turn/wall/acquisition state"),
        OutState.Get_MissingContact().Get_Seconds() == 0.0 && OutState.Get_CandidateSeen().Get_Seconds() == 0.0
            && OutState.Get_CandidateNormal() == Hit().Get_Normal() && OutState.Get_CandidatePoint() == Hit().Get_Position()
            && OutState.Get_Obstruction() == ck::EProceduralSurfaceObstruction::None
            && OutState.Get_ObstructionPoint() == FVector::ZeroVector && OutState.Get_ObstructionNormal() == FVector::ZeroVector
            && OutState.Get_ObstructionStandoff() == 0.0f && NOT OutState.Get_ClimbAcquisition().IsSet());
    const auto PreservedBody = OutBody;
    const auto PreservedState = OutState;
    for (const auto Dt : {0.0, -0.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::max()})
    {
        TestFalse(TEXT("Invalid dt rejects the complete proposal"), ck::TryMake_ProceduralSupportReturn(
            Settings, Hit(), FCk_Time{Dt}, InputBody, InputState, OutBody, OutState));
        TestTrue(TEXT("Rejected proposal preserves all output fields atomically"),
            OutBody.Equals(PreservedBody, 0.0) && SameState(OutState, PreservedState));
    }
    TestFalse(TEXT("Wrong gravity support cannot publish any output"), ck::TryMake_ProceduralSupportReturn(
        Settings, Hit().Set_Normal(FVector::ForwardVector), FCk_Time{Seconds}, InputBody, InputState, OutBody, OutState));
    TestTrue(TEXT("Invalid contact preserves previous accepted outputs"), OutBody.Equals(PreservedBody, 0.0) && SameState(OutState, PreservedState));
    TestFalse(TEXT("Nonrigid body cannot publish a support return"), ck::TryMake_ProceduralSupportReturn(
        Settings, Hit(), FCk_Time{Seconds}, FTransform{FQuat::Identity, InputBody.GetLocation(), FVector{2.0}},
        InputState, OutBody, OutState));
    TestTrue(TEXT("Malformed body leaves both output values intact"), OutBody.Equals(PreservedBody, 0.0) && SameState(OutState, PreservedState));
    TestTrue(TEXT("All input state fields remain unchanged"), SameState(InputState, State()) && InputBody.Equals(Body(), 0.0));
    const auto* Reflected = StaticEnum<ECk_SurfaceMotion_ContactSource>();
    TestTrue(TEXT("NativeEnvelope has an appended matching reflected provenance value"), Reflected != nullptr
        && Reflected->GetNameStringByValue(static_cast<int64>(ECk_SurfaceMotion_ContactSource::NativeEnvelope)) == TEXT("NativeEnvelope")
        && static_cast<uint8>(ECk_SurfaceMotion_ContactSource::NativeEnvelope) == static_cast<uint8>(ck::EProceduralSurfaceContactSource::NativeEnvelope)
        && static_cast<uint8>(ck::EProceduralSurfaceContactSource::NativeEnvelope) > static_cast<uint8>(ck::EProceduralSurfaceContactSource::Step));
    return true;
}

#endif

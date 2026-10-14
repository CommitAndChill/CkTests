#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimation/Core/CkProceduralSupportRegion.h"

#include "Misc/AutomationTest.h"
#include <limits>

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_support
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
        MakeSquare()
        -> TArray<ck::procedural_support::FContact>
    {
        return {
            {FVector{-10.0, -10.0, 0.0}, FVector::UpVector, true},
            {FVector{10.0, -10.0, 0.0}, FVector::UpVector, true},
            {FVector{10.0, 10.0, 0.0}, FVector::UpVector, true},
            {FVector{-10.0, 10.0, 0.0}, FVector::UpVector, true}};
    }
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportCountTest,
    "Ck.ProceduralAnimation.Action.SupportRegion.ExplicitTrustedCount",
    ck_test_procedural_support::TestFlags)

auto
    FCkProceduralSupportCountTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck::procedural_support;
    auto Contacts = ck_test_procedural_support::MakeSquare();
    TestEqual(TEXT("Default policy cannot authorize borrowing"),
        Evaluate({}, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::Disabled);
    const auto Policy = FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::CountOnly, 3};
    TestEqual(TEXT("Four trusted remaining contacts satisfy three"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::Accepted);
    Contacts.RemoveAt(0);
    TestEqual(TEXT("Prospective first withdrawal still satisfies three"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::Accepted);
    Contacts[0] = FContact{Contacts[0].Get_Position(), FVector::UpVector, false};
    const auto Rejected = Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector);
    TestEqual(TEXT("Untrusted contact cannot support another withdrawal"), Rejected.Get_Result(), EResult::InsufficientContacts);
    TestEqual(TEXT("Evidence reports actual trusted count"), Rejected.Get_TrustedContacts(), 2);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportRegionTest,
    "Ck.ProceduralAnimation.Action.SupportRegion.InsetAndFrame",
    ck_test_procedural_support::TestFlags)

auto
    FCkProceduralSupportRegionTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck::procedural_support;
    auto Contacts = ck_test_procedural_support::MakeSquare();
    auto Policy = FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::ProjectedSupportRegion, 3};
    Policy.Set_ProjectedRegionInsetCm(2.0f);
    const auto Center = Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector);
    TestEqual(TEXT("Root inside inset polygon accepted"), Center.Get_Result(), EResult::Accepted);
    TestTrue(TEXT("Margin measured in centimetres"), FMath::IsNearlyEqual(Center.Get_MinimumEdgeDistanceCm(), 10.0, 1.0e-6));
    auto Region = TArray<FVector>{};
    const auto WithRegion = Evaluate(Policy, Contacts, FVector{0.0, 0.0, 25.0}, FVector::UpVector, &Region);
    TestEqual(TEXT("Optional diagnostic geometry preserves the support verdict"), WithRegion.Get_Result(), Center.Get_Result());
    TestEqual(TEXT("Square exports four projected hull vertices"), Region.Num(), 4);
    for (const auto& Vertex : Region)
    {
        TestTrue(TEXT("Hull is projected on the root proxy plane in centimetres"),
            FMath::IsNearlyEqual(Vertex.Z, 25.0) && FMath::IsNearlyEqual(FMath::Abs(Vertex.X), 10.0)
            && FMath::IsNearlyEqual(FMath::Abs(Vertex.Y), 10.0));
    }
    Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::ZeroVector, &Region);
    TestTrue(TEXT("Rejected diagnostic query clears previous region"), Region.IsEmpty());
    TestEqual(TEXT("Inset boundary is inclusive"),
        Evaluate(Policy, Contacts, FVector{8.0, 0.0, 0.0}, FVector::UpVector).Get_Result(), EResult::Accepted);
    TestEqual(TEXT("Inside polygon but outside inset rejects"),
        Evaluate(Policy, Contacts, FVector{9.0, 0.0, 0.0}, FVector::UpVector).Get_Result(), EResult::OutsideRegion);
    TestEqual(TEXT("Root outside polygon rejects"),
        Evaluate(Policy, Contacts, FVector{11.0, 0.0, 0.0}, FVector::UpVector).Get_Result(), EResult::OutsideRegion);

    const auto Rotation = FQuat{FVector::RightVector, PI / 2.0};
    const auto Translation = FVector{700.0, -400.0, 50.0};
    const auto Up = Rotation.RotateVector(FVector::UpVector);
    for (auto& Contact : Contacts)
    { Contact = FContact{Rotation.RotateVector(Contact.Get_Position()) + Translation, Up, true}; }
    Swap(Contacts[0], Contacts[2]);
    TestEqual(TEXT("Equivalent wall frame and shuffled roster preserve admission"),
        Evaluate(Policy, Contacts, Translation, Up).Get_Result(), EResult::Accepted);
    Contacts[0] = FContact{Contacts[0].Get_Position(), FVector::UpVector, true};
    TestEqual(TEXT("Orthogonal contact surface rejects coherent-frame claim"),
        Evaluate(Policy, Contacts, Translation, Up).Get_Result(), EResult::IncoherentNormals);
    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportInvalidTest,
    "Ck.ProceduralAnimation.Action.SupportRegion.DegenerateAndMalformed",
    ck_test_procedural_support::TestFlags)

auto
    FCkProceduralSupportInvalidTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck::procedural_support;
    auto Policy = FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::ProjectedSupportRegion, 3};
    auto Contacts = TArray<FContact>{
        {FVector{-10.0, 0.0, 0.0}, FVector::UpVector, true},
        {FVector{0.0, 0.0, 0.0}, FVector::UpVector, true},
        {FVector{10.0, 0.0, 0.0}, FVector::UpVector, true}};
    TestEqual(TEXT("Collinear contacts do not fabricate area"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::DegenerateRegion);
    Contacts[1] = Contacts[0];
    TestEqual(TEXT("Coincident contacts do not fabricate area"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::DegenerateRegion);
    TestEqual(TEXT("Non-unit frame rejects"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector{0.0, 0.0, 2.0}).Get_Result(), EResult::InvalidFrame);
    Contacts[1] = FContact{FVector{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, FVector::UpVector, true};
    TestEqual(TEXT("Malformed trusted contact rejects"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::InvalidContact);
    Policy.Set_SupportHoldTimeout(FCk_Time{});
    TestFalse(TEXT("Borrow protection requires a positive finite hold timeout"), ValidatePolicy(Policy));
    Policy.Set_SupportHoldTimeout(FCk_Time{0.25});
    Policy.Set_MaxContactNormalSpreadDegrees(90.0f);
    TestFalse(TEXT("Policy cannot authorize orthogonal support surfaces"), ValidatePolicy(Policy));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportFrameNormalsTest,
    "Ck.ProceduralAnimation.Action.SupportRegion.CoherentNormalsMustAgreeWithSupportFrame",
    ck_test_procedural_support::TestFlags)

auto
    FCkProceduralSupportFrameNormalsTest::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck::procedural_support;
    auto Contacts = ck_test_procedural_support::MakeSquare();
    auto Policy = FCk_ProceduralLimbControl_Support{ECk_ProceduralLimbControl_SupportMode::ProjectedSupportRegion, 3};
    const auto NearWallNormal = FQuat{FVector::RightVector, FMath::DegreesToRadians(89.0)}.RotateVector(FVector::UpVector);
    for (auto& Contact : Contacts)
    { Contact = FContact{Contact.Get_Position(), NearWallNormal, true}; }
    TestEqual(TEXT("Mutually identical near-wall normals cannot authorize floor-frame support"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::IncoherentNormals);
    Policy.Set_Mode(ECk_ProceduralLimbControl_SupportMode::CountOnly);
    TestEqual(TEXT("Contact-count policy still requires coherent support-frame evidence"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::IncoherentNormals);
    const auto BoundaryNormal = FQuat{FVector::RightVector, FMath::DegreesToRadians(20.0)}.RotateVector(FVector::UpVector);
    for (auto& Contact : Contacts)
    { Contact = FContact{Contact.Get_Position(), BoundaryNormal, true}; }
    TestEqual(TEXT("The authored angular cone includes its boundary"),
        Evaluate(Policy, Contacts, FVector::ZeroVector, FVector::UpVector).Get_Result(), EResult::Accepted);
    return true;
}

#endif

// --------------------------------------------------------------------------------------------------------------------

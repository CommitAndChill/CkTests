#include <Misc/AutomationTest.h>

#include <Jolt/Jolt.h>

#if WITH_DEV_AUTOMATION_TESTS

#include "CkJolt/CkJolt_Utils.h"
#include "CkJolt/CollisionLayers/CkJoltCollisionLayerTable.h"
#include "CkJolt/CollisionLayers/CkJoltCollisionLayer_Utils.h"
#include "CkJolt/Constraint/CkJoltConstraint_CollisionFilter.h"

#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>

// --------------------------------------------------------------------------------------------------------------------
// The filter a constraint with ConnectedBodiesCollide = Disable puts its two bodies in, on a standalone physics system
// (no step, no ECS): which pairs it disables, how it hands a pair to another system's filter (Jolt asks only the first
// non-null filter of a pair), and how a body leaves it.
// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_jolt_constraint_collision_filter
{
    constexpr auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    using JPH::RTTI;

    // Another system's filter: a fixed verdict, or (Defers) the same hand-off rule the filter under test follows.
    class FTest_ForeignFilter final : public JPH::GroupFilter
    {
    public:
        bool _Verdict = false;
        bool _Defers = false;
        mutable int32 _Calls = 0;

        auto CanCollide(const JPH::CollisionGroup& InGroupA, const JPH::CollisionGroup& InGroupB) const -> bool override
        {
            ++_Calls;
            if (_Defers && InGroupB.GetGroupFilter() != nullptr && InGroupB.GetGroupFilter() != this)
            { return InGroupB.GetGroupFilter()->CanCollide(InGroupB, InGroupA); }

            return _Verdict;
        }
    };

    struct FScopedPhysics
    {
        FScopedPhysics()
        {
            ck::jolt::Request_GlobalJoltInit();

            _LayerTable = MakeUnique<ck::jolt::FCk_Jolt_CollisionLayerTable>();
            _LayerTable->Build_FromCollisionProfiles();
            _BroadPhaseLayerInterface = MakeUnique<ck::jolt::FCk_Jolt_BroadPhaseLayerInterface_Table>(*_LayerTable);
            _ObjectVsBroadPhaseFilter = MakeUnique<ck::jolt::FCk_Jolt_ObjectVsBroadPhaseLayerFilter_Table>(*_LayerTable);
            _ObjectVsObjectFilter = MakeUnique<ck::jolt::FCk_Jolt_ObjectLayerPairFilter_Table>(*_LayerTable);

            _PhysicsSystem = MakeUnique<JPH::PhysicsSystem>();
            _PhysicsSystem->Init(64, 0, 64, 64, *_BroadPhaseLayerInterface, *_ObjectVsBroadPhaseFilter, *_ObjectVsObjectFilter);

            _Box = new JPH::BoxShape{JPH::Vec3{10.0f, 10.0f, 10.0f}};

            const auto Signature = ck::jolt::TryDerive_SignatureFromProfile(FName{TEXT("BlockAll")}, ECk_Jolt_BodyDomain::Dynamic);
            _Layer = Signature.IsSet()
                ? JPH::ObjectLayer{_LayerTable->Get_OrRegisterLayer(*Signature)}
                : JPH::ObjectLayer{_LayerTable->Get_ProbeLayer()};
        }

        ~FScopedPhysics()
        {
            auto& BodyInterface = _PhysicsSystem->GetBodyInterface();
            for (const auto& BodyId : _Bodies)
            {
                if (BodyInterface.IsAdded(BodyId))
                { BodyInterface.RemoveBody(BodyId); }
                BodyInterface.DestroyBody(BodyId);
            }

            _Box = nullptr;
            _PhysicsSystem.Reset();
            _ObjectVsObjectFilter.Reset();
            _ObjectVsBroadPhaseFilter.Reset();
            _BroadPhaseLayerInterface.Reset();
            _LayerTable.Reset();

            ck::jolt::Request_GlobalJoltShutdown();
        }

        auto Add_Body(const JPH::GroupFilter* InFilter = nullptr) -> JPH::BodyID
        {
            auto Settings = JPH::BodyCreationSettings{_Box.GetPtr(), JPH::RVec3{static_cast<float>(_Bodies.Num()) * 100.0f, 0.0f, 0.0f},
                JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, _Layer};
            Settings.mCollisionGroup = JPH::CollisionGroup{InFilter, 7, 3};

            auto& BodyInterface = _PhysicsSystem->GetBodyInterface();
            auto* Body = BodyInterface.CreateBody(Settings);
            BodyInterface.AddBody(Body->GetID(), JPH::EActivation::DontActivate);
            _Bodies.Add(Body->GetID());
            return Body->GetID();
        }

        auto Get_Group(JPH::BodyID InBody) -> JPH::CollisionGroup
        {
            const auto Lock = JPH::BodyLockRead{_PhysicsSystem->GetBodyLockInterface(), InBody};
            return Lock.GetBody().GetCollisionGroup();
        }

        TUniquePtr<ck::jolt::FCk_Jolt_CollisionLayerTable> _LayerTable;
        TUniquePtr<ck::jolt::FCk_Jolt_BroadPhaseLayerInterface_Table> _BroadPhaseLayerInterface;
        TUniquePtr<ck::jolt::FCk_Jolt_ObjectVsBroadPhaseLayerFilter_Table> _ObjectVsBroadPhaseFilter;
        TUniquePtr<ck::jolt::FCk_Jolt_ObjectLayerPairFilter_Table> _ObjectVsObjectFilter;
        TUniquePtr<JPH::PhysicsSystem> _PhysicsSystem;
        JPH::RefConst<JPH::Shape> _Box;
        JPH::ObjectLayer _Layer = 0;
        TArray<JPH::BodyID> _Bodies;
    };
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_JoltConstraint_CollisionFilter_PairsJoinAndLeave,
    "Ck.Jolt.Constraint.CollisionFilter.PairsJoinAndLeave",
    ck_test_jolt_constraint_collision_filter::TestFlags)

bool FCkTest_JoltConstraint_CollisionFilter_PairsJoinAndLeave::RunTest(const FString&)
{
    using namespace ck_test_jolt_constraint_collision_filter;
    using EJoin = ck::jolt::ECk_JoltConstraint_CollisionFilter_JoinResult;

    auto Physics = FScopedPhysics{};
    auto& System = *Physics._PhysicsSystem;
    auto Filter = JPH::Ref<ck::jolt::FCk_JoltConstraint_CollisionFilter>{new ck::jolt::FCk_JoltConstraint_CollisionFilter{}};

    const auto A = Physics.Add_Body();
    const auto B = Physics.Add_Body();
    const auto C = Physics.Add_Body();

    TestTrue(TEXT("a pair joins"), Filter->Request_DisablePair(System, A, B) == EJoin::Joined);
    TestTrue(TEXT("a joined body carries the filter"), Physics.Get_Group(A).GetGroupFilter() == Filter.GetPtr());
    TestEqual(TEXT("a joined body keeps its group id"), Physics.Get_Group(A).GetGroupID(), JPH::CollisionGroup::GroupID{7});
    TestEqual(TEXT("a joined body's sub-group is its own body id"), Physics.Get_Group(B).GetSubGroupID(), B.GetIndexAndSequenceNumber());
    TestFalse(TEXT("the disabled pair does not collide"), Physics.Get_Group(A).CanCollide(Physics.Get_Group(B)));
    TestFalse(TEXT("... whichever body Jolt asks first"), Physics.Get_Group(B).CanCollide(Physics.Get_Group(A)));

    // Per pair, not per chain.
    TestTrue(TEXT("a second pair joins"), Filter->Request_DisablePair(System, B, C) == EJoin::Joined);
    TestTrue(TEXT("the two ends of a chain still collide"), Physics.Get_Group(A).CanCollide(Physics.Get_Group(C)));

    // Counted: two disables of one pair need two enables.
    Filter->Request_DisablePair(System, A, B);
    Filter->Request_EnablePair(System, A, B);
    TestTrue(TEXT("one of two disables undone: the pair still does not collide"), Filter->Get_IsPairDisabled(A, B));
    Filter->Request_EnablePair(System, A, B);
    TestFalse(TEXT("both undone: the pair collides"), Filter->Get_IsPairDisabled(A, B));
    TestTrue(TEXT("both undone: the groups say so"), Physics.Get_Group(A).CanCollide(Physics.Get_Group(B)));
    TestFalse(TEXT("a body with no pair left leaves"), Filter->Get_IsJoined(A));
    TestTrue(TEXT("... with its original group back"),
        Physics.Get_Group(A).GetGroupFilter() == nullptr && Physics.Get_Group(A).GetSubGroupID() == 3);
    TestTrue(TEXT("a body still in a pair stays"), Filter->Get_IsJoined(B));

    // A body that is destroyed takes its pairs with it.
    Filter->Request_ForgetBody(System, C);
    TestFalse(TEXT("a forgotten body's pair is gone"), Filter->Get_IsPairDisabled(B, C));
    TestFalse(TEXT("its partner, left with none, leaves"), Filter->Get_IsJoined(B));
    TestTrue(TEXT("... with its original group back"), Physics.Get_Group(B).GetGroupFilter() == nullptr);

    TestFalse(TEXT("enabling a pair never disabled is a no-op"), Filter->Get_IsJoined(A));
    Filter->Request_EnablePair(System, A, C);

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkTest_JoltConstraint_CollisionFilter_DefersToAForeignFilter,
    "Ck.Jolt.Constraint.CollisionFilter.DefersToAForeignFilter",
    ck_test_jolt_constraint_collision_filter::TestFlags)

bool FCkTest_JoltConstraint_CollisionFilter_DefersToAForeignFilter::RunTest(const FString&)
{
    using namespace ck_test_jolt_constraint_collision_filter;
    using EJoin = ck::jolt::ECk_JoltConstraint_CollisionFilter_JoinResult;

    auto Physics = FScopedPhysics{};
    auto& System = *Physics._PhysicsSystem;
    auto Filter = JPH::Ref<ck::jolt::FCk_JoltConstraint_CollisionFilter>{new ck::jolt::FCk_JoltConstraint_CollisionFilter{}};
    auto Foreign = JPH::Ref<FTest_ForeignFilter>{new FTest_ForeignFilter{}};

    const auto A = Physics.Add_Body();
    const auto B = Physics.Add_Body();
    const auto Other = Physics.Add_Body(Foreign.GetPtr());
    Filter->Request_DisablePair(System, A, B);

    // Jolt asks the joined body's filter first: it must give the other system's verdict, asked as Jolt asked it before.
    Foreign->_Verdict = false;
    TestFalse(TEXT("a foreign filter's 'no' stands"), Physics.Get_Group(A).CanCollide(Physics.Get_Group(Other)));
    TestTrue(TEXT("... and it was asked"), Foreign->_Calls > 0);
    Foreign->_Verdict = true;
    TestTrue(TEXT("a foreign filter's 'yes' stands"), Physics.Get_Group(A).CanCollide(Physics.Get_Group(Other)));
    TestTrue(TEXT("a joined body against a body with no filter collides"),
        Physics.Get_Group(A).CanCollide(JPH::CollisionGroup{}));

    // A filter that defers the same way hands the pair straight back: no recursion, and the pair collides.
    Foreign->_Defers = true;
    Foreign->_Verdict = false;
    Foreign->_Calls = 0;
    TestTrue(TEXT("two deferring filters settle on 'collide'"), Physics.Get_Group(A).CanCollide(Physics.Get_Group(Other)));
    TestTrue(TEXT("... after asking the foreign filter once"), Foreign->_Calls == 1);
    Foreign->_Defers = false;

    // A body that carries another system's filter never joins, and a refusal changes nothing.
    TestTrue(TEXT("a body with a foreign filter is refused"), Filter->Request_DisablePair(System, A, Other) == EJoin::RefusedForeignFilter);
    TestTrue(TEXT("... its group is untouched"), Physics.Get_Group(Other).GetGroupFilter() == Foreign.GetPtr());
    TestFalse(TEXT("... it did not join"), Filter->Get_IsJoined(Other));
    TestFalse(TEXT("... and no pair was added"), Filter->Get_IsPairDisabled(A, Other));
    TestTrue(TEXT("... the refused body's partner kept its one pair"), Filter->Get_IsPairDisabled(A, B));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif

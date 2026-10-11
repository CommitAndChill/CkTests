#include "CkRotateTowardsGym_Runtime.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/Net/CkNet_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkRotateTowards/CkRotateTowards_Fragment.h"
#include "CkTween/CkTween_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkEntityTag/CkEntityTag_Utils.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/UnrealType.h"

// CkTests-private, non-reflected provenance. Only the one-shot claim below adds
// it to a fresh empty simulation owner; no AS setter can adopt an old cohort.
namespace ck
{
    struct FFragment_RotateTowardsGym_SourceScope
    {
        TWeakObjectPtr<AGameStateBase> Source;
        int32 Generation = 0;
    };
}

namespace ck_rotate_towards_activation
{
    struct FSourceFrame
    {
        UWorld* World = nullptr;
        AGameStateBase* Source = nullptr;
        FCk_Handle WorldRoot;
        FCk_Handle WorldBridge;
        FCkRotateTowardsGym_MotionAdmission* Stored = nullptr;
        int32 Generation = 0;
    };

    auto Live(const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                H, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto HasTweenRole(const FCk_Handle& H) -> bool
    {
        return H.Has<ck::FFragment_Tween_Params>() ||
            H.Has<ck::FFragment_Tween>() ||
            H.Has<ck::FFragment_Tween_Requests>() ||
            H.Has<ck::FFragment_Tween_Chain>() ||
            H.Has<ck::FFragment_Tween_SplineFollow>() ||
            H.Has<ck::FFragment_Tween_CurveDrive>() ||
            H.Has<ck::FFragment_Tween_EasingCurve>() ||
            H.Has<ck::FTag_Tween_Playing>() ||
            H.Has<ck::FTag_Tween_Paused>() ||
            H.Has<ck::FTag_Tween_Completed>() ||
            H.Has<ck::FTag_Tween_InYoyoDelay>();
    }

    auto HasRotateRole(const FCk_Handle& H) -> bool
    {
        return H.Has<ck::FFragment_RotateTowards_Tunables>() ||
            H.Has<ck::FFragment_RotateTowards>() ||
            H.Has<ck::FFragment_RotateTowards_SolveResult>() ||
            H.Has<ck::FFragment_RotateTowards_RangeClamp>() ||
            H.Has<ck::FFragment_RotateTowards_Requests>() ||
            H.Has<ck::FTag_RotateTowards_Disabled>() ||
            H.Has<ck::FTag_RotateTowards_HasTarget>() ||
            H.Has<ck::FTag_RotateTowards_AtTarget>();
    }

    auto PlainOwner(const FCk_Handle& Owner) -> bool
    {
        return !Owner.Has<ck::FFragment_Transform>() &&
            !Owner.Has<ck::FFragment_SceneNode>() &&
            !Owner.Has<ck::FFragment_UnrealComponent>() &&
            !HasTweenRole(Owner) && !HasRotateRole(Owner);
    }

    auto ConstructedSimulationOwner(const FSourceFrame& F,
                                     const FCk_Handle& Owner) -> bool
    {
        if (!Live(Owner) || Owner == F.WorldRoot || Owner == F.WorldBridge ||
            Owner.Get_RegistryView().Get_RegistryHandle() !=
                F.WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            !Owner.Has<ck::FFragment_LifetimeOwner>() ||
            Owner.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != F.WorldBridge ||
            !IsValid(F.Source) ||
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor_Recursive(Owner) != F.Source ||
            !PlainOwner(Owner) || !Owner.Has<ck::FFragment_EntityScript>() ||
            UCk_Utils_Net_UE::Get_EntityReplication(Owner) != ECk_Replication::Replicates ||
            !Owner.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
        { return false; }
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Owner);
        auto* Driver = Owner.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        if (!IsValid(Script) || Script->GetClass()->GetPathName() !=
                TEXT("/Script/Angelscript.Ck_EntityScript_RotateTowardsGym_Owner") ||
            !IsValid(Driver) || Driver->Get_AssociatedEntity() != Owner)
        { return false; }
        const auto* Generation = FindFProperty<FIntProperty>(
            Script->GetClass(), TEXT("Generation"));
        return Generation && Generation->GetPropertyValue_InContainer(Script) == F.Generation;
    }

    auto HasRetainedSpawnCarrier(const FCk_Handle& Target,
                                 const FCk_Handle& Carrier) -> bool
    {
        if (!Live(Target) || Carrier == FCk_Handle{} ||
            !Target.Has<ck::FFragment_LifetimeDependents>())
        { return false; }
        int32 Occurrences = 0;
        for (const auto& Child :
            Target.Get<ck::FFragment_LifetimeDependents>().Get_Entities())
        { Occurrences += Child == Carrier ? 1 : 0; }
        return Occurrences == 1;
    }

    auto CheckKnownRoles(const FCkRotateTowardsGym_MotionAdmission& A,
                         const TArray<FCk_Handle>& All) -> bool
    {
        if (!PlainOwner(A.Owner) || All.Num() != 41) { return false; }
        // BuildGraph fixes indices 0..2 roots, 3..7 motion, 8..14 turrets,
        // 15..18 tweens, 19..29 cube nodes, and 30..40 components.
        for (int32 I = 0; I < All.Num(); ++I)
        {
            const auto& H = All[I];
            if (I >= 8 && I < 15)
            {
                if (HasTweenRole(H) || !HasRotateRole(H) ||
                    H.Has<ck::FFragment_SceneNode>() ||
                    H.Has<ck::FFragment_UnrealComponent>()) { return false; }
            }
            else if (I >= 15 && I < 19)
            {
                if (HasRotateRole(H) || !HasTweenRole(H) ||
                    H.Has<ck::FFragment_Transform>() ||
                    H.Has<ck::FFragment_SceneNode>() ||
                    H.Has<ck::FFragment_UnrealComponent>()) { return false; }
            }
            else if (HasTweenRole(H) || HasRotateRole(H)) { return false; }
        }
        return true;
    }

    auto ReadSource(AGameStateBase* Source, FSourceFrame& Out,
                    bool RequireCurrentAndNotDestroying = true) -> bool
    {
        Out = {};
        if (!IsValid(Source) || Source->HasAnyFlags(
                RF_ClassDefaultObject | RF_ArchetypeObject | RF_BeginDestroyed | RF_FinishDestroyed) ||
            (RequireCurrentAndNotDestroying && Source->IsActorBeingDestroyed()) ||
            !Source->HasAuthority() || !IsValid(Source->GetWorld()) ||
            (RequireCurrentAndNotDestroying && Source->GetWorld()->GetGameState() != Source) ||
            Source->GetClass()->GetPathName() !=
                TEXT("/Script/Angelscript.Ck_RotateTowardsGym_GameState")) { return false; }
        auto* World = Source->GetWorld();
        if (World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            !World->HasBegunPlay() ||
            (World->GetNetMode() != NM_Standalone &&
                World->GetNetMode() != NM_ListenServer && World->GetNetMode() != NM_DedicatedServer))
        { return false; }
        const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
        const bool RootEligible = RequireCurrentAndNotDestroying ? Live(Root) :
            ck::IsValid(Root, ck::IsValid_Policy_IncludePendingKill{}) &&
            !Root.Has_Any<ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
        if (!RootEligible || !Root.Has<TWeakObjectPtr<UWorld>>() ||
            Root.Get<TWeakObjectPtr<UWorld>>().Get() != World) { return false; }
        const UClass* Class = Source->GetClass();
        const auto* Admission = FindFProperty<FStructProperty>(Class, TEXT("MotionAdmission"));
        const auto* Generation = FindFProperty<FIntProperty>(Class, TEXT("MotionGeneration"));
        const auto* Bridge = FindFProperty<FStructProperty>(Class, TEXT("WorldBridge"));
        if (!Admission || Admission->Struct != FCkRotateTowardsGym_MotionAdmission::StaticStruct() ||
            !Generation || !Bridge || Bridge->Struct != FCk_Handle::StaticStruct())
        { return false; }
        Out.World = World;
        Out.Source = Source;
        Out.WorldRoot = Root;
        Out.WorldBridge = *Bridge->ContainerPtrToValuePtr<FCk_Handle>(Source);
        Out.Stored = Admission->ContainerPtrToValuePtr<FCkRotateTowardsGym_MotionAdmission>(Source);
        Out.Generation = Generation->GetPropertyValue_InContainer(Source);
        if (!Out.Stored || Out.Generation <= 0 || Out.Generation >= 2147483646 ||
            !Live(Out.WorldBridge) || Out.WorldBridge == Root ||
            Out.WorldBridge.Get_RegistryView().Get_RegistryHandle() !=
                Root.Get_RegistryView().Get_RegistryHandle() ||
            !Out.WorldBridge.Has<ck::FFragment_LifetimeOwner>() ||
            Out.WorldBridge.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Root ||
            UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Source) != Out.WorldBridge ||
            UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Out.WorldBridge) != Source ||
            !Out.WorldBridge.Has<ck::FFragment_EntityScript>() ||
            UCk_Utils_Net_UE::Get_EntityReplication(Out.WorldBridge) !=
                ECk_Replication::Replicates ||
            !Out.WorldBridge.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
        { return false; }
        auto* BridgeScript = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Out.WorldBridge);
        auto* BridgeDriver = Out.WorldBridge.Get<
            TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
        return IsValid(BridgeScript) && BridgeScript->GetClass()->GetPathName() ==
                TEXT("/Script/Angelscript.Ck_EntityScript_RotateTowardsGym_WorldRoot") &&
            IsValid(BridgeDriver) && BridgeDriver->Get_AssociatedEntity() == Out.WorldBridge &&
            BridgeDriver->GetOwningActor() == Source;
    }

    // Caller has first validated Source as a readable UObject and Owner as an
    // allocated, phase-safe entity of the same world. Weak identity comparison
    // includes the UObject serial; Get(true) also rejects stale recycled slots.
    auto HasSourceScope(AGameStateBase* Source, const FSourceFrame& F,
                        const FCk_Handle& Owner) -> bool
    {
        if (!Owner.Has<ck::FFragment_RotateTowardsGym_SourceScope>()) { return false; }
        const auto& Scope = Owner.Get<ck::FFragment_RotateTowardsGym_SourceScope,
            ck::IsValid_Policy_IncludePendingKill>();
        const TWeakObjectPtr<AGameStateBase> Incoming(Source);
        return Scope.Generation == F.Generation && !Scope.Source.IsExplicitlyNull() &&
            Scope.Source.HasSameIndexAndSerialNumber(Incoming) &&
            Scope.Source.Get(true) == Source;
    }

    auto Add(TArray<FCk_Handle>& All, TArray<int32>& Parents,
             const FCk_Handle& H, int32 Parent) -> void
    { All.Add(H); Parents.Add(Parent); }

    // Parent -1 denotes the independently validated simulation Owner.
    auto BuildGraph(const FCkRotateTowardsGym_MotionAdmission& A,
                    TArray<FCk_Handle>& All, TArray<int32>& Parents) -> bool
    {
        All.Reset(); Parents.Reset();
        if (A.Roots.Num() != 3 || A.Motion.Num() != 5 || A.Turrets.Num() != 7 ||
            A.Tweens.Num() != 4 || A.CubeNodes.Num() != 11 ||
            A.CubeComponents.Num() != 11 || A.ComponentAdded.Num() != 11) { return false; }
        for (int32 I = 0; I < 3; ++I) { Add(All, Parents, A.Roots[I], -1); }
        // Five motion identities: target0, target1, rest1, carrier2, targetNode2.
        constexpr int32 MotionParents[5] = {0, 1, 1, 2, 6};
        for (int32 I = 0; I < 5; ++I) { Add(All, Parents, A.Motion[I], MotionParents[I]); }
        constexpr int32 TurretParents[7] = {0, 0, 1, 2, 2, 2, 2};
        for (int32 I = 0; I < 7; ++I) { Add(All, Parents, A.Turrets[I], TurretParents[I]); }
        constexpr int32 TweenParents[4] = {3, 4, 6, 7};
        for (int32 I = 0; I < 4; ++I) { Add(All, Parents, A.Tweens[I], TweenParents[I]); }
        // Cube nodes are target0/turret0/turret1, target1/rest1/turret2,
        // targetNode2/turret3/turret4/turret5/turret6.
        for (int32 I = 0; I < 11; ++I) { Add(All, Parents, A.CubeNodes[I], -1); }
        for (int32 I = 0; I < 11; ++I) { Add(All, Parents, A.CubeComponents[I], 19 + I); }
        return All.Num() == 41 && Parents.Num() == 41;
    }

    auto CheckGraph(const FSourceFrame& F, const FCkRotateTowardsGym_MotionAdmission& A,
                    const TArray<FCk_Handle>& All, const TArray<int32>& Parents) -> bool
    {
        if (!ConstructedSimulationOwner(F, A.Owner))
        { return false; }
        TSet<FCk_Handle> Unique;
        const auto Registry = F.WorldRoot.Get_RegistryView().Get_RegistryHandle();
        for (int32 I = 0; I < All.Num(); ++I)
        {
            const auto H = All[I];
            const auto ExpectedParent = Parents[I] < 0 ? A.Owner : All[Parents[I]];
            if (!Live(H) || H.Get_RegistryView().Get_RegistryHandle() != Registry ||
                H == A.Owner || H == F.WorldRoot || Unique.Contains(H) ||
                !H.Has<ck::FFragment_LifetimeOwner>() ||
                H.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != ExpectedParent)
            { return false; }
            Unique.Add(H);
        }
        if (Unique.Num() != 41) { return false; }
        // Use raw lifetime arrays: filtered child helpers can hide transferred/foreign
        // still-allocated references that would be in an actual owner read-set.
        for (int32 ParentIndex = -1; ParentIndex < All.Num(); ++ParentIndex)
        {
            const auto Parent = ParentIndex < 0 ? A.Owner : All[ParentIndex];
            TSet<FCk_Handle> Expected, Actual;
            for (int32 I = 0; I < All.Num(); ++I)
            { if (Parents[I] == ParentIndex) { Expected.Add(All[I]); } }
            if (Parent.Has<ck::FFragment_LifetimeDependents>())
            {
                for (const auto& Child : Parent.Get<ck::FFragment_LifetimeDependents>().Get_Entities())
                {
                    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child)) { continue; }
                    if (Child.Get_RegistryView().Get_RegistryHandle() != Registry ||
                        Actual.Contains(Child) || !Expected.Contains(Child) ||
                        !Child.Has<ck::FFragment_LifetimeOwner>() ||
                        Child.Get<ck::FFragment_LifetimeOwner, ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != Parent)
                    { return false; }
                    Actual.Add(Child);
                }
            }
            if (Actual.Num() != Expected.Num()) { return false; }
            TArray<FCk_Handle> Direct;
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                    Parent, F.WorldRoot, Direct) || Direct.Num() != Expected.Num())
            { return false; }
            for (const auto& Child : Direct)
            { if (!Expected.Contains(Child)) { return false; } }
        }
        // The raw destroy read-set alone does not find an allocated entity whose
        // current Owner names this cohort but which was omitted from its parent's
        // dependent array. Scan the registry's owner fragments as a second proof.
        TMap<FCk_Handle, FCk_Handle> ExpectedOwners;
        for (int32 I = 0; I < All.Num(); ++I)
        { ExpectedOwners.Add(All[I], Parents[I] < 0 ? A.Owner : All[Parents[I]]); }
        TSet<FCk_Handle> ActualOwned;
        bool OwnersExact = true;
        const auto RegistryView = F.WorldRoot.Get_RegistryView();
        RegistryView.View<ck::FFragment_LifetimeOwner>().ForEach(
            [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Ownership) -> void
            {
                const auto Child = FCk_Handle{Entity, Registry};
                if (!ck::IsValid(Child, ck::IsValid_Policy_IncludePendingKill{})) { return; }
                const auto CurrentOwner = Ownership.Get_Entity();
                if (CurrentOwner != A.Owner && !Unique.Contains(CurrentOwner)) { return; }
                const auto* ExpectedOwner = ExpectedOwners.Find(Child);
                if (!ExpectedOwner || *ExpectedOwner != CurrentOwner || ActualOwned.Contains(Child))
                { OwnersExact = false; return; }
                ActualOwned.Add(Child);
            });
        if (!OwnersExact || ActualOwned.Num() != All.Num()) { return false; }
        return true;
    }

    auto SameTransform(const FCk_Handle& H, const FTransform& Expected) -> bool
    {
        return H.Has<ck::FFragment_Transform>() &&
            H.Get<ck::FFragment_Transform>().Get_Transform().Equals(Expected, 0.0);
    }

    auto CheckInitialTransforms(const FCkRotateTowardsGym_MotionAdmission& A) -> bool
    {
        const FVector Origins[3] = {FVector(1400, 0, 150), FVector(5000, 0, 150),
            FVector(1400, 3000, 150)};
        for (int32 I = 0; I < 3; ++I)
        { if (!SameTransform(A.Roots[I], FTransform(Origins[I]))) { return false; } }
        if (!SameTransform(A.Motion[0], FTransform(Origins[0] + FVector(400, -300, 0))) ||
            !SameTransform(A.Motion[1], FTransform(Origins[1] + FRotator(0, 70, 0).Vector() * 400)) ||
            !SameTransform(A.Motion[2], FTransform(Origins[1] + FVector(400, 0, 0))) ||
            !SameTransform(A.Motion[3], FTransform(Origins[2] + FVector(400, -300, 0))) ||
            !SameTransform(A.Motion[4], FTransform(Origins[2] + FVector(400, -300, -150))))
        { return false; }
        const int32 TurretStation[7] = {0, 0, 1, 2, 2, 2, 2};
        const int32 TurretY[7] = {-75, 75, 0, -225, -75, 75, 225};
        for (int32 I = 0; I < 7; ++I)
        {
            if (!SameTransform(A.Turrets[I],
                    FTransform(Origins[TurretStation[I]] + FVector(0, TurretY[I], 0))))
            { return false; }
        }
        const auto TargetNode = A.Motion[4];
        if (!TargetNode.Has<ck::FFragment_SceneNode>() ||
            !ck::USceneNodeParent_Utils::Has(TargetNode) ||
            UCk_Utils_SceneNode_UE::Get_Parent(UCk_Utils_SceneNode_UE::Cast(TargetNode)) != A.Motion[3] ||
            !UCk_Utils_SceneNode_UE::Get_Offset(
                UCk_Utils_SceneNode_UE::Cast(TargetNode)).Equals(FTransform(FVector(0, 0, -150)), 0.0))
        { return false; }
        return true;
    }

    auto PoseDeltaSummary(const FTransform& Actual,
                          const FTransform& Expected) -> FString
    {
        if (!Actual.IsValid() || Actual.ContainsNaN() ||
            !Expected.IsValid() || Expected.ContainsNaN())
        { return TEXT("pose-invalid"); }
        const auto D = Actual.GetTranslation() - Expected.GetTranslation();
        const auto S = Actual.GetScale3D() - Expected.GetScale3D();
        const double TranslationMax = FMath::Max(FMath::Max(
            FMath::Abs(D.X), FMath::Abs(D.Y)), FMath::Abs(D.Z));
        const double ScaleMax = FMath::Max(FMath::Max(
            FMath::Abs(S.X), FMath::Abs(S.Y)), FMath::Abs(S.Z));
        const double AngularRadians = Actual.GetRotation().AngularDistance(
            Expected.GetRotation());
        return FString::Printf(TEXT("translationMax=%.17g angularRadians=%.17g scaleMax=%.17g"),
            TranslationMax, AngularRadians, ScaleMax);
    }

    auto HasPendingScenePose(const FCk_Handle& H) -> bool
    {
        return H.Has<ck::FTag_SceneNode_PropagationQueued>() ||
            (H.Has<ck::FFragment_SceneNode_Requests>() &&
                !H.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().IsEmpty());
    }

    auto CheckCubeComponents(const FSourceFrame& F,
                             const FCkRotateTowardsGym_MotionAdmission& A,
                             const TArray<FCk_Handle>& All,
                             const TArray<int32>& Parents, int32* OutBadSlot = nullptr,
                             FString* OutDetail = nullptr,
                             bool UseProductionPoseContract = false,
                             bool* OutPosePending = nullptr) -> bool
    {
        bool PosePending = false;
        for (int32 I = 0; I < 11; ++I)
        {
            const auto Node = A.CubeNodes[I];
            const auto ComponentHandle = A.CubeComponents[I];
            if (Node != UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
                    F.World, F.Generation, I))
            { if (OutBadSlot) { *OutBadSlot = I; }
                if (OutDetail) { *OutDetail = TEXT("replicated-cube-identity"); }
                return false; }
            constexpr int32 SceneParents[11] = {3, 8, 9, 4, 5, 10, 7, 11, 12, 13, 14};
            const auto ExpectedParent = All[SceneParents[I]];
            const bool TurretCube = I == 1 || I == 2 || I == 5 || I >= 7;
            const FVector Offset = TurretCube ? FVector(40, 0, 0) : FVector::ZeroVector;
            const FVector Scale = TurretCube ? FVector(0.9, 0.3, 0.3) :
                I == 4 ? FVector(0.1, 0.1, 0.1) : FVector(0.25, 0.25, 0.25);
            const FTransform Local(FRotator::ZeroRotator, Offset, Scale);
            if (A.ComponentAdded[I] != 1 || !Node.Has<ck::FFragment_SceneNode>() ||
                !Node.Has<ck::FFragment_Transform>() ||
                !ExpectedParent.Has<ck::FFragment_Transform>() ||
                !ck::USceneNodeParent_Utils::Has(Node) ||
                UCk_Utils_SceneNode_UE::Get_Parent(Node) != ExpectedParent ||
                !UCk_Utils_SceneNode_UE::Get_Offset(Node).Equals(Local, 0.0) ||
                !ComponentHandle.Has<ck::FFragment_UnrealComponent>() ||
                !ComponentHandle.Has<ck::FFragment_UnrealComponent_Params>() ||
                !ComponentHandle.Has<ck::FTag_UnrealComponent_IsScene>() ||
                ComponentHandle.Has<ck::FTag_UnrealComponent_NeedsSetup>() ||
                ComponentHandle.Has<ck::FTag_UnrealComponent_TransformPushDisabled>())
            { if (OutBadSlot) { *OutBadSlot = I; }
                if (OutDetail)
                {
                    *OutDetail = TEXT("node-schema-parent-offset-or-pose");
                    if (Node.Has<ck::FFragment_Transform>() &&
                        ExpectedParent.Has<ck::FFragment_Transform>())
                    {
                        const auto& NodePose = Node.Get<ck::FFragment_Transform>().Get_Transform();
                        const auto ExpectedPose = Local *
                            ExpectedParent.Get<ck::FFragment_Transform>().Get_Transform();
                        *OutDetail += FString::Printf(TEXT(" node exact=%d default=%d %s"),
                            NodePose.Equals(ExpectedPose, 0.0), NodePose.Equals(ExpectedPose),
                            *PoseDeltaSummary(NodePose, ExpectedPose));
                    }
                }
                return false; }
            const auto& ParentPose = ExpectedParent.Get<ck::FFragment_Transform>().Get_Transform();
            const auto& NodePose = Node.Get<ck::FFragment_Transform>().Get_Transform();
            // Active value-shape checks apply even when the current pose happens
            // to match exactly; a queued production request is still pending.
            if (UseProductionPoseContract &&
                (ParentPose.GetScale3D() != FVector::OneVector ||
                 NodePose.GetScale3D() != Scale || !NodePose.IsValid() || NodePose.ContainsNaN()))
            { if (OutBadSlot) { *OutBadSlot = I; }
                if (OutDetail) { *OutDetail = TEXT("node-pose-invalid-or-scale"); }
                return false; }
            const bool NodePending = UseProductionPoseContract && HasPendingScenePose(Node);
            PosePending = PosePending || NodePending;
            if (!NodePose.Equals(Local * ParentPose, 0.0))
            {
                if (!UseProductionPoseContract)
                { if (OutBadSlot) { *OutBadSlot = I; }
                    if (OutDetail) { *OutDetail = TEXT("dormant-node-pose-not-exact"); }
                    return false; }
                // The fixed local cube rotation is identity with positive scale.
                // Production parent publication uses FTransform::Equals default
                // tolerance, so compare in parent space instead of child space.
                if (!NodePending && !ParentPose.Equals(Local.Inverse() * NodePose))
                { if (OutBadSlot) { *OutBadSlot = I; }
                    if (OutDetail)
                    { *OutDetail = FString::Printf(TEXT("node-parent-basis-outside-production %s"),
                          *PoseDeltaSummary(NodePose, Local * ParentPose)); }
                    return false; }
            }
            const auto& Params = ComponentHandle.Get<ck::FFragment_UnrealComponent_Params>();
            const auto& Fragment = ComponentHandle.Get<ck::FFragment_UnrealComponent>();
            if (Params.Get_DebugName() != FName(TEXT("RotateTowardsGym_Cube")) ||
                Params.Get_ComponentClass().Get() != UStaticMeshComponent::StaticClass() ||
                Fragment.Get_OwningEntity() != Node) { if (OutBadSlot) { *OutBadSlot = I; }
                if (OutDetail) { *OutDetail = TEXT("params-class-or-owning-entity"); }
                return false; }
            auto* Mesh = Cast<UStaticMeshComponent>(Fragment.Get_Component().Get());
            if (Mesh != UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(
                    F.World, F.Generation, I))
            { if (OutBadSlot) { *OutBadSlot = I; }
                if (OutDetail) { *OutDetail = TEXT("replicated-cube-component-identity"); }
                return false; }
            if (!IsValid(Mesh) || Mesh->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
                Mesh->GetClass() != UStaticMeshComponent::StaticClass() ||
                Mesh->GetWorld() != F.World || !Mesh->IsRegistered() ||
                UCk_Utils_UnrealComponent_UE::TryGet_OwningHandle_FromComponent(Mesh) != ComponentHandle ||
                Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision ||
                !Mesh->IsVisible() || Mesh->bHiddenInGame ||
                // Active mesh pushes use FTransform::Equals's default tolerance;
                // fresh dormant admission still requires exact placement.
                !(UseProductionPoseContract
                    ? Mesh->GetComponentTransform().Equals(
                        Node.Get<ck::FFragment_Transform>().Get_Transform())
                    : Mesh->GetComponentTransform().Equals(
                        Node.Get<ck::FFragment_Transform>().Get_Transform(), 0.0)))
            { if (OutBadSlot) { *OutBadSlot = I; }
                if (OutDetail)
                {
                    *OutDetail = TEXT("component-bridge-style-or-pose");
                    if (IsValid(Mesh) && !Mesh->HasAnyFlags(
                            RF_BeginDestroyed | RF_FinishDestroyed))
                    {
                        const auto& BridgeNodePose = Node.Get<ck::FFragment_Transform>().Get_Transform();
                        const auto MeshPose = Mesh->GetComponentTransform();
                        const bool StyleOk = Mesh->GetClass() == UStaticMeshComponent::StaticClass() &&
                            Mesh->GetWorld() == F.World && Mesh->IsRegistered() &&
                            UCk_Utils_UnrealComponent_UE::TryGet_OwningHandle_FromComponent(Mesh) ==
                                ComponentHandle &&
                            Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                            Mesh->IsVisible() && !Mesh->bHiddenInGame;
                        *OutDetail += FString::Printf(TEXT(" styleOk=%d exact=%d default=%d %s"),
                            StyleOk, MeshPose.Equals(BridgeNodePose, 0.0), MeshPose.Equals(BridgeNodePose),
                            *PoseDeltaSummary(MeshPose, BridgeNodePose));
                    }
                }
                return false; }
            auto* Asset = Mesh->GetStaticMesh().Get();
            if (!IsValid(Asset) || Asset->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
                Asset->GetPathName() != TEXT("/Engine/BasicShapes/Cube.Cube")) { if (OutBadSlot) { *OutBadSlot = I; }
                if (OutDetail) { *OutDetail = TEXT("cube-asset"); }
                return false; }
        }
        if (OutPosePending) { *OutPosePending = PosePending; }
        return true;
    }

    auto CheckTweens(const FCkRotateTowardsGym_MotionAdmission& A) -> bool
    {
        const FVector Origins[3] = {FVector(1400, 0, 150), FVector(5000, 0, 150),
            FVector(1400, 3000, 150)};
        const FVector Starts[4] = {
            Origins[0] + FVector(400, -300, 0),
            Origins[1] + FRotator(0, 70, 0).Vector() * 400,
            Origins[2] + FVector(400, -300, 0),
            FVector(0, 0, -150)};
        const FVector Ends[4] = {
            Origins[0] + FVector(400, 300, 0),
            Origins[1] + FRotator(0, -70, 0).Vector() * 400,
            Origins[2] + FVector(400, 300, 0),
            FVector(0, 0, 150)};
        const float Durations[4] = {3.0f, 4.0f, 3.0f, 2.0f};
        for (int32 I = 0; I < 4; ++I)
        {
            const auto H = A.Tweens[I];
            if (!H.Has<ck::FFragment_Tween_Params>() || !H.Has<ck::FFragment_Tween>() ||
                !H.Has<ck::FTag_Tween_Paused>() || H.Has<ck::FTag_Tween_Playing>() ||
                H.Has<ck::FTag_Tween_Completed>() || H.Has<ck::FTag_Tween_InYoyoDelay>() ||
                H.Has<ck::FFragment_Tween_Chain>() || H.Has<ck::FFragment_Tween_SplineFollow>() ||
                H.Has<ck::FFragment_Tween_CurveDrive>() || H.Has<ck::FFragment_Tween_EasingCurve>() ||
                (H.Has<ck::FFragment_Tween_Requests>() &&
                    !H.Get<ck::FFragment_Tween_Requests>().Get_Requests().IsEmpty()))
            { return false; }
            const auto& Spec = H.Get<ck::FFragment_Tween_Params>();
            const auto& State = H.Get<ck::FFragment_Tween>();
            if (Spec.Get_Duration() != Durations[I] ||
                Spec.Get_Easing() != ECk_TweenEasing::InOutSine ||
                Spec.Get_LoopType() != ECk_TweenLoopType::Yoyo ||
                Spec.Get_LoopCount() != -1 || Spec.Get_YoyoDelay() != 0.0f ||
                Spec.Get_Target() != (I == 3 ? ECk_TweenTarget::SceneNodeOffset_Location :
                    ECk_TweenTarget::Transform_Location) ||
                Spec.Get_CompletionBehavior() != ECk_TweenCompletionBehavior::DoNothing ||
                !Spec.Get_StartValue().IsVector() || !Spec.Get_EndValue().IsVector() ||
                Spec.Get_StartValue().GetAsVector() != Starts[I] ||
                Spec.Get_EndValue().GetAsVector() != Ends[I] ||
                State.Get_State() != ECk_TweenState::Paused ||
                State.Get_CurrentTime() != 0.0f || State.Get_YoyoDelayTimer() != 0.0f ||
                State.Get_CurrentLoop() != 0 || State.Get_IsReversed() ||
                State.Get_TimeMultiplier() != 1.0f ||
                !State.Get_CurrentValue().IsVector() ||
                State.Get_CurrentValue().GetAsVector() != Starts[I])
            { return false; }
        }
        return true;
    }

    auto CheckAxis(const FCk_RotateTowards_Axis& Axis, bool Locked, float Rate) -> bool
    {
        return Axis.Get_Mode() == (Locked ? ECk_RotateTowards_AxisMode::Locked :
            ECk_RotateTowards_AxisMode::Free) &&
            Axis.Get_TurnRateDegPerSec() == Rate;
    }

    auto CheckTurrets(const FCkRotateTowardsGym_MotionAdmission& A) -> bool
    {
        for (int32 I = 0; I < 7; ++I)
        {
            const auto H = A.Turrets[I];
            if (!H.Has<ck::FFragment_RotateTowards_Tunables>() ||
                !H.Has<ck::FFragment_RotateTowards>() ||
                !H.Has<ck::FFragment_RotateTowards_SolveResult>() ||
                !H.Has<ck::FTag_RotateTowards_Disabled>() ||
                !H.Has<ck::FTag_RotateTowards_HasTarget>() ||
                H.Has<ck::FTag_RotateTowards_AtTarget>() ||
                (H.Has<ck::FFragment_RotateTowards_Requests>() &&
                    !H.Get<ck::FFragment_RotateTowards_Requests>().Get_Requests().IsEmpty()))
            { return false; }
            const auto Target = I < 2 ? A.Motion[0] : I == 2 ? A.Motion[1] : A.Motion[4];
            const auto& Tunables = H.Get<ck::FFragment_RotateTowards_Tunables>();
            const auto& Rotate = H.Get<ck::FFragment_RotateTowards>();
            const auto& Solve = H.Get<ck::FFragment_RotateTowards_SolveResult>();
            const float Rate = I < 2 ? 60.0f : 90.0f;
            if (Rotate.Get_Target() != Target ||
                Tunables.Get_Mode() != (I == 1 ? ECk_RotateTowards_Mode::Instant :
                    ECk_RotateTowards_Mode::RateLimited) ||
                !CheckAxis(Tunables.Get_Yaw(), I == 4, Rate) ||
                !CheckAxis(Tunables.Get_Pitch(), I == 5, Rate) ||
                !CheckAxis(Tunables.Get_Roll(), false, Rate) ||
                Tunables.Get_ReachedToleranceDeg() != 1.0f ||
                Solve.Get_DesiredRotation() != FRotator::ZeroRotator ||
                Solve.Get_RemainingDelta() != FRotator::ZeroRotator ||
                Solve.Get_LastAppliedRotation() != FRotator::ZeroRotator)
            { return false; }
            if (I == 2)
            {
                if (!H.Has<ck::FFragment_RotateTowards_RangeClamp>()) { return false; }
                const auto& Clamp = H.Get<ck::FFragment_RotateTowards_RangeClamp>();
                if (Clamp.Get_RestReferencePoint() != A.Motion[2] ||
                    Clamp.Get_Pitch().Get_Enabled() != ECk_EnableDisable::Disable ||
                    Clamp.Get_Roll().Get_Enabled() != ECk_EnableDisable::Disable ||
                    Clamp.Get_Yaw().Get_Enabled() != ECk_EnableDisable::Enable ||
                    Clamp.Get_Yaw().Get_RangeDeg().Get_Min(ECk_Inclusiveness::Inclusive) != -45.0 ||
                    Clamp.Get_Yaw().Get_RangeDeg().Get_Max(ECk_Inclusiveness::Inclusive) != 45.0)
                { return false; }
            }
            else if (H.Has<ck::FFragment_RotateTowards_RangeClamp>()) { return false; }
        }
        return true;
    }

    auto Validate(AGameStateBase* Source, const FCkRotateTowardsGym_MotionAdmission& Candidate,
                  FSourceFrame& Out) -> bool
    {
        if (!ReadSource(Source, Out) || !Out.Stored || Candidate.Activated || Candidate.Closed ||
            Candidate.Generation != Out.Generation ||
            !FCkRotateTowardsGym_MotionAdmission::StaticStruct()->CompareScriptStruct(
                &Candidate, Out.Stored, PPF_None) ||
            !Live(Candidate.Owner) || !HasSourceScope(Source, Out, Candidate.Owner))
        { return false; }
        TArray<FCk_Handle> All;
        TArray<int32> Parents;
        if (!BuildGraph(Candidate, All, Parents) ||
            !CheckGraph(Out, Candidate, All, Parents) ||
            !CheckKnownRoles(Candidate, All) ||
            !CheckInitialTransforms(Candidate) ||
            !CheckCubeComponents(Out, Candidate, All, Parents) ||
            !CheckTweens(Candidate) || !CheckTurrets(Candidate))
        { return false; }
        return true;
    }

    auto FiniteTransformValue(const FTransform& T) -> bool
    {
        const auto P = T.GetTranslation();
        const auto S = T.GetScale3D();
        const auto Q = T.GetRotation();
        return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y) && FMath::IsFinite(P.Z) &&
            FMath::IsFinite(S.X) && FMath::IsFinite(S.Y) && FMath::IsFinite(S.Z) &&
            FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) &&
            FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W) &&
            FMath::IsFinite(Q.SizeSquared()) && Q.IsNormalized() && T.IsValid();
    }

    auto FiniteTransform(const FCk_Handle& H) -> bool
    {
        return H.Has<ck::FFragment_Transform>() &&
            FiniteTransformValue(H.Get<ck::FFragment_Transform>().Get_Transform());
    }

    auto FiniteRotation(const FRotator& R) -> bool
    {
        return FMath::IsFinite(R.Pitch) && FMath::IsFinite(R.Yaw) &&
            FMath::IsFinite(R.Roll);
    }

    auto FiniteVector(const FVector& V) -> bool
    {
        return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) &&
            FMath::IsFinite(V.Z);
    }

    auto CheckActiveTransforms(const FCkRotateTowardsGym_MotionAdmission& A,
                               FString* OutDetail = nullptr,
                               bool* OutPosePending = nullptr) -> bool
    {
        const FVector Origins[3] = {FVector(1400, 0, 150), FVector(5000, 0, 150),
            FVector(1400, 3000, 150)};
        for (int32 I = 0; I < 3; ++I)
        {
            if (!FiniteTransform(A.Roots[I]) ||
                !SameTransform(A.Roots[I], FTransform(Origins[I])))
            {
                if (OutDetail) { *OutDetail = FString::Printf(TEXT("root=%d"), I); }
                return false;
            }
        }
        for (int32 I = 0; I < A.Motion.Num(); ++I)
        {
            if (!FiniteTransform(A.Motion[I]))
            {
                if (OutDetail) { *OutDetail = FString::Printf(TEXT("motion-finite=%d"), I); }
                return false;
            }
        }
        if (!SameTransform(A.Motion[2], FTransform(Origins[1] + FVector(400, 0, 0))))
        {
            if (OutDetail) { *OutDetail = TEXT("rest-point-pose"); }
            return false;
        }
        const int32 TurretStation[7] = {0, 0, 1, 2, 2, 2, 2};
        const int32 TurretY[7] = {-75, 75, 0, -225, -75, 75, 225};
        for (int32 I = 0; I < 7; ++I)
        {
            const auto H = FCk_Handle(A.Turrets[I]);
            if (!FiniteTransform(H))
            {
                if (OutDetail) { *OutDetail = FString::Printf(TEXT("turret-finite=%d"), I); }
                return false;
            }
            const auto& T = H.Get<ck::FFragment_Transform>().Get_Transform();
            if (T.GetTranslation() !=
                    Origins[TurretStation[I]] + FVector(0, TurretY[I], 0) ||
                T.GetScale3D() != FVector::OneVector)
            {
                if (OutDetail) { *OutDetail = FString::Printf(TEXT("turret-origin-or-scale=%d"), I); }
                return false;
            }
        }
        if (!SameTransform(A.Turrets[6],
                FTransform(Origins[2] + FVector(0, 225, 0))))
        {
            if (OutDetail) { *OutDetail = TEXT("disabled-turret-pose"); }
            return false;
        }
        for (int32 I = 0; I < A.CubeNodes.Num(); ++I)
        {
            if (!FiniteTransform(A.CubeNodes[I]))
            {
                if (OutDetail) { *OutDetail = FString::Printf(TEXT("cube-node-finite=%d"), I); }
                return false;
            }
        }
        const auto TargetNode = A.Motion[4];
        if (!TargetNode.Has<ck::FFragment_SceneNode>() ||
            !ck::USceneNodeParent_Utils::Has(TargetNode) ||
            UCk_Utils_SceneNode_UE::Get_Parent(
                UCk_Utils_SceneNode_UE::Cast(TargetNode)) != A.Motion[3])
        {
            if (OutDetail) { *OutDetail = TEXT("target-node-parent"); }
            return false;
        }
        const auto Offset = UCk_Utils_SceneNode_UE::Get_Offset(
            UCk_Utils_SceneNode_UE::Cast(TargetNode));
        if (!FiniteTransformValue(Offset))
        {
            if (OutDetail) { *OutDetail = TEXT("target-node-offset-finite"); }
            return false;
        }
        const auto& Actual = TargetNode.Get<ck::FFragment_Transform>().Get_Transform();
        const auto& ParentPose = A.Motion[3].Get<ck::FFragment_Transform>().Get_Transform();
        const auto Expected = Offset * ParentPose;
        // The authored target-node tween changes Z only, never rotation or scale.
        if (!Offset.GetRotation().Equals(FQuat::Identity, 0.0) ||
            Offset.GetScale3D() != FVector::OneVector ||
            Offset.GetTranslation().X != 0.0 || Offset.GetTranslation().Y != 0.0 ||
            Offset.GetTranslation().Z < -150.0 || Offset.GetTranslation().Z > 150.0 ||
            ParentPose.GetScale3D() != FVector::OneVector ||
            Actual.GetScale3D() != FVector::OneVector)
        { if (OutDetail) { *OutDetail = TEXT("target-node-relative-shape-or-scale"); }
            return false; }
        const bool TargetPending = HasPendingScenePose(TargetNode);
        if (TargetPending && OutPosePending) { *OutPosePending = true; }
        if (!Actual.Equals(Expected, 0.0))
        {
            if (!TargetPending && !ParentPose.Equals(Offset.Inverse() * Actual))
            { if (OutDetail)
                { *OutDetail = FString::Printf(TEXT("target-node-parent-basis-outside-production %s"),
                      *PoseDeltaSummary(Actual, Expected)); }
                return false; }
        }
        return true;
    }

    auto TurretRecipe(const FCkRotateTowardsGym_MotionAdmission& A,
                      const FCk_Handle& H, int32 Index) -> bool;
    auto TweenRecipe(const FCkRotateTowardsGym_MotionAdmission& A,
                     const FCk_Handle& H, int32 Index) -> bool;

    auto CheckActiveTweens(const FCkRotateTowardsGym_MotionAdmission& A,
                           int32* OutBadSlot = nullptr) -> bool
    {
        for (int32 I = 0; I < 4; ++I)
        {
            const auto H = FCk_Handle(A.Tweens[I]);
            if (!TweenRecipe(A, H, I) || !H.Has<ck::FTag_Tween_Playing>() ||
                H.Has<ck::FTag_Tween_Paused>() || H.Has<ck::FTag_Tween_Completed>() ||
                H.Has<ck::FTag_Tween_InYoyoDelay>() ||
                (H.Has<ck::FFragment_Tween_Requests>() &&
                    !H.Get<ck::FFragment_Tween_Requests>().Get_Requests().IsEmpty()))
            { if (OutBadSlot) { *OutBadSlot = I; } return false; }
            const auto& S = H.Get<ck::FFragment_Tween>();
            const float Duration = H.Get<ck::FFragment_Tween_Params>().Get_Duration();
            if (S.Get_State() != ECk_TweenState::Playing ||
                !FMath::IsFinite(S.Get_CurrentTime()) ||
                S.Get_CurrentTime() < 0.0f || S.Get_CurrentTime() > Duration ||
                S.Get_YoyoDelayTimer() != 0.0f ||
                S.Get_TimeMultiplier() != 1.0f || S.Get_CurrentLoop() < 0 ||
                !S.Get_CurrentValue().IsVector() ||
                !FiniteVector(S.Get_CurrentValue().GetAsVector())) { if (OutBadSlot) { *OutBadSlot = I; } return false; }
        }
        return true;
    }

    auto CheckActiveTurrets(const FCkRotateTowardsGym_MotionAdmission& A,
                           int32* OutBadSlot = nullptr) -> bool
    {
        for (int32 I = 0; I < 7; ++I)
        {
            const auto H = FCk_Handle(A.Turrets[I]);
            if (!TurretRecipe(A, H, I) ||
                !H.Has<ck::FFragment_RotateTowards_SolveResult>() ||
                !H.Has<ck::FTag_RotateTowards_HasTarget>() ||
                H.Has<ck::FTag_RotateTowards_Disabled>() != (I == 6) ||
                (H.Has<ck::FFragment_RotateTowards_Requests>() &&
                    !H.Get<ck::FFragment_RotateTowards_Requests>().Get_Requests().IsEmpty()))
            { if (OutBadSlot) { *OutBadSlot = I; } return false; }
            const auto& Solve = H.Get<ck::FFragment_RotateTowards_SolveResult>();
            if (!FiniteRotation(Solve.Get_DesiredRotation()) ||
                !FiniteRotation(Solve.Get_RemainingDelta()) ||
                !FiniteRotation(Solve.Get_LastAppliedRotation())) { if (OutBadSlot) { *OutBadSlot = I; } return false; }
        }
        return true;
    }
}

FCk_Handle UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCube(
    UWorld* World, int32 Generation, int32 Slot)
{
    if (!IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        Generation <= 0 || Slot < 0 || Slot >= 11) { return {}; }
    const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!ck_rotate_towards_activation::Live(Root) ||
        !Root.Has<TWeakObjectPtr<UWorld>>() ||
        Root.Get<TWeakObjectPtr<UWorld>>().Get() != World) { return {}; }
    const FName Tag(*FString::Printf(TEXT("RotateTowardsGym_G%d_Cube%d"),
        Generation, Slot));
    const auto Found = UCk_Utils_EntityTag_UE::ForEach_Entity(Root, Tag);
    if (Found.Num() != 1) { return {}; }
    const auto H = Found[0];
    if (!ck_rotate_towards_activation::Live(H) ||
        H.Get_RegistryView().Get_RegistryHandle() !=
            Root.Get_RegistryView().Get_RegistryHandle() ||
        !H.Has<ck::FFragment_EntityScript>() ||
        !H.Has<ck::FFragment_Transform>() ||
        UCk_Utils_Net_UE::Get_EntityReplication(H) != ECk_Replication::Replicates ||
        !H.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>() ||
        (World->GetNetMode() == NM_Client &&
            (H.Has<ck::FFragment_SceneNode>() ||
             ck_rotate_towards_activation::HasTweenRole(H) ||
             ck_rotate_towards_activation::HasRotateRole(H)))) { return {}; }
    auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H);
    auto* Driver = H.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
    if (!IsValid(Script) || Script->GetClass()->GetPathName() !=
            TEXT("/Script/Angelscript.Ck_EntityScript_RotateTowardsGym_Cube") ||
        !IsValid(Driver) || Driver->Get_AssociatedEntity() != H) { return {}; }
    const auto* G = FindFProperty<FIntProperty>(Script->GetClass(), TEXT("Generation"));
    const auto* S = FindFProperty<FIntProperty>(Script->GetClass(), TEXT("Slot"));
    if (!G || !S || G->GetPropertyValue_InContainer(Script) != Generation ||
        S->GetPropertyValue_InContainer(Script) != Slot ||
        !H.Has<ck::FFragment_LifetimeOwner>()) { return {}; }
    auto* State = World->GetGameState();
    if (!IsValid(State) || State->GetClass()->GetPathName() !=
            TEXT("/Script/Angelscript.Ck_RotateTowardsGym_GameState"))
    { return {}; }
    const auto Bridge = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(State);
    const auto Owner = H.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
    ck_rotate_towards_activation::FSourceFrame Frame;
    Frame.World = World;
    Frame.Source = State;
    Frame.WorldRoot = Root;
    Frame.WorldBridge = Bridge;
    Frame.Generation = Generation;
    return ck_rotate_towards_activation::Live(Bridge) &&
        Bridge.Has<ck::FFragment_LifetimeOwner>() &&
        Bridge.Get<ck::FFragment_LifetimeOwner>().Get_Entity() == Root &&
        UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(Bridge) == State &&
        Bridge.Has<ck::FFragment_EntityScript>() &&
        IsValid(UCk_Utils_EntityScript_UE::TryGet_EntityScript(Bridge)) &&
        UCk_Utils_EntityScript_UE::TryGet_EntityScript(Bridge)->GetClass()->GetPathName() ==
            TEXT("/Script/Angelscript.Ck_EntityScript_RotateTowardsGym_WorldRoot") &&
        UCk_Utils_Net_UE::Get_EntityReplication(Bridge) == ECk_Replication::Replicates &&
        Bridge.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>() &&
        IsValid(Bridge.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get()) &&
        Bridge.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>()
            ->Get_AssociatedEntity() == Bridge &&
        ck_rotate_towards_activation::ConstructedSimulationOwner(Frame, Owner)
            ? H : FCk_Handle{};
}

UStaticMeshComponent* UCk_RotateTowardsGym_Runtime_UE::TryGet_ReplicaCubeMesh(
    UWorld* World, int32 Generation, int32 Slot)
{
    const auto Cube = TryGet_ReplicaCube(World, Generation, Slot);
    if (!ck_rotate_towards_activation::Live(Cube)) { return nullptr; }
    UStaticMeshComponent* Result = nullptr;
    int32 Count = 0;
    const auto Registry = Cube.Get_RegistryView();
    Registry.View<ck::FFragment_LifetimeOwner, ck::FFragment_UnrealComponent>().ForEach(
        [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Lifetime,
            const ck::FFragment_UnrealComponent& Component) -> void
        {
            if (Lifetime.Get_Entity() != Cube) { return; }
            const FCk_Handle Child{Entity, Registry.Get_RegistryHandle()};
            if (!ck_rotate_towards_activation::Live(Child) ||
                !Child.Has<ck::FFragment_UnrealComponent_Params>() ||
                !Child.Has<ck::FTag_UnrealComponent_IsScene>() ||
                Child.Has<ck::FTag_UnrealComponent_NeedsSetup>() ||
                Child.Has<ck::FTag_UnrealComponent_TransformPushDisabled>() ||
                Component.Get_OwningEntity() != Cube) { Count = 2; return; }
            auto* Mesh = Cast<UStaticMeshComponent>(Component.Get_Component().Get());
            if (!IsValid(Mesh) || Mesh->GetClass() != UStaticMeshComponent::StaticClass() ||
                Mesh->GetWorld() != World || !Mesh->IsRegistered() ||
                Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision ||
                !Mesh->IsVisible() || Mesh->bHiddenInGame ||
                !IsValid(Mesh->GetStaticMesh().Get()) ||
                Mesh->GetStaticMesh()->GetPathName() !=
                    TEXT("/Engine/BasicShapes/Cube.Cube") ||
                FCk_Handle(UCk_Utils_UnrealComponent_UE::TryGet_OwningHandle_FromComponent(Mesh)) !=
                    Child) { Count = 2; return; }
            Result = Mesh;
            ++Count;
        });
    return Count == 1 ? Result : nullptr;
}

bool UCk_RotateTowardsGym_Runtime_UE::Get_IsReplicaCubePoseConformant(
    UWorld* World, int32 Generation, int32 Slot)
{
    const auto Cube = TryGet_ReplicaCube(World, Generation, Slot);
    const auto* Mesh = TryGet_ReplicaCubeMesh(World, Generation, Slot);
    if (!ck_rotate_towards_activation::Live(Cube) || !Mesh) { return false; }
    const auto& Pose = Cube.Get<ck::FFragment_Transform>().Get_Transform();
    return Pose.IsValid() && !Pose.ContainsNaN() &&
        Mesh->GetComponentTransform().Equals(Pose);
}

bool UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveMotionEntity(
    const FCk_Handle& Entity, const FCk_Handle& WorldRoot)
{
    if (!ck_rotate_towards_activation::Live(WorldRoot) ||
        WorldRoot != FCk_Handle{WorldRoot.Get_RegistryView().Get_TransientEntity(),
            WorldRoot.Get_RegistryView().Get_RegistryHandle()} ||
        !WorldRoot.Has<TWeakObjectPtr<UWorld>>() ||
        !IsValid(WorldRoot.Get<TWeakObjectPtr<UWorld>>().Get()) ||
        !ck_rotate_towards_activation::Live(Entity) || Entity == WorldRoot ||
        Entity.Get_RegistryView().Get_RegistryHandle() !=
            WorldRoot.Get_RegistryView().Get_RegistryHandle())
    { return false; }
    return !ck_rotate_towards_activation::HasTweenRole(Entity) &&
        !ck_rotate_towards_activation::HasRotateRole(Entity);
}

bool UCk_RotateTowardsGym_Runtime_UE::Get_IsPassiveAllocatedMotionEntity(
    const FCk_Handle& Entity, const FCk_Handle& WorldRoot)
{
    // First establish allocated registry-backed values; Has<T> checks only
    // registry validity and is safe on a represented pending-destroy entity.
    if (!ck::IsValid(WorldRoot, ck::IsValid_Policy_IncludePendingKill{}) ||
        !ck::IsValid(Entity, ck::IsValid_Policy_IncludePendingKill{}))
    { return false; }
    const auto Registry = WorldRoot.Get_RegistryView();
    if (WorldRoot != FCk_Handle{Registry.Get_TransientEntity(), Registry.Get_RegistryHandle()} ||
        Entity == WorldRoot ||
        Entity.Get_RegistryView().Get_RegistryHandle() != Registry.Get_RegistryHandle() ||
        !WorldRoot.Has<TWeakObjectPtr<UWorld>>())
    { return false; }
    auto* const World = WorldRoot.Get<TWeakObjectPtr<UWorld>,
        ck::IsValid_Policy_IncludePendingKill>().Get();
    if (!IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World) != WorldRoot)
    { return false; }
    return !ck_rotate_towards_activation::HasTweenRole(Entity) &&
        !ck_rotate_towards_activation::HasRotateRole(Entity);
}

bool UCk_RotateTowardsGym_Runtime_UE::TryValidate_Cohort(
    AGameStateBase* Source, const FCkRotateTowardsGym_MotionAdmission& Candidate)
{
    ck_rotate_towards_activation::FSourceFrame Frame;
    return ck_rotate_towards_activation::Validate(Source, Candidate, Frame);
}

ECkRotateTowardsGym_ActiveCohortState
UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortState(AGameStateBase* Source)
{
    using State = ECkRotateTowardsGym_ActiveCohortState;
    ck_rotate_towards_activation::FSourceFrame Frame;
    if (!ck_rotate_towards_activation::ReadSource(Source, Frame) || !Frame.Stored)
    { return State::Rejected; }
    const auto& A = *Frame.Stored;
    if (A.Generation != Frame.Generation || !A.Activated || A.Closed ||
        !ck_rotate_towards_activation::Live(A.Owner) ||
        !ck_rotate_towards_activation::HasSourceScope(Source, Frame, A.Owner))
    { return State::Rejected; }
    TArray<FCk_Handle> All;
    TArray<int32> Parents;
    if (!ck_rotate_towards_activation::BuildGraph(A, All, Parents) ||
        !ck_rotate_towards_activation::CheckGraph(Frame, A, All, Parents) ||
        !ck_rotate_towards_activation::CheckKnownRoles(A, All))
    { return State::Rejected; }
    bool TargetPending = false;
    bool CubePending = false;
    if (!ck_rotate_towards_activation::CheckActiveTransforms(A, nullptr, &TargetPending) ||
        !ck_rotate_towards_activation::CheckCubeComponents(
            Frame, A, All, Parents, nullptr, nullptr, true, &CubePending) ||
        !ck_rotate_towards_activation::CheckActiveTweens(A) ||
        !ck_rotate_towards_activation::CheckActiveTurrets(A))
    { return State::Rejected; }
    return TargetPending || CubePending ? State::Pending : State::Ready;
}

bool UCk_RotateTowardsGym_Runtime_UE::TryValidate_ActiveCohort(AGameStateBase* Source)
{
    return Get_ActiveCohortState(Source) == ECkRotateTowardsGym_ActiveCohortState::Ready;
}

FString UCk_RotateTowardsGym_Runtime_UE::Get_ActiveCohortDiagnostic(
    AGameStateBase* Source)
{
    ck_rotate_towards_activation::FSourceFrame Frame;
    if (!ck_rotate_towards_activation::ReadSource(Source, Frame) || !Frame.Stored)
    { return TEXT("stage=read-source"); }
    const auto& A = *Frame.Stored;
    if (A.Generation != Frame.Generation || !A.Activated || A.Closed ||
        !ck_rotate_towards_activation::Live(A.Owner) ||
        !ck_rotate_towards_activation::HasSourceScope(Source, Frame, A.Owner))
    { return TEXT("stage=source-scope-or-flags"); }
    TArray<FCk_Handle> All;
    TArray<int32> Parents;
    if (!ck_rotate_towards_activation::BuildGraph(A, All, Parents))
    { return TEXT("stage=graph-schema"); }
    if (!ck_rotate_towards_activation::CheckGraph(Frame, A, All, Parents))
    { return TEXT("stage=graph-ownership"); }
    if (!ck_rotate_towards_activation::CheckKnownRoles(A, All))
    { return TEXT("stage=known-roles"); }
    FString Detail;
    bool TargetPending = false;
    bool CubePending = false;
    if (!ck_rotate_towards_activation::CheckActiveTransforms(A, &Detail, &TargetPending))
    { return FString::Printf(TEXT("stage=active-transforms %s"), *Detail); }
    int32 BadSlot = -1;
    if (!ck_rotate_towards_activation::CheckCubeComponents(
            Frame, A, All, Parents, &BadSlot, &Detail, true, &CubePending))
    { return FString::Printf(TEXT("stage=cubes slot=%d %s"), BadSlot, *Detail); }
    if (!ck_rotate_towards_activation::CheckActiveTweens(A, &BadSlot))
    { return FString::Printf(TEXT("stage=tweens slot=%d"), BadSlot); }
    if (!ck_rotate_towards_activation::CheckActiveTurrets(A, &BadSlot))
    { return FString::Printf(TEXT("stage=turrets slot=%d"), BadSlot); }
    if (TargetPending || CubePending)
    {
        int32 CubeMask = 0;
        int32 QueuedTags = 0;
        int32 RequestCount = 0;
        const auto CountQueue = [&](const FCk_Handle& H) -> void
        {
            QueuedTags += H.Has<ck::FTag_SceneNode_PropagationQueued>() ? 1 : 0;
            if (H.Has<ck::FFragment_SceneNode_Requests>())
            { RequestCount += H.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().Num(); }
        };
        CountQueue(A.Motion[4]);
        for (int32 I = 0; I < A.CubeNodes.Num(); ++I)
        {
            if (ck_rotate_towards_activation::HasPendingScenePose(A.CubeNodes[I]))
            { CubeMask |= 1 << I; }
            CountQueue(A.CubeNodes[I]);
        }
        return FString::Printf(
            TEXT("stage=pending-scene-pose target=%d cubeMask=0x%03x queuedTags=%d requests=%d"),
            TargetPending, CubeMask, QueuedTags, RequestCount);
    }
    return TEXT("stage=pass");
}

bool UCk_RotateTowardsGym_Runtime_UE::TryClaim_NewOwner(
    AGameStateBase* Source, const FCk_Handle& Owner, int32 Generation)
{
    ck_rotate_towards_activation::FSourceFrame Frame;
    if (!ck_rotate_towards_activation::ReadSource(Source, Frame) || !Frame.Stored ||
        Generation != Frame.Generation || Frame.Stored->Generation != Generation ||
        Frame.Stored->Owner != Owner || Frame.Stored->Activated || Frame.Stored->Closed ||
        !ck_rotate_towards_activation::ConstructedSimulationOwner(Frame, Owner) ||
        Owner.Has<ck::FFragment_RotateTowardsGym_SourceScope>() ||
        Owner.Has<ck::FTag_EntityScript_ContinueConstruction>() ||
        Owner.Has<ck::FTag_EntityScript_FinishConstruction>() ||
        Owner.Has<ck::FTag_EntityScript_ConstructedThisFrame>() ||
        !Owner.Has<ck::FTag_EntityScript_HasBegunPlay>() ||
        !UCk_Utils_EntityReplicationDriver_UE::Get_IsReplicationComplete(Owner) ||
        !ck_rotate_towards_activation::HasRetainedSpawnCarrier(
            Owner, Frame.Stored->OwnerSpawnCarrier))
    { return false; }
    bool CarrierAllocated = false;
    if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
            Frame.Stored->OwnerSpawnCarrier, Owner, Frame.WorldBridge,
            Frame.WorldRoot, Frame.WorldRoot, CarrierAllocated) || CarrierAllocated)
    { return false; }
    // The reverse list retains historical handles. An allocated entry is
    // disqualifying even if its current owner was changed or omitted from a
    // filtered direct-child query; only truly deallocated entries may remain.
    for (const auto& Child :
        Owner.Get<ck::FFragment_LifetimeDependents>().Get_Entities())
    {
        if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child))
        { return false; }
    }
    bool HasCurrentChild = false;
    const auto Registry = Frame.WorldRoot.Get_RegistryView();
    Registry.View<ck::FFragment_LifetimeOwner>().ForEach(
        [&](FCk_Entity Entity, const ck::FFragment_LifetimeOwner& Lifetime) -> void
        {
            const auto Child = FCk_Handle{Entity, Registry.Get_RegistryHandle()};
            if (ck::IsValid(Child, ck::IsValid_Policy_IncludePendingKill{}) &&
                Lifetime.Get_Entity() == Owner)
            { HasCurrentChild = true; }
        });
    if (HasCurrentChild) { return false; }
    auto MutableOwner = Owner;
    MutableOwner.Add<ck::FFragment_RotateTowardsGym_SourceScope>(
        ck::FFragment_RotateTowardsGym_SourceScope{TWeakObjectPtr<AGameStateBase>(Source), Generation});
    return true;
}

bool UCk_RotateTowardsGym_Runtime_UE::Get_IsSourceContext(
    AGameStateBase* Source, const FCk_Handle& Owner, int32 Generation)
{
    ck_rotate_towards_activation::FSourceFrame Frame;
    if (!ck_rotate_towards_activation::ReadSource(Source, Frame) || !Frame.Stored ||
        Generation != Frame.Generation || Frame.Stored->Generation != Generation ||
        Frame.Stored->Owner != Owner || Frame.Stored->Activated || Frame.Stored->Closed ||
        !ck_rotate_towards_activation::ConstructedSimulationOwner(Frame, Owner) ||
        !ck_rotate_towards_activation::HasSourceScope(Source, Frame, Owner))
    { return false; }
    return true;
}

bool UCk_RotateTowardsGym_Runtime_UE::Get_IsSourceConstructionDrained(
    AGameStateBase* Source)
{
    ck_rotate_towards_activation::FSourceFrame Frame;
    if (!ck_rotate_towards_activation::ReadSource(Source, Frame) || !Frame.Stored ||
        Frame.Stored->Generation != Frame.Generation ||
        Frame.Stored->Activated || Frame.Stored->Closed ||
        !ck_rotate_towards_activation::ConstructedSimulationOwner(
            Frame, Frame.Stored->Owner) ||
        !ck_rotate_towards_activation::HasSourceScope(
            Source, Frame, Frame.Stored->Owner))
    { return false; }
    TArray<FCk_Handle> All;
    TArray<int32> Parents;
    if (!ck_rotate_towards_activation::BuildGraph(*Frame.Stored, All, Parents))
    { return false; }
    const auto Owner = Frame.Stored->Owner;
    if (Frame.Stored->CubeSpawnCarriers.Num() != 11) { return false; }
    // The request processor removes the request fragment before the carrier
    // deallocates. Check captured identities, including pending-kill carriers,
    // against the exact target/owner chain instead of scanning request tags.
    TSet<FCk_Handle> UniqueCarriers;
    bool CarrierAllocated = false;
    if (!ck_rotate_towards_activation::HasRetainedSpawnCarrier(
            Owner, Frame.Stored->OwnerSpawnCarrier) ||
        !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
            Frame.Stored->OwnerSpawnCarrier, Owner, Frame.WorldBridge,
            Frame.WorldRoot, Frame.WorldRoot, CarrierAllocated) || CarrierAllocated)
    { return false; }
    UniqueCarriers.Add(Frame.Stored->OwnerSpawnCarrier);
    for (int32 Slot = 0; Slot < 11; ++Slot)
    {
        const auto Carrier = Frame.Stored->CubeSpawnCarriers[Slot];
        if (UniqueCarriers.Contains(Carrier) ||
            !ck_rotate_towards_activation::HasRetainedSpawnCarrier(
                All[19 + Slot], Carrier) ||
            !UCk_EntityScriptGym_Spawn_Runtime_UE::TryCheck_CapturedStationCarrierWithParent(
                Carrier, All[19 + Slot], Owner, Frame.WorldRoot,
                Frame.WorldBridge, CarrierAllocated) || CarrierAllocated)
        { return false; }
        UniqueCarriers.Add(Carrier);
    }
    if (Owner.Has<ck::FTag_EntityScript_ContinueConstruction>() ||
        Owner.Has<ck::FTag_EntityScript_FinishConstruction>() ||
        Owner.Has<ck::FTag_EntityScript_ConstructedThisFrame>() ||
        !Owner.Has<ck::FTag_EntityScript_HasBegunPlay>() ||
        !UCk_Utils_EntityReplicationDriver_UE::Get_IsReplicationComplete(Owner))
    { return false; }
    for (int32 I = 0; I < All.Num(); ++I)
    {
        const auto H = All[I];
        const auto Parent = Parents[I] < 0 ? Owner : All[Parents[I]];
        if (!ck_rotate_towards_activation::Live(H) ||
            H.Get_RegistryView().Get_RegistryHandle() !=
                Frame.WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
            !H.Has<ck::FFragment_LifetimeOwner>() ||
            H.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Parent ||
            H.Has<ck::FTag_SceneNode_PropagationQueued>() ||
            (H.Has<ck::FFragment_Transform_Requests>() &&
                (!H.Get<ck::FFragment_Transform_Requests>().Get_LocationRequests().IsEmpty() ||
                 !H.Get<ck::FFragment_Transform_Requests>().Get_RotationRequests().IsEmpty() ||
                 H.Get<ck::FFragment_Transform_Requests>().Get_ScaleRequests().IsSet() ||
                 !H.Get<ck::FFragment_Transform_Requests>().Get_ForceRefreshRequests().IsEmpty())) ||
            (H.Has<ck::FFragment_SceneNode_Requests>() &&
                !H.Get<ck::FFragment_SceneNode_Requests>().Get_Requests().IsEmpty()) ||
            (H.Has<ck::FFragment_Tween_Requests>() &&
                !H.Get<ck::FFragment_Tween_Requests>().Get_Requests().IsEmpty()))
        { return false; }
        if (I >= 19 && I < 30)
        {
            if (H != TryGet_ReplicaCube(Frame.World, Frame.Generation, I - 19) ||
                H.Has<ck::FTag_EntityScript_ContinueConstruction>() ||
                H.Has<ck::FTag_EntityScript_FinishConstruction>() ||
                H.Has<ck::FTag_EntityScript_ConstructedThisFrame>() ||
                !H.Has<ck::FTag_EntityScript_HasBegunPlay>() ||
                !UCk_Utils_EntityReplicationDriver_UE::Get_IsReplicationComplete(H))
            { return false; }
        }
        else if (I >= 30)
        {
            auto* Mesh = TryGet_ReplicaCubeMesh(Frame.World, Frame.Generation, I - 30);
            const auto Node = All[19 + I - 30];
            if (!IsValid(Mesh) || !Node.Has<ck::FFragment_Transform>() ||
                !Mesh->GetComponentTransform().Equals(
                    Node.Get<ck::FFragment_Transform>().Get_Transform()))
            { return false; }
        }
    }
    return true;
}

bool UCk_RotateTowardsGym_Runtime_UE::TryActivate_Cohort(AGameStateBase* Source)
{
    ck_rotate_towards_activation::FSourceFrame Frame;
    if (!ck_rotate_towards_activation::ReadSource(Source, Frame) || !Frame.Stored) { return false; }
    auto Candidate = *Frame.Stored;
    if (!ck_rotate_towards_activation::Validate(Source, Candidate, Frame)) { return false; }

    // All recoverable failure checks end above. These are exactly the existing
    // Tween Resume and Rotate Enable tag/state transitions, without requests or
    // callbacks, so no member can advance independently before cohort publication.
    for (int32 I = 0; I < 4; ++I)
    {
        auto H = Candidate.Tweens[I];
        H.Remove<ck::FTag_Tween_Paused>();
        H.Add<ck::FTag_Tween_Playing>();
        H.Get<ck::FFragment_Tween>().Set_State(ECk_TweenState::Playing);
    }
    for (int32 I = 0; I < 6; ++I)
    { Candidate.Turrets[I].Remove<ck::FTag_RotateTowards_Disabled>(); }
    Frame.Stored->Activated = true;
    return true;
}

namespace ck_rotate_towards_activation
{
    // An Initiate-tagged entity can still contain an active producer; EndPlay
    // and later phases are outside the ordinary fragment mutation contract.
    auto SafeAllocated(const FSourceFrame& F, const FCk_Handle& H) -> bool
    {
        return ck::IsValid(H, ck::IsValid_Policy_IncludePendingKill{}) &&
            H.Get_RegistryView().Get_RegistryHandle() ==
                F.WorldRoot.Get_RegistryView().Get_RegistryHandle() &&
            !H.Has_Any<ck::FTag_DestroyEntity_EndPlay, ck::FTag_DestroyEntity_Teardown,
                ck::FTag_DestroyEntity_Await, ck::FTag_DestroyEntity_Finalize>();
    }

    auto DirectOwned(const FSourceFrame& F, const FCk_Handle& Child,
                     const FCk_Handle& Parent) -> bool
    {
        return SafeAllocated(F, Child) && SafeAllocated(F, Parent) && Child != Parent &&
            Child.Has<ck::FFragment_LifetimeOwner>() &&
            Child.Get<ck::FFragment_LifetimeOwner,
                ck::IsValid_Policy_IncludePendingKill>().Get_Entity() == Parent;
    }

    auto RootPath(const FSourceFrame& F, const FCkRotateTowardsGym_MotionAdmission& A,
                  int32 Index) -> bool
    {
        return Index >= 0 && Index < 3 && A.Roots.IsValidIndex(Index) &&
            DirectOwned(F, A.Roots[Index], A.Owner) &&
            A.Roots[Index].Has<ck::FFragment_Transform>();
    }

    auto MotionPath(const FSourceFrame& F, const FCkRotateTowardsGym_MotionAdmission& A,
                    int32 Index) -> bool
    {
        if (Index < 0 || Index >= 5 || !A.Motion.IsValidIndex(Index)) { return false; }
        const int32 RootIndex[4] = {0, 1, 1, 2};
        if (Index < 4)
        {
            return RootPath(F, A, RootIndex[Index]) &&
                DirectOwned(F, A.Motion[Index], A.Roots[RootIndex[Index]]) &&
                A.Motion[Index].Has<ck::FFragment_Transform>();
        }
        return MotionPath(F, A, 3) && DirectOwned(F, A.Motion[4], A.Motion[3]) &&
            A.Motion[4].Has<ck::FFragment_Transform>() &&
            A.Motion[4].Has<ck::FFragment_SceneNode>() &&
            ck::USceneNodeParent_Utils::Has(A.Motion[4]) &&
            UCk_Utils_SceneNode_UE::Get_Parent(UCk_Utils_SceneNode_UE::Cast(A.Motion[4])) ==
                A.Motion[3];
    }

    auto UniqueProducer(const FCkRotateTowardsGym_MotionAdmission& A,
                        const FCk_Handle& H) -> bool
    {
        int32 Count = 0;
        for (const auto& T : A.Turrets) { Count += T == H ? 1 : 0; }
        for (const auto& T : A.Tweens) { Count += T == H ? 1 : 0; }
        for (const auto& Root : A.Roots) { if (Root == H) { return false; } }
        for (const auto& Motion : A.Motion) { if (Motion == H) { return false; } }
        for (const auto& Node : A.CubeNodes) { if (Node == H) { return false; } }
        for (const auto& Component : A.CubeComponents)
        { if (Component == H) { return false; } }
        return Count == 1;
    }

    auto TurretRecipe(const FCkRotateTowardsGym_MotionAdmission& A,
                      const FCk_Handle& H, int32 Index) -> bool
    {
        if (!H.Has<ck::FFragment_RotateTowards_Tunables>() ||
            !H.Has<ck::FFragment_RotateTowards>() ||
            !H.Has<ck::FFragment_RotateTowards_SolveResult>() ||
            !H.Has<ck::FTag_RotateTowards_HasTarget>()) { return false; }
        const auto Target = Index < 2 ? A.Motion[0] : Index == 2 ? A.Motion[1] : A.Motion[4];
        const auto& Tunables = H.Get<ck::FFragment_RotateTowards_Tunables>();
        const auto& Rotate = H.Get<ck::FFragment_RotateTowards>();
        const float Rate = Index < 2 ? 60.0f : 90.0f;
        if (Rotate.Get_Target() != Target ||
            Tunables.Get_Mode() != (Index == 1 ? ECk_RotateTowards_Mode::Instant :
                ECk_RotateTowards_Mode::RateLimited) ||
            !CheckAxis(Tunables.Get_Yaw(), Index == 4, Rate) ||
            !CheckAxis(Tunables.Get_Pitch(), Index == 5, Rate) ||
            !CheckAxis(Tunables.Get_Roll(), false, Rate) ||
            Tunables.Get_ReachedToleranceDeg() != 1.0f) { return false; }
        if (Index != 2) { return !H.Has<ck::FFragment_RotateTowards_RangeClamp>(); }
        if (!H.Has<ck::FFragment_RotateTowards_RangeClamp>()) { return false; }
        const auto& Clamp = H.Get<ck::FFragment_RotateTowards_RangeClamp>();
        return Clamp.Get_RestReferencePoint() == A.Motion[2] &&
            Clamp.Get_Pitch().Get_Enabled() == ECk_EnableDisable::Disable &&
            Clamp.Get_Roll().Get_Enabled() == ECk_EnableDisable::Disable &&
            Clamp.Get_Yaw().Get_Enabled() == ECk_EnableDisable::Enable &&
            Clamp.Get_Yaw().Get_RangeDeg().Get_Min(ECk_Inclusiveness::Inclusive) == -45.0 &&
            Clamp.Get_Yaw().Get_RangeDeg().Get_Max(ECk_Inclusiveness::Inclusive) == 45.0;
    }

    auto TweenRecipe(const FCkRotateTowardsGym_MotionAdmission& A,
                     const FCk_Handle& H, int32 Index) -> bool
    {
        if (!H.Has<ck::FFragment_Tween_Params>() || !H.Has<ck::FFragment_Tween>() ||
            H.Has<ck::FFragment_Tween_Chain>() || H.Has<ck::FFragment_Tween_SplineFollow>() ||
            H.Has<ck::FFragment_Tween_CurveDrive>() ||
            H.Has<ck::FFragment_Tween_EasingCurve>()) { return false; }
        const FVector Origins[3] = {FVector(1400, 0, 150), FVector(5000, 0, 150),
            FVector(1400, 3000, 150)};
        const FVector Starts[4] = {Origins[0] + FVector(400, -300, 0),
            Origins[1] + FRotator(0, 70, 0).Vector() * 400,
            Origins[2] + FVector(400, -300, 0), FVector(0, 0, -150)};
        const FVector Ends[4] = {Origins[0] + FVector(400, 300, 0),
            Origins[1] + FRotator(0, -70, 0).Vector() * 400,
            Origins[2] + FVector(400, 300, 0), FVector(0, 0, 150)};
        const float Durations[4] = {3.0f, 4.0f, 3.0f, 2.0f};
        const auto& Spec = H.Get<ck::FFragment_Tween_Params>();
        return Spec.Get_Duration() == Durations[Index] &&
            Spec.Get_Easing() == ECk_TweenEasing::InOutSine &&
            Spec.Get_LoopType() == ECk_TweenLoopType::Yoyo &&
            Spec.Get_LoopCount() == -1 && Spec.Get_YoyoDelay() == 0.0f &&
            Spec.Get_Target() == (Index == 3 ? ECk_TweenTarget::SceneNodeOffset_Location :
                ECk_TweenTarget::Transform_Location) &&
            Spec.Get_CompletionBehavior() == ECk_TweenCompletionBehavior::DoNothing &&
            Spec.Get_StartValue().IsVector() && Spec.Get_EndValue().IsVector() &&
            Spec.Get_StartValue().GetAsVector() == Starts[Index] &&
            Spec.Get_EndValue().GetAsVector() == Ends[Index];
    }

    auto Classify(FCkRotateTowardsGym_QuiesceResult& Out, int32 Slot,
                  const FCk_Handle& H, int32 Kind) -> void
    {
        TArray<int32>* Slots = Kind == 0 ? &Out.StoppedSlots :
            Kind == 1 ? &Out.AbsentSlots : &Out.UnsafeSlots;
        TArray<FCk_Handle>* Ids = Kind == 0 ? &Out.Stopped :
            Kind == 1 ? &Out.Absent : &Out.Unsafe;
        Slots->Add(Slot);
        Ids->Add(H);
        if (Kind == 0) { ++Out.StoppedCount; }
        else if (Kind == 1) { ++Out.AbsentCount; }
        else { ++Out.UnsafeCount; }
    }
}

bool UCk_RotateTowardsGym_Runtime_UE::TryQuiesce_KnownProducers(
    AGameStateBase* Source, FCkRotateTowardsGym_QuiesceResult& OutResult)
{
    OutResult = {};
    ck_rotate_towards_activation::FSourceFrame Frame;
    if (!ck_rotate_towards_activation::ReadSource(Source, Frame, false) || !Frame.Stored ||
        Frame.Stored->Generation != Frame.Generation ||
        !ck_rotate_towards_activation::SafeAllocated(Frame, Frame.Stored->Owner) ||
        Frame.Stored->Owner == Frame.WorldRoot ||
        Frame.Stored->Owner == Frame.WorldBridge ||
        Frame.Stored->Owner.Get_RegistryView().Get_RegistryHandle() !=
            Frame.WorldRoot.Get_RegistryView().Get_RegistryHandle() ||
        !Frame.Stored->Owner.Has<ck::FFragment_LifetimeOwner>() ||
        Frame.Stored->Owner.Get<ck::FFragment_LifetimeOwner,
            ck::IsValid_Policy_IncludePendingKill>().Get_Entity() != Frame.WorldBridge ||
        !ck_rotate_towards_activation::HasSourceScope(Source, Frame, Frame.Stored->Owner))
    { return false; }

    Frame.Stored->Closed = true;
    const auto A = *Frame.Stored;
    OutResult.SchemaUnsafe = A.Roots.Num() != 3 || A.Motion.Num() != 5 ||
        A.Turrets.Num() != 7 || A.Tweens.Num() != 4;
    for (int32 I = 3; I < A.Roots.Num(); ++I) { OutResult.Unexpected.Add(A.Roots[I]); }
    for (int32 I = 5; I < A.Motion.Num(); ++I) { OutResult.Unexpected.Add(A.Motion[I]); }
    for (int32 I = 7; I < A.Turrets.Num(); ++I) { OutResult.Unexpected.Add(A.Turrets[I]); }
    for (int32 I = 4; I < A.Tweens.Num(); ++I) { OutResult.Unexpected.Add(A.Tweens[I]); }
    OutResult.UnexpectedCount = OutResult.Unexpected.Num();
    for (int32 Slot = 0; Slot < 11; ++Slot)
    {
        const bool IsTurret = Slot < 7;
        const int32 Index = IsTurret ? Slot : Slot - 7;
        if ((IsTurret && !A.Turrets.IsValidIndex(Index)) ||
            (!IsTurret && !A.Tweens.IsValidIndex(Index)))
        { ck_rotate_towards_activation::Classify(OutResult, Slot, {}, 2); continue; }
        auto H = IsTurret ? FCk_Handle(A.Turrets[Index]) : FCk_Handle(A.Tweens[Index]);
        if (H == FCk_Handle{})
        { ck_rotate_towards_activation::Classify(OutResult, Slot, H, 1); continue; }
        if (H.Get_RegistryView().Get_RegistryHandle() !=
                Frame.WorldRoot.Get_RegistryView().Get_RegistryHandle())
        { ck_rotate_towards_activation::Classify(OutResult, Slot, H, 2); continue; }
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H))
        { ck_rotate_towards_activation::Classify(OutResult, Slot, H, 1); continue; }
        if (!ck_rotate_towards_activation::SafeAllocated(Frame, H) ||
            !ck_rotate_towards_activation::UniqueProducer(A, H))
        { ck_rotate_towards_activation::Classify(OutResult, Slot, H, 2); continue; }

        if (IsTurret)
        {
            const int32 RootIndex = Index < 2 ? 0 : Index == 2 ? 1 : 2;
            const int32 TargetIndex = Index < 2 ? 0 : Index == 2 ? 1 : 4;
            if (!H.Has<ck::FFragment_Transform>() ||
                H.Has<ck::FFragment_SceneNode>() ||
                H.Has<ck::FFragment_UnrealComponent>() ||
                !ck_rotate_towards_activation::RootPath(Frame, A, RootIndex) ||
                !ck_rotate_towards_activation::MotionPath(Frame, A, TargetIndex) ||
                (Index == 2 && !ck_rotate_towards_activation::MotionPath(Frame, A, 2)) ||
                !ck_rotate_towards_activation::DirectOwned(Frame, H, A.Roots[RootIndex]) ||
                !ck_rotate_towards_activation::TurretRecipe(A, H, Index))
            { ck_rotate_towards_activation::Classify(OutResult, Slot, H, 2); continue; }
            const bool Pending = H.Has<ck::FFragment_RotateTowards_Requests>() &&
                !H.Get<ck::FFragment_RotateTowards_Requests>().Get_Requests().IsEmpty();
            // A second kernel on this same entity is not stopped by the turret tag.
            const bool CrossKernel = ck_rotate_towards_activation::HasTweenRole(H);
            H.AddOrGet<ck::FTag_RotateTowards_Disabled>();
            ck_rotate_towards_activation::Classify(
                OutResult, Slot, H, (Pending || CrossKernel) ? 2 : 0);
            continue;
        }

        const int32 MotionIndex[4] = {0, 1, 3, 4};
        if (H.Has<ck::FFragment_Transform>() ||
            H.Has<ck::FFragment_SceneNode>() ||
            H.Has<ck::FFragment_UnrealComponent>() ||
            !ck_rotate_towards_activation::MotionPath(Frame, A, MotionIndex[Index]) ||
            !ck_rotate_towards_activation::DirectOwned(Frame, H, A.Motion[MotionIndex[Index]]) ||
            !ck_rotate_towards_activation::TweenRecipe(A, H, Index))
        { ck_rotate_towards_activation::Classify(OutResult, Slot, H, 2); continue; }
        const bool Pending = H.Has<ck::FFragment_Tween_Requests>() &&
            !H.Get<ck::FFragment_Tween_Requests>().Get_Requests().IsEmpty();
        // Pausing the tween cannot quiesce an additional rotate kernel.
        const bool CrossKernel = ck_rotate_towards_activation::HasRotateRole(H);
        const auto State = H.Get<ck::FFragment_Tween>().Get_State();
        const bool Playing = H.Has<ck::FTag_Tween_Playing>();
        const bool Paused = H.Has<ck::FTag_Tween_Paused>();
        const bool Completed = H.Has<ck::FTag_Tween_Completed>();
        const bool YoyoDelay = H.Has<ck::FTag_Tween_InYoyoDelay>();
        if (State == ECk_TweenState::Playing && Playing && !Paused && !Completed)
        {
            H.Remove<ck::FTag_Tween_Playing>();
            H.AddOrGet<ck::FTag_Tween_Paused>();
            H.Get<ck::FFragment_Tween>().Set_State(ECk_TweenState::Paused);
        }
        else if (!(State == ECk_TweenState::Paused && Paused && !Playing && !Completed) &&
                 !((State == ECk_TweenState::Cancelled || State == ECk_TweenState::Completed) &&
                   Completed && !Playing && !Paused && !YoyoDelay))
        { ck_rotate_towards_activation::Classify(OutResult, Slot, H, 2); continue; }
        ck_rotate_towards_activation::Classify(
            OutResult, Slot, H, (Pending || CrossKernel) ? 2 : 0);
    }
    OutResult.AllKnownStopped = !OutResult.SchemaUnsafe && OutResult.UnsafeSlots.IsEmpty() &&
        OutResult.StoppedSlots.Num() + OutResult.AbsentSlots.Num() == 11;
    return OutResult.AllKnownStopped;
}

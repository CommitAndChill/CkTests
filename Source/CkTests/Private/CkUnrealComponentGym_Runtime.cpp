#include "CkUnrealComponentGym_Runtime.h"

#include "CkCore/Validation/CkIsValid.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Net/CkNet_Fragment.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

namespace CkUnrealComponentGymRuntimePrivate
{
    bool Live(const UObject* Object)
    {
        return ck::IsValid(Object) &&
            !Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool SafePose(const FTransform& Pose)
    {
        const FVector Position = Pose.GetLocation();
        const FVector Scale = Pose.GetScale3D();
        const FQuat Rotation = Pose.GetRotation();
        return FMath::IsFinite(Position.X) && FMath::IsFinite(Position.Y) &&
            FMath::IsFinite(Position.Z) && FMath::IsFinite(Scale.X) &&
            FMath::IsFinite(Scale.Y) && FMath::IsFinite(Scale.Z) &&
            FMath::IsFinite(Rotation.X) && FMath::IsFinite(Rotation.Y) &&
            FMath::IsFinite(Rotation.Z) && FMath::IsFinite(Rotation.W) &&
            Position.GetAbsMax() <= 10000000.0 &&
            Scale.GetAbsMax() <= 10000.0 &&
            Rotation.IsNormalized();
    }

    UClass* ClassForRole(int32 Role)
    {
        switch (Role)
        {
            case 0: return UStaticMeshComponent::StaticClass();
            case 1: return UPointLightComponent::StaticClass();
            case 2: return USpotLightComponent::StaticClass();
            case 3: return UTextRenderComponent::StaticClass();
            case 4: return UArrowComponent::StaticClass();
            case 5: return UBoxComponent::StaticClass();
            case 6: return USphereComponent::StaticClass();
            case 7: return UCapsuleComponent::StaticClass();
            default: return nullptr;
        }
    }

    bool Near(float Actual, float Expected)
    {
        return FMath::IsFinite(Actual) && FMath::IsNearlyEqual(Actual, Expected, 0.02f);
    }

    bool HasAuthoredStyle(const USceneComponent* Component, int32 Role)
    {
        if (!Live(Component) || !Component->IsRegistered() ||
            Component->GetMobility() != EComponentMobility::Movable)
        { return false; }

        const auto* Primitive = Cast<UPrimitiveComponent>(Component);
        if (Primitive && Primitive->IsSimulatingPhysics()) { return false; }
        if (Role >= 4 && (!Primitive || Primitive->bHiddenInGame || !Primitive->IsVisible()))
        { return false; }

        switch (Role)
        {
            case 0:
            {
                const auto* Mesh = Cast<UStaticMeshComponent>(Component);
                const UStaticMesh* const Asset = Mesh ? Mesh->GetStaticMesh() : nullptr;
                return Live(Asset) &&
                    Asset->GetPathName() == TEXT("/Engine/BasicShapes/Cube.Cube") &&
                    Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                    Mesh->GetComponentScale().Equals(FVector::OneVector, 0.0);
            }
            case 1:
            {
                const auto* Light = Cast<UPointLightComponent>(Component);
                return Light && Near(Light->Intensity, 50000.0f) &&
                    Near(Light->AttenuationRadius, 1200.0f) &&
                    Light->GetLightColor().Equals(FLinearColor(1.0f, 0.4f, 0.2f), 0.02f);
            }
            case 2:
            {
                const auto* Light = Cast<USpotLightComponent>(Component);
                return Light && Near(Light->Intensity, 100000.0f) &&
                    Near(Light->AttenuationRadius, 1500.0f) &&
                    Near(Light->InnerConeAngle, 20.0f) &&
                    Near(Light->OuterConeAngle, 35.0f) &&
                    Light->GetLightColor().Equals(FLinearColor(0.3f, 0.7f, 1.0f), 0.02f);
            }
            case 3:
            {
                const auto* Text = Cast<UTextRenderComponent>(Component);
                return Text && Text->Text.ToString() == TEXT("ENTITY") &&
                    Near(Text->WorldSize, 40.0f) &&
                    Text->TextRenderColor == FColor(255, 255, 0, 255) &&
                    Text->HorizontalAlignment == EHTA_Center;
            }
            case 4:
            {
                const auto* Arrow = Cast<UArrowComponent>(Component);
                return Arrow && Arrow->ArrowColor == FColor(255, 0, 255, 255) &&
                    Near(Arrow->ArrowSize, 5.0f) && Near(Arrow->ArrowLength, 100.0f) &&
                    !Arrow->bIsScreenSizeScaled && !Arrow->IsEditorOnly();
            }
            case 5:
            {
                const auto* Box = Cast<UBoxComponent>(Component);
                return Box && Box->GetUnscaledBoxExtent().Equals(FVector(80.0), 0.0) &&
                    Box->GetCollisionEnabled() == ECollisionEnabled::QueryOnly;
            }
            case 6:
            {
                const auto* Sphere = Cast<USphereComponent>(Component);
                return Sphere && Near(Sphere->GetUnscaledSphereRadius(), 80.0f) &&
                    Sphere->GetCollisionEnabled() == ECollisionEnabled::QueryOnly;
            }
            case 7:
            {
                const auto* Capsule = Cast<UCapsuleComponent>(Component);
                return Capsule && Near(Capsule->GetUnscaledCapsuleRadius(), 50.0f) &&
                    Near(Capsule->GetUnscaledCapsuleHalfHeight(), 100.0f) &&
                    Capsule->GetCollisionEnabled() == ECollisionEnabled::QueryOnly;
            }
            default: return false;
        }
    }
    bool ResolveSourceComponent(
        AActor* ExpectedSourceActor, const FCk_Handle& RawEntity,
        const FCk_Handle& RawComponent, int32 Role,
        UActorComponent*& OutComponent, AActor*& OutHostActor)
    {
    OutComponent = nullptr;
    OutHostActor = nullptr;
    if (!IsInGameThread() || IsGarbageCollecting() || Role < 0 || Role > 7 ||
        RawEntity == FCk_Handle{} || RawComponent == FCk_Handle{} ||
        !Live(ExpectedSourceActor) || ExpectedSourceActor->IsActorBeingDestroyed() ||
        ExpectedSourceActor->GetIsReplicated())
    { return false; }

    UWorld* const World = ExpectedSourceActor->GetWorld();
    if (!Live(World) || World->bIsTearingDown || !World->IsGameWorld() ||
        World->GetNetMode() == NM_Client || !ExpectedSourceActor->HasAuthority())
    { return false; }
    USceneComponent* const SourceRoot = ExpectedSourceActor->GetRootComponent();
    if (!Live(SourceRoot) || SourceRoot->GetOwner() != ExpectedSourceActor ||
        SourceRoot->GetWorld() != World || !SourceRoot->IsRegistered() ||
        SourceRoot->GetMobility() != EComponentMobility::Movable)
    { return false; }

    const FCk_Handle Context =
        UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!ck::IsValid(Context) || !ck::IsValid(RawEntity) ||
        RawEntity.Get_RegistryView().Get_RegistryHandle() !=
            Context.Get_RegistryView().Get_RegistryHandle() ||
        !RawEntity.Has<ck::FFragment_Net_Params>() ||
        RawEntity.Get<ck::FFragment_Net_Params>()
            .Get_ConnectionSettings().Get_Replication() !=
                ECk_Replication::DoesNotReplicate ||
        RawEntity.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>() ||
        !RawEntity.Has<TWeakObjectPtr<UWorld>>() ||
        RawEntity.Get<TWeakObjectPtr<UWorld>>().Get() != World ||
        !UCk_Utils_OwningActor_UE::Has(RawEntity) ||
        !UCk_Utils_Transform_UE::Has(RawEntity) ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            RawEntity, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
    { return false; }
    if (UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(ExpectedSourceActor) != RawEntity ||
        UCk_Utils_OwningActor_UE::TryGet_EntityOwningActor(RawEntity) != ExpectedSourceActor)
    { return false; }

    if (!ck::IsValid(RawComponent) ||
        RawComponent.Get_RegistryView().Get_RegistryHandle() !=
            Context.Get_RegistryView().Get_RegistryHandle() ||
        !RawComponent.Has<ck::FFragment_UnrealComponent>() ||
        !RawComponent.Has<ck::FFragment_UnrealComponent_Params>() ||
        !RawComponent.Has<ck::FTag_UnrealComponent_IsScene>() ||
        RawComponent.Has<ck::FTag_UnrealComponent_NeedsSetup>() ||
        !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(RawComponent) ||
        UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(RawComponent) != RawEntity ||
        UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
            RawComponent, ECk_EntityLifetime_DestructionPhase::BeginDestroy))
    { return false; }

    const auto& Fragment = RawComponent.Get<ck::FFragment_UnrealComponent>();
    const auto& Params = RawComponent.Get<ck::FFragment_UnrealComponent_Params>();
    UClass* const ExpectedClass = ClassForRole(Role);
    if (!ExpectedClass || Fragment.Get_OwningEntity() != RawEntity ||
        Params.Get_ComponentClass().Get() != ExpectedClass ||
        Params.Get_TickPolicy() != ECk_UnrealComponent_TickPolicy::DoNotTick ||
        Params.Get_DebugName() != FName(TEXT("UnrealComponentGym")))
    { return false; }

    const auto Typed = UCk_Utils_UnrealComponent_UE::CastChecked(RawComponent);
    const auto Recorded = UCk_Utils_UnrealComponent_UE::Get_AllHandles(RawEntity);
    if (Recorded.Num() != 1 || Recorded[0] != Typed)
    { return false; }
    UActorComponent* const RawObject = Fragment.Get_Component().Get();
    if (!Live(RawObject) || RawObject->GetClass() != ExpectedClass ||
        RawObject->GetWorld() != World || !RawObject->IsRegistered() ||
        UCk_Utils_UnrealComponent_UE::TryGet_OwningHandle_FromComponent(RawObject) != Typed)
    { return false; }
    AActor* const HostActor = RawObject->GetOwner();
    if (!Live(HostActor) || HostActor->IsActorBeingDestroyed() ||
        HostActor == ExpectedSourceActor || HostActor->GetWorld() != World ||
        HostActor->GetIsReplicated() ||
        RawObject->GetOuter() != HostActor ||
        HostActor->GetClass() != AActor::StaticClass() ||
        !HostActor->HasAnyFlags(RF_Transient))
    { return false; }
    OutComponent = RawObject;
    OutHostActor = HostActor;
    return true;
    }
}

bool UCk_UnrealComponentGym_Runtime_UE::TryCaptureSourceComponent(
    AActor* ExpectedSourceActor, const FCk_Handle& RawEntity,
    const FCk_Handle& RawComponent, int32 Role,
    UActorComponent*& OutComponent, AActor*& OutHostActor)
{
    return CkUnrealComponentGymRuntimePrivate::ResolveSourceComponent(
        ExpectedSourceActor, RawEntity, RawComponent, Role,
        OutComponent, OutHostActor);
}

bool UCk_UnrealComponentGym_Runtime_UE::TryGetSourceSample(
    AActor* ExpectedSourceActor, AActor* ExpectedHostActor,
    UActorComponent* ExpectedComponent, const FCk_Handle& RawEntity,
    const FCk_Handle& RawComponent, int32 Role,
    FCkUnrealComponentGym_SourceSample& OutSample)
{
    OutSample = {};
    using namespace CkUnrealComponentGymRuntimePrivate;
    if (!IsInGameThread() || IsGarbageCollecting() ||
        !Live(ExpectedComponent) || !Live(ExpectedHostActor) ||
        ExpectedHostActor->IsActorBeingDestroyed())
    { return false; }
    UActorComponent* ActualComponent = nullptr;
    AActor* ActualHostActor = nullptr;
    if (!ResolveSourceComponent(ExpectedSourceActor, RawEntity, RawComponent,
            Role, ActualComponent, ActualHostActor) ||
        ActualComponent != ExpectedComponent ||
        ActualHostActor != ExpectedHostActor)
    { return false; }
    const auto* Scene = Cast<USceneComponent>(ActualComponent);
    if (!Scene || Scene->GetMobility() != EComponentMobility::Movable)
    { return false; }
    const FTransform Pose = Scene->GetComponentTransform();
    if (!SafePose(Pose)) { return false; }

    FCkUnrealComponentGym_SourceSample Candidate;
    Candidate.Role = Role;
    Candidate.ActualComponentClass = ActualComponent->GetClass()->GetFName();
    Candidate.ComponentWorldPose = Pose;
    Candidate.bStyleValid = HasAuthoredStyle(Scene, Role);
    Candidate.bTransformQueueEmpty = true;
    if (RawEntity.Has<ck::FFragment_Transform_Requests>())
    {
        const auto& Requests = RawEntity.Get<ck::FFragment_Transform_Requests>();
        Candidate.bTransformQueueEmpty =
            Requests.Get_RotationRequests().IsEmpty() &&
            Requests.Get_LocationRequests().IsEmpty() &&
            !Requests.Get_ScaleRequests().IsSet() &&
            Requests.Get_ForceRefreshRequests().IsEmpty();
    }
    OutSample = Candidate;
    return true;
}

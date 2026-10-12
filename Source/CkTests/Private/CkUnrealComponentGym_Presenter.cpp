#include "CkUnrealComponentGym_Presenter.h"

#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

namespace CkUnrealComponentGymPresenterPrivate
{
    bool IsFinitePose(const FTransform& Pose)
    {
        // A non-unit quaternion is finite but cannot be installed without an
        // engine normalization that changes the wire value.
        return !Pose.ContainsNaN() && Pose.GetRotation().IsNormalized();
    }
}

ACk_UnrealComponentGym_Presenter_UE::ACk_UnrealComponentGym_Presenter_UE()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    bAlwaysRelevant = false;
    SetActorHiddenInGame(true);

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;
    SceneRoot->SetMobility(EComponentMobility::Movable);

    StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
    PointLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PointLight"));
    SpotLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("SpotLight"));
    TextRender = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TextRender"));
    Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
    Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
    Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
    Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));

    USceneComponent* const Slots[SlotCount] =
    {
        StaticMesh, PointLight, SpotLight, TextRender,
        Arrow, Box, Sphere, Capsule
    };
    for (USceneComponent* const Slot : Slots)
    {
        Slot->SetupAttachment(SceneRoot);
        Slot->SetMobility(EComponentMobility::Movable);
        Slot->SetAbsolute(true, true, true);
        Slot->SetVisibility(false);
        Slot->SetHiddenInGame(true);
    }

    const ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    CubeAsset = Cube.Object;
    if (Cube.Succeeded())
    { StaticMesh->SetStaticMesh(CubeAsset); }
    StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StaticMesh->SetWorldScale3D(FVector(1.0f, 1.0f, 1.0f));

    PointLight->SetIntensity(50000.0f);
    PointLight->SetAttenuationRadius(1200.0f);
    PointLight->SetLightColor(FLinearColor(1.0f, 0.4f, 0.2f, 1.0f));

    SpotLight->SetIntensity(100000.0f);
    SpotLight->SetAttenuationRadius(1500.0f);
    SpotLight->SetInnerConeAngle(20.0f);
    SpotLight->SetOuterConeAngle(35.0f);
    SpotLight->SetLightColor(FLinearColor(0.3f, 0.7f, 1.0f, 1.0f));

    TextRender->SetText(FText::FromString(TEXT("ENTITY")));
    TextRender->SetWorldSize(40.0f);
    TextRender->SetTextRenderColor(FColor(255, 255, 0, 255));
    TextRender->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);

    Arrow->SetArrowColor(FLinearColor(1.0f, 0.0f, 1.0f, 1.0f));
    Arrow->ArrowSize = 5.0f;
    Arrow->ArrowLength = 100.0f;
    Arrow->bIsScreenSizeScaled = false;
    Arrow->bIsEditorOnly = false;

    Box->SetBoxExtent(FVector(80.0f, 80.0f, 80.0f), false);
    Sphere->SetSphereRadius(80.0f, false);
    Capsule->SetCapsuleSize(50.0f, 100.0f, false);
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACk_UnrealComponentGym_Presenter_UE::BeginPlay()
{
    Super::BeginPlay();
    CapturedWorld = GetWorld();
    CapturedLocalController = Cast<APlayerController>(GetOwner());
    bInOperation = true;
    HideKnownOwnedVisuals();
    if (bFailed || !HasIntactCohort() ||
        !HasExpectedVisibilityState(true))
    { FailClosed(); }
    bInOperation = false;
}

void ACk_UnrealComponentGym_Presenter_UE::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    FailClosed();
    Super::EndPlay(EndPlayReason);
}

bool ACk_UnrealComponentGym_Presenter_UE::HasLocalContext() const
{
    const UWorld* const World = CapturedWorld.Get();
    const APlayerController* const Controller = CapturedLocalController.Get();
    return IsInGameThread() && IsValid(World) && IsValid(Controller) &&
        GetWorld() == World && Controller->GetWorld() == World &&
        GetOwner() == Controller && World->IsGameWorld() &&
        World->GetNetMode() != NM_DedicatedServer &&
        Controller->IsLocalController();
}

USceneComponent* ACk_UnrealComponentGym_Presenter_UE::GetSlot(
    int32 Index) const
{
    switch (Index)
    {
    case 0: return StaticMesh;
    case 1: return PointLight;
    case 2: return SpotLight;
    case 3: return TextRender;
    case 4: return Arrow;
    case 5: return Box;
    case 6: return Sphere;
    case 7: return Capsule;
    default: return nullptr;
    }
}

bool ACk_UnrealComponentGym_Presenter_UE::IsKnownSlot(
    int32 Index) const
{
    const FName Names[SlotCount] =
    {
        FName(TEXT("StaticMesh")), FName(TEXT("PointLight")),
        FName(TEXT("SpotLight")), FName(TEXT("TextRender")),
        FName(TEXT("Arrow")), FName(TEXT("Box")),
        FName(TEXT("Sphere")), FName(TEXT("Capsule"))
    };
    const UClass* const Classes[SlotCount] =
    {
        UStaticMeshComponent::StaticClass(), UPointLightComponent::StaticClass(),
        USpotLightComponent::StaticClass(), UTextRenderComponent::StaticClass(),
        UArrowComponent::StaticClass(), UBoxComponent::StaticClass(),
        USphereComponent::StaticClass(), UCapsuleComponent::StaticClass()
    };
    if (Index < 0 || Index >= SlotCount)
    { return false; }
    const USceneComponent* const Slot = GetSlot(Index);
    return IsValid(Slot) && Slot->GetOwner() == this &&
        Slot->GetOuter() == this && Slot->GetFName() == Names[Index] &&
        Slot->GetClass() == Classes[Index] &&
        Slot->HasAnyFlags(RF_DefaultSubObject);
}

bool ACk_UnrealComponentGym_Presenter_UE::HasIntactCohort() const
{
    if (!IsInGameThread() || IsActorBeingDestroyed() ||
        !IsValid(SceneRoot.Get()) || GetRootComponent() != SceneRoot ||
        SceneRoot->GetOwner() != this || SceneRoot->GetOuter() != this ||
        SceneRoot->GetFName() != TEXT("SceneRoot") ||
        SceneRoot->GetClass() != USceneComponent::StaticClass() ||
        !SceneRoot->HasAnyFlags(RF_DefaultSubObject) ||
        !SceneRoot->IsRegistered() || !HasLocalContext() ||
        !IsValid(CubeAsset.Get()) || !IsValid(StaticMesh.Get()) ||
        StaticMesh->GetStaticMesh() != CubeAsset)
    { return false; }

    TArray<USceneComponent*> SceneComponents;
    GetComponents<USceneComponent>(SceneComponents);
    if (SceneComponents.Num() != SlotCount + 1)
    { return false; }
    for (const USceneComponent* const Actual : SceneComponents)
    {
        bool bExpected = Actual == SceneRoot;
        for (int32 I = 0; !bExpected && I < SlotCount; ++I)
        { bExpected = Actual == GetSlot(I); }
        if (!bExpected)
        { return false; }
    }

    for (int32 I = 0; I < SlotCount; ++I)
    {
        const USceneComponent* const Slot = GetSlot(I);
        if (!IsKnownSlot(I) || !Slot->IsRegistered() ||
            Slot->GetAttachParent() != SceneRoot ||
            !Slot->IsUsingAbsoluteLocation() ||
            !Slot->IsUsingAbsoluteRotation() ||
            !Slot->IsUsingAbsoluteScale())
        { return false; }
        for (int32 J = 0; J < I; ++J)
        {
            if (Slot == GetSlot(J))
            { return false; }
        }
    }
    return HasAuthoredStyle();
}

bool ACk_UnrealComponentGym_Presenter_UE::HasAuthoredStyle() const
{
    // These are the visible authored defaults from the AS driver. Poses are
    // deliberately excluded because the accepted wire value owns them.
    return PointLight->Intensity == 50000.0f &&
        PointLight->AttenuationRadius == 1200.0f &&
        PointLight->GetLightColor().Equals(
            FLinearColor(1.0f, 0.4f, 0.2f, 1.0f), 0.01f) &&
        SpotLight->Intensity == 100000.0f &&
        SpotLight->AttenuationRadius == 1500.0f &&
        SpotLight->InnerConeAngle == 20.0f &&
        SpotLight->OuterConeAngle == 35.0f &&
        SpotLight->GetLightColor().Equals(
            FLinearColor(0.3f, 0.7f, 1.0f, 1.0f), 0.01f) &&
        TextRender->Text.ToString() == TEXT("ENTITY") &&
        TextRender->WorldSize == 40.0f &&
        TextRender->TextRenderColor == FColor(255, 255, 0, 255) &&
        TextRender->HorizontalAlignment == EHorizTextAligment::EHTA_Center &&
        Arrow->ArrowColor == FColor(255, 0, 255, 255) &&
        Arrow->ArrowSize == 5.0f && Arrow->ArrowLength == 100.0f &&
        !Arrow->bIsScreenSizeScaled && !Arrow->bIsEditorOnly &&
        Box->GetUnscaledBoxExtent().Equals(FVector(80.0f), KINDA_SMALL_NUMBER) &&
        Sphere->GetUnscaledSphereRadius() == 80.0f &&
        Capsule->GetUnscaledCapsuleRadius() == 50.0f &&
        Capsule->GetUnscaledCapsuleHalfHeight() == 100.0f;
}

bool ACk_UnrealComponentGym_Presenter_UE::HasExpectedVisibilityState(
    bool bExpectSuspended) const
{
    if (IsHidden() != bExpectSuspended ||
        StaticMesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
    { return false; }
    for (int32 I = 0; I < SlotCount; ++I)
    {
        const USceneComponent* const Slot = GetSlot(I);
        if (Slot->IsVisible() == bExpectSuspended ||
            Slot->bHiddenInGame != bExpectSuspended)
        { return false; }
    }
    const ECollisionEnabled::Type ShapeCollision =
        bExpectSuspended || !bEnableClientShapeQueries
            ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly;
    return Box->GetCollisionEnabled() == ShapeCollision &&
        Sphere->GetCollisionEnabled() == ShapeCollision &&
        Capsule->GetCollisionEnabled() == ShapeCollision;
}

void ACk_UnrealComponentGym_Presenter_UE::HideKnownOwnedVisuals()
{
    // Actor-level hiding keeps the eight-slot reveal visually atomic. The
    // fixed-slot checks below never operate on a foreign replacement pointer.
    SetActorHiddenInGame(true);
    for (int32 I = 0; I < SlotCount; ++I)
    {
        if (!IsKnownSlot(I))
        { continue; }
        GetSlot(I)->SetVisibility(false);
        if (!IsKnownSlot(I))
        { continue; }
        GetSlot(I)->SetHiddenInGame(true);
        if (!IsKnownSlot(I))
        { continue; }
        if (UPrimitiveComponent* const Primitive =
                Cast<UPrimitiveComponent>(GetSlot(I)))
        {
            Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        }
    }
}

void ACk_UnrealComponentGym_Presenter_UE::FailClosed()
{
    bFailed = true;
    bSuspended = true;
    const bool bWasOperating = bInOperation;
    bInOperation = true;
    HideKnownOwnedVisuals();
    bInOperation = bWasOperating;
}

bool ACk_UnrealComponentGym_Presenter_UE::TryCheckHealth()
{
    if (bInOperation)
    {
        bFailed = true;
        return false;
    }
    if (bFailed)
    {
        FailClosed();
        return false;
    }
    bInOperation = true;
    const bool bHealthy = HasIntactCohort() &&
        (bSuspended || InstalledSequence > 0) &&
        HasExpectedVisibilityState(bSuspended);
    if (!bHealthy || bFailed)
    { FailClosed(); }
    bInOperation = false;
    return bHealthy && !bFailed;
}

bool ACk_UnrealComponentGym_Presenter_UE::Request_Suspend()
{
    if (bInOperation)
    {
        bFailed = true;
        return false;
    }
    if (bFailed)
    {
        FailClosed();
        return false;
    }
    bInOperation = true;
    if (!HasIntactCohort() ||
        (!bSuspended && InstalledSequence <= 0) ||
        !HasExpectedVisibilityState(bSuspended))
    {
        FailClosed();
        bInOperation = false;
        return false;
    }
    HideKnownOwnedVisuals();
    if (bFailed || !HasIntactCohort() ||
        !HasExpectedVisibilityState(true))
    {
        FailClosed();
        bInOperation = false;
        return false;
    }
    bSuspended = true;
    bInOperation = false;
    return true;
}

bool ACk_UnrealComponentGym_Presenter_UE::Request_CloseFailure()
{
    const bool bWasOpen = !bFailed;
    if (bInOperation)
    {
        bFailed = true;
        return bWasOpen;
    }
    FailClosed();
    return bWasOpen;
}

bool ACk_UnrealComponentGym_Presenter_UE::TryInstall(
    int32 Generation, int64 Sequence, const TArray<FTransform>& WorldPoses)
{
    if (bInOperation)
    {
        bFailed = true;
        return false;
    }
    if (bFailed)
    {
        FailClosed();
        return false;
    }
    bInOperation = true;
    const auto Abort = [this]() -> bool
    {
        FailClosed();
        bInOperation = false;
        return false;
    };

    bool bValidFrame = Generation > 0 && Sequence > 0 &&
        WorldPoses.Num() == SlotCount && Sequence > InstalledSequence &&
        Generation >= InstalledGeneration;
    if (bValidFrame)
    {
        for (const FTransform& Pose : WorldPoses)
        {
            if (!CkUnrealComponentGymPresenterPrivate::IsFinitePose(Pose))
            {
                bValidFrame = false;
                break;
            }
        }
    }
    if (!bValidFrame || !HasIntactCohort() ||
        (!bSuspended && InstalledSequence <= 0) ||
        !HasExpectedVisibilityState(bSuspended))
    { return Abort(); }

    // Freeze the caller's value array before invoking component setters that
    // could synchronously call back into its owner.
    FTransform Poses[SlotCount];
    for (int32 I = 0; I < SlotCount; ++I)
    { Poses[I] = WorldPoses[I]; }

    // Actor-level hiding is the visual commit barrier. Per-slot setters may
    // invoke callbacks, so recheck identity and terminal state after each.
    HideKnownOwnedVisuals();
    if (bFailed || !HasIntactCohort() || !IsHidden())
    { return Abort(); }
    for (int32 I = 0; I < SlotCount; ++I)
    {
        if (!IsKnownSlot(I))
        { return Abort(); }
        GetSlot(I)->SetWorldTransform(Poses[I], false, nullptr,
            ETeleportType::TeleportPhysics);
        if (bFailed || !HasIntactCohort() || !IsHidden())
        { return Abort(); }
    }
    for (int32 I = 0; I < SlotCount; ++I)
    {
        if (!IsKnownSlot(I) ||
            !GetSlot(I)->GetComponentTransform().Equals(Poses[I],
                KINDA_SMALL_NUMBER))
        { return Abort(); }
    }

    for (int32 I = 0; I < SlotCount; ++I)
    {
        if (!IsKnownSlot(I))
        { return Abort(); }
        GetSlot(I)->SetHiddenInGame(false);
        if (bFailed || !HasIntactCohort() || !IsHidden())
        { return Abort(); }
        GetSlot(I)->SetVisibility(true);
        if (bFailed || !HasIntactCohort() || !IsHidden())
        { return Abort(); }
    }
    if (bEnableClientShapeQueries)
    {
        if (!IsKnownSlot(5))
        { return Abort(); }
        Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        if (bFailed || !HasIntactCohort() || !IsHidden())
        { return Abort(); }
        if (!IsKnownSlot(6))
        { return Abort(); }
        Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        if (bFailed || !HasIntactCohort() || !IsHidden())
        { return Abort(); }
        if (!IsKnownSlot(7))
        { return Abort(); }
        Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        if (bFailed || !HasIntactCohort() || !IsHidden())
        { return Abort(); }
    }
    SetActorHiddenInGame(false);
    if (bFailed || !HasIntactCohort() ||
        !HasExpectedVisibilityState(false))
    { return Abort(); }
    InstalledGeneration = Generation;
    InstalledSequence = Sequence;
    bSuspended = false;
    bInOperation = false;
    return true;
}

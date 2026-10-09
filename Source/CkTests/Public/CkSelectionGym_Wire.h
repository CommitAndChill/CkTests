#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameplayTagContainer.h"
#include "CkStateMachine/StateMachine/CkStateMachine_Fragment_Data.h"
#include "CkTimer/CkTimer_Fragment_Data.h"
#include "UObject/SoftObjectPtr.h"
#include "CkSelectionGym_Wire.generated.h"

class UMaterialInterface;
class UStaticMesh;

UENUM(BlueprintType)
enum class ECkSelectionGym_WireLifecycle : uint8
{
    Initializing,
    Active,
    Retiring,
    Failed
};

UENUM(BlueprintType)
enum class ECkSelectionGym_VisualKind : uint8
{
    Invalid,
    LabelMarker,
    OcclusionWall
};

// Every key is an opaque, positive authority identity scoped by SourceGeneration.
// Zero parent means the current world's transient root; -1 means unset/invalid.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_NodeRow
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int64 Key = 0;
    UPROPERTY(BlueprintReadWrite) int64 LifetimeParentKey = -1;
    UPROPERTY(BlueprintReadWrite) int64 ContextRootKey = -1;
    UPROPERTY(BlueprintReadWrite) FName DebugName = NAME_None;
    UPROPERTY(BlueprintReadWrite) bool OwnContextRoot = false;
    UPROPERTY(BlueprintReadWrite) FGameplayTag RootLabel;
    UPROPERTY(BlueprintReadWrite) bool HasTransform = false;
    UPROPERTY(BlueprintReadWrite) FTransform WorldPose = FTransform::Identity;
    UPROPERTY(BlueprintReadWrite) bool HasSceneNode = false;
    UPROPERTY(BlueprintReadWrite) int64 SceneNodeParentKey = -1;
    UPROPERTY(BlueprintReadWrite) FTransform SceneNodeLocalOffset = FTransform::Identity;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_TimerFact
{
    GENERATED_BODY()

    // Owner is the entity whose canonical Timer provider enumerates timers.
    UPROPERTY(BlueprintReadWrite) int64 OwnerNodeKey = 0;
    UPROPERTY(BlueprintReadWrite) int64 TimerKey = 0;
    UPROPERTY(BlueprintReadWrite) FGameplayTag NameTag;
    UPROPERTY(BlueprintReadWrite) ECk_Timer_State State = ECk_Timer_State::Paused;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_IntegerFact
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int64 OwnerNodeKey = 0;
    UPROPERTY(BlueprintReadWrite) int64 AttributeKey = 0;
    UPROPERTY(BlueprintReadWrite) FGameplayTag NameTag;
    UPROPERTY(BlueprintReadWrite) int32 BaseValue = 0;
    UPROPERTY(BlueprintReadWrite) int32 FinalValue = 0;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_StateChainFact
{
    GENERATED_BODY()

    // Ordered depth-first as observed by the canonical StateMachine provider.
    UPROPERTY(BlueprintReadWrite) int32 Depth = -1;
    UPROPERTY(BlueprintReadWrite) FString StateClassName;
    UPROPERTY(BlueprintReadWrite) ECk_SmRunStatus RunStatus = ECk_SmRunStatus::Stopped;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_StateTransitionFact
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString FromStateName;
    UPROPERTY(BlueprintReadWrite) FString ToStateName;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_StateMachineFact
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int64 OwnerNodeKey = 0;
    UPROPERTY(BlueprintReadWrite) int64 StateMachineKey = 0;
    UPROPERTY(BlueprintReadWrite) bool HasDebugHistory = false;
    UPROPERTY(BlueprintReadWrite) TArray<FCkSelectionGym_StateChainFact> Chain;
    // Full current ring in source order. Local settings apply depth/display caps.
    UPROPERTY(BlueprintReadWrite) TArray<FCkSelectionGym_StateTransitionFact> History;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_VisualRow
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int64 VisualKey = 0;
    // Zero for the unpaired occlusion wall; positive for a labelled node marker.
    UPROPERTY(BlueprintReadWrite) int64 NodeKey = 0;
    UPROPERTY(BlueprintReadWrite) ECkSelectionGym_VisualKind Kind =
        ECkSelectionGym_VisualKind::Invalid;
    UPROPERTY(BlueprintReadWrite) FTransform ActorPose = FTransform::Identity;
    UPROPERTY(BlueprintReadWrite) FTransform MeshRelativePose = FTransform::Identity;
    UPROPERTY(BlueprintReadWrite) TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(BlueprintReadWrite) TSoftObjectPtr<UMaterialInterface> Material;
    UPROPERTY(BlueprintReadWrite) bool MeshVisible = false;
    UPROPERTY(BlueprintReadWrite) bool ActorCollisionEnabled = false;
    UPROPERTY(BlueprintReadWrite) TEnumAsByte<ECollisionEnabled::Type> MeshCollisionMode =
        ECollisionEnabled::NoCollision;
    UPROPERTY(BlueprintReadWrite) TEnumAsByte<ECollisionChannel> ObjectChannel =
        ECC_WorldStatic;
    UPROPERTY(BlueprintReadWrite) TEnumAsByte<ECollisionResponse> VisibilityResponse =
        ECR_Ignore;
    UPROPERTY(BlueprintReadWrite) FString LabelText;
    UPROPERTY(BlueprintReadWrite) FTransform LabelRelativePose = FTransform::Identity;
    UPROPERTY(BlueprintReadWrite) float LabelWorldSize = 0.0f;
    UPROPERTY(BlueprintReadWrite) FColor LabelColor = FColor::White;
};

// Values only: no authority FCk_Handle, AActor pointer, timer/SM fragment,
// provider object or client-local selection identity crosses the wire.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkSelectionGym_WireSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int32 SchemaVersion = 0;
    UPROPERTY(BlueprintReadWrite) int64 SourceGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 ScenarioGeneration = 0;
    UPROPERTY(BlueprintReadWrite) int64 Sequence = 0;
    UPROPERTY(BlueprintReadWrite) ECkSelectionGym_WireLifecycle Lifecycle =
        ECkSelectionGym_WireLifecycle::Initializing;
    UPROPERTY(BlueprintReadWrite) FString FailureReason;
    UPROPERTY(BlueprintReadWrite) int32 Scenario = -1;
    // Explicit source-owned fixture root; other direct transient roots remain
    // valid members of the same proven bank (notably scenario 0).
    UPROPERTY(BlueprintReadWrite) int64 ScenarioRootKey = 0;
    UPROPERTY(BlueprintReadWrite) FVector SourceOrigin = FVector::ZeroVector;
    UPROPERTY(BlueprintReadWrite) int32 DeclaredNodeCount = -1;
    UPROPERTY(BlueprintReadWrite) int32 DeclaredVisualCount = -1;
    UPROPERTY(BlueprintReadWrite) int32 DeclaredTimerCount = -1;
    UPROPERTY(BlueprintReadWrite) int32 DeclaredIntegerCount = -1;
    UPROPERTY(BlueprintReadWrite) int32 DeclaredStateMachineCount = -1;
    UPROPERTY(BlueprintReadWrite) TArray<FCkSelectionGym_NodeRow> Nodes;
    UPROPERTY(BlueprintReadWrite) TArray<FCkSelectionGym_VisualRow> Visuals;
    UPROPERTY(BlueprintReadWrite) TArray<FCkSelectionGym_TimerFact> Timers;
    UPROPERTY(BlueprintReadWrite) TArray<FCkSelectionGym_IntegerFact> Integers;
    UPROPERTY(BlueprintReadWrite) TArray<FCkSelectionGym_StateMachineFact> StateMachines;
};

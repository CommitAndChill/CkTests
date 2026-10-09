#pragma once

#include "CoreMinimal.h"
#include "CkCore/Enums/CkEnums.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkPmg/CkPmg_Fragment_Data.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkCueGym_SourceVisuals.generated.h"

class AActor;

UENUM(BlueprintType)
enum class ECkCueGym_RelayReadiness : uint8
{
    Invalid,
    Pending,
    Ready
};

UENUM(BlueprintType)
enum class ECkCueGym_SourceVisualOutcome : uint8
{
    Pending,
    Complete,
    Invalid
};

UENUM(BlueprintType)
enum class ECkCueGym_SourceVisualKind : uint8
{
    Invalid,
    Sphere,
    Box
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkCueGym_SourceVisualLine
{
    GENERATED_BODY()

    // PMG's actual entity-local outline input, not reconstructed from shape params.
    UPROPERTY(BlueprintReadOnly) FVector Start = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector End = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) float Thickness = 0.0f;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkCueGym_SourceVisualRow
{
    GENERATED_BODY()

    // Full EnTT entity ID including its version, plus one; scoped by the AS
    // generation/role epoch. Never derive this from an ephemeral PMG shape.
    UPROPERTY(BlueprintReadOnly) int64 Key = 0;
    UPROPERTY(BlueprintReadOnly) int32 Role = -1;
    UPROPERTY(BlueprintReadOnly) ECkCueGym_SourceVisualKind Kind = ECkCueGym_SourceVisualKind::Invalid;
    // Version 1 reconstructs only exact authored PMG sphere/box outlines.
    UPROPERTY(BlueprintReadOnly) int32 OutlineRecipeVersion = 0;
    UPROPERTY(BlueprintReadOnly) FTransform Pose = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) float Radius = 0.0f;
    UPROPERTY(BlueprintReadOnly) FVector Extent = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) int32 Segments = 0;
    UPROPERTY(BlueprintReadOnly) int32 Rings = 0;
    UPROPERTY(BlueprintReadOnly) ECk_Plane_Axis Axis = ECk_Plane_Axis::XY;
    UPROPERTY(BlueprintReadOnly) bool DrawLines = true;
    UPROPERTY(BlueprintReadOnly) ECk_Pmg_RenderMode RenderMode = ECk_Pmg_RenderMode::DoubleSided;
    UPROPERTY(BlueprintReadOnly) bool VisibleByRenderMode = true;
    UPROPERTY(BlueprintReadOnly) bool EnableCollision = false;
    UPROPERTY(BlueprintReadOnly) float LineThickness = 0.0f;
    UPROPERTY(BlueprintReadOnly) double Duration = 0.0;
    // Authority/local exact source witness. The wire carries the proven recipe.
    UPROPERTY(BlueprintReadOnly, NotReplicated) TArray<FCkCueGym_SourceVisualLine> Lines;
};

UCLASS()
class CKTESTS_API UCkCueGym_SourceVisuals : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // A passive authority-world observation. Pending never grants a source
    // serial; Invalid requires the caller to close its source epoch.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue")
    static ECkCueGym_RelayReadiness Get_RelayReadiness(
        UWorld* InWorld, AActor* InSourceState);

    // Monotonic wall clock for a single per-epoch channel wait deadline.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Cue")
    static double Get_RelayWallClockSeconds();

    // Clears outputs first. Complete alone permits publication. Pending means
    // a legitimate unsettled cue/PMG frame. Invalid closes the caller's world.
    // Caller handles GS, epoch, and closed-world checks before and after query.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Cue")
    static ECkCueGym_SourceVisualOutcome TryGet_SourceVisuals(
        UWorld* InWorld, const FCk_Handle& InSourceOwner,
        const TArray<FCk_Handle>& InRoleRoots,
        TArray<FCkCueGym_SourceVisualRow>& OutRows,
        FString& OutFailureReason);

    // Atomic, clear-first expansion for received value rows. An input with
    // local witness lines must match the versioned recipe exactly.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Cue")
    static bool TryExpand_SourceVisualRows(
        const TArray<FCkCueGym_SourceVisualRow>& InRows,
        TArray<FCkCueGym_SourceVisualRow>& OutRows);
};

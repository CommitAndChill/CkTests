#pragma once

#include "CoreMinimal.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkPoiDisplayDefinition/CkPoiDisplayDefinition_Fragment_Data.h"
#include "CkPmg/CkPmg_Fragment_Data.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkCompassGym_SourceCatalog.generated.h"

class AActor;
class UTexture2D;

UENUM(BlueprintType)
enum class ECkCompassGym_SourceKind : uint8
{
    Invalid, Ring, Waypoint, Stress, Ping
};

UENUM(BlueprintType)
enum class ECkCompassGym_CatalogOutcome : uint8
{
    Pending, Complete, Invalid
};

// Authority-local retained first returns. Never replicate this ledger.
USTRUCT(BlueprintType)
struct CKTESTS_API FCkCompassGym_SourceReceipt
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadWrite) int64 CatalogId = 0;
    UPROPERTY(BlueprintReadWrite) ECkCompassGym_SourceKind Kind = ECkCompassGym_SourceKind::Invalid;
    UPROPERTY(BlueprintReadWrite) FCk_Handle Host;
    UPROPERTY(BlueprintReadWrite) FCk_Handle Marker;
    UPROPERTY(BlueprintReadWrite) FCk_Handle Timer;
    UPROPERTY(BlueprintReadWrite) FGameplayTag ExpectedCategory;
    // Set only after one Complete observation; missing materialization then fails closed.
    UPROPERTY(BlueprintReadWrite) bool Admitted = false;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkCompassGym_SourceLine
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FVector Start = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector End = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) float Thickness = 0.0f;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkCompassGym_SourceMarker
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FTransform Pose = FTransform::Identity;
    UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) float Radius = 0.0f;
    UPROPERTY(BlueprintReadOnly) int32 Segments = 0;
    UPROPERTY(BlueprintReadOnly) int32 Rings = 0;
    UPROPERTY(BlueprintReadOnly) ECk_Plane_Axis Axis = ECk_Plane_Axis::XY;
    UPROPERTY(BlueprintReadOnly) bool DrawLines = false;
    UPROPERTY(BlueprintReadOnly) float LineThickness = 0.0f;
    UPROPERTY(BlueprintReadOnly) double Duration = 0.0;
    UPROPERTY(BlueprintReadOnly) ECk_Pmg_RenderMode RenderMode = ECk_Pmg_RenderMode::DoubleSided;
    UPROPERTY(BlueprintReadOnly) bool VisibleByRenderMode = true;
    UPROPERTY(BlueprintReadOnly) bool EnableCollision = false;
    // Local source witness. A later wire may use a verified compact recipe.
    UPROPERTY(BlueprintReadOnly, NotReplicated) TArray<FCkCompassGym_SourceLine> Lines;
};

USTRUCT(BlueprintType)
struct CKTESTS_API FCkCompassGym_SourceRow
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) int64 CatalogId = 0;
    UPROPERTY(BlueprintReadOnly) ECkCompassGym_SourceKind Kind = ECkCompassGym_SourceKind::Invalid;
    UPROPERTY(BlueprintReadOnly) FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FGameplayTag Category;
    UPROPERTY(BlueprintReadOnly) bool HasDisplay = false;
    UPROPERTY(BlueprintReadOnly) FGameplayTag Consumer;
    UPROPERTY(BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Icon;
    UPROPERTY(BlueprintReadOnly) int32 Priority = 0;
    UPROPERTY(BlueprintReadOnly) ECk_Poi_OffscreenPolicy OffscreenPolicy = ECk_Poi_OffscreenPolicy::Hide;
    UPROPERTY(BlueprintReadOnly) FLinearColor Tint = FLinearColor::White;
    UPROPERTY(BlueprintReadOnly) FVector2D SizeHint = FVector2D(32.0, 32.0);
    UPROPERTY(BlueprintReadOnly) bool HasMarker = false;
    UPROPERTY(BlueprintReadOnly) FCkCompassGym_SourceMarker Marker;
};

UCLASS()
class CKTESTS_API UCkCompassGym_SourceCatalog : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Clears both outputs first. Caller validates its source epoch and closed-world
    // state before and after this passive read, and bounds Pending in wall time.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Compass")
    static ECkCompassGym_CatalogOutcome TryGet_SourceCatalog(
        UWorld* World, AActor* ExactGameState, const FCk_Handle& SourceRoot,
        bool CompositionComplete,
        const TArray<FCkCompassGym_SourceReceipt>& Receipts,
        TArray<FCkCompassGym_SourceRow>& OutRows, FString& OutFailureReason);
};

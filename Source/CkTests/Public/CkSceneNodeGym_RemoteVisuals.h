#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CkSceneNodeGym_FrameSampler.h"
#include "CkEcs/Handle/CkHandle.h"
#include "CkSceneNodeGym_RemoteVisuals.generated.h"
class AGameStateBase;
class UWorld;
UENUM(BlueprintType)
enum class ECk_SceneNodeGym_RemoteVisualState : uint8
{ Building, Ready, Retiring, Complete, Unproven };

// A client receipt binds source actor channels. It creates no actors, ECS entities,
// simulation roles or pose transport. Captured serials survive presentation closure.
UCLASS(NotBlueprintable)
class CKTESTS_API UCk_SceneNodeGym_RemoteVisuals_UE final : public UActorComponent
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_RemoteVisualRuntime_UE;
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FCk_SceneNodeRemoteVisuals_TestAccess;
#endif
    struct FRole
    {
        TWeakObjectPtr<AActor> Actor;
        FCk_Handle Entity;
        bool bCaptured = false;
        bool bFinished = false;
    };
    TWeakObjectPtr<AGameStateBase> Source;
    TWeakObjectPtr<UWorld> ClientWorld;
    UPROPERTY(Transient) TObjectPtr<AActor> FrozenScopeIdentity;
    UPROPERTY(Transient) TArray<TSubclassOf<AActor>> VisualClasses;
    TArray<FRole> Roles;
    int32 Generation = 0;
    int64 InstalledSequence = 0;
    int64 InstalledFrameSerial = 0;
    FCkSceneNodeGym_Frame InstalledFrame;
    ECk_SceneNodeGym_RemoteVisualState State = ECk_SceneNodeGym_RemoteVisualState::Building;
    FName FailureReason = NAME_None;
    bool bClosed = false;
public:
    UCk_SceneNodeGym_RemoteVisuals_UE();
private:
    void Fail(FName Reason, const AActor* Rejected = nullptr);
};
UCLASS()
class CKTESTS_API UCk_SceneNodeGym_RemoteVisualRuntime_UE final : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
    friend class UCk_SceneNodeGym_RemoteVisuals_UE;
public:
    // Lifecycle/display metadata only. Nonempty poses are rejected.
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool TryMakeFrame(int32 Generation, int64 FrameSerial,
        const TArray<FTransform>& WorldPoses, FCkSceneNodeGym_Frame& OutFrame);
    // Waits without allocation until all source identities and applied outputs are ready.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_RemoteVisuals_UE* TryBegin(AGameStateBase* ClientState,
        int64 Sequence, const FCkSceneNodeGym_Frame& Received,
        const TArray<TSubclassOf<AActor>>& VisualClasses);
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool TryInstall(UCk_SceneNodeGym_RemoteVisuals_UE* Receipt,
        int64 Sequence, const FCkSceneNodeGym_Frame& Received);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool Get_IsInstalledConformant(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt,
        int64 Sequence, const FCkSceneNodeGym_Frame& Received);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static bool TryGet_FinishedRole(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt,
        int32 Role, AActor*& OutActor);
    // Visibility is presentation only; never writes a pose or adopts/destroys a replica.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static void Request_SetVisualVisible(AActor* Actor, bool Visible);
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool Request_Retire(UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    // Explicit structural closure and physical retirement observation, including frozen metadata.
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static bool Request_Advance(UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static ECk_SceneNodeGym_RemoteVisualState Get_State(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static FName Get_FailureReason(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int32 Get_Generation(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int64 Get_InstalledSequence(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    UFUNCTION(BlueprintPure, Category="Ck|Tests|Gym|SceneNode")
    static int64 Get_InstalledFrameSerial(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
#if WITH_DEV_AUTOMATION_TESTS
    // Read-only explanation of the production applied-pose prerequisite.
    static FString DescribeAppliedPose(TWeakObjectPtr<const AActor> Actor);
#endif
    UFUNCTION(BlueprintCallable, Category="Ck|Tests|Gym|SceneNode")
    static UCk_SceneNodeGym_RemoteVisuals_UE* TryBeginSuccessor(AGameStateBase* ClientState,
        UCk_SceneNodeGym_RemoteVisuals_UE* CompleteReceipt,
        int64 Sequence, const FCkSceneNodeGym_Frame& Received,
        const TArray<TSubclassOf<AActor>>& VisualClasses);
private:
    static bool HasClientContext(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    static bool HasSafeFrame(int64 Sequence, const FCkSceneNodeGym_Frame& Frame);
    static bool HasSafeClasses(const TArray<TSubclassOf<AActor>>& Classes);
    static bool HasNoOtherRemoteReceipt(AGameStateBase* ClientState, UCk_SceneNodeGym_RemoteVisuals_UE* Allowed = nullptr);
    static bool HasExactCurrentRoles(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt, bool bRequireComplete);
    static bool HasNoUnknownDescendants(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
    static bool HasAllRecordedActorsAbsent(const UCk_SceneNodeGym_RemoteVisuals_UE* Receipt);
};

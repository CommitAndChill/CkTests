#pragma once

#include "CoreMinimal.h"

#include <GameFramework/GameModeBase.h>
#include <GameFramework/GameStateBase.h>
#include <Engine/LocalPlayer.h>
#include <Containers/Ticker.h>
#include <Subsystems/GameInstanceSubsystem.h>
#include <Templates/SubclassOf.h>

#include "CkGym_Registry.generated.h"

class ACkGym_PlayerController_UE;
class AGameStateBase;
class ULocalPlayer;
class UWorld;

UENUM(BlueprintType)
enum class ECkGym_TravelResult : uint8
{
    None, Queued, Accepted, Rejected, Landed, TravelFailed, TimedOut,
};

// --------------------------------------------------------------------------------------------------------------------
//
// The gym registry: one entry per registered gym, owned per-GameInstance so it survives ServerTravel
// between gyms but stays isolated per PIE client (a module-static store would collapse multi-client
// PIE instances into one registry).
//
// Registration happens at gym GameMode BeginPlay (CkTests_Gyms::RegisterAll / BB_Gyms::RegisterAll
// via the CkGym_Cycler AngelScript facade), which is why plain runtime subsystem lifetime suffices.
//
// Fields are public UPROPERTYs on purpose: AngelScript HUD code reads them directly by name
// (Entry.DisplayName), matching the AS struct this replaced.
//
// --------------------------------------------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct CKTESTS_API FCkGym_Entry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSubclassOf<AGameModeBase> GameModeClass;

    // Optional per-gym level override. When empty, falls back to the subsystem's GymLevelName.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString LevelName;

    // Grouping key for the gym switchboard, conventionally the owning module's name ("CkCrowd").
    // Empty lands the gym in the fallback bucket at display time; the stored value stays empty.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Category;

    // Optional stable hint-code override for hint-select mode. Empty = derived from DisplayName.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString HintCode;

    // Authored selector annotation; this does not certify multiplayer readiness.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool HasMultiplayerComponent = false;
};

// --------------------------------------------------------------------------------------------------------------------

UCLASS(NotBlueprintable)
class CKTESTS_API UCkGym_Registry_Subsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;

    // Dedupes by DisplayName so duplicate registrations on hot-reload or level re-entry are idempotent.
    auto
    Register(
        const FCkGym_Entry& InEntry) -> void;

    auto
    Get_Entries() const -> const TArray<FCkGym_Entry>&;

    auto
    Get_CurrentGymIndex() const -> int32;

    auto
    Get_GymLevelName() const -> FString;

    auto
    Get_SuppressHUDDuringStartup() const -> bool;

    auto
    Set_SuppressHUDDuringStartup(
        bool InSuppress) -> void;

    auto
    Get_HasConsumedStartupResolve() const -> bool;

    auto
    MarkStartupResolveConsumed() -> void;

    auto
    Find_GymIndexByName(
        const FString& InName) const -> int32;

    // Replaces a client replica only after validating the complete published catalog.
    auto
    TryReplaceReplica(
        const UWorld* InWorld,
        const TArray<FCkGym_Entry>& InEntries,
        int32 InCurrentIndex,
        bool InSuppressHUD) -> bool;

    static auto
    IsValidCatalog(
        const TArray<FCkGym_Entry>& InEntries,
        int32 InCurrentIndex) -> bool;

    // Called only after the destination GameMode has actually landed.
    auto
    TryRecordLandedGym(
        const UWorld* InWorld) -> bool;

    // A successful return means UWorld accepted ServerTravel; current/recents change on landing.
    auto
    TryRequest_TravelToGym(
        UWorld* InWorld,
        int32 InIndex,
        ACkGym_PlayerController_UE* InRequester = nullptr,
        int32 InRequestId = 0) -> bool;

    auto
    TryRequest_TravelToGymByName(
        UWorld* InWorld,
        const FString& InName,
        ACkGym_PlayerController_UE* InRequester = nullptr,
        int32 InRequestId = 0) -> bool;

    auto TryBeginLocalTravel(ULocalPlayer* InPlayer, const FString& InName, int32& OutRequestId) -> bool;
    auto TryApplyLocalTravelResult(ULocalPlayer* InPlayer, int32 InRequestId,
        ECkGym_TravelResult InResult, const FString& InDetail) -> bool;
    auto GetLocalTravelResult(const ULocalPlayer* InPlayer) const -> ECkGym_TravelResult;
    auto GetLocalTravelDetail(const ULocalPlayer* InPlayer) const -> FString;
    auto ResolveLocalTravelOnCatalog(const UWorld* InWorld, const AGameStateBase* InState,
        const TArray<FCkGym_Entry>& InEntries, int32 InCurrentIndex) -> void;

    // ServerTravels to the entry at the (wrapped) index, persisting CurrentGymIndex, LastGymName,
    // and the per-user recents list.
    auto
    Request_TravelToGym(
        int32 InIndex) -> void;

public:
    // Pure helpers, extracted so the arithmetic is unit-testable without a GameInstance.

    // Wraps any index into [0, InNum); undefined for InNum <= 0 (callers guard on an empty registry).
    static auto
    Get_WrappedIndex(
        int32 InIndex,
        int32 InNum) -> int32;

    // The recents list after visiting InVisited: most-recent-first, deduped (a re-visit moves to
    // the front), capped at InCap.
    static auto
    Get_RecentsAfterVisit(
        const TArray<FString>& InRecents,
        const FString& InVisited,
        int32 InCap) -> TArray<FString>;

    static auto ChooseLandedIndex(const TArray<FCkGym_Entry>& InEntries,
        const UClass* InLandedGameModeClass, const FString& InLandedLevel,
        const FString& InPendingName, const FString& InDefaultLevel) -> int32;

private:
    auto HasReadableReplica() const -> bool;

    UPROPERTY(Transient)
    TArray<FCkGym_Entry> _Entries;

    int32 _CurrentGymIndex = -1;

    // Default level used when a gym entry doesn't specify its own LevelName.
    FString _GymLevelName = TEXT("TestGyms_CkTests_Level");

    // Set by ACk_Gym_Base_GameMode::BeginPlay when it has decided to auto-travel to a startup gym;
    // cleared when the destination loads (and on no-travel paths). HUDs skip drawing while true so
    // the cycler menu doesn't flash on the launcher level during the transition.
    bool _SuppressHUDDuringStartup = false;

    // The startup-gym preference is a boot-time redirect, consumed exactly once per game session.
    // Without this, the base GameMode's BeginPlay (which fires on EVERY gym travel, not just boot)
    // would re-resolve the preference and yank the user back to the pinned gym whenever they travel
    // anywhere else. GameInstance lifetime = survives ServerTravel, resets per PIE run.
    bool _StartupResolveConsumed = false;

    FString _PendingGymName;
    TWeakObjectPtr<UWorld> _PendingSourceWorld;
    TWeakObjectPtr<ACkGym_PlayerController_UE> _PendingRequester;
    int32 _PendingRequestId = 0;
    double _PendingDeadlineSeconds = 0.0;
    FDelegateHandle _TravelFailureHandle;
    FTSTicker::FDelegateHandle _PendingTickerHandle;

    struct FLocalTravelTicket
    {
        int32 RequestId = 0;
        FString Name;
        TWeakObjectPtr<UWorld> SourceWorld;
        TWeakObjectPtr<AGameStateBase> SourceState;
        ECkGym_TravelResult Result = ECkGym_TravelResult::None;
        FString Detail;
        double DeadlineSeconds = 0.0;
    };
    TMap<TWeakObjectPtr<ULocalPlayer>, FLocalTravelTicket> _LocalTickets;
    int32 _NextLocalRequestId = 0;
    FTSTicker::FDelegateHandle _LocalTicketTickerHandle;

    auto ClearPendingTravel() -> void;
    auto HandlePendingTravelFailure(UWorld* InWorld, const FString& InError) -> void;
    auto PollPendingTravel(float InDeltaSeconds) -> bool;
    auto PollLocalTravelTickets(float InDeltaSeconds) -> bool;
};

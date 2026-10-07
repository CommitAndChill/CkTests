#include "CkGymRegistry_Utils.h"

#include "CkGymStartup_Utils.h"
#include "CkGym_NetworkRuntime.h"
#include "CkTests/CkTests_Log.h"

#include "CkCore/Macros/CkMacros.h"

#include <Engine/GameInstance.h>
#include <Engine/World.h>
#include <Engine/LocalPlayer.h>
#include <GameFramework/Pawn.h>

namespace
{
    auto HasReadableGymCatalog(const UObject* InContext) -> bool
    {
        const auto* World = IsValid(InContext) ? InContext->GetWorld() : nullptr;
        if (NOT IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        if (World->GetNetMode() != NM_Client)
        { return true; }
        const auto* State = World->GetGameState<ACkGym_GameState_UE>();
        return IsValid(State) && State->Get_HasPublishedCatalog();
    }

    auto IsExplicitPlayerContext(const UObject* InContext) -> bool
    {
        return IsValid(Cast<ACkGym_PlayerController_UE>(InContext)) ||
            IsValid(Cast<ULocalPlayer>(InContext)) || IsValid(Cast<APawn>(InContext));
    }
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_Utils_GymRegistry_UE::
    Request_RegisterGym(
        const UObject* InWorldContextObject,
        const FString& InDisplayName,
        TSubclassOf<AGameModeBase> InGameModeClass,
        const FString& InLevelName,
        const FString& InCategory)
    -> void
{
    auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    {
        ck::tests::Warning(TEXT("[GymRegistry] Request_RegisterGym called but subsystem not available (DisplayName={})"),
            InDisplayName);
        return;
    }

    auto Entry = FCkGym_Entry{};
    Entry.DisplayName = InDisplayName;
    Entry.GameModeClass = InGameModeClass;
    Entry.LevelName = InLevelName;
    Entry.Category = InCategory;

    Subsystem->Register(Entry);
}

auto
    UCk_Utils_GymRegistry_UE::
    Get_GymRegistry(
        const UObject* InWorldContextObject)
    -> TArray<FCkGym_Entry>
{
    const auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem) || NOT HasReadableGymCatalog(InWorldContextObject))
    { return {}; }

    return Subsystem->Get_Entries();
}

auto
    UCk_Utils_GymRegistry_UE::
    Get_CurrentGymIndex(
        const UObject* InWorldContextObject)
    -> int32
{
    const auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem) || NOT HasReadableGymCatalog(InWorldContextObject))
    { return INDEX_NONE; }

    return Subsystem->Get_CurrentGymIndex();
}

auto
    UCk_Utils_GymRegistry_UE::
    Get_SuppressHUDDuringStartup(
        const UObject* InWorldContextObject)
    -> bool
{
    const auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem) || NOT HasReadableGymCatalog(InWorldContextObject))
    { return true; }

    return Subsystem->Get_SuppressHUDDuringStartup();
}

auto
    UCk_Utils_GymRegistry_UE::
    Request_Set_SuppressHUDDuringStartup(
        const UObject* InWorldContextObject,
        bool InSuppress)
    -> void
{
    auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return; }

    Subsystem->Set_SuppressHUDDuringStartup(InSuppress);
}

auto
    UCk_Utils_GymRegistry_UE::
    Find_GymIndexByName(
        const UObject* InWorldContextObject,
        const FString& InName)
    -> int32
{
    const auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem) || NOT HasReadableGymCatalog(InWorldContextObject))
    { return INDEX_NONE; }

    return Subsystem->Find_GymIndexByName(InName);
}

auto
    UCk_Utils_GymRegistry_UE::
    Get_HasConsumedStartupResolve(
        const UObject* InWorldContextObject)
    -> bool
{
    const auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return false; }

    return Subsystem->Get_HasConsumedStartupResolve();
}

auto
    UCk_Utils_GymRegistry_UE::
    Request_MarkStartupResolveConsumed(
        const UObject* InWorldContextObject)
    -> void
{
    auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return; }

    Subsystem->MarkStartupResolveConsumed();
}

auto
    UCk_Utils_GymRegistry_UE::
    Resolve_StartupGymIndex(
        const UObject* InWorldContextObject)
    -> int32
{
    const auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return INDEX_NONE; }

    switch (UCk_Utils_GymStartup_UE::Get_StartupMode())
    {
        case ECkGym_StartupMode::Default:
            return Subsystem->Find_GymIndexByName(UCk_Utils_GymStartup_UE::Get_DefaultGymName());
        case ECkGym_StartupMode::Last:
            return Subsystem->Find_GymIndexByName(UCk_Utils_GymStartup_UE::Get_LastGymName());
        case ECkGym_StartupMode::Cycler:
        default:
            return INDEX_NONE;
    }
}

auto
    UCk_Utils_GymRegistry_UE::
    Request_TravelToGym(
        const UObject* InWorldContextObject,
        int32 InIndex)
    -> void
{
    auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return; }
    const auto* World = InWorldContextObject->GetWorld();
    if (IsExplicitPlayerContext(InWorldContextObject))
    {
        const auto& Entries = Subsystem->Get_Entries();
        if (Entries.IsEmpty())
        { return; }
        const auto WrappedIndex = (InIndex % Entries.Num() + Entries.Num()) % Entries.Num();
        TryRequest_TravelToGymFromPlayer(InWorldContextObject, Entries[WrappedIndex].DisplayName);
        return;
    }
    if (IsValid(World) && World->GetNetMode() != NM_Client)
    { Subsystem->Request_TravelToGym(InIndex); }
}

auto
    UCk_Utils_GymRegistry_UE::
    Request_TravelToGymByName(
        const UObject* InWorldContextObject,
        const FString& InName)
    -> void
{
    auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return; }

    const auto Index = Subsystem->Find_GymIndexByName(InName);
    if (Index == INDEX_NONE)
    {
        ck::tests::Warning(TEXT("[GymRegistry] Request_TravelToGymByName: no gym registered with DisplayName '{}'"),
            InName);
        return;
    }

    Request_TravelToGym(InWorldContextObject, Index);
}

auto
    UCk_Utils_GymRegistry_UE::
    TryRequest_TravelToGymFromPlayer(
        const UObject* InPlayerContext,
        const FString& InName)
    -> bool
{
    if (NOT IsValid(InPlayerContext) ||
        InPlayerContext->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) || InName.IsEmpty())
    { return false; }

    const auto* ExplicitController = Cast<ACkGym_PlayerController_UE>(InPlayerContext);
    const auto* ExplicitPlayer = Cast<ULocalPlayer>(InPlayerContext);
    const auto* ExplicitPawn = Cast<APawn>(InPlayerContext);
    auto* World = InPlayerContext->GetWorld();
    if (NOT IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        NOT World->IsGameWorld())
    { return false; }

    auto* Controller = const_cast<ACkGym_PlayerController_UE*>(ExplicitController);
    if (IsValid(ExplicitPlayer))
    {
        Controller = Cast<ACkGym_PlayerController_UE>(ExplicitPlayer->GetPlayerController(World));
        if (NOT IsValid(Controller) || Controller->GetLocalPlayer() != ExplicitPlayer)
        { return false; }
    }
    else if (IsValid(ExplicitPawn))
    {
        Controller = Cast<ACkGym_PlayerController_UE>(ExplicitPawn->GetController());
        if (NOT IsValid(Controller) || Controller->GetPawn() != ExplicitPawn)
        { return false; }
    }
    else if (NOT IsValid(ExplicitController))
    { return false; }

    const auto ControllerIsValid = IsValid(Controller) &&
        NOT Controller->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) &&
        Controller->GetWorld() == World &&
        Controller->IsLocalController() && IsValid(Controller->GetLocalPlayer());
    auto* State = ControllerIsValid ? World->GetGameState<ACkGym_GameState_UE>() : nullptr;
    auto* Registry = ControllerIsValid ? DoGet_Subsystem(Controller) : nullptr;
    if (NOT ControllerIsValid || NOT IsValid(State) || NOT State->Get_HasPublishedCatalog() ||
        NOT IsValid(Registry) || Registry->Find_GymIndexByName(InName) == INDEX_NONE)
    { return false; }

    return Controller->TryQueueTravelRequest(InName);
}

auto
    UCk_Utils_GymRegistry_UE::
    Request_NextGym(
        const UObject* InWorldContextObject)
    -> void
{
    auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return; }

    Request_TravelToGym(InWorldContextObject, Subsystem->Get_CurrentGymIndex() + 1);
}

auto
    UCk_Utils_GymRegistry_UE::
    Request_PrevGym(
        const UObject* InWorldContextObject)
    -> void
{
    auto* Subsystem = DoGet_Subsystem(InWorldContextObject);
    if (NOT IsValid(Subsystem))
    { return; }

    Request_TravelToGym(InWorldContextObject, Subsystem->Get_CurrentGymIndex() - 1);
}

// --------------------------------------------------------------------------------------------------------------------

auto
    UCk_Utils_GymRegistry_UE::
    DoGet_Subsystem(
        const UObject* InWorldContextObject)
    -> UCkGym_Registry_Subsystem*
{
    if (NOT IsValid(InWorldContextObject))
    { return nullptr; }

    const auto* World = InWorldContextObject->GetWorld();
    if (NOT IsValid(World))
    { return nullptr; }

    auto* GameInstance = World->GetGameInstance();
    if (NOT IsValid(GameInstance))
    { return nullptr; }

    return GameInstance->GetSubsystem<UCkGym_Registry_Subsystem>();
}

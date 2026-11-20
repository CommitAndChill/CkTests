#include "CkTests/HexGrid/CkHexPersistenceReadinessProbe.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Persistence/CkPersistenceInitialization.h"
#include "CkGrid/HexGridSystem/Grid/CkHexGrid_Utils.h"
#include "CkGrid/HexGridSystem/Occupancy/CkHexGrid_Projection.h"
#include "CkGrid/HexGridSystem/Persistence/CkHexGridPersistence_Fragment_Data.h"
#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Utils.h"
#include "CkSnapshot/CkSnapshot_Utils.h"

auto UCk_HexPersistenceReadinessProbe::Construct(FCk_Handle& InHandle, const FInstancedStruct& InSpawnParams)
    -> ECk_EntityScript_ConstructionFlow
{
    const auto Flow = Super::Construct(InHandle, InSpawnParams);
    auto Board = UCk_Utils_HexGrid_UE::Cast(InHandle);
    if (!ck::IsValid(Board) || !UCk_Utils_Snapshot_UE::Get_IsLoadInProgress(InHandle)) { return Flow; }
    AwaitingAtConstruct = UCk_Utils_HexGrid_UE::Get_Readiness(Board).Get_State() == ECk_HexGridReadiness::AwaitingRestore;
    FCk_Delegate_HexGrid_ReadinessChanged Ready;
    Ready.BindDynamic(this, &ThisClass::OnReadiness);
    UCk_Utils_HexGrid_UE::BindTo_OnReadinessChanged(Board, Ready);
    FCk_Delegate_Hydration_OnHydrated Hydrated;
    Hydrated.BindDynamic(this, &ThisClass::OnHydrated);
    UCk_Utils_Snapshot_UE::Promise_OnHydrated(InHandle, Hydrated);

    const TWeakObjectPtr<UCk_HexPersistenceReadinessProbe> Weak{this};
    ck::persistence::Subscribe(InHandle, FCk_RepData_HexGridState::StaticStruct(), TEXT("HexReadinessProbe"),
        [Weak](FCk_Handle Entity, const ck::persistence::FInitializationStatus& Status)
        {
            if (!Weak.IsValid() || Status.Get_Disposition() != ck::persistence::EInitializationDisposition::BaselineCommitted)
            { return; }
            const auto Grid = UCk_Utils_HexGrid_UE::Cast(Entity);
            Weak->CommittedWhileAwaiting = !Status.Get_IsReleased() &&
                UCk_Utils_HexGrid_UE::Get_Readiness(Grid).Get_State() == ECk_HexGridReadiness::AwaitingRestore;
            const auto Projection = ck::hex_grid::ReadCommittedProjection(Grid);
            Weak->EmptyAtCommit = Projection._Placements.IsEmpty() && Projection._Blockers.IsEmpty() &&
                Projection._ManualBlocks.IsEmpty() && Projection._LocalTags.IsEmpty();
        });

    FCk_Request_HexGridPlacement_Remove Request{FGuid{9,0,0,1}};
    FCk_Delegate_HexGridPlacement_OnResult Result;
    Result.BindDynamic(this, &ThisClass::OnRequest);
    Request.Set_OnResult(Result);
    UCk_Utils_HexGridPlacement_UE::Request_Remove(Board, Request, {});

    FCk_Handle_HexGrid Fresh;
    ECk_HexBoardFailure Failure{};
    FreshChildReady = UCk_Utils_HexGrid_UE::TryCreate(InHandle, FTransform::Identity, FCk_HexGrid_Spec{}, Fresh, Failure)
        && UCk_Utils_HexGrid_UE::Get_IsReady(Fresh);
    return Flow;
}

void UCk_HexPersistenceReadinessProbe::OnReadiness(FCk_Handle_HexGrid InGrid, const FCk_HexGridReadiness_Result& InResult)
{
    if (InResult.Get_State() != ECk_HexGridReadiness::Ready && InResult.Get_State() != ECk_HexGridReadiness::Unavailable)
    { return; }
    ++TerminalNotifications;
    EarlyTerminalNotification |= !ck::persistence::Query(InGrid, FCk_RepData_HexGridState::StaticStruct()).Get_IsReleased();
}

void UCk_HexPersistenceReadinessProbe::OnRequest(const FCk_HexGridPlacement_Result& InResult)
{
    RequestRejectedNotReady = !InResult.Get_Succeeded() && InResult.Get_Failure() == ECk_HexGridFailure::NotReady;
}

void UCk_HexPersistenceReadinessProbe::OnHydrated(FCk_Handle InHandle)
{
    ++HydratedNotifications;
    HydratedSawReady = UCk_Utils_HexGrid_UE::Get_IsReady(UCk_Utils_HexGrid_UE::Cast(InHandle));
}

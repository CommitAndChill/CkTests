#pragma once

#include "CkTests/Net/CkAutoTest_NetSubject_MixedGrid.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CkHexDenseAcceptance.generated.h"

UCLASS()
class UCk_HexDenseAcceptance_UE : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    bool ShouldCreateSubsystem(UObject* Outer) const override;
    void Initialize(FSubsystemCollectionBase& Collection) override;
    void Deinitialize() override;

private:
    using FBoard = ACk_AutoTest_NetSubject_DenseHexBoard_UE;
    using FPiece = ACk_AutoTest_NetSubject_MixedGridObject_UE;
    enum class EStep { Boot, Spawn, Fill, Capture, Restore, Join, Drain, Done };
    enum class ERestoreControl { None, EmptyBoards, UnplacedActors };
    ERestoreControl RestoreControl = ERestoreControl::None;
    EStep Step = EStep::Boot;
    FTSTicker::FDelegateHandle Ticker;
    TArray<TWeakObjectPtr<FBoard>> Boards;
    TArray<TWeakObjectPtr<FPiece>> Pieces;
    TArray<FCk_Handle> LifetimeWitnesses;
    TWeakObjectPtr<UWorld> PreviousWorld;
    TArray<FCk_HexCoord> Coordinates;
    TMap<FCk_HexCoord,int32> Indices;
    FString Directory, ManifestHash;
    FName Slot;
    int32 Filled = 0, Cursor = 0;
    bool Server = false, ObserveRelayCollision = false;
    double BeforeClientBrowse = 0.;
    TSet<uint32> CapturedRelayIds;
    double Deadline = 0, LoadStart = 0, SaveMs = 0, ConnectedAt = 0;
    bool Tick(float Delta);
    bool Finish(bool Success, const TCHAR* Reason);
    void Advance(EStep Next);
    bool Write(const TCHAR* Name, const FString& Text) const;
    FString Read(const TCHAR* Name) const;
    bool Verify(UWorld* World, FString& OutManifest);
    bool VerifyControl(UWorld* World, FString& OutManifest);
    bool WitnessesReleased() const;
    bool RecordSave(double RestoreMs) const;
};

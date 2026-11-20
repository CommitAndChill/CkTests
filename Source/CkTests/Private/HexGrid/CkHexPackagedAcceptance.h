#pragma once

#include "CkGrid/HexGridSystem/Placement/CkHexGridPlacement_Result_Data.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CkHexPackagedAcceptance.generated.h"

// Opt-in Development package acceptance. The game instance survives snapshot map travel.
UCLASS()
class UCk_HexPackagedAcceptance_UE : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    enum class EStep : uint8 { Boot, Compose, Place, Block, Move, Restore, LateJoin, Teardown, Done };
    UFUNCTION()
    void OnMove(const FCk_HexGridPlacement_Result& InResult);
    bool Tick(float Delta);
    void Advance(EStep Next, const TCHAR* Evidence);
    bool Finish(bool Success, const TCHAR* Reason);
    bool WriteReceipt(const TCHAR* Name, const FString& Value) const;
    FString ReadReceipt(const TCHAR* Name) const;

    FTSTicker::FDelegateHandle Ticker;
    EStep Step = EStep::Boot;
    FString Directory;
    FString Identity;
    FName Slot;
    double Deadline = 0;
    double OpenConnectionObservedAt = 0;
    double SaveRequestMs = 0;
    double LoadStartedAt = 0;
    bool Server = false;
    bool BlockedCallback = false;
    TWeakObjectPtr<UWorld> PriorWorld;
    TArray<FCk_Handle> PriorHandles;
};

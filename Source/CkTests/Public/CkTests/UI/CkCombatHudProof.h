#pragma once

#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "CkTests/UI/CkCombatHudProofData.h"

#include "CkCombatHudProof.generated.h"

class FCkUiView;
class FCkUiWidgetRegistrySnapshot;
class FCkCombatHudProofModel;
class UCk_UI_AuthoredSurface_UE;

/** Passive source-authored HUD: gameplay input and world ticking remain with the player. */
UCLASS(BlueprintType)
class CKTESTS_API UCk_CombatHudProof_UE : public UUserWidget
{
    GENERATED_BODY()

public:
    UCk_CombatHudProof_UE(const FObjectInitializer& InObjectInitializer);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Combat HUD Proof")
    static UCk_CombatHudProof_UE* Open(APlayerController* InPlayerController, ECk_CombatHudProof_Theme InTheme);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Combat HUD Proof")
    bool Request_Command(ECk_CombatHudProof_Command InCommand);

    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Combat HUD Proof")
    void Request_Close();

    /** Preview a source pair atomically. The next changed on-disk pair supersedes this preview. */
    UFUNCTION(BlueprintCallable, Category = "Ck|UI|Combat HUD Proof")
    bool Request_ApplySource(const FString& InMarkup, const FString& InStylesheet);

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Combat HUD Proof")
    FCk_CombatHudProof_State Get_State() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Combat HUD Proof")
    ECk_CombatHudProof_Theme Get_Theme() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Combat HUD Proof")
    FString Get_Summary() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Combat HUD Proof")
    FString Get_SourceError() const;

    UFUNCTION(BlueprintPure, Category = "Ck|UI|Combat HUD Proof")
    bool IsActive() const;

    auto Get_AuthoredView() const -> TSharedPtr<FCkUiView>;
    auto Get_Model() const -> TSharedPtr<FCkCombatHudProofModel>;

protected:
    auto RebuildWidget() -> TSharedRef<SWidget> override;
    auto NativeDestruct() -> void override;

private:
    auto DoInitialize(ECk_CombatHudProof_Theme InTheme) -> bool;
    auto DoReloadFiles() -> bool;
    auto DoTick(float InDeltaTime) -> bool;
    auto DoRelease() -> void;

    UPROPERTY(Transient)
    TObjectPtr<UCk_UI_AuthoredSurface_UE> _Surface;

    ECk_CombatHudProof_Theme _Theme = ECk_CombatHudProof_Theme::Futuristic;
    TSharedPtr<FCkCombatHudProofModel> _Model;
    TSharedPtr<FCkUiView> _View;
    TSharedPtr<const FCkUiWidgetRegistrySnapshot> _ControlRegistry;
    FTSTicker::FDelegateHandle _TickHandle;
    FString _ResourceRoot;
    FString _SourceError;
    FString _LastMarkup;
    FString _LastStyle;
    float _PollRemaining = 0.5f;
    bool _Active = false;
    bool _HasSourceAttempt = false;
};

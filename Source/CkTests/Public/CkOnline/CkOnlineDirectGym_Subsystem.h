#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CkOnline/Direct/CkOnlineDirect_Fragment_Data.h"

#include "CkOnlineDirectGym_Subsystem.generated.h"

class SEditableTextBox;
class SWindow;
class SWidget;
class UCk_OnlineDirect_Subsystem_UE;

UENUM(BlueprintType)
enum class ECkOnlineDirectGym_TextField : uint8
{ Name, AdvertisedAddress, JoinDescriptor, ChatDraft };

/** Human-driven text input and ownership guard for the Direct TCP editor gyms. */
UCLASS(BlueprintType)
class CKTESTS_API UCkOnlineDirectGym_Subsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    auto Deinitialize() -> void override;

    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_EditText(ECkOnlineDirectGym_TextField InField);
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_CopyDescriptor();
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_Host();
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_Join();
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_JoinCopiedInvite();
    /** Queues a copied, saved or programmatic invite for the gym's owned profile. */
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_JoinInvite(const FString& InDescriptor);
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_ToggleReady();
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_SendChat();
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_EditChatAndSend();
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_Leave();
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_StopOwnedProfile();
    /** Writes one sanitized, manually triggered evidence sample; never declares a pass. */
    UFUNCTION(BlueprintCallable, Category = "Ck|Tests|Online|Direct")
    bool Request_LogCheckpoint(bool InChat);

    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    bool Get_IsEditing() const { return _IsEditing; }
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    bool Get_IsOwnedProfile() const;
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_Name() const { return _Name; }
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_AdvertisedAddress() const { return _AdvertisedAddress; }
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_JoinDescriptorDraft() const { return _JoinDescriptor; }
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_ChatDraft() const { return _ChatDraft; }
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_LastError() const { return _LastError; }
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_QueueText() const;
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_CompletionText() const;
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_RosterText() const;
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_TranscriptText(int32 InMaxMessages = 32) const;
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    int32 Get_MessageCount() const;
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    FString Get_MessageLineFromNewest(int32 InReverseIndex) const;
    UFUNCTION(BlueprintPure, Category = "Ck|Tests|Online|Direct")
    bool Get_IsLocalReady() const;

private:
    auto GetService() const -> UCk_OnlineDirect_Subsystem_UE*;
    auto EnsureOwnedStarted() -> bool;
    auto Queue(ECk_OnlineDirect_Action InAction, const FString& InText = FString(), bool InReady = false) -> bool;
    auto Reject(const FString& InReason) -> bool;
    auto OpenTextEditor(ECkOnlineDirectGym_TextField InField, bool InSendOnEnter) -> bool;
    auto CommitText(const FString& InText) -> void;
    auto CloseEditor() -> void;
    auto OnEditorClosed() -> void;
    auto RestoreFocus() -> void;

    FString _Name = TEXT("Player");
    FString _AdvertisedAddress = TEXT("127.0.0.1");
    FString _JoinDescriptor;
    FString _ChatDraft;
    FString _LastError;
    FString _LastQueuedAction = TEXT("none");
    FGuid _OwnedGeneration;
    int32 _AcceptedRequests = 0;
    int32 _CheckpointOrdinal = 0;
    int32 _SlateUserIndex = 0;
    bool _OwnsProfile = false;
    bool _IsEditing = false;
    bool _SendOnEnter = false;
    bool _IsDeinitializing = false;
    ECkOnlineDirectGym_TextField _EditingField = ECkOnlineDirectGym_TextField::Name;
    TWeakPtr<SWidget> _PreviousFocus;
    TSharedPtr<SWindow> _InputWindow;
    TSharedPtr<SEditableTextBox> _InputBox;
};

#include "CkOnline/CkOnlineDirectGym_Subsystem.h"
#include "CkOnlineDirectGym_Evidence.h"

#include "CkOnline/CkOnline_Log.h"
#include "CkOnline/Direct/CkOnlineDirect_Subsystem.h"
#include "CkOnline/Subsystem/CkOnline_Subsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Input/Reply.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SWindow.h"

namespace
{
auto FieldName(const ECkOnlineDirectGym_TextField InField) -> const TCHAR*
{
    switch (InField)
    {
        case ECkOnlineDirectGym_TextField::Name: return TEXT("Player name");
        case ECkOnlineDirectGym_TextField::AdvertisedAddress: return TEXT("Advertised IPv4 address");
        case ECkOnlineDirectGym_TextField::JoinDescriptor: return TEXT("Join descriptor");
        case ECkOnlineDirectGym_TextField::ChatDraft: return TEXT("Chat draft");
    }
    return TEXT("Text");
}

auto ActionName(const ECk_OnlineDirect_Action InAction) -> const TCHAR*
{
    switch (InAction)
    {
        case ECk_OnlineDirect_Action::Host: return TEXT("Host");
        case ECk_OnlineDirect_Action::Join: return TEXT("Join");
        case ECk_OnlineDirect_Action::Leave: return TEXT("Leave");
        case ECk_OnlineDirect_Action::SetReady: return TEXT("SetReady");
        case ECk_OnlineDirect_Action::SendText: return TEXT("SendText");
    }
    return TEXT("Unknown");
}

auto ResultName(const ECk_OnlineDirect_Result InResult) -> const TCHAR*
{
    switch (InResult)
    {
        case ECk_OnlineDirect_Result::Succeeded: return TEXT("Succeeded");
        case ECk_OnlineDirect_Result::Cancelled: return TEXT("Cancelled");
        case ECk_OnlineDirect_Result::TimedOut: return TEXT("TimedOut");
        case ECk_OnlineDirect_Result::InvalidInput: return TEXT("InvalidInput");
        case ECk_OnlineDirect_Result::InvalidState: return TEXT("InvalidState");
        case ECk_OnlineDirect_Result::TransportError: return TEXT("TransportError");
        case ECk_OnlineDirect_Result::Unauthorized: return TEXT("Unauthorized");
        case ECk_OnlineDirect_Result::Expired: return TEXT("Expired");
        case ECk_OnlineDirect_Result::Full: return TEXT("Full");
        case ECk_OnlineDirect_Result::ProtocolError: return TEXT("ProtocolError");
    }
    return TEXT("Unknown");
}
}

auto UCkOnlineDirectGym_Subsystem::GetService() const -> UCk_OnlineDirect_Subsystem_UE*
{
    auto* GameInstance = GetGameInstance();
    return IsValid(GameInstance) ? GameInstance->GetSubsystem<UCk_OnlineDirect_Subsystem_UE>() : nullptr;
}

auto UCkOnlineDirectGym_Subsystem::Reject(const FString& InReason) -> bool
{
    _LastError = InReason;
    return false;
}

auto UCkOnlineDirectGym_Subsystem::Get_IsOwnedProfile() const -> bool
{
    auto* Service = GetService();
    return _OwnsProfile && Service != nullptr && Service->Get_IsStarted()
        && _OwnedGeneration.IsValid()
        && Service->Get_Snapshot().Get_ServiceGeneration() == _OwnedGeneration;
}

auto UCkOnlineDirectGym_Subsystem::EnsureOwnedStarted() -> bool
{
    auto* Service = GetService();
    if (Service == nullptr) { return Reject(TEXT("Direct TCP service subsystem unavailable.")); }
    if (Service->Get_IsStarted())
    {
        if (Get_IsOwnedProfile()) { return true; }
        _OwnsProfile = false;
        return Reject(TEXT("Stop previous fake profile first (another Direct profile owns this game instance)."));
    }
    auto* BaseService = GetGameInstance()->GetSubsystem<UCk_Online_Subsystem_UE>();
    if (BaseService != nullptr && BaseService->Get_Diagnostics().Get_State() != ECk_OnlineService_State::Stopped)
    { return Reject(TEXT("Stop previous fake profile first; the shared Online service is occupied.")); }

    FCk_OnlineDirect_Spec Spec;
    Spec.Set_Name(_Name);
    Spec.Set_AdvertisedAddress(_AdvertisedAddress);
    Spec.Set_Port(7788);
    Spec.Set_Capacity(4);
    Spec.Set_InviteLifetimeSeconds(300.0);
    if (!Service->Request_Start(Spec))
    { return Reject(TEXT("Direct profile start rejected; check name and advertised IPv4 address.")); }

    _OwnedGeneration = Service->Get_Snapshot().Get_ServiceGeneration();
    if (!_OwnedGeneration.IsValid())
    {
        Service->Request_Stop();
        return Reject(TEXT("Direct profile started without a valid service generation."));
    }
    _OwnsProfile = true;
    _AcceptedRequests = 0;
    _LastQueuedAction = TEXT("none");
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectGym_Subsystem::Queue(const ECk_OnlineDirect_Action InAction,
    const FString& InText, const bool InReady) -> bool
{
    auto* Service = GetService();
    if (Service == nullptr || !Get_IsOwnedProfile())
    { return Reject(TEXT("No owned Direct profile. Host or Join first.")); }
    if (!Service->Request(InAction, InText, InReady))
    { return Reject(FString::Printf(TEXT("%s request was not queued; inspect service state and input."), ActionName(InAction))); }
    if (_AcceptedRequests < MAX_int32) { ++_AcceptedRequests; }
    _LastQueuedAction = ActionName(InAction);
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectGym_Subsystem::Request_Host() -> bool
{
    if (!EnsureOwnedStarted()) { return false; }
    return Queue(ECk_OnlineDirect_Action::Host);
}

auto UCkOnlineDirectGym_Subsystem::Request_Join() -> bool
{
    if (_JoinDescriptor.IsEmpty()) { return Reject(TEXT("Enter or paste the host's join descriptor with O first.")); }
    if (!EnsureOwnedStarted()) { return false; }
    return Queue(ECk_OnlineDirect_Action::Join, _JoinDescriptor);
}

auto UCkOnlineDirectGym_Subsystem::Request_ToggleReady() -> bool
{
    return Queue(ECk_OnlineDirect_Action::SetReady, FString(), !Get_IsLocalReady());
}

auto UCkOnlineDirectGym_Subsystem::Request_SendChat() -> bool
{
    if (_ChatDraft.IsEmpty()) { return Reject(TEXT("Type a chat draft with B first.")); }
    return Queue(ECk_OnlineDirect_Action::SendText, _ChatDraft);
}

auto UCkOnlineDirectGym_Subsystem::Request_Leave() -> bool
{ return Queue(ECk_OnlineDirect_Action::Leave); }

auto UCkOnlineDirectGym_Subsystem::Request_StopOwnedProfile() -> bool
{
    auto* Service = GetService();
    if (Service == nullptr || !Get_IsOwnedProfile())
    { return Reject(TEXT("This gym does not own the active Direct profile.")); }
    Service->Request_Stop();
    _OwnsProfile = false;
    _OwnedGeneration.Invalidate();
    _LastQueuedAction = TEXT("Stopped owned profile");
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectGym_Subsystem::Request_LogCheckpoint(const bool InChat) -> bool
{
    if (!IsInGameThread())
    {
        UE_LOG(CkOnline, Display, TEXT("[OnlineCheckpoint] rejected=not-game-thread"));
        return false;
    }
    auto* Service = GetService();
    if (Service == nullptr || !_OwnsProfile || !_OwnedGeneration.IsValid() || !Service->Get_IsStarted())
    {
        UE_LOG(CkOnline, Display, TEXT("[OnlineCheckpoint] rejected=no-owned-active-profile"));
        return false;
    }
    auto* Instance = GetGameInstance();
    auto* BaseService = IsValid(Instance) ? Instance->GetSubsystem<UCk_Online_Subsystem_UE>() : nullptr;
    if (!IsValid(BaseService) || BaseService->Get_Diagnostics().Get_Generation() != _OwnedGeneration)
    {
        UE_LOG(CkOnline, Display, TEXT("[OnlineCheckpoint] rejected=stale-owned-generation"));
        return false;
    }
    const auto Snapshot = Service->Get_Snapshot();
    const auto State = Snapshot.Get_State();
    if (Snapshot.Get_ServiceGeneration() != _OwnedGeneration || !Snapshot.Get_LobbyEpoch().IsValid()
        || !Snapshot.Get_LocalMemberId().IsValid()
        || (State != ECk_OnlineDirect_State::Hosting && State != ECk_OnlineDirect_State::Joined))
    {
        UE_LOG(CkOnline, Display, TEXT("[OnlineCheckpoint] rejected=inactive-or-stale-snapshot"));
        return false;
    }
    if (_CheckpointOrdinal < MAX_int32) { ++_CheckpointOrdinal; }
    UE_LOG(CkOnline, Display, TEXT("%s"), *ck::test::direct::FormatCheckpoint(Snapshot, InChat, _CheckpointOrdinal));
    return true;
}

auto UCkOnlineDirectGym_Subsystem::Get_IsLocalReady() const -> bool
{
    auto* Service = GetService();
    if (Service == nullptr || !Service->Get_IsStarted()) { return false; }
    const auto Snapshot = Service->Get_Snapshot();
    for (const auto& Member : Snapshot.Get_Members())
    {
        if (Member.Get_Id() == Snapshot.Get_LocalMemberId()) { return Member.Get_Ready(); }
    }
    return false;
}

auto UCkOnlineDirectGym_Subsystem::Get_QueueText() const -> FString
{
    auto* Service = GetService();
    const int32 Completed = Service != nullptr ? Service->Get_CompletedCount() : 0;
    return FString::Printf(TEXT("Accepted locally: %d; completed operations: %d; last queued: %s"),
        _AcceptedRequests, Completed, *_LastQueuedAction);
}

auto UCkOnlineDirectGym_Subsystem::Get_CompletionText() const -> FString
{
    auto* Service = GetService();
    if (Service == nullptr || Service->Get_CompletedCount() == 0) { return TEXT("none (queued is not completed)"); }
    const auto Completion = Service->Get_LastCompletion();
    return FString::Printf(TEXT("%s / %s / %s"), *Completion.Get_OperationId().ToString(),
        ResultName(Completion.Get_Result()), *Completion.Get_Detail());
}

auto UCkOnlineDirectGym_Subsystem::Get_RosterText() const -> FString
{
    auto* Service = GetService();
    if (Service == nullptr) { return TEXT("none"); }
    const auto Snapshot = Service->Get_Snapshot();
    TArray<FString> Entries;
    for (const auto& Member : Snapshot.Get_Members())
    {
        Entries.Add(FString::Printf(TEXT("%s [%s] %s"), *Member.Get_Name(),
            *Member.Get_Id().ToString(), Member.Get_Ready() ? TEXT("READY") : TEXT("not ready")));
    }
    return Entries.IsEmpty() ? TEXT("none") : FString::Join(Entries, TEXT(" | "));
}

auto UCkOnlineDirectGym_Subsystem::Get_MessageCount() const -> int32
{
    auto* Service = GetService();
    return Service != nullptr ? Service->Get_Snapshot().Get_Messages().Num() : 0;
}

auto UCkOnlineDirectGym_Subsystem::Get_MessageLineFromNewest(const int32 InReverseIndex) const -> FString
{
    auto* Service = GetService();
    if (Service == nullptr || InReverseIndex < 0) { return TEXT(""); }
    const auto Messages = Service->Get_Snapshot().Get_Messages();
    const int32 Index = Messages.Num() - 1 - InReverseIndex;
    if (!Messages.IsValidIndex(Index)) { return TEXT(""); }
    const auto& Message = Messages[Index];
    return FString::Printf(TEXT("#%d %s [%s]: %s"), Message.Get_Sequence(),
        *Message.Get_SenderName(), *Message.Get_SenderId().ToString(), *Message.Get_Text());
}

auto UCkOnlineDirectGym_Subsystem::Get_TranscriptText(const int32 InMaxMessages) const -> FString
{
    TArray<FString> Lines;
    const int32 Count = FMath::Min(FMath::Clamp(InMaxMessages, 0, 32), Get_MessageCount());
    for (int32 Index = Count - 1; Index >= 0; --Index) { Lines.Add(Get_MessageLineFromNewest(Index)); }
    return Lines.IsEmpty() ? TEXT("none") : FString::Join(Lines, TEXT(" | "));
}

auto UCkOnlineDirectGym_Subsystem::Request_CopyDescriptor() -> bool
{
    auto* Service = GetService();
    if (Service == nullptr || !Service->Get_IsStarted()) { return Reject(TEXT("Host first; no descriptor to copy.")); }
    const auto Snapshot = Service->Get_Snapshot();
    if (Snapshot.Get_State() != ECk_OnlineDirect_State::Hosting || Snapshot.Get_JoinDescriptor().IsEmpty())
    { return Reject(TEXT("Only the hosting profile has an active descriptor to copy.")); }
    FPlatformApplicationMisc::ClipboardCopy(*Snapshot.Get_JoinDescriptor());
    _LastError.Empty();
    return true;
}

auto UCkOnlineDirectGym_Subsystem::Request_EditText(const ECkOnlineDirectGym_TextField InField) -> bool
{
    if (_IsEditing) { return Reject(TEXT("Finish or cancel the current text edit first.")); }
    if (!FSlateApplication::IsInitialized()) { return Reject(TEXT("Slate text input is unavailable.")); }
    _EditingField = InField;
    FString Initial;
    switch (InField)
    {
        case ECkOnlineDirectGym_TextField::Name: Initial = _Name; break;
        case ECkOnlineDirectGym_TextField::AdvertisedAddress: Initial = _AdvertisedAddress; break;
        case ECkOnlineDirectGym_TextField::JoinDescriptor:
            FPlatformApplicationMisc::ClipboardPaste(Initial);
            if (Initial.IsEmpty()) { Initial = _JoinDescriptor; }
            break;
        case ECkOnlineDirectGym_TextField::ChatDraft: Initial = _ChatDraft; break;
    }
    auto& Slate = FSlateApplication::Get();
    _PreviousFocus = Slate.GetUserFocusedWidget(_SlateUserIndex);
    const TWeakObjectPtr<UCkOnlineDirectGym_Subsystem> WeakSelf(this);
    TSharedPtr<SEditableTextBox> NewBox;
    const TSharedRef<SWindow> NewWindow = SNew(SWindow)
        .Title(FText::FromString(FString::Printf(TEXT("Online Direct: %s (Enter save, Esc cancel)"), FieldName(InField))))
        .ClientSize(FVector2D(640.0, 80.0))
        .SupportsMaximize(false)
        .SupportsMinimize(false)
        .FocusWhenFirstShown(true);
    NewWindow->SetContent(SAssignNew(NewBox, SEditableTextBox)
        .Text(FText::FromString(Initial))
        .SelectAllTextWhenFocused(true)
        .OnTextCommitted_Lambda([WeakSelf](const FText& InText, const ETextCommit::Type InReason)
        {
            if (InReason != ETextCommit::OnEnter) { return; }
            if (auto* Self = WeakSelf.Get()) { Self->CommitText(InText.ToString()); }
        })
        .OnKeyDownHandler_Lambda([WeakSelf](const FGeometry&, const FKeyEvent& InEvent)
        {
            if (InEvent.GetKey() != EKeys::Escape) { return FReply::Unhandled(); }
            if (auto* Self = WeakSelf.Get()) { Self->CloseEditor(); }
            return FReply::Handled();
        }));
    NewWindow->SetOnWindowClosed(FOnWindowClosed::CreateLambda([WeakSelf](const TSharedRef<SWindow>&)
    {
        if (auto* Self = WeakSelf.Get()) { Self->OnEditorClosed(); }
    }));
    _InputWindow = NewWindow;
    _InputBox = NewBox;
    _IsEditing = true;
    Slate.AddWindow(NewWindow);
    Slate.SetUserFocus(_SlateUserIndex, NewBox.ToSharedRef(), EFocusCause::SetDirectly);
    return true;
}

auto UCkOnlineDirectGym_Subsystem::CommitText(const FString& InText) -> void
{
    if (!_IsEditing || InText.Len() > 512)
    {
        _LastError = TEXT("Text exceeds the 512-character gym input limit.");
        CloseEditor();
        return;
    }
    switch (_EditingField)
    {
        case ECkOnlineDirectGym_TextField::Name: _Name = InText; break;
        case ECkOnlineDirectGym_TextField::AdvertisedAddress: _AdvertisedAddress = InText; break;
        case ECkOnlineDirectGym_TextField::JoinDescriptor: _JoinDescriptor = InText; break;
        case ECkOnlineDirectGym_TextField::ChatDraft: _ChatDraft = InText; break;
    }
    _LastError.Empty();
    CloseEditor();
}

auto UCkOnlineDirectGym_Subsystem::RestoreFocus() -> void
{
    if (!FSlateApplication::IsInitialized()) { _PreviousFocus.Reset(); return; }
    auto& Slate = FSlateApplication::Get();
    if (GEngine != nullptr && GEngine->GameViewport != nullptr && GEngine->GameViewport->GetGameViewportWidget().IsValid())
    { Slate.SetUserFocus(_SlateUserIndex, GEngine->GameViewport->GetGameViewportWidget().ToSharedRef(), EFocusCause::SetDirectly); }
    else if (const auto Previous = _PreviousFocus.Pin())
    { Slate.SetUserFocus(_SlateUserIndex, Previous.ToSharedRef(), EFocusCause::SetDirectly); }
    else { Slate.ClearUserFocus(_SlateUserIndex); }
    _PreviousFocus.Reset();
}

auto UCkOnlineDirectGym_Subsystem::CloseEditor() -> void
{
    const auto Window = MoveTemp(_InputWindow);
    _InputBox.Reset();
    _IsEditing = false;
    if (Window.IsValid() && FSlateApplication::IsInitialized()) { Window->RequestDestroyWindow(); }
    RestoreFocus();
}

auto UCkOnlineDirectGym_Subsystem::OnEditorClosed() -> void
{
    if (!_IsEditing || _IsDeinitializing) { return; }
    _InputWindow.Reset();
    _InputBox.Reset();
    _IsEditing = false;
    RestoreFocus();
}

auto UCkOnlineDirectGym_Subsystem::Deinitialize() -> void
{
    _IsDeinitializing = true;
    CloseEditor();
    _OwnsProfile = false;
    _OwnedGeneration.Invalidate();
    Super::Deinitialize();
}

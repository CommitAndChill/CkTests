#pragma once

#include "CkUICore/UserWidget/CkActivatableWidget.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkWidgets/Authoring/CkAuthoredSurface_Widget.h"
#include "Blueprint/WidgetTree.h"

#include "CkUiOwnedSession_TestOwner.generated.h"

/** Real CommonUI owner for the shared session proof; covering suspends, shutdown retires. */
UCLASS()
class UCk_UiOwnedSession_TestOwner_UE : public UCk_ActivatableWidget_UE
{
    GENERATED_BODY()
public:
    auto Prepare(FCk_UI_AuthoredSession_Config InConfig, const FString& InMarkup,
        const FString& InStyles, TArray<FString>& OutErrors) -> bool
    {
        if (Session != nullptr || Surface != nullptr)
        {
            OutErrors = {TEXT("Owned-session fixture preparation succeeds once.")};
            return false;
        }
        WidgetTree = NewObject<UWidgetTree>(this, TEXT("OwnedSessionTree"));
        Surface = WidgetTree->ConstructWidget<UCk_UI_AuthoredSurface_UE>(
            UCk_UI_AuthoredSurface_UE::StaticClass(), TEXT("OwnedSurface"));
        WidgetTree->RootWidget = Surface;
        Session = NewObject<UCk_UI_AuthoredSession_UE>(this);
        InConfig.Set_InteractionOwner(this);
        const bool Prepared = Session->TryInitialize(InConfig, OutErrors)
            && Session->TryReload(InMarkup, InStyles, OutErrors)
            && Session->TryMount(Surface, TEXT("main"), OutErrors);
        if (!Prepared)
        {
            Shutdown();
            Surface->ReleaseView();
            WidgetTree->RootWidget = nullptr;
            Session = nullptr;
            Surface = nullptr;
            WidgetTree = nullptr;
        }
        return Prepared;
    }

    auto Shutdown() -> void
    {
        if (IsValid(Session)) { Session->Request_Release(); }
    }

    UPROPERTY()
    UCk_UI_AuthoredSession_UE* Session = nullptr;
    UPROPERTY()
    UCk_UI_AuthoredSurface_UE* Surface = nullptr;

protected:
    auto NativeOnActivated() -> void override
    {
        Super::NativeOnActivated();
        if (IsValid(Session)) { Session->TryBeginInput(); }
    }
    auto NativeOnDeactivated() -> void override
    {
        if (IsValid(Session)) { Session->Request_EndInput(); }
        Super::NativeOnDeactivated();
    }
    auto NativeDestruct() -> void override
    {
        Shutdown();
        Super::NativeDestruct();
    }
    auto BeginDestroy() -> void override
    {
        Shutdown();
        Super::BeginDestroy();
    }
};

#pragma once

#include "CkWidgets/Authoring/CkAuthoredSession.h"

#include "CkUiAuthoredSession_TestListener.generated.h"

/** Reflected receiver for the session's production dynamic delegates. Never retains its session. */
UCLASS()
class UCk_UiAuthoredSession_TestListener_UE : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void HandleAction(const FString& Name)
    {
        ++ActionCalls;
        LastAction = Name;
        if (ReleaseOnAction)
        {
            if (UCk_UI_AuthoredSession_UE* Owner = Session.Get()) { Owner->Request_Release(); }
        }
    }

    UFUNCTION()
    void HandleText(const FString& Name, const FText& Proposed)
    {
        ++TextCalls;
        LastTextName = Name;
        LastText = Proposed;
        if (AcceptEdits)
        {
            if (UCk_UI_AuthoredSession_UE* Owner = Session.Get()) { Owner->TrySetText(Name, Proposed); }
        }
    }

    UFUNCTION()
    void HandleNumber(const FString& Name, float Proposed)
    {
        ++NumberCalls;
        LastNumberName = Name;
        LastNumber = Proposed;
        if (AcceptEdits)
        {
            if (UCk_UI_AuthoredSession_UE* Owner = Session.Get()) { Owner->TrySetNumber(Name, Proposed); }
        }
    }

    UFUNCTION()
    void HandleBool(const FString& Name, bool Proposed)
    {
        ++BoolCalls;
        LastBoolName = Name;
        LastBool = Proposed;
        if (AcceptEdits)
        {
            if (UCk_UI_AuthoredSession_UE* Owner = Session.Get()) { Owner->TrySetBool(Name, Proposed); }
        }
    }

    TWeakObjectPtr<UCk_UI_AuthoredSession_UE> Session;
    bool ReleaseOnAction = false;
    bool AcceptEdits = false;
    int32 ActionCalls = 0;
    int32 TextCalls = 0;
    int32 NumberCalls = 0;
    int32 BoolCalls = 0;
    FString LastAction;
    FString LastTextName;
    FString LastNumberName;
    FString LastBoolName;
    FText LastText;
    float LastNumber = 0.0f;
    bool LastBool = false;
};

#pragma once

#include "UserSettings/EnhancedInputUserSettings.h"
#include "CkGameSettings/UI/CkGameSettingsUI_KeyBindingPageWidget.h"
#include "CkGameSettingsGym_Widgets.h"
#include "Blueprint/UserWidget.h"
#include "CkTest_GameSettings_TransientInputSettings.generated.h"

// This class is selected only for a fresh, GUID-named PIE save slot by the exact-owner fixture.
// Real Enhanced Input profiles, IMC registration and MapPlayerKey remain in use; the two disk
// persistence entry points are deliberately suppressed for the disposable test profiles.
UCLASS()
class UCkTest_GameSettings_TransientInputSettings final : public UEnhancedInputUserSettings
{
    GENERATED_BODY()

public:
    void SaveSettings() override { ++SynchronousSaveAttempts; }
    void AsyncSaveSettings() override { ++AsynchronousSaveAttempts; }

    int32 Get_SynchronousSaveAttempts() const { return SynchronousSaveAttempts; }
    int32 Get_AsynchronousSaveAttempts() const { return AsynchronousSaveAttempts; }

protected:
    // The GUID slot should never exist. If a collision is observed, do not migrate any
    // persisted profile into this transient fixture instance.
    void HandleSettingsClassChangedOnLoad_Impl(UEnhancedInputUserSettings*) override {}

private:
    int32 SynchronousSaveAttempts = 0;
    int32 AsynchronousSaveAttempts = 0;
};

// Exposes only the inherited protected event entry points. The production page still builds its
// rows, binds its delegate, and starts capture itself; the fixture never writes capture state.
UCLASS()
class UCkTest_GameSettings_ForwardedKeyBindingPage final
    : public UCk_GameSettingsGym_KeyPage
{
    GENERATED_BODY()

public:
    FReply ForwardNativeKey(const FGeometry& Geometry, const FKeyEvent& Event)
    { return NativeOnKeyDown(Geometry, Event); }

    FReply ForwardNativeAnalog(const FGeometry& Geometry, const FAnalogInputEvent& Event)
    { return NativeOnAnalogValueChanged(Geometry, Event); }
};

// Concrete viewport root for the foreign-owner hierarchy case.
UCLASS()
class UCkTest_GameSettings_ViewportRoot final : public UUserWidget
{
    GENERATED_BODY()
};

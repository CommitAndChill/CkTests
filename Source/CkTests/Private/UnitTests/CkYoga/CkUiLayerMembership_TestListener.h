#pragma once

#include "CkUI/Layout/CkUI_LayerMembership.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"

#include "CkUiLayerMembership_TestListener.generated.h"

UCLASS()
class UCk_UiLayerMembership_TestListener_UE : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION()
    void HandleRetired(UCk_UI_LayerMembership_UE* InMembership)
    {
        const TWeakObjectPtr<UCk_UI_LayerMembership_UE> Identity{InMembership};
        ++Calls;
        AllCallbacksSawRetired = AllCallbacksSawRetired && IsValid(InMembership) && !InMembership->Get_IsCurrent();
        if (Reenter)
        {
            Reenter = false;
            auto* Subsystem = ReentrySubsystem.Get();
            auto* Owner = ReentryOwner.Get();
            if (Subsystem != nullptr && Owner != nullptr
                && Subsystem->PushWidgetToLayer_Instance(ReentryTag, Owner) == Owner)
            {
                Replacement = NewObject<UCk_UI_LayerMembership_UE>(this);
                TArray<FString> Errors;
                ReentrySucceeded = Replacement->TryObserve(Owner, ReentryTag, Errors);
            }
        }
        if (CollectDuringCallback)
        {
            if (BeforeCollect) { BeforeCollect(); }
            CollectGarbage(RF_NoFlags);
            CallbackLiveAfterGc = Identity.IsValid() && Identity.Get() == InMembership;
        }
        if (auto* LiveMembership = Identity.Get()) { LiveMembership->Request_Stop(); }
    }

    int32 Calls = 0;
    bool AllCallbacksSawRetired = true;
    bool Reenter = false;
    bool ReentrySucceeded = false;
    bool CollectDuringCallback = false;
    bool CallbackLiveAfterGc = false;
    TFunction<void()> BeforeCollect;
    TWeakObjectPtr<UCk_UI_Layout_Subsystem_UE> ReentrySubsystem;
    TWeakObjectPtr<UCommonActivatableWidget> ReentryOwner;
    FGameplayTag ReentryTag;
    UPROPERTY()
    UCk_UI_LayerMembership_UE* Replacement = nullptr;
};

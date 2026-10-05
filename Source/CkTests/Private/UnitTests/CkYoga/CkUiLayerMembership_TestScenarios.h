#pragma once

#include "CkUiLayerMembership_TestListener.h"
#include "CkUiOwnedSession_TestOwner.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"
#include "CkUI/Layout/CkUI_LayoutConfigAsset.h"
#include "CkUI/Layout/CkUI_LayerStack.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_ui_layer_membership_test
{
    inline auto CallStart(UObject* Object, FName Name, UCommonActivatableWidget* Owner, FGameplayTag Tag) -> bool
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        if (Function == nullptr) { return false; }
        auto* OwnerProperty = FindFProperty<FObjectPropertyBase>(Function, TEXT("InOwner"));
        auto* TagProperty = FindFProperty<FStructProperty>(Function, TEXT("InLayerTag"));
        auto* ReturnProperty = CastField<FBoolProperty>(Function->GetReturnProperty());
        if (OwnerProperty == nullptr || TagProperty == nullptr || ReturnProperty == nullptr
            || TagProperty->Struct != FGameplayTag::StaticStruct()) { return false; }
        FStructOnScope Parameters{Function};
        auto* Memory = Parameters.GetStructMemory();
        OwnerProperty->SetObjectPropertyValue_InContainer(Memory, Owner);
        TagProperty->CopyCompleteValue(TagProperty->ContainerPtrToValuePtr<void>(Memory), &Tag);
        Object->ProcessEvent(Function, Memory);
        return ReturnProperty->GetPropertyValue_InContainer(Memory);
    }

    inline auto CallBool(UObject* Object, FName Name) -> bool
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(Name) : nullptr;
        auto* ReturnProperty = Function != nullptr ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
        if (ReturnProperty == nullptr || Function->NumParms != 1) { return false; }
        FStructOnScope Parameters{Function};
        Object->ProcessEvent(Function, Parameters.GetStructMemory());
        return ReturnProperty->GetPropertyValue_InContainer(Parameters.GetStructMemory());
    }

    inline auto CallCount(UObject* Object) -> int32
    {
        UFunction* Function = IsValid(Object) ? Object->FindFunction(TEXT("Get_RetirementCount")) : nullptr;
        auto* ReturnProperty = Function != nullptr ? CastField<FIntProperty>(Function->GetReturnProperty()) : nullptr;
        if (ReturnProperty == nullptr || Function->NumParms != 1) { return INDEX_NONE; }
        FStructOnScope Parameters{Function};
        Object->ProcessEvent(Function, Parameters.GetStructMemory());
        return ReturnProperty->GetPropertyValue_InContainer(Parameters.GetStructMemory());
    }

    inline auto Advance() -> void
    {
        // Advance the production fallback outside callbacks; do not manually tick a partial world.
        FTSTicker::GetCoreTicker().Tick(1.0f / 60.0f);
        FSlateApplication::Get().Tick();
        FTSTicker::GetCoreTicker().Tick(1.0f / 60.0f);
    }

    inline auto Run(FAutomationTestBase& Test, ULocalPlayer* Player, FGameplayTag Tag,
        UCk_UI_LayoutConfigAsset_UE* Config) -> void
    {
        if (!Test.TestTrue(TEXT("membership fixture retains real admitted local player"), IsValid(Player)
            && IsValid(Player->GetWorld()) && IsValid(Config))) { return; }
        auto* Controller = Player->GetPlayerController(Player->GetWorld());
        auto* Subsystem = Player->GetSubsystem<UCk_UI_Layout_Subsystem_UE>();
        if (!Test.TestTrue(TEXT("membership fixture uses real controller and layout subsystem"),
            IsValid(Controller) && IsValid(Subsystem))) { return; }
        ON_SCOPE_EXIT { Subsystem->DestroyLayout(); };
        Subsystem->CreateLayout(Config);
        auto* Layer = Subsystem->Get_Layer(Tag);
        if (!Test.TestNotNull(TEXT("membership fixture has production stack"), Layer)) { return; }
        TStrongObjectPtr<UCk_UiOwnedSession_TestOwner_UE> Owner{CreateWidget<UCk_UiOwnedSession_TestOwner_UE>(Controller)};
        TStrongObjectPtr<UCk_UiOwnedSession_TestOwner_UE> Cover{CreateWidget<UCk_UiOwnedSession_TestOwner_UE>(Controller)};
        if (!Test.TestTrue(TEXT("membership real widget fixtures create"), IsValid(Owner.Get()) && IsValid(Cover.Get()))) { return; }
        TStrongObjectPtr<UCk_UiLayerMembership_TestListener_UE> Listener{NewObject<UCk_UiLayerMembership_TestListener_UE>()};
        const auto NewWatcher = [&]()
        {
            TStrongObjectPtr<UCk_UI_LayerMembership_UE> Result{NewObject<UCk_UI_LayerMembership_UE>()};
            Result->OnRetired.AddDynamic(Listener.Get(), &UCk_UiLayerMembership_TestListener_UE::HandleRetired);
            return Result;
        };
        const auto Push = [&]() -> bool
        { return Subsystem->PushWidgetToLayer_Instance(Tag, Owner.Get()) == Owner.Get(); };
        TArray<FString> Errors;
        auto Watcher = NewWatcher();
        Test.TestFalse(TEXT("membership null owner admission rejects"), Watcher->TryObserve(nullptr, Tag, Errors));
        Test.TestTrue(TEXT("rejected null owner has diagnostic and no callback"), !Errors.IsEmpty()
            && !Watcher->Get_IsCurrent() && Listener->Calls == 0);
        Test.TestFalse(TEXT("membership unadmitted owner rejects"), Watcher->TryObserve(Owner.Get(), Tag, Errors));
        if (!Test.TestTrue(TEXT("membership owner pushes through real production layout"), Push())) { return; }
        Test.TestFalse(TEXT("membership invalid tag rejects before publication"), Watcher->TryObserve(Owner.Get(), FGameplayTag{}, Errors));
        if (!Test.TestTrue(TEXT("failed admission allows fresh retry on same watcher"), Watcher->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        const uint64 FirstGeneration = Layer->Get_WidgetAdmissionGeneration(Owner.Get());
        Test.TestTrue(TEXT("accepted membership has nonzero generation"), FirstGeneration != 0 && Watcher->Get_IsCurrent());
        Test.TestTrue(TEXT("cover pushes through production stack"), Subsystem->PushWidgetToLayer_Instance(Tag, Cover.Get()) == Cover.Get());
        Advance();
        Test.TestTrue(TEXT("covered admitted parent retains current membership"), Layer->ContainsWidget(Owner.Get())
            && !Owner->IsActivated() && Watcher->Get_IsCurrent() && Listener->Calls == 0
            && Layer->Get_WidgetAdmissionGeneration(Owner.Get()) == FirstGeneration);
        Subsystem->PopWidgetFromLayer(Tag);
        Advance();
        Test.TestTrue(TEXT("uncovered parent retains same membership"), Owner->IsActivated() && Watcher->Get_IsCurrent());
        Subsystem->RemoveWidget(Owner.Get());
        Advance();
        Test.TestTrue(TEXT("pop retires once before reflected callback"), !Watcher->Get_IsCurrent()
            && Listener->Calls == 1 && Listener->AllCallbacksSawRetired);
        Advance();
        Test.TestEqual(TEXT("retirement callback remains one-shot"), Listener->Calls, 1);
        if (!Test.TestTrue(TEXT("owner can reenter production layer"), Push())) { return; }
        Test.TestFalse(TEXT("successful watcher lifetime cannot observe new admission"), Watcher->TryObserve(Owner.Get(), Tag, Errors));
        auto Readded = NewWatcher();
        if (!Test.TestTrue(TEXT("fresh watcher observes reentry"), Readded->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        const uint64 BeforeInherited = Layer->Get_WidgetAdmissionGeneration(Owner.Get());
        Subsystem->PushWidgetToLayer_Instance(Tag, Cover.Get());
        {
            const auto RetainedRoot = Owner->GetCachedWidget();
            if (!Test.TestTrue(TEXT("covered inherited removal retains real Slate root"), RetainedRoot.IsValid())) { return; }
            Layer->RemoveWidget(*Owner.Get());
            Layer->AddWidgetInstance(*Owner.Get());
            Test.TestTrue(TEXT("same-frame inherited remove/add assigns fresh generation"), Layer->ContainsWidget(Owner.Get())
                && Layer->Get_WidgetAdmissionGeneration(Owner.Get()) != BeforeInherited
                && Layer->Get_WidgetAdmissionGeneration(Owner.Get()) != 0 && !Readded->Get_IsCurrent());
            Advance();
        }
        Test.TestTrue(TEXT("old inherited admission retires once"), Listener->Calls == 2 && Listener->AllCallbacksSawRetired);
        auto Cleared = NewWatcher();
        if (!Test.TestTrue(TEXT("new inherited admission can be observed"), Cleared->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        {
            const bool WasPaused = Player->GetWorld()->IsPaused();
            const bool AcquiredPause = !WasPaused && Controller->SetPause(true);
            ON_SCOPE_EXIT { if (AcquiredPause && IsValid(Controller)) { Controller->SetPause(false); } };
            if (!Test.TestTrue(TEXT("membership ticker fixture enters real paused world"),
                Player->GetWorld()->IsPaused())) { return; }
            Subsystem->ClearLayer(Tag);
            Advance();
            Test.TestTrue(TEXT("layer clear retires exact admission while world paused"), !Cleared->Get_IsCurrent()
                && Listener->Calls == 3 && Player->GetWorld()->IsPaused());
        }

        if (!Test.TestTrue(TEXT("auto-deactivation owner pushes"), Push())) { return; }
        auto Deactivated = NewWatcher();
        if (!Test.TestTrue(TEXT("auto-deactivation admission observes"), Deactivated->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        Owner->DeactivateWidget();
        Advance();
        Test.TestTrue(TEXT("CommonUI auto-deactivation removes and retires top admission"), !Layer->ContainsWidget(Owner.Get())
            && !Deactivated->Get_IsCurrent() && Listener->Calls == 4);
        if (!Test.TestTrue(TEXT("explicit-stop owner pushes"), Push())) { return; }
        auto Stopped = NewWatcher();
        if (!Test.TestTrue(TEXT("explicit-stop admission observes"), Stopped->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        Subsystem->RemoveWidget(Owner.Get());
        Stopped->Request_Stop();
        Stopped->Request_Stop();
        Advance();
        Test.TestTrue(TEXT("explicit stop cancels pending retirement silently and remains terminal"), !Stopped->Get_IsCurrent() && Listener->Calls == 4
            && !Stopped->TryObserve(Owner.Get(), Tag, Errors));

        if (!Test.TestTrue(TEXT("resource-reset owner pushes"), Push())) { return; }
        auto Reset = NewWatcher();
        if (!Test.TestTrue(TEXT("resource-reset admission observes"), Reset->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        static_cast<UWidget*>(Layer)->ReleaseSlateResources(true);
        Advance();
        Test.TestTrue(TEXT("resource release invalidates membership generation"), Layer->Get_WidgetAdmissionGeneration(Owner.Get()) == 0
            && !Reset->Get_IsCurrent() && Listener->Calls == 5);
        Subsystem->DestroyLayout();
        Subsystem->CreateLayout(Config);
        Layer = Subsystem->Get_Layer(Tag);
        if (!Test.TestNotNull(TEXT("resource reset rebuilds real mounted layout"), Layer)) { return; }

        auto* Pooled = Layer->PushWidgetClass(UCk_UiOwnedSession_TestOwner_UE::StaticClass());
        if (!Test.TestNotNull(TEXT("production class pool creates owner"), Pooled)) { return; }
        auto PooledWatcher = NewWatcher();
        if (!Test.TestTrue(TEXT("production class-pool admission observes"), PooledWatcher->TryObserve(Pooled, Tag, Errors))) { return; }
        const uint64 PoolGeneration = Layer->Get_WidgetAdmissionGeneration(Pooled);
        Layer->PopWidget();
        auto* Reused = Layer->PushWidgetClass(UCk_UiOwnedSession_TestOwner_UE::StaticClass());
        Test.TestTrue(TEXT("actual class pool reuses UObject with new admission"), Reused == Pooled
            && Layer->Get_WidgetAdmissionGeneration(Reused) != PoolGeneration
            && Layer->Get_WidgetAdmissionGeneration(Reused) != 0 && !PooledWatcher->Get_IsCurrent());
        Advance();
        Test.TestEqual(TEXT("old pooled admission retires once"), Listener->Calls, 6);
        auto Destroyed = NewWatcher();
        if (!Test.TestTrue(TEXT("replacement pooled admission observes"), Destroyed->TryObserve(Reused, Tag, Errors))) { return; }
        Subsystem->DestroyLayout();
        Subsystem->CreateLayout(Config);
        Layer = Subsystem->Get_Layer(Tag);
        if (!Test.TestTrue(TEXT("layout destruction retires old watcher without targeting replacement"),
            Layer != nullptr && !Destroyed->Get_IsCurrent())) { return; }
        Advance();
        Test.TestEqual(TEXT("layout replacement retires once"), Listener->Calls, 7);
        if (!Test.TestTrue(TEXT("new layout admits independent owner"), Push())) { return; }
        auto Current = NewWatcher();
        if (!Test.TestTrue(TEXT("new layout admission observes"), Current->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        Destroyed->Request_Stop();
        Test.TestTrue(TEXT("old watcher stop cannot remove or retire new owner"), Current->Get_IsCurrent() && Layer->ContainsWidget(Owner.Get()));
        auto Ephemeral = NewWatcher();
        if (!Test.TestTrue(TEXT("unrooted watcher GC fixture observes"), Ephemeral->TryObserve(Owner.Get(), Tag, Errors))) { return; }
        const TWeakObjectPtr<UCk_UI_LayerMembership_UE> WeakWatcher{Ephemeral.Get()};
        Ephemeral.Reset();
        CollectGarbage(RF_NoFlags);
        Test.TestTrue(TEXT("ticker and owner do not root watcher"), !WeakWatcher.IsValid() && Current->Get_IsCurrent());
        const TWeakObjectPtr<UCk_UiOwnedSession_TestOwner_UE> WeakOwner{Owner.Get()};
        Subsystem->DestroyLayout();
        Advance();
        Owner.Reset();
        Cover.Reset();
        Layer = nullptr;
        CollectGarbage(RF_NoFlags);
        Test.TestTrue(TEXT("watcher does not root removed owner"), !WeakOwner.IsValid() && !Current->Get_IsCurrent());
        Test.TestTrue(TEXT("all membership retirement callbacks observed revoked state"), Listener->Calls == 8
            && Listener->AllCallbacksSawRetired);
        Subsystem->CreateLayout(Config);
        TStrongObjectPtr<UCk_UiOwnedSession_TestOwner_UE> ReentryOwner{CreateWidget<UCk_UiOwnedSession_TestOwner_UE>(Controller)};
        TStrongObjectPtr<UCk_UiLayerMembership_TestListener_UE> ReentryListener{NewObject<UCk_UiLayerMembership_TestListener_UE>()};
        TStrongObjectPtr<UCk_UI_LayerMembership_UE> ReentryWatcher{NewObject<UCk_UI_LayerMembership_UE>()};
        if (!Test.TestTrue(TEXT("callback reentry fixture admits real owner"),
            Subsystem->PushWidgetToLayer_Instance(Tag, ReentryOwner.Get()) == ReentryOwner.Get()
            && ReentryWatcher->TryObserve(ReentryOwner.Get(), Tag, Errors))) { return; }
        ReentryListener->Reenter = true;
        ReentryListener->CollectDuringCallback = true;
        ReentryListener->ReentrySubsystem = Subsystem;
        ReentryListener->ReentryOwner = ReentryOwner.Get();
        ReentryListener->ReentryTag = Tag;
        const TWeakObjectPtr<UCk_UI_LayerMembership_UE> WeakReentryWatcher{ReentryWatcher.Get()};
        ReentryListener->BeforeCollect = [&ReentryWatcher]() { ReentryWatcher.Reset(); };
        ReentryWatcher->OnRetired.AddDynamic(ReentryListener.Get(), &UCk_UiLayerMembership_TestListener_UE::HandleRetired);
        Subsystem->RemoveWidget(ReentryOwner.Get());
        Advance();
        ReentryListener->BeforeCollect = {};
        auto* ReplacementLayer = Subsystem->Get_Layer(Tag);
        Test.TestTrue(TEXT("retirement callback reentry and GC publish independent new admission"), ReentryListener->Calls == 1
            && ReentryListener->AllCallbacksSawRetired && ReentryListener->ReentrySucceeded && ReentryListener->CallbackLiveAfterGc
            && IsValid(ReentryListener->Replacement) && ReentryListener->Replacement->Get_IsCurrent()
            && ReentryWatcher.Get() == nullptr && WeakReentryWatcher.IsValid() && !WeakReentryWatcher->Get_IsCurrent()
            && IsValid(ReplacementLayer) && ReplacementLayer->ContainsWidget(ReentryOwner.Get()));
        CollectGarbage(RF_NoFlags);
        Test.TestFalse(TEXT("retired watcher is collectible after callback self-keepalive ends"), WeakReentryWatcher.IsValid());
        Test.TestTrue(TEXT("old callback cleanup cannot retire callback-created watcher"),
            IsValid(ReentryListener->Replacement) && ReentryListener->Replacement->Get_IsCurrent());
        if (IsValid(ReentryListener->Replacement)) { ReentryListener->Replacement->Request_Stop(); }
        {
            auto* ForeignController = Player->GetWorld()->GetFirstPlayerController();
            if (!Test.TestTrue(TEXT("context rejection uses another real local-player controller"),
                IsValid(ForeignController) && ForeignController != Controller && ForeignController->IsLocalController())) { return; }
            TStrongObjectPtr<UCk_UI_LayerMembership_UE> ContextWatcher{NewObject<UCk_UI_LayerMembership_UE>()};
            if (!Test.TestTrue(TEXT("context fixture observes exact original controller"),
                ContextWatcher->TryObserve(ReentryOwner.Get(), Tag, Errors))) { return; }
            ReentryOwner->SetOwningPlayer(ForeignController);
            Test.TestFalse(TEXT("foreign real controller reassignment rejects frozen context immediately"), ContextWatcher->Get_IsCurrent());
            Advance();
            ReentryOwner->SetOwningPlayer(Controller);
            Test.TestFalse(TEXT("restoring owner context cannot revive retired admission"), ContextWatcher->Get_IsCurrent());
        }
        Subsystem->RemoveWidget(ReentryOwner.Get());
        Advance();
        TStrongObjectPtr<UCk_UiOwnedSession_TestOwner_UE> ScriptOwner{CreateWidget<UCk_UiOwnedSession_TestOwner_UE>(Controller)};
        if (!Test.TestTrue(TEXT("script probe owner pushes through production layout"),
            Subsystem->PushWidgetToLayer_Instance(Tag, ScriptOwner.Get()) == ScriptOwner.Get())) { return; }
        auto* MembershipClass = UCk_UI_LayerMembership_UE::StaticClass();
        auto* ObserveFunction = MembershipClass->FindFunctionByName(TEXT("TryObserve"));
        auto* StopFunction = MembershipClass->FindFunctionByName(TEXT("Request_Stop"));
        auto* CurrentFunction = MembershipClass->FindFunctionByName(TEXT("Get_IsCurrent"));
        auto* RetiredProperty = FindFProperty<FMulticastDelegateProperty>(MembershipClass, TEXT("OnRetired"));
        Test.TestTrue(TEXT("membership callable and assignable reflection metadata is exposed"), ObserveFunction != nullptr
            && ObserveFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable) && StopFunction != nullptr
            && StopFunction->HasAnyFunctionFlags(FUNC_BlueprintCallable) && CurrentFunction != nullptr
            && CurrentFunction->HasAnyFunctionFlags(FUNC_BlueprintPure) && RetiredProperty != nullptr
            && RetiredProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable));
        TStrongObjectPtr<UCk_UI_LayerMembership_UE> Reflected{NewObject<UCk_UI_LayerMembership_UE>()};
        Test.TestTrue(TEXT("native reflected ProcessEvent admission uses discovered parameter properties"),
            CallStart(Reflected.Get(), TEXT("TryObserve"), ScriptOwner.Get(), Tag) && CallBool(Reflected.Get(), TEXT("Get_IsCurrent")));
        Reflected->Request_Stop();
        UClass* ProbeClass = LoadObject<UClass>(nullptr, TEXT("/Script/Angelscript.Ck_LayerMembership_Probe"));
        if (!Test.TestNotNull(TEXT("actual AngelScript membership probe class loads"), ProbeClass)) { return; }
        TStrongObjectPtr<UObject> Probe{NewObject<UObject>(GetTransientPackage(), ProbeClass)};
        if (!Test.TestTrue(TEXT("actual AngelScript probe admits production layer membership"),
            CallStart(Probe.Get(), TEXT("TryStart"), ScriptOwner.Get(), Tag)
            && CallBool(Probe.Get(), TEXT("Get_Current")) && CallCount(Probe.Get()) == 0)) { return; }
        Subsystem->RemoveWidget(ScriptOwner.Get());
        Advance();
        Test.TestTrue(TEXT("actual AngelScript delegate observes revoked membership exactly once"),
            !CallBool(Probe.Get(), TEXT("Get_Current")) && CallCount(Probe.Get()) == 1
            && CallBool(Probe.Get(), TEXT("Get_CallbacksRetired")));
        Advance();
        Test.TestEqual(TEXT("actual AngelScript retirement remains one-shot"), CallCount(Probe.Get()), 1);
        auto* ProbeStop = Probe->FindFunction(TEXT("Request_Stop"));
        if (!Test.TestTrue(TEXT("actual AngelScript probe exposes zero-argument stop"),
            ProbeStop != nullptr && ProbeStop->NumParms == 0)) { return; }
        FStructOnScope StopParameters{ProbeStop};
        Probe->ProcessEvent(ProbeStop, StopParameters.GetStructMemory());
        Test.TestTrue(TEXT("actual AngelScript stop leaves retired query and event count unchanged"),
            !CallBool(Probe.Get(), TEXT("Get_Current")) && CallCount(Probe.Get()) == 1);
    }
}

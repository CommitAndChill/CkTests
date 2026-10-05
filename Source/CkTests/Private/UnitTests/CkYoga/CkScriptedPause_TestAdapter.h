#pragma once

#include "CkTests/UI/CkPauseMenuProof.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkUI/Layout/CkUI_LayerMembership.h"
#include "CommonActivatableWidget.h"
#include "Blueprint/UserWidget.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_scripted_pause_test
{
    inline auto Function(UObject* InObject, const FName InName, const int32 InInputCount) -> UFunction*
    {
        UFunction* Found = IsValid(InObject) ? InObject->FindFunction(InName) : nullptr;
        if (Found == nullptr || Found->NumParms != InInputCount + (Found->GetReturnProperty() != nullptr ? 1 : 0))
        { return nullptr; }
        return Found;
    }

    inline auto Input(UFunction* InFunction, const int32 InIndex) -> FProperty*
    {
        if (InFunction == nullptr) { return nullptr; }
        int32 Index = 0;
        for (TFieldIterator<FProperty> It(InFunction); It; ++It)
        {
            if (!It->HasAnyPropertyFlags(CPF_Parm) || It->HasAnyPropertyFlags(CPF_ReturnParm)) { continue; }
            if (Index++ == InIndex) { return *It; }
        }
        return nullptr;
    }

    inline auto Object(UObject* InObject, const FName InName) -> UObject*
    {
        UFunction* Call = Function(InObject, InName, 0);
        auto* Result = Call != nullptr ? CastField<FObjectPropertyBase>(Call->GetReturnProperty()) : nullptr;
        if (Result == nullptr) { return nullptr; }
        FStructOnScope Args{Call};
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        UObject* Value = Result->GetObjectPropertyValue_InContainer(Args.GetStructMemory());
        return IsValid(Value) ? Value : nullptr;
    }

    inline auto Bool(UObject* InObject, const FName InName) -> bool
    {
        UFunction* Call = Function(InObject, InName, 0);
        auto* Result = Call != nullptr ? CastField<FBoolProperty>(Call->GetReturnProperty()) : nullptr;
        if (Result == nullptr) { return false; }
        FStructOnScope Args{Call};
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    inline auto String(UObject* InObject, const FName InName) -> FString
    {
        UFunction* Call = Function(InObject, InName, 0);
        auto* Result = Call != nullptr ? CastField<FStrProperty>(Call->GetReturnProperty()) : nullptr;
        if (Result == nullptr) { return {}; }
        FStructOnScope Args{Call};
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    inline auto Settings(UObject* InObject, const FName InName) -> FCk_UiAuthoringProof_Settings
    {
        UFunction* Call = Function(InObject, InName, 0);
        auto* Result = Call != nullptr ? CastField<FStructProperty>(Call->GetReturnProperty()) : nullptr;
        if (Result == nullptr || Result->Struct != FCk_UiAuthoringProof_Settings::StaticStruct()) { return {}; }
        FStructOnScope Args{Call};
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        FCk_UiAuthoringProof_Settings Value;
        Result->CopyCompleteValue(&Value, Result->ContainerPtrToValuePtr<void>(Args.GetStructMemory()));
        return Value;
    }

    inline auto Void(UObject* InObject, const FName InName) -> bool
    {
        UFunction* Call = Function(InObject, InName, 0);
        if (Call == nullptr || Call->GetReturnProperty() != nullptr) { return false; }
        FStructOnScope Args{Call};
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return true;
    }

    inline auto BoolName(UObject* InObject, const FName InFunction, const FName InValue) -> bool
    {
        UFunction* Call = Function(InObject, InFunction, 1);
        auto* Value = CastField<FNameProperty>(Input(Call, 0));
        auto* Result = Call != nullptr ? CastField<FBoolProperty>(Call->GetReturnProperty()) : nullptr;
        if (Value == nullptr || Result == nullptr) { return false; }
        FStructOnScope Args{Call};
        Value->SetPropertyValue_InContainer(Args.GetStructMemory(), InValue);
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    inline auto IntName(UObject* InObject, const FName InFunction, const FName InValue) -> int32
    {
        UFunction* Call = Function(InObject, InFunction, 1);
        auto* Value = CastField<FNameProperty>(Input(Call, 0));
        auto* Result = Call != nullptr ? CastField<FIntProperty>(Call->GetReturnProperty()) : nullptr;
        if (Value == nullptr || Result == nullptr) { return INDEX_NONE; }
        FStructOnScope Args{Call};
        Value->SetPropertyValue_InContainer(Args.GetStructMemory(), InValue);
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    inline auto BoolNameBool(UObject* InObject, const FName InFunction, const FName InName, const bool InEnabled) -> bool
    {
        UFunction* Call = Function(InObject, InFunction, 2);
        auto* Name = CastField<FNameProperty>(Input(Call, 0));
        auto* Enabled = CastField<FBoolProperty>(Input(Call, 1));
        auto* Result = Call != nullptr ? CastField<FBoolProperty>(Call->GetReturnProperty()) : nullptr;
        if (Name == nullptr || Enabled == nullptr || Result == nullptr) { return false; }
        FStructOnScope Args{Call};
        Name->SetPropertyValue_InContainer(Args.GetStructMemory(), InName);
        Enabled->SetPropertyValue_InContainer(Args.GetStructMemory(), InEnabled);
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    inline auto BoolNameText(UObject* InObject, const FName InFunction, const FName InName, const FText& InText) -> bool
    {
        UFunction* Call = Function(InObject, InFunction, 2);
        auto* Name = CastField<FNameProperty>(Input(Call, 0));
        auto* Text = CastField<FTextProperty>(Input(Call, 1));
        auto* Result = Call != nullptr ? CastField<FBoolProperty>(Call->GetReturnProperty()) : nullptr;
        if (Name == nullptr || Text == nullptr || Result == nullptr) { return false; }
        FStructOnScope Args{Call};
        Name->SetPropertyValue_InContainer(Args.GetStructMemory(), InName);
        Text->SetPropertyValue_InContainer(Args.GetStructMemory(), InText);
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    inline auto BoolStrings(UObject* InObject, const FName InFunction,
        const FString& InFirst, const FString& InSecond) -> bool
    {
        UFunction* Call = Function(InObject, InFunction, 2);
        auto* First = CastField<FStrProperty>(Input(Call, 0));
        auto* Second = CastField<FStrProperty>(Input(Call, 1));
        auto* Result = Call != nullptr ? CastField<FBoolProperty>(Call->GetReturnProperty()) : nullptr;
        if (First == nullptr || Second == nullptr || Result == nullptr) { return false; }
        FStructOnScope Args{Call};
        First->SetPropertyValue_InContainer(Args.GetStructMemory(), InFirst);
        Second->SetPropertyValue_InContainer(Args.GetStructMemory(), InSecond);
        InObject->ProcessEvent(Call, Args.GetStructMemory());
        return Result->GetPropertyValue_InContainer(Args.GetStructMemory());
    }

    struct FMenu
    {
        TStrongObjectPtr<UCommonActivatableWidget> Script;

        static auto Open(APlayerController* InPlayer) -> TSharedPtr<FMenu>
        {
            if (!IsValid(InPlayer)) { return {}; }
            UClass* Class = FSoftClassPath{TEXT("/Script/Angelscript.Ck_ScriptedPause_Widget")}
                .TryLoadClass<UCommonActivatableWidget>();
            if (!IsValid(Class)) { return {}; }
            UCommonActivatableWidget* Widget = CreateWidget<UCommonActivatableWidget>(InPlayer, Class);
            if (!IsValid(Widget)) { return {}; }
            TSharedPtr<FMenu> Menu = MakeShared<FMenu>();
            Menu->Script.Reset(Widget);
            if (!Bool(Widget, TEXT("TryOpen"))) { return {}; }
            return Menu;
        }

        auto Get_Session() const -> UCk_UI_AuthoredSession_UE*
        { return Cast<UCk_UI_AuthoredSession_UE>(Object(Script.Get(), TEXT("Get_Session"))); }
        auto Get_Membership() const -> UCk_UI_LayerMembership_UE*
        { return Cast<UCk_UI_LayerMembership_UE>(Object(Script.Get(), TEXT("Get_Membership"))); }
        auto Get_SettingsChild() const -> UCk_PauseMenuProof_UE*
        { return Cast<UCk_PauseMenuProof_UE>(Object(Script.Get(), TEXT("Get_SettingsChild"))); }
        auto Get_AuthoredView() const -> TSharedPtr<FCkUiView>
        { const auto* Session = Get_Session(); return IsValid(Session) ? Session->Get_View() : nullptr; }
        auto Get_SlateUserIndex() const -> int32
        { const auto* Session = Get_Session(); return IsValid(Session) ? Session->Get_SlateUserIndex() : INDEX_NONE; }
        auto Get_IsCurrent() const -> bool { return Bool(Script.Get(), TEXT("Get_IsCurrent")); }
        auto IsActivated() const -> bool { return IsValid(Script.Get()) && Script->IsActivated(); }
        auto Get_SourceError() const -> FString { return String(Script.Get(), TEXT("Get_SourceError")); }
        auto Get_AppliedSettings() const -> FCk_UiAuthoringProof_Settings
        { return Settings(Script.Get(), TEXT("Get_AppliedSettings")); }
        auto Get_ActionCount(const FName InAction) const -> int32
        { return IntName(Script.Get(), TEXT("Get_ActionCount"), InAction); }
        auto Request_SetActionEnabled(const FName InAction, const bool InEnabled) const -> bool
        { return BoolNameBool(Script.Get(), TEXT("Request_SetActionEnabled"), InAction, InEnabled); }
        auto Request_SetLabel(const FName InName, const FText& InText) const -> bool
        { return BoolNameText(Script.Get(), TEXT("Request_SetLabel"), InName, InText); }
        auto Request_ApplySource(const FString& InMarkup, const FString& InStyle) const -> bool
        { return BoolStrings(Script.Get(), TEXT("Request_ApplySource"), InMarkup, InStyle); }
        auto Request_ShowSettings() const -> bool
        { return Bool(Script.Get(), TEXT("Request_ShowSettings")); }
        auto Request_Close() const -> bool { return Void(Script.Get(), TEXT("Request_Close")); }
    };
}

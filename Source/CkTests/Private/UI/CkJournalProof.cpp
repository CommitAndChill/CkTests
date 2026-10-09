#include "CkTests/UI/CkJournalProof.h"
#include "UI/CkJournalProofModel.h"
#include "UI/CkJournalProofArt.h"
#include "UI/CkUiReplicaPresentation.h"

#include "CkCore/Macros/CkMacros.h"
#include "CkSlateLayout/CkUiFontRegistry.h"
#include "CkSlateLayout/CkUiKeyHint.h"
#include "CkSlateLayout/CkUiTreeCollection.h"
#include "CkSlateLayout/SCkUiTree.h"
#include "CkSlateLayout/SCkUiTabs.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSurface_Widget.h"
#include "CkUI/Layout/CkUI_Layout_Subsystem.h"
#include "CkUI/Layout/CkUI_LayoutConfigAsset.h"
#include "CkUI/Layout/CkUI_LayerStack.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Layout/Children.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"

namespace ck_journal_host
{
    auto Layer() -> FGameplayTag
    {
        return FGameplayTag::RequestGameplayTag(TEXT("CkTests.UI.Layer.PauseMenuProof"));
    }

    auto Resource(const TCHAR* InFile) -> FString
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
        return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(),
            TEXT("Resources/UiAuthoring/SkyrimJournalProof"), InFile) : FString{};
    }

    auto FindNode(const FCkUiNode& InNode, const FString& InId) -> const FCkUiNode*
    {
        if (InNode.Id == InId) { return &InNode; }
        for (const auto& Child : InNode.Children)
        {
            if (const auto* Found = FindNode(Child, InId)) { return Found; }
        }
        return nullptr;
    }

    auto FindBoundText(const FCkUiNode& InNode, const FString& InBinding) -> const FCkUiNode*
    {
        if (InNode.Kind == ECkUiNodeKind::Text && InNode.Binding == InBinding)
        {
            return &InNode;
        }
        for (const auto& Child : InNode.Children)
        {
            if (const auto* Found = FindBoundText(Child, InBinding)) { return Found; }
        }
        return nullptr;
    }

    auto FindFocusableAt(const TSharedRef<SWidget>& InRoot, int32& InOutIndex) -> TSharedPtr<SWidget>
    {
        if (InRoot->SupportsKeyboardFocus())
        {
            if (InOutIndex == 0) { return InRoot; }
            --InOutIndex;
        }
        auto* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const auto Found = FindFocusableAt(Children->GetChildAt(Index), InOutIndex);
                Found.IsValid())
            {
                return Found;
            }
        }
        return {};
    }

    auto ObjectiveKey(FName InQuestKey, FName InObjectiveKey) -> FString
    {
        const FString Quest = InQuestKey.ToString();
        return FString::Printf(TEXT("%d:%s%s"), Quest.Len(), *Quest,
            *InObjectiveKey.ToString());
    }

    auto TextField(const FString& InValue) -> FCkUiFieldValue
    {
        auto Field = FCkUiFieldValue{};
        Field.Kind = ECkUiFieldKind::Text;
        Field.Text = FText::FromString(InValue);
        return Field;
    }

    auto ColorField(const FLinearColor& InValue) -> FCkUiFieldValue
    {
        auto Field = FCkUiFieldValue{};
        Field.Kind = ECkUiFieldKind::Color;
        Field.Color = InValue;
        return Field;
    }

    auto AddNode(TArray<FCkUiTreeNodeData>& InOutNodes, const FString& InKey,
        const FString& InName, const FString& InMarker, bool InMuted) -> void
    {
        auto Node = FCkUiTreeNodeData{};
        Node.Key = InKey;
        Node.Fields.Add(TEXT("name"), TextField(InName));
        Node.Fields.Add(TEXT("marker"), TextField(InMarker));
        Node.Fields.Add(TEXT("name-color"), ColorField(InMuted
            ? FLinearColor{0.56f, 0.54f, 0.50f, 1.0f} : FLinearColor{0.94f, 0.92f, 0.85f, 1.0f}));
        InOutNodes.Add(MoveTemp(Node));
    }

    auto IsTabSet(const FCkUiNode& InNode) -> bool
    {
        if (InNode.Kind != ECkUiNodeKind::Tabs || InNode.Binding != TEXT("JournalTab")
            || InNode.Action != TEXT("SelectJournalTab"))
        {
            return false;
        }
        const auto HasKey = [&InNode](const TCHAR* InKey)
        {
            return InNode.Children.ContainsByPredicate([InKey](const FCkUiNode& InChild)
            {
                return InChild.Kind == ECkUiNodeKind::Tab && InChild.TabKey == InKey;
            });
        };
        return HasKey(TEXT("quests")) && HasKey(TEXT("stats")) && HasKey(TEXT("system"));
    }
}

class FCkJournalInput final : public IInputProcessor, public TSharedFromThis<FCkJournalInput>
{
public:
    explicit FCkJournalInput(UCk_JournalProof_UE* InHost) : _Host(InHost) {}

    auto Tick(float, FSlateApplication& InSlate, TSharedRef<ICursor>) -> void override
    {
        if (NOT _Host.IsValid()) { InSlate.UnregisterInputPreProcessor(AsShared()); return; }
        if (_Host->IsJournalOpen() && NOT InSlate.IsActive()) { _Host->Request_Close(); }
    }

    auto HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& InEvent) -> bool override
    {
        if (NOT _Host.IsValid() || InEvent.GetUserIndex() != _Host->_UserIndex) { return false; }
        const FKey Key = InEvent.GetKey();
        if (_Host->IsJournalOpen())
        {
            if (NOT _Host->CanHandleOpenInput()) { return false; }
            if (Key == EKeys::J || Key == EKeys::Escape)
            {
                _HeldKeys.Add(Key);
                if (NOT InEvent.IsRepeat()) { _Host->Request_Close(); }
                return true;
            }
            if (Key == EKeys::Q || Key == EKeys::E)
            {
                _HeldKeys.Add(Key);
                if (NOT InEvent.IsRepeat())
                {
                    const FName Tab = _Host->_Model->Get_TabKey();
                    const FName Next = Key == EKeys::Q
                        ? (Tab == FName{TEXT("quests")} ? FName{TEXT("system")}
                            : Tab == FName{TEXT("stats")} ? FName{TEXT("quests")} : FName{TEXT("stats")})
                        : (Tab == FName{TEXT("quests")} ? FName{TEXT("stats")}
                            : Tab == FName{TEXT("stats")} ? FName{TEXT("system")} : FName{TEXT("quests")});
                    _Host->DoSelectTab(Next.ToString(), true);
                }
                return true;
            }
            if (Key == EKeys::M)
            {
                _HeldKeys.Add(Key);
                if (NOT InEvent.IsRepeat()) { _Host->DoShowMapTarget(); }
                return true;
            }
            return false;
        }
        if (InEvent.IsRepeat() && _HeldKeys.Contains(Key)) { return true; }
        _HeldKeys.Remove(Key);
        if (Key == EKeys::J && NOT InEvent.IsRepeat() && _Host->CanOpenFromInput()
            && _Host->Request_Open())
        {
            _HeldKeys.Add(Key);
            return true;
        }
        return false;
    }

    auto HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& InEvent) -> bool override
    {
        if (NOT _Host.IsValid() || InEvent.GetUserIndex() != _Host->_UserIndex) { return false; }
        return _HeldKeys.Remove(InEvent.GetKey()) > 0;
    }

private:
    TWeakObjectPtr<UCk_JournalProof_UE> _Host;
    TSet<FKey> _HeldKeys;
};

UCk_JournalProof_UE::UCk_JournalProof_UE(const FObjectInitializer& InInitializer)
    : Super(InInitializer)
{
    SetIsFocusable(true);
}

auto UCk_JournalProof_UE::Create(APlayerController* InController) -> UCk_JournalProof_UE*
{
    const bool ValidController = IsValid(InController) && InController->IsLocalController();
    CK_ENSURE_IF_NOT(ValidController, TEXT("Journal requires a local player controller")) {}
    if (NOT ValidController) { return nullptr; }
    const bool ValidContext = IsValid(InController->GetWorld())
        && IsValid(InController->GetLocalPlayer()) && FSlateApplication::IsInitialized();
    CK_ENSURE_IF_NOT(ValidContext, TEXT("Journal requires a world, local player and Slate")) {}
    if (NOT ValidContext) { return nullptr; }
    const auto SlateUser = InController->GetLocalPlayer()->GetSlateUser();
    if (NOT SlateUser.IsValid()) { return nullptr; }
    auto* Host = CreateWidget<UCk_JournalProof_UE>(InController);
    if (NOT IsValid(Host)) { return nullptr; }
    Host->_Model = MakeShared<FCkJournalProofModel>();
    Host->_UserIndex = SlateUser->GetUserIndex();
    Host->_Alive = true;
    Host->_Input = MakeShared<FCkJournalInput>(Host);
    FSlateApplication::Get().RegisterInputPreProcessor(Host->_Input, 0);
    Host->_TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(Host, &ThisClass::DoTick));
    return Host;
}

auto UCk_JournalProof_UE::CanOpenFromInput() const -> bool
{
    const auto* World = GetWorld();
    const auto* ViewportClient = IsValid(World) ? World->GetGameViewport() : nullptr;
    const bool Valid = _Alive && NOT _Open && IsValid(ViewportClient)
        && FSlateApplication::IsInitialized() && NOT World->IsPaused();
    if (NOT Valid) { return false; }
    const auto Viewport = ViewportClient->GetGameViewportWidget();
    if (NOT Viewport.IsValid()) { return false; }
    auto& Slate = FSlateApplication::Get();
    const auto Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
    return Slate.IsActive() && Window.IsValid() && Window->IsActive()
        && Slate.GetUserFocusedWidget(_UserIndex).Get() == Viewport.Get();
}

auto UCk_JournalProof_UE::CanHandleOpenInput() const -> bool
{
    const auto* World = GetWorld();
    const auto* ViewportClient = IsValid(World) ? World->GetGameViewport() : nullptr;
    if (NOT _Alive || NOT _Open || NOT IsValid(ViewportClient)
        || NOT FSlateApplication::IsInitialized())
    {
        return false;
    }
    const auto Viewport = ViewportClient->GetGameViewportWidget();
    if (NOT Viewport.IsValid()) { return false; }
    auto& Slate = FSlateApplication::Get();
    const auto Window = Slate.FindWidgetWindow(Viewport.ToSharedRef());
    if (NOT Slate.IsActive() || NOT Window.IsValid() || NOT Window->IsActive()
        || NOT _View.IsValid() || _View->IsOwnerReleased())
    {
        return false;
    }
    auto* Player = GetOwningLocalPlayer();
    auto* Subsystem = IsValid(Player)
        ? Player->GetSubsystem<UCk_UI_Layout_Subsystem_UE>() : nullptr;
    const auto* Layer = IsValid(Subsystem) ? Subsystem->Get_Layer(ck_journal_host::Layer()) : nullptr;
    if (NOT IsValid(Layer) || Layer->GetActiveWidget() != this) { return false; }
    const auto Root = _View->GetRegion(TEXT("main"));
    const auto Focused = Slate.GetUserFocusedWidget(_UserIndex);
    const auto HostSlate = GetCachedWidget();
    return Focused.Get() == &Root.Get()
        || Slate.HasUserFocusedDescendants(Root, _UserIndex)
        || (HostSlate.IsValid() && Focused == HostSlate);
}

auto UCk_JournalProof_UE::Request_Open() -> bool
{
    if (NOT _Alive || _Open || NOT _Model.IsValid()) { return false; }
    auto* Controller = GetOwningPlayer();
    if (NOT IsValid(Controller) || NOT IsValid(GetWorld())
        || NOT IsValid(GetOwningLocalPlayer()))
    {
        return false;
    }
    auto* Subsystem = GetOwningLocalPlayer()->GetSubsystem<UCk_UI_Layout_Subsystem_UE>();
    if (NOT IsValid(Subsystem)) { return false; }
    const bool HadLayout = Subsystem->Has_Layout();
    if (HadLayout)
    {
        const auto* ExistingLayer = Subsystem->Get_Layer(ck_journal_host::Layer());
        const bool CompatibleLayer = IsValid(ExistingLayer)
            && ExistingLayer->Get_LayerTag() == ck_journal_host::Layer()
            && ExistingLayer->GetActiveWidget() == nullptr;
        if (NOT CompatibleLayer)
        {
            _SourceError = TEXT("The journal layer is unavailable or occupied.");
            return false;
        }
    }
    auto* Config = LoadObject<UCk_UI_LayoutConfigAsset_UE>(nullptr,
        TEXT("/Script/AngelscriptAssets.CkTests_PauseMenuProof_Layout"));
    const auto LayerConfig = IsValid(Config)
        ? Config->Get_LayerConfig(ck_journal_host::Layer()) : TOptional<FCk_UI_LayerConfig>{};
    if (NOT LayerConfig.IsSet())
    {
        _SourceError = TEXT("The journal layer configuration is unavailable.");
        return false;
    }
    if (HadLayout)
    {
        const auto* ExistingLayer = Subsystem->Get_Layer(ck_journal_host::Layer());
        const bool ExactConfiguration = IsValid(ExistingLayer)
            && ExistingLayer->Get_Priority() == LayerConfig->Get_Priority()
            && ExistingLayer->Get_DefaultInputMode() == LayerConfig->Get_InputMode()
            && ExistingLayer->GetClass() == LayerConfig->Get_LayerWidgetClass().Get();
        if (NOT ExactConfiguration)
        {
            _SourceError = TEXT("The existing journal layer has a different configuration.");
            return false;
        }
    }
    if (NOT DoPrepare()) { DoClosePresentation(); return false; }
    if (IsValid(_Surface) && NOT _Surface->TryConfigure(_View, TEXT("main")))
    {
        DoClosePresentation();
        return false;
    }
    const TSharedRef<SWidget> PreparedRoot = TakeWidget();
    const bool Retained = IsValid(_Surface) && _Surface->HasConfiguredView()
        && _Surface->GetCachedWidget().IsValid();
    if (NOT Retained)
    {
        _SourceError = TEXT("The authored journal surface could not be retained.");
        DoClosePresentation();
        return false;
    }
    if (NOT DoBindTreeKeys() || NOT DoSync())
    {
        _SourceError = TEXT("The native journal trees could not be mounted.");
        DoClosePresentation();
        return false;
    }
    if (NOT HadLayout) { Subsystem->CreateLayout(Config); }
    const bool LayerReady = Subsystem->HasLayer(ck_journal_host::Layer())
        && IsValid(Subsystem->Get_Layer(ck_journal_host::Layer()))
        && Subsystem->Get_Layer(ck_journal_host::Layer())->GetActiveWidget() == nullptr;
    if (NOT LayerReady)
    {
        _SourceError = TEXT("The journal layer could not be created vacant.");
        DoClosePresentation();
        if (NOT HadLayout) { Subsystem->DestroyLayout(); }
        return false;
    }
    _PreviousFocus = FSlateApplication::Get().GetUserFocusedWidget(_UserIndex);
    _HadCursor = Controller->bShowMouseCursor;
    _Open = true;
    Controller->bShowMouseCursor = true;
    auto* Pushed = Subsystem->PushWidgetToLayer_Instance(ck_journal_host::Layer(), this);
    if (Pushed != this || NOT _Open || NOT IsValid(_Surface)
        || NOT _Surface->HasConfiguredView())
    {
        _SourceError = TEXT("The journal layer rejected its prepared widget.");
        DoClosePresentation();
        Subsystem->RemoveWidget(this);
        if (NOT HadLayout) { Subsystem->DestroyLayout(); }
        return false;
    }
    if (NOT GetWorld()->IsPaused())
    {
        _OwnsPause = Controller->SetPause(true);
        if (NOT _OwnsPause)
        {
            _SourceError = TEXT("The journal could not acquire pause.");
            DoClosePresentation();
            if (NOT HadLayout) { Subsystem->DestroyLayout(); }
            return false;
        }
    }
    ActivateWidget();
    _OpenElapsed = 0.0f;
    _PollRemaining = 0.5f;
    SetRenderOpacity(0.0f);
    _NeedsFocus = NOT DoFocus();
    _Status.Reset();
    return true;
}

auto UCk_JournalProof_UE::DoPrepare() -> bool
{
    auto Registry = FCkUiWidgetRegistry{};
    const auto Art = RegisterJournalArt(Registry);
    if (NOT Art.Succeeded)
    {
        _SourceError = FString::Join(Art.Errors, TEXT("\n"));
        return false;
    }
    const auto Hints = RegisterKeyHintUiControl(Registry);
    if (NOT Hints.Succeeded)
    {
        _SourceError = FString::Join(Hints.Errors, TEXT("\n"));
        return false;
    }
    _Registry = Registry.CreateSnapshot();
    const auto Schema = TArray<FCkUiFieldSchema>{
        {TEXT("name"), ECkUiFieldKind::Text},
        {TEXT("marker"), ECkUiFieldKind::Text},
        {TEXT("name-color"), ECkUiFieldKind::Color},
    };
    const auto QuestCreated = FCkUiTreeCollection::TryCreate(Schema, _Quests);
    const auto ObjectiveCreated = FCkUiTreeCollection::TryCreate(Schema, _Objectives);
    if (NOT QuestCreated.Succeeded || NOT ObjectiveCreated.Succeeded || NOT DoSync())
    {
        _SourceError = TEXT("Journal tree collections could not be prepared.");
        return false;
    }

    auto Data = FCkUiView::FDataBindings{};
    const auto Weak = TWeakObjectPtr<UCk_JournalProof_UE>{this};
    Data.CanDispatchEvents = TAttribute<bool>::CreateLambda([Weak]()
    {
        return Weak.IsValid() && Weak->IsJournalOpen() && NOT Weak->_Syncing;
    });
    Data.SlateUserIndex = _UserIndex;
    Data.Trees.Add(TEXT("Quests"), _Quests);
    Data.Trees.Add(TEXT("Objectives"), _Objectives);
    Data.Visibility.Add(TEXT("QuestSummaryVisible"), TAttribute<bool>::CreateLambda([Weak]()
    {
        return Weak.IsValid() && Weak->_Model.IsValid()
            && Weak->_Model->Get_SelectedKey() != FName{TEXT("miscellaneous")};
    }));
    Data.Visibility.Add(TEXT("QuestFooterVisible"), TAttribute<bool>::CreateLambda([Weak]()
    {
        return Weak.IsValid() && Weak->_Model.IsValid()
            && Weak->_Model->Get_TabKey() == FName{TEXT("quests")};
    }));
    Data.Visibility.Add(TEXT("CategoryFooterVisible"), TAttribute<bool>::CreateLambda([Weak]()
    {
        return Weak.IsValid() && Weak->_Model.IsValid()
            && Weak->_Model->Get_TabKey() != FName{TEXT("quests")};
    }));
    Data.String.Add(TEXT("JournalTab"), TAttribute<FString>::CreateLambda([Weak]()
    {
        return Weak.IsValid() && Weak->_Model.IsValid()
            ? Weak->_Model->Get_TabKey().ToString() : FString{};
    }));
    Data.Number.Add(TEXT("JournalTabIndex"), TAttribute<float>::CreateLambda([Weak]()
    {
        if (NOT Weak.IsValid() || NOT Weak->_Model.IsValid()) { return 0.0f; }
        const FName Tab = Weak->_Model->Get_TabKey();
        return Tab == FName{TEXT("stats")} ? 1.0f
            : Tab == FName{TEXT("system")} ? 2.0f : 0.0f;
    }));
    Data.StringChanged.Add(TEXT("SelectJournalTab"), FCkUiOnStringChanged::CreateLambda(
        [Weak](FString InKey)
        {
            if (Weak.IsValid()) { Weak->DoSelectTab(InKey); }
        }));
    Data.TreeSelectionChanged.Add(TEXT("SelectQuest"), FOnCkUiTreeSelectionChanged::CreateLambda(
        [Weak](TOptional<FString> InKey, ESelectInfo::Type)
        {
            if (Weak.IsValid()) { Weak->DoSelectQuest(MoveTemp(InKey)); }
        }));
    Data.TreeSelectionChanged.Add(TEXT("SelectObjective"), FOnCkUiTreeSelectionChanged::CreateLambda(
        [Weak](TOptional<FString> InKey, ESelectInfo::Type)
        {
            if (Weak.IsValid()) { Weak->DoSelectObjective(MoveTemp(InKey)); }
        }));
    for (const auto* Name : {TEXT("QuestTitle"), TEXT("QuestSummary"), TEXT("QuestState"),
        TEXT("FooterDate"), TEXT("FooterKeys"), TEXT("FooterStatus"),
        TEXT("StatsSummary"), TEXT("SystemSummary")})
    {
        const FString Key{Name};
        Data.Text.Add(Key, TAttribute<FText>::CreateLambda([Weak, Key]()
        {
            return Weak.IsValid() ? Weak->DoText(Key) : FText{};
        }));
    }
    auto Actions = FCkUiView::FActions{};
    Actions.Add(TEXT("CloseJournal"), FSimpleDelegate::CreateWeakLambda(this,
        [this]() { if (IsJournalOpen()) { Request_Close(); } }));
    TSharedPtr<const FCkUiFontRegistrySnapshot> FontsSnapshot;
    if (NOT ck_ui_replica_presentation::TryLoad(TEXT("Skyrim"), Data, FontsSnapshot, _SourceError))
    {
        return false;
    }
    _View = FCkUiView::Create({}, MoveTemp(Actions), {}, {},
        MoveTemp(Data), _Registry, MoveTemp(FontsSnapshot));
    _View->GetRegion(TEXT("main"));
    _LastMarkup.Reset();
    _LastStyle.Reset();
    return DoReload();
}

auto UCk_JournalProof_UE::DoSync() -> bool
{
    if (NOT _Model.IsValid() || NOT _Quests.IsValid() || NOT _Objectives.IsValid()) { return false; }
    const TOptional<FString> PreviousObjective = _View.IsValid()
        && _View->GetTree(TEXT("objective-list")).IsValid()
        ? _View->GetTree(TEXT("objective-list"))->GetSelectedKey() : TOptional<FString>{};
    auto QuestNodes = TArray<FCkUiTreeNodeData>{};
    auto ObjectiveNodes = TArray<FCkUiTreeNodeData>{};
    auto CandidateObjectiveKeys = TMap<FString, TPair<FName, FName>>{};
    const FName SelectedKey = _Model->Get_SelectedKey();
    const bool Miscellaneous = SelectedKey == FName{TEXT("miscellaneous")};
    int32 MiscCount = 0;
    for (const auto& Quest : _Model->Get_Quests())
    {
        if (Quest.Miscellaneous)
        {
            ++MiscCount;
        }
        else
        {
            const FString Marker = Quest.Completed ? TEXT("\u25c6")
                : Quest.Tracked ? TEXT("\u25b6") : TEXT("\u25c7");
            ck_journal_host::AddNode(QuestNodes, Quest.Key.ToString(), Quest.Title, Marker,
                Quest.Completed);
        }
        if (Quest.Key != SelectedKey && NOT (Miscellaneous && Quest.Miscellaneous))
        {
            continue;
        }
        for (const auto& Objective : Quest.Objectives)
        {
            const FString ObjectiveKey = ck_journal_host::ObjectiveKey(Quest.Key, Objective.Key);
            CandidateObjectiveKeys.Add(ObjectiveKey, TPair<FName, FName>{Quest.Key, Objective.Key});
            const FString Name = Miscellaneous
                ? FString::Printf(TEXT("%s: %s"), *Quest.Title, *Objective.Text) : Objective.Text;
            const FString Marker = Objective.Completed ? TEXT("\u25c6")
                : Quest.Tracked ? TEXT("\u25b6") : TEXT("\u25c7");
            ck_journal_host::AddNode(ObjectiveNodes, ObjectiveKey, Name, Marker,
                Objective.Completed);
        }
    }
    if (MiscCount > 0)
    {
        ck_journal_host::AddNode(QuestNodes, TEXT("miscellaneous"),
            TEXT("Miscellaneous"), TEXT("\u25c7"), false);
    }
    const TOptional<FString> FirstObjective = ObjectiveNodes.IsEmpty()
        ? TOptional<FString>{} : TOptional<FString>{ObjectiveNodes[0].Key};
    _Syncing = true;
    const auto QuestResult = _Quests->TrySetNodes(MoveTemp(QuestNodes));
    const auto ObjectiveResult = QuestResult.Succeeded
        ? _Objectives->TrySetNodes(MoveTemp(ObjectiveNodes)) : FCkUiLoadResult{};
    if (NOT QuestResult.Succeeded || NOT ObjectiveResult.Succeeded)
    {
        _Syncing = false;
        _SourceError = TEXT("Journal tree publication failed.");
        if (_Open) { Request_Close(); }
        return false;
    }
    _ObjectiveKeys = MoveTemp(CandidateObjectiveKeys);
    if (_View.IsValid())
    {
        const auto QuestTree = _View->GetTree(TEXT("quest-list"));
        const auto ObjectiveTree = _View->GetTree(TEXT("objective-list"));
        const TOptional<FString> ObjectiveKey = PreviousObjective.IsSet()
            && _ObjectiveKeys.Contains(PreviousObjective.GetValue()) ? PreviousObjective
            : FirstObjective;
        const bool Admitted = QuestTree.IsValid() && ObjectiveTree.IsValid()
            && QuestTree->TryRequestSelectKey(SelectedKey.ToString(), false, true)
            && ObjectiveTree->TryRequestSelectKey(ObjectiveKey, false, true);
        if (NOT Admitted)
        {
            if (QuestTree.IsValid()) { QuestTree->CancelSelectionRequest(); }
            if (ObjectiveTree.IsValid()) { ObjectiveTree->CancelSelectionRequest(); }
            _Syncing = false;
            _SourceError = TEXT("Journal could not admit its native keyed selection.");
            if (_Open) { Request_Close(); }
            return false;
        }
    }
    _Syncing = false;
    return true;
}

auto UCk_JournalProof_UE::DoText(const FString& InName) const -> FText
{
    if (NOT _Model.IsValid()) { return {}; }
    if (InName == TEXT("FooterDate")) { return FText::FromString(TEXT("17th of Last Seed, 4E 201")); }
    const bool QuestsTab = _Model->Get_TabKey() == FName{TEXT("quests")};
    if (InName == TEXT("FooterKeys"))
    {
        return FText::FromString(QuestsTab
            ? TEXT("Enter Toggle Active     M Show on Map")
            : TEXT("Q / E Categories    Esc Close"));
    }
    if (InName == TEXT("FooterStatus"))
    {
        return QuestsTab ? FText::FromString(_Status) : FText{};
    }
    if (InName == TEXT("StatsSummary"))
    {
        return FText::FromString(FString::Printf(TEXT("QUESTS COMPLETED  %d"),
            _Model->Get_CompletedQuestCount()));
    }
    if (InName == TEXT("SystemSummary"))
    {
        return FText::FromString(TEXT("JOURNAL  /  SYSTEM MENU"));
    }
    const FName SelectedKey = _Model->Get_SelectedKey();
    if (SelectedKey == FName{TEXT("miscellaneous")})
    {
        if (InName == TEXT("QuestTitle")) { return FText::FromString(TEXT("MISCELLANEOUS")); }
        if (InName == TEXT("QuestSummary"))
        {
            return FText::FromString(TEXT("Small tasks and rumors gathered on the road."));
        }
        if (InName == TEXT("QuestState")) { return FText::FromString(TEXT("MISCELLANEOUS")); }
        return {};
    }
    const auto* Quest = _Model->FindQuest(SelectedKey);
    if (Quest == nullptr) { return {}; }
    if (InName == TEXT("QuestTitle")) { return FText::FromString(Quest->Title.ToUpper()); }
    if (InName == TEXT("QuestSummary")) { return FText::FromString(Quest->Summary); }
    if (InName == TEXT("QuestState"))
    {
        return FText::FromString(Quest->Completed ? TEXT("COMPLETED")
            : Quest->Tracked ? TEXT("ACTIVE") : TEXT("AVAILABLE"));
    }
    return {};
}

auto UCk_JournalProof_UE::DoSelectQuest(TOptional<FString> InKey) -> void
{
    if (NOT IsJournalOpen() || _Syncing || NOT InKey.IsSet()) { return; }
    if (_Model->TrySelect(FName{*InKey.GetValue()})) { DoSync(); }
}

auto UCk_JournalProof_UE::DoSelectObjective(TOptional<FString> InKey) -> void
{
    if (NOT IsJournalOpen() || _Syncing || NOT InKey.IsSet()) { return; }
    const auto* Pair = _ObjectiveKeys.Find(InKey.GetValue());
    if (Pair == nullptr || _Model->FindQuest(Pair->Key) == nullptr) { return; }
}

auto UCk_JournalProof_UE::DoSelectTab(const FString& InKey, bool InFromShortcut) -> void
{
    if (NOT IsJournalOpen() || _Syncing) { return; }
    if (NOT _Model->TrySetTab(FName{*InKey})) { return; }
    const auto Tabs = _View.IsValid() ? _View->GetTabs(TEXT("journal-tabs")) : nullptr;
    const auto Focused = FSlateApplication::IsInitialized()
        ? FSlateApplication::Get().GetUserFocusedWidget(_UserIndex) : nullptr;
    if (InFromShortcut || NOT Tabs.IsValid() || NOT Tabs->IsRetainedHeader(Focused))
    {
        _NeedsFocus = NOT DoFocus();
    }
}

auto UCk_JournalProof_UE::DoToggleSelected(bool InObjectiveTree) -> bool
{
    if (NOT IsJournalOpen() || NOT _View.IsValid()) { return false; }
    FName QuestKey = _Model->Get_SelectedKey();
    if (InObjectiveTree)
    {
        const auto Tree = _View->GetTree(TEXT("objective-list"));
        if (NOT Tree.IsValid()) { return false; }
        const auto Selected = Tree->GetSelectedKey();
        const auto* Pair = Selected.IsSet() ? _ObjectiveKeys.Find(Selected.GetValue()) : nullptr;
        if (Pair == nullptr) { return false; }
        QuestKey = Pair->Key;
    }
    const bool Accepted = _Model->TryToggleTracked(QuestKey);
    return Accepted && DoSync();
}

auto UCk_JournalProof_UE::DoShowMapTarget() -> bool
{
    if (NOT IsJournalOpen() || NOT _Model.IsValid()) { return false; }
    FName QuestKey = _Model->Get_SelectedKey();
    if (QuestKey == FName{TEXT("miscellaneous")})
    {
        const auto ObjectiveTree = _View.IsValid() ? _View->GetTree(TEXT("objective-list")) : nullptr;
        const auto Selected = ObjectiveTree.IsValid()
            ? ObjectiveTree->GetSelectedKey() : TOptional<FString>{};
        const auto* Pair = Selected.IsSet() ? _ObjectiveKeys.Find(Selected.GetValue()) : nullptr;
        if (Pair != nullptr) { QuestKey = Pair->Key; }
        else
        {
            for (const auto& Quest : _Model->Get_Quests())
            {
                if (Quest.Miscellaneous && NOT Quest.Completed)
                {
                    QuestKey = Quest.Key;
                    break;
                }
            }
        }
    }
    const auto* Quest = _Model->FindQuest(QuestKey);
    if (Quest == nullptr) { return false; }
    _Status = FString::Printf(TEXT("Target: %s (journal fixture)"), *Quest->Title);
    return true;
}

auto UCk_JournalProof_UE::DoBindTreeKeys() -> bool
{
    if (NOT _View.IsValid()) { return false; }
    const auto QuestTree = _View->GetTree(TEXT("quest-list"));
    const auto ObjectiveTree = _View->GetTree(TEXT("objective-list"));
    if (NOT QuestTree.IsValid() || NOT ObjectiveTree.IsValid()) { return false; }
    const auto Weak = TWeakObjectPtr<UCk_JournalProof_UE>{this};
    QuestTree->SetHostKeyDownHandler(FOnKeyDown::CreateLambda(
        [Weak](const FGeometry&, const FKeyEvent& InEvent)
        {
            if (NOT Weak.IsValid() || NOT Weak->IsJournalOpen()
                || InEvent.GetUserIndex() != Weak->_UserIndex
                || InEvent.GetKey() != EKeys::Enter || InEvent.IsRepeat())
            {
                return FReply::Unhandled();
            }
            Weak->DoToggleSelected(false);
            return FReply::Handled();
        }));
    ObjectiveTree->SetHostKeyDownHandler(FOnKeyDown::CreateLambda(
        [Weak](const FGeometry&, const FKeyEvent& InEvent)
        {
            if (NOT Weak.IsValid() || NOT Weak->IsJournalOpen()
                || InEvent.GetUserIndex() != Weak->_UserIndex
                || InEvent.GetKey() != EKeys::Enter || InEvent.IsRepeat())
            {
                return FReply::Unhandled();
            }
            Weak->DoToggleSelected(true);
            return FReply::Handled();
        }));
    return true;
}

auto UCk_JournalProof_UE::Request_ApplySource(const FString& InMarkup,
    const FString& InStyle) -> bool
{
    if (NOT _View.IsValid() || _View->IsOwnerReleased()) { return false; }
    auto Document = FCkUiDocument{};
    const auto Parsed = FCkUiDocumentParser::TryParse(InMarkup, InStyle, {}, Document,
        TEXT("SkyrimJournalProof"), _Registry);
    if (NOT Parsed.Succeeded)
    {
        _SourceError = FString::Join(Parsed.Errors, TEXT("\n"));
        return false;
    }
    const auto* Main = Document.Regions.Find(TEXT("main"));
    const auto* Journal = Main != nullptr
        ? ck_journal_host::FindNode(*Main, TEXT("journal-region")) : nullptr;
    const auto* Layout = Journal != nullptr
        ? ck_journal_host::FindNode(*Journal, TEXT("journal-layout")) : nullptr;
    const auto* Tabs = Layout != nullptr
        ? ck_journal_host::FindNode(*Layout, TEXT("journal-tabs")) : nullptr;
    const auto* QuestTree = Layout != nullptr
        ? ck_journal_host::FindNode(*Layout, TEXT("quest-list")) : nullptr;
    const auto* ObjectiveTree = Layout != nullptr
        ? ck_journal_host::FindNode(*Layout, TEXT("objective-list")) : nullptr;
    const auto* Title = Layout != nullptr
        ? ck_journal_host::FindBoundText(*Layout, TEXT("QuestTitle")) : nullptr;
    const auto* Summary = Layout != nullptr
        ? ck_journal_host::FindBoundText(*Layout, TEXT("QuestSummary")) : nullptr;
    const bool ValidStructure = Document.Regions.Num() == 1 && Journal != nullptr
        && Layout != nullptr && Tabs != nullptr && ck_journal_host::IsTabSet(*Tabs)
        && QuestTree != nullptr && QuestTree->Kind == ECkUiNodeKind::Tree
        && QuestTree->Binding == TEXT("Quests")
        && QuestTree->SelectionAction == TEXT("SelectQuest")
        && ObjectiveTree != nullptr && ObjectiveTree->Kind == ECkUiNodeKind::Tree
        && ObjectiveTree->Binding == TEXT("Objectives")
        && ObjectiveTree->SelectionAction == TEXT("SelectObjective")
        && Title != nullptr && Title->Kind == ECkUiNodeKind::Text
        && Title->Binding == TEXT("QuestTitle")
        && Summary != nullptr && Summary->Kind == ECkUiNodeKind::Text
        && Summary->Binding == TEXT("QuestSummary");
    if (NOT ValidStructure)
    {
        _SourceError = TEXT("Journal requires its main region, tabs, bound trees and quest detail.");
        return false;
    }
    const auto Result = _View->TryReload(InMarkup, InStyle, TEXT("SkyrimJournalProof"));
    if (NOT Result.Succeeded)
    {
        _SourceError = FString::Join(Result.Errors, TEXT("\n"));
        return false;
    }
    _SourceError.Reset();
    if (_Open && (NOT DoBindTreeKeys() || NOT DoSync()))
    {
        Request_Close();
        return false;
    }
    return true;
}

auto UCk_JournalProof_UE::DoReload() -> bool
{
    FString Markup;
    FString Style;
    if (NOT FFileHelper::LoadFileToString(Markup,
            *ck_journal_host::Resource(TEXT("SkyrimJournalProof.ui.html")))
        || NOT FFileHelper::LoadFileToString(Style,
            *ck_journal_host::Resource(TEXT("SkyrimJournalProof.ui.css"))))
    {
        _SourceError = TEXT("Both authored journal source files are required.");
        return false;
    }
    if (Markup == _LastMarkup && Style == _LastStyle) { return _SourceError.IsEmpty(); }
    _LastMarkup = Markup;
    _LastStyle = Style;
    return Request_ApplySource(Markup, Style);
}

auto UCk_JournalProof_UE::RebuildWidget() -> TSharedRef<SWidget>
{
    if (NOT _View.IsValid()) { return SNullWidget::NullWidget; }
    if (NOT IsValid(WidgetTree)) { WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree")); }
    if (NOT IsValid(WidgetTree->RootWidget))
    {
        auto* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),
            TEXT("JournalOverlay"));
        WidgetTree->RootWidget = Root;
        const auto Fill = [](UOverlaySlot* InSlot)
        {
            InSlot->SetHorizontalAlignment(HAlign_Fill);
            InSlot->SetVerticalAlignment(VAlign_Fill);
        };
        auto* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),
            TEXT("JournalDimmer"));
        Dim->SetBrushColor(FLinearColor{0.018f, 0.018f, 0.016f, 0.72f});
        Dim->SetVisibility(ESlateVisibility::HitTestInvisible);
        Fill(Root->AddChildToOverlay(Dim));
        auto* Safe = WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass(),
            TEXT("JournalSafeZone"));
        Fill(Root->AddChildToOverlay(Safe));
        auto* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),
            TEXT("JournalScale"));
        Scale->SetStretch(EStretch::ScaleToFit);
        Safe->AddChild(Scale);
        auto* Reference = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),
            TEXT("JournalReference"));
        Reference->SetWidthOverride(1920.0f);
        Reference->SetHeightOverride(1080.0f);
        Scale->AddChild(Reference);
        _Surface = WidgetTree->ConstructWidget<UCk_UI_AuthoredSurface_UE>(
            UCk_UI_AuthoredSurface_UE::StaticClass(), TEXT("AuthoredJournal"));
        Reference->AddChild(_Surface);
    }
    const bool Configured = IsValid(_Surface) && _Surface->TryConfigure(_View, TEXT("main"));
    CK_ENSURE_IF_NOT(Configured, TEXT("Journal could not mount its authored surface")) {}
    if (NOT Configured) { return SNullWidget::NullWidget; }
    return Super::RebuildWidget();
}

auto UCk_JournalProof_UE::DoGetFocusTarget() const -> TSharedPtr<SWidget>
{
    if (NOT IsJournalOpen() || NOT _View.IsValid() || NOT _Model.IsValid()) { return nullptr; }
    const FName Tab = _Model->Get_TabKey();
    if (Tab == FName{TEXT("quests")})
    {
        const auto QuestTree = _View->GetTree(TEXT("quest-list"));
        return QuestTree.IsValid() ? QuestTree->GetTree() : nullptr;
    }
    if (Tab == FName{TEXT("system")})
    {
        const auto Close = _View->GetButton(TEXT("journal-close"));
        if (Close.IsValid() && Close->IsEnabled()
            && Close->GetVisibility().IsVisible())
        {
            return Close;
        }
    }
    const auto Tabs = _View->GetTabs(TEXT("journal-tabs"));
    if (NOT Tabs.IsValid()) { return nullptr; }
    int32 Index = Tab == FName{TEXT("stats")} ? 1 : 2;
    return ck_journal_host::FindFocusableAt(Tabs.ToSharedRef(), Index);
}

auto UCk_JournalProof_UE::DoFocus() -> bool
{
    const auto FocusTarget = DoGetFocusTarget();
    if (NOT FocusTarget.IsValid()) { return false; }
    auto& Slate = FSlateApplication::Get();
    Slate.SetUserFocus(_UserIndex, FocusTarget, EFocusCause::Navigation);
    return Slate.GetUserFocusedWidget(_UserIndex) == FocusTarget;
}

auto UCk_JournalProof_UE::DoTick(float InDeltaTime) -> bool
{
    if (NOT _Alive || NOT IsValid(GetOwningPlayer()) || NOT IsValid(GetWorld()))
    {
        Request_Shutdown();
        return false;
    }
    if (NOT FMath::IsFinite(InDeltaTime) || InDeltaTime < 0.0f) { return true; }
    if (_Open)
    {
        const auto QuestTree = _View.IsValid() ? _View->GetTree(TEXT("quest-list")) : nullptr;
        const auto ObjectiveTree = _View.IsValid() ? _View->GetTree(TEXT("objective-list")) : nullptr;
        if (NOT QuestTree.IsValid() || NOT ObjectiveTree.IsValid()
            || QuestTree->GetSelectionRequestState() == ECkUiTreeSelectionRequestState::Rejected
            || ObjectiveTree->GetSelectionRequestState() == ECkUiTreeSelectionRequestState::Rejected)
        {
            _SourceError = TEXT("Journal native keyed selection was rejected.");
            Request_Close();
            return true;
        }
        _OpenElapsed += InDeltaTime;
        SetRenderOpacity(FMath::Clamp(_OpenElapsed / 0.14f, 0.0f, 1.0f));
        if (_NeedsFocus) { _NeedsFocus = NOT DoFocus(); }
        _PollRemaining -= InDeltaTime;
        if (_PollRemaining <= 0.0f)
        {
            _PollRemaining = 0.5f;
            DoReload();
        }
    }
    return true;
}

auto UCk_JournalProof_UE::Request_AdvanceSelected() -> bool
{
    if (NOT _Alive || NOT _Model.IsValid()) { return false; }
    const FName Selected = _Model->Get_SelectedKey();
    for (const auto& Quest : _Model->Get_Quests())
    {
        if (Quest.Key != Selected
            && NOT (Selected == FName{TEXT("miscellaneous")} && Quest.Miscellaneous))
        {
            continue;
        }
        for (const auto& Objective : Quest.Objectives)
        {
            if (Objective.Completed) { continue; }
            const bool Accepted = _Model->TryCompleteObjective(Quest.Key, Objective.Key);
            return Accepted && (NOT _Open || DoSync());
        }
    }
    return false;
}

auto UCk_JournalProof_UE::Request_ToggleStress() -> bool
{
    if (NOT _Alive || NOT _Model.IsValid()) { return false; }
    const bool Next = NOT _Stress;
    if (NOT _Model->TryReset(Next)) { return false; }
    _Stress = Next;
    if (_Open && NOT DoSync()) { return false; }
    _Status = _Stress ? TEXT("Stress fixture: 130 journal entries.")
        : TEXT("Normal journal fixture restored.");
    return true;
}

auto UCk_JournalProof_UE::Request_Reset() -> bool
{
    if (NOT _Alive || NOT _Model.IsValid() || NOT _Model->TryReset()) { return false; }
    _Stress = false;
    if (_Open && NOT DoSync()) { return false; }
    _Status = TEXT("Journal fixture reset.");
    return true;
}

auto UCk_JournalProof_UE::IsJournalOpen() const -> bool
{
    return _Alive && _Open;
}

auto UCk_JournalProof_UE::Get_Summary() const -> FString
{
    return _Status;
}

auto UCk_JournalProof_UE::Get_SourceError() const -> FString
{
    return _SourceError;
}

auto UCk_JournalProof_UE::Request_Close() -> void
{
    DoClosePresentation();
}

auto UCk_JournalProof_UE::DoClosePresentation() -> void
{
    const bool WasOpen = _Open;
    _Open = false;
    _NeedsFocus = false;
    if (_View.IsValid())
    {
        if (const auto QuestTree = _View->GetTree(TEXT("quest-list")); QuestTree.IsValid())
        {
            QuestTree->SetHostKeyDownHandler({});
        }
        if (const auto ObjectiveTree = _View->GetTree(TEXT("objective-list")); ObjectiveTree.IsValid())
        {
            ObjectiveTree->SetHostKeyDownHandler({});
        }
    }
    if (IsValid(_Surface)) { _Surface->ReleaseView(); }
    if (_View.IsValid()) { _View->ReleaseOwnerInteractions(); }
    _View.Reset();
    _Quests.Reset();
    _Objectives.Reset();
    _ObjectiveKeys.Reset();
    auto* Controller = GetOwningPlayer();
    if (_OwnsPause)
    {
        _OwnsPause = false;
        if (IsValid(Controller)) { Controller->SetPause(false); }
    }
    if (WasOpen && IsValid(Controller)) { Controller->bShowMouseCursor = _HadCursor; }
    if (WasOpen)
    {
        DeactivateWidget();
        if (auto* Player = GetOwningLocalPlayer(); IsValid(Player))
        {
            if (auto* Subsystem = Player->GetSubsystem<UCk_UI_Layout_Subsystem_UE>();
                IsValid(Subsystem))
            {
                Subsystem->RemoveWidget(this);
            }
        }
        if (const auto Previous = _PreviousFocus.Pin(); Previous.IsValid()
            && FSlateApplication::IsInitialized())
        {
            FSlateApplication::Get().SetUserFocus(_UserIndex, Previous, EFocusCause::SetDirectly);
        }
    }
}

auto UCk_JournalProof_UE::Request_Shutdown() -> void
{
    if (NOT _Alive) { return; }
    _Alive = false;
    DoClosePresentation();
    if (FSlateApplication::IsInitialized() && _Input.IsValid())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(_Input);
    }
    _Input.Reset();
    if (_TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(_TickHandle); }
    _TickHandle.Reset();
}

auto UCk_JournalProof_UE::NativeGetDesiredFocusTarget() const -> UWidget*
{
    return const_cast<UCk_JournalProof_UE*>(this);
}

auto UCk_JournalProof_UE::NativeOnFocusReceived(const FGeometry& InGeometry,
    const FFocusEvent& InEvent) -> FReply
{
    if (_Open && IsActivated() && _View.IsValid() && InEvent.GetUser() == _UserIndex)
    {
        const auto Target = DoGetFocusTarget();
        if (Target.IsValid())
        {
            _NeedsFocus = false;
            return FReply::Handled().SetUserFocus(Target.ToSharedRef(), EFocusCause::Navigation);
        }
    }
    return Super::NativeOnFocusReceived(InGeometry, InEvent);
}

auto UCk_JournalProof_UE::NativeOnDeactivated() -> void
{
    if (_Open) { Request_Close(); }
    Super::NativeOnDeactivated();
}

auto UCk_JournalProof_UE::NativeDestruct() -> void
{
    if (_Open) { Request_Close(); }
    Super::NativeDestruct();
}

auto UCk_JournalProof_UE::BeginDestroy() -> void
{
    Request_Shutdown();
    Super::BeginDestroy();
}

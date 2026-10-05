#include "CkUiAuthoredSession_TestListener.h"

#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkWidgets/Authoring/CkAuthoredSurface_Widget.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"
#include "Styling/CoreStyle.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_authored_session
{
    auto Config() -> FCk_UI_AuthoredSession_Config
    {
        FCk_UI_AuthoredSession_Config Result;
        Result.Set_Text({FCk_UI_AuthoredText{TEXT("label"), FText::FromString(TEXT("Initial"))}});
        Result.Set_Number({FCk_UI_AuthoredNumber{TEXT("amount"), 20.0f}});
        Result.Set_Bool({FCk_UI_AuthoredBool{TEXT("enabled"), false}});
        Result.Set_Actions({TEXT("fire")});
        return Result;
    }

    auto Markup() -> FString
    {
        return TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
            "<text id=\"label\" bind=\"label\"/>"
            "<search id=\"query\" bind=\"label\" placeholder=\"Propose text\"/>"
            "<slider id=\"amount-control\" class=\"slider\" value-bind=\"amount\" interaction=\"amount\" min=\"0\" max=\"100\" step=\"10\"/>"
            "<checkbox id=\"enabled-control\" value-bind=\"enabled\" changed=\"enabled\" label-bind=\"label\"/>"
            "<button id=\"fire\" action=\"fire\">Fire</button>"
            "</column></region></ui>");
    }

    auto Styles() -> FString { return TEXT(".slider { width: 300px; height: 32px; }"); }

    auto CollectionRecord(const FString& InKey, const FString& InTitle = TEXT("Original")) -> FCk_UI_AuthoredRecord
    {
        FCk_UI_AuthoredFieldValue Title{TEXT("title"), ECk_UI_AuthoredFieldKind::Text};
        Title.Set_Text(FText::FromString(InTitle));
        FCk_UI_AuthoredFieldValue Amount{TEXT("amount"), ECk_UI_AuthoredFieldKind::Number}; Amount.Set_Number(3.5f);
        FCk_UI_AuthoredFieldValue Enabled{TEXT("enabled"), ECk_UI_AuthoredFieldKind::Bool}; Enabled.Set_Bool(true);
        FCk_UI_AuthoredFieldValue Count{TEXT("count"), ECk_UI_AuthoredFieldKind::Integer}; Count.Set_Integer(-12);
        FCk_UI_AuthoredFieldValue Tint{TEXT("tint"), ECk_UI_AuthoredFieldKind::Color}; Tint.Set_Color(FLinearColor(0.1f, 0.2f, 0.3f, 0.4f));
        FCk_UI_AuthoredRecord Result{InKey};
        // Reverse order exercises schema-ordered readback independently of caller field ordering.
        Result.Set_Fields({Tint, Count, Enabled, Amount, Title});
        return Result;
    }

    auto CollectionDeclaration(const FString& InName = TEXT("items")) -> FCk_UI_AuthoredCollection
    {
        FCk_UI_AuthoredFieldSchema Optional{TEXT("subtitle"), ECk_UI_AuthoredFieldKind::Text}; Optional.Set_Required(false);
        FCk_UI_AuthoredCollection Result{InName};
        Result.Set_Schema({{TEXT("title"), ECk_UI_AuthoredFieldKind::Text},
            {TEXT("amount"), ECk_UI_AuthoredFieldKind::Number}, {TEXT("enabled"), ECk_UI_AuthoredFieldKind::Bool},
            {TEXT("count"), ECk_UI_AuthoredFieldKind::Integer}, {TEXT("tint"), ECk_UI_AuthoredFieldKind::Color}, Optional});
        Result.Set_Records({CollectionRecord(TEXT("a")), CollectionRecord(TEXT("b"))});
        return Result;
    }

    template <typename TWidget>
    auto Find(const TSharedRef<SWidget>& InRoot, const FString& InType) -> TSharedPtr<TWidget>
    {
        if (InRoot->GetTypeAsString() == InType) { return StaticCastSharedRef<TWidget>(InRoot); }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            const auto Found = Find<TWidget>(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InType);
            if (Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto Child(const TSharedRef<SWidget>& InRoot) -> TSharedPtr<SWidget>
    {
        const FChildren* Children = InRoot->GetChildren();
        return Children != nullptr && Children->Num() == 1
            ? ConstCastSharedRef<SWidget>(Children->GetChildAt(0)) : TSharedPtr<SWidget>{};
    }

    auto Bind(UCk_UI_AuthoredSession_UE& InSession, UCk_UiAuthoredSession_TestListener_UE& InListener) -> void
    {
        InListener.Session = &InSession;
        InSession.OnAction.AddDynamic(&InListener, &UCk_UiAuthoredSession_TestListener_UE::HandleAction);
        InSession.OnTextChanged.AddDynamic(&InListener, &UCk_UiAuthoredSession_TestListener_UE::HandleText);
        InSession.OnNumberChanged.AddDynamic(&InListener, &UCk_UiAuthoredSession_TestListener_UE::HandleNumber);
        InSession.OnBoolChanged.AddDynamic(&InListener, &UCk_UiAuthoredSession_TestListener_UE::HandleBool);
    }

    auto Key(const FKey InKey) -> FKeyEvent { return FKeyEvent(InKey, FModifierKeysState{}, 0, false, 0, 0); }
    auto Toggle(const TSharedRef<SCheckBox>& InCheckbox) -> void
    {
        InCheckbox->OnKeyDown(InCheckbox->GetCachedGeometry(), Key(EKeys::SpaceBar));
        InCheckbox->OnKeyUp(InCheckbox->GetCachedGeometry(), Key(EKeys::SpaceBar));
    }
    auto Advance(const TSharedRef<SSlider>& InSlider) -> void
    {
        InSlider->OnNavigation(InSlider->GetCachedGeometry(),
            FNavigationEvent(FModifierKeysState{}, 0, EUINavigation::Right, ENavigationGenesis::Keyboard));
    }
    auto Tick(FSlateApplication& InSlate) -> void { InSlate.PumpMessages(); InSlate.Tick(); InSlate.Tick(); }

    struct FWindowScope final
    {
        TSharedPtr<SWindow> Window;
        ~FWindowScope()
        {
            if (Window.IsValid() && FSlateApplication::IsInitialized())
            { FSlateApplication::Get().DestroyWindowImmediately(Window.ToSharedRef()); }
        }
    };

    struct FScratch final
    {
        FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/UiSessions"), FGuid::NewGuid().ToString());
        TArray<FString> Files;
        auto Path(const TCHAR* InName) -> FString
        {
            IFileManager::Get().MakeDirectory(*Directory, true);
            const FString File = FPaths::Combine(Directory, InName);
            Files.Add(File);
            return File;
        }
        ~FScratch()
        {
            for (const FString& File : Files) { IFileManager::Get().Delete(*File); }
            IFileManager::Get().DeleteDirectory(*Directory, false, false);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiAuthoredSession_Admission,
    "Ck.UiAuthoring.UMG.Session.AdmissionAndScalars",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkUiAuthoredSession_Admission::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_authored_session;
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Session{NewObject<UCk_UI_AuthoredSession_UE>()};
    TArray<FString> Errors;
    TestFalse(TEXT("Default session is not usable"), Session->Get_IsUsable());
    TestFalse(TEXT("Uninitialized session cannot reload"), Session->TryReload(Markup(), Styles(), Errors));
    TestFalse(TEXT("Uninitialized session cannot mutate declarations"), Session->TrySetText(TEXT("label"), FText::GetEmpty()));
    TestFalse(TEXT("Uninitialized session cannot expose a partial view"), Session->Get_View().IsValid());

    TArray<FCk_UI_AuthoredSession_Config> Invalid;
    auto Duplicate = Config();
    Duplicate.Set_Text({FCk_UI_AuthoredText{TEXT("label"), FText::FromString(TEXT("First"))},
        FCk_UI_AuthoredText{TEXT("label"), FText::FromString(TEXT("Second"))}});
    Invalid.Add(Duplicate);
    auto CrossType = Config();
    CrossType.Set_Number({FCk_UI_AuthoredNumber{TEXT("label"), 10.0f}});
    Invalid.Add(CrossType);
    auto ActionCollision = Config(); ActionCollision.Set_Actions({TEXT("label")}); Invalid.Add(ActionCollision);
    auto DuplicateAction = Config(); DuplicateAction.Set_Actions({TEXT("fire"), TEXT("fire")}); Invalid.Add(DuplicateAction);
    auto EmptyRegions = Config(); EmptyRegions.Set_Regions({}); Invalid.Add(EmptyRegions);
    auto DuplicateRegions = Config(); DuplicateRegions.Set_Regions({TEXT("main"), TEXT("main")}); Invalid.Add(DuplicateRegions);
    for (const FCk_UI_AuthoredButtonRequirement& Requirement : {
        FCk_UI_AuthoredButtonRequirement{},
        FCk_UI_AuthoredButtonRequirement{TEXT("bad id"), TEXT("fire"), TEXT("enabled")},
        FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT(""), TEXT("enabled")},
        FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("missing"), TEXT("enabled")},
        FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("")},
        FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("missing")},
        FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("label")}})
    {
        auto BadRequirement = Config(); BadRequirement.Set_RequiredButtons({Requirement}); Invalid.Add(BadRequirement);
    }
    auto DuplicateRequirements = Config();
    DuplicateRequirements.Set_RequiredButtons({
        FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("enabled")},
        FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("enabled")}});
    Invalid.Add(DuplicateRequirements);
    for (const FString& Region : {FString{}, FString{TEXT("bad region")}, FString{TEXT("missing")}})
    {
        auto Requirement = FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("enabled")};
        Requirement.Set_Region(Region);
        auto BadRegion = Config(); BadRegion.Set_RequiredButtons({Requirement}); Invalid.Add(BadRegion);
    }
    for (const FString& Name : {FString{}, FString{TEXT("bad name")}, FString{TEXT("@internal")}})
    {
        auto BadName = Config(); BadName.Set_Bool({FCk_UI_AuthoredBool{Name, true}}); Invalid.Add(BadName);
        auto BadRegion = Config(); BadRegion.Set_Regions({Name}); Invalid.Add(BadRegion);
    }
    for (const float Value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        auto Nonfinite = Config(); Nonfinite.Set_Number({FCk_UI_AuthoredNumber{TEXT("amount"), Value}}); Invalid.Add(Nonfinite);
    }
    for (const FCk_UI_AuthoredSession_Config& Candidate : Invalid)
    {
        TestFalse(TEXT("Malformed declaration set rejects atomically"), Session->TryInitialize(Candidate, Errors));
        TestTrue(TEXT("Rejected declarations report diagnostics and publish no view"), !Errors.IsEmpty()
            && !Session->Get_IsUsable() && !Session->Get_View().IsValid() && Session->Get_Revision() == 0);
        FText Untouched = FText::FromString(TEXT("sentinel"));
        TestFalse(TEXT("Rejected initialization publishes no accepted scalar subset"), Session->TryGetText(TEXT("label"), Untouched));
        TestEqual(TEXT("Getter rejection preserves output"), Untouched.ToString(), FString(TEXT("sentinel")));
    }
    auto Accepted = Config();
    if (!TestTrue(TEXT("A valid configuration can retry after every rejection"), Session->TryInitialize(Accepted, Errors)))
    { for (const FString& Error : Errors) { AddError(Error); } return false; }
    TestTrue(TEXT("Successful initialization has a usable complete native view"), Errors.IsEmpty()
        && Session->Get_IsUsable() && Session->Get_View().IsValid());
    Accepted.Set_Text({FCk_UI_AuthoredText{TEXT("label"), FText::FromString(TEXT("Caller changed"))}});
    FText Text;
    float Number = 0.0f;
    bool Boolean = true;
    TestTrue(TEXT("Accepted values are independent of caller configuration"), Session->TryGetText(TEXT("label"), Text)
        && Text.ToString() == TEXT("Initial") && Session->TryGetNumber(TEXT("amount"), Number)
        && Number == 20.0f && Session->TryGetBool(TEXT("enabled"), Boolean) && !Boolean);
    const TSharedPtr<FCkUiView> AcceptedView = Session->Get_View();
    TestFalse(TEXT("Initialization succeeds only once"), Session->TryInitialize(Accepted, Errors));
    TestTrue(TEXT("Second initialization preserves accepted native identity"), Session->Get_View() == AcceptedView);

    TestTrue(TEXT("Typed setters accept declared values"), Session->TrySetText(TEXT("label"), FText::FromString(TEXT("Accepted")))
        && Session->TrySetNumber(TEXT("amount"), 40.0f) && Session->TrySetBool(TEXT("enabled"), true));
    TestFalse(TEXT("Text setter rejects unknown and wrong-type names"), Session->TrySetText(TEXT("missing"), FText::GetEmpty())
        || Session->TrySetText(TEXT("amount"), FText::GetEmpty()));
    TestFalse(TEXT("Number setter rejects wrong type and nonfinite values"), Session->TrySetNumber(TEXT("label"), 1.0f)
        || Session->TrySetNumber(TEXT("amount"), std::numeric_limits<float>::quiet_NaN())
        || Session->TrySetNumber(TEXT("amount"), std::numeric_limits<float>::infinity()));
    TestFalse(TEXT("Bool setter rejects unknown and wrong-type names"), Session->TrySetBool(TEXT("missing"), false)
        || Session->TrySetBool(TEXT("label"), false));
    TestTrue(TEXT("Rejected scalar edits preserve every accepted value"), Session->TryGetText(TEXT("label"), Text)
        && Text.ToString() == TEXT("Accepted") && Session->TryGetNumber(TEXT("amount"), Number)
        && Number == 40.0f && Session->TryGetBool(TEXT("enabled"), Boolean) && Boolean);
    Text = FText::FromString(TEXT("sentinel")); Number = -7.0f; Boolean = true;
    TestFalse(TEXT("Typed getters reject unknown and wrong-type names"), Session->TryGetText(TEXT("amount"), Text)
        || Session->TryGetNumber(TEXT("enabled"), Number) || Session->TryGetBool(TEXT("missing"), Boolean));
    TestTrue(TEXT("Rejected getters preserve all caller outputs"), Text.ToString() == TEXT("sentinel") && Number == -7.0f && Boolean);

    Session->Request_Release(); Session->Request_Release();
    TestTrue(TEXT("Explicit release is terminal and retires the held native owner"), !Session->Get_IsUsable()
        && AcceptedView.IsValid() && AcceptedView->IsOwnerReleased());
    TestFalse(TEXT("Released session cannot initialize again"), Session->TryInitialize(Config(), Errors));
    TestFalse(TEXT("Released session cannot edit or reload"), Session->TrySetNumber(TEXT("amount"), 0.0f)
        || Session->TryReload(Markup(), Styles(), Errors));
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> NeverInitialized{NewObject<UCk_UI_AuthoredSession_UE>()};
    NeverInitialized->Request_Release();
    TestFalse(TEXT("Release before initialization is also terminal"), NeverInitialized->TryInitialize(Config(), Errors));
    TestTrue(TEXT("Never-initialized released session publishes no native owner"), !NeverInitialized->Get_IsUsable()
        && !NeverInitialized->Get_View().IsValid());
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> SeparateNamespace{NewObject<UCk_UI_AuthoredSession_UE>()};
    ON_SCOPE_EXIT { SeparateNamespace->Request_Release(); };
    auto RegionNamedLikeScalar = Config(); RegionNamedLikeScalar.Set_Regions({TEXT("label")});
    TestTrue(TEXT("Regions use a separate namespace from scalar declarations"),
        SeparateNamespace->TryInitialize(RegionNamedLikeScalar, Errors)
        && SeparateNamespace->TryReload(TEXT("<ui version=\"1\"><region name=\"label\"><text id=\"content\" bind=\"label\"/></region></ui>"), TEXT(""), Errors)
        && SeparateNamespace->TryGetText(TEXT("label"), Text) && Text.ToString() == TEXT("Initial"));

    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Collections{NewObject<UCk_UI_AuthoredSession_UE>()};
    ON_SCOPE_EXIT { Collections->Request_Release(); };
    auto CollectionConfig = Config(); CollectionConfig.Set_Collections({CollectionDeclaration()});
    TArray<FCk_UI_AuthoredSession_Config> InvalidCollections;
    const auto AddBadCollection = [&InvalidCollections](const FCk_UI_AuthoredCollection& InBad)
    {
        auto Candidate = Config(); Candidate.Set_Collections({CollectionDeclaration(), InBad});
        InvalidCollections.Add(MoveTemp(Candidate));
    };
    auto BadSchema = CollectionDeclaration(TEXT("later"));
    auto Schema = BadSchema.Get_Schema(); if (!TestTrue(TEXT("Collection schema fixture has six fields"), Schema.Num() == 6)) { return false; } Schema[0].Set_Kind(static_cast<ECk_UI_AuthoredFieldKind>(255));
    BadSchema.Set_Schema(Schema); AddBadCollection(BadSchema);
    BadSchema = CollectionDeclaration(TEXT("later")); Schema = BadSchema.Get_Schema();
    if (!TestTrue(TEXT("Duplicate schema fixture has fields"), !Schema.IsEmpty())) { return false; }
    const FCk_UI_AuthoredFieldSchema DuplicateSchemaField = Schema[0];
    Schema.Add(DuplicateSchemaField);
    BadSchema.Set_Schema(Schema); AddBadCollection(BadSchema);
    auto BadRecord = CollectionRecord(TEXT("bad"));
    auto Fields = BadRecord.Get_Fields();
    if (!TestTrue(TEXT("Record fixture has all five supplied fields"), Fields.Num() == 5)) { return false; }
    const FCk_UI_AuthoredFieldValue DuplicateRecordField = Fields.Last();
    Fields.Add(DuplicateRecordField); BadRecord.Set_Fields(Fields);
    auto BadCollection = CollectionDeclaration(TEXT("later")); BadCollection.Set_Records({BadRecord}); AddBadCollection(BadCollection);
    BadRecord = CollectionRecord(TEXT("bad")); Fields = BadRecord.Get_Fields(); if (!TestTrue(TEXT("Record fixture has all five supplied fields"), Fields.Num() == 5)) { return false; }
    Fields[0].Set_Kind(static_cast<ECk_UI_AuthoredFieldKind>(255)); BadRecord.Set_Fields(Fields);
    BadCollection.Set_Records({BadRecord}); AddBadCollection(BadCollection);
    for (const float Nonfinite : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        BadRecord = CollectionRecord(TEXT("bad")); Fields = BadRecord.Get_Fields(); if (!TestTrue(TEXT("Record fixture has all five supplied fields"), Fields.Num() == 5)) { return false; } Fields[3].Set_Number(Nonfinite);
        BadRecord.Set_Fields(Fields); BadCollection.Set_Records({BadRecord}); AddBadCollection(BadCollection);
        BadRecord = CollectionRecord(TEXT("bad")); Fields = BadRecord.Get_Fields(); if (!TestTrue(TEXT("Record fixture has all five supplied fields"), Fields.Num() == 5)) { return false; } Fields[0].Set_Color(FLinearColor(Nonfinite, 0, 0, 1));
        BadRecord.Set_Fields(Fields); BadCollection.Set_Records({BadRecord}); AddBadCollection(BadCollection);
    }
    auto DuplicateCollection = CollectionConfig;
    DuplicateCollection.Set_Collections({CollectionDeclaration(), CollectionDeclaration()}); InvalidCollections.Add(DuplicateCollection);
    auto CollectionCollision = CollectionConfig;
    CollectionCollision.Set_Collections({CollectionDeclaration(TEXT("label"))}); InvalidCollections.Add(CollectionCollision);
    for (const auto& Candidate : InvalidCollections)
    {
        TestFalse(TEXT("Invalid later collection rejects the entire declaration set"), Collections->TryInitialize(Candidate, Errors));
        TArray<FCk_UI_AuthoredRecord> RejectedRecords{FCk_UI_AuthoredRecord{TEXT("sentinel")}};
        TestTrue(TEXT("Failed collection admission exposes no earlier model, scalar or view"), !Errors.IsEmpty()
            && !Collections->Get_IsUsable() && !Collections->Get_View().IsValid()
            && !Collections->Get_Collection(TEXT("items")).IsValid()
            && !Collections->TryGetRecords(TEXT("items"), RejectedRecords)
            && RejectedRecords.Num() == 1 && RejectedRecords[0].Get_Key() == TEXT("sentinel"));
        FText RejectedScalar;
        TestFalse(TEXT("Collection admission cannot leave an accepted scalar subset"), Collections->TryGetText(TEXT("label"), RejectedScalar));
    }
    if (!TestTrue(TEXT("Complete collection configuration retries after every atomic rejection"), Collections->TryInitialize(CollectionConfig, Errors)))
    { for (const auto& Error : Errors) { AddError(Error); } return false; }
    const auto Model = Collections->Get_Collection(TEXT("items"));
    if (!TestTrue(TEXT("Reflected declaration owns a real native collection"), Model.IsValid())) { return false; }
    const auto OriginalA = Model->FindRecord(TEXT("a"));
    const auto OriginalB = Model->FindRecord(TEXT("b"));
    if (!TestTrue(TEXT("Seeded keys resolve to native record identities"), OriginalA.IsValid() && OriginalB.IsValid())) { return false; }
    TArray<FCk_UI_AuthoredRecord> Readback;
    if (!TestTrue(TEXT("Reflected readback returns both complete seeded records"), Collections->TryGetRecords(TEXT("items"), Readback)
        && Readback.Num() == 2 && Readback[0].Get_Fields().Num() == 5)) { return false; }
    const auto& Values = Readback[0].Get_Fields();
    TestTrue(TEXT("All five kinds preserve names, discriminants and typed values in schema order"),
        Values[0].Get_Name() == TEXT("title") && Values[0].Get_Kind() == ECk_UI_AuthoredFieldKind::Text && Values[0].Get_Text().ToString() == TEXT("Original")
        && Values[1].Get_Name() == TEXT("amount") && Values[1].Get_Kind() == ECk_UI_AuthoredFieldKind::Number && Values[1].Get_Number() == 3.5f
        && Values[2].Get_Name() == TEXT("enabled") && Values[2].Get_Kind() == ECk_UI_AuthoredFieldKind::Bool && Values[2].Get_Bool()
        && Values[3].Get_Name() == TEXT("count") && Values[3].Get_Kind() == ECk_UI_AuthoredFieldKind::Integer && Values[3].Get_Integer() == -12
        && Values[4].Get_Name() == TEXT("tint") && Values[4].Get_Kind() == ECk_UI_AuthoredFieldKind::Color
        && Values[4].Get_Color() == FLinearColor(0.1f, 0.2f, 0.3f, 0.4f));
    TestTrue(TEXT("Omitted optional field remains absent in native and reflected readback"), OriginalA->FindField(TEXT("subtitle")) == nullptr
        && !Values.ContainsByPredicate([](const auto& Value) { return Value.Get_Name() == TEXT("subtitle"); }));
    Readback = {FCk_UI_AuthoredRecord{TEXT("sentinel")}};
    int64 UntouchedRevision = -37;
    TestFalse(TEXT("Record getter rejects scalar names"), Collections->TryGetRecords(TEXT("label"), Readback));
    TestFalse(TEXT("Collection revision getter rejects unknown names"), Collections->TryGetCollectionRevision(TEXT("missing"), UntouchedRevision));
    TestTrue(TEXT("Typed collection getter failures preserve caller outputs"), Readback.Num() == 1
        && Readback[0].Get_Key() == TEXT("sentinel") && UntouchedRevision == -37);

    int32 ChangedCalls = 0;
    const FDelegateHandle ChangedHandle = Model->OnChanged().AddLambda([&ChangedCalls] { ++ChangedCalls; });
    ON_SCOPE_EXIT { Model->OnChanged().Remove(ChangedHandle); };
    const int64 InitialCollectionRevision = Model->GetRevision();
    const int64 InitialViewRevision = Collections->Get_Revision();
    int64 ReflectedRevision = -1;
    TestTrue(TEXT("Reflected revision reads the admitted native model revision"),
        Collections->TryGetCollectionRevision(TEXT("items"), ReflectedRevision) && ReflectedRevision == InitialCollectionRevision);
    TestFalse(TEXT("Unknown collection replacement rejects without a fallback model"),
        Collections->TrySetRecords(TEXT("missing"), {CollectionRecord(TEXT("a"))}, Errors));
    TestTrue(TEXT("Unknown collection rejection preserves model and publishes no event"), !Errors.IsEmpty()
        && !Collections->Get_Collection(TEXT("missing")).IsValid() && Model->GetRevision() == InitialCollectionRevision && ChangedCalls == 0);
    TestTrue(TEXT("Identical complete replacement succeeds without publication"), Collections->TrySetRecords(TEXT("items"),
        {CollectionRecord(TEXT("a")), CollectionRecord(TEXT("b"))}, Errors)
        && Model->GetRevision() == InitialCollectionRevision && ChangedCalls == 0);
    TestTrue(TEXT("Pure reorder publishes exactly one collection revision and event"), Collections->TrySetRecords(TEXT("items"),
        {CollectionRecord(TEXT("b")), CollectionRecord(TEXT("a"))}, Errors)
        && Model->GetRevision() == InitialCollectionRevision + 1 && ChangedCalls == 1
        && Model->GetRecords().Num() == 2 && Model->GetRecords()[0] == OriginalB && Model->GetRecords()[1] == OriginalA);
    TestTrue(TEXT("Collection replacement preserves native model identity and document revision"),
        Collections->Get_Collection(TEXT("items")) == Model && Collections->Get_Revision() == InitialViewRevision);
    const int64 BeforeRejectRevision = Model->GetRevision();
    const int32 BeforeRejectEvents = ChangedCalls;
    TArray<TArray<FCk_UI_AuthoredRecord>> InvalidReplacements;
    for (const auto& Candidate : InvalidCollections)
    {
        if (Candidate.Get_Collections().Num() == 2 && Candidate.Get_Collections()[1].Get_Name() == TEXT("later"))
        { InvalidReplacements.Add(Candidate.Get_Collections()[1].Get_Records()); }
    }
    // Required-field, wrong-type, unknown-field and duplicate-key validation occurs after conversion.
    BadRecord = CollectionRecord(TEXT("bad")); Fields = BadRecord.Get_Fields(); if (!TestTrue(TEXT("Record fixture has all five supplied fields"), Fields.Num() == 5)) { return false; } Fields.RemoveAt(4);
    BadRecord.Set_Fields(Fields); InvalidReplacements.Add({BadRecord});
    BadRecord = CollectionRecord(TEXT("bad")); Fields = BadRecord.Get_Fields(); if (!TestTrue(TEXT("Record fixture has all five supplied fields"), Fields.Num() == 5)) { return false; } Fields[4].Set_Kind(ECk_UI_AuthoredFieldKind::Bool);
    BadRecord.Set_Fields(Fields); InvalidReplacements.Add({BadRecord});
    BadRecord = CollectionRecord(TEXT("bad")); Fields = BadRecord.Get_Fields(); if (!TestTrue(TEXT("Record fixture has all five supplied fields"), Fields.Num() == 5)) { return false; } Fields[4].Set_Name(TEXT("undeclared"));
    BadRecord.Set_Fields(Fields); InvalidReplacements.Add({BadRecord});
    InvalidReplacements.Add({CollectionRecord(TEXT("a")), CollectionRecord(TEXT("a"))});
    for (const auto& Candidate : InvalidReplacements)
    {
        // Schema-only invalid configurations still contain valid records; test only malformed data here.
        if (Candidate.Num() == 2 && Candidate[0].Get_Key() == TEXT("a") && Candidate[1].Get_Key() == TEXT("b")) { continue; }
        TestFalse(TEXT("Malformed replacement rejects without publishing a record subset"), Collections->TrySetRecords(TEXT("items"), Candidate, Errors));
        TestTrue(TEXT("Rejected replacement diagnoses and retains order, identities, revision and event count"), !Errors.IsEmpty()
            && Model->GetRevision() == BeforeRejectRevision && ChangedCalls == BeforeRejectEvents
            && Model->GetRecords().Num() == 2 && Model->GetRecords()[0] == OriginalB && Model->GetRecords()[1] == OriginalA);
    }
    if (!TestTrue(TEXT("Full replacement removes omitted records and updates retained records"), Collections->TrySetRecords(TEXT("items"),
        {CollectionRecord(TEXT("a"), TEXT("Changed"))}, Errors) && Model->GetRecords().Num() == 1
        && Model->FindRecord(TEXT("a")) == OriginalA && !Model->FindRecord(TEXT("b")).IsValid())) { return false; }
    TestTrue(TEXT("Reinserted key has a new identity while retained key keeps its identity"), Collections->TrySetRecords(TEXT("items"),
        {CollectionRecord(TEXT("a"), TEXT("Changed")), CollectionRecord(TEXT("b"))}, Errors)
        && Model->FindRecord(TEXT("a")) == OriginalA && Model->FindRecord(TEXT("b")).IsValid() && Model->FindRecord(TEXT("b")) != OriginalB);

    bool RecursiveRejected = false;
    bool ListenerReadCoherent = false;
    const int32 BeforeRecursiveEvents = ChangedCalls;
    const TWeakObjectPtr<UCk_UI_AuthoredSession_UE> WeakCollections = Collections.Get();
    const FDelegateHandle RecursiveHandle = Model->OnChanged().AddLambda([WeakCollections, &RecursiveRejected, &ListenerReadCoherent]
    {
        auto* Owner = WeakCollections.Get();
        if (Owner == nullptr) { return; }
        TArray<FCk_UI_AuthoredRecord> During;
        TArray<FString> NestedErrors;
        RecursiveRejected = !Owner->TrySetRecords(TEXT("items"), {}, NestedErrors) && !NestedErrors.IsEmpty();
        ListenerReadCoherent = Owner->TryGetRecords(TEXT("items"), During) && During.Num() == 1
            && During[0].Get_Key() == TEXT("a") && During[0].Get_Fields().Num() == 5
            && During[0].Get_Fields()[0].Get_Text().ToString() == TEXT("Published");
    });
    TestTrue(TEXT("Publication listener observes accepted complete data and recursive edit rejects"), Collections->TrySetRecords(TEXT("items"),
        {CollectionRecord(TEXT("a"), TEXT("Published"))}, Errors) && RecursiveRejected && ListenerReadCoherent
        && ChangedCalls == BeforeRecursiveEvents + 1);
    Model->OnChanged().Remove(RecursiveHandle);
    const FDelegateHandle ReleaseHandle = Model->OnChanged().AddLambda([WeakCollections]
    { if (auto* Owner = WeakCollections.Get()) { Owner->Request_Release(); } });
    ON_SCOPE_EXIT { Model->OnChanged().Remove(ReleaseHandle); };
    TestFalse(TEXT("Release during accepted publication causes the outer reflected operation to fail"), Collections->TrySetRecords(TEXT("items"),
        {CollectionRecord(TEXT("a"), TEXT("Final accepted"))}, Errors));
    const auto FinalRecord = Model->FindRecord(TEXT("a"));
    if (!TestTrue(TEXT("Retained native reader may observe accepted publication after terminal release"), FinalRecord.IsValid())) { return false; }
    const auto* FinalTitle = FinalRecord->FindField(TEXT("title"));
    TestTrue(TEXT("Release preserves accepted native data while session access becomes unavailable"), FinalTitle != nullptr
        && FinalTitle->Text.ToString() == TEXT("Final accepted") && !Errors.IsEmpty() && !Collections->Get_IsUsable()
        && !Collections->Get_Collection(TEXT("items")).IsValid());
    const int32 ReleasedEventCount = ChangedCalls;
    TestFalse(TEXT("Released session rejects later replacement without another publication"), Collections->TrySetRecords(TEXT("items"), {}, Errors));
    TestEqual(TEXT("Rejected retired mutation dispatches no downstream collection event"), ChangedCalls, ReleasedEventCount);
    Readback = {FCk_UI_AuthoredRecord{TEXT("sentinel")}}; UntouchedRevision = -37;
    TestFalse(TEXT("Released record getter rejects"), Collections->TryGetRecords(TEXT("items"), Readback));
    TestFalse(TEXT("Released collection revision getter rejects"), Collections->TryGetCollectionRevision(TEXT("items"), UntouchedRevision));
    TestTrue(TEXT("Released getters preserve caller outputs"), Readback.Num() == 1 && Readback[0].Get_Key() == TEXT("sentinel") && UntouchedRevision == -37);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiAuthoredSession_Native,
    "Ck.UiAuthoring.UMG.Session.NativeTransactionsAndEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiAuthoredSession_Native::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_authored_session;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Session native test requires Slate.")); return false; }
    {
        TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Required{NewObject<UCk_UI_AuthoredSession_UE>()};
        TStrongObjectPtr<UCk_UI_AuthoredSurface_UE> RequiredSurface{NewObject<UCk_UI_AuthoredSurface_UE>()};
        TStrongObjectPtr<UCk_UiAuthoredSession_TestListener_UE> RequiredListener{NewObject<UCk_UiAuthoredSession_TestListener_UE>()};
        ON_SCOPE_EXIT { Required->Request_Release(); RequiredSurface->ReleaseView(); };
        Bind(*Required.Get(), *RequiredListener.Get());
        auto RequiredConfig = Config();
        RequiredConfig.Set_Bool({FCk_UI_AuthoredBool{TEXT("enabled"), true}, FCk_UI_AuthoredBool{TEXT("otherEnabled"), true}});
        RequiredConfig.Set_Actions({TEXT("fire"), TEXT("otherAction")});
        RequiredConfig.Set_Regions({TEXT("main"), TEXT("details")});
        RequiredConfig.Set_RequiredButtons({FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("enabled")}});
        const FString RequiredMarkup = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
            "<button id=\"fire\" action=\"fire\" enabled-bind=\"enabled\">Fire</button>"
            "</column></region><region name=\"details\"><text id=\"detail\">Details</text></region></ui>");
        TArray<FString> RequiredErrors;
        if (!TestTrue(TEXT("Required button contract admits a complete configuration and matching source"),
            Required->TryInitialize(RequiredConfig, RequiredErrors)
            && Required->TryReload(RequiredMarkup, TEXT(""), RequiredErrors)
            && Required->TryMount(RequiredSurface.Get(), TEXT("main"), RequiredErrors)))
        { for (const FString& Error : RequiredErrors) { AddError(Error); } return false; }
        RequiredConfig.Set_RequiredButtons({});
        const auto RequiredView = Required->Get_View();
        const auto RequiredRegion = RequiredView->GetRegion(TEXT("main"));
        const auto RequiredRoot = Child(RequiredRegion);
        const auto RequiredButton = RequiredView->GetButton(TEXT("fire"));
        if (!TestTrue(TEXT("Required concrete button is realized"), RequiredButton.IsValid())) { return false; }
        FSlateApplication& RequiredSlate = FSlateApplication::Get();
        FWindowScope RequiredWindow;
        RequiredWindow.Window = SNew(SWindow).ClientSize(FVector2D(300, 160)).CreateTitleBar(false)[RequiredSurface->TakeWidget()];
        RequiredSlate.AddWindow(RequiredWindow.Window.ToSharedRef(), true);
        Tick(RequiredSlate);
        RequiredSlate.SetKeyboardFocus(RequiredButton, EFocusCause::SetDirectly);
        const int64 RequiredRevision = Required->Get_Revision();
        const auto RejectRequired = [this, &Required, &RequiredView, &RequiredRegion, &RequiredRoot, &RequiredButton,
            &RequiredListener, &RequiredSlate, &RequiredErrors, RequiredRevision](const FString& Candidate)
        {
            TestFalse(TEXT("Required button mismatch rejects before native commit"), Required->TryReload(Candidate, TEXT(""), RequiredErrors));
            bool Value = false;
            TestTrue(TEXT("Contract rejection preserves complete view, controls, focus, model and callback counts"),
                !RequiredErrors.IsEmpty() && Required->Get_View() == RequiredView && Required->Get_Revision() == RequiredRevision
                && Child(RequiredRegion) == RequiredRoot && RequiredView->GetButton(TEXT("fire")) == RequiredButton
                && RequiredSlate.GetKeyboardFocusedWidget() == RequiredButton
                && Required->TryGetBool(TEXT("enabled"), Value) && Value
                && RequiredListener->ActionCalls == 0 && RequiredListener->TextCalls == 0
                && RequiredListener->NumberCalls == 0 && RequiredListener->BoolCalls == 0);
        };
        RejectRequired(RequiredMarkup.Replace(TEXT("<button id=\"fire\" action=\"fire\" enabled-bind=\"enabled\">Fire</button>"), TEXT("")));
        RejectRequired(RequiredMarkup.Replace(TEXT("id=\"fire\""), TEXT("id=\"renamed\"")));
        RejectRequired(RequiredMarkup.Replace(TEXT("<button id=\"fire\" action=\"fire\" enabled-bind=\"enabled\">Fire</button>"), TEXT("<text id=\"fire\">Fire</text>")));
        RejectRequired(RequiredMarkup.Replace(TEXT("action=\"fire\""), TEXT("action=\"otherAction\"")));
        RejectRequired(RequiredMarkup.Replace(TEXT("enabled-bind=\"enabled\""), TEXT("enabled-bind=\"otherEnabled\"")));
        RejectRequired(RequiredMarkup.Replace(TEXT(" enabled-bind=\"enabled\""), TEXT("")));
        RejectRequired(RequiredMarkup.Replace(TEXT("<button id=\"fire\" action=\"fire\" enabled-bind=\"enabled\">Fire</button>"),
            TEXT("<repeat id=\"rows\" bind=\"items\"><button id=\"fire\" action=\"fire\" enabled-bind=\"enabled\">Fire</button></repeat>")));
        TestTrue(TEXT("A repeat-template ID cannot satisfy the concrete root requirement"),
            RequiredErrors.ContainsByPredicate([](const FString& Error) { return Error.Contains(TEXT("Required ordinary button")); }));
        RejectRequired(RequiredMarkup.Replace(TEXT("name=\"main\""), TEXT("name=\"swap\""))
            .Replace(TEXT("name=\"details\""), TEXT("name=\"main\""))
            .Replace(TEXT("name=\"swap\""), TEXT("name=\"details\"")));
        FScratch RequiredScratch;
        const FString RequiredPath = RequiredScratch.Path(TEXT("required.ui.html"));
        const FString RequiredCss = RequiredScratch.Path(TEXT("required.ui.css"));
        if (!TestTrue(TEXT("Required source file fixtures save"), FFileHelper::SaveStringToFile(
            RequiredMarkup.Replace(TEXT("action=\"fire\""), TEXT("action=\"otherAction\"")), *RequiredPath)
            && FFileHelper::SaveStringToFile(TEXT(""), *RequiredCss))) { return false; }
        TestFalse(TEXT("File reload shares required-button admission"), Required->TryReloadFiles(RequiredPath, RequiredCss, RequiredErrors));
        TestTrue(TEXT("File contract rejection preserves revision and concrete button"), Required->Get_Revision() == RequiredRevision
            && RequiredView->GetButton(TEXT("fire")) == RequiredButton && RequiredListener->ActionCalls == 0);
        TestTrue(TEXT("Matching requirement can reload after every rejection"), Required->TryReload(RequiredMarkup, TEXT(""), RequiredErrors)
            && Required->Get_Revision() == RequiredRevision + 1);
        const auto AcceptedRequiredButton = RequiredView->GetButton(TEXT("fire"));
        if (!TestTrue(TEXT("Accepted required reload realizes a concrete button"), AcceptedRequiredButton.IsValid())) { return false; }
        AcceptedRequiredButton->SimulateClick();
        TestEqual(TEXT("Accepted required button dispatches its declared action"), RequiredListener->ActionCalls, 1);
        TStrongObjectPtr<UCk_UI_AuthoredSession_UE> OtherRegion{NewObject<UCk_UI_AuthoredSession_UE>()};
        ON_SCOPE_EXIT { OtherRegion->Request_Release(); };
        auto OtherRegionConfig = RequiredConfig;
        auto OtherRegionRequirement = FCk_UI_AuthoredButtonRequirement{TEXT("fire"), TEXT("fire"), TEXT("enabled")};
        OtherRegionRequirement.Set_Region(TEXT("details"));
        OtherRegionConfig.Set_RequiredButtons({OtherRegionRequirement});
        TestTrue(TEXT("Explicit non-main required region is supported"), OtherRegion->TryInitialize(OtherRegionConfig, RequiredErrors)
            && OtherRegion->TryReload(RequiredMarkup.Replace(TEXT("name=\"main\""), TEXT("name=\"swap\""))
                .Replace(TEXT("name=\"details\""), TEXT("name=\"main\""))
                .Replace(TEXT("name=\"swap\""), TEXT("name=\"details\"")), TEXT(""), RequiredErrors));
    }
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Session{NewObject<UCk_UI_AuthoredSession_UE>()};
    TStrongObjectPtr<UCk_UiAuthoredSession_TestListener_UE> Listener{NewObject<UCk_UiAuthoredSession_TestListener_UE>()};
    TStrongObjectPtr<UCk_UI_AuthoredSurface_UE> Surface{NewObject<UCk_UI_AuthoredSurface_UE>()};
    TArray<FString> Errors;
    ON_SCOPE_EXIT { Session->Request_Release(); Surface->ReleaseView(); };
    Bind(*Session.Get(), *Listener.Get());
    if (!TestTrue(TEXT("Default main region loads production scalar controls without native preregistration"),
        Session->TryInitialize(Config(), Errors) && Session->TryReload(Markup(), Styles(), Errors)))
    { for (const FString& Error : Errors) { AddError(Error); } return false; }
    const TSharedPtr<FCkUiView> View = Session->Get_View();
    if (!TestTrue(TEXT("Accepted session exposes its native owner"), View.IsValid())) { return false; }
    TestFalse(TEXT("Null surface is rejected"), Session->TryMount(nullptr, TEXT("main"), Errors));
    TestFalse(TEXT("Uncommitted region is rejected"), Session->TryMount(Surface.Get(), TEXT("missing"), Errors));
    if (!TestTrue(TEXT("Accepted region mounts into the production UMG surface"), Session->TryMount(Surface.Get(), TEXT("main"), Errors))) { return false; }
    const TSharedRef<SWidget> Host = Surface->TakeWidget();
    const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
    if (!TestTrue(TEXT("Surface child is the accepted region"), Child(Host) == Region)) { return false; }
    const auto DefaultText = Find<SCkFlexText>(Region, TEXT("SCkFlexText"));
    if (!TestTrue(TEXT("Session realizes its default text font"), DefaultText.IsValid())) { return false; }
    const auto CoreFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
    const auto TextFont = DefaultText->GetFont();
    TestTrue(TEXT("Session default text uses Core's real composite instead of missing-glyph fallback"),
        CoreFont.GetCompositeFont() != nullptr && TextFont.GetCompositeFont() == CoreFont.GetCompositeFont()
        && TextFont.TypefaceFontName == CoreFont.TypefaceFontName);
    FSlateApplication& Slate = FSlateApplication::Get();
    FWindowScope Window;
    Window.Window = SNew(SWindow).AutoCenter(EAutoCenter::None).ClientSize(FVector2D(440, 300)).CreateTitleBar(false)[Host];
    Slate.AddWindow(Window.Window.ToSharedRef(), true);
    Tick(Slate);
    const TSharedPtr<SButton> Button = View->GetButton(TEXT("fire"));
    const TSharedPtr<SSlider> Slider = Find<SSlider>(Region, TEXT("SCkUiSlider"));
    const TSharedPtr<SCheckBox> Checkbox = Find<SCheckBox>(Region, TEXT("SCheckBox"));
    const TSharedPtr<SSearchBox> Search = Find<SSearchBox>(Region, TEXT("SSearchBox"));
    if (!TestTrue(TEXT("Committed session realizes all native event controls"), Button.IsValid() && Slider.IsValid()
        && Checkbox.IsValid() && Search.IsValid())) { return false; }

    Slate.ClearKeyboardFocus();
    const int32 InitialActionCalls = Listener->ActionCalls;
    const int32 InitialTextCalls = Listener->TextCalls;
    const int32 InitialNumberCalls = Listener->NumberCalls;
    const int32 InitialBoolCalls = Listener->BoolCalls;
    Button->SimulateClick();
    Search->SetText(FText::FromString(TEXT("Proposed")));
    Advance(Slider.ToSharedRef());
    Toggle(Checkbox.ToSharedRef());
    TestEqual(TEXT("Native button dispatches one reflected action"), Listener->ActionCalls - InitialActionCalls, 1);
    TestEqual(TEXT("Native button dispatches its declared action name"), Listener->LastAction, FString(TEXT("fire")));
    TestEqual(TEXT("Native search dispatches one text proposal"), Listener->TextCalls - InitialTextCalls, 1);
    TestEqual(TEXT("Native search proposes its declared scalar"), Listener->LastTextName, FString(TEXT("label")));
    TestEqual(TEXT("Native search proposes the edited text"), Listener->LastText.ToString(), FString(TEXT("Proposed")));
    TestEqual(TEXT("Native slider dispatches one commit proposal"), Listener->NumberCalls - InitialNumberCalls, 1);
    TestEqual(TEXT("Native slider proposes its declared scalar"), Listener->LastNumberName, FString(TEXT("amount")));
    TestTrue(*FString::Printf(TEXT("Native slider proposes one domain step (actual %.9g, expected 30)"), Listener->LastNumber),
        FMath::IsNearlyEqual(Listener->LastNumber, 30.0f, UE_KINDA_SMALL_NUMBER));
    TestEqual(TEXT("Native checkbox dispatches one bool proposal"), Listener->BoolCalls - InitialBoolCalls, 1);
    TestEqual(TEXT("Native checkbox proposes its declared scalar"), Listener->LastBoolName, FString(TEXT("enabled")));
    TestTrue(TEXT("Native checkbox proposes the checked state"), Listener->LastBool);
    FText Text;
    float Number = 0.0f;
    bool Boolean = true;
    TestTrue(TEXT("Dispatch proposes edits without mutating the accepted model"), Session->TryGetText(TEXT("label"), Text)
        && Text.ToString() == TEXT("Initial") && Session->TryGetNumber(TEXT("amount"), Number)
        && Number == 20.0f && Session->TryGetBool(TEXT("enabled"), Boolean) && !Boolean);
    Listener->AcceptEdits = true;
    Advance(Slider.ToSharedRef());
    Toggle(Checkbox.ToSharedRef());
    Search->SetText(FText::FromString(TEXT("Accepted from listener")));
    TestTrue(TEXT("Explicit proposal consumer can read accepted number"), Session->TryGetNumber(TEXT("amount"), Number));
    TestTrue(*FString::Printf(TEXT("Explicit consumer accepts one slider step (actual %.9g, expected 30)"), Number),
        FMath::IsNearlyEqual(Number, 30.0f, UE_KINDA_SMALL_NUMBER));
    TestTrue(TEXT("Explicit proposal consumer can read accepted bool"), Session->TryGetBool(TEXT("enabled"), Boolean));
    TestTrue(TEXT("Explicit consumer accepts the checked state"), Boolean);
    TestTrue(TEXT("Explicit proposal consumer can read accepted text"), Session->TryGetText(TEXT("label"), Text));
    TestEqual(TEXT("Explicit consumer accepts the edited text"), Text.ToString(), FString(TEXT("Accepted from listener")));
    Listener->AcceptEdits = false;

    const int64 Revision = Session->Get_Revision();
    const float AcceptedNumber = Number;
    const int32 AcceptedEventCount = Listener->ActionCalls + Listener->TextCalls + Listener->NumberCalls + Listener->BoolCalls;
    const TSharedPtr<SWidget> AcceptedRoot = Child(Region);
    if (!TestTrue(TEXT("Accepted region has one mounted document root"), AcceptedRoot.IsValid())) { return false; }
    const auto ExpectRejected = [this, &Session, &Listener, &View, &Host, &Region, &AcceptedRoot, Revision, AcceptedNumber, AcceptedEventCount, &Errors](const FString& InMarkup, const FString& InStyles)
    {
        TestFalse(TEXT("Malformed candidate rejects"), Session->TryReload(InMarkup, InStyles, Errors));
        TestTrue(TEXT("Rejected reload diagnoses and preserves revision, view and mounted content"), !Errors.IsEmpty()
            && Session->Get_Revision() == Revision && Session->Get_View() == View && Child(Host) == Region && Child(Region) == AcceptedRoot);
        float UnchangedNumber = 0.0f;
        bool UnchangedBool = false;
        FText UnchangedText;
        TestTrue(TEXT("Rejected reload dispatches no proposal and changes no accepted scalar"),
            Listener->ActionCalls + Listener->TextCalls + Listener->NumberCalls + Listener->BoolCalls == AcceptedEventCount
            && Session->TryGetNumber(TEXT("amount"), UnchangedNumber) && UnchangedNumber == AcceptedNumber
            && Session->TryGetBool(TEXT("enabled"), UnchangedBool) && UnchangedBool
            && Session->TryGetText(TEXT("label"), UnchangedText) && UnchangedText.ToString() == TEXT("Accepted from listener"));
    };
    ExpectRejected(TEXT("<ui>"), Styles());
    ExpectRejected(Markup(), TEXT(".slider { unsupported-property: 1px; }"));
    ExpectRejected(Markup().Replace(TEXT("bind=\"label\""), TEXT("bind=\"missing\"")), Styles());
    FScratch Scratch;
    const FString MarkupPath = Scratch.Path(TEXT("session.ui.html"));
    const FString CssPath = Scratch.Path(TEXT("session.ui.css"));
    const FString MissingPath = Scratch.Path(TEXT("missing.ui.css"));
    if (!TestTrue(TEXT("Session source fixtures are saved"), FFileHelper::SaveStringToFile(Markup(), *MarkupPath)
        && FFileHelper::SaveStringToFile(Styles(), *CssPath))) { return false; }
    TestFalse(TEXT("A missing file rejects the complete source reload"), Session->TryReloadFiles(MarkupPath, MissingPath, Errors));
    TestTrue(TEXT("File failure preserves accepted revision and mounted child"), !Errors.IsEmpty()
        && Session->Get_Revision() == Revision && Child(Region) == AcceptedRoot && Child(Host) == Region);
    TestTrue(TEXT("Valid files use the same production transaction"), Session->TryReloadFiles(MarkupPath, CssPath, Errors));
    TestEqual(TEXT("Accepted file reload advances revision once"), Session->Get_Revision(), Revision + 1);
    Tick(Slate);
    TestTrue(TEXT("Compatible file reload retains native scalar controls"), Find<SSlider>(Region, TEXT("SCkUiSlider")) == Slider
        && Find<SCheckBox>(Region, TEXT("SCheckBox")) == Checkbox && Find<SSearchBox>(Region, TEXT("SSearchBox")) == Search);
    const TSharedPtr<SButton> CurrentButton = View->GetButton(TEXT("fire"));
    if (!TestTrue(TEXT("Accepted reload realizes its current action button"), CurrentButton.IsValid())) { return false; }

    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Other{NewObject<UCk_UI_AuthoredSession_UE>()};
    TStrongObjectPtr<UCk_UI_AuthoredSurface_UE> OtherSurface{NewObject<UCk_UI_AuthoredSurface_UE>()};
    ON_SCOPE_EXIT { Other->Request_Release(); OtherSurface->ReleaseView(); };
    if (!TestTrue(TEXT("A different surface has an independent accepted owner"), Other->TryInitialize(Config(), Errors)
        && Other->TryReload(Markup(), Styles(), Errors) && Other->TryMount(OtherSurface.Get(), TEXT("main"), Errors))) { return false; }
    const TSharedRef<SWidget> OtherHost = OtherSurface->TakeWidget();
    const TSharedPtr<SWidget> OtherMount = Child(OtherHost);
    TestFalse(TEXT("A session cannot move to a second surface"), Session->TryMount(OtherSurface.Get(), TEXT("main"), Errors));
    TestTrue(TEXT("Second-surface rejection preserves both accepted owners and mounts"), Session->Get_IsUsable()
        && Other->Get_IsUsable() && Child(Host) == Region && Child(OtherHost) == OtherMount);

    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Multiple{NewObject<UCk_UI_AuthoredSession_UE>()};
    TStrongObjectPtr<UCk_UI_AuthoredSurface_UE> MultipleSurface{NewObject<UCk_UI_AuthoredSurface_UE>()};
    TStrongObjectPtr<UCk_UiAuthoredSession_TestListener_UE> MultipleListener{NewObject<UCk_UiAuthoredSession_TestListener_UE>()};
    ON_SCOPE_EXIT { Multiple->Request_Release(); MultipleSurface->ReleaseView(); };
    auto MultipleConfig = Config(); MultipleConfig.Set_Regions({TEXT("main"), TEXT("details")});
    const FString MultipleMarkup = Markup().Replace(TEXT("</ui>"),
        TEXT("<region name=\"details\"><text id=\"detail-text\" bind=\"label\"/></region></ui>"));
    Bind(*Multiple.Get(), *MultipleListener.Get());
    if (!TestTrue(TEXT("Explicit region declarations load a complete two-region document"),
        Multiple->TryInitialize(MultipleConfig, Errors) && Multiple->TryReload(MultipleMarkup, Styles(), Errors)
        && Multiple->TryMount(MultipleSurface.Get(), TEXT("main"), Errors))) { return false; }
    const TSharedPtr<FCkUiView> MultipleView = Multiple->Get_View();
    if (!TestTrue(TEXT("Two-region fixture exposes its accepted native view"), MultipleView.IsValid())) { return false; }
    const TSharedRef<SWidget> MultipleHost = MultipleSurface->TakeWidget();
    const TSharedRef<SWidget> MainMount = MultipleView->GetRegion(TEXT("main"));
    const TSharedRef<SWidget> DetailsMount = MultipleView->GetRegion(TEXT("details"));
    const TSharedPtr<SWidget> MainChild = Child(MainMount);
    const TSharedPtr<SWidget> DetailsChild = Child(DetailsMount);
    const int64 MultipleRevision = Multiple->Get_Revision();
    if (!TestTrue(TEXT("Both declared regions publish complete mounted children"), MainChild.IsValid() && DetailsChild.IsValid()
        && MultipleView->HasRegion(TEXT("main")) && MultipleView->HasRegion(TEXT("details")) && Child(MultipleHost) == MainMount)) { return false; }
    const int32 MultipleEventCount = MultipleListener->ActionCalls + MultipleListener->TextCalls
        + MultipleListener->NumberCalls + MultipleListener->BoolCalls;
    const TArray<FString> InvalidRegionDocuments = {
        Markup(),
        MultipleMarkup.Replace(TEXT("name=\"details\""), TEXT("name=\"unknown\"")),
        MultipleMarkup.Replace(TEXT("</ui>"), TEXT("<region name=\"extra\"><text id=\"extra-text\">Extra</text></region></ui>")),
        MultipleMarkup.Replace(TEXT("id=\"detail-text\" bind=\"label\""), TEXT("id=\"detail-text\" bind=\"missing\""))
    };
    for (const FString& Candidate : InvalidRegionDocuments)
    {
        TestFalse(TEXT("Incomplete, additional, renamed or invalid regions reject the complete transaction"),
            Multiple->TryReload(Candidate, Styles(), Errors));
        FText PreservedText;
        float PreservedNumber = 0.0f;
        bool PreservedBool = true;
        TestTrue(TEXT("Region rejection preserves both children, revision, mount, model and dispatch count"), !Errors.IsEmpty()
            && Multiple->Get_Revision() == MultipleRevision && Multiple->Get_View() == MultipleView
            && Child(MainMount) == MainChild && Child(DetailsMount) == DetailsChild && Child(MultipleHost) == MainMount
            && Multiple->TryGetText(TEXT("label"), PreservedText) && PreservedText.ToString() == TEXT("Initial")
            && Multiple->TryGetNumber(TEXT("amount"), PreservedNumber) && PreservedNumber == 20.0f
            && Multiple->TryGetBool(TEXT("enabled"), PreservedBool) && !PreservedBool
            && MultipleListener->ActionCalls + MultipleListener->TextCalls + MultipleListener->NumberCalls
                + MultipleListener->BoolCalls == MultipleEventCount);
    }
    TestTrue(TEXT("One surface can switch between its session's accepted regions"),
        Multiple->TryMount(MultipleSurface.Get(), TEXT("details"), Errors) && Child(MultipleHost) == DetailsMount
        && Multiple->Get_Revision() == MultipleRevision && !MultipleView->IsOwnerReleased());
    TestFalse(TEXT("An undeclared region cannot replace the switched mount"), Multiple->TryMount(MultipleSurface.Get(), TEXT("unknown"), Errors));
    TestFalse(TEXT("Multiple declared regions do not permit mounting on a second surface"), Multiple->TryMount(OtherSurface.Get(), TEXT("main"), Errors));
    TestTrue(TEXT("Rejected region/surface switches preserve both accepted surface owners"),
        Multiple->Get_IsUsable() && Other->Get_IsUsable() && Child(MultipleHost) == DetailsMount && Child(OtherHost) == OtherMount);
    TestTrue(TEXT("The same surface can switch back without retiring either accepted region"),
        Multiple->TryMount(MultipleSurface.Get(), TEXT("main"), Errors) && Child(MultipleHost) == MainMount
        && Child(MainMount) == MainChild && Child(DetailsMount) == DetailsChild);

    Listener->ReleaseOnAction = true;
    const int32 BeforeAction = Listener->ActionCalls;
    const int32 BeforeText = Listener->TextCalls;
    const int32 BeforeNumber = Listener->NumberCalls;
    const int32 BeforeBool = Listener->BoolCalls;
    CurrentButton->SimulateClick();
    TestTrue(TEXT("Release from a reflected action wins and retires the configured owner"), Listener->ActionCalls == BeforeAction + 1
        && !Session->Get_IsUsable() && View->IsOwnerReleased() && !Surface->HasConfiguredView());
    CurrentButton->SimulateClick();
    Button->SimulateClick();
    Search->SetText(FText::FromString(TEXT("Stale proposal")));
    Advance(Slider.ToSharedRef());
    Toggle(Checkbox.ToSharedRef());
    TestTrue(TEXT("Externally held retired controls cannot emit further callbacks"), Listener->ActionCalls == BeforeAction + 1
        && Listener->TextCalls == BeforeText && Listener->NumberCalls == BeforeNumber && Listener->BoolCalls == BeforeBool);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiAuthoredSession_Lifetime,
    "Ck.UiAuthoring.UMG.Session.OwnerRetirementAndGc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkUiAuthoredSession_Lifetime::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_authored_session;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Session lifetime test requires Slate.")); return false; }
    TArray<FString> Errors;
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Session{NewObject<UCk_UI_AuthoredSession_UE>()};
    TStrongObjectPtr<UCk_UI_AuthoredSurface_UE> Surface{NewObject<UCk_UI_AuthoredSurface_UE>()};
    ON_SCOPE_EXIT { Session->Request_Release(); Surface->ReleaseView(); };
    if (!TestTrue(TEXT("First owner initializes, commits and mounts"), Session->TryInitialize(Config(), Errors)
        && Session->TryReload(Markup(), Styles(), Errors) && Session->TryMount(Surface.Get(), TEXT("main"), Errors))) { return false; }
    const TSharedPtr<FCkUiView> OldView = Session->Get_View();
    if (!TestTrue(TEXT("Old native view is available before replacement"), OldView.IsValid())) { return false; }
    const TSharedRef<SWidget> Host = Surface->TakeWidget();
    const TSharedRef<FCkUiView> Replacement = FCkUiView::Create({});
    Replacement->GetRegion(TEXT("main"));
    const FString ReplacementMarkup = TEXT("<ui version=\"1\"><region name=\"main\"><text id=\"replacement\">New owner</text></region></ui>");
    if (!TestTrue(TEXT("Independent replacement commits and takes the existing surface"), Replacement->TryReload(ReplacementMarkup, TEXT("")).Succeeded
        && Surface->TryConfigure(Replacement, TEXT("main")))) { return false; }
    TestTrue(TEXT("Surface replacement permanently retires the session's view"), OldView->IsOwnerReleased()
        && !Session->Get_IsUsable() && !Session->TrySetNumber(TEXT("amount"), 0.0f));
    const TSharedPtr<SWidget> ReplacementMount = Child(Host);
    Session->Request_Release();
    TestTrue(TEXT("Stale session release preserves the surface's newer owner"), Surface->HasConfiguredView()
        && !Replacement->IsOwnerReleased() && Child(Host) == ReplacementMount);

    TStrongObjectPtr<UCk_UiAuthoredSession_TestListener_UE> Listener{NewObject<UCk_UiAuthoredSession_TestListener_UE>()};
    TSharedPtr<FCkUiView> HeldView;
    TSharedPtr<SButton> HeldButton;
    TSharedPtr<SSearchBox> HeldSearch;
    TSharedPtr<SSlider> HeldSlider;
    TSharedPtr<SCheckBox> HeldCheckbox;
    TWeakObjectPtr<UCk_UI_AuthoredSession_UE> WeakSession;
    {
        TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Collected{NewObject<UCk_UI_AuthoredSession_UE>()};
        Bind(*Collected.Get(), *Listener.Get());
        if (!TestTrue(TEXT("GC fixture initializes and commits a native view"), Collected->TryInitialize(Config(), Errors)
            && Collected->TryReload(Markup(), Styles(), Errors))) { return false; }
        HeldView = Collected->Get_View();
        if (!TestTrue(TEXT("GC fixture has a native owner"), HeldView.IsValid())) { return false; }
        const TSharedRef<SWidget> HeldRegion = HeldView->GetRegion(TEXT("main"));
        HeldButton = HeldView->GetButton(TEXT("fire"));
        HeldSearch = Find<SSearchBox>(HeldRegion, TEXT("SSearchBox"));
        HeldSlider = Find<SSlider>(HeldRegion, TEXT("SCkUiSlider"));
        HeldCheckbox = Find<SCheckBox>(HeldRegion, TEXT("SCheckBox"));
        if (!TestTrue(TEXT("GC fixture retains actual native controls"), HeldButton.IsValid() && HeldSearch.IsValid()
            && HeldSlider.IsValid() && HeldCheckbox.IsValid())) { return false; }
        HeldButton->SimulateClick();
        TestEqual(TEXT("GC fixture dispatches before dropping reflected ownership"), Listener->ActionCalls, 1);
        WeakSession = Collected.Get();
    }
    FlushRenderingCommands();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("Held native view and widgets do not retain their session UObject"), WeakSession.IsValid());
    TestTrue(TEXT("UObject destruction retires the externally held native owner"), HeldView->IsOwnerReleased());
    HeldButton->SimulateClick();
    HeldSearch->SetText(FText::FromString(TEXT("After GC")));
    Advance(HeldSlider.ToSharedRef());
    Toggle(HeldCheckbox.ToSharedRef());
    TestTrue(TEXT("Native weak adapters remain inert after session GC"), Listener->ActionCalls == 1
        && Listener->TextCalls == 0 && Listener->NumberCalls == 0 && Listener->BoolCalls == 0);
    return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS

#include "CkSlateLayout/CkUiDocument.h"
#include "CkSlateLayout/CkUiTreeCollection.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/SCkUiTree.h"

#include "Framework/Application/SlateApplication.h"
#include "Misc/AutomationTest.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/ITableRow.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_tree_visual_style
{
    struct FWindowScope final { explicit FWindowScope(FSlateApplication& InSlate) : Slate(InSlate) {} ~FWindowScope() { if (Window.IsValid()) { Slate.DestroyWindowImmediately(Window.ToSharedRef()); } } FSlateApplication& Slate; TSharedPtr<SWindow> Window; };
    auto Tick(FSlateApplication& InSlate) -> void { InSlate.PumpMessages(); InSlate.Tick(); InSlate.Tick(); }
    auto Schema() -> TArray<FCkUiFieldSchema> { return {{TEXT("name"), ECkUiFieldKind::Text}}; }
    auto Node(const FString& InKey, TOptional<FString> InParent = {}) -> FCkUiTreeNodeData
    { FCkUiTreeNodeData Result; Result.Key = InKey; Result.ParentKey = MoveTemp(InParent); Result.Fields.Add(TEXT("name"), FCkUiFieldValue{.Kind=ECkUiFieldKind::Text,.Text=FText::FromString(InKey)}); return Result; }
    auto Markup() -> FString { return TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\" class=\"root\"><tree id=\"tree\" class=\"fill\" bind=\"tree\" selection-action=\"selected\"><row id=\"cell\"><text id=\"name\" bind-field=\"name\"/></row></tree></column></region></ui>"); }
    auto Styles() -> FString { return TEXT(".root { gap: 0px; } .fill { flex-grow: 1; -ck-tree-background: #704020; -ck-tree-row-background: #101820; -ck-tree-row-hover-background: #182838; -ck-tree-row-selected-background: #203850; -ck-tree-row-selected-accent-color: #20d8f0; -ck-tree-row-selected-accent-width: 3px; }"); }
    auto WrongNodeStyles(const FString& InProperty) -> FString
    {
        const FString RootRule = FString::Printf(TEXT(".root { gap: 0px; %s: %s; }"), *InProperty, InProperty.EndsWith(TEXT("width")) ? TEXT("1px") : TEXT("#ffffff"));
        return Styles().Replace(TEXT(".root { gap: 0px; }"), *RootRule);
    }
    auto SampleViewport(FSlateApplication& InSlate, const TSharedRef<SWidget>& InNative, FColor& OutColor) -> bool
    {
        Tick(InSlate);
        TArray<FColor> Pixels;
        FIntVector Size;
        if (!InSlate.TakeScreenshot(InNative, Pixels, Size) || Size.X < 100 || Size.Y < 120 || Pixels.Num() < Size.X * Size.Y) { return false; }
        OutColor = Pixels[(Size.Y - 12) * Size.X + Size.X / 3];
        return true;
    }
    auto PixelDistance(const FColor& A, const FColor& B) -> int32
    { return FMath::Abs(int32(A.R) - int32(B.R)) + FMath::Abs(int32(A.G) - int32(B.G)) + FMath::Abs(int32(A.B) - int32(B.B)); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiTreeVisualStyleTest, "Ck.UiAuthoring.Tree.VisualStyle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
auto FCkUiTreeVisualStyleTest::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_tree_visual_style;
    FCkUiDocument Document;
    const FCkUiLoadResult Parsed = FCkUiDocumentParser::TryParse(Markup(), Styles(), {}, Document, TEXT("UiTreeVisualStyleParser"));
    if (!TestTrue(TEXT("Tree visual properties parse"), Parsed.Succeeded)) { return false; }
    const FCkUiNode* Root = Document.Regions.Find(TEXT("main"));
    const FCkUiNode* TreeNode = Root != nullptr && Root->Children.Num() == 1 ? &Root->Children[0] : nullptr;
    if (!TestTrue(TEXT("Tree node retains visual style"), TreeNode != nullptr && TreeNode->TreeVisualStyle.Enabled)) { return false; }
    TestTrue(TEXT("Tree viewport and state colors retain authored values"), TreeNode->TreeVisualStyle.Background.IsSet() && TreeNode->TreeVisualStyle.RowBackground.IsSet() && TreeNode->TreeVisualStyle.RowHoverBackground.IsSet() && TreeNode->TreeVisualStyle.RowSelectedBackground.IsSet() && TreeNode->TreeVisualStyle.SelectedAccentColor.IsSet());
    TestTrue(TEXT("Tree accent width retains authored value"), TreeNode->TreeVisualStyle.SelectedAccentWidth.IsSet() && FMath::IsNearlyEqual(TreeNode->TreeVisualStyle.SelectedAccentWidth.GetValue(), 3.0f));
    for (const FString& Property : {TEXT("-ck-tree-background"), TEXT("-ck-tree-row-background"), TEXT("-ck-tree-row-hover-background"), TEXT("-ck-tree-row-selected-background"), TEXT("-ck-tree-row-selected-accent-color"), TEXT("-ck-tree-row-selected-accent-width")})
    { auto WrongDocument = FCkUiDocument{}; TestFalse(TEXT("Tree visual property rejects on non-tree node"), FCkUiDocumentParser::TryParse(Markup(), WrongNodeStyles(Property), {}, WrongDocument, TEXT("UiTreeVisualStyleWrongNode")).Succeeded); }
    auto InvalidColorDocument = FCkUiDocument{};
    TestFalse(TEXT("Invalid tree visual color rejects"), FCkUiDocumentParser::TryParse(Markup(), Styles().Replace(TEXT("#20d8f0"), TEXT("not-a-color")), {}, InvalidColorDocument, TEXT("UiTreeVisualStyleInvalidColor")).Succeeded);
    auto InvalidViewportDocument = FCkUiDocument{};
    TestFalse(TEXT("Invalid tree viewport color rejects"), FCkUiDocumentParser::TryParse(Markup(), Styles().Replace(TEXT("#704020"), TEXT("not-a-color")), {}, InvalidViewportDocument, TEXT("UiTreeVisualStyleInvalidBackground")).Succeeded);

    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Tree visual style runtime test requires Slate.")); return false; }
    TSharedPtr<FCkUiTreeCollection> Model;
    if (!TestTrue(TEXT("Tree model creates"), FCkUiTreeCollection::TryCreate(Schema(), Model).Succeeded) || !Model.IsValid()) { return false; }
    if (!TestTrue(TEXT("Tree hierarchy publishes"), Model->TrySetNodes({Node(TEXT("root")), Node(TEXT("child"), FString(TEXT("root")))}).Succeeded)) { return false; }
    int32 SelectionEvents = 0;
    auto Data = FCkUiView::FDataBindings{};
    Data.Trees.Add(TEXT("tree"), Model);
    Data.TreeSelectionChanged.Add(TEXT("selected"), FOnCkUiTreeSelectionChanged::CreateLambda([&SelectionEvents](TOptional<FString>, ESelectInfo::Type) { ++SelectionEvents; }));
    TSharedPtr<FCkUiView> View = FCkUiView::Create({}, {}, {}, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12), MoveTemp(Data));
    TSharedPtr<SWidget> Region = View->GetRegion(TEXT("main"));
    FSlateApplication& Slate = FSlateApplication::Get(); FWindowScope Scope{Slate};
    Scope.Window = SNew(SWindow).AutoCenter(EAutoCenter::None).ClientSize(FVector2D{480, 280}).CreateTitleBar(false).HasCloseButton(false)[Region.ToSharedRef()]; Slate.AddWindow(Scope.Window.ToSharedRef(), true);
    const FCkUiLoadResult Loaded = View->TryReload(Markup(), Styles(), TEXT("UiTreeVisualStyleRuntime"));
    if (!TestTrue(TEXT("Styled production tree mounts"), Loaded.Succeeded)) { return false; }
    Tick(Slate);
    const TSharedPtr<SCkUiTree> Tree = View->GetTree(TEXT("tree"));
    const TSharedPtr<STreeView<SCkUiTree::FNode>> Native = Tree.IsValid() ? Tree->GetTree() : nullptr;
    const SCkUiTree::FNode RootNode = Model->FindNode(TEXT("root"));
    if (!TestTrue(TEXT("Mounted styled tree exposes native selection surface"), Tree.IsValid() && Native.IsValid() && RootNode.IsValid())) { return false; }
    Native->RequestScrollIntoView(RootNode); Tick(Slate);
    const TSharedPtr<ITableRow> RowBeforeReload = Native->WidgetFromItem(RootNode);
    TestTrue(TEXT("Mounted styled tree realizes its native row"), RowBeforeReload.IsValid());
    Native->SetItemExpansion(RootNode, true);
    Native->SetSelection(RootNode, ESelectInfo::OnMouseClick); Tick(Slate);
    TestTrue(TEXT("Native selection drives tree callback and selected state"), Tree->GetSelectedKey().IsSet() && Tree->GetSelectedKey().GetValue() == TEXT("root") && SelectionEvents == 1);
    FColor InitialPixel;
    if (!TestTrue(TEXT("Native tree viewport captures below realized rows"), SampleViewport(Slate, Native.ToSharedRef(), InitialPixel))) { return false; }
    const int64 Revision = View->GetRevision();
    const FString ReloadStyles = Styles().Replace(TEXT("#704020"), TEXT("#204070")).Replace(TEXT("#101820"), TEXT("#111a24"));
    const FCkUiLoadResult Reloaded = View->TryReload(Markup(), ReloadStyles, TEXT("UiTreeVisualStyleReload"));
    TestTrue(TEXT("Compatible styled reload succeeds"), Reloaded.Succeeded);
    Tick(Slate);
    TestTrue(TEXT("Compatible styled reload preserves tree selection and expansion"), View->GetRevision() == Revision + 1 && View->GetTree(TEXT("tree")) == Tree && Tree->GetTree() == Native && Tree->GetSelectedKey().IsSet() && Tree->GetSelectedKey().GetValue() == TEXT("root") && Tree->GetExpandedKeys().Contains(TEXT("root")));
    TestTrue(TEXT("Compatible styled reload preserves realized row identity"), Native->WidgetFromItem(RootNode) == RowBeforeReload);
    FColor ReloadPixel;
    if (!TestTrue(TEXT("Retained native tree viewport captures after style reload"), SampleViewport(Slate, Native.ToSharedRef(), ReloadPixel))) { return false; }
    TestTrue(TEXT("Retained native viewport paints updated background"), PixelDistance(InitialPixel, ReloadPixel) > 20);
    const int64 AcceptedRevision = View->GetRevision();
    const FCkUiLoadResult NegativeWidth = View->TryReload(Markup(), ReloadStyles.Replace(TEXT("3px"), TEXT("-1px")), TEXT("UiTreeVisualStyleNegativeWidth"));
    TestFalse(TEXT("Negative tree accent width rejects atomically"), NegativeWidth.Succeeded);
    TestTrue(TEXT("Rejected width retains mounted tree identity and selection"), View->GetRevision() == AcceptedRevision && View->GetTree(TEXT("tree")) == Tree && Tree->GetSelectedKey().IsSet() && Tree->GetSelectedKey().GetValue() == TEXT("root"));
    const FCkUiLoadResult InvalidBackground = View->TryReload(Markup(), ReloadStyles.Replace(TEXT("#204070"), TEXT("invalid")), TEXT("UiTreeVisualStyleInvalidBackgroundReload"));
    TestFalse(TEXT("Malformed tree viewport background rejects atomically"), InvalidBackground.Succeeded);
    const FCkUiLoadResult WrongNode = View->TryReload(Markup(), WrongNodeStyles(TEXT("-ck-tree-background")), TEXT("UiTreeVisualStyleWrongNodeReload"));
    TestFalse(TEXT("Tree viewport background on a column rejects atomically"), WrongNode.Succeeded);
    FColor RejectedPixel;
    if (!TestTrue(TEXT("Native viewport captures after rejected reloads"), SampleViewport(Slate, Native.ToSharedRef(), RejectedPixel))) { return false; }
    TestTrue(TEXT("Rejected styles preserve revision, native identity, selection, and painted background"), View->GetRevision() == AcceptedRevision && View->GetTree(TEXT("tree")) == Tree && Tree->GetTree() == Native && Tree->GetSelectedKey().IsSet() && Tree->GetSelectedKey().GetValue() == TEXT("root") && PixelDistance(RejectedPixel, ReloadPixel) <= 4);
    const FString DefaultStyles = ReloadStyles.Replace(TEXT("-ck-tree-background: #204070; "), TEXT(""));
    const FCkUiLoadResult DefaultReload = View->TryReload(Markup(), DefaultStyles, TEXT("UiTreeVisualStyleDefaultBackground"));
    TestTrue(TEXT("Omitted tree viewport background restores stock style"), DefaultReload.Succeeded);
    FColor DefaultPixel;
    if (!TestTrue(TEXT("Native viewport captures after default restoration"), SampleViewport(Slate, Native.ToSharedRef(), DefaultPixel))) { return false; }
    TestTrue(TEXT("Default restoration retains native tree and selection and changes viewport paint"), View->GetTree(TEXT("tree")) == Tree && Tree->GetTree() == Native && Tree->GetSelectedKey().IsSet() && Tree->GetSelectedKey().GetValue() == TEXT("root") && PixelDistance(DefaultPixel, ReloadPixel) > 20);
    return true;
}

#endif

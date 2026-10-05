#include "CkWidgets/Authoring/CkUiFontResources.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "CkSlateLayout/CkFlexText.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/CkUiFontRegistry.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkSlateLayout/SCkUiTable.h"
#include "CkSlateLayout/SCkUiTabs.h"

#include "Async/Async.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontCache.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Layout/Children.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "RenderingThread.h"
#include "Styling/CoreStyle.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_tests_ui_font_registry
{
    auto FontFile(const TCHAR* InName) -> FString
    {
        return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), InName));
    }

    auto Family(const FString& InName, const bool bWithBold = true, const bool bWithFallback = true) -> FCkUiFontFamilyRegistration
    {
        FCkUiFontFamilyRegistration Result;
        Result.Name = InName;
        Result.RegularFile = FontFile(TEXT("Roboto-Regular.ttf"));
        if (bWithBold) { Result.BoldFile = FontFile(TEXT("Roboto-Bold.ttf")); }
        if (bWithFallback)
        {
            Result.FallbackRegularFile = FontFile(TEXT("DroidSansFallback.ttf"));
            if (bWithBold) { Result.FallbackBoldFile = FontFile(TEXT("DroidSansFallback.ttf")); }
        }
        return Result;
    }

    struct FScratch final
    {
        FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/UiFonts"), FGuid::NewGuid().ToString());
        TArray<FString> Files;

        auto Path(const TCHAR* InName) -> FString
        {
            IFileManager::Get().MakeDirectory(*Directory, true);
            const FString Result = FPaths::Combine(Directory, InName);
            Files.Add(Result);
            return Result;
        }

        ~FScratch()
        {
            for (const FString& File : Files) { IFileManager::Get().Delete(*File); }
            IFileManager::Get().DeleteDirectory(*Directory, false, false);
        }
    };

    auto Markup(const FString& InBody) -> FString
    {
        return TEXT("<ui version=\"1\"><menu id=\"commands\"><menu-item key=\"noop\" label=\"No operation\" action=\"noop\"/></menu><region name=\"main\"><column id=\"root\">") + InBody + TEXT("</column></region></ui>");
    }

    auto FindFlex(const TSharedRef<SWidget>& InRoot, const FString& InText) -> TSharedPtr<SCkFlexText>
    {
        if (InRoot->GetTypeAsString() == TEXT("SCkFlexText"))
        {
            const TSharedRef<SCkFlexText> Text = StaticCastSharedRef<SCkFlexText>(InRoot);
            if (Text->GetText().ToString() == InText) { return Text; }
        }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<SCkFlexText> Found = FindFlex(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InText); Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto FindLabel(const TSharedRef<SWidget>& InRoot, const FString& InText) -> TSharedPtr<STextBlock>
    {
        if (InRoot->GetTypeAsString() == TEXT("STextBlock"))
        {
            const TSharedRef<STextBlock> Text = StaticCastSharedRef<STextBlock>(InRoot);
            if (Text->GetText().ToString() == InText) { return Text; }
        }
        const FChildren* Children = InRoot->GetChildren();
        for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
        {
            if (const TSharedPtr<STextBlock> Found = FindLabel(ConstCastSharedRef<SWidget>(Children->GetChildAt(Index)), InText); Found.IsValid()) { return Found; }
        }
        return {};
    }

    auto HasFont(const FSlateFontInfo& InFont, const FCompositeFont* InComposite, const FName InFace,
        const float InSize, const int32 InTracking) -> bool
    {
        return InFont.GetCompositeFont() == InComposite && InFont.TypefaceFontName == InFace
            && InFont.Size == InSize && InFont.LetterSpacing == InTracking;
    }

    auto Tick(FSlateApplication& InSlate) -> void { InSlate.PumpMessages(); InSlate.Tick(); InSlate.Tick(); }

    struct FWindowScope final
    {
        TSharedPtr<SWindow> Window;
        ~FWindowScope() { Close(); }
        auto Close() -> void
        {
            if (Window.IsValid()) { FSlateApplication::Get().DestroyWindowImmediately(Window.ToSharedRef()); Window.Reset(); }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiFonts_RegistryValidation,
    "Ck.UiAuthoring.Fonts.RegistryValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkUiFonts_RegistryValidation::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_font_registry;
    FCkUiFontRegistry Registry;
    for (const FString& Invalid : {FString{}, FString{TEXT("Sans")}, FString{TEXT("9font")}, FString{TEXT("font_name")},
        FString{TEXT("font name")}, FString{TEXT("font.ttf")}, FString{TEXT("sans-serif")}, FString{TEXT("monospace")}})
    {
        TestFalse(TEXT("Invalid/reserved name is outside the shared grammar"), FCkUiFontRegistry::IsFamilyName(Invalid));
        const FCkUiLoadResult Rejected = FCkUiFontResources::Register(Registry, Family(Invalid));
        TestFalse(TEXT("Invalid/reserved family rejects"), Rejected.Succeeded);
        TestTrue(TEXT("Rejection diagnoses and publishes no family"), !Rejected.Errors.IsEmpty() && !Registry.CreateSnapshot()->HasFamily(Invalid));
    }
    TestTrue(TEXT("Lowercase CSS identifier accepted"), FCkUiFontRegistry::IsFamilyName(TEXT("journal-font2")));
    TestFalse(TEXT("Core admission rejects null composites"), Registry.Register(TEXT("null-composite"), {}).Succeeded);
    const TSharedRef<FCompositeFont> EmptyComposite = MakeShared<FCompositeFont>();
    TestFalse(TEXT("Core admission rejects absent primary faces"), Registry.Register(TEXT("empty-composite"), EmptyComposite).Succeeded);
    const TSharedRef<FCompositeFont> PathOnly = MakeShared<FCompositeFont>();
    PathOnly->DefaultTypeface.Fonts.Emplace(TEXT("Regular"), FontFile(TEXT("Roboto-Regular.ttf")), EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
    TestFalse(TEXT("Core admission rejects borrowed file-path fonts"), Registry.Register(TEXT("path-only"), PathOnly).Succeeded);
    TestTrue(TEXT("Malformed core composites publish no families"), !Registry.CreateSnapshot()->HasFamily(TEXT("null-composite"))
        && !Registry.CreateSnapshot()->HasFamily(TEXT("empty-composite")) && !Registry.CreateSnapshot()->HasFamily(TEXT("path-only")));

    // This prerequisite must reject before touching Slate or constructing any face assets.
    const FCkUiLoadResult OffThread = Async(EAsyncExecution::Thread, []
    {
        FCkUiFontRegistry Local;
        return FCkUiFontResources::Register(Local, Family(TEXT("off-thread")));
    }).Get();
    TestFalse(TEXT("Registration off the game thread rejects"), OffThread.Succeeded);
    TestTrue(TEXT("Prerequisite rejection is explicit"), OffThread.Errors.Num() == 1 && OffThread.Errors[0].Contains(TEXT("game thread")));

    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Registry validation requires initialized Slate font services.")); return false; }
    for (const TCHAR* File : {TEXT("Roboto-Regular.ttf"), TEXT("Roboto-Bold.ttf"), TEXT("DroidSansFallback.ttf")})
    {
        if (!TestTrue(TEXT("Engine font fixture exists"), FPaths::FileExists(FontFile(File)))) { return false; }
    }
    if (!TestTrue(TEXT("Complete family registers"), FCkUiFontResources::Register(Registry, Family(TEXT("journal"))).Succeeded)) { return false; }
    const TSharedRef<const FCkUiFontRegistrySnapshot> Before = Registry.CreateSnapshot();
    FSlateFontInfo Regular;
    if (!TestTrue(TEXT("Regular face resolves"), Before->TryResolve(TEXT("journal"), TEXT("Regular"), Regular))) { return false; }
    FSlateFontInfo Bold;
    TestTrue(TEXT("Bold face resolves"), Before->TryResolve(TEXT("journal"), TEXT("Bold"), Bold));
    TestTrue(TEXT("Both faces share the admitted composite"), Regular.GetCompositeFont() != nullptr && Regular.GetCompositeFont() == Bold.GetCompositeFont());
    TestEqual(TEXT("Resolution starts at default size"), Regular.Size, 10.0f);
    int32 ScaleCase = 0;
    for (const float Scale : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        const FString Name = FString::Printf(TEXT("invalid-scale-%d"), ScaleCase++);
        const auto Invalid = MakeShared<FCompositeFont>(*Regular.GetCompositeFont());
        Invalid->FallbackTypeface.ScalingFactor = Scale;
        const auto Rejected = Registry.Register(Name, Invalid);
        TestFalse(TEXT("Invalid fallback scale rejects before publication"), Rejected.Succeeded);
        TestTrue(TEXT("Rejected fallback scale diagnoses without a partial family"), !Rejected.Errors.IsEmpty() && !Registry.CreateSnapshot()->HasFamily(Name));
    }
    TestFalse(TEXT("Duplicate family rejects"), FCkUiFontResources::Register(Registry, Family(TEXT("journal"), false, false)).Succeeded);
    FSlateFontInfo AfterDuplicate;
    Registry.CreateSnapshot()->TryResolve(TEXT("journal"), TEXT("Bold"), AfterDuplicate);
    TestTrue(TEXT("Duplicate rejection retains original complete family"), AfterDuplicate == Bold);
    TestTrue(TEXT("Later family registers"), FCkUiFontResources::Register(Registry, Family(TEXT("later"), false, false)).Succeeded);
    TestFalse(TEXT("Earlier snapshot never gains later registration"), Before->HasFamily(TEXT("later")));
    TestTrue(TEXT("New snapshot sees later registration"), Registry.CreateSnapshot()->HasFamily(TEXT("later")));
    for (const TPair<FString, FName>& Request : TArray<TPair<FString, FName>>{
        {TEXT("missing"), TEXT("Regular")}, {TEXT("journal"), TEXT("Italic")}, {TEXT("later"), TEXT("Bold")}})
    {
        FSlateFontInfo Preserved = Bold;
        TestFalse(TEXT("Absent family or face cannot resolve"), Registry.CreateSnapshot()->TryResolve(Request.Key, Request.Value, Preserved));
        TestTrue(TEXT("Failed resolution preserves every output font field"), Preserved == Bold);
    }

    FScratch Scratch;
    const FString Missing = Scratch.Path(TEXT("missing.ttf"));
    const FString Empty = Scratch.Path(TEXT("empty.ttf"));
    const FString Text = Scratch.Path(TEXT("text.ttf"));
    const FString Truncated = Scratch.Path(TEXT("truncated.ttf"));
    if (!TestTrue(TEXT("Empty fixture saved"), FFileHelper::SaveArrayToFile(TArray<uint8>{}, *Empty))
        || !TestTrue(TEXT("Text fixture saved"), FFileHelper::SaveStringToFile(TEXT("This is not a font."), *Text))) { return false; }
    TArray<uint8> Header;
    if (!TestTrue(TEXT("Read real font before truncating its header"), FFileHelper::LoadFileToArray(Header, *FontFile(TEXT("Roboto-Regular.ttf"))))) { return false; }
    Header.SetNum(12);
    if (!TestTrue(TEXT("Truncated fixture saved"), FFileHelper::SaveArrayToFile(Header, *Truncated))) { return false; }
    int32 CaseIndex = 0;
    for (const FString& InvalidFile : {FString{}, Missing, Empty, Text, Truncated})
    {
        FCkUiFontFamilyRegistration Invalid = Family(FString::Printf(TEXT("invalid-%d"), CaseIndex++));
        Invalid.RegularFile = InvalidFile;
        const FCkUiLoadResult Rejected = FCkUiFontResources::Register(Registry, Invalid);
        TestFalse(TEXT("Unreadable, empty, text, or truncated primary rejects"), Rejected.Succeeded);
        TestTrue(TEXT("Invalid primary leaves no partial admission"), !Rejected.Errors.IsEmpty() && !Registry.CreateSnapshot()->HasFamily(Invalid.Name));
    }
    for (int32 InvalidSlot = 0; InvalidSlot < 3; ++InvalidSlot)
    {
        FCkUiFontFamilyRegistration Invalid = Family(FString::Printf(TEXT("invalid-sibling-%d"), InvalidSlot));
        if (InvalidSlot == 0) { Invalid.BoldFile = Text; }
        if (InvalidSlot == 1) { Invalid.FallbackRegularFile = Truncated; }
        if (InvalidSlot == 2) { Invalid.FallbackBoldFile = Missing; }
        TestFalse(TEXT("One malformed face among valid siblings rejects whole family"), FCkUiFontResources::Register(Registry, Invalid).Succeeded);
        TestFalse(TEXT("No good siblings published after failed family"), Registry.CreateSnapshot()->HasFamily(Invalid.Name));
        TestTrue(TEXT("Same name can register after failure"), FCkUiFontResources::Register(Registry, Family(Invalid.Name)).Succeeded);
    }
    for (const bool bMissingPrimaryBold : {true, false})
    {
        FCkUiFontFamilyRegistration Invalid = Family(bMissingPrimaryBold ? TEXT("no-bold") : TEXT("no-fallback"));
        if (bMissingPrimaryBold) { Invalid.BoldFile.Reset(); } else { Invalid.FallbackRegularFile.Reset(); }
        TestFalse(TEXT("Bold fallback prerequisites are mandatory"), FCkUiFontResources::Register(Registry, Invalid).Succeeded);
        TestFalse(TEXT("Invalid fallback prerequisites publish nothing"), Registry.CreateSnapshot()->HasFamily(Invalid.Name));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiFonts_NativeRuntime,
    "Ck.UiAuthoring.Fonts.NativeRuntime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

auto FCkUiFonts_NativeRuntime::RunTest(const FString&) -> bool
{
    using namespace ck_tests_ui_font_registry;
    if (!FSlateApplication::IsInitialized() || !IsInGameThread()) { AddError(TEXT("Native font test requires Slate on the game thread.")); return false; }
    {
        FCkUiView::FActions DefaultActions;
        DefaultActions.Add(TEXT("noop"), FSimpleDelegate::CreateLambda([] {}));
        const FSlateFontInfo Core = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FSlateFontInfo{}.Size);
        FSlateFontInfo Decorated;
        Decorated.Size = 19;
        Decorated.LetterSpacing = 135;
        Decorated.OutlineSettings = FFontOutlineSettings(2, FLinearColor::Red);
        Decorated.OutlineSettings.bMiteredCorners = true;
        Decorated.FontMaterial = UMaterial::GetDefaultMaterial(MD_UI);
        if (!TestTrue(TEXT("Preservation fixture has a real UI material"), IsValid(Decorated.FontMaterial))) { return false; }
        FSlateFontInfo ExpectedDecorated = Decorated;
        ExpectedDecorated.CompositeFont = Core.CompositeFont;
        ExpectedDecorated.TypefaceFontName = TEXT("Regular");
        const TSharedRef<const FCompositeFont> ExplicitComposite = MakeShared<FCompositeFont>(*Core.GetCompositeFont());
        FSlateFontInfo Explicit(ExplicitComposite, 21, TEXT("Regular"), FFontOutlineSettings(1, FLinearColor::Blue));
        Explicit.LetterSpacing = 90;
        const TStrongObjectPtr<UFont> FontObject(NewObject<UFont>());
        FontObject->FontCacheType = EFontCacheType::Runtime;
        FontObject->GetMutableInternalCompositeFont() = *Core.GetCompositeFont();
        const FSlateFontInfo ObjectFont(FontObject.Get(), 23, TEXT("Regular"));
        FSlateFontInfo NotANumber;
        NotANumber.Size = std::numeric_limits<float>::quiet_NaN();
        FSlateFontInfo Infinite;
        Infinite.Size = std::numeric_limits<float>::infinity();
        struct FDefaultCase
        {
            FString Name;
            TSharedRef<FCkUiView> View;
            FSlateFontInfo Expected;
            bool Capture;
        };
        const TArray<FDefaultCase> Cases = {
            {TEXT("omitted"), FCkUiView::Create({}, DefaultActions), Core, true},
            {TEXT("explicit-empty"), FCkUiView::Create({}, DefaultActions, {}, {}), Core, true},
            {TEXT("positive-decorated"), FCkUiView::Create({}, DefaultActions, {}, Decorated), ExpectedDecorated, false},
            {TEXT("explicit-composite"), FCkUiView::Create({}, DefaultActions, {}, Explicit), Explicit, false},
            {TEXT("font-object"), FCkUiView::Create({}, DefaultActions, {}, ObjectFont), ObjectFont, false},
            {TEXT("nan-size"), FCkUiView::Create({}, DefaultActions, {}, NotANumber), FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10), false},
            {TEXT("infinite-size"), FCkUiView::Create({}, DefaultActions, {}, Infinite), FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10), false}
        };
        FSlateApplication& DefaultSlate = FSlateApplication::Get();
        FWindowScope DefaultWindow;
        DefaultWindow.Window = SNew(SWindow).ClientSize(FVector2D(640, 300));
        DefaultSlate.AddWindow(DefaultWindow.Window.ToSharedRef(), true);
        const FString DefaultMarkup = Markup(TEXT("<text id=\"default-regular\" class=\"line\">Default Regular</text>")
            TEXT("<text id=\"default-bold\" class=\"line bold\">Default Bold</text>")
            TEXT("<button id=\"default-button\" class=\"line bold\" action=\"noop\">Default Button</button>"));
        const FString DefaultStyle = TEXT(".line { height: 80px; text-wrap: nowrap; } .bold { font-weight: bold; }");
        for (const FDefaultCase& Case : Cases)
        {
            const TSharedRef<SWidget> Mount = Case.View->GetRegion(TEXT("main"));
            const FCkUiLoadResult Loaded = Case.View->TryReload(DefaultMarkup, DefaultStyle);
            if (!TestTrue(*FString::Printf(TEXT("%s default-font document loads"), *Case.Name), Loaded.Succeeded))
            { for (const FString& Error : Loaded.Errors) { AddError(Error); } continue; }
            if (Case.Capture) { DefaultWindow.Window->SetContent(Mount); Tick(DefaultSlate); }
            for (const TCHAR* Label : {TEXT("Default Regular"), TEXT("Default Bold"), TEXT("Default Button")})
            {
                const TSharedPtr<SCkFlexText> Text = FindFlex(Mount, Label);
                if (!TestTrue(*FString::Printf(TEXT("%s %s native text exists"), *Case.Name, Label), Text.IsValid())) { continue; }
                const FSlateFontInfo Font = Text->GetFont();
                // Malformed-size cases stay detached and never reach font measurement or rendering.
                const bool SafeSize = FMath::IsFinite(Font.Size) && Font.Size > 0;
                TestTrue(*FString::Printf(TEXT("%s %s has a finite positive size before measurement"), *Case.Name, Label), SafeSize);
                const FName Face = FString(Label) == TEXT("Default Regular") ? FName(TEXT("Regular")) : FName(TEXT("Bold"));
                TestTrue(*FString::Printf(TEXT("%s %s retains exact expected composite, face, positive size and tracking"), *Case.Name, Label),
                    HasFont(Font, Case.Expected.GetCompositeFont(), Face, Case.Expected.Size, Case.Expected.LetterSpacing));
                TestTrue(*FString::Printf(TEXT("%s %s preserves outline and material"), *Case.Name, Label),
                    Font.OutlineSettings.IsIdenticalTo(Case.Expected.OutlineSettings) && Font.FontMaterial == Case.Expected.FontMaterial
                    && Font.FontObject == Case.Expected.FontObject);
                if (Case.Capture && SafeSize)
                {
                    const FGeometry Geometry = Text->GetCachedGeometry();
                    const FVector2D Needed = Text->Measure(YGUndefined, YGMeasureModeUndefined, YGUndefined, YGMeasureModeUndefined, Geometry.Scale);
                    const FVector2D Space = Geometry.GetLocalSize();
                    TestTrue(*FString::Printf(TEXT("%s %s natural text fits its native allocation"), *Case.Name, Label),
                        Geometry.Scale > 0 && FMath::IsFinite(Geometry.Scale) && Needed.X > 0 && Needed.Y > 0
                        && FMath::IsFinite(Needed.X) && FMath::IsFinite(Needed.Y)
                        && Needed.X <= Space.X + 0.5 && Needed.Y <= Space.Y + 0.5);
                }
            }
            if (Case.Capture)
            {
                TArray<FColor> Pixels;
                FIntVector Size = FIntVector::ZeroValue;
                const bool Captured = DefaultSlate.TakeScreenshot(Mount, Pixels, Size);
                const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/DefaultFontProof"));
                IFileManager::Get().MakeDirectory(*Directory, true);
                TestTrue(*FString::Printf(TEXT("%s native default-font capture saved"), *Case.Name), Captured && Size.X > 0 && Size.Y > 0
                    && Pixels.Num() >= Size.X * Size.Y && FImageUtils::SaveImageByExtension(*FPaths::Combine(Directory, Case.Name + TEXT(".png")),
                        FImageView(Pixels.GetData(), Size.X, Size.Y, ERawImageFormat::BGRA8)));
            }
        }
    }
    TSharedPtr<SCkFlexText> SessionHeld;
    TWeakObjectPtr<UCk_UI_AuthoredSession_UE> ReleasedSession;
    TArray<TWeakObjectPtr<UFontFace>> SessionAssets;
    TArray<TArray<uint8>> SessionBytes;
    {
        // Exercise reflected admission through the same native owner used by script cards.
        FScratch InvalidSources;
        const FString Missing = InvalidSources.Path(TEXT("absent-face.ttf"));
        const FString Malformed = InvalidSources.Path(TEXT("malformed-face.ttf"));
        if (!TestTrue(TEXT("Malformed reflected font fixture saved"),
            FFileHelper::SaveStringToFile(TEXT("not a font"), *Malformed))) { return false; }
        auto AuthoredFamily = []() -> FCk_UI_AuthoredFontFamily
        {
            FCk_UI_AuthoredFontFamily Result(TEXT("card-ui"), FontFile(TEXT("Roboto-Regular.ttf")));
            Result.Set_BoldFile(FontFile(TEXT("Roboto-Bold.ttf")));
            Result.Set_FallbackRegularFile(FontFile(TEXT("DroidSansFallback.ttf")));
            Result.Set_FallbackBoldFile(FontFile(TEXT("DroidSansFallback.ttf")));
            return Result;
        };
        auto ConfigFor = [](TArray<FCk_UI_AuthoredFontFamily> InFamilies) -> FCk_UI_AuthoredSession_Config
        {
            FCk_UI_AuthoredSession_Config Config;
            Config.Set_FontFamilies(MoveTemp(InFamilies));
            Config.Set_Text(TArray<FCk_UI_AuthoredText>{FCk_UI_AuthoredText(TEXT("marker"), FText::FromString(TEXT("stable")))});
            Config.Set_Actions(TArray<FString>{TEXT("noop")});
            return Config;
        };
        const TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Session(NewObject<UCk_UI_AuthoredSession_UE>());
        auto Reject = [this, &Session](const TCHAR* InLabel, const FCk_UI_AuthoredSession_Config& InConfig)
        {
            TArray<FString> Errors;
            TestFalse(InLabel, Session->TryInitialize(InConfig, Errors));
            TestTrue(TEXT("Rejected font config diagnoses and publishes no view or catalog"),
                !Errors.IsEmpty() && !Session->Get_IsUsable() && !Session->Get_View().IsValid() && Session->Get_Revision() == 0);
        };
        FCk_UI_AuthoredFontFamily Invalid = AuthoredFamily();
        Reject(TEXT("Default reflected font family rejects"), ConfigFor({FCk_UI_AuthoredFontFamily{}}));
        Invalid.Set_RegularFile(TEXT(""));
        Reject(TEXT("Missing required Regular rejects"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_RegularFile(Missing);
        Reject(TEXT("Missing default face file rejects"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_RegularFile(Malformed);
        Reject(TEXT("Malformed nonempty primary font rejects"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_Name(TEXT("Bad Name"));
        Reject(TEXT("Invalid reflected family name rejects"), ConfigFor({Invalid}));
        Reject(TEXT("Duplicate reflected family rejects"), ConfigFor({AuthoredFamily(), AuthoredFamily()}));
        Invalid = AuthoredFamily(); Invalid.Set_BoldFile(Missing);
        Reject(TEXT("Missing optional Bold face rejects whole catalog"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_FallbackRegularFile(Missing);
        Reject(TEXT("Missing optional regular fallback rejects whole catalog"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_FallbackBoldFile(Missing);
        Reject(TEXT("Missing optional bold fallback rejects whole catalog"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_FallbackRegularFile(TEXT(""));
        Reject(TEXT("Bold fallback without regular fallback rejects"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_BoldFile(TEXT(""));
        Reject(TEXT("Bold fallback without primary Bold rejects"), ConfigFor({Invalid}));
        Invalid = AuthoredFamily(); Invalid.Set_Name(TEXT("second-ui")); Invalid.Set_BoldFile(Missing);
        Reject(TEXT("Valid first and invalid second family reject atomically"), ConfigFor({AuthoredFamily(), Invalid}));

        TArray<FString> Errors;
        FCk_UI_AuthoredFontFamily RegularOnly(TEXT("regular-only"), FontFile(TEXT("Roboto-Regular.ttf")));
        FCk_UI_AuthoredSession_Config AcceptedConfig = ConfigFor({AuthoredFamily(), RegularOnly});
        if (!TestTrue(TEXT("Corrected reflected catalog retries on same session"),
            Session->TryInitialize(AcceptedConfig, Errors)))
        { for (const FString& Error : Errors) { AddError(Error); } return false; }
        AcceptedConfig.Set_FontFamilies(TArray<FCk_UI_AuthoredFontFamily>{});
        const TSharedPtr<FCkUiView> View = Session->Get_View();
        if (!TestTrue(TEXT("Admitted reflected catalog publishes its view"), View.IsValid())) { return false; }
        const TSharedRef<SWidget> Region = View->GetRegion(TEXT("main"));
        const FString Body = TEXT("<text id=\"named\" class=\"named\">Session font</text>")
            TEXT("<text id=\"bold\" class=\"named bold\">Session bold</text><text id=\"core\">Core font</text>");
        const FString Style = TEXT(".named { font-family: card-ui; font-size: 21px; } .bold { font-weight: bold; }");
        if (!TestTrue(TEXT("Reflected family reloads into real native tree"), Session->TryReload(Markup(Body), Style, Errors)))
        { for (const FString& Error : Errors) { AddError(Error); } return false; }
        TSharedPtr<SCkFlexText> Held = FindFlex(Region, TEXT("Session font"));
        const TSharedPtr<SCkFlexText> BoldText = FindFlex(Region, TEXT("Session bold"));
        const TSharedPtr<SCkFlexText> CoreText = FindFlex(Region, TEXT("Core font"));
        const FCompositeFont* Composite = Held.IsValid() ? Held->GetFont().GetCompositeFont() : nullptr;
        if (!TestTrue(TEXT("Reflected named Regular, Bold and default Core resolve independently"), Held.IsValid() && BoldText.IsValid() && CoreText.IsValid()
            && HasFont(Held->GetFont(), Composite, TEXT("Regular"), 21, 0) && Composite != nullptr
            && HasFont(BoldText->GetFont(), Composite, TEXT("Bold"), 21, 0)
            && CoreText->GetFont().GetCompositeFont() == FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10).GetCompositeFont()
            && CoreText->GetFont().GetCompositeFont() != Composite)) { return false; }
        const int64 AcceptedRevision = Session->Get_Revision();
        if (!TestTrue(TEXT("Accepted session region has a committed child"),
            Region->GetChildren() != nullptr && Region->GetChildren()->Num() > 0)) { return false; }
        const TSharedRef<SWidget> AcceptedRoot = Region->GetChildren()->GetChildAt(0);
        for (const FString& InvalidStyle : {TEXT(".named { font-family: unknown-ui; }"),
            TEXT(".named { font-family: regular-only; font-weight: bold; }")})
        {
            Errors.Reset();
            TestFalse(TEXT("Unknown family or missing Bold reload rejects"), Session->TryReload(Markup(Body), InvalidStyle, Errors));
            TestTrue(TEXT("Rejected font reload preserves accepted tree and revision"), !Errors.IsEmpty()
                && Session->Get_Revision() == AcceptedRevision && Region->GetChildren() != nullptr
                && Region->GetChildren()->Num() > 0 && Region->GetChildren()->GetChildAt(0) == AcceptedRoot
                && FindFlex(Region, TEXT("Session font")) == Held
                && CoreText->GetFont().GetCompositeFont() == FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10).GetCompositeFont());
            FText Model;
            TestTrue(TEXT("Rejected font reload preserves reflected model"),
                Session->TryGetText(TEXT("marker"), Model) && Model.ToString() == TEXT("stable"));
        }
        Errors.Reset();
        TestTrue(TEXT("Corrected reflected CSS reload succeeds"), Session->TryReload(Markup(Body), Style, Errors)
            && Session->Get_Revision() == AcceptedRevision + 1);
        SessionHeld = FindFlex(Region, TEXT("Session font"));
        const FCompositeFont* HeldComposite = SessionHeld.IsValid() ? SessionHeld->GetFont().GetCompositeFont() : nullptr;
        if (!TestTrue(TEXT("Reflected family has owned primary and fallback faces"), HeldComposite != nullptr
            && HeldComposite->DefaultTypeface.Fonts.Num() == 2 && HeldComposite->FallbackTypeface.Typeface.Fonts.Num() == 2)) { return false; }
        for (const FTypeface* Typeface : {&HeldComposite->DefaultTypeface, &HeldComposite->FallbackTypeface.Typeface})
        {
            for (const FTypefaceEntry& Entry : Typeface->Fonts)
            { SessionAssets.Add(Cast<UFontFace>(const_cast<UObject*>(Entry.Font.GetFontFaceAsset()))); }
        }
        for (const TCHAR* Name : {TEXT("Roboto-Regular.ttf"), TEXT("Roboto-Bold.ttf"),
            TEXT("DroidSansFallback.ttf"), TEXT("DroidSansFallback.ttf")})
        {
            TArray<uint8> Bytes;
            if (!TestTrue(TEXT("Read reflected font source bytes"), FFileHelper::LoadFileToArray(Bytes, *FontFile(Name)))) { return false; }
            SessionBytes.Add(MoveTemp(Bytes));
        }
        ReleasedSession = Session.Get();
        Session->Request_Release();
    }
    FlushRenderingCommands();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("Retained font text does not retain the released session UObject"), ReleasedSession.IsValid());
    for (int32 Index = 0; Index < SessionAssets.Num(); ++Index)
    {
        const UFontFace* Asset = SessionAssets[Index].Get();
        TestTrue(TEXT("Externally held session text retains primary and fallback assets after release and GC"),
            Asset != nullptr && Asset->LoadingPolicy == EFontLoadingPolicy::Inline
            && Asset->FontFaceData->GetData() == SessionBytes[Index]);
    }
    TestTrue(TEXT("Externally held session text retains named composite after release and GC"),
        SessionHeld.IsValid() && SessionHeld->GetFont().GetCompositeFont() != nullptr);
    SessionHeld.Reset();
    FScratch Scratch;
    FCkUiFontFamilyRegistration Registration = Family(TEXT("journal"));
    // All four assets use private copies, so post-registration disk changes exercise byte ownership.
    const FString Sources[] = {Registration.RegularFile, Registration.BoldFile.GetValue(), Registration.FallbackRegularFile.GetValue(), Registration.FallbackBoldFile.GetValue()};
    TArray<TArray<uint8>> ExpectedBytes;
    TArray<FString> Copies;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        TArray<uint8> Bytes;
        if (!TestTrue(TEXT("Read real font source"), FFileHelper::LoadFileToArray(Bytes, *Sources[Index]))) { return false; }
        const FString Copy = Scratch.Path(*FString::Printf(TEXT("face-%d.ttf"), Index));
        if (!TestTrue(TEXT("Save private registration source"), FFileHelper::SaveArrayToFile(Bytes, *Copy))) { return false; }
        ExpectedBytes.Add(MoveTemp(Bytes));
        Copies.Add(Copy);
    }
    Registration.RegularFile = Copies[0]; Registration.BoldFile = Copies[1];
    Registration.FallbackRegularFile = Copies[2]; Registration.FallbackBoldFile = Copies[3];
    TUniquePtr<FCkUiFontRegistry> Registry = MakeUnique<FCkUiFontRegistry>();
    if (!TestTrue(TEXT("Native catalog registers all four owned faces"), FCkUiFontResources::Register(*Registry, Registration).Succeeded)
        || !TestTrue(TEXT("Regular-only family registers"), FCkUiFontResources::Register(*Registry, Family(TEXT("regular-only"), false, false)).Succeeded)) { return false; }
    TSharedPtr<const FCkUiFontRegistrySnapshot> Snapshot = Registry->CreateSnapshot();
    FSlateFontInfo Resolved;
    if (!TestTrue(TEXT("Catalog supplies font identity"), Snapshot->TryResolve(TEXT("journal"), TEXT("Regular"), Resolved))) { return false; }
    const FCompositeFont* Composite = Resolved.GetCompositeFont();
    if (!TestTrue(TEXT("Complete primary and fallback typefaces exist"), Composite != nullptr
        && Composite->DefaultTypeface.Fonts.Num() == 2 && Composite->FallbackTypeface.Typeface.Fonts.Num() == 2)) { return false; }
    TArray<TWeakObjectPtr<UFontFace>> Assets;
    for (const FTypeface* Typeface : {&Composite->DefaultTypeface, &Composite->FallbackTypeface.Typeface})
    {
        for (const FTypefaceEntry& Entry : Typeface->Fonts)
        {
            Assets.Add(Cast<UFontFace>(const_cast<UObject*>(Entry.Font.GetFontFaceAsset())));
        }
    }
    for (const FString& Copy : Copies)
    {
        if (!TestTrue(TEXT("Overwrite only private registration sources"), FFileHelper::SaveStringToFile(TEXT("replaced after admission"), *Copy))) { return false; }
    }

    TSharedPtr<FCkUiCollection> Collection;
    if (!TestTrue(TEXT("Collection schema creates"), FCkUiCollection::TryCreate({{TEXT("label"), ECkUiFieldKind::Text}}, Collection).Succeeded)) { return false; }
    FCkUiRecordData Record;
    Record.Key = TEXT("one");
    Record.Fields.Add(TEXT("label"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text, .Text = FText::FromString(TEXT("Catalog row"))});
    if (!TestTrue(TEXT("Production collection receives record"), Collection->TrySetRecords({Record}).Succeeded)) { return false; }
    FCkUiView::FDataBindings Data;
    Data.Collections.Add(TEXT("records"), Collection);
    Data.String.Add(TEXT("active"), TAttribute<FString>{TEXT("first")});
    Data.StringChanged.Add(TEXT("activate"), FCkUiOnStringChanged::CreateLambda([](const FString&) {}));
    int32 FactoryCalls = 0;
    FCkUiWidgetRegistry Widgets;
    FCkUiCustomWidgetRegistration Probe;
    Probe.Schema.Tag = TEXT("font-probe");
    Probe.Factory = [&FactoryCalls](const FCkUiCustomWidgetArguments&, FString&) -> TSharedPtr<SWidget>
    { ++FactoryCalls; return SNew(SBox); };
    if (!TestTrue(TEXT("Factory probe registers"), Widgets.Register(MoveTemp(Probe)).Succeeded)) { return false; }
    FCkUiView::FActions Actions;
    Actions.Add(TEXT("noop"), FSimpleDelegate::CreateLambda([] {}));
    TSharedPtr<FCkUiView> View = FCkUiView::Create({}, MoveTemp(Actions), {}, FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12), MoveTemp(Data), Widgets.CreateSnapshot(), Snapshot);
    TSharedPtr<SBox> Region = StaticCastSharedRef<SBox>(View->GetRegion(TEXT("main")));
    FWindowScope Window;
    Window.Window = SNew(SWindow).ClientSize(FVector2D(640, 480))[Region.ToSharedRef()];
    FSlateApplication& Slate = FSlateApplication::Get();
    Slate.AddWindow(Window.Window.ToSharedRef(), true);
    TSharedPtr<SCkFlexText> HeldText;
    {
        const FString Body = TEXT("<text id=\"label\" class=\"named\">Owned marker \u4e2d</text>")
            TEXT("<button id=\"button\" class=\"named bold\" action=\"noop\">Catalog button</button>")
            TEXT("<menu-button id=\"menu-button\" menu=\"commands\" label=\"Catalog menu\" class=\"named bold\"/>")
            TEXT("<text id=\"generic\" class=\"generic\">Generic label</text>")
            TEXT("<text id=\"mono\" class=\"mono\">Mono label</text>")
            TEXT("<tabs id=\"tabs\" class=\"tabs\" value-bind=\"active\" changed=\"activate\"><tab id=\"first-panel\" key=\"first\" label=\"Catalog tab\"><text id=\"panel-label\">Panel</text></tab></tabs>")
            TEXT("<repeat id=\"repeat\" bind=\"records\"><text id=\"repeat-value\" class=\"named\" bind-field=\"label\"/></repeat>")
            TEXT("<table id=\"table\" class=\"collection\" bind=\"records\"><table-column id=\"column\" label=\"Name\" sort-field=\"label\"><text id=\"cell\" class=\"named\" bind-field=\"label\"/></table-column></table>");
        const FString Styles = TEXT(".named { font-family: journal; font-size: 17px; letter-spacing: .12em; }")
            TEXT(".bold { font-weight: bold; } .tabs { font-family: journal; height: 80px; -ck-tabs-font-weight: normal; -ck-tabs-font-size: 19px; }")
            TEXT(".collection { height: 140px; }")
            TEXT(".generic { font-family: sans-serif; } .mono { font-family: monospace; }");
        const FCkUiLoadResult Loaded = View->TryReload(Markup(Body), Styles);
        if (!TestTrue(TEXT("Named fonts mount through native production view"), Loaded.Succeeded))
        { for (const FString& Error : Loaded.Errors) { AddError(Error); } return false; }
        Tick(Slate);
        HeldText = FindFlex(Region.ToSharedRef(), TEXT("Owned marker \u4e2d"));
        TestTrue(TEXT("Text applies exact composite, Regular, size, and tracking"), HeldText.IsValid()
            && HasFont(HeldText->GetFont(), Composite, TEXT("Regular"), 17, 120));
        const TSharedPtr<SCkFlexText> Button = FindFlex(Region.ToSharedRef(), TEXT("Catalog button"));
        TestTrue(TEXT("Button applies exact composite, Bold, size, and tracking"), Button.IsValid()
            && HasFont(Button->GetFont(), Composite, TEXT("Bold"), 17, 120));
        const TSharedPtr<STextBlock> Menu = FindLabel(Region.ToSharedRef(), TEXT("Catalog menu"));
        TestTrue(TEXT("Menu button applies exact composite, Bold, size, and tracking"), Menu.IsValid()
            && HasFont(Menu->GetFont(), Composite, TEXT("Bold"), 17, 120));
        const TSharedPtr<STextBlock> Tab = FindLabel(Region.ToSharedRef(), TEXT("Catalog tab"));
        TestTrue(TEXT("Tabs use their explicit weight and size with base tracking"), Tab.IsValid()
            && HasFont(Tab->GetFont(), Composite, TEXT("Regular"), 19, 0));
        const TSharedPtr<SCkFlexText> Generic = FindFlex(Region.ToSharedRef(), TEXT("Generic label"));
        TestTrue(TEXT("Generic sans-serif retains existing base font"), Generic.IsValid()
            && Generic->GetFont().GetCompositeFont() == FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12).GetCompositeFont());
        const TSharedPtr<SCkFlexText> Mono = FindFlex(Region.ToSharedRef(), TEXT("Mono label"));
        TestTrue(TEXT("Generic monospace retains base composite and Mono face"), Mono.IsValid()
            && HasFont(Mono->GetFont(), FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12).GetCompositeFont(), TEXT("Mono"), 12, 0));
        const TSharedPtr<SCkFlexText> Panel = FindFlex(Region.ToSharedRef(), TEXT("Panel"));
        TestTrue(TEXT("Named tab family does not inherit into panel text"), Panel.IsValid() && Panel->GetFont().GetCompositeFont() != Composite);
        const TSharedPtr<SCkFlexText> Repeated = FindFlex(Region.ToSharedRef(), TEXT("Catalog row"));
        TestTrue(TEXT("Repeat child receives snapshot and exact font identity"), Repeated.IsValid()
            && HasFont(Repeated->GetFont(), Composite, TEXT("Regular"), 17, 120));
        const TSharedPtr<SCkUiTable> Table = View->GetTable(TEXT("table"));
        const TSharedPtr<SCkFlexText> Cell = Table.IsValid() ? FindFlex(Table.ToSharedRef(), TEXT("Catalog row")) : nullptr;
        TestTrue(TEXT("Realized native table cell receives snapshot and exact font identity"), Cell.IsValid()
            && HasFont(Cell->GetFont(), Composite, TEXT("Regular"), 17, 120));

        const TSharedPtr<SCkUiTabs> Tabs = View->GetTabs(TEXT("tabs"));
        const int64 BeforeStyleReload = View->GetRevision();
        const FCkUiLoadResult StyleReload = View->TryReload(Markup(Body), Styles.Replace(TEXT("font-size: 17px"), TEXT("font-size: 18px")));
        TestTrue(TEXT("Named family style reload accepts"), StyleReload.Succeeded);
        TestEqual(TEXT("Accepted named font reload advances revision once"), View->GetRevision(), BeforeStyleReload + 1);
        TestTrue(TEXT("Named font style reload retains native tabs identity"), Tabs.IsValid() && View->GetTabs(TEXT("tabs")) == Tabs);
        Tick(Slate);
        HeldText = FindFlex(Region.ToSharedRef(), TEXT("Owned marker \u4e2d"));
        TestTrue(TEXT("Accepted reload applies new size with same admitted composite"), HeldText.IsValid()
            && HasFont(HeldText->GetFont(), Composite, TEXT("Regular"), 18, 120));
        const FCkUiLoadResult BoldTabsReload = View->TryReload(Markup(Body), Styles.Replace(TEXT("-ck-tabs-font-weight: normal"), TEXT("-ck-tabs-font-weight: bold")));
        if (!TestTrue(TEXT("Tabs explicit Bold face reload accepts"), BoldTabsReload.Succeeded))
        { for (const FString& Error : BoldTabsReload.Errors) { AddError(Error); } return false; }
        Tick(Slate);
        const TSharedPtr<STextBlock> BoldTab = FindLabel(Region.ToSharedRef(), TEXT("Catalog tab"));
        TestTrue(TEXT("Tabs reload resolves explicit Bold with retained native identity"), View->GetTabs(TEXT("tabs")) == Tabs
            && BoldTab.IsValid() && HasFont(BoldTab->GetFont(), Composite, TEXT("Bold"), 19, 0));
        HeldText = FindFlex(Region.ToSharedRef(), TEXT("Owned marker \u4e2d"));

        const int64 Revision = View->GetRevision();
        const TSharedRef<SWidget> AcceptedRoot = Region->GetChildren()->GetChildAt(0);
        for (const FString& InvalidStyles : {TEXT(".invalid { font-family: unknown-font; }"), TEXT(".invalid { font-family: regular-only; font-weight: bold; }")})
        {
            const FCkUiLoadResult Rejected = View->TryReload(Markup(TEXT("<font-probe id=\"probe\"/><text id=\"invalid\" class=\"invalid\">Reject</text>")), InvalidStyles);
            TestFalse(TEXT("Unknown family or absent effective face rejects candidate"), Rejected.Succeeded);
            TestTrue(TEXT("Font rejection has diagnostics"), !Rejected.Errors.IsEmpty());
            TestTrue(TEXT("Font rejection reaches resource resolution"), Rejected.Errors.ContainsByPredicate([](const FString& Error)
                { return Error.Contains(TEXT("requires registered font family")); }));
            TestEqual(TEXT("Font rejection precedes every custom factory"), FactoryCalls, 0);
            TestEqual(TEXT("Font rejection retains accepted revision"), View->GetRevision(), Revision);
            TestTrue(TEXT("Font rejection retains accepted region root"), Region->GetChildren()->GetChildAt(0) == AcceptedRoot);
        }
        const FCkUiLoadResult RejectedTabs = View->TryReload(Markup(TEXT("<font-probe id=\"probe\"/><tabs id=\"invalid-tabs\" class=\"invalid\" value-bind=\"active\" changed=\"activate\"><tab id=\"invalid-panel\" key=\"first\" label=\"Reject tab\"><text id=\"invalid-panel-text\">Panel</text></tab></tabs>")),
            TEXT(".invalid { font-family: regular-only; -ck-tabs-font-weight: bold; }"));
        TestFalse(TEXT("Absent Bold selected by final tabs weight override rejects"), RejectedTabs.Succeeded);
        TestTrue(TEXT("Tabs rejection diagnoses the required registered Bold face"), RejectedTabs.Errors.ContainsByPredicate([](const FString& Error)
            { return Error.Contains(TEXT("requires registered font family 'regular-only' with face 'Bold'")); }));
        TestEqual(TEXT("Tabs font validation precedes custom factory"), FactoryCalls, 0);
        TestEqual(TEXT("Tabs font rejection retains accepted revision"), View->GetRevision(), Revision);
        TestTrue(TEXT("Tabs font rejection retains accepted root and native tabs"), Region->GetChildren()->GetChildAt(0) == AcceptedRoot && View->GetTabs(TEXT("tabs")) == Tabs);

    }
    if (!HeldText.IsValid()) { return false; }
    // Drop every registry/view owner before GC. HeldText is the surviving consumer.
    Window.Close();
    View.Reset(); Region.Reset(); Snapshot.Reset(); Registry.Reset(); Resolved = FSlateFontInfo{};
    FlushRenderingCommands();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    for (int32 Index = 0; Index < Assets.Num(); ++Index)
    {
        const UFontFace* Asset = Assets[Index].Get();
        TestTrue(TEXT("Primary and fallback UFontFace survive external text ownership across GC"), Asset != nullptr);
        if (Asset != nullptr)
        {
            TestTrue(TEXT("Admitted primary/fallback bytes survive source replacement and GC unchanged"), Asset->FontFaceData->GetData() == ExpectedBytes[Index]);
            TestTrue(TEXT("Owned font face uses inline loading"), Asset->LoadingPolicy == EFontLoadingPolicy::Inline);
        }
    }
    const FSlateFontInfo HeldFont = HeldText->GetFont();
    const TSharedRef<FSlateFontCache> Cache = Slate.GetRenderer()->GetFontCache();
    const FCompositeFont* HeldComposite = HeldFont.GetCompositeFont();
    if (HeldComposite != nullptr && HeldComposite->DefaultTypeface.Fonts.Num() == 2 && HeldComposite->FallbackTypeface.Typeface.Fonts.Num() == 2)
    {
        const FFontData& Primary = HeldComposite->DefaultTypeface.Fonts[0].Font;
        const FFontData& Fallback = HeldComposite->FallbackTypeface.Typeface.Fonts[0].Font;
        TestFalse(TEXT("Marker glyph is absent from primary without fallback"), Cache->CanLoadCodepoint(Primary, 0x4e2d, EFontFallback::FF_NoFallback));
        TestTrue(TEXT("Registered fallback owns marker glyph"), Cache->CanLoadCodepoint(Fallback, 0x4e2d, EFontFallback::FF_NoFallback));
        float Scaling = 1.0f;
        const FFontData& Selected = Cache->GetFontDataForCodepoint(HeldFont, 0x4e2d, Scaling);
        TestTrue(TEXT("Native font cache selects registered fallback asset"), Selected.GetFontFaceAsset() == Fallback.GetFontFaceAsset());
        const FVector2D Measured = HeldText->ComputeDesiredSize(1.0f);
        TestTrue(TEXT("Surviving native text measures with admitted primary and marker fallback"), Measured.X > 0 && Measured.Y > 0 && FMath::IsFinite(Measured.X) && FMath::IsFinite(Measured.Y));
    }
    else { AddError(TEXT("Surviving text lost its complete owned composite.")); }
    return !HasAnyErrors();
}

#endif

#include "CoreMinimal.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkSlateLayout/CkUiWorldMap.h"
#include "CkSlateLayout/CkUiCollection.h"
#include "CkSlateLayout/CkUiDocument.h"
#include "CkSlateLayout/SCkUiSurface.h"
#include "CkWidgets/Authoring/CkAuthoredSession.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"
#include <limits>

namespace ck_world_map_native_test
{
    auto Text(const FString& Name, const FString& Value) -> FCk_UI_AuthoredFieldValue
    { FCk_UI_AuthoredFieldValue Field{Name, ECk_UI_AuthoredFieldKind::Text}; Field.Set_Text(FText::FromString(Value)); return Field; }
    auto Number(const FString& Name, float Value) -> FCk_UI_AuthoredFieldValue
    { FCk_UI_AuthoredFieldValue Field{Name, ECk_UI_AuthoredFieldKind::Number}; Field.Set_Number(Value); return Field; }
    auto Bool(const FString& Name, bool Value) -> FCk_UI_AuthoredFieldValue
    { FCk_UI_AuthoredFieldValue Field{Name, ECk_UI_AuthoredFieldKind::Bool}; Field.Set_Bool(Value); return Field; }
    auto Record(const FString& Key, float X, float Y, const FString& Kind = TEXT("quest"),
        bool Enabled = true, bool Visible = true) -> FCk_UI_AuthoredRecord
    {
        FCk_UI_AuthoredRecord Result{Key};
        Result.Set_Fields({Number(TEXT("x"), X), Number(TEXT("y"), Y), Text(TEXT("label"), Key),
            Text(TEXT("kind"), Kind), Bool(TEXT("enabled"), Enabled), Bool(TEXT("visible"), Visible)});
        return Result;
    }
    auto Collection() -> FCk_UI_AuthoredCollection
    {
        FCk_UI_AuthoredCollection Result{TEXT("Markers")};
        Result.Set_Schema({{TEXT("x"), ECk_UI_AuthoredFieldKind::Number},
            {TEXT("y"), ECk_UI_AuthoredFieldKind::Number}, {TEXT("label"), ECk_UI_AuthoredFieldKind::Text},
            {TEXT("kind"), ECk_UI_AuthoredFieldKind::Text}, {TEXT("enabled"), ECk_UI_AuthoredFieldKind::Bool},
            {TEXT("visible"), ECk_UI_AuthoredFieldKind::Bool}});
        Result.Set_Records({Record(TEXT("quest"), 0.25f, 0.25f),
            Record(TEXT("vendor"), 0.75f, 0.75f, TEXT("vendor")),
            Record(TEXT("hidden"), 0.5f, 0.5f, TEXT("hazard"), true, false)});
        return Result;
    }
    template <typename TWidget>
    auto Find(const TSharedRef<SWidget>& Root, const FString& Type, FName Tag) -> TSharedPtr<TWidget>
    {
        if (Root->GetTypeAsString() == Type && Root->GetTag() == Tag) { return StaticCastSharedRef<TWidget>(Root); }
        const auto* Children = Root->GetChildren();
        for (int32 I = 0; Children != nullptr && I < Children->Num(); ++I)
        { if (const auto Match = Find<TWidget>(ConstCastSharedRef<SWidget>(Children->GetChildAt(I)), Type, Tag); Match.IsValid()) { return Match; } }
        return {};
    }
    constexpr const TCHAR* Markup = TEXT("<ui version=\"1\"><region name=\"main\"><column id=\"root\">"
        "<world-map id=\"map\" markers-bind=\"Markers\" image-bind=\"MapImage\" select=\"SelectMarker\" "
        "selected-key-bind=\"SelectedKey\" route-bind=\"RouteVisible\" origin-x=\"0.3\" origin-y=\"0.55\"/>"
        "</column></region></ui>");
    auto Buttons(bool Held) -> const TSet<FKey>&
    {
        static const TSet<FKey> None;
        static const TSet<FKey> Left{EKeys::LeftMouseButton};
        return Held ? Left : None;
    }
    auto Pointer(uint32 User, FVector2D Position, FVector2D Last, FKey Button, bool Held) -> FPointerEvent
    { return FPointerEvent(User, FSlateApplication::CursorPointerIndex, Position, Last,
        Buttons(Held), Button, 0.0f, FModifierKeysState{}); }
    auto Near(FVector2D A, FVector2D B, double Error = 0.005) -> bool
    { return (A - B).Size() <= Error; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkUiWorldMap_Native,
    "Ck.UiAuthoring.WorldMap.NativeControl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto FCkUiWorldMap_Native::RunTest(const FString&) -> bool
{
    using namespace ck_world_map_native_test;
    if (!FSlateApplication::IsInitialized()) { AddError(TEXT("World map native test requires Slate.")); return false; }
    TStrongObjectPtr<UTexture2D> Texture{UTexture2D::CreateTransient(400, 200)};
    if (!TestNotNull(TEXT("Map fixture texture exists"), Texture.Get())) { return false; }
    auto* Platform = Texture->GetPlatformData();
    if (!TestTrue(TEXT("Map fixture has writable pixels"), Platform != nullptr && Platform->Mips.Num() == 1)) { return false; }
    auto* Texels = static_cast<FColor*>(Platform->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
    if (!TestNotNull(TEXT("Map texture mip locks"), Texels)) { return false; }
    for (int32 I = 0; I < 400 * 200; ++I) { Texels[I] = FColor(36, 144, 208, 255); }
    Platform->Mips[0].BulkData.Unlock(); Texture->UpdateResource();
    FCk_UI_AuthoredImage Image{TEXT("MapImage")}; Image.Set_Texture(Texture.Get());
    FCk_UI_AuthoredSession_Config Config;
    Config.Set_Collections({Collection()}); Config.Set_Images({Image});
    Config.Set_Text({FCk_UI_AuthoredText{TEXT("SelectedKey"), FText::FromString(TEXT("quest"))}});
    Config.Set_Bool({FCk_UI_AuthoredBool{TEXT("RouteVisible"), true}});
    Config.Set_ItemActions({TEXT("SelectMarker")}); Config.Set_RegisterMapControls(true);
    TStrongObjectPtr<UCk_UI_AuthoredSession_UE> Session{NewObject<UCk_UI_AuthoredSession_UE>()};
    TArray<FString> Errors;
    if (!TestTrue(TEXT("Map session initializes"), Session->TryInitialize(Config, Errors))) { return false; }
    if (!TestTrue(TEXT("Registered map admits a complete collection and image"), Session->TryReload(Markup, TEXT(""), Errors)))
    { Session->Request_Release(); return false; }
    const auto View = Session->Get_View();
    const auto Map = Find<SCkUiWorldMap>(View->GetRegion(TEXT("main")), TEXT("SCkUiWorldMap"), TEXT("map"));
    if (!TestTrue(TEXT("Authored region contains the native map"), Map.IsValid()))
    { Session->Request_Release(); return false; }
    auto& Slate = FSlateApplication::Get();
    const TSharedRef<SWindow> Window = SNew(SWindow).ClientSize(FVector2D(640, 480))
        .CreateTitleBar(false)[View->GetRegion(TEXT("main"))];
    Slate.AddWindow(Window, true);
    ON_SCOPE_EXIT { Session->Request_Release(); Slate.DestroyWindowImmediately(Window); };
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    if (!GUsingNullRHI)
    {
        TArray<FColor> Pixels; FIntVector Size = FIntVector::ZeroValue;
        if (TestTrue(TEXT("Native map has a renderer screenshot"),
            Slate.TakeScreenshot(Map.ToSharedRef(), Pixels, Size)
            && Size.X > 40 && Size.Y > 40 && Pixels.Num() >= Size.X * Size.Y))
        {
            const FColor CenterPixel = Pixels[(Size.Y / 2) * Size.X + Size.X / 2];
            TestTrue(TEXT("Production map renderer paints the actual blue fixture texture"),
                CenterPixel.B > 150 && CenterPixel.G > 90 && CenterPixel.R < 90);
            TestTrue(TEXT("Route scalar can change in the authored session"),
                Session->TrySetBool(TEXT("RouteVisible"), false));
            Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
            TArray<FColor> WithoutRoute; FIntVector OtherSize = FIntVector::ZeroValue;
            if (TestTrue(TEXT("Native map renders after route is disabled"),
                Slate.TakeScreenshot(Map.ToSharedRef(), WithoutRoute, OtherSize)
                && OtherSize == Size && WithoutRoute.Num() >= Size.X * Size.Y))
            {
                int32 Changed = 0;
                for (int32 I = 0; I < Size.X * Size.Y; ++I)
                {
                    const FColor A = Pixels[I], B = WithoutRoute[I];
                    if (FMath::Abs(int32(A.R) - int32(B.R)) + FMath::Abs(int32(A.G) - int32(B.G))
                        + FMath::Abs(int32(A.B) - int32(B.B)) > 75) { ++Changed; }
                }
                TestTrue(TEXT("Route binding changes actual rendered map pixels"), Changed > 20);
                const auto Marker = Map->GetMarkerLocalPosition(TEXT("quest"));
                if (TestTrue(TEXT("Selected marker has a paint location"), Marker.IsSet()))
                {
                    const FVector2D LocalSize = Map->GetCachedGeometry().GetLocalSize();
                    const int32 MX = FMath::RoundToInt(Marker.GetValue().X * Size.X / LocalSize.X);
                    const int32 MY = FMath::RoundToInt(Marker.GetValue().Y * Size.Y / LocalSize.Y);
                    int32 Amber = 0;
                    for (int32 Y = FMath::Max(0, MY - 11); Y <= FMath::Min(Size.Y - 1, MY + 11); ++Y)
                    for (int32 X = FMath::Max(0, MX - 11); X <= FMath::Min(Size.X - 1, MX + 11); ++X)
                    {
                        const FColor C = WithoutRoute[Y * Size.X + X];
                        if (C.R > 140 && C.R > C.B * 1.4 && C.G > C.B * 1.1) { ++Amber; }
                    }
                    TestTrue(TEXT("Selected marker paints amber pixels over the map image"), Amber > 2);
                }
            }
            TestTrue(TEXT("Route binding restores"), Session->TrySetBool(TEXT("RouteVisible"), true));
        }
    }
    TestTrue(TEXT("Marker snapshot contains all admitted records"), Map->GetMarkerCount() == 3
        && Map->GetVisibleMarkerCount() == 2
        && Map->GetMarkerLocalPosition(TEXT("quest")).IsSet()
        && !Map->GetMarkerLocalPosition(TEXT("hidden")).IsSet());
    FVector2D Projected, Unprojected;
    TestTrue(TEXT("Wide viewport aspect-fits a 2:1 map with fixed vertical gutters"),
        Map->TryProjectToLocal(FVector2D(0.5, 0.5), FVector2D(800, 800), Projected)
        && Near(Projected, FVector2D(400, 400), 0.01)
        && Map->TryProjectToLocal(FVector2D(0, 0), FVector2D(800, 800), Projected)
        && Near(Projected, FVector2D(0, 200), 0.01)
        && Map->TryUnprojectFromLocal(Projected, FVector2D(800, 800), Unprojected)
        && Near(Unprojected, FVector2D(0, 0), 0.01));
    TestTrue(TEXT("Tall viewport aspect-fits with horizontal gutters"),
        Map->TryProjectToLocal(FVector2D(1, 1), FVector2D(400, 800), Projected)
        && Near(Projected, FVector2D(400, 500), 0.01)
        && Map->TryUnprojectFromLocal(Projected, FVector2D(400, 800), Unprojected)
        && Near(Unprojected, FVector2D(1, 1), 0.01));
    TestFalse(TEXT("Gutter cannot be mistaken for a map coordinate"),
        Map->TryUnprojectFromLocal(FVector2D(200, 100), FVector2D(400, 800), Unprojected));
    TestFalse(TEXT("Malformed projection arguments reject"),
        Map->TryProjectToLocal(FVector2D(std::numeric_limits<float>::quiet_NaN(), 0.5), FVector2D(800, 800), Projected));

    const FVector2D InitialCenter = Map->GetCameraCenter(); const float InitialZoom = Map->GetZoom();
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    TestTrue(TEXT("Malformed camera requests reject atomically"),
        !Map->TrySetCamera(FVector2D(NaN, 0.5), 2)
        && !Map->TrySetCamera(FVector2D(0.5, 0.5), NaN)
        && !Map->TrySetCamera(FVector2D(0.5, 0.5), 0.5f)
        && !Map->TrySetCamera(FVector2D(0.5, 0.5), 8.1f)
        && !Map->TryZoomBy(NaN) && !Map->TryZoomBy(-1)
        && Near(Map->GetCameraCenter(), InitialCenter) && Map->GetZoom() == InitialZoom);
    TestTrue(TEXT("Camera zoom and reveal use admitted marker position"),
        Map->TrySetCamera(FVector2D(0.2, 0.8), 2.0f)
        && Map->TryRevealMarker(TEXT("vendor"))
        && Map->GetCameraCenter().X > 0.6 && Map->GetCameraCenter().X <= 0.75
        && Map->GetCameraCenter().Y > 0.5 && Map->GetCameraCenter().Y <= 0.75
        && !Map->TryRevealMarker(TEXT("hidden"))
        && !Map->TryRevealMarker(TEXT("missing")));
    TestTrue(TEXT("Camera clamps both axes at maximum zoom"),
        Map->TrySetCamera(FVector2D(-4, 6), 8)
        && Map->GetCameraCenter().X >= 0 && Map->GetCameraCenter().X <= 1
        && Map->GetCameraCenter().Y >= 0 && Map->GetCameraCenter().Y <= 1);
    TestTrue(TEXT("Compatible reload retains control and its camera"),
        Session->TryReload(Markup, TEXT(""), Errors)
        && Find<SCkUiWorldMap>(View->GetRegion(TEXT("main")), TEXT("SCkUiWorldMap"), TEXT("map")) == Map
        && Map->GetZoom() == 8);
    TestTrue(TEXT("Reset camera before anchored wheel input"),
        Map->TrySetCamera(FVector2D(0.5, 0.5), 2));
    const FVector2D Viewport = Map->GetCachedGeometry().GetLocalSize();
    const FVector2D WheelLocal = Viewport * FVector2D(0.60, 0.55);
    FVector2D WheelBefore, WheelAfter;
    const FGeometry WheelGeometry = Map->GetCachedGeometry();
    const FVector2D WheelScreen = WheelGeometry.LocalToAbsolute(WheelLocal);
    const FPointerEvent Wheel{0, FSlateApplication::CursorPointerIndex,
        WheelScreen, WheelScreen, Buttons(false), EKeys::Invalid, 1.0f, FModifierKeysState{}};
    TestTrue(TEXT("Wheel zoom preserves the map coordinate under its interior cursor"),
        Map->TryUnprojectFromLocal(WheelLocal, Viewport, WheelBefore)
        && Map->OnMouseWheel(WheelGeometry, Wheel).IsEventHandled()
        && Map->GetZoom() > 2
        && Map->TryUnprojectFromLocal(WheelLocal, Viewport, WheelAfter)
        && Near(WheelBefore, WheelAfter, 0.005));
    TestTrue(TEXT("Map gutter still rejects unprojection at fit zoom"),
        Map->TrySetCamera(FVector2D(0.5, 0.5), 1)
        && !Map->TryUnprojectFromLocal(FVector2D(Viewport.X * 0.5, 1), Viewport, WheelAfter));
    const FVector2D GutterLocal(Viewport.X * 0.5, 1);
    const FVector2D GutterScreen = WheelGeometry.LocalToAbsolute(GutterLocal);
    const FPointerEvent GutterWheel{0, FSlateApplication::CursorPointerIndex,
        GutterScreen, GutterScreen, Buttons(false), EKeys::Invalid, 1.0f, FModifierKeysState{}};
    const FVector2D BeforeGutter = Map->GetCameraCenter(); const float BeforeGutterZoom = Map->GetZoom();
    TestTrue(TEXT("Wheel over fixed fit gutter is unhandled and leaves camera unchanged"),
        !Map->OnMouseWheel(WheelGeometry, GutterWheel).IsEventHandled()
        && Near(Map->GetCameraCenter(), BeforeGutter) && Map->GetZoom() == BeforeGutterZoom);

    TArray<FCk_UI_AuthoredRecord> Baseline;
    TestTrue(TEXT("Accepted collection can be inspected"), Session->TryGetRecords(TEXT("Markers"), Baseline));
    const int64 Revision = Map->GetAcceptedRevision();
    auto NonFinite = Baseline; NonFinite.Add(Record(TEXT("nan"), NaN, 0.5f));
    Errors.Reset();
    TestTrue(TEXT("Collection rejects nonfinite marker coordinates before publication"),
        !Session->TrySetRecords(TEXT("Markers"), NonFinite, Errors)
        && Map->GetAcceptedRevision() == Revision && Map->TryRevealMarker(TEXT("quest")));
    for (const auto& Bad : {Record(TEXT("negative"), -0.1f, 0.5f),
        Record(TEXT("over"), 1.1f, 0.5f), Record(TEXT("unknown-kind"), 0.5f, 0.5f, TEXT("unknown"))})
    {
        auto Candidate = Baseline; Candidate.Add(Bad); Errors.Reset();
        Session->TrySetRecords(TEXT("Markers"), Candidate, Errors);
        TestTrue(TEXT("Malformed revision preserves complete accepted drawing"),
            Map->GetAcceptedRevision() == Revision && Map->GetMarkerCount() == 3
            && !Map->GetMarkerLocalPosition(Bad.Get_Key()).IsSet());
        TestFalse(TEXT("Malformed current collection blocks map commands"), Map->TryRevealMarker(TEXT("quest")));
    }
    Errors.Reset();
    TestTrue(TEXT("Valid replacement restores input eligibility"),
        Session->TrySetRecords(TEXT("Markers"), Baseline, Errors) && Map->TryRevealMarker(TEXT("quest")));

    Map->TrySetCamera(FVector2D(0.5, 0.5), 1);
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    const FGeometry Geometry = Map->GetCachedGeometry();
    const auto Local = Map->GetMarkerLocalPosition(TEXT("quest"));
    if (TestTrue(TEXT("Visible marker has a local position for the physical gesture"), Local.IsSet()))
    {
        const FVector2D Start = Geometry.LocalToAbsolute(Local.GetValue());
        const auto Down = Pointer(0, Start, Start, EKeys::LeftMouseButton, true);
        const auto Up = Pointer(0, Start, Start, EKeys::LeftMouseButton, false);
        const TArray<TSharedRef<SWindow>> Windows{Window};
        TestTrue(TEXT("Native map receives a routed pointer hit"),
            Slate.LocateWindowUnderMouse(Start, Windows, false, 0).ContainsWidget(Map.Get()));
        Slate.SetCursorPos(Start);
        Slate.ProcessMouseMoveEvent(Pointer(0, Start, Start, EKeys::Invalid, false), true);
        TestTrue(TEXT("Owning pointer captures the marker press"),
            Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down)
            && Map->HasActivePointer() && Map->GetOwnedCapture().Num() == 1);
        TestFalse(TEXT("Active pointer rejects a document reload"), Session->TryReload(Markup, TEXT(""), Errors));
        TestFalse(TEXT("Foreign user cannot release owner's pointer"),
            Map->OnMouseButtonUp(Geometry, Pointer(1, Start, Start, EKeys::LeftMouseButton, false)).IsEventHandled());
        TestTrue(TEXT("Owning release closes the gesture"), Slate.ProcessMouseButtonUpEvent(Up)
            && !Map->HasActivePointer() && Map->GetOwnedCapture().IsEmpty());
        Slate.Tick();
        TestTrue(TEXT("Second press can become a pan without a stale click"),
            Map->TrySetCamera(FVector2D(0.5, 0.5), 2)
            && Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), Down));
        const FVector2D Moved = Start + FVector2D(40, 0);
        Slate.SetCursorPos(Moved);
        Slate.ProcessMouseMoveEvent(Pointer(0, Moved, Start, EKeys::Invalid, true), false);
        TestTrue(TEXT("Pan changes the camera after the drag threshold"),
            !Near(Map->GetCameraCenter(), FVector2D(0.5, 0.5), 0.01));
        const FVector2D DuringPan = Map->GetCameraCenter(); const float DuringZoom = Map->GetZoom();
        TestTrue(TEXT("Programmatic camera changes reject during a held pan without perturbing it"),
            !Map->TrySetCamera(FVector2D(0.9, 0.9), 4)
            && !Map->TryZoomBy(1.2f)
            && Near(Map->GetCameraCenter(), DuringPan) && Map->GetZoom() == DuringZoom);
        Slate.ProcessMouseButtonUpEvent(Pointer(0, Moved, Start, EKeys::LeftMouseButton, false));
    }
    if (Local.IsSet())
    {
        Map->TrySetCamera(FVector2D(0.5, 0.5), 1);
        const FVector2D Start = Map->GetCachedGeometry().LocalToAbsolute(
            Map->GetMarkerLocalPosition(TEXT("quest")).GetValue());
        Slate.SetCursorPos(Start);
        Slate.ProcessMouseMoveEvent(Pointer(0, Start, Start, EKeys::Invalid, false), true);
        const bool Pressed = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
            Pointer(0, Start, Start, EKeys::LeftMouseButton, true));
        const auto User = Slate.GetUser(0);
        TestTrue(TEXT("Owner-release proof starts with an actual Slate pointer capture"),
            Pressed && User.IsValid() && User->GetPointerCaptor(FSlateApplication::CursorPointerIndex).Get() == Map.Get());
        Session->Request_Release();
        TestTrue(TEXT("Releasing session clears the Slate captor and retained pointer"),
            User.IsValid() && User->GetPointerCaptor(FSlateApplication::CursorPointerIndex).Get() != Map.Get()
            && !Map->HasActivePointer() && Map->GetOwnedCapture().IsEmpty());
    }
    else { Session->Request_Release(); }
    TestTrue(TEXT("Retained control becomes inert after owner release"),
        View->IsOwnerReleased() && !Map->TryZoomBy(1.2f) && !Map->TryRevealMarker(TEXT("quest"))
        && !Map->HasActivePointer() && Map->GetOwnedCapture().IsEmpty());
    const auto Admission = [this, &Config](FCk_UI_AuthoredCollection Candidate, bool Expected, const TCHAR* Label)
    {
        auto CandidateConfig = Config; CandidateConfig.Set_Collections({Candidate});
        TStrongObjectPtr<UCk_UI_AuthoredSession_UE> CandidateSession{NewObject<UCk_UI_AuthoredSession_UE>()};
        TArray<FString> AdmissionErrors;
        const bool Initialized = CandidateSession->TryInitialize(CandidateConfig, AdmissionErrors);
        const bool Accepted = Initialized && CandidateSession->TryReload(Markup, TEXT(""), AdmissionErrors);
        TestEqual(Label, Accepted, Expected);
        CandidateSession->Request_Release();
    };
    auto Missing = Collection();
    {
        auto Records = Missing.Get_Records(); auto Fields = Records[0].Get_Fields();
        Fields.RemoveAll([](const FCk_UI_AuthoredFieldValue& Field) { return Field.Get_Name() == TEXT("label"); });
        Records[0].Set_Fields(Fields); Missing.Set_Records(Records);
    }
    Admission(Missing, false, TEXT("Missing required marker label rejects admission"));
    auto WrongType = Collection();
    {
        auto Schema = WrongType.Get_Schema();
        for (auto& Field : Schema)
        { if (Field.Get_Name() == TEXT("x")) { Field = FCk_UI_AuthoredFieldSchema(TEXT("x"), ECk_UI_AuthoredFieldKind::Text); } }
        WrongType.Set_Schema(Schema);
    }
    Admission(WrongType, false, TEXT("Wrong marker coordinate field kind rejects admission"));
    auto TooMany = Collection();
    {
        TArray<FCk_UI_AuthoredRecord> Records;
        for (int32 I = 0; I < 1025; ++I)
        { Records.Add(Record(FString::Printf(TEXT("marker-%d"), I), 0.5f, 0.5f)); }
        TooMany.Set_Records(Records);
    }
    Admission(TooMany, false, TEXT("Collection over marker limit rejects atomically"));
    auto NoVisible = Collection();
    {
        auto Schema = NoVisible.Get_Schema();
        Schema.RemoveAll([](const FCk_UI_AuthoredFieldSchema& Field) { return Field.Get_Name() == TEXT("visible"); });
        NoVisible.Set_Schema(Schema);
        auto Records = NoVisible.Get_Records();
        for (auto& Item : Records)
        {
            auto Fields = Item.Get_Fields();
            Fields.RemoveAll([](const FCk_UI_AuthoredFieldValue& Field) { return Field.Get_Name() == TEXT("visible"); });
            Item.Set_Fields(Fields);
        }
        NoVisible.Set_Records(Records);
    }
    Admission(NoVisible, true, TEXT("Optional visible field defaults to visible"));
    TSharedPtr<FCkUiCollection> CallbackMarkers;
    const TArray<FCkUiFieldSchema> CallbackSchema = {
        {TEXT("x"), ECkUiFieldKind::Number}, {TEXT("y"), ECkUiFieldKind::Number},
        {TEXT("label"), ECkUiFieldKind::Text}, {TEXT("kind"), ECkUiFieldKind::Text},
        {TEXT("enabled"), ECkUiFieldKind::Bool}, {TEXT("visible"), ECkUiFieldKind::Bool}};
    if (!TestTrue(TEXT("Callback fixture collection initializes"),
        FCkUiCollection::TryCreate(CallbackSchema, CallbackMarkers).Succeeded)) { return !HasAnyErrors(); }
    FCkUiRecordData CallbackRecord; CallbackRecord.Key = TEXT("callback-marker");
    CallbackRecord.Fields.Add(TEXT("x"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Number, .Number = 0.5f});
    CallbackRecord.Fields.Add(TEXT("y"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Number, .Number = 0.5f});
    CallbackRecord.Fields.Add(TEXT("label"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text,
        .Text = FText::FromString(TEXT("Callback marker"))});
    CallbackRecord.Fields.Add(TEXT("kind"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Text,
        .Text = FText::FromString(TEXT("quest"))});
    CallbackRecord.Fields.Add(TEXT("enabled"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Bool, .Bool = true});
    CallbackRecord.Fields.Add(TEXT("visible"), FCkUiFieldValue{.Kind = ECkUiFieldKind::Bool, .Bool = true});
    if (!TestTrue(TEXT("Callback fixture marker publishes"),
        CallbackMarkers->TrySetRecords({CallbackRecord}).Succeeded)) { return !HasAnyErrors(); }
    FCkUiWidgetRegistry CallbackRegistry;
    if (!TestTrue(TEXT("Callback map control registers"), RegisterWorldMapUiControl(CallbackRegistry).Succeeded))
    { return !HasAnyErrors(); }
    FSlateBrush CallbackBrush; CallbackBrush.ImageSize = FVector2D(400, 200);
    FSlateBrush ZeroSizeBrush; ZeroSizeBrush.ImageSize = FVector2D(0, 200);
    const FSlateBrush* BoundBrush = &CallbackBrush;
    bool CallbackSeen = false, CaptureGoneInsideCallback = false, CaptureGoneAfterRelease = false;
    TWeakPtr<SCkUiWorldMap> CallbackMapWeak;
    TWeakPtr<FCkUiView> CallbackViewWeak;
    FCkUiView::FDataBindings CallbackData;
    CallbackData.CanDispatchEvents = TAttribute<bool>(true);
    CallbackData.Collections.Add(TEXT("Markers"), CallbackMarkers);
    CallbackData.Images.Add(TEXT("MapImage"), TAttribute<const FSlateBrush*>::CreateLambda(
        [&BoundBrush]() -> const FSlateBrush* { return BoundBrush; }));
    CallbackData.Text.Add(TEXT("SelectedKey"), FText::FromString(TEXT("callback-marker")));
    CallbackData.Visibility.Add(TEXT("RouteVisible"), TAttribute<bool>(false));
    CallbackData.StringChanged.Add(TEXT("SelectMarker"), FCkUiOnStringChanged::CreateLambda(
        [&CallbackSeen, &CaptureGoneInsideCallback, &CaptureGoneAfterRelease,
            &CallbackMapWeak, &CallbackViewWeak](const FString& Key)
        {
            CallbackSeen = Key == TEXT("callback-marker");
            const auto User = FSlateApplication::Get().GetUser(0);
            const auto MapWidget = CallbackMapWeak.Pin();
            CaptureGoneInsideCallback = User.IsValid() && MapWidget.IsValid()
                && User->GetPointerCaptor(FSlateApplication::CursorPointerIndex).Get() != MapWidget.Get();
            if (const auto CallbackView = CallbackViewWeak.Pin(); CallbackView.IsValid())
            { CallbackView->ReleaseOwnerInteractions(); }
            CaptureGoneAfterRelease = User.IsValid() && MapWidget.IsValid()
                && User->GetPointerCaptor(FSlateApplication::CursorPointerIndex).Get() != MapWidget.Get();
        }));
    const auto CallbackView = FCkUiView::Create({}, {}, {}, {}, MoveTemp(CallbackData),
        CallbackRegistry.CreateSnapshot());
    CallbackViewWeak = CallbackView;
    const auto CallbackRegion = CallbackView->GetRegion(TEXT("main"));
    const auto CallbackLoad = CallbackView->TryReload(Markup, TEXT(""), TEXT("WorldMapCallback"));
    if (!TestTrue(TEXT("Callback map mounts through native registry"), CallbackLoad.Succeeded))
    { AddError(FString::Join(CallbackLoad.Errors, TEXT("; "))); return !HasAnyErrors(); }
    const auto CallbackMap = Find<SCkUiWorldMap>(CallbackRegion, TEXT("SCkUiWorldMap"), TEXT("map"));
    if (!TestTrue(TEXT("Callback map control exists"), CallbackMap.IsValid())) { return !HasAnyErrors(); }
    CallbackMapWeak = CallbackMap;
    const int64 CallbackRevision = CallbackView->GetRevision();
    BoundBrush = nullptr;
    const auto NullImageReload = CallbackView->TryReload(Markup, TEXT(""), TEXT("WorldMapNullImage"));
    BoundBrush = &ZeroSizeBrush;
    const auto ZeroImageReload = CallbackView->TryReload(Markup, TEXT(""), TEXT("WorldMapZeroImage"));
    BoundBrush = &CallbackBrush;
    TestTrue(TEXT("Null and zero-size image brushes reject reload without replacing accepted map"),
        !NullImageReload.Succeeded && !ZeroImageReload.Succeeded
        && CallbackView->GetRevision() == CallbackRevision
        && Find<SCkUiWorldMap>(CallbackRegion, TEXT("SCkUiWorldMap"), TEXT("map")) == CallbackMap);
    const TSharedRef<SWindow> CallbackWindow = SNew(SWindow).ClientSize(FVector2D(640, 480))
        .CreateTitleBar(false)[CallbackRegion];
    Slate.AddWindow(CallbackWindow, true);
    ON_SCOPE_EXIT { CallbackView->ReleaseOwnerInteractions(); Slate.DestroyWindowImmediately(CallbackWindow); };
    Slate.PumpMessages(); Slate.Tick(); Slate.Tick();
    const auto CallbackLocal = CallbackMap->GetMarkerLocalPosition(TEXT("callback-marker"));
    if (TestTrue(TEXT("Callback marker has live painted geometry"), CallbackLocal.IsSet()))
    {
        const FVector2D Position = CallbackMap->GetCachedGeometry().LocalToAbsolute(CallbackLocal.GetValue());
        Slate.SetCursorPos(Position);
        Slate.ProcessMouseMoveEvent(Pointer(0, Position, Position, EKeys::Invalid, false), true);
        const bool Pressed = Slate.ProcessMouseButtonDownEvent(CallbackWindow->GetNativeWindow(),
            Pointer(0, Position, Position, EKeys::LeftMouseButton, true));
        const auto User = Slate.GetUser(0);
        TestTrue(TEXT("Callback fixture holds real Slate capture before release"), Pressed
            && User.IsValid() && User->GetPointerCaptor(FSlateApplication::CursorPointerIndex).Get() == CallbackMap.Get());
        const bool Released = Slate.ProcessMouseButtonUpEvent(
            Pointer(0, Position, Position, EKeys::LeftMouseButton, false));
        TestTrue(TEXT("Selection callback sees capture cleared before retiring its owner"),
            Released && CallbackSeen && CaptureGoneInsideCallback && CaptureGoneAfterRelease
            && CallbackView->IsOwnerReleased() && !CallbackMap->HasActivePointer()
            && User.IsValid() && User->GetPointerCaptor(FSlateApplication::CursorPointerIndex).Get() != CallbackMap.Get());
    }
    return !HasAnyErrors();
}

#endif

#include "UI/CkUiReplicaPresentation.h"

#include "CkSlateLayout/CkUiFontRegistry.h"
#include "CkWidgets/Authoring/CkUiFontResources.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_ui_replica_presentation
{
    struct FRootedBrush final : FSlateBrush
    {
        TStrongObjectPtr<UTexture2D> Texture;
        explicit FRootedBrush(UTexture2D* InTexture) : Texture(InTexture) {}
    };

    auto TryLoad(const FString& InTheme, FCkUiView::FDataBindings& InOutData,
        TSharedPtr<const FCkUiFontRegistrySnapshot>& OutFonts, FString& OutError) -> bool
    {
        const bool IsMassEffect = InTheme == TEXT("MassEffect");
        const bool IsBattlefield = InTheme == TEXT("Battlefield");
        const bool IsInventory = InTheme == TEXT("Inventory");
        const bool IsMenu = InTheme == TEXT("Menu");
        if (!IsMassEffect && !IsBattlefield && !IsInventory && !IsMenu && InTheme != TEXT("Skyrim"))
        { OutError = TEXT("Unknown replica presentation theme."); return false; }
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("CkTests"));
        if (!Plugin.IsValid())
        { OutError = TEXT("Replica presentation resources are unavailable."); return false; }
        const FString Root = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/UiAuthoring/ReplicaPresentation"));
        const FString FontRoot = IsInventory || IsMenu ? FPaths::Combine(Plugin->GetBaseDir(),
            TEXT("Resources/UiAuthoring/DarkFantasyHudProof/Fonts")) : FPaths::Combine(Root, TEXT("Fonts"));
        FCkUiFontRegistry Fonts;
        const auto Admit = [&](const TCHAR* Name, const TCHAR* Regular, const TCHAR* Bold) -> bool
        {
            const auto Result = FCkUiFontResources::Register(Fonts, {
                .Name = Name,
                .RegularFile = FPaths::Combine(FontRoot, Regular),
                .BoldFile = FPaths::Combine(FontRoot, Bold),
                .FallbackRegularFile = FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/DroidSansFallback.ttf")),
            });
            if (!Result.Succeeded) { OutError = FString::Join(Result.Errors, TEXT("\n")); return false; }
            return true;
        };
        if (IsMassEffect)
        {
            if (!Admit(TEXT("me-ui"), TEXT("Rajdhani-Regular.ttf"), TEXT("Rajdhani-SemiBold.ttf"))
                || !Admit(TEXT("me-display"), TEXT("Rajdhani-SemiBold.ttf"), TEXT("Rajdhani-Bold.ttf")))
            { return false; }
        }
        else if (IsBattlefield)
        {
            if (!Admit(TEXT("bf-ui"), TEXT("BarlowCondensed-Regular.ttf"), TEXT("BarlowCondensed-Medium.ttf"))
                || !Admit(TEXT("bf-display"), TEXT("BarlowCondensed-Medium.ttf"), TEXT("BarlowCondensed-SemiBold.ttf")))
            { return false; }
        }
        else if (IsMenu)
        {
            if (!Admit(TEXT("menu-ui"), TEXT("SourceSans3-Regular.ttf"), TEXT("SourceSans3-Bold.ttf"))
                || !Admit(TEXT("menu-display"), TEXT("SourceSans3-Regular.ttf"), TEXT("SourceSans3-Bold.ttf")))
            { return false; }
        }
        else if (IsInventory)
        {
            if (!Admit(TEXT("inventory-ui"), TEXT("SourceSans3-Regular.ttf"), TEXT("SourceSans3-Bold.ttf"))
                || !Admit(TEXT("inventory-display"), TEXT("SourceSans3-Regular.ttf"), TEXT("SourceSans3-Bold.ttf")))
            { return false; }
        }
        else if (!Admit(TEXT("skyrim-ui"), TEXT("BarlowCondensed-Regular.ttf"), TEXT("BarlowCondensed-Medium.ttf"))
            || !Admit(TEXT("skyrim-display"), TEXT("BarlowCondensed-Medium.ttf"), TEXT("BarlowCondensed-SemiBold.ttf"))
            || !Admit(TEXT("journal-ui"), TEXT("BarlowCondensed-Regular.ttf"), TEXT("BarlowCondensed-Medium.ttf")))
        { return false; }
        if (IsBattlefield || IsInventory || IsMenu)
        {
            OutFonts = Fonts.CreateSnapshot();
            OutError.Reset();
            return true;
        }
        const FString Image = FPaths::Combine(Root, IsMassEffect ? TEXT("me-backdrop.png") : TEXT("skyrim-backdrop.png"));
        if (!FPaths::FileExists(Image))
        { OutError = TEXT("Replica scene artwork is unavailable."); return false; }
        UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Image);
        if (!IsValid(Texture))
        { OutError = TEXT("Replica scene artwork could not be imported."); return false; }
        TStrongObjectPtr<UTexture2D> PendingTexture{Texture};
        const int32 Width = Texture->GetSizeX();
        const int32 Height = Texture->GetSizeY();
        if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096)
        { OutError = TEXT("Replica scene dimensions are outside the bounded proof contract."); return false; }
        auto Brush = MakeShared<FRootedBrush>(Texture);
        Brush->DrawAs = ESlateBrushDrawType::Image;
        Brush->ImageSize = FVector2D{static_cast<double>(Width), static_cast<double>(Height)};
        Brush->SetResourceObject(Texture);
        InOutData.Images.Add(TEXT("ReplicaBackdrop"), TAttribute<const FSlateBrush*>::CreateLambda(
            [Brush]() -> const FSlateBrush* { return &Brush.Get(); }));
        OutFonts = Fonts.CreateSnapshot();
        OutError.Reset();
        return true;
    }
}

#pragma once

#include "CkSlateLayout/SCkUiSurface.h"

namespace ck_ui_replica_presentation
{
    // Trusted proof resources; publish no partial bindings on missing fonts/artwork.
    auto TryLoad(const FString& InTheme, FCkUiView::FDataBindings& InOutData,
        TSharedPtr<const FCkUiFontRegistrySnapshot>& OutFonts, FString& OutError) -> bool;
}

#pragma once

#include <NativeGameplayTags.h>

namespace ck::tests::hex_persistence
{
    CKTESTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AuthoredLocal);
    CKTESTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AuthoredBlocker);

    inline constexpr auto BoardAssetPath = TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_PersistenceBoard.DA_HexGrid_PersistenceBoard");
    inline constexpr auto Radius32BoardAssetPath = TEXT("/CkTests/CkGrid/HexGrid/DA_HexGrid_Phase8Radius32.DA_HexGrid_Phase8Radius32");
}

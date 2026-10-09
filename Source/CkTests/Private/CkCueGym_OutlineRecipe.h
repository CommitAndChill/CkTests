#pragma once

#include "CkCueGym_SourceVisuals.h"

// Only the two authored Cue PMG primitive outlines are representable. The
// caller compares generated lines with the actual source fragment before
// selecting a compact wire representation.
namespace ck_cue_outline_recipe
{
    CKTESTS_API bool TryGenerate(
        const FCkCueGym_SourceVisualRow& Row,
        TArray<FCkCueGym_SourceVisualLine>& OutLines);

    CKTESTS_API bool Get_MatchesActualLines(
        const FCkCueGym_SourceVisualRow& Row,
        const TArray<FCkCueGym_SourceVisualLine>& ActualLines);
}

#include "CkSnapshot_SaveCompletionProbe.h"

auto
    UCkSnapshot_SaveCompletionProbe::
    OnSaveComplete(ECk_SnapshotResult InResult)
    -> void
{
    ++CallbackCount;
    Result = InResult;
}

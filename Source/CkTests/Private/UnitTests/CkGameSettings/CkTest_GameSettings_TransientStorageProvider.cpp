#include "CkTest_GameSettings_TransientStorageProvider.h"

TArray<FCk_GameSettings_StoredValue>
UCkTest_GameSettings_TransientStorageProvider::Get_StoredValues_Implementation(
    ECk_GameSettings_Scope Scope, int32 UserId) const
{
    TArray<FCk_GameSettings_StoredValue> Result;
    for (const auto& Stored : Values)
        if (Stored.Scope == Scope && Stored.UserId == UserId)
            Result.Add(Stored.Entry);
    return Result;
}

void UCkTest_GameSettings_TransientStorageProvider::Request_StoreValue_Implementation(
    ECk_GameSettings_Scope Scope, int32 UserId, FName Key, const FString& Value)
{
    for (auto& Stored : Values)
        if (Stored.Scope == Scope && Stored.UserId == UserId &&
            Stored.Entry.Get_Key() == Key)
        { Stored.Entry = FCk_GameSettings_StoredValue(Key, Value); return; }
    Values.Add({Scope, UserId, FCk_GameSettings_StoredValue(Key, Value)});
}

void UCkTest_GameSettings_TransientStorageProvider::Request_RemoveValue_Implementation(
    ECk_GameSettings_Scope Scope, int32 UserId, FName Key)
{
    Values.RemoveAll([&](const FValue& Stored)
    { return Stored.Scope == Scope && Stored.UserId == UserId &&
        Stored.Entry.Get_Key() == Key; });
}

void UCkTest_GameSettings_TransientStorageProvider::Request_Flush_Implementation()
{
    // The complete production provider contract runs against transient memory.
}

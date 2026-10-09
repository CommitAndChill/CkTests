#pragma once

#include "CkGameSettings/Storage/CkGameSettings_StorageProvider.h"
#include "CkTest_GameSettings_TransientStorageProvider.generated.h"

/** Per-PIE-instance storage used only by the authored gym fixture; never reads or writes a file. */
UCLASS()
class CKTESTS_API UCkTest_GameSettings_TransientStorageProvider final
    : public UCk_GameSettings_StorageProvider_UE
{
    GENERATED_BODY()
public:
    TArray<FCk_GameSettings_StoredValue> Get_StoredValues_Implementation(
        ECk_GameSettings_Scope Scope, int32 UserId) const override;
    void Request_StoreValue_Implementation(
        ECk_GameSettings_Scope Scope, int32 UserId,
        FName Key, const FString& Value) override;
    void Request_RemoveValue_Implementation(
        ECk_GameSettings_Scope Scope, int32 UserId, FName Key) override;
    void Request_Flush_Implementation() override;
private:
    struct FValue
    {
        ECk_GameSettings_Scope Scope;
        int32 UserId;
        FCk_GameSettings_StoredValue Entry;
    };
    TArray<FValue> Values;
};

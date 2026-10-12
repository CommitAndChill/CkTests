#include "CoreMinimal.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "CkGym_ControlPanelTypes.h"
#include "CkGym_StartupSettings.h"
#include "CkEntityScriptGym_Runtime.h"
#include "CkGymStation_VisibleRuntime.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Font.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "CkTests/Net/CkNetAutomation_Common.h"
#include "CkDynamic/CkDynamic_Utils.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkEntityTag/CkEntityTag_Utils.h"
#include "CkTimer/CkTimer_Utils.h"
#include "CkTimer/CkTimer_Fragment.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace ck_station_showcase_network
{
    constexpr const TCHAR* Tags[8] = {TEXT("Gym.StationShowcase.AutoSize.Wide"), TEXT("Gym.StationShowcase.AutoSize.Tall"),
        TEXT("Gym.StationShowcase.AutoSize.Runtime"), TEXT("Gym.StationShowcase.Default"), TEXT("Gym.StationShowcase.Small"),
        TEXT("Gym.StationShowcase.Large"), TEXT("Gym.StationShowcase.NoSpotlight"), TEXT("Gym.StationShowcase.RightAlign")};
    template<class T> auto P(const UStruct* S, const TCHAR* N) -> T*
    { return IsValid(S) ? FindFProperty<T>(S, N) : nullptr; }
    auto Int(const UStruct* S, const void* D, const TCHAR* N) -> int32
    { const auto* Q = P<FIntProperty>(S, N); return D && Q ? Q->GetPropertyValue_InContainer(D) : INDEX_NONE; }
    auto Bool(const UStruct* S, const void* D, const TCHAR* N) -> bool
    { const auto* Q = P<FBoolProperty>(S, N); return D && Q && Q->GetPropertyValue_InContainer(D); }
    auto Object(const UStruct* S, const void* D, const TCHAR* N) -> UObject*
    { const auto* Q = P<FObjectPropertyBase>(S, N); return D && Q ? Q->GetObjectPropertyValue_InContainer(D) : nullptr; }
    auto Str(const UStruct* S, const void* D, const TCHAR* N) -> FString
    { const auto* Q = P<FStrProperty>(S, N); return D && Q ? Q->GetPropertyValue_InContainer(D) : FString{}; }
    auto Live(const FCk_Handle& H) -> bool
    { return ck::IsValid(H) && !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(H, ECk_EntityLifetime_DestructionPhase::BeginDestroy); }
    auto Root(UWorld* W) -> FCk_Handle
    { return IsValid(W) ? UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*W) : FCk_Handle{}; }
    auto Tagged(UWorld* W, int32 I) -> TArray<FCk_Handle>
    { const auto H = Root(W); return Live(H) && I >= 0 && I < 8 ? UCk_Utils_EntityTag_UE::ForEach_Entity(H, FName(Tags[I])) : TArray<FCk_Handle>{}; }
    auto Capture(const FCk_Handle& H, TSet<FCk_Handle>& Out) -> void
    {
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) || Out.Contains(H)) { return; }
        Out.Add(H);
        if (Live(H)) { for (const auto& Child : UCk_Utils_EntityLifetime_UE::Get_LifetimeDependents(H)) { Capture(Child, Out); } }
    }
    auto Retired(const TSet<FCk_Handle>& Captured) -> bool
    { for (const auto& H : Captured) { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H)) { return false; } } return true; }
    // Parameters belong to real reflected public methods/RPCs; no production fields are written.
    struct FCall
    {
        UObject* Target; UFunction* Function; TUniquePtr<FStructOnScope> Args; bool Valid;
        FCall(UObject* T, const TCHAR* Name, EFunctionFlags Required = FUNC_None)
            : Target(T), Function(IsValid(T) ? T->FindFunction(Name) : nullptr),
              Valid(Function && Function->HasAllFunctionFlags(Required))
        { if (Valid) { Args = MakeUnique<FStructOnScope>(Function); } }
        void I32(const TCHAR* Name, int32 V)
        { auto* Q = P<FIntProperty>(Function, Name); if (!Valid || !Q) { Valid = false; return; } Q->SetPropertyValue_InContainer(Args->GetStructMemory(), V); }
        bool Send() { if (!Valid || !IsValid(Target)) { return false; } Target->ProcessEvent(Function, Args->GetStructMemory()); return true; }
        bool B() const { return Valid && Bool(Function, Args->GetStructMemory(), TEXT("ReturnValue")); }
    };
    auto PureBool(UObject* T, const TCHAR* Name) -> bool
    { FCall C(T, Name, FUNC_BlueprintPure); return C.Send() && C.B(); }
    auto PureInt(UObject* T, const TCHAR* Name) -> int32
    { FCall C(T, Name, FUNC_BlueprintPure); return C.Send() ? Int(C.Function, C.Args->GetStructMemory(), TEXT("ReturnValue")) : INDEX_NONE; }
    auto Handles(UObject* T, const TCHAR* Name, bool Property = false) -> TArray<FCk_Handle>
    {
        FCall C(T, Name, FUNC_BlueprintPure);
        const UStruct* S = Property && IsValid(T) ? static_cast<const UStruct*>(T->GetClass()) : static_cast<const UStruct*>(C.Function);
        const auto* Q = P<FArrayProperty>(S, Property ? Name : TEXT("ReturnValue"));
        const auto* Inner = Q ? CastField<FStructProperty>(Q->Inner) : nullptr;
        if (!Inner || Inner->Struct != FCk_Handle::StaticStruct()) { return {}; }
        if (!Property && !C.Send()) { return {}; }
        const void* D = Property ? static_cast<void*>(T) : C.Args->GetStructMemory();
        return *Q->ContainerPtrToValuePtr<TArray<FCk_Handle>>(D);
    }
    auto Handle(UObject* T, const TCHAR* Name, bool Property = false) -> FCk_Handle
    {
        FCall C(T, Name, FUNC_BlueprintPure);
        const UStruct* S = Property && IsValid(T) ? static_cast<const UStruct*>(T->GetClass()) : static_cast<const UStruct*>(C.Function);
        const auto* Q = P<FStructProperty>(S, Property ? Name : TEXT("ReturnValue"));
        if (!Q || Q->Struct != FCk_Handle::StaticStruct() || (!Property && !C.Send())) { return {}; }
        return *Q->ContainerPtrToValuePtr<FCk_Handle>(Property ? static_cast<void*>(T) : C.Args->GetStructMemory());
    }
    struct FFixture
    {
        UClass *Mode = nullptr, *StateClass = nullptr, *PCClass = nullptr, *StationClass = nullptr, *TickerClass = nullptr;
        UScriptStruct *SnapshotType = nullptr, *CommitType = nullptr, *Display = nullptr, *Anchors = nullptr,
            *VisibleGeometry = nullptr;
        FStructProperty *Snapshot = nullptr, *Commit = nullptr, *CommitSample = nullptr;
        TWeakObjectPtr<UWorld> Server;
        TWeakObjectPtr<AGameStateBase> States[3]; TWeakObjectPtr<AGameModeBase> OriginalMode;
        TWeakObjectPtr<APlayerController> Local[2], Remote[2];
        TWeakObjectPtr<APawn> LocalPawn[2], RemotePawn[2];
        TSharedPtr<FStructOnScope> Frozen, Recipes;
        TMap<uint64, TSharedPtr<FStructOnScope>> Samples[3]; TArray<uint64> SampleOrder[3];
        static uint64 Key(int32 Generation, int32 Sequence) { return (uint64(uint32(Generation)) << 32) | uint32(Sequence); }
        TSet<FCk_Handle> Old[3], Terminal[3];
        FDelegateHandle Observer;
        mutable FString LastCohortDiagnostic;
        bool Dedicated = false, Captured = false, Departed = false, Strict = false, PeerQueued = false;
        bool SawInstalled[2]{}, SawRetainedAdmission = false, SawOldInstallAgainstLatest = false;
        bool AdmissionCacheObserved = false, PreinstalledAtAdmission = false;
        int32 PreviousInstalledDelivery[2]{}, AdmissionInstalledDelivery = 0;
        int32 Origin = 0, FirstDelivery = 0, FirstRequest = 11, PeerRequest = 21;
        ECkGym_StartupMode Startup = ECkGym_StartupMode::Cycler; FString Last; TArray<FString> Recents;
        AGameStateBase* State(UWorld* W) const
        { auto* S = IsValid(W) ? W->GetGameState() : nullptr; return IsValid(S) && S->GetClass() == StateClass ? S : nullptr; }
        const void* Data(UWorld* W) const
        { auto* S = State(W); return S && Snapshot ? Snapshot->ContainerPtrToValuePtr<void>(S) : nullptr; }
        const void* OwnerCommit(APlayerController* PC) const
        { return IsValid(PC) && PC->GetClass() == PCClass && Commit ? Commit->ContainerPtrToValuePtr<void>(PC) : nullptr; }
        bool Context() const
        {
            UWorld* W = ck::auto_test::net::Get_ServerWorld();
            if (!IsValid(W) || !State(W) || !IsValid(W->GetAuthGameMode()) || W->GetAuthGameMode()->GetClass() != Mode ||
                W->GetNetMode() != (Dedicated ? NM_DedicatedServer : NM_ListenServer) ||
                ck::auto_test::net::Get_AllPIEWorlds().Num() != 3 || (Captured && (Server.Get() != W || States[0].Get() != State(W) || OriginalMode.Get() != W->GetAuthGameMode()))) { return false; }
            for (int32 I = 0; I < 2; ++I)
            {
                UWorld* C = ck::auto_test::net::Get_ClientWorld(I);
                APlayerController* PC = IsValid(C) ? C->GetFirstPlayerController() : nullptr;
                APlayerController* R = ck::auto_test::net::TryGet_RemoteClientPlayerController(W, I);
                auto* LP = IsValid(PC) ? PC->GetLocalPlayer() : nullptr;
                auto* GI = IsValid(C) ? C->GetGameInstance() : nullptr;
                if (!State(C) || C->GetNetMode() != NM_Client || !IsValid(PC) || !IsValid(R) ||
                    PC->GetClass() != PCClass || R->GetClass() != PCClass || !IsValid(LP) || !IsValid(GI) ||
                    GI->GetWorld() != C || GI->GetLocalPlayers().Num() != 1 || LP->GetWorld() != C || LP->GetPlayerController(C) != PC ||
                    !PC->IsLocalController() || PC->GetWorld() != C || R->GetWorld() != W ||
                    (Captured && (States[I + 1].Get() != State(C) || Local[I].Get() != PC || Remote[I].Get() != R ||
                        ((!Departed || I == 1) && (PC->GetPawn() != LocalPawn[I].Get() || R->GetPawn() != RemotePawn[I].Get()))))) { return false; }
            }
            return true;
        }
        void Restore()
        {
            if (Observer.IsValid()) { FWorldDelegates::OnWorldPostActorTick.Remove(Observer); Observer.Reset(); }
            auto* S = GetMutableDefault<UCkGym_StartupSettings>();
            S->StartupMode = Startup; S->LastGymName = Last; S->RecentGymNames = Recents; S->SaveConfig();
        }
    };
    auto Complete(const FFixture& F, UWorld* W, const void* Sample) -> bool
    {
        if (!F.State(W) || !Sample || !IsValid(F.SnapshotType)) { return false; }
        FCall C(F.State(W), TEXT("Get_IsCompleteSnapshot"), FUNC_BlueprintPure);
        const auto* Arg = P<FStructProperty>(C.Function, TEXT("InSample"));
        if (!C.Valid || !Arg || Arg->Struct != F.SnapshotType) { return false; }
        Arg->CopyCompleteValue(Arg->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory()), Sample);
        return C.Send() && C.B();
    }
    auto CopyStruct(const UScriptStruct* T, const void* D) -> TSharedPtr<FStructOnScope>
    {
        if (!IsValid(T) || !D) { return {}; }
        auto R = MakeShared<FStructOnScope>(T); T->CopyScriptStruct(R->GetStructMemory(), D); return R;
    }
    auto Installed(const FFixture& F, APlayerController* PC) -> TSharedPtr<FStructOnScope>
    {
        FCall C(PC, TEXT("Get_InstalledShowcaseCommit"), FUNC_BlueprintPure);
        const auto* Return = P<FStructProperty>(C.Function, TEXT("ReturnValue"));
        return Return && Return->Struct == F.CommitType && C.Send()
            ? CopyStruct(F.CommitType, Return->ContainerPtrToValuePtr<void>(C.Args->GetStructMemory())) : nullptr;
    }
    auto Rpc(const FFixture& F, int32 Owner, int32 Serial, int32 Generation) -> bool
    {
        if (Owner < 0 || Owner >= 2 || !F.Context() || !F.Local[Owner].IsValid() ||
            F.Local[Owner]->GetClass() != F.PCClass) { return false; }
        // A destroyed captured pawn and a current null pawn must never compare as a valid owner.
        const APawn* LocalPawn = F.Local[Owner]->GetPawn();
        const APawn* RemotePawn = F.Remote[Owner]->GetPawn();
        if (!F.LocalPawn[Owner].IsValid() || !F.RemotePawn[Owner].IsValid() ||
            !IsValid(LocalPawn) || !IsValid(RemotePawn) ||
            LocalPawn != F.LocalPawn[Owner].Get() || RemotePawn != F.RemotePawn[Owner].Get() ||
            LocalPawn->GetController() != F.Local[Owner].Get() ||
            RemotePawn->GetController() != F.Remote[Owner].Get() ||
            LocalPawn->GetWorld() != F.Local[Owner]->GetWorld() ||
            RemotePawn->GetWorld() != F.Server.Get() || !F.Remote[Owner]->HasAuthority()) { return false; }
        FCall C(F.Local[Owner].Get(), TEXT("Server_RequestShowcaseRebuild"), static_cast<EFunctionFlags>(FUNC_Net | FUNC_NetServer));
        if (!C.Valid || !IsValid(C.Function) || C.Function->NumParms != 2 ||
            C.Function->HasAnyFunctionFlags(static_cast<EFunctionFlags>(FUNC_NetClient | FUNC_NetMulticast | FUNC_Static | FUNC_Delegate))) { return false; }
        int32 Count = 0; FIntProperty* SerialParam = nullptr; FIntProperty* GenerationParam = nullptr;
        for (TFieldIterator<FProperty> It(C.Function, EFieldIteratorFlags::ExcludeSuper); It; ++It)
        {
            FProperty* Param = *It;
            if (!Param->HasAnyPropertyFlags(CPF_Parm)) { continue; }
            auto* Integer = CastField<FIntProperty>(Param);
            if (!Integer || Integer->ArrayDim != 1 || Integer->GetSize() != sizeof(int32) ||
                Integer->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm | CPF_ReferenceParm) ||
                Integer->GetOffset_ForInternal() < 0 ||
                Integer->GetOffset_ForInternal() + sizeof(int32) > C.Function->ParmsSize) { return false; }
            if (Integer->GetFName() == FName(TEXT("InSerial")) && !SerialParam) { SerialParam = Integer; }
            else if (Integer->GetFName() == FName(TEXT("InGeneration")) && !GenerationParam) { GenerationParam = Integer; }
            else { return false; }
            ++Count;
        }
        if (Count != 2 || !SerialParam || !GenerationParam ||
            SerialParam->GetOffset_ForInternal() == GenerationParam->GetOffset_ForInternal()) { return false; }
        C.I32(TEXT("InSerial"), Serial); C.I32(TEXT("InGeneration"), Generation); return C.Send();
    }
    auto Rows(APlayerController* PC) -> TArray<FCkGym_ControlRow>
    {
        FCall C(PC, TEXT("Get_ControlRows"), FUNC_BlueprintPure);
        const auto* Return = P<FArrayProperty>(C.Function, TEXT("ReturnValue"));
        const auto* Inner = Return ? CastField<FStructProperty>(Return->Inner) : nullptr;
        return Inner && Inner->Struct == FCkGym_ControlRow::StaticStruct() && C.Send()
            ? *Return->ContainerPtrToValuePtr<TArray<FCkGym_ControlRow>>(C.Args->GetStructMemory()) : TArray<FCkGym_ControlRow>{};
    }
    auto TimerCount(UWorld* W) -> int32
    {
        const auto H = Root(W); if (!Live(H)) { return INDEX_NONE; }
        int32 Count = 0; const auto Registry = H.Get_RegistryView();
        Registry.View<ck::FFragment_Timer>().ForEach([&Count](FCk_Entity, const ck::FFragment_Timer&) { ++Count; });
        return Count;
    }
    auto TickerCensus(UWorld* W, const FFixture& F, int32 Expected, int32 ExpectedStations = 8) -> bool
    {
        const auto H = Root(W); if (!Live(H)) { return false; }
        int32 Count = 0, StationCount = 0; const auto Registry = H.Get_RegistryView();
        Registry.View<ck::FFragment_EntityScript>().ForEach([&](FCk_Entity, const ck::FFragment_EntityScript& Fragment)
        { const auto Script = Fragment.Get_Script(); if (!Script.IsValid()) { return; }
            if (Script->GetClass() == F.TickerClass) { ++Count; }
            if (Script->GetClass() == F.StationClass) { ++StationCount; } });
        return Count == Expected && StationCount == ExpectedStations;
    }
    auto Present(const FFixture& F, UWorld* W, APlayerController* PC, bool Authority) -> bool
    {
        const auto* D = F.Data(W);
        if (!Complete(F, W, D) || !IsValid(PC) || !IsValid(PC->GetHUD()) || PC->GetHUD()->GetOwner() != PC ||
            !PureBool(PC, TEXT("Get_ShowcasePresentationReady"))) { F.LastCohortDiagnostic = TEXT("Present prerequisite 1"); return false; }
        const auto Local = Handles(PC, TEXT("Get_ShowcaseLocalStations"));
        if (Authority ? !Local.IsEmpty() : Local.Num() != 8) { F.LastCohortDiagnostic = TEXT("Present prerequisite 2"); return false; }
        const auto* RecipeArray = P<FArrayProperty>(F.SnapshotType, TEXT("Stations"));
        const auto* Recipe = RecipeArray ? CastField<FStructProperty>(RecipeArray->Inner) : nullptr;
        if (!Recipe || !IsValid(Recipe->Struct)) { F.LastCohortDiagnostic = TEXT("Present prerequisite 3"); return false; }
        FScriptArrayHelper Recipes(RecipeArray, RecipeArray->ContainerPtrToValuePtr<void>(D));
        if (Recipes.Num() != 8) { F.LastCohortDiagnostic = TEXT("Present prerequisite 4"); return false; }
        const auto Shown = PureInt(PC, TEXT("Get_ShowcaseShownSequence"));
        const int32 WorldIndex = W == F.Server.Get() ? 0 : W == F.Local[0]->GetWorld() ? 1 : W == F.Local[1]->GetWorld() ? 2 : INDEX_NONE;
        if (WorldIndex == INDEX_NONE) { F.LastCohortDiagnostic = TEXT("Present prerequisite 5"); return false; }
        const auto* Saved = F.Samples[WorldIndex].Find(FFixture::Key(Int(F.SnapshotType, D, TEXT("Generation")), Shown));
        if (!Saved || !Saved->IsValid() || !Complete(F, W, (*Saved)->GetStructMemory())) { F.LastCohortDiagnostic = TEXT("Present prerequisite 6"); return false; }
        const void* ShownData = (*Saved)->GetStructMemory();
        for (int32 I = 0; I < 8; ++I)
        {
            const auto H = Tagged(W, I);
            if (H.Num() != 1 || !Live(H[0]) || (!Authority && Local[I] != H[0])) { F.LastCohortDiagnostic = TEXT("Present prerequisite 7"); return false; }
            const auto Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(H[0]);
            if (!IsValid(Script) || Script->GetClass() != F.StationClass || Script->GetWorld() != W ||
                !UCk_Utils_Transform_UE::Has(H[0])) { F.LastCohortDiagnostic = TEXT("Present prerequisite 8"); return false; }
            const auto* X = P<FStructProperty>(Recipe->Struct, TEXT("InitialTransform"));
            if (!X || X->Struct != TBaseStructure<FTransform>::Get()) { F.LastCohortDiagnostic = TEXT("Present prerequisite 9"); return false; }
            const auto& ExpectedX = *X->ContainerPtrToValuePtr<FTransform>(Recipes.GetRawPtr(I));
            if (!ExpectedX.Equals(FTransform(FRotator::ZeroRotator, FVector(-1000, -7500 + I * 1500, 0)), 0.0) ||
                !UCk_Utils_Transform_UE::Get_EntityCurrentTransform(UCk_Utils_Transform_UE::Cast(H[0])).Equals(ExpectedX, 1.e-9)) { F.LastCohortDiagnostic = TEXT("Present prerequisite 10"); return false; }
            const bool Auto = Bool(Recipe->Struct, Recipes.GetRawPtr(I), TEXT("AutoSize"));
            for (auto Name : {TEXT("Width"), TEXT("Depth"), TEXT("Height")})
            {
                const auto* Dimension = P<FDoubleProperty>(Script->GetClass(), Name);
                if (!Dimension || !FMath::IsFinite(Dimension->GetPropertyValue_InContainer(Script)) ||
                    Dimension->GetPropertyValue_InContainer(Script) <= 0.0) { F.LastCohortDiagnostic = TEXT("Present prerequisite 11"); return false; }
            }
            for (TFieldIterator<FProperty> It(Recipe->Struct); It; ++It)
            {
                const auto* Actual = FindFProperty<FProperty>(Script->GetClass(), It->GetFName());
                if (!Actual || !Actual->SameType(*It)) { F.LastCohortDiagnostic = TEXT("Present prerequisite 12"); return false; }
                // Auto-size Width/Height deliberately mutate from authored seeds; all other full tuners remain exact.
                if (Auto && (It->GetFName() == TEXT("Width") || It->GetFName() == TEXT("Height"))) { continue; }
                if (!Actual->Identical(Actual->ContainerPtrToValuePtr<void>(Script), It->ContainerPtrToValuePtr<void>(Recipes.GetRawPtr(I)), PPF_None)) { F.LastCohortDiagnostic = TEXT("Present prerequisite 13"); return false; }
            }
            if (!UCk_Utils_DynamicFragment_UE::Has_Fragment(H[0], F.Anchors) ||
                !UCk_Utils_DynamicFragment_UE::Has_Fragment(H[0], F.Display)) { F.LastCohortDiagnostic = TEXT("Present prerequisite 14"); return false; }
            const auto* Anchor = UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(H[0], F.Anchors);
            const auto* Text = UCk_Utils_DynamicFragment_UE::TryGet_Fragment_TypeUnsafe(H[0], F.Display);
            const auto* Footprint = P<FStructProperty>(F.Anchors, TEXT("FootprintCenter"));
            if (!Anchor || !Text || !Footprint || Footprint->Struct != TBaseStructure<FVector>::Get() ||
                !Footprint->ContainerPtrToValuePtr<FVector>(Anchor->GetMemory())->Equals(ExpectedX.GetLocation(), 1.e-9)) { F.LastCohortDiagnostic = TEXT("Present prerequisite 15"); return false; }
            for (auto Name : {TEXT("AgentSpawnFront"), TEXT("AgentSpawnBack"), TEXT("AgentSpawnLeft"), TEXT("AgentSpawnRight"), TEXT("PanelTopFront"), TEXT("PanelCenter")})
            {
                const auto* Field = P<FStructProperty>(F.Anchors, Name);
                if (!Field || Field->Struct != TBaseStructure<FVector>::Get() ||
                    Field->ContainerPtrToValuePtr<FVector>(Anchor->GetMemory())->ContainsNaN()) { F.LastCohortDiagnostic = TEXT("Present prerequisite 16"); return false; }
            }
            {
                for (const auto& Pair : {TPair<const TCHAR*, const TCHAR*>(TEXT("Title"), TEXT("StationTitles")),
                    {TEXT("Description"), TEXT("StationBodies")}, {TEXT("Instructions"), TEXT("StationInstructions")}})
                {
                    const auto* Values = P<FArrayProperty>(F.SnapshotType, Pair.Value);
                    const auto* Value = Values ? CastField<FStrProperty>(Values->Inner) : nullptr;
                    const auto* Field = P<FTextProperty>(F.Display, Pair.Key);
                    if (!Value || !Field) { F.LastCohortDiagnostic = TEXT("Present prerequisite 17"); return false; }
                    FScriptArrayHelper A(Values, Values->ContainerPtrToValuePtr<void>(ShownData));
                    if (A.Num() != 8 || Field->GetPropertyValue_InContainer(Text->GetMemory()).ToString() != Value->GetPropertyValue(A.GetRawPtr(I))) { F.LastCohortDiagnostic = TEXT("Present prerequisite 18"); return false; }
                }
            }
        }
        return true;
    }
    // Each client must match a real, complete authority sample at its own exact epoch.
    // Histories are bounded at capture; neither owner can pass on a local-only value.
    auto SharedSnapshotEqual(const FFixture& F, bool RequireNineLines = false) -> bool
    {
        if (!F.Context() || !IsValid(F.SnapshotType)) { return false; }
        const int32 Generation = Int(F.SnapshotType, F.Data(F.Server.Get()), TEXT("Generation"));
        if (Generation <= 0) { return false; }
        for (int32 I = 0; I < 2; ++I)
        { if (Int(F.SnapshotType, F.Data(F.Local[I]->GetWorld()), TEXT("Generation")) != Generation) { return false; } }
        for (int32 Owner = 0; Owner < 2; ++Owner)
        {
            bool Matched = false;
            UWorld* ClientWorld = F.Local[Owner]->GetWorld();
            for (int32 N = F.SampleOrder[0].Num() - 1; N >= 0; --N)
            {
                const uint64 K = F.SampleOrder[0][N];
                if (int32(K >> 32) != Generation) { continue; }
                const auto* Authority = F.Samples[0].Find(K);
                const auto* Client = F.Samples[Owner + 1].Find(K);
                if (!Authority || !Client || !Authority->IsValid() || !Client->IsValid()) { continue; }
                const void* A = (*Authority)->GetStructMemory();
                const void* B = (*Client)->GetStructMemory();
                if (RequireNineLines && (Int(F.SnapshotType, A, TEXT("RevealedLines")) != 9 ||
                    Int(F.SnapshotType, A, TEXT("TickCount")) <= 9 ||
                    Int(F.SnapshotType, B, TEXT("RevealedLines")) != 9 ||
                    Int(F.SnapshotType, B, TEXT("TickCount")) <= 9)) { continue; }
                if (Complete(F, F.Server.Get(), A) && Complete(F, ClientWorld, B) &&
                    F.SnapshotType->CompareScriptStruct(A, B, PPF_None))
                { Matched = true; break; }
            }
            if (!Matched) { return false; }
        }
        return true;
    }
    struct FVisiblePair
    {
        FCk_Handle_SceneNode Node;
        FCk_Handle_UnrealComponent Handle;
        USceneComponent* Component = nullptr;
    };
    constexpr const TCHAR* VisibleNames[10] = {TEXT("BackWall"), TEXT("FloorStage"), TEXT("LeftTrim"),
        TEXT("RightTrim"), TEXT("TopTrim"), TEXT("BottomTrim"), TEXT("TitleText"),
        TEXT("DescriptionText"), TEXT("FloorDescriptionText"), TEXT("Spotlight")};
    auto VisibleRole(int32 I) -> ECkGym_StationVisibleRole
    { return static_cast<ECkGym_StationVisibleRole>(I); }
    auto CaptureWholeAllocated(const FCk_Handle& Station, TSet<FCk_Handle>& Out) -> bool
    {
        Out.Reset();
        TArray<FCk_Handle> Pending{Station};
        while (!Pending.IsEmpty())
        {
            const auto H = Pending.Pop(EAllowShrinking::No);
            if (!UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(H) ||
                Out.Num() >= 4096) { return false; }
            if (Out.Contains(H)) { continue; }
            Out.Add(H);
            if (H.Has<ck::FFragment_LifetimeDependents>())
            {
                for (const auto& Child : H.Get<ck::FFragment_LifetimeDependents>().Get_Entities())
                { if (UCk_EntityScriptGym_Spawn_Runtime_UE::Get_IsEntityAllocated(Child)) { Pending.Add(Child); } }
            }
        }
        return Out.Contains(Station);
    }
    auto FindVisiblePairs(const FCk_Handle& Station, FVisiblePair (&Pairs)[10]) -> bool
    {
        if (!Live(Station)) { return false; }
        TSet<FCk_Handle> WholeStation;
        Capture(Station, WholeStation);
        bool Found[10]{};
        for (const auto& H : WholeStation)
        {
            if (!Live(H) || !H.Has<ck::FFragment_UnrealComponent_Params>()) { continue; }
            const FName Label = H.Get<ck::FFragment_UnrealComponent_Params>().Get_DebugName();
            int32 Role = INDEX_NONE;
            for (int32 I = 0; I < 10; ++I) { if (Label == FName(VisibleNames[I])) { Role = I; break; } }
            if (Role == INDEX_NONE) { continue; }
            if (Found[Role] || !H.Has<ck::FFragment_UnrealComponent>() ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H)) { return false; }
            const auto Node = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H);
            if (!Live(Node) || !WholeStation.Contains(Node) || !UCk_Utils_SceneNode_UE::Has(Node) ||
                !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Node) ||
                UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Node) != Station ||
                H.Get<ck::FFragment_UnrealComponent>().Get_OwningEntity() != Node) { return false; }
            const auto Typed = UCk_Utils_UnrealComponent_UE::Cast(H);
            if (!Live(Typed)) { return false; }
            auto* Actual = Cast<USceneComponent>(UCk_Utils_UnrealComponent_UE::Get_Component(Typed));
            if (!IsValid(Actual) || !Actual->IsRegistered() ||
                UCk_Utils_UnrealComponent_UE::TryGet_OwningHandle_FromComponent(Actual) != Typed) { return false; }
            Pairs[Role] = {UCk_Utils_SceneNode_UE::Cast(Node), Typed, Actual};
            Found[Role] = true;
        }
        for (bool B : Found) { if (!B) { return false; } }
        return true;
    }
    auto VisibleEqualsActual(const FVisiblePair& Pair, const FCkGym_StationVisiblePiece& Piece, int32 Role) -> bool
    {
        if (Piece.Role != VisibleRole(Role) || !IsValid(Pair.Component) ||
            !Piece.ComponentWorldTransform.Equals(Pair.Component->GetComponentTransform(), 0.0) ||
            Piece.Visible != Pair.Component->IsVisible() ||
            Piece.HiddenInGame != Pair.Component->bHiddenInGame) { return false; }
        if (Role < 6)
        {
            auto* Mesh = Cast<UStaticMeshComponent>(Pair.Component);
            auto* MID = Mesh ? Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)) : nullptr;
            auto* Parent = MID ? MID->Parent.Get() : nullptr;
            FLinearColor Color;
            return Mesh && IsValid(Mesh->GetStaticMesh()) && IsValid(MID) &&
                IsValid(Parent) && IsValid(Parent->GetMaterial()) &&
                MID->GetVectorParameterValue(FHashedMaterialParameterInfo{FName(TEXT("Color"))}, Color) &&
                Piece.MeshAssetPath == Mesh->GetStaticMesh()->GetPathName() &&
                Piece.ParentMaterialPath == Parent->GetPathName() &&
                Piece.BaseMaterialPath == Parent->GetMaterial()->GetPathName() &&
                Piece.MeshColor == Color && Piece.CollisionEnabled == int32(Mesh->GetCollisionEnabled());
        }
        if (Role < 9)
        {
            auto* Text = Cast<UTextRenderComponent>(Pair.Component);
            if (!Text) { return false; }
            const FVector Bounds = Text->IsVisible() && !Text->bHiddenInGame && !Text->Text.IsEmpty()
                ? Text->GetTextLocalSize() : FVector::ZeroVector;
            return Piece.Text == Text->Text.ToString() &&
                Piece.FontPath == (IsValid(Text->Font.Get()) ? Text->Font->GetPathName() : FString{}) &&
                Piece.TextMaterialPath == (IsValid(Text->TextMaterial.Get()) ? Text->TextMaterial->GetPathName() : FString{}) &&
                Piece.ShownTextLocalSize == Bounds && Piece.TextWorldSize == Text->WorldSize &&
                Piece.TextXScale == Text->XScale && Piece.TextYScale == Text->YScale &&
                Piece.TextHorizontalSpacing == Text->HorizSpacingAdjust &&
                Piece.TextVerticalSpacing == Text->VertSpacingAdjust &&
                Piece.TextColor == Text->TextRenderColor &&
                Piece.HorizontalAlignment == int32(Text->HorizontalAlignment.GetValue()) &&
                Piece.VerticalAlignment == int32(Text->VerticalAlignment.GetValue());
        }
        auto* Spot = Cast<USpotLightComponent>(Pair.Component);
        return Spot && Piece.SpotIntensity == Spot->Intensity &&
            Piece.SpotAttenuationRadius == Spot->AttenuationRadius &&
            Piece.SpotInnerConeAngle == Spot->InnerConeAngle && Piece.SpotOuterConeAngle == Spot->OuterConeAngle &&
            Piece.SpotColor == Spot->LightColor && Piece.SpotIntensityUnits == int32(Spot->GetLightUnits());
    }
    auto VisibleASGeometry(const FFixture& F, const FCk_Handle& Station, UWorld* W,
                           const FVisiblePair (&Pairs)[10]) -> bool
    {
        auto* Script = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Station);
        FCall Call(Script, TEXT("TryGet_VisibleGeometry"), FUNC_BlueprintPure);
        auto* Out = P<FStructProperty>(Call.Function, TEXT("Out"));
        auto* Return = P<FBoolProperty>(Call.Function, TEXT("ReturnValue"));
        auto* Auto = P<FBoolProperty>(F.VisibleGeometry, TEXT("AutoSize"));
        auto* Width = P<FDoubleProperty>(F.VisibleGeometry, TEXT("Width"));
        auto* Depth = P<FDoubleProperty>(F.VisibleGeometry, TEXT("Depth"));
        auto* Height = P<FDoubleProperty>(F.VisibleGeometry, TEXT("Height"));
        auto* Xform = P<FStructProperty>(F.VisibleGeometry, TEXT("StationWorldTransform"));
        auto* PieceArray = P<FArrayProperty>(F.VisibleGeometry, TEXT("Pieces"));
        auto* AnchorArray = P<FArrayProperty>(F.VisibleGeometry, TEXT("Anchors"));
        if (!IsValid(Script) || Script->GetClass() != F.StationClass || Script->GetWorld() != W ||
            !IsValid(F.VisibleGeometry) || !Call.Valid || Call.Function->NumParms != 2 ||
            !Out || Out->Struct != F.VisibleGeometry || !Out->HasAnyPropertyFlags(CPF_OutParm) ||
            Out->HasAnyPropertyFlags(CPF_ReturnParm) || !Return ||
            !Return->HasAnyPropertyFlags(CPF_ReturnParm) ||
            !Auto || !Width || !Depth || !Height || !Xform ||
            Xform->Struct != TBaseStructure<FTransform>::Get() ||
            !PieceArray || !AnchorArray) { return false; }
        const auto* PieceInner = CastField<FStructProperty>(PieceArray->Inner);
        const auto* AnchorInner = CastField<FStructProperty>(AnchorArray->Inner);
        if (!PieceInner || PieceInner->Struct != FCkGym_StationVisiblePiece::StaticStruct() ||
            !AnchorInner || AnchorInner->Struct != TBaseStructure<FTransform>::Get() ||
            !Call.Send() || !Call.B()) { return false; }
        const void* D = Out->ContainerPtrToValuePtr<void>(Call.Args->GetStructMemory());
        const auto* ScriptWidth = P<FDoubleProperty>(Script->GetClass(), TEXT("Width"));
        const auto* ScriptDepth = P<FDoubleProperty>(Script->GetClass(), TEXT("Depth"));
        const auto* ScriptHeight = P<FDoubleProperty>(Script->GetClass(), TEXT("Height"));
        const auto* Offset = P<FDoubleProperty>(Script->GetClass(), TEXT("AgentSpawnOffset"));
        const auto* ScriptAuto = P<FBoolProperty>(Script->GetClass(), TEXT("AutoSize"));
        if (!D || !ScriptWidth || !ScriptDepth || !ScriptHeight || !Offset || !ScriptAuto ||
            !UCk_Utils_Transform_UE::Has(Station) ||
            Auto->GetPropertyValue_InContainer(D) != ScriptAuto->GetPropertyValue_InContainer(Script) ||
            Width->GetPropertyValue_InContainer(D) != ScriptWidth->GetPropertyValue_InContainer(Script) ||
            Depth->GetPropertyValue_InContainer(D) != ScriptDepth->GetPropertyValue_InContainer(Script) ||
            Height->GetPropertyValue_InContainer(D) != ScriptHeight->GetPropertyValue_InContainer(Script))
        { return false; }
        const auto StationX = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
            UCk_Utils_Transform_UE::Cast(Station));
        if (!Xform->ContainerPtrToValuePtr<FTransform>(D)->Equals(StationX, 0.0)) { return false; }
        FScriptArrayHelper Pieces(PieceArray, PieceArray->ContainerPtrToValuePtr<void>(D));
        FScriptArrayHelper Anchors(AnchorArray, AnchorArray->ContainerPtrToValuePtr<void>(D));
        if (Pieces.Num() != 10 || Anchors.Num() != 7) { return false; }
        for (int32 I = 0; I < 10; ++I)
        {
            const auto& FromScript = *reinterpret_cast<const FCkGym_StationVisiblePiece*>(Pieces.GetRawPtr(I));
            FCkGym_StationVisiblePiece FromNative;
            if (!UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(
                    Station, Pairs[I].Node, Pairs[I].Handle, VisibleRole(I), W, FromNative) ||
                !FCkGym_StationVisiblePiece::StaticStruct()->CompareScriptStruct(
                    &FromScript, &FromNative, PPF_None) ||
                !VisibleEqualsActual(Pairs[I], FromScript, I) ||
                !FromScript.NodeWorldTransform.Equals(
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                        UCk_Utils_Transform_UE::Cast(Pairs[I].Node)), 0.0)) { return false; }
        }
        TArray<FCk_Handle> Direct;
        if (!UCk_EntityScriptGym_Spawn_Runtime_UE::TryGet_AllocatedDirectLifetimeDependents(
                Station, Root(W), Direct)) { return false; }
        TArray<FCk_Handle> AnchorNodes;
        for (const auto& Child : Direct)
        {
            if (!Live(Child) || !UCk_Utils_SceneNode_UE::Has(Child)) { continue; }
            bool IsPiece = false;
            for (const auto& Pair : Pairs) { IsPiece = IsPiece || Child == Pair.Node; }
            if (!IsPiece) { AnchorNodes.Add(Child); }
        }
        if (AnchorNodes.Num() != 7) { return false; }
        TSet<FCk_Handle> Matched;
        const double Wcm = Width->GetPropertyValue_InContainer(D) * 100.0;
        const double Dcm = Depth->GetPropertyValue_InContainer(D) * 100.0;
        const double Hcm = Height->GetPropertyValue_InContainer(D) * 100.0;
        const double SpawnOffset = Offset->GetPropertyValue_InContainer(Script);
        for (int32 I = 0; I < 7; ++I)
        {
            const auto& A = *reinterpret_cast<const FTransform*>(Anchors.GetRawPtr(I));
            FVector Local = FVector::ZeroVector;
            switch (I)
            {
                case 1: Local.X = Dcm * 0.5 + SpawnOffset; break;
                case 2: Local.X = -Dcm * 0.5 - SpawnOffset; break;
                case 3: Local.Y = Wcm * 0.5 + SpawnOffset; break;
                case 4: Local.Y = -Wcm * 0.5 - SpawnOffset; break;
                case 5: Local = FVector(Dcm * 0.5, 0, Hcm); break;
                case 6: Local.Z = Hcm * 0.5; break;
                default: break;
            }
            if (!A.GetLocation().Equals(StationX.TransformPosition(Local), 1.e-8)) { return false; }
            int32 ThisMatches = 0;
            for (const auto& Node : AnchorNodes)
            {
                if (!UCk_Utils_Transform_UE::Has(Node)) { return false; }
                const auto Actual = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                    UCk_Utils_Transform_UE::Cast(Node));
                if (A.Equals(Actual, 0.0))
                {
                    if (Matched.Contains(Node)) { return false; }
                    Matched.Add(Node); ++ThisMatches;
                }
            }
            if (ThisMatches != 1) { return false; }
        }
        return Matched.Num() == 7;
    }
    auto VisibleStation(const FFixture& F, const FCk_Handle& Station, UWorld* W, int32 Slot) -> bool
    {
        FVisiblePair Pairs[10];
        if (!FindVisiblePairs(Station, Pairs)) { return false; }
        for (int32 I = 0; I < 10; ++I)
        {
            FCkGym_StationVisiblePiece Piece;
            if (!UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(
                    Station, Pairs[I].Node, Pairs[I].Handle, VisibleRole(I), W, Piece) ||
                !VisibleEqualsActual(Pairs[I], Piece, I) ||
                (I == 8 && Piece.Visible != (Slot == 3)) ||
                (I == 9 && Piece.Visible == (Slot == 6)) ||
                (I == 8 && !Piece.Visible && Piece.ShownTextLocalSize != FVector::ZeroVector) ||
                !Piece.NodeWorldTransform.Equals(
                    UCk_Utils_Transform_UE::Get_EntityCurrentTransform(
                        UCk_Utils_Transform_UE::Cast(Pairs[I].Node)), 0.0)) { return false; }
        }
        return VisibleASGeometry(F, Station, W, Pairs);
    }
    auto VisibleRejectsWithoutMutation(const FFixture& F, UWorld* W) -> bool
    {
        const auto Station = Tagged(W, 0);
        if (Station.Num() != 1 || !Live(Station[0]) || !F.Data(W)) { return false; }
        FVisiblePair Pairs[10];
        if (!FindVisiblePairs(Station[0], Pairs)) { return false; }
        FCkGym_StationVisiblePiece Pieces[10];
        for (int32 I = 0; I < 10; ++I)
        {
            if (!UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(
                    Station[0], Pairs[I].Node, Pairs[I].Handle, VisibleRole(I), W, Pieces[I]) ||
                !VisibleEqualsActual(Pairs[I], Pieces[I], I)) { return false; }
        }
        TSet<FCk_Handle> Before, After;
        if (!CaptureWholeAllocated(Station[0], Before)) { return false; }
        const auto Snapshot = CopyStruct(F.SnapshotType, F.Data(W));
        if (!Snapshot.IsValid()) { return false; }
        UWorld* WrongWorld = W == F.Server.Get() ? F.Local[0]->GetWorld() : F.Server.Get();
        auto Rejected = [&](const FCk_Handle& S, const FCk_Handle_SceneNode& N,
                            const FCk_Handle_UnrealComponent& C, ECkGym_StationVisibleRole R, UWorld* World)
        {
            FCkGym_StationVisiblePiece Out;
            Out.Visible = true;
            if (UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(S, N, C, R, World, Out)) { return false; }
            const FCkGym_StationVisiblePiece Empty;
            if (!FCkGym_StationVisiblePiece::StaticStruct()->CompareScriptStruct(&Out, &Empty, PPF_None)) { return false; }
            if (!CaptureWholeAllocated(Station[0], After)) { return false; }
            bool Same = Before.Num() == After.Num() &&
                F.SnapshotType->CompareScriptStruct(Snapshot->GetStructMemory(), F.Data(W), PPF_None);
            for (const auto& H : Before) { Same = Same && After.Contains(H); }
            for (int32 I = 0; I < 10; ++I)
            {
                FCkGym_StationVisiblePiece Current;
                Same = Same && UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(
                    Station[0], Pairs[I].Node, Pairs[I].Handle, VisibleRole(I), W, Current) &&
                    FCkGym_StationVisiblePiece::StaticStruct()->CompareScriptStruct(&Pieces[I], &Current, PPF_None) &&
                    VisibleEqualsActual(Pairs[I], Current, I);
            }
            After.Reset();
            return Same;
        };
        if (!(Rejected({}, Pairs[0].Node, Pairs[0].Handle, VisibleRole(0), W) &&
            Rejected(Station[0], {}, Pairs[0].Handle, VisibleRole(0), W) &&
            Rejected(Station[0], Pairs[0].Node, {}, VisibleRole(0), W) &&
            Rejected(Station[0], Pairs[0].Node, Pairs[0].Handle, VisibleRole(0), WrongWorld) &&
            Rejected(Station[0], Pairs[0].Node, Pairs[0].Handle, VisibleRole(1), W) &&
            Rejected(Station[0], Pairs[0].Node, Pairs[1].Handle, VisibleRole(0), W) &&
            Rejected(Station[0], Pairs[1].Node, Pairs[0].Handle, VisibleRole(0), W) &&
            Rejected(Station[0], Pairs[0].Node, Pairs[0].Handle,
                static_cast<ECkGym_StationVisibleRole>(255), W))) { return false; }

        // The real station script's total getter must also reject its default object
        // without exposing any partly built geometry or affecting the ready world.
        auto* CDO = F.StationClass ? F.StationClass->GetDefaultObject(false) : nullptr;
        FCall DefaultCall(CDO, TEXT("TryGet_VisibleGeometry"), FUNC_BlueprintPure);
        auto* Out = P<FStructProperty>(DefaultCall.Function, TEXT("Out"));
        auto* OutWidth = P<FDoubleProperty>(F.VisibleGeometry, TEXT("Width"));
        auto* OutPieces = P<FArrayProperty>(F.VisibleGeometry, TEXT("Pieces"));
        auto* OutAnchors = P<FArrayProperty>(F.VisibleGeometry, TEXT("Anchors"));
        if (!IsValid(CDO) || !DefaultCall.Valid || !Out ||
            Out->Struct != F.VisibleGeometry || !Out->HasAnyPropertyFlags(CPF_OutParm) ||
            !OutWidth || !OutPieces || !OutAnchors) { return false; }
        void* Data = Out->ContainerPtrToValuePtr<void>(DefaultCall.Args->GetStructMemory());
        OutWidth->SetPropertyValue_InContainer(Data, -123.0);
        FScriptArrayHelper(OutPieces, OutPieces->ContainerPtrToValuePtr<void>(Data)).AddValue();
        FScriptArrayHelper(OutAnchors, OutAnchors->ContainerPtrToValuePtr<void>(Data)).AddValue();
        if (!DefaultCall.Send() || DefaultCall.B()) { return false; }
        FStructOnScope Empty(F.VisibleGeometry);
        if (!F.VisibleGeometry->CompareScriptStruct(Data, Empty.GetStructMemory(), PPF_None) ||
            !CaptureWholeAllocated(Station[0], After) || After.Num() != Before.Num() ||
            !F.SnapshotType->CompareScriptStruct(Snapshot->GetStructMemory(), F.Data(W), PPF_None))
        { return false; }
        for (const auto& H : Before) { if (!After.Contains(H)) { return false; } }
        for (int32 I = 0; I < 10; ++I)
        {
            FCkGym_StationVisiblePiece Current;
            if (!UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(
                    Station[0], Pairs[I].Node, Pairs[I].Handle, VisibleRole(I), W, Current) ||
                !FCkGym_StationVisiblePiece::StaticStruct()->CompareScriptStruct(
                    &Pieces[I], &Current, PPF_None) ||
                !VisibleEqualsActual(Pairs[I], Current, I)) { return false; }
        }
        return true;
    }
    auto Cohort(const FFixture& F) -> bool
    {
        UWorld* W = F.Server.Get(); auto* S = F.State(W);
        if (!S || !Complete(F, W, F.Data(W)) || !PureBool(S, TEXT("Get_IsReady")) || !TickerCensus(W, F, 1) || TimerCount(W) != 9) { F.LastCohortDiagnostic = TEXT("Cohort prerequisite 1"); return false; }
        const auto Stations = Handles(S, TEXT("ServerStations"), true);
        const auto Ticker = Handle(S, TEXT("ServerTicker"), true);
        const auto Clock = Handle(S, TEXT("Get_ShowcaseClock"));
        if (Stations.Num() != 8 || !Live(Ticker) || !Live(Clock) || !UCk_Utils_Timer_UE::Has(Clock) ||
            !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Clock) || UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Clock) != Ticker) { F.LastCohortDiagnostic = TEXT("Cohort prerequisite 2"); return false; }
        FCk_Handle Owner;
        for (int32 I = 0; I < 8; ++I)
        {
            const auto H = Tagged(W, I);
            if (H.Num() != 1 || H[0] != Stations[I] || !Live(H[0]) || !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H[0])) { F.LastCohortDiagnostic = TEXT("Cohort prerequisite 3"); return false; }
            const auto ThisOwner = UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H[0]);
            if (I == 0) { Owner = ThisOwner; }
            if (!Live(Owner) || ThisOwner != Owner) { F.LastCohortDiagnostic = TEXT("Cohort prerequisite 4"); return false; }
        }
        if (!UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(Ticker) || UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(Ticker) != Owner) { F.LastCohortDiagnostic = TEXT("Cohort prerequisite 5"); return false; }
        for (int32 I = 0; I < 2; ++I)
        {
            if (!TickerCensus(F.Local[I]->GetWorld(), F, 0) || TimerCount(F.Local[I]->GetWorld()) != 8)
            { F.LastCohortDiagnostic = FString::Printf(TEXT("Cohort client %d census/timers (timers=%d)"), I, TimerCount(F.Local[I]->GetWorld())); return false; }
            if (!Present(F, F.Local[I]->GetWorld(), F.Local[I].Get(), false))
            { F.LastCohortDiagnostic = FString::Printf(TEXT("client %d: "), I) + F.LastCohortDiagnostic; return false; }
        }
        if (!SharedSnapshotEqual(F)) { F.LastCohortDiagnostic = TEXT("shared complete exact epoch equality"); return false; }
        if (!F.Dedicated && !Present(F, W, W->GetFirstPlayerController(), true))
        { F.LastCohortDiagnostic = TEXT("listen host: ") + F.LastCohortDiagnostic; return false; }
        F.LastCohortDiagnostic.Empty(); return true;
    }
    auto SaveCohorts(const FFixture& F, TSet<FCk_Handle>* Out) -> bool
    {
        for (int32 World = 0; World < 3; ++World)
        {
            UWorld* W = World == 0 ? F.Server.Get() : F.Local[World - 1]->GetWorld();
            const auto H = Tagged(W, 0);
            if (H.Num() != 1 || !Live(H[0]) || !UCk_Utils_EntityLifetime_UE::Has_LifetimeOwner(H[0])) { return false; }
            Capture(UCk_Utils_EntityLifetime_UE::Get_LifetimeOwner(H[0]), Out[World]);
            if (Out[World].Num() < 9) { return false; }
        }
        return true;
    }
    auto Consumed(const FFixture& F, int32 Owner, int32 Request) -> bool
    {
        const auto C = Installed(F, F.Local[Owner].Get()); const auto* Authority = F.OwnerCommit(F.Remote[Owner].Get());
        if (!C.IsValid() || !Authority || !F.SawInstalled[Owner]) { return false; }
        const void* D = C->GetStructMemory(); const auto* PC = F.Remote[Owner].Get();
        return Int(F.CommitType, D, TEXT("RequestSerial")) == Request &&
            Int(PC->GetClass(), PC, TEXT("LastControlSerial")) == Request &&
            Int(PC->GetClass(), PC, TEXT("LastControlResult")) == 1 &&
            Int(PC->GetClass(), PC, TEXT("LastAcknowledgedDeliverySerial")) == Int(F.CommitType, D, TEXT("DeliverySerial")) &&
            Int(F.CommitType, D, TEXT("DeliverySerial")) == Int(F.CommitType, Authority, TEXT("DeliverySerial")) &&
            Object(F.CommitType, D, TEXT("Owner")) == F.Local[Owner].Get() &&
            Object(F.CommitType, D, TEXT("OwnerPawn")) == F.Local[Owner]->GetPawn() &&
            Object(F.CommitType, D, TEXT("OriginState")) == F.State(F.Local[Owner]->GetWorld()) &&
            Complete(F, F.Local[Owner]->GetWorld(), F.CommitSample->ContainerPtrToValuePtr<void>(D));
    }
    auto SharedEpochObservation(const FFixture& F) -> FString
    {
        if (!F.Context() || !IsValid(F.SnapshotType)) { return TEXT(" [epochDiagnostic=context/schema unavailable]"); }
        const int32 G = Int(F.SnapshotType, F.Data(F.Server.Get()), TEXT("Generation"));
        int32 Pairs[2]{}, Triple = 0; FString Out;
        const auto Describe = [](const UStruct* Type, const void* A, const void* B, const FString& Prefix, FString& Text, int32& Count)
        {
            if (!IsValid(Type) || !A || !B) { return; }
            for (TFieldIterator<FProperty> It(Type); It; ++It)
            {
                const auto* Field = *It;
                if (!Field || Field->Identical_InContainer(A, B)) { continue; }
                ++Count;
                if (Count > 16) { continue; }
                FString AT, BT;
                Field->ExportTextItem_Direct(AT, Field->ContainerPtrToValuePtr<void>(A), nullptr, nullptr, PPF_None);
                Field->ExportTextItem_Direct(BT, Field->ContainerPtrToValuePtr<void>(B), nullptr, nullptr, PPF_None);
                AT.ReplaceInline(TEXT("\n"), TEXT(" ")); BT.ReplaceInline(TEXT("\n"), TEXT(" "));
                Text += FString::Printf(TEXT(" [%s%s authority='%s' client='%s']"), *Prefix, *Field->GetName(), *AT.Left(256), *BT.Left(256));
                const auto* Struct = CastField<FStructProperty>(Field);
                if (Struct && Struct->Struct == TBaseStructure<FTransform>::Get())
                {
                    const auto& AX = *Field->ContainerPtrToValuePtr<FTransform>(A);
                    const auto& BX = *Field->ContainerPtrToValuePtr<FTransform>(B);
                    const auto AQ = AX.GetRotation(), BQ = BX.GetRotation();
                    Text += FString::Printf(TEXT(" [transformDelta loc=%.17g scale=%.17g authorityQ=%.17g,%.17g,%.17g,%.17g clientQ=%.17g,%.17g,%.17g,%.17g]"),
                        FVector::Dist(AX.GetLocation(), BX.GetLocation()), FVector::Dist(AX.GetScale3D(), BX.GetScale3D()),
                        AQ.X, AQ.Y, AQ.Z, AQ.W, BQ.X, BQ.Y, BQ.Z, BQ.W);
                }
            }
        };
        for (int32 Owner = 0; Owner < 2; ++Owner)
        {
            uint64 Chosen = 0;
            for (auto K : F.SampleOrder[0])
            {
                if (int32(K >> 32) != G) { continue; }
                const auto* A = F.Samples[0].Find(K); const auto* B = F.Samples[Owner + 1].Find(K);
                if (!A || !B || !A->IsValid() || !B->IsValid()) { continue; }
                ++Pairs[Owner]; Chosen = K;
                if (Owner == 0)
                { const auto* C = F.Samples[2].Find(K); if (C && C->IsValid()) { ++Triple; } }
            }
            Out += FString::Printf(TEXT(" [owner=%d authoritySharedKeys=%d latestSharedSequence=%u]"), Owner, Pairs[Owner], uint32(Chosen));
            if (!Chosen) { continue; }
            const void* A = (*F.Samples[0].Find(Chosen))->GetStructMemory();
            const void* B = (*F.Samples[Owner + 1].Find(Chosen))->GetStructMemory();
            Out += FString::Printf(TEXT(" [sameKeyWholeEqual=%d]"), F.SnapshotType->CompareScriptStruct(A, B, PPF_None));
            int32 Differences = 0;
            Describe(F.SnapshotType, A, B, TEXT("snapshot."), Out, Differences);
            const auto* Stations = P<FArrayProperty>(F.SnapshotType, TEXT("Stations"));
            const auto* Inner = Stations ? CastField<FStructProperty>(Stations->Inner) : nullptr;
            if (!Inner || !IsValid(Inner->Struct)) { continue; }
            FScriptArrayHelper AA(Stations, Stations->ContainerPtrToValuePtr<void>(A));
            FScriptArrayHelper BB(Stations, Stations->ContainerPtrToValuePtr<void>(B));
            for (int32 I = 0; I < FMath::Min(8, FMath::Min(AA.Num(), BB.Num())); ++I)
            { Describe(Inner->Struct, AA.GetRawPtr(I), BB.GetRawPtr(I), FString::Printf(TEXT("station%d."), I), Out, Differences); }
            Out += FString::Printf(TEXT(" [differentReflectedFields=%d]"), Differences);
        }
        return FString::Printf(TEXT(" [authorityGeneration=%d commonThreeWorldKeys=%d]"), G, Triple) + Out;
    }
    struct FStep { FString Name; TFunction<bool()> Body; bool Wait; };
    class FScenario final : public IAutomationLatentCommand
    {
    public:
        FScenario(FAutomationTestBase* T, TSharedRef<FFixture> F, TArray<FStep>&& S) : Test(T), Fixture(F), Steps(MoveTemp(S)) {}
        bool Update() override
        {
            if (ck::auto_test::net::Get_HasSessionFailed() || Index == Steps.Num()) { return true; }
            if (Start < 0) { Start = FPlatformTime::Seconds(); }
            auto& S = Steps[Index]; const bool Context = Fixture->Context();
            if (Context && S.Body()) { ++Index; Start = -1; return Index == Steps.Num(); }
            if (Context && S.Wait && FPlatformTime::Seconds() - Start < 45) { return false; }
            FString Error = TEXT("Station Showcase phase failed: ") + S.Name;
            Error += TEXT(" [last cohort prerequisite: ") + Fixture->LastCohortDiagnostic + TEXT("]");
            for (int32 World = 0; World < 3; ++World)
            {
                UWorld* W = World == 0 ? Fixture->Server.Get() : Fixture->Local[World - 1].IsValid() ? Fixture->Local[World - 1]->GetWorld() : nullptr;
                const void* D = Fixture->Data(W);
                APlayerController* PC = World == 0 ? IsValid(W) ? W->GetFirstPlayerController() : nullptr : Fixture->Local[World - 1].Get();
                Error += FString::Printf(TEXT(" [world=%d ready=%d failed=%d gen=%d seq=%d shown=%d timers=%d present=%d local=%d history=%d]"),
                    World, Bool(Fixture->SnapshotType, D, TEXT("Ready")), Bool(Fixture->SnapshotType, D, TEXT("Failed")),
                    Int(Fixture->SnapshotType, D, TEXT("Generation")), Int(Fixture->SnapshotType, D, TEXT("Sequence")),
                    PureInt(PC, TEXT("Get_ShowcaseShownSequence")), TimerCount(W),
                    PureBool(PC, TEXT("Get_ShowcasePresentationReady")), Handles(PC, TEXT("Get_ShowcaseLocalStations")).Num(), Fixture->Samples[World].Num());
            }
            if (Fixture->Strict)
            { Error += FString::Printf(TEXT(" [peerQueued=%d retainedAdmission=%d admissionCacheObserved=%d preinstalledAtAdmission=%d admissionInstalled=%d previousInstalled=%d oldInstallAgainstLatest=%d firstDelivery=%d]"),
                Fixture->PeerQueued, Fixture->SawRetainedAdmission, Fixture->AdmissionCacheObserved, Fixture->PreinstalledAtAdmission,
                Fixture->AdmissionInstalledDelivery, Fixture->PreviousInstalledDelivery[1], Fixture->SawOldInstallAgainstLatest, Fixture->FirstDelivery); }
            Error += SharedEpochObservation(*Fixture);
            Test->AddError(Error); ck::auto_test::net::FailSession(Error); return true;
        }
    private: FAutomationTestBase* Test; TSharedRef<FFixture> Fixture; TArray<FStep> Steps; int32 Index = 0; double Start = -1;
    };
    class FAlwaysRestore final : public IAutomationLatentCommand
    {
    public: explicit FAlwaysRestore(TSharedRef<FFixture> F) : Fixture(F) {} bool Update() override { Fixture->Restore(); return true; }
    private: TSharedRef<FFixture> Fixture;
    };
}

namespace ck_station_showcase_network
{
    auto SaveSample(FFixture& F, UWorld* W) -> void
    {
        const int32 I = W == F.Server.Get() ? 0 : F.Local[0].IsValid() && W == F.Local[0]->GetWorld() ? 1 :
            F.Local[1].IsValid() && W == F.Local[1]->GetWorld() ? 2 : INDEX_NONE;
        const void* D = F.Data(W);
        if (I == INDEX_NONE || !D || !Bool(F.SnapshotType, D, TEXT("Ready"))) { return; }
        const int32 G = Int(F.SnapshotType, D, TEXT("Generation")), Q = Int(F.SnapshotType, D, TEXT("Sequence"));
        if (G <= 0 || Q <= 0) { return; }
        const uint64 K = FFixture::Key(G, Q);
        if (F.Samples[I].Contains(K)) { return; }
        F.Samples[I].Add(K, CopyStruct(F.SnapshotType, D)); F.SampleOrder[I].Add(K);
        if (F.SampleOrder[I].Num() > 256) { F.Samples[I].Remove(F.SampleOrder[I][0]); F.SampleOrder[I].RemoveAt(0); }
    }
    auto Schema(FFixture& F) -> bool
    {
        F.Snapshot = P<FStructProperty>(F.StateClass, TEXT("Snapshot")); F.Commit = P<FStructProperty>(F.PCClass, TEXT("LastControlCommit"));
        if (!F.Snapshot || !F.Commit) { return false; }
        F.SnapshotType = F.Snapshot->Struct.Get(); F.CommitType = F.Commit->Struct.Get();
        F.CommitSample = P<FStructProperty>(F.CommitType, TEXT("Snapshot"));
        if (!F.CommitSample || F.CommitSample->Struct != F.SnapshotType) { return false; }
        for (auto Name : {TEXT("CatalogVersion"), TEXT("Generation"), TEXT("Sequence"), TEXT("TickCount"), TEXT("RevealedLines")})
        { if (!P<FIntProperty>(F.SnapshotType, Name)) { return false; } }
        for (auto Name : {TEXT("Ready"), TEXT("Failed")}) { if (!P<FBoolProperty>(F.SnapshotType, Name)) { return false; } }
        for (auto Name : {TEXT("RuntimeTitle"), TEXT("RuntimeBody"), TEXT("FailureReason")}) { if (!P<FStrProperty>(F.SnapshotType, Name)) { return false; } }
        for (auto Name : {TEXT("StationTitles"), TEXT("StationBodies"), TEXT("StationInstructions")})
        { const auto* A = P<FArrayProperty>(F.SnapshotType, Name); if (!A || !CastField<FStrProperty>(A->Inner)) { return false; } }
        const auto* A = P<FArrayProperty>(F.SnapshotType, TEXT("Stations"));
        const auto* R = A ? CastField<FStructProperty>(A->Inner) : nullptr;
        if (!R || !IsValid(R->Struct) || R->Struct->GetName() != TEXT("Ck_GymStation_SpawnParams")) { return false; }
        for (auto Name : {TEXT("DeliverySerial"), TEXT("RequestSerial"), TEXT("OriginGeneration")})
        { if (!P<FIntProperty>(F.CommitType, Name)) { return false; } }
        for (auto Name : {TEXT("OriginState"), TEXT("Owner"), TEXT("OwnerPawn")})
        { if (!P<FObjectPropertyBase>(F.CommitType, Name)) { return false; } }
        for (auto Name : {TEXT("LastControlAttemptSerial"), TEXT("LastControlSerial"), TEXT("LastControlResult"), TEXT("LastAcknowledgedDeliverySerial")})
        { if (!P<FIntProperty>(F.PCClass, Name)) { return false; } }
        if (!P<FBoolProperty>(F.PCClass, TEXT("OwnerSessionFailed"))) { return false; }
        F.Display = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.CkGym_Station_TitleAndDescription"));
        F.Anchors = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Angelscript.CkGym_Station_Anchors"));
        F.VisibleGeometry = FindObject<UScriptStruct>(nullptr,
            TEXT("/Script/Angelscript.CkGym_StationVisibleGeometry"));
        if (!IsValid(F.Display) || !IsValid(F.Anchors) || !IsValid(F.VisibleGeometry))
        { return false; }
        for (auto Name : {TEXT("Title"), TEXT("Description"), TEXT("Instructions")}) { if (!P<FTextProperty>(F.Display, Name)) { return false; } }
        return true;
    }
    auto Run(FAutomationTestBase& Test, const FString& Parameters, bool Strict) -> bool
    {
        auto F = MakeShared<FFixture>(); F->Strict = Strict;
        if (!ck::auto_test::net::TryGet_DedicatedServer(Parameters, F->Dedicated)) { return false; }
        F->Mode = FSoftClassPath(TEXT("/Script/Angelscript.Ck_StationShowcaseGym_GameMode")).TryLoadClass<AGameModeBase>();
        F->StateClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_StationShowcaseGym_GameState")).TryLoadClass<AGameStateBase>();
        F->PCClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_StationShowcaseGym_PlayerController")).TryLoadClass<APlayerController>();
        F->StationClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_EntityScript_GymStation")).TryLoadClass<UCk_EntityScript_UE>();
        F->TickerClass = FSoftClassPath(TEXT("/Script/Angelscript.Ck_EntityScript_GymStation_Showcase_AutoSizeTicker")).TryLoadClass<UCk_EntityScript_UE>();
        if (!Test.TestTrue(TEXT("actual authored classes and complete typed schema"),
            F->Mode && F->StateClass && F->PCClass && F->StationClass && F->TickerClass && Schema(*F))) { return false; }
        const auto* Settings = GetDefault<UCkGym_StartupSettings>();
        F->Startup = Settings->StartupMode; F->Last = Settings->LastGymName; F->Recents = Settings->RecentGymNames;
        GetMutableDefault<UCkGym_StartupSettings>()->StartupMode = ECkGym_StartupMode::Cycler;
        Test.AddCommand(new FCk_Latent_StartPIEMultiClient(3, TEXT("/CkTests/TestGyms/TestGyms_CkTests_Level"), Parameters, F->Mode));
        Test.AddCommand(new FCk_Latent_WaitForPIEReady(3, 60));
        Test.AddCommand(new FCk_Latent_WaitUntil(&Test, FCk_NetAutoTest_Condition::CreateLambda([F]()
        { return F->Context() && PureBool(F->State(ck::auto_test::net::Get_ServerWorld()), TEXT("Get_IsReady")); }),
            60, TEXT("actual Showcase three-world authority construction")));
        TArray<FStep> Steps;
        auto Act = [&Steps](FString N, TFunction<bool()> B) { Steps.Add({MoveTemp(N), MoveTemp(B), false}); };
        auto Wait = [&Steps](FString N, TFunction<bool()> B) { Steps.Add({MoveTemp(N), MoveTemp(B), true}); };
        Act(TEXT("capture exact owners and arm pure production observations"), [F]()
        {
            F->Server = ck::auto_test::net::Get_ServerWorld();
            F->OriginalMode = F->Server->GetAuthGameMode(); F->States[0] = F->State(F->Server.Get());
            for (int32 I = 0; I < 2; ++I)
            {
                F->Local[I] = ck::auto_test::net::Get_ClientWorld(I)->GetFirstPlayerController();
                F->Remote[I] = ck::auto_test::net::TryGet_RemoteClientPlayerController(F->Server.Get(), I);
                F->States[I + 1] = F->State(F->Local[I]->GetWorld());
                F->LocalPawn[I] = F->Local[I]->GetPawn(); F->RemotePawn[I] = F->Remote[I]->GetPawn();
                if (!F->LocalPawn[I].IsValid() || !F->RemotePawn[I].IsValid()) { return false; }
            }
            F->Captured = true;
            F->Observer = FWorldDelegates::OnWorldPostActorTick.AddLambda([F](UWorld* W, ELevelTick, float)
            {
                if (!F->Context()) { return; }
                SaveSample(*F, W);
                const auto* First = F->OwnerCommit(F->Remote[1].Get());
                const int32 Delivery = Int(F->CommitType, First, TEXT("DeliverySerial"));
                const bool Retained = Delivery > 0 && Int(F->PCClass, F->Remote[1].Get(), TEXT("LastAcknowledgedDeliverySerial")) < Delivery;
                if (Retained && Int(F->CommitType, First, TEXT("RequestSerial")) == F->FirstRequest && !F->Frozen.IsValid())
                { F->Frozen = CopyStruct(F->CommitType, First); F->FirstDelivery = Delivery; }
                // The strict case sends through a real local owning RPC only after its own complete new GS sample arrives.
                if (F->Strict && !F->PeerQueued && Retained && W == F->Local[0]->GetWorld() &&
                    Complete(*F, W, F->Data(W)) && Int(F->SnapshotType, F->Data(W), TEXT("Generation")) == F->Origin + 1)
                { F->PeerQueued = Rpc(*F, 0, F->PeerRequest, F->Origin + 1); }
                if (!F->SawRetainedAdmission && W == F->Server.Get() && Retained && F->PeerQueued &&
                    Int(F->PCClass, F->Remote[0].Get(), TEXT("LastControlAttemptSerial")) == F->PeerRequest &&
                    Int(F->PCClass, F->Remote[0].Get(), TEXT("LastControlResult")) == 0 &&
                    !Bool(F->SnapshotType, F->Data(W), TEXT("Ready")))
                {
                    F->SawRetainedAdmission = true;
                    // Observe the actual cache at admission: a preinstalled held value is not a new installation.
                    const auto AtAdmission = Installed(*F, F->Local[1].Get());
                    if (AtAdmission.IsValid())
                    {
                        F->AdmissionCacheObserved = true;
                        F->AdmissionInstalledDelivery = Int(F->CommitType, AtAdmission->GetStructMemory(), TEXT("DeliverySerial"));
                        F->PreinstalledAtAdmission = F->AdmissionInstalledDelivery == F->FirstDelivery;
                        F->PreviousInstalledDelivery[1] = F->AdmissionInstalledDelivery;
                    }
                }
                for (int32 I = 0; I < 2; ++I)
                {
                    if (W != F->Local[I]->GetWorld()) { continue; }
                    const auto InstalledCommit = Installed(*F, F->Local[I].Get());
                    const auto* LocalCommit = F->OwnerCommit(F->Local[I].Get());
                    if (!InstalledCommit.IsValid() || !LocalCommit) { continue; }
                    const void* D = InstalledCommit->GetStructMemory();
                    const int32 InstalledDelivery = Int(F->CommitType, D, TEXT("DeliverySerial"));
                    const bool NewInstallation = InstalledDelivery != F->PreviousInstalledDelivery[I];
                    F->PreviousInstalledDelivery[I] = InstalledDelivery;
                    if (InstalledDelivery > 0 && Int(F->PCClass, F->Remote[I].Get(), TEXT("LastAcknowledgedDeliverySerial")) < InstalledDelivery &&
                        F->CommitType->CompareScriptStruct(D, LocalCommit, PPF_None) &&
                        Complete(*F, W, F->CommitSample->ContainerPtrToValuePtr<void>(D)))
                    {
                        F->SawInstalled[I] = true;
                        if (I == 1 && F->SawRetainedAdmission && F->AdmissionCacheObserved &&
                            !F->PreinstalledAtAdmission && NewInstallation && InstalledDelivery == F->FirstDelivery &&
                            (!Bool(F->SnapshotType, F->Data(W), TEXT("Ready")) ||
                             Int(F->SnapshotType, F->Data(W), TEXT("Generation")) > F->Origin + 1))
                        { F->SawOldInstallAgainstLatest = true; }
                    }
                }
            });
            return true;
        });
        Wait(TEXT("all eight full tuners/layout, local shells/own HUD and authority-only clock"), [F]()
        { SaveSample(*F, F->Server.Get()); for (int32 I = 0; I < 2; ++I) { SaveSample(*F, F->Local[I]->GetWorld()); } return Cohort(*F); });
        Wait(TEXT("actual ten visible role components per live station in all three worlds"), [F]()
        {
            if (!F->Context() || !Cohort(*F)) { return false; }
            for (int32 World = 0; World < 3; ++World)
            {
                UWorld* W = World == 0 ? F->Server.Get() : F->Local[World - 1]->GetWorld();
                for (int32 Slot = 0; Slot < 8; ++Slot)
                { const auto H = Tagged(W, Slot); if (H.Num() != 1 || !VisibleStation(*F, H[0], W, Slot)) { return false; } }
            }
            return true;
        });
        Act(TEXT("malformed visible captures reject without changing whole real station closure or shared snapshot"), [F]()
        {
            for (int32 World = 0; World < 3; ++World)
            {
                UWorld* W = World == 0 ? F->Server.Get() : F->Local[World - 1]->GetWorld();
                if (!VisibleRejectsWithoutMutation(*F, W)) { return false; }
            }
            return true;
        });
        Wait(TEXT("authored nine reveal lines and continuing native tick counter"), [F]()
        {
            const void* D = F->Data(F->Server.Get());
            const FString Body = Str(F->SnapshotType, D, TEXT("RuntimeBody"));
            if (!Cohort(*F) || !SharedSnapshotEqual(*F, true) || Int(F->SnapshotType, D, TEXT("RevealedLines")) != 9 || Int(F->SnapshotType, D, TEXT("TickCount")) <= 9 ||
                !Body.Contains(TEXT("Tick reveal step 9: final reveal")) || !Body.Contains(TEXT("Tick: "))) { return false; }
            for (int32 I = 1; I <= 9; ++I) { if (!Body.Contains(FString::Printf(TEXT("Tick reveal step %d:"), I))) { return false; } }
            return true;
        });
        Act(TEXT("public R metadata and first real owning rebuild RPC"), [F]()
        {
            for (int32 I = 0; I < 2; ++I)
            { const auto R = Rows(F->Local[I].Get()); if (R.IsEmpty() || R[0].Kind != ECkGym_ControlKind::Action || R[0].Key != EKeys::R || !R[0].Enabled) { return false; } }
            F->Origin = Int(F->SnapshotType, F->Data(F->Server.Get()), TEXT("Generation"));
            return SaveCohorts(*F, F->Old) && Rpc(*F, 1, F->FirstRequest, F->Origin);
        });
        Wait(TEXT("first complete real installed owner cache, explicit server ACK and allocation retirement"), [F]()
        { return Consumed(*F, 1, F->FirstRequest) && Retired(F->Old[0]) && Retired(F->Old[1]) && Retired(F->Old[2]); });
        if (Strict)
        {
            Wait(TEXT("strict retained-concurrency: real peer authoritative admission before old ACK and old whole install against advanced/not-ready latest"), [F]()
            { return F->PeerQueued && F->SawRetainedAdmission && F->SawOldInstallAgainstLatest; });
        }
        else
        {
            Wait(TEXT("new-generation whole cohort healthy before independent peer rebuild"), [F]() { return Cohort(*F); });
            Act(TEXT("second owner's independent real rebuild RPC"), [F]()
            { return Rpc(*F, 0, F->PeerRequest, Int(F->SnapshotType, F->Data(F->Local[0]->GetWorld()), TEXT("Generation"))); });
        }
        Wait(TEXT("peer complete owner outcome and unchanged frozen first-owner package"), [F]()
        {
            const void* First = F->OwnerCommit(F->Remote[1].Get());
            return F->Frozen.IsValid() && First && Consumed(*F, 0, F->PeerRequest) && Consumed(*F, 1, F->FirstRequest) &&
                F->CommitType->CompareScriptStruct(F->Frozen->GetStructMemory(), First, PPF_None) && Cohort(*F);
        });
        Act(TEXT("pure whole cache/HUD reads preserve owner serials, receipts and frozen packages"), [F]()
        {
            for (int32 I = 0; I < 2; ++I)
            {
                APlayerController* PC = F->Remote[I].Get();
                const int32 Attempt = Int(F->PCClass, PC, TEXT("LastControlAttemptSerial"));
                const int32 Accepted = Int(F->PCClass, PC, TEXT("LastControlSerial"));
                const int32 Ack = Int(F->PCClass, PC, TEXT("LastAcknowledgedDeliverySerial"));
                const auto Before = CopyStruct(F->CommitType, F->OwnerCommit(PC));
                if (!Before.IsValid()) { return false; }
                for (int32 Read = 0; Read < 16; ++Read)
                { if (!Installed(*F, F->Local[I].Get()).IsValid() || Rows(F->Local[I].Get()).IsEmpty()) { return false; } }
                if (Attempt != Int(F->PCClass, PC, TEXT("LastControlAttemptSerial")) ||
                    Accepted != Int(F->PCClass, PC, TEXT("LastControlSerial")) || Ack != Int(F->PCClass, PC, TEXT("LastAcknowledgedDeliverySerial")) ||
                    !F->CommitType->CompareScriptStruct(Before->GetStructMemory(), F->OwnerCommit(PC), PPF_None)) { return false; }
            }
            return true;
        });
        Act(TEXT("actual original-owner pawn-context departure"), [F]()
        { if (!IsValid(F->Remote[0]->GetPawn())) { return false; } F->Departed = true; F->Remote[0]->UnPossess(); return true; });
        Wait(TEXT("owner becomes terminal while shared authority/peer cohort survives"), [F]()
        {
            const auto R = Rows(F->Local[0].Get());
            return Bool(F->PCClass, F->Remote[0].Get(), TEXT("OwnerSessionFailed")) &&
                Bool(F->PCClass, F->Local[0].Get(), TEXT("OwnerSessionFailed")) && !R.IsEmpty() && !R[0].Enabled &&
                Cohort(*F) && Consumed(*F, 1, F->FirstRequest);
        });
        Act(TEXT("actual required station destruction after capturing all allocated closures"), [F]()
        {
            if (!SaveCohorts(*F, F->Terminal)) { return false; }
            const auto H = Tagged(F->Server.Get(), 3); if (H.Num() != 1 || !Live(H[0])) { return false; }
            auto Victim = H[0]; UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(Victim); return true;
        });
        Wait(TEXT("required child loss invalidates and retires the entire authority/client cohort"), [F]()
        {
            if (!Bool(F->SnapshotType, F->Data(F->Server.Get()), TEXT("Failed")) ||
                Bool(F->SnapshotType, F->Data(F->Server.Get()), TEXT("Ready")) || PureBool(F->State(F->Server.Get()), TEXT("Get_IsReady")) ||
                !Handles(F->State(F->Server.Get()), TEXT("ServerStations"), true).IsEmpty() ||
                Live(Handle(F->State(F->Server.Get()), TEXT("ServerTicker"), true)) || !TickerCensus(F->Server.Get(), *F, 0, 0)) { return false; }
            for (int32 World = 0; World < 3; ++World)
            {
                UWorld* W = World == 0 ? F->Server.Get() : F->Local[World - 1]->GetWorld();
                if (!Retired(F->Terminal[World]) || !Bool(F->SnapshotType, F->Data(W), TEXT("Failed")) || !TickerCensus(W, *F, 0, 0) || TimerCount(W) != 0) { return false; }
                for (int32 Slot = 0; Slot < 8; ++Slot) { if (!Tagged(W, Slot).IsEmpty()) { return false; } }
                if (World > 0 && (!Handles(F->Local[World - 1].Get(), TEXT("Get_ShowcaseLocalStations")).IsEmpty() ||
                    PureBool(F->Local[World - 1].Get(), TEXT("Get_ShowcasePresentationReady")))) { return false; }
            }
            return true;
        });
        Test.AddCommand(new FScenario(&Test, F, MoveTemp(Steps)));
        Test.AddCommand(new FAlwaysRestore(F));
        Test.AddCommand(new FCk_Latent_EndPIE());
        return true;
    }
}

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStationShowcaseGym_Network,
    "Ck.StationShowcase.Network.Production", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkStationShowcaseGym_Network::RunTest(const FString& Parameters)
{ return ck_station_showcase_network::Run(*this, Parameters, false); }

CK_IMPLEMENT_NET_AUTOMATION_TEST(FCkStationShowcaseGym_RetainedConcurrency_Network,
    "Ck.StationShowcase.Network.RetainedConcurrency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FCkStationShowcaseGym_RetainedConcurrency_Network::RunTest(const FString& Parameters)
{ return ck_station_showcase_network::Run(*this, Parameters, true); }
#endif

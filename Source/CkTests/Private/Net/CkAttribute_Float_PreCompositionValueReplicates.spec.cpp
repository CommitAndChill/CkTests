// Regression test for generic replicated-container retry after real pre-composition NotReady.
// The server adds Armor(77) after actor construction. A bounded client receipt gate proves that
// exact Current Base/Final payload is pending, the dispatcher already deferred it, and client Armor
// is still absent. That same Update composes Armor(5) inside the existing retry watchdog; the normal
// dispatcher and attribute pipeline must then apply 77. No synthetic receipt or handler replay.
//
// Surface in Session Frontend: Ck.Attribute.Net.Float_PreComposition_StashedValue_Applies

#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

#include "CkAttribute/FloatAttribute/CkFloatAttribute_Fragment_Data.h"
#include "CkAttribute/FloatAttribute/CkFloatAttribute_Fragment.h"
#include "CkAttribute/FloatAttribute/CkFloatAttribute_Utils.h"
#include "CkEcs/OwningActor/CkOwningActor_Utils.h"
#include "CkEcs/Net/EntityReplicationDriver/CkEntityReplicationDriver_Fragment.h"
#include "CkEcs/Persistence/CkPersistenceHydration_Processor.h"

#include "CkTests/Net/CkAutoTest_NetSubject.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

// --------------------------------------------------------------------------------------------------------------------

namespace
{
    // Registered in Config/DefaultGameplayTags.ini. Deliberately NOT one of the attributes the
    // subject's entity-script composes in Construct — the test needs an attribute that does not
    // exist on the client when its replicated value first arrives.
    constexpr auto PreComposition_AttributeTagName = TEXT("FloatAttribute.Armor");

    // Value the server bakes into its Add. This must survive pending receive -> deferred apply.
    constexpr auto PreComposition_ServerValue = 77.0f;

    // Value the client composes with, distinct from the server's so a dropped stash shows up as a
    // clear mismatch instead of coincidentally matching.
    constexpr auto PreComposition_ClientInitialValue = 5.0f;

    class FCk_Latent_ComposeFloatOnPendingReceipt final : public IAutomationLatentCommand
    {
    public:
        FCk_Latent_ComposeFloatOnPendingReceipt(
            FAutomationTestBase& InTest,
            int32 InMaximumUpdates)
            : _Test{InTest}, _MaximumUpdates{InMaximumUpdates}
        {
        }

        auto
        Update()
            -> bool override
        {
            if (_Finished)
            { return true; }
            ++_Updates;

            auto OwnerEntity = FCk_Handle{};
            auto ActorIdentity = FString{};
            auto PendingSince = 0.0;
            auto ObservedAt = 0.0;
            auto PendingAge = 0.0;
            auto ReceivedBase = 0.0f;
            auto ReceivedFinal = 0.0f;
            const auto AttributeTag = FGameplayTag::RequestGameplayTag(FName{PreComposition_AttributeTagName});
            {
                // Borrowed UObject/entry/payload pointers end before Add; reacquire on every Update.
                auto* Client = ck::auto_test::net::Get_ClientWorld(0);
                if (ck::Is_NOT_Valid(Client))
                { return DoWait(TEXT("Stage 3: client world unavailable")); }

                auto* Subject = ACk_AutoTest_NetSubject::Find(Client);
                if (ck::Is_NOT_Valid(Subject))
                { return DoWait(TEXT("Stage 3: client world has no valid ACk_AutoTest_NetSubject - actor failed to replicate")); }

                OwnerEntity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Subject);
                if (ck::Is_NOT_Valid(OwnerEntity))
                { return DoWait(TEXT("Stage 3: client-side actor has no bridged entity - entity-script Construct did not run on client")); }

                if (ck::IsValid(UCk_Utils_FloatAttribute_UE::TryGet(OwnerEntity, AttributeTag)))
                { return DoReject(TEXT("client Armor already exists before the pending-receipt composition")); }

                if (NOT OwnerEntity.Has<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>())
                { return DoWait(TEXT("client owner has no replication-driver fragment")); }
                auto* Driver = OwnerEntity.Get<TObjectPtr<UCk_Fragment_EntityReplicationDriver_Rep>>().Get();
                if (ck::Is_NOT_Valid(Driver))
                { return DoWait(TEXT("client replication-driver UObject is invalid")); }

                const auto* Entry = Driver->FindEntry(FCk_RepData_FloatAttributes::StaticStruct());
                if (Entry == nullptr)
                { return DoWait(TEXT("client has not received the FloatAttributes entry")); }
                if (NOT Entry->Data.IsValid())
                { return DoWait(TEXT("received FloatAttributes data is invalid")); }
                if (Entry->Data.GetScriptStruct() != FCk_RepData_FloatAttributes::StaticStruct())
                { return DoReject(TEXT("received FloatAttributes entry has the wrong data type")); }
                const auto* Payload = Entry->Data.GetPtr<FCk_RepData_FloatAttributes>();
                if (Payload == nullptr)
                { return DoReject(TEXT("received FloatAttributes data has no safe typed payload")); }
                const auto* ReceivedArmor = Payload->Attributes.FindByPredicate(
                    [&AttributeTag](const FCk_Fragment_FloatAttribute_BaseFinal& InAttribute) -> bool
                    {
                        return InAttribute.Get_AttributeName() == AttributeTag
                            && InAttribute.Get_Component() == ECk_MinMaxCurrent::Current
                            && InAttribute.Get_Base() == PreComposition_ServerValue
                            && InAttribute.Get_Final() == PreComposition_ServerValue;
                    });
                if (ReceivedArmor == nullptr)
                { return DoWait(TEXT("client has not received Armor Current Base/Final 77")); }

                if (NOT Entry->_PendingApply)
                { return DoReject(TEXT("matching Armor 77 receipt is no longer pending; no late fallback composition")); }
                PendingSince = Entry->_PendingSinceRealTimeSeconds;
                if (PendingSince == 0.0)
                { return DoWait(TEXT("matching Armor receipt has not yet returned NotReady from the dispatcher")); }
                if (NOT FMath::IsFinite(PendingSince) || PendingSince < 0.0)
                { return DoReject(TEXT("matching Armor receipt has an invalid NotReady timestamp")); }
                ObservedAt = FPlatformTime::Seconds();
                PendingAge = ObservedAt - PendingSince;
                if (NOT FMath::IsFinite(PendingAge) || PendingAge < 0.0
                    || PendingAge >= ck::PendingApplyTimeoutSeconds)
                { return DoReject(TEXT("matching Armor receipt is outside the existing pending-apply watchdog")); }

                ActorIdentity = Subject->GetPathName();
                ReceivedBase = ReceivedArmor->Get_Base();
                ReceivedFinal = ReceivedArmor->Get_Final();
            }

            UE_LOG(LogTemp, Display,
                TEXT("[ATTRIBUTE-PRECOMPOSE-RECEIPT] actor=%s attribute=FloatAttribute.Armor component=Current base=%.9f final=%.9f pending=true pendingSince=%.9f observedAt=%.9f age=%.9f update=%d"),
                *ActorIdentity, static_cast<double>(ReceivedBase), static_cast<double>(ReceivedFinal),
                PendingSince, ObservedAt, PendingAge, _Updates);
            _Finished = true;
            UCk_Utils_FloatAttribute_UE::Add(OwnerEntity,
                FCk_FloatAttribute_Spec{AttributeTag, PreComposition_ClientInitialValue},
                ECk_Replication::Replicates);
            return true;
        }

    private:
        auto
        DoWait(
            const FString& InMissingPrerequisite)
            -> bool
        {
            if (_Updates < _MaximumUpdates)
            { return false; }
            return DoReject(FString::Printf(TEXT("no eligible receipt within %d updates; last prerequisite: %s"),
                _MaximumUpdates, *InMissingPrerequisite));
        }

        auto
        DoReject(
            const FString& InReason)
            -> bool
        {
            _Finished = true;
            _Test.AddError(FString::Printf(TEXT("Stage 3 pending-receipt gate: %s (update %d); client Armor was not composed"),
                *InReason, _Updates));
            return true;
        }

        FAutomationTestBase& _Test;
        int32 _MaximumUpdates;
        int32 _Updates = 0;
        bool _Finished = false;
    };
}

// --------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkAttributeNet_Float_PreComposition_StashedValue_Applies,
    "Ck.Attribute.Net.Float_PreComposition_StashedValue_Applies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FCkAttributeNet_Float_PreComposition_StashedValue_Applies::RunTest(const FString& Parameters)
{
    // CkActorRelay × Iris ambient noise under multi-client PIE — same backstop the SM
    // replication tests use. Restored at the end via a trailing latent command.
    bSuppressLogErrors = true;
    bSuppressLogWarnings = true;

    constexpr auto NumPIEClients = 2;
    constexpr auto ExpectedTotalWorlds = 2;
    constexpr auto ReadyTimeoutSeconds = 30.0f;
    constexpr auto FramesAfterSpawn = 30;
    // Maximum updates to prove the actual client receipt and compose in the same Update.
    constexpr auto FramesAfterServerAdd = 120;
    // Unchanged drain window for the generic dispatcher after the client's late composition.
    constexpr auto FramesAfterClientCompose = 120;

    const auto MapPath = FString{TEXT("/Engine/Maps/Entry")};

    auto OwnerSlot = MakeShared<FCk_Handle>();

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(NumPIEClients, MapPath));
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(ExpectedTotalWorlds, ReadyTimeoutSeconds));

    // ---- Stage 1 — server spawns the subject; WithActor entity constructs on both worlds. ----------------------

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this](UWorld* InServer) -> void
        {
            auto SpawnInfo = FActorSpawnParameters{};
            SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            auto* Subject = InServer->SpawnActor<ACk_AutoTest_NetSubject>(
                ACk_AutoTest_NetSubject::StaticClass(), FTransform::Identity, SpawnInfo);

            if (Subject == nullptr)
            {
                AddError(TEXT("server-side spawn of ACk_AutoTest_NetSubject returned null"));
                return;
            }
        })));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterSpawn));

    // ---- Stage 2 — server composes the Armor attribute with the sentinel value. The client ---------------------
    // ---- world does NOT have this attribute, so the replicated container change stashes there. ------------------

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(
        FCk_NetAutoTest_ServerAction::CreateLambda([this, OwnerSlot](UWorld* InServer) -> void
        {
            auto* Subject = ACk_AutoTest_NetSubject::Find(InServer);
            if (Subject == nullptr)
            {
                AddError(TEXT("Stage 2: server-side subject actor missing — Stage 1 spawn never happened?"));
                return;
            }

            auto OwnerEntity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(Subject);
            if (ck::Is_NOT_Valid(OwnerEntity))
            {
                AddError(TEXT("Stage 2: server-side TryGet_ActorEntityHandle returned invalid — WithActor::Construct did not run yet (extend FramesAfterSpawn?)"));
                return;
            }

            const auto AttributeTag = FGameplayTag::RequestGameplayTag(FName{PreComposition_AttributeTagName});

            UCk_Utils_FloatAttribute_UE::Add(OwnerEntity,
                FCk_FloatAttribute_Spec{AttributeTag, PreComposition_ServerValue},
                ECk_Replication::Replicates);

            *OwnerSlot = OwnerEntity;
        })));

    // Stage 3: actual matching client receipt + dispatcher NotReady precede same-Update Add(5).
    // This is bounded by the original 120 updates, not an unproved fixed-delay stash window.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_ComposeFloatOnPendingReceipt(*this, FramesAfterServerAdd));

    // The normal generic dispatcher applies the pending entry, then the attribute pipeline computes it.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_TickWorlds(FramesAfterClientCompose));

    // ---- Cross-world assertion -----------------------------------------------------------------------------------

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([this, OwnerSlot]() -> bool
        {
            auto* Server = ck::auto_test::net::Get_ServerWorld();
            auto* Client = ck::auto_test::net::Get_ClientWorld(0);

            if (Server == nullptr || Client == nullptr)
            {
                AddError(TEXT("server and/or client world unavailable at assertion time"));
                return false;
            }

            const auto AttributeTag = FGameplayTag::RequestGameplayTag(FName{PreComposition_AttributeTagName});

            if (ck::IsValid(*OwnerSlot))
            {
                auto ServerOwner = *OwnerSlot;
                const auto ServerAttribute = UCk_Utils_FloatAttribute_UE::TryGet(ServerOwner, AttributeTag);
                if (ck::Is_NOT_Valid(ServerAttribute))
                {
                    AddError(TEXT("server-side TryGet for the Armor attribute returned an invalid handle"));
                }
                else
                {
                    const auto ServerValue = UCk_Utils_FloatAttribute_UE::Get_FinalValue(ServerAttribute);
                    TestEqual(TEXT("server FinalValue matches the composed sentinel value"),
                        ServerValue, PreComposition_ServerValue);
                }
            }
            else
            {
                AddError(TEXT("server-side OwnerSlot was never populated (Stage 2 lambda bailed early?)"));
            }

            auto* ClientSubject = ACk_AutoTest_NetSubject::Find(Client);
            if (ClientSubject == nullptr)
            {
                AddError(TEXT("client world has no ACk_AutoTest_NetSubject at assertion time"));
                return false;
            }

            const auto ClientOwnerEntity = UCk_Utils_OwningActor_UE::TryGet_ActorEntityHandle(ClientSubject);
            if (ck::Is_NOT_Valid(ClientOwnerEntity))
            {
                AddError(TEXT("client-side actor has no bridged entity at assertion time"));
                return false;
            }

            const auto ClientAttribute = UCk_Utils_FloatAttribute_UE::TryGet(ClientOwnerEntity, AttributeTag);
            if (ck::Is_NOT_Valid(ClientAttribute))
            {
                AddError(TEXT("client-side TryGet found no Armor attribute — Stage 3 composition did not run"));
                return false;
            }

            // The pending received value must override the client's locally-composed initial value.
            // A missing receipt/retry or failed composition remains a failure, never a fallback Add.
            const auto ClientValue = UCk_Utils_FloatAttribute_UE::Get_FinalValue(ClientAttribute);
            TestEqual(TEXT("client FinalValue matches the server value that replicated BEFORE client composition"),
                ClientValue, PreComposition_ServerValue);

            return true;
        }),
        TEXT("pre-composition replicated attribute value applies after late client composition")));

    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());

    // Restore static log suppression after teardown so subsequent tests see default semantics.
    ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_AssertCondition(this,
        FCk_NetAutoTest_Assertion::CreateLambda([]() -> bool
        {
            FAutomationTestBase::bSuppressLogErrors = false;
            FAutomationTestBase::bSuppressLogWarnings = false;
            return true;
        }),
        TEXT("restore log suppression statics")));

    return true;
}

// --------------------------------------------------------------------------------------------------------------------

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

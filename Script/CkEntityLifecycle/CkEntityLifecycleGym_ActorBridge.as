//============================================================================
// ENTITY LIFECYCLE GYM - ACTOR BRIDGE
// Tests: utils_owning_actor, utils_entity_bridge
//============================================================================

class UCk_EntityScript_EntityLifecycleGym_ActorBridge : UCkLifecycleGym_DemoBase
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	// Test results
	UPROPERTY() bool Pass_GetOwnerEntity = false;
	UPROPERTY() bool Pass_GetEntityOwningActor = false;
	UPROPERTY() bool Pass_ActorHandleRoundtrip = false;
	UPROPERTY() bool Pass_TryGetRecursive = false;
	UPROPERTY() bool Pass_IsActorEcsReady = false;
	UPROPERTY() bool Pass_HasOwningActor = false;
	UPROPERTY() bool Pass_OwningActorInChain = false;

	// Display values
	FString OwnerHandleString = "";
	FString ActorName = "";
	FString ReplicationStatus = "";

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
        if (!CheckConstruct(InHandle)) { return ECk_EntityScript_ConstructionFlow::Finished; }
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_LifecycleGym_ActorBridge");

		// Display tick
		if (!SharedAuthority) { utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick")); }

		// --- Run all tests ---

		// Get owner entity (the station entity)
		auto OwnerHandle = ck::OwnerEntity(this);
		Pass_GetOwnerEntity = utils_handle::Get_IsValid(OwnerHandle);
		OwnerHandleString = utils_handle::Conv_HandleToString(OwnerHandle);

		// Get owning actor from owner handle
		if (!CkLifecycleGym::IsLive(OwnerHandle) || !utils_owning_actor::Has(OwnerHandle)) { FailDemo("Bridge probe owner has no actor"); return ECk_EntityScript_ConstructionFlow::Finished; }
		auto StationActor = utils_owning_actor::TryGet_EntityOwningActor(OwnerHandle);
		Pass_GetEntityOwningActor = ck::IsValid(StationActor);
		if (ck::IsValid(StationActor))
		{
			ActorName = StationActor.GetName().ToString();
		}

		// Roundtrip: actor -> handle -> should equal owner handle
		if (ck::IsValid(StationActor))
		{
			auto RoundtripHandle = utils_owning_actor::TryGet_ActorEntityHandle(StationActor);
			Pass_ActorHandleRoundtrip = utils_handle::IsEqual(RoundtripHandle, OwnerHandle);
		}

		// Recursive: from self entity script's entity, walk up to find station actor
		auto RecursiveActor = utils_owning_actor::TryGet_EntityOwningActor_Recursive(InHandle);
		Pass_TryGetRecursive = ck::IsValid(RecursiveActor);

		// ECS readiness
		if (ck::IsValid(StationActor))
		{
			Pass_IsActorEcsReady = utils_owning_actor::Get_IsActorEcsReady(StationActor);
		}

		// Has owning actor component
		Pass_HasOwningActor = utils_owning_actor::Has(OwnerHandle);

		// Find owning actor entity in ownership chain
		auto OwnerInChain = utils_owning_actor::TryGet_Entity_OwningActor_InOwnershipChain(InHandle);
		Pass_OwningActorInChain = utils_handle::Get_IsValid(OwnerInChain);

		// Actor replication status
		if (ck::IsValid(StationActor))
		{
			auto ReplStatus = StationActor.bReplicates;
			ReplicationStatus = f"{ReplStatus}";
		}

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void DisplayTick(FCk_Handle_Timer InHandle, FCk_Chrono InChrono, FCk_Time InDeltaT)
	{
        if (SharedAuthority || !CkLifecycleGym::IsLive(FCk_Handle(InHandle)) || !utils_timer::DoCast(FCk_Handle(InHandle)).IsSet()) { return; }
		Request_UpdateDisplay();
	}

	void Request_UpdateDisplay()
	{
        if (!CanRun(ck::ToEntity(this))) { return; }
		auto SelfEntity = ck::ToEntity(this);
		auto TitleText = "ACTOR BRIDGE (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		auto D = "";

		D = f"{D}--- utils_owning_actor ---\n";
		D = f"{D}" + (Pass_GetOwnerEntity ? "[+] " : "[-] ") + f"Bridge probe owner handle valid: {OwnerHandleString}\n";
		D = f"{D}" + (Pass_GetEntityOwningActor ? "[+] " : "[-] ") + f"GetEntityOwningActor: {ActorName}\n";
		D = f"{D}" + (Pass_ActorHandleRoundtrip ? "[+] " : "[-] ") + "Actor->Handle roundtrip\n";
		D = f"{D}" + (Pass_TryGetRecursive ? "[+] " : "[-] ") + "TryGet_Recursive (self->bridge probe)\n";
		D = f"{D}" + (Pass_IsActorEcsReady ? "[+] " : "[-] ") + "IsActorEcsReady\n";
		D = f"{D}" + (Pass_HasOwningActor ? "[+] " : "[-] ") + "Has(bridge probe owner)\n";
		D = f"{D}" + (Pass_OwningActorInChain ? "[+] " : "[-] ") + "OwningActor in chain\n\n";

		D = f"{D}--- actor replication ---\n";
		D = f"{D}    Replication status: {ReplicationStatus}\n";

		auto AllPassed = Pass_GetOwnerEntity && Pass_GetEntityOwningActor
			&& Pass_ActorHandleRoundtrip && Pass_TryGetRecursive
			&& Pass_IsActorEcsReady && Pass_HasOwningActor && Pass_OwningActorInChain;

		D = f"{D}\nRESULT: " + (AllPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");

		auto Instructions = "Tests actor-to-entity handle roundtrip, recursive actor lookup,\n"
			+ "ECS readiness, and replication status.";

		        Sample = FCkLifecycleGym_RowSample();
        Sample.BranchGeneration = BranchGeneration;
        Sample.Title = TitleText; Sample.Body = D; Sample.Instructions = Instructions;
        Sample.ActualCallbackCount = ActualCallbackCount;
        Sample.LastCompletedCycle = LastCompletedCycle;
        Sample.LastCycleCallbackCount = LastCycleCallbackCount;
        Sample.Results.Add(Pass_GetOwnerEntity);
        Sample.Results.Add(Pass_GetEntityOwningActor);
        Sample.Results.Add(Pass_ActorHandleRoundtrip);
        Sample.Results.Add(Pass_TryGetRecursive);
        Sample.Results.Add(Pass_IsActorEcsReady);
        Sample.Results.Add(Pass_HasOwningActor);
        Sample.Results.Add(Pass_OwningActorInChain);
        if (!SharedAuthority) { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, D, Instructions); }
	}
}

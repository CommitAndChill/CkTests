//============================================================================
// ENTITY LIFECYCLE GYM - OWNERSHIP TREE
// Tests: utils_entity_lifetime (creation & ownership queries)
//============================================================================

class UCk_EntityScript_EntityLifecycleGym_OwnershipTree : UCkLifecycleGym_DemoBase
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY() FCk_Handle ChildA;
    UPROPERTY() FCk_Handle ChildB;
	// Test results
	UPROPERTY() bool Pass_CreateChildA = false;
	UPROPERTY() bool Pass_CreateChildB = false;
	UPROPERTY() bool Pass_CreateTransient = false;
	UPROPERTY() bool Pass_OwnerOfChildA = false;
	UPROPERTY() bool Pass_OwnerOfChildB = false;
	UPROPERTY() bool Pass_DependentsCount = false;
	UPROPERTY() bool Pass_IsTransient_Transient = false;
	UPROPERTY() bool Pass_IsTransient_Self = false;
	UPROPERTY() bool Pass_WorldForEntity = false;

	// Display values
	FString ChildAString = "";
	FString ChildBString = "";
	FString TransientString = "";
	UPROPERTY() int32 DependentsCount = 0;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
        if (!CheckConstruct(InHandle)) { return ECk_EntityScript_ConstructionFlow::Finished; }
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_LifecycleGym_OwnershipTree");

		// Display tick
		if (!SharedAuthority) { utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick")); }

		// --- Create child entities ---

		// Child A
		ChildA = utils_entity_lifetime::Request_CreateEntity(InHandle);
        if (!CkLifecycleGym::IsLive(ChildA)) { FailDemo("ChildA allocation failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		Pass_CreateChildA = utils_handle::Get_IsValid(ChildA);
		ChildAString = utils_handle::Conv_HandleToString(ChildA);
		utils_handle::Set_DebugName(ChildA, n"ChildA");

		// Child B
		ChildB = utils_entity_lifetime::Request_CreateEntity(InHandle);
        if (!CkLifecycleGym::IsLive(ChildB)) { FailDemo("ChildB allocation failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		Pass_CreateChildB = utils_handle::Get_IsValid(ChildB);
		ChildBString = utils_handle::Conv_HandleToString(ChildB);
		utils_handle::Set_DebugName(ChildB, n"ChildB");

		// Transient entity
		auto TransientEntity = utils_entity_lifetime::Request_CreateEntity_TransientOwner();
        if (!CkLifecycleGym::IsLive(TransientEntity)) { FailDemo("TransientEntity allocation failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
		Remember(TransientEntity);
		Pass_CreateTransient = utils_handle::Get_IsValid(TransientEntity);
		TransientString = utils_handle::Conv_HandleToString(TransientEntity);
		utils_handle::Set_DebugName(TransientEntity, n"OwnershipTree.Transient");

		// --- Query ownership ---

		// Owner of ChildA should be self
		auto OwnerOfA = utils_entity_lifetime::Get_LifetimeOwner(ChildA);
		Pass_OwnerOfChildA = utils_handle::IsEqual(OwnerOfA, InHandle);

		// Owner of ChildB should be self
		auto OwnerOfB = utils_entity_lifetime::Get_LifetimeOwner(ChildB);
		Pass_OwnerOfChildB = utils_handle::IsEqual(OwnerOfB, InHandle);

		// Dependents of self should include both children
		auto Dependents = utils_entity_lifetime::Get_LifetimeDependents(InHandle);
		DependentsCount = Dependents.Num();
		Pass_DependentsCount = (DependentsCount >= 2);

		// Transient check
		Pass_IsTransient_Transient = utils_entity_lifetime::Get_IsTransientEntity(TransientEntity);
		Pass_IsTransient_Self = (utils_entity_lifetime::Get_IsTransientEntity(InHandle) == false);

		// World for entity
		auto EntityWorld = utils_entity_lifetime::Get_WorldForEntity(InHandle);
		Pass_WorldForEntity = ck::IsValid(EntityWorld);

		// Clean up transient entity (we don't need it beyond the test)
		utils_entity_lifetime::Request_DestroyEntity(TransientEntity);

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
		auto TitleText = "OWNERSHIP TREE (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		auto D = "";

		D = f"{D}--- Entity Creation ---\n";
		D = f"{D}" + (Pass_CreateChildA ? "[+] " : "[-] ") + f"CreateEntity ChildA: {ChildAString}\n";
		D = f"{D}" + (Pass_CreateChildB ? "[+] " : "[-] ") + f"CreateEntity ChildB: {ChildBString}\n";
		D = f"{D}" + (Pass_CreateTransient ? "[+] " : "[-] ") + f"CreateEntity_TransientOwner: {TransientString}\n\n";

		D = f"{D}--- Ownership Queries ---\n";
		D = f"{D}" + (Pass_OwnerOfChildA ? "[+] " : "[-] ") + "Owner(ChildA) == Self\n";
		D = f"{D}" + (Pass_OwnerOfChildB ? "[+] " : "[-] ") + "Owner(ChildB) == Self\n";
		D = f"{D}" + (Pass_DependentsCount ? "[+] " : "[-] ") + f"Dependents(Self).Num >= 2 (count: {DependentsCount})\n\n";

		D = f"{D}--- Transient & World ---\n";
		D = f"{D}" + (Pass_IsTransient_Transient ? "[+] " : "[-] ") + "IsTransient(transient) = true\n";
		D = f"{D}" + (Pass_IsTransient_Self ? "[+] " : "[-] ") + "IsTransient(self) = false\n";
		D = f"{D}" + (Pass_WorldForEntity ? "[+] " : "[-] ") + "WorldForEntity valid\n";

		auto AllPassed = Pass_CreateChildA && Pass_CreateChildB && Pass_CreateTransient
			&& Pass_OwnerOfChildA && Pass_OwnerOfChildB && Pass_DependentsCount
			&& Pass_IsTransient_Transient && Pass_IsTransient_Self && Pass_WorldForEntity;

		D = f"{D}\nRESULT: " + (AllPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");

		auto Instructions = "Creates parent-child entity hierarchy and queries\n"
			+ "ownership relationships, dependents, and transient state.";

		        Sample = FCkLifecycleGym_RowSample();
        Sample.BranchGeneration = BranchGeneration;
        Sample.Title = TitleText; Sample.Body = D; Sample.Instructions = Instructions;
        Sample.ActualCallbackCount = ActualCallbackCount;
        Sample.LastCompletedCycle = LastCompletedCycle;
        Sample.LastCycleCallbackCount = LastCycleCallbackCount;
        Sample.Results.Add(Pass_CreateChildA);
        Sample.Results.Add(Pass_CreateChildB);
        Sample.Results.Add(Pass_CreateTransient);
        Sample.Results.Add(Pass_OwnerOfChildA);
        Sample.Results.Add(Pass_OwnerOfChildB);
        Sample.Results.Add(Pass_DependentsCount);
        Sample.Results.Add(Pass_IsTransient_Transient);
        Sample.Results.Add(Pass_IsTransient_Self);
        Sample.Results.Add(Pass_WorldForEntity);
        Sample.Counters.Add(DependentsCount);
        if (!SharedAuthority) { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, D, Instructions); }
	}
}

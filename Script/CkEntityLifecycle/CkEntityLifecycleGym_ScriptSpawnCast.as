//============================================================================
// ENTITY LIFECYCLE GYM - SCRIPT SPAWN & CAST
// Tests: utils_entity_script, utils_pending_entity_script
//============================================================================

//----------------------------------------------------------------------------
// Helper entity script - minimal target for spawning
//----------------------------------------------------------------------------

class UCk_EntityScript_EntityLifecycleGym_SpawnTarget : UCk_GenericEntityScript_UE
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
		InHandle.Set_DebugName(n"SpawnTarget");
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_LifecycleGym_SpawnTarget");

		return ECk_EntityScript_ConstructionFlow::Finished;
	}
}

//----------------------------------------------------------------------------
// Main station entity script
//----------------------------------------------------------------------------

class UCk_EntityScript_EntityLifecycleGym_ScriptSpawnCast : UCkLifecycleGym_DemoBase
{
	default _Replication = ECk_Replication::DoesNotReplicate;

	UPROPERTY(ExposeOnSpawn)
	FTransform InitialTransform = FTransform::Identity;

	UPROPERTY() FCk_Handle_PendingEntityScript PendingTarget;
    UPROPERTY() FCk_Handle SpawnTarget;
	// Test results
	UPROPERTY() bool Pass_SpawnRequest = false;
	UPROPERTY() bool Pass_PendingIsValid = false;
	UPROPERTY() bool Pass_ConstructedCallback = false;
	UPROPERTY() bool Pass_Has = false;
	UPROPERTY() bool Pass_GetScriptClass = false;
	UPROPERTY() bool Pass_DoCast = false;
	UPROPERTY() bool Pass_DoCastChecked = false;
	UPROPERTY() bool Pass_ScriptInChain = false;

	// Display values
	FString ScriptClassName = "";
	UPROPERTY() bool TestsComplete = false;

	UFUNCTION(BlueprintOverride)
	ECk_EntityScript_ConstructionFlow DoConstruct(FCk_Handle& InHandle)
	{
	    auto _CkPerfScope = ck::ScopedStat();
        if (!CheckConstruct(InHandle)) { return ECk_EntityScript_ConstructionFlow::Finished; }
		InHandle.Set_DebugName(n"ScriptSpawnCast");
		utils_transform::Add(InHandle, InitialTransform, ECk_Replication::DoesNotReplicate);
		utils_entity_tag::Add(InHandle, n"TAG_LifecycleGym_ScriptSpawnCast");

		// Display tick
		if (!SharedAuthority) { utils_timer::Create_Tick(InHandle, FCk_Delegate_Timer(this, n"DisplayTick")); }

		// Spawn helper entity script
		auto SpawnParams = FCk_Gym_TransformSpawnParams(InitialTransform);
		auto PendingHandle = utils_entity_script::Request_SpawnEntity(
			InHandle,
			UCk_EntityScript_EntityLifecycleGym_SpawnTarget,
			FInstancedStruct::Make(SpawnParams));

		PendingTarget = PendingHandle;
		Pass_SpawnRequest = utils_pending_entity_script::Get_IsValid(PendingHandle);
        if (!Pass_SpawnRequest) { FailDemo("SpawnTarget request failed"); return ECk_EntityScript_ConstructionFlow::Finished; }
        Remember(PendingTarget.Get_EntityUnderConstruction());
		Pass_PendingIsValid = Pass_SpawnRequest;

		if (Pass_SpawnRequest)
		{
			// Register construction callback
			utils_pending_entity_script::Promise_OnConstructed(
				PendingHandle,
				FCk_Delegate_EntityScript_Constructed(this, n"OnSpawnTargetConstructed"));
		}

		return ECk_EntityScript_ConstructionFlow::Finished;
	}

	UFUNCTION()
	private void OnSpawnTargetConstructed(FCk_Handle_EntityScript InEntityScriptHandle)
	{
		auto Self = ck::ToEntity(this);
        auto Child = FCk_Handle(InEntityScriptHandle);
        if (!CanRun(Self)) { return; }
        if (!utils_pending_entity_script::Get_IsValid(PendingTarget) || PendingTarget.Get_EntityUnderConstruction() != Child || !CkLifecycleGym::IsDirect(Child, Self) || !utils_entity_script::Has(Child)) { FailDemo("SpawnTarget callback identity rejected"); return; }
        auto Actual = UCk_Utils_EntityScript_UE::TryGet_EntityScript(Child);
        if (ck::Is_NOT_Valid(Actual) || Actual.GetClass() != UCk_EntityScript_EntityLifecycleGym_SpawnTarget || TestsComplete) { FailDemo("SpawnTarget class or callback mismatch"); return; }
        SpawnTarget = Child; Pass_ConstructedCallback = true; ActualCallbackCount++; LastCompletedCycle = 0; LastCycleCallbackCount = 1;

		auto EntityHandle = FCk_Handle(InEntityScriptHandle);

		// Test: Has entity script
		Pass_Has = utils_entity_script::Has(EntityHandle);

		// Test: Get script class
		auto ScriptClass = utils_entity_script::Get_ScriptClass(InEntityScriptHandle);
		ScriptClassName = f"{ScriptClass}";
		Pass_GetScriptClass = ck::IsValid(ScriptClass);

		// Test: DoCast
		auto CastResult = utils_entity_script::DoCast(EntityHandle);
		Pass_DoCast = CastResult.IsSet();

		// Test: DoCastChecked
		if (!CastResult.IsSet()) { FailDemo("SpawnTarget cast rejected"); return; }
		auto CastCheckedResult = utils_entity_script::DoCastChecked(EntityHandle);
		Pass_DoCastChecked = utils_handle::Get_IsValid(FCk_Handle(CastCheckedResult));

		// Test: Find entity script in ownership chain
		auto ScriptInChain = utils_entity_script::TryGet_Entity_EntityScript_InOwnershipChain(EntityHandle);
		Pass_ScriptInChain = utils_handle::Get_IsValid(ScriptInChain);

		TestsComplete = true;
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
		auto TitleText = "SCRIPT SPAWN & CAST (" + CkGym_Common::Get_NetworkRoleTitle(SelfEntity) + ")";

		auto D = "";

		D = f"{D}--- utils_pending_entity_script ---\n";
		D = f"{D}" + (Pass_SpawnRequest ? "[+] " : "[-] ") + "Request_SpawnEntity valid\n";
		D = f"{D}" + (Pass_PendingIsValid ? "[+] " : "[-] ") + "Pending Get_IsValid\n";
		D = f"{D}" + (Pass_ConstructedCallback ? "[+] " : "[-] ") + "Promise_OnConstructed fired\n\n";

		D = f"{D}--- utils_entity_script ---\n";
		D = f"{D}" + (Pass_Has ? "[+] " : "[-] ") + "Has(entity)\n";
		D = f"{D}" + (Pass_GetScriptClass ? "[+] " : "[-] ") + f"Get_ScriptClass: {ScriptClassName}\n";
		D = f"{D}" + (Pass_DoCast ? "[+] " : "[-] ") + "DoCast succeeded\n";
		D = f"{D}" + (Pass_DoCastChecked ? "[+] " : "[-] ") + "DoCastChecked valid\n";
		D = f"{D}" + (Pass_ScriptInChain ? "[+] " : "[-] ") + "ScriptEntity in chain\n";

		if (TestsComplete)
		{
			auto AllPassed = Pass_SpawnRequest && Pass_PendingIsValid && Pass_ConstructedCallback
				&& Pass_Has && Pass_GetScriptClass && Pass_DoCast
				&& Pass_DoCastChecked && Pass_ScriptInChain;

			D = f"{D}\nRESULT: " + (AllPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
		}
		else
		{
			D = f"{D}\nWaiting for construction callback...";
		}

		auto Instructions = "Tests entity script spawning, construction callbacks,\n"
			+ "type queries, and handle casting.";

		        Sample = FCkLifecycleGym_RowSample();
        Sample.BranchGeneration = BranchGeneration;
        Sample.Title = TitleText; Sample.Body = D; Sample.Instructions = Instructions;
        Sample.ActualCallbackCount = ActualCallbackCount;
        Sample.LastCompletedCycle = LastCompletedCycle;
        Sample.LastCycleCallbackCount = LastCycleCallbackCount;
        Sample.Results.Add(Pass_SpawnRequest);
        Sample.Results.Add(Pass_PendingIsValid);
        Sample.Results.Add(Pass_ConstructedCallback);
        Sample.Results.Add(Pass_Has);
        Sample.Results.Add(Pass_GetScriptClass);
        Sample.Results.Add(Pass_DoCast);
        Sample.Results.Add(Pass_DoCastChecked);
        Sample.Results.Add(Pass_ScriptInChain);
        if (!SharedAuthority) { CkGym_Common::Update_StationDisplay(SelfEntity, TitleText, D, Instructions); }
	}
}

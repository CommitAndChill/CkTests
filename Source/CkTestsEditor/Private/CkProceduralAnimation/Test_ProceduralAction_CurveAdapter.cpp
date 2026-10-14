#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "CkProceduralAnimationEditor/CkProceduralActionAuthoringSession.h"
#include "CkProceduralAnimationEditor/CkProceduralActionEditorModel.h"
#include "CkProceduralAnimationEditor/CkProceduralAction_CurveAdapter.h"

#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"

namespace ck_procedural_action_curve_test
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    auto
        Body()
        -> FName
    { return FCk_ProceduralAction_PoseGroup::Get_BodyChannelName(); }

    auto
        MakeClip()
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage(), NAME_None,
                RF_Transient | RF_Transactional)};
        auto Phase = FCk_ProceduralAction_Phase{FName(TEXT("Move")), FCk_Time{1.0}};
        auto First = FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.25},
            FVector{10.0, 100.0, 0.0}, FQuat::Identity};
        auto Second = FCk_ProceduralAction_TransformKey{FCk_FloatRange_0to1{0.75},
            FVector{20.0, 200.0, 0.0}, FQuat::Identity};
        auto Keys = TArray<FCk_ProceduralAction_TransformKey>{};
        Keys.Add(First);
        Keys.Add(Second);
        Phase.Set_BodyKeys(Keys);
        auto Phases = TArray<FCk_ProceduralAction_Phase>{};
        Phases.Add(Phase);
        Clip->Set_Phases(Phases);
        return Clip;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_CurveAdapter_KeyIdentityAndUndo,
    "Ck.ProceduralAnimation.Action.Editor.CurveAdapter.KeyIdentityAndUndo",
    ck_procedural_action_curve_test::Flags)

auto
    FCkTest_ProceduralAction_CurveAdapter_KeyIdentityAndUndo::
    RunTest(
        const FString&)
    -> bool
{
    if (NOT TestNotNull(TEXT("editor transaction system"), GEditor))
    { return false; }
    auto Clip = ck_procedural_action_curve_test::MakeClip();
    auto Session = MakeShared<FCk_ProceduralActionAuthoringSession>(Clip.Get());
    auto Model = MakeShared<FCk_ProceduralActionEditorModel>();
    Model->SelectSection(FName(TEXT("Move")));
    auto Adapter = MakeShared<FCkProceduralAction_CurveAdapter>();
    Adapter->Init(Session, Model, FSimpleDelegate{});
    Adapter->Refresh();
    TestFalse(TEXT("proxy model owner is nontransactional"),
        Adapter->GetProxyOwner()->HasAnyFlags(RF_Transactional));
    auto* Curve = Adapter->FindCurve(ck_procedural_action_curve_test::Body(), 1);
    if (NOT TestNotNull(TEXT("the body's Y axis has a curve"), Curve))
    { return false; }
    TestNotNull(TEXT("the body has a curve per axis"), Adapter->FindCurve(ck_procedural_action_curve_test::Body(), 2));

    auto FirstHandle = FKeyHandle::Invalid();
    for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
    {
        if (FMath::IsNearlyEqual(Curve->GetKeyTime(*It), 0.25f))
        { FirstHandle = *It; break; }
    }
    if (NOT TestTrue(TEXT("first key has stable handle at its time in seconds"), FirstHandle != FKeyHandle::Invalid()))
    { return false; }
    {
        const auto Transaction = FScopedTransaction{FText::FromString(TEXT("Cross authored curve keys"))};
        Curve->SetKeyTime(FirstHandle, 0.9f);
        Adapter->OnCurveModified(ck_procedural_action_curve_test::Body(), 1);
    }

    const auto& Keys = Clip->Get_Phases()[0].Get_BodyKeys();
    if (NOT TestEqual(TEXT("crossing keeps two keys"), Keys.Num(), 2))
    { return false; }
    TestEqual(TEXT("earlier key retains its Y coordinate"), Keys[0].Get_Translation().Y, 200.0);
    TestEqual(TEXT("moved key retains its Y coordinate"), Keys[1].Get_Translation().Y, 100.0);
    TestTrue(TEXT("moved key has new time"),
        FMath::IsNearlyEqual(Keys[1].Get_Time().Get_Value(), 0.9, 1e-5));
    if (NOT TestTrue(TEXT("undo curve edit"), GEditor->UndoTransaction()))
    { return false; }
    TestEqual(TEXT("undo restores first Y coordinate"),
        Clip->Get_Phases()[0].Get_BodyKeys()[0].Get_Translation().Y, 100.0);
    TestEqual(TEXT("undo restores first time"),
        Clip->Get_Phases()[0].Get_BodyKeys()[0].Get_Time().Get_Value(), 0.25);
    Adapter->Refresh();
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_CurveAdapter_DeferredRejectClose,
    "Ck.ProceduralAnimation.Action.Editor.CurveAdapter.DeferredRejectClose",
    ck_procedural_action_curve_test::Flags)

auto
    FCkTest_ProceduralAction_CurveAdapter_DeferredRejectClose::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = ck_procedural_action_curve_test::MakeClip();
    auto Session = MakeShared<FCk_ProceduralActionAuthoringSession>(Clip.Get());
    auto Model = MakeShared<FCk_ProceduralActionEditorModel>();
    Model->SelectSection(FName(TEXT("Move")));
    TSharedPtr<FCkProceduralAction_CurveAdapter> Adapter = MakeShared<FCkProceduralAction_CurveAdapter>();
    Adapter->Init(Session, Model, FSimpleDelegate{});
    Adapter->Refresh();
    auto* MixedCurve = Adapter->FindCurve(ck_procedural_action_curve_test::Body(), 0);
    if (NOT TestNotNull(TEXT("the body's X axis has a curve"), MixedCurve))
    { return false; }
    const auto MixedHandle = *MixedCurve->GetKeyHandleIterator();
    MixedCurve->SetKeyInterpMode(MixedHandle, RCIM_Cubic, false);
    Adapter->OnCurveModified(ck_procedural_action_curve_test::Body(), 0);
    TestTrue(TEXT("mixed interpolation is diagnosed"),
        Adapter->GetStatusText().ToString().Contains(TEXT("Mixed or weighted")));
    TestEqual(TEXT("mixed interpolation leaves authored mode unchanged"),
        Clip->Get_Phases()[0].Get_BodyInterpolation(), ECk_ProceduralAction_Interpolation::Linear);
    Adapter.Reset();

    Adapter = MakeShared<FCkProceduralAction_CurveAdapter>();
    Adapter->Init(Session, Model, FSimpleDelegate{});
    Adapter->Refresh();
    auto* Curve = Adapter->FindCurve(ck_procedural_action_curve_test::Body(), 0);
    if (NOT TestNotNull(TEXT("the body's X axis has a curve after reopening"), Curve))
    { return false; }
    const auto Handle = *Curve->GetKeyHandleIterator();
    Curve->SetKeyTime(Handle, 1.5f);
    Adapter->OnCurveModified(ck_procedural_action_curve_test::Body(), 0);
    TestTrue(TEXT("an edit past the section reports that keys stay inside their section"),
        Adapter->GetStatusText().ToString().Contains(TEXT("inside their section")));
    TestTrue(TEXT("rebuild is deferred outside model callback"), Adapter->GetHasPendingRebuild());
    TestEqual(TEXT("rejected edit leaves authored key unchanged"),
        Clip->Get_Phases()[0].Get_BodyKeys()[0].Get_Time().Get_Value(), 0.25);
    const auto WeakAdapter = TWeakPtr<FCkProceduralAction_CurveAdapter>{Adapter};
    Adapter.Reset();
    FTSTicker::GetCoreTicker().Tick(0.0f);
    TestFalse(TEXT("closing with pending rebuild releases adapter"), WeakAdapter.IsValid());
    return NOT HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkTest_ProceduralAction_CurveAdapter_MalformedDraftLane,
    "Ck.ProceduralAnimation.Action.Editor.CurveAdapter.MalformedDraftLane",
    ck_procedural_action_curve_test::Flags)

auto
    FCkTest_ProceduralAction_CurveAdapter_MalformedDraftLane::
    RunTest(
        const FString&)
    -> bool
{
    auto Clip = ck_procedural_action_curve_test::MakeClip();
    auto Phases = Clip->Get_Phases();
    auto Keys = Phases[0].Get_BodyKeys();
    Keys.Swap(0, 1);
    Phases[0].Set_BodyKeys(Keys);
    Clip->Set_Phases(Phases);

    auto Session = MakeShared<FCk_ProceduralActionAuthoringSession>(Clip.Get());
    auto Model = MakeShared<FCk_ProceduralActionEditorModel>();
    Model->SelectSection(FName(TEXT("Move")));
    auto Adapter = MakeShared<FCkProceduralAction_CurveAdapter>();
    Adapter->Init(Session, Model, FSimpleDelegate{});
    Adapter->Refresh();
    TestTrue(TEXT("out-of-order draft displays a Details correction"),
        Adapter->GetStatusText().ToString().Contains(TEXT("correct this draft in Details")));
    TestNull(TEXT("invalid draft has no editable proxy curve"), Adapter->FindCurve(ck_procedural_action_curve_test::Body(), 0));
    TestEqual(TEXT("draft key order remains untouched"),
        Clip->Get_Phases()[0].Get_BodyKeys()[0].Get_Time().Get_Value(), 0.75);

    Keys.Swap(0, 1);
    Phases[0].Set_BodyKeys(Keys);
    Clip->Set_Phases(Phases);
    Adapter->Refresh();
    const auto* Corrected = Adapter->FindCurve(ck_procedural_action_curve_test::Body(), 0);
    TestTrue(TEXT("corrected draft obtains editable curve"), Corrected != nullptr && Corrected->GetNumKeys() == 2);
    TestFalse(TEXT("diagnostic clears after Details correction"),
        Adapter->GetStatusText().ToString().Contains(TEXT("correct this draft")));

    Keys[1].Set_Time(FCk_FloatRange_0to1{0.25});
    Phases[0].Set_BodyKeys(Keys);
    Clip->Set_Phases(Phases);
    Adapter->Refresh();
    TestNull(TEXT("duplicate-time draft has no editable proxy curve"), Adapter->FindCurve(ck_procedural_action_curve_test::Body(), 0));
    TestEqual(TEXT("duplicate-time draft remains authored"),
        Clip->Get_Phases()[0].Get_BodyKeys()[1].Get_Time().Get_Value(), 0.25);
    return NOT HasAnyErrors();
}

#endif

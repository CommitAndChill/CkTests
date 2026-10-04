#include "UI/CkPowerWheelProofModel.h"

#include "CkCore/Macros/CkMacros.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkPowerWheelModel_QueueAndRelease,
    "Ck.UiAuthoring.PowerWheel.Model.QueueAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPowerWheelModel_QueueAndRelease::RunTest(const FString&)
{
    FCkPowerWheelProofModel Model;
    const auto& Powers = Model.Get_Powers();
    if (NOT TestEqual(TEXT("fixture has sixteen powers"), Powers.Num(), 16)) { return false; }
    const float ShepardAngles[] = {25, 44, 63, 82, 101, 120, 139, 158};
    const float GarrusAngles[] = {-178, -158, -138, -118};
    const float LiaraAngles[] = {-62, -42, -22, -2};
    for (int32 Index = 0; Index < Powers.Num(); ++Index)
    {
        const int32 ExpectedActor = Index < 8 ? 0 : Index < 12 ? 1 : 2;
        const float ExpectedAngle = Index < 8 ? ShepardAngles[Index]
            : Index < 12 ? GarrusAngles[Index - 8] : LiaraAngles[Index - 12];
        TestEqual(TEXT("power actor follows sector"), Powers[Index].Actor, ExpectedActor);
        TestEqual(TEXT("power angle follows sector"), Powers[Index].Angle, ExpectedAngle);
    }
    TestTrue(TEXT("hold opens wheel"), Model.TryBegin());
    TestFalse(TEXT("double begin is rejected"), Model.TryBegin());
    TestTrue(TEXT("first Shepard power queues"), Model.TryQueue(TEXT("shepard-warp")));
    TestTrue(TEXT("second Shepard power replaces first"), Model.TryQueue(TEXT("shepard-throw")));
    TestTrue(TEXT("Garrus queues independently"), Model.TryQueue(TEXT("garrus-overload")));
    TestTrue(TEXT("Liara queues independently"), Model.TryQueue(TEXT("liara-warp")));
    TestTrue(TEXT("queued Shepard power is replacement"),
        Model.Get_QueuedKey(0) == FName{TEXT("shepard-throw")});
    TestTrue(TEXT("selected blocked powers still show descriptions"), Model.TrySelect(TEXT("liara-stasis")));
    TArray<FCkPowerWheelCommand> Commands;
    TestTrue(TEXT("release dispatches all queued actors together"), Model.TryRelease(Commands));
    TestEqual(TEXT("three commands returned"), Commands.Num(), 3);
    if (Commands.Num() == 3)
    {
        TestTrue(TEXT("Shepard replacement dispatched first"), Commands[0].Key == FName{TEXT("shepard-throw")} && Commands[0].Actor == 0);
        TestTrue(TEXT("Garrus dispatched second"), Commands[1].Key == FName{TEXT("garrus-overload")} && Commands[1].Actor == 1);
        TestTrue(TEXT("Liara dispatched third"), Commands[2].Key == FName{TEXT("liara-warp")} && Commands[2].Actor == 2);
    }
    TestFalse(TEXT("release closes wheel"), Model.Get_IsOpen());
    TestEqual(TEXT("dispatch count counts commands"), Model.Get_DispatchCount(), 3);
    TestTrue(TEXT("Shepard shared cooldown blocks other Shepard powers"),
        NOT Model.IsUsable(TEXT("shepard-warp")) && Model.Get_BlockReason(TEXT("shepard-warp")) == TEXT("Cooling down"));
    TestTrue(TEXT("Garrus shared cooldown blocks other Garrus powers"), NOT Model.IsUsable(TEXT("garrus-concussive-shot")));
    TestTrue(TEXT("Liara shared cooldown blocks other Liara powers"), NOT Model.IsUsable(TEXT("liara-stasis")));
    for (const auto& Power : Model.Get_Powers())
    {
        TestTrue(TEXT("shared actor cooldown never exceeds displayed duration"),
            Power.CooldownSeconds <= Power.CooldownDuration);
        TestTrue(TEXT("normalized actor cooldown stays within display range"),
            Model.Get_CooldownFraction(Power.Key) >= 0.0f && Model.Get_CooldownFraction(Power.Key) <= 1.0f);
    }
    TestEqual(TEXT("long Garrus cooldown is shared by shorter sibling"),
        Model.Get_Powers()[9].CooldownDuration, 6.0f);
    TestTrue(TEXT("wheel reopens during cooldown"), Model.TryBegin());
    TestFalse(TEXT("cooling power cannot queue"), Model.TryQueue(TEXT("shepard-warp")));
    const float Remaining = Model.Get_Powers()[0].CooldownSeconds;
    TestFalse(TEXT("time is frozen while held open"), Model.Advance(10.0f));
    TestEqual(TEXT("held-open cooldown remains unchanged"), Model.Get_Powers()[0].CooldownSeconds, Remaining);
    TestTrue(TEXT("cancel closes without dispatch"), Model.RequestCancel());
    TestEqual(TEXT("cancel preserves dispatch count"), Model.Get_DispatchCount(), 3);
    TestTrue(TEXT("closed time advances"), Model.Advance(10.0f));
    TestTrue(TEXT("cooldowns expire together"), Model.IsUsable(TEXT("shepard-warp"))
        && Model.IsUsable(TEXT("garrus-concussive-shot")) && Model.IsUsable(TEXT("liara-stasis")));
    TestEqual(TEXT("Garrus sibling recovers its authored recharge duration"),
        Model.Get_Powers()[9].CooldownDuration, 5.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkPowerWheelModel_AtomicFailures,
    "Ck.UiAuthoring.PowerWheel.Model.AtomicFailures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPowerWheelModel_AtomicFailures::RunTest(const FString&)
{
    FCkPowerWheelProofModel Model;
    TestFalse(TEXT("unknown key cannot select while closed"), Model.TrySelect(TEXT("missing")));
    TestFalse(TEXT("unknown key cannot queue while closed"), Model.TryQueue(TEXT("missing")));
    TArray<FCkPowerWheelCommand> ClosedCommands;
    TestFalse(TEXT("release while closed rejects"), Model.TryRelease(ClosedCommands));
    TestFalse(TEXT("negative delta rejects"), Model.Advance(-1.0f));
    TestFalse(TEXT("NaN delta rejects"), Model.Advance(std::numeric_limits<float>::quiet_NaN()));
    TestTrue(TEXT("failed time advances preserve fixture cooldown"), Model.Get_Powers()[0].CooldownSeconds == 0.0f);
    TestTrue(TEXT("hold opens"), Model.TryBegin());
    TestTrue(TEXT("blocked power can be selected for description"), Model.TrySelect(TEXT("shepard-throw")));
    TestTrue(TEXT("initially unshielded power can queue"), Model.TryQueueSelected());
    TestTrue(TEXT("Garrus queues in same transaction"), Model.TryQueue(TEXT("garrus-overload")));
    TestTrue(TEXT("target becomes shielded before release"), Model.SetTargetShielded(true));
    TArray<FCkPowerWheelCommand> Commands = {{FName{TEXT("sentinel")}, 2}};
    TestFalse(TEXT("stale shield-sensitive request rejects entire release"), Model.TryRelease(Commands));
    TestTrue(TEXT("rejected output preserved"), Commands.Num() == 1 && Commands[0].Key == FName{TEXT("sentinel")});
    TestFalse(TEXT("stale rejection closes wheel"), Model.Get_IsOpen());
    TestTrue(TEXT("stale rejection clears all queues"), Model.Get_QueuedKey(0).IsNone() && Model.Get_QueuedKey(1).IsNone());
    TestEqual(TEXT("stale rejection has zero dispatches"), Model.Get_DispatchCount(), 0);
    TestTrue(TEXT("stale rejection starts no actor cooldown"),
        Model.Get_Powers()[0].CooldownSeconds == 0.0f && Model.Get_Powers()[8].CooldownSeconds == 0.0f);
    TestTrue(TEXT("shielded blocker is explicit"), Model.Get_BlockReason(TEXT("shepard-throw")) == TEXT("Target is shielded"));
    TestFalse(TEXT("cancel after rejected release does nothing"), Model.RequestCancel());
    TestTrue(TEXT("reopen permits independent unblocked powers"), Model.TryBegin());
    TestTrue(TEXT("shielded power remains selectable for description"), Model.TrySelect(TEXT("shepard-throw")));
    TestFalse(TEXT("shielded power cannot queue"), Model.TryQueue(TEXT("shepard-throw")));
    TestFalse(TEXT("selected shielded power cannot queue"), Model.TryQueueSelected());
    TestTrue(TEXT("unblocked Garrus power can queue"), Model.TryQueue(TEXT("garrus-overload")));
    TestTrue(TEXT("cancel clears a valid pending power"), Model.RequestCancel());
    TestEqual(TEXT("cancel never dispatches"), Model.Get_DispatchCount(), 0);
    TestTrue(TEXT("cancel never starts cooldown"), Model.Get_Powers()[8].CooldownSeconds == 0.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkPowerWheelModel_Configuration,
    "Ck.UiAuthoring.PowerWheel.Model.Configuration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkPowerWheelModel_Configuration::RunTest(const FString&)
{
    FCkPowerWheelProofModel Model;
    auto Candidate = Model.Get_Powers();
    TestTrue(TEXT("fixture wheel opens"), Model.TryBegin());
    TestTrue(TEXT("fixture queue accepted"), Model.TryQueue(TEXT("garrus-overload")));
    auto Invalid = Candidate;
    Invalid[1].Key = Invalid[0].Key;
    TestFalse(TEXT("duplicate key rejects configuration"), Model.TryConfigure(Invalid));
    Invalid = Candidate;
    Invalid[2].Actor = 3;
    TestFalse(TEXT("out-of-range actor rejects configuration"), Model.TryConfigure(Invalid));
    Invalid = Candidate;
    Invalid[2].Angle = std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("non-finite angle rejects configuration"), Model.TryConfigure(Invalid));
    Invalid = Candidate;
    Invalid[2].Angle = Invalid[1].Angle;
    TestFalse(TEXT("duplicate angle rejects configuration"), Model.TryConfigure(Invalid));
    Invalid = Candidate;
    Invalid[0].Angle = 179.0f;
    TestFalse(TEXT("wrapped near-neighbor angle rejects configuration"), Model.TryConfigure(Invalid));
    Invalid = Candidate;
    Invalid[2].CooldownSeconds = Invalid[2].CooldownDuration + 1.0f;
    TestFalse(TEXT("invalid cooldown rejects configuration"), Model.TryConfigure(Invalid));
    Invalid = Candidate;
    Invalid[2].Label.Reset();
    TestFalse(TEXT("missing label rejects configuration"), Model.TryConfigure(Invalid));
    TestFalse(TEXT("empty configuration rejects"), Model.TryConfigure({}));
    Invalid = Candidate;
    while (Invalid.Num() < 25)
    {
        auto Extra = Invalid[0];
        Extra.Key = FName{*FString::Printf(TEXT("extra-%d"), Invalid.Num())};
        Invalid.Add(MoveTemp(Extra));
    }
    TestFalse(TEXT("more than 24 powers rejects"), Model.TryConfigure(Invalid));
    TestTrue(TEXT("all bad configurations preserve open wheel, queue and fixture"),
        Model.Get_IsOpen() && Model.Get_QueuedKey(1) == FName{TEXT("garrus-overload")}
            && Model.Get_Powers().Num() == 16 && Model.Get_Powers()[2].Angle == 63.0f);
    TestTrue(TEXT("valid replacement configures atomically"), Model.TryConfigure(Candidate));
    TestFalse(TEXT("valid replacement closes old hold"), Model.Get_IsOpen());
    TestTrue(TEXT("valid replacement clears queue"), Model.Get_QueuedKey(1).IsNone());
    auto Unavailable = Model.Get_Powers();
    Unavailable[0].Available = false;
    TestTrue(TEXT("unavailable fixture configuration accepted"), Model.TryConfigure(Unavailable));
    TestTrue(TEXT("availability gate reports explicit reason"),
        NOT Model.IsUsable(TEXT("shepard-warp")) && Model.Get_BlockReason(TEXT("shepard-warp")) == TEXT("Unavailable"));
    Model.ResetFixture();
    TestTrue(TEXT("reset restores default availability"), Model.IsUsable(TEXT("shepard-warp")));
    return true;
}

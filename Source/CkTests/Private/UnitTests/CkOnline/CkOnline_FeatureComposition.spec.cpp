#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "CkOnline/CkOnline_Host.h"
#include "CkOnline/CkOnline_Processor.h"
#include "CkEcs/Scheduler/CkProcessorGraph.h"
#include "CkEcs/Scheduler/CkProcessorTraits.inl.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Processor.h"

namespace ck_online_feature_test
{
    class FStubProcessor : public ck::TProcessor<FStubProcessor, ck::TReadWrite<ck::FFragment_OnlineService>>
    {
    public:
        using TProcessor::TProcessor;
        auto ForEachEntity(TimeType InDeltaTime, HandleType InHandle, ck::FFragment_OnlineService& InState) -> void {}
    };

    class FInvalidFeature final : public ck::IOnlineServiceFeature
    {
    public:
        int32 Mode = 0;
        int32 Shutdowns = 0;
        FCk_Handle_OnlineService Attached;
        auto TryAttach(FCk_Handle_OnlineService InService) -> bool override
        { Attached = InService; return Mode != 0; }
        auto Get_LocalProcessors() const -> TArray<ck::FProcessorDescriptor> override
        {
            if (Mode == 1) { return {}; }
            auto Descriptor = ck::BuildDescriptor<FStubProcessor>([](const FCk_Registry& InRegistry)
                -> ck::concepts::FTickableType { return FStubProcessor{InRegistry}; });
            Descriptor._Name = ck::Get_ProcessorCanonicalName<ck::FProcessor_OnlineService_Update>();
            if (Mode == 2) { Descriptor._Name = NAME_None; }
            if (Mode == 4)
            {
                Descriptor._Name = ck::Get_ProcessorCanonicalName<ck::FProcessor_EntityLifetime_DestroyEntity>();
            }
            return {Descriptor};
        }
        auto Dispatch() -> void override {}
        auto BeginShutdown() -> void override { ++Shutdowns; }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkOnlineFeature_AtomicComposition,
    "Ck.Online.Feature.AtomicCompositionRejectsInvalidGraph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkOnlineFeature_AtomicComposition::RunTest(const FString& Parameters)
{
    for (int32 Mode = 0; Mode < 5; ++Mode)
    {
        const auto Feature = MakeShared<ck_online_feature_test::FInvalidFeature>();
        Feature->Mode = Mode;
        const auto Host = ck::FOnlineServiceHost::TryCreateFeature(Feature, FCk_Time{1.0});
        TestFalse(FString::Printf(TEXT("Invalid feature mode %d is never published"), Mode), Host.IsValid());
        TestEqual(TEXT("Rejected composition closes the feature once"), Feature->Shutdowns, 1);
        TestFalse(TEXT("Rejected composition leaves no live service entity"),
            ck::IsValid(Feature->Attached, ck::IsValid_Policy_IncludePendingKill{}));
    }
    return true;
}
#endif

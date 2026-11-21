// The scene-node layers are generated from ck::MaxSceneNodeLayers, not listed by hand. This reads what actually reached
// the processor registry: one Update per layer, one QueueChildren per layer but the last, nothing past the last layer,
// each layer's Update ordered after the layer above it has published its children, and the registry receiving them in
// the order Update<0>, QueueChildren<0>, Update<1>, ... Update<Max - 1>.

#include "CkEcs/Scheduler/CkProcessorRegistry.h"
#include "CkEcs/Scheduler/CkProcessorTraits.inl.h"

#include "CkEcsExt/SceneNode/CkSceneNode_Processor.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"

#include "CkTests/CkTests_Log.h"

#include "../CkUnitTest_Common.h"

#include "Misc/AutomationTest.h"

#include <utility>

#if WITH_DEV_AUTOMATION_TESTS

namespace ck_scene_node_depth_registration_test
{
    struct FLayerNames
    {
        FName Update;
        // NAME_None for the last layer, which has no QueueChildren.
        FName QueueChildren;
    };

    template <int32 T_LayerIndex>
    auto
        Get_QueueChildrenName()
        -> FName
    {
        if constexpr (T_LayerIndex < ck::MaxSceneNodeLayers - 1)
        { return ck::Get_ProcessorCanonicalName<ck::TProcessor_SceneNode_QueueChildren<T_LayerIndex>>(); }
        else
        { return NAME_None; }
    }

    template <int32... T_LayerIndices>
    auto
        Get_LayerNames(
            std::integer_sequence<int32, T_LayerIndices...>)
        -> TArray<FLayerNames>
    {
        return TArray<FLayerNames>{
            FLayerNames{
                ck::Get_ProcessorCanonicalName<ck::TProcessor_SceneNode_Update<T_LayerIndices>>(),
                Get_QueueChildrenName<T_LayerIndices>()}...};
    }

    auto
        Find_DescriptorIndex(
            const TArray<ck::FProcessorDescriptor>& InDescriptors,
            FName InName)
        -> int32
    {
        return InDescriptors.IndexOfByPredicate([&](const ck::FProcessorDescriptor& InDescriptor)
        {
            return InDescriptor._Name == InName;
        });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCkSceneNode_Depth_EveryLayerIsRegisteredInOrder,
    "Ck.SceneNode.Depth.EveryLayerIsRegisteredInOrder",
    ck::tests::kCkUnitTestFlags)

auto
    FCkSceneNode_Depth_EveryLayerIsRegisteredInOrder::
    RunTest(
        const FString& InParameters)
    -> bool
{
    using namespace ck_scene_node_depth_registration_test;

    const auto MaxDepth = UCk_Utils_SceneNode_UE::Get_MaxDepth();
    TestEqual(TEXT("Get_MaxDepth is ck::MaxSceneNodeLayers"), MaxDepth, ck::MaxSceneNodeLayers);

    const auto& Descriptors = ck::FProcessorRegistry::Get().Get_AllDescriptors();
    const auto Layers = Get_LayerNames(ck::FSceneNodeLayerIndices{});

    auto PreviousRegistryIndex = int32{INDEX_NONE};
    auto RegistrationOrder = TArray<FString>{};

    for (auto LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
    {
        const auto UpdateIndex = Find_DescriptorIndex(Descriptors, Layers[LayerIndex].Update);
        if (NOT TestTrue(*FString::Printf(TEXT("Layer %d has its Update processor registered"), LayerIndex),
                UpdateIndex != INDEX_NONE))
        { continue; }

        const auto& Update = Descriptors[UpdateIndex];
        TestTrue(*FString::Printf(TEXT("Layer %d's Update registers after everything of the layer above"), LayerIndex),
            UpdateIndex > PreviousRegistryIndex);
        PreviousRegistryIndex = UpdateIndex;
        RegistrationOrder.Add(Layers[LayerIndex].Update.ToString());

        const auto IsLastLayer = LayerIndex == Layers.Num() - 1;
        TestEqual(*FString::Printf(TEXT("Only the last layer, %d, has no QueueChildren"), Layers.Num() - 1),
            Layers[LayerIndex].QueueChildren.IsNone(), IsLastLayer);

        if (NOT Layers[LayerIndex].QueueChildren.IsNone())
        {
            const auto QueueChildrenIndex = Find_DescriptorIndex(Descriptors, Layers[LayerIndex].QueueChildren);
            if (TestTrue(*FString::Printf(TEXT("Layer %d has its QueueChildren processor registered"), LayerIndex),
                    QueueChildrenIndex != INDEX_NONE))
            {
                TestTrue(*FString::Printf(TEXT("Layer %d's QueueChildren runs after its Update"), LayerIndex),
                    Descriptors[QueueChildrenIndex]._RunAfter.Contains(Layers[LayerIndex].Update));
                TestEqual(*FString::Printf(TEXT("Layer %d's QueueChildren registers right after its Update"), LayerIndex),
                    QueueChildrenIndex, UpdateIndex + 1);
                PreviousRegistryIndex = QueueChildrenIndex;
                RegistrationOrder.Add(Layers[LayerIndex].QueueChildren.ToString());
            }
        }

        if (LayerIndex == 0)
        {
            TestTrue(TEXT("Layer 0's Update runs after the root publish"),
                Update._RunAfter.Contains(ck::Get_ProcessorCanonicalName<ck::FProcessor_SceneNode_QueueRootChildren>()));
            TestTrue(TEXT("Layer 0's Update runs after the scene-node request drain"),
                Update._RunAfter.Contains(ck::Get_ProcessorCanonicalName<ck::FProcessor_SceneNode_HandleRequests>()));
        }
        else
        {
            TestTrue(*FString::Printf(TEXT("Layer %d's Update runs after layer %d's QueueChildren"), LayerIndex, LayerIndex - 1),
                Update._RunAfter.Contains(Layers[LayerIndex - 1].QueueChildren));
        }
    }

    auto RegisteredUpdates = 0;
    auto RegisteredQueueChildren = 0;
    for (const auto& Descriptor : Descriptors)
    {
        const auto Name = Descriptor._Name.ToString();
        RegisteredUpdates += Name.Contains(TEXT("TProcessor_SceneNode_Update<")) ? 1 : 0;
        RegisteredQueueChildren += Name.Contains(TEXT("TProcessor_SceneNode_QueueChildren<")) ? 1 : 0;
    }

    TestEqual(TEXT("Registered Update processors"), RegisteredUpdates, MaxDepth);
    TestEqual(TEXT("Registered QueueChildren processors"), RegisteredQueueChildren, MaxDepth - 1);

    UE_LOG(CkTests, Display, TEXT("[SceneNodeLayerRegistration] %s"), *FString::Join(RegistrationOrder, TEXT(" | ")));

    return true;
}

#endif

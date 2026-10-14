#pragma once

#include "CkProceduralAnimation/Action/CkProceduralAction_Data.h"
#include "CkProceduralAnimation/Action/CkProceduralAction_Fragment_Data.h"

#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// --------------------------------------------------------------------------------------------------------------------

namespace ck_test_procedural_action_clip_fixture
{
    constexpr auto NodDegrees = 1.0;

    // One phase that turns every slot's endpoint NodDegrees about its root and back, in each limb's rest-normalized
    // frame, so a straight limb along its rest endpoint stays on its reach sphere.
    inline auto
        MakeNodClip(
            const TArray<FName>& InSlots,
            FCk_Time InDuration)
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
        const auto Radians = FMath::DegreesToRadians(NodDegrees);
        const auto Nodded = FVector{FMath::Cos(Radians) - 1.0, FMath::Sin(Radians), 0.0};
        auto Slots = TArray<FCk_ProceduralAction_Slot>{};
        auto Tracks = TArray<FCk_ProceduralAction_LimbTrack>{};
        for (const auto SlotId : InSlots)
        {
            auto Slot = FCk_ProceduralAction_Slot{SlotId};
            Slot.Set_Space(ECk_ProceduralAction_TrackSpace::LimbRestNormalized);
            Slots.Add(Slot);
            auto Track = FCk_ProceduralAction_LimbTrack{SlotId};
            Track.Set_ReachPolicy(ECk_ProceduralAction_ReachPolicy::ProjectToReach);
            Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
                FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, FVector::ZeroVector},
                FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.5}, Nodded},
                FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, FVector::ZeroVector}});
            Tracks.Add(Track);
        }
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Nod")}, InDuration};
        Phase.Set_LimbTracks(Tracks);
        Clip->Set_Slots(Slots);
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Clip;
    }

    // One phase that holds the slot's endpoint at InPosition in InSpace; the orientation stays the captured one.
    inline auto
        MakeHoldClip(
            FName InSlot,
            ECk_ProceduralAction_TrackSpace InSpace,
            const FVector& InPosition,
            FCk_Time InDuration)
        -> TStrongObjectPtr<UCk_ProceduralAction_Data>
    {
        auto Clip = TStrongObjectPtr<UCk_ProceduralAction_Data>{
            NewObject<UCk_ProceduralAction_Data>(GetTransientPackage())};
        auto Slot = FCk_ProceduralAction_Slot{InSlot};
        Slot.Set_Space(InSpace);
        auto Track = FCk_ProceduralAction_LimbTrack{InSlot};
        Track.Set_ReachPolicy(ECk_ProceduralAction_ReachPolicy::ProjectToReach);
        Track.Set_PositionKeys(TArray<FCk_ProceduralAction_VectorKey>{
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{0.0}, InPosition},
            FCk_ProceduralAction_VectorKey{FCk_FloatRange_0to1{1.0}, InPosition}});
        auto Phase = FCk_ProceduralAction_Phase{FName{TEXT("Hold")}, InDuration};
        Phase.Set_LimbTracks(TArray<FCk_ProceduralAction_LimbTrack>{Track});
        Clip->Set_Slots(TArray<FCk_ProceduralAction_Slot>{Slot});
        Clip->Set_Phases(TArray<FCk_ProceduralAction_Phase>{Phase});
        return Clip;
    }

    // Binds slot i of the clip to limb i.
    inline auto
        MakePlay(
            UCk_ProceduralAction_Data* InClip,
            const TArray<FName>& InLimbs,
            ECk_ProceduralAction_Delivery InDelivery = ECk_ProceduralAction_Delivery::Explicit)
        -> FCk_ProceduralAction_PlaySpec
    {
        auto Bindings = TArray<FCk_ProceduralAction_SlotBinding>{};
        for (auto Index = 0; Index < InLimbs.Num() && Index < InClip->Get_Slots().Num(); ++Index)
        { Bindings.Add(FCk_ProceduralAction_SlotBinding{InClip->Get_Slots()[Index].Get_Id(), InLimbs[Index]}); }
        auto Play = FCk_ProceduralAction_PlaySpec{TSoftObjectPtr<UCk_ProceduralAction_Data>{InClip}};
        Play.Set_SlotBindings(Bindings);
        Play.Set_Delivery(InDelivery);
        return Play;
    }
}

// --------------------------------------------------------------------------------------------------------------------

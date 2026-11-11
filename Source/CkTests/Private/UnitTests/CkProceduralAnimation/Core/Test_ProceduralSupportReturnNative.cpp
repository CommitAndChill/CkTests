#include "../../CkUnitTest_Common.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/Processor/CkProcessor.h"
#include "CkEcs/Scheduler/CkProcessorRegistration.h"
#include "CkEcsExt/Transform/CkTransform_Utils.h"
#include "CkJolt/Body/CkJoltBody_Utils.h"
#include "CkProceduralAnimation/CkProceduralAnimation_Utils.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Control.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Fragment.h"
#include "CkProceduralAnimation/Articulation/CkProceduralArticulation_Utils.h"
#include "CkProceduralAnimation/BodyPose/CkProceduralBodyPose_Utils.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Fragment.h"
#include "CkProceduralAnimation/Gait/CkProceduralGait_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Utils.h"
#include "CkProceduralAnimation/Leg/CkProceduralLeg_Fragment.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Fragment.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Processor.h"
#include "CkProceduralAnimation/SurfaceMotion/CkSurfaceMotion_Utils.h"
#include "CkTests/Net/CkNetAutomation_Common.h"

#include <Engine/World.h>
#include <Misc/App.h>
#include <UObject/StrongObjectPtr.h>
#include <UObject/SoftObjectPath.h>
#include <UObject/StructOnScope.h>
#include <UObject/UnrealType.h>

namespace ck_test_support_return_native
{
    enum class EScenario : uint8 { IndependentFloor, OwnedFloor, ClaimedBody, NoPoseEnvelopeBudget };
    enum class EPhase : uint8 { MatureFloor, RotateBody, AddMotion, MatureMotion, Drive, Complete };

    struct FSetupFailureCopy
    {
        uint64 Frame = 0;
        bool RootKnown = false;
        uint64 RootNumber = 0;
        uint64 RootVersion = 0;
        uint64 InputSolve = 0;
        bool BodyKnown = false;
        FTransform Body = FTransform::Identity;
        bool UpKnown = false;
        FVector Up = FVector::ZeroVector;
        bool GaitMetadataKnown = false;
        int32 TrustedCount = -1;
        int32 PlaneState = -1;
        FVector PlaneNormal = FVector::ZeroVector;
        FCk_ProceduralLeg_Foot CurrentFeet[4]{};
        FCk_ProceduralLeg_Foot RetainedFeet[4]{};
        bool CurrentKnown[4]{};
        bool RetainedKnown[4]{};
    };

    struct FState
    {
        FAutomationTestBase* Test = nullptr;
        TWeakObjectPtr<UWorld> World;
        EScenario Scenario = EScenario::IndependentFloor;
        EPhase Phase = EPhase::MatureFloor;
        FVector Origin = FVector::ZeroVector;
        FCk_Handle_Transform Body;
        FCk_Handle_ProceduralGait Gait;
        FCk_Handle_SurfaceMotion Motion;
        FCk_Handle_ProceduralBodyPose Pose;
        FCk_Handle_ProceduralArticulation Subject;
        FCk_ArticulationControlToken ClaimToken{88217, 1};
        FCk_ArticulationChannelSet BodyChannels{TArray<FName>{}, true};
        uint64 RevisionBeforeClaim = 0;
        uint64 ClaimedRevision = 0;
        FCk_Handle FloorEntity;
        FCk_Handle WallEntity;
        FCk_Handle_JoltBody Floor;
        FCk_Handle_JoltBody Wall;
        TArray<FCk_Handle_ProceduralLeg> Legs;
        TArray<FCk_ProceduralLeg_Foot> RetainedFeet;
        TStrongObjectPtr<UCk_ProceduralRig_Data> Rig;
        TStrongObjectPtr<UCk_ProceduralGait_Data> Preset;
        TStrongObjectPtr<UObject> AsProof;
        FTransform WallBody = FTransform::Identity;
        FTransform BeforeBody = FTransform::Identity;
        FTransform CommittedBody = FTransform::Identity;
        FVector CommittedUp = FVector::ZeroVector;
        TArray<FCk_ProceduralLeg_Foot> BeforeFeet;
        uint64 BeforeFrame = 0;
        uint64 BeforeSolve = 0;
        uint64 CommittedFrame = 0;
        int32 PendingFrames = 0;
        int32 NativeRejectedFrames = 0;
        int32 NextAcceptedFrames = 0;
        int32 DriveFrames = 0;
        bool Failed = false;
        bool MatureMotionFailureCopied = false;
        bool NativeZeroCopied = false;
        bool SupportOutcomeCopied = false;
        bool ClaimTaken = false;
        bool FixedTime = false;
        bool OriginalFixedTime = false;
        double OriginalFixedDelta = 0.0;

        ~FState()
        {
            RestoreTime();
        }

        auto
        Get_IsBudgetScenario() const
            -> bool
        {
            return Scenario == EScenario::NoPoseEnvelopeBudget;
        }

        auto
        Get_UsesPose() const
            -> bool
        {
            return NOT Get_IsBudgetScenario();
        }

        auto
        RestoreTime()
            -> void
        {
            if (FixedTime)
            {
                FApp::SetUseFixedTimeStep(OriginalFixedTime);
                FApp::SetFixedDeltaTime(OriginalFixedDelta);
                FixedTime = false;
            }
        }

        auto
        Require(
            bool InCondition,
            const TCHAR* InMessage)
            -> bool
        {
            if (NOT InCondition)
            {
                Test->AddError(FString::Printf(TEXT("SupportReturnNative scenario%d phase%d: %s"),
                    static_cast<int32>(Scenario), static_cast<int32>(Phase), InMessage));
                Failed = true;
            }
            return InCondition;
        }

        auto
        ReadFeet() const
            -> TArray<FCk_ProceduralLeg_Foot>
        {
            auto Feet = TArray<FCk_ProceduralLeg_Foot>{};
            for (const auto& Leg : Legs)
            { Feet.Add(UCk_Utils_ProceduralLeg_UE::Get_Foot(Leg)); }
            return Feet;
        }

        static auto
        SameFeet(
            const TArray<FCk_ProceduralLeg_Foot>& InLeft,
            const TArray<FCk_ProceduralLeg_Foot>& InRight)
            -> bool
        {
            if (InLeft.Num() != InRight.Num())
            { return false; }
            for (auto Index = 0; Index < InLeft.Num(); ++Index)
            {
                const auto& Left = InLeft[Index];
                const auto& Right = InRight[Index];
                if (Left.Get_Position() != Right.Get_Position() || Left.Get_Normal() != Right.Get_Normal()
                    || Left.Get_Phase() != Right.Get_Phase() || Left.Get_Contact() != Right.Get_Contact())
                { return false; }
            }
            return true;
        }

        static auto
        SameState(
            const ck::FProceduralSurfaceMotionState& InLeft,
            const ck::FProceduralSurfaceMotionState& InRight)
            -> bool
        {
            return InLeft.Get_SupportNormal() == InRight.Get_SupportNormal()
                && InLeft.Get_SupportPoint() == InRight.Get_SupportPoint()
                && InLeft.Get_TravelTangent() == InRight.Get_TravelTangent()
                && InLeft.Get_StepForward() == InRight.Get_StepForward()
                && InLeft.Get_Velocity() == InRight.Get_Velocity()
                && InLeft.Get_MissingContact() == InRight.Get_MissingContact()
                && InLeft.Get_Grounded() == InRight.Get_Grounded()
                && InLeft.Get_ContactTrusted() == InRight.Get_ContactTrusted()
                && InLeft.Get_ContactSource() == InRight.Get_ContactSource()
                && InLeft.Get_CandidateNormal() == InRight.Get_CandidateNormal()
                && InLeft.Get_CandidatePoint() == InRight.Get_CandidatePoint()
                && InLeft.Get_CandidateSeen() == InRight.Get_CandidateSeen()
                && InLeft.Get_Obstruction() == InRight.Get_Obstruction()
                && InLeft.Get_ObstructionPoint() == InRight.Get_ObstructionPoint()
                && InLeft.Get_ObstructionNormal() == InRight.Get_ObstructionNormal()
                && InLeft.Get_ObstructionStandoff() == InRight.Get_ObstructionStandoff()
                && InLeft.Get_ClimbAcquisition().IsSet() == InRight.Get_ClimbAcquisition().IsSet()
                && (NOT InLeft.Get_ClimbAcquisition().IsSet()
                    || (InLeft.Get_ClimbAcquisition()->Get_Position() == InRight.Get_ClimbAcquisition()->Get_Position()
                        && InLeft.Get_ClimbAcquisition()->Get_Normal() == InRight.Get_ClimbAcquisition()->Get_Normal()
                        && InLeft.Get_ClimbAcquisition()->Get_Fraction() == InRight.Get_ClimbAcquisition()->Get_Fraction()
                        && InLeft.Get_ClimbAcquisition()->Get_Hit() == InRight.Get_ClimbAcquisition()->Get_Hit()));
        }

        auto
        HasReflectedNativeEnvelopeSource()
            -> bool
        {
            auto* Function = UCk_Utils_SurfaceMotion_UE::StaticClass()->FindFunctionByName(
                GET_FUNCTION_NAME_CHECKED(UCk_Utils_SurfaceMotion_UE, Get_ContactSource));
            if (NOT Require(Function != nullptr, TEXT("The reflected contact getter exists")))
            { return false; }
            auto* HandleProperty = FindFProperty<FStructProperty>(Function, TEXT("InHandle"));
            auto* ReturnProperty = CastField<FEnumProperty>(Function->GetReturnProperty());
            if (NOT Require(HandleProperty != nullptr && HandleProperty->Struct == FCk_Handle_SurfaceMotion::StaticStruct()
                    && ReturnProperty != nullptr, TEXT("The real reflected getter carries the typed handle and enum")))
            { return false; }
            auto Parameters = FStructOnScope{Function};
            HandleProperty->CopyCompleteValue(HandleProperty->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()), &Motion);
            UCk_Utils_SurfaceMotion_UE::StaticClass()->GetDefaultObject()->ProcessEvent(Function, Parameters.GetStructMemory());
            const auto Source = ReturnProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(
                ReturnProperty->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()));
            return Source == static_cast<uint8>(ECk_SurfaceMotion_ContactSource::NativeEnvelope);
        }

#if WITH_ANGELSCRIPT_CK
        auto
        HasAngelscriptNativeEnvelopeSource()
            -> bool
        {
            auto* Function = ck::IsValid(AsProof.Get()) ? AsProof->FindFunction(TEXT("CheckNativeEnvelope")) : nullptr;
            auto* Argument = Function != nullptr ? FindFProperty<FStructProperty>(Function, TEXT("InMotion")) : nullptr;
            const auto* Return = Function != nullptr ? CastField<FBoolProperty>(Function->GetReturnProperty()) : nullptr;
            if (NOT Require(Function != nullptr && Function->NumParms == 2 && Argument != nullptr
                    && Argument->Struct == FCk_Handle_SurfaceMotion::StaticStruct() && Argument->ArrayDim == 1
                    && Argument->HasAnyPropertyFlags(CPF_Parm) && NOT Argument->HasAnyPropertyFlags(CPF_ReturnParm)
                    && Return != nullptr, TEXT("The actual AS helper exposes exactly one typed SurfaceMotion input and a bool result")))
            { return false; }
            auto Parameters = FStructOnScope{Function};
            Argument->CopyCompleteValue(Argument->ContainerPtrToValuePtr<void>(Parameters.GetStructMemory()), &Motion);
            AsProof->ProcessEvent(Function, Parameters.GetStructMemory());
            return Return->GetPropertyValue_InContainer(Parameters.GetStructMemory());
        }
#endif

        auto
        HasFloorPlants() const
            -> bool
        {
            if (Legs.Num() != 4 || UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(Gait) != 4)
            { return false; }
            for (const auto& Foot : ReadFeet())
            {
                if (Foot.Get_Phase() != ECk_ProceduralLeg_FootPhase::Planted
                    || Foot.Get_Contact() != ECk_ProceduralLeg_FootContact::Trusted
                    || NOT FMath::IsNearlyEqual(Foot.Get_Position().Z, Origin.Z, 1.0e-3)
                    || NOT Foot.Get_Normal().Equals(FVector::UpVector, 1.0e-3))
                { return false; }
            }
            const auto FeetPlane = UCk_Utils_ProceduralGait_UE::Get_FeetPlane(Gait);
            return FeetPlane.Get_State() != ECk_ProceduralGait_FeetPlane::None
                && FeetPlane.Get_Normal().Equals(FVector::UpVector, 1.0e-3);
        }

        auto
        ReportMatureMotionFailure(
            bool InFloorPlantsKnown,
            bool InFloorPlants,
            bool InSameFeetKnown,
            bool InExactSameFeet,
            const FSetupFailureCopy& InCopy)
            -> void
        {
            if (MatureMotionFailureCopied)
            { return; }
            MatureMotionFailureCopied = true;
            const auto& BodyPoint = InCopy.Body.GetLocation();
            Test->AddInfo(FString::Printf(TEXT("SupportReturnSetupFailure scenario%d phase%d frame%llu rootKnown%d root%llu version%llu inputSolve%llu bodyKnown%d body(%.17g,%.17g,%.17g) upKnown%d up(%.17g,%.17g,%.17g) floorPlantsKnown%d floorPlants%d sameFeetKnown%d exactSameFeet%d gaitMetadataKnown%d trusted%d plane%d planeNormal(%.17g,%.17g,%.17g)"),
                static_cast<int32>(Scenario), static_cast<int32>(Phase), static_cast<unsigned long long>(InCopy.Frame),
                InCopy.RootKnown, static_cast<unsigned long long>(InCopy.RootNumber), static_cast<unsigned long long>(InCopy.RootVersion),
                static_cast<unsigned long long>(InCopy.InputSolve), InCopy.BodyKnown, BodyPoint.X, BodyPoint.Y, BodyPoint.Z,
                InCopy.UpKnown, InCopy.Up.X, InCopy.Up.Y, InCopy.Up.Z, InFloorPlantsKnown, InFloorPlants,
                InSameFeetKnown, InExactSameFeet, InCopy.GaitMetadataKnown, InCopy.TrustedCount, InCopy.PlaneState,
                InCopy.PlaneNormal.X, InCopy.PlaneNormal.Y, InCopy.PlaneNormal.Z));
            for (auto Index = 0; Index < 4; ++Index)
            {
                if (NOT InCopy.CurrentKnown[Index] || NOT InCopy.RetainedKnown[Index])
                {
                    Test->AddInfo(FString::Printf(TEXT("SupportReturnSetupFoot index%d currentKnown%d retainedKnown%d"),
                        Index, InCopy.CurrentKnown[Index], InCopy.RetainedKnown[Index]));
                    continue;
                }
                const auto& Current = InCopy.CurrentFeet[Index];
                const auto& Retained = InCopy.RetainedFeet[Index];
                const auto& CurrentPoint = Current.Get_Position();
                const auto& RetainedPoint = Retained.Get_Position();
                const auto& CurrentNormal = Current.Get_Normal();
                const auto& RetainedNormal = Retained.Get_Normal();
                Test->AddInfo(FString::Printf(TEXT("SupportReturnSetupFoot index%d currentPhase%d retainedPhase%d currentContact%d retainedContact%d currentPoint(%.17g,%.17g,%.17g) retainedPoint(%.17g,%.17g,%.17g) pointDelta(%.17g,%.17g,%.17g) currentNormal(%.17g,%.17g,%.17g) retainedNormal(%.17g,%.17g,%.17g) normalDelta(%.17g,%.17g,%.17g)"),
                    Index, static_cast<int32>(Current.Get_Phase()), static_cast<int32>(Retained.Get_Phase()),
                    static_cast<int32>(Current.Get_Contact()), static_cast<int32>(Retained.Get_Contact()),
                    CurrentPoint.X, CurrentPoint.Y, CurrentPoint.Z, RetainedPoint.X, RetainedPoint.Y, RetainedPoint.Z,
                    CurrentPoint.X - RetainedPoint.X, CurrentPoint.Y - RetainedPoint.Y, CurrentPoint.Z - RetainedPoint.Z,
                    CurrentNormal.X, CurrentNormal.Y, CurrentNormal.Z, RetainedNormal.X, RetainedNormal.Y, RetainedNormal.Z,
                    CurrentNormal.X - RetainedNormal.X, CurrentNormal.Y - RetainedNormal.Y, CurrentNormal.Z - RetainedNormal.Z));
            }
        }

        auto
        AddBox(
            FCk_Handle InOwner,
            const FVector& InCenter,
            const FVector& InHalfExtents,
            FCk_Handle& OutEntity,
            FCk_Handle_JoltBody& OutBody)
            -> bool
        {
            OutEntity = ck::IsValid(InOwner) ? UCk_Utils_EntityLifetime_UE::Request_CreateEntity(InOwner)
                : UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World.Get());
            if (NOT Require(ck::IsValid(OutEntity), TEXT("Public terrain lifetime entity composes")))
            { return false; }
            const auto Transform = UCk_Utils_Transform_UE::Add(OutEntity, FTransform{Origin + InCenter},
                ECk_Replication::DoesNotReplicate);
            if (NOT Require(ck::IsValid(Transform), TEXT("Public terrain transform composes")))
            { return false; }
            auto Shape = FCk_Jolt_ShapeDimensions{ECk_Jolt_ShapeType::Box};
            Shape.Set_HalfExtents(InHalfExtents);
            auto Spec = FCk_JoltBody_Spec{ECk_JoltBody_ShapeSource::ExplicitShape};
            Spec.Set_ShapeDimensions(Shape).Set_MotionType(ECk_MotionType::Static).Set_CollisionProfileName(TEXT("BlockAll"));
            OutBody = UCk_Utils_JoltBody_UE::Add(OutEntity, Spec);
            return Require(ck::IsValid(OutBody), TEXT("Attributed native terrain uses the public body API"));
        }

        auto
        Compose()
            -> void
        {
#if WITH_ANGELSCRIPT_CK
            if (Scenario == EScenario::IndependentFloor)
            {
                auto* ProofClass = FSoftClassPath{TEXT("/Script/Angelscript.Ck_ProceduralSupportReturnProof")}.TryLoadClass<UObject>();
                if (NOT Require(ProofClass != nullptr && NOT ProofClass->HasAnyClassFlags(CLASS_Abstract),
                        TEXT("The native commit fixture requires its real compiled AngelScript helper")))
                { return; }
                AsProof.Reset(NewObject<UObject>(World.Get(), ProofClass));
                if (NOT Require(ck::IsValid(AsProof.Get()), TEXT("The reflected AS helper object is constructed before native movement")))
                { return; }
            }
#endif
            OriginalFixedTime = FApp::UseFixedTimeStep();
            OriginalFixedDelta = FApp::GetFixedDeltaTime();
            FApp::SetFixedDeltaTime(Get_IsBudgetScenario() ? 0.2 : 1.0 / 90.0);
            FApp::SetUseFixedTimeStep(true);
            FixedTime = true;
            auto Root = UCk_Utils_EntityLifetime_UE::Request_CreateEntity_TransientOwner(World.Get());
            Body = UCk_Utils_Transform_UE::Add(Root, FTransform{Origin + FVector{0.0, 0.0, 64.0}},
                ECk_Replication::DoesNotReplicate);
            if (NOT Require(ck::IsValid(Body), TEXT("Public walker transform composes")))
            { return; }
            if (NOT AddBox(Scenario == EScenario::OwnedFloor ? Body.ConvertToHandle() : FCk_Handle{},
                    FVector{-170.0, 0.0, -20.0}, FVector{230.0, 200.0, 20.0}, FloorEntity, Floor))
            { return; }
            auto Specs = TArray<FCk_ProceduralLeg_Spec>{};
            auto Chains = TArray<FCk_ProceduralWalker_LegChain>{};
            for (auto Index = 0; Index < 4; ++Index)
            {
                const auto Front = Index < 2;
                const auto Side = Index % 2 == 0 ? -1.0 : 1.0;
                const auto Id = FName{*FString::Printf(TEXT("SupportLeg%d"), Index)};
                auto Spec = FCk_ProceduralLeg_Spec{Id,
                    FCk_ProceduralLeg_Placement{FVector{Front ? 20.0 : -20.0, Side * 20.0, 0.0},
                        FVector{Front ? 55.0 : 30.0, Side * 45.0, -64.0}},
                    FCk_ProceduralLeg_ChainGeometry{TArray<float>{100.0f, 100.0f}}};
                Spec.Set_FootContactRadius(2.0f);
                Specs.Add(Spec);
                auto Parts = TArray<FCk_Handle_Transform>{};
                for (auto Part = 0; Part < 2; ++Part)
                {
                    auto PartEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Body);
                    Parts.Add(UCk_Utils_Transform_UE::Add(PartEntity, FTransform{Origin}, ECk_Replication::DoesNotReplicate));
                }
                auto Chain = FCk_ProceduralRig_Spec{Parts};
                auto FootEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Body);
                Chain.Set_Foot(UCk_Utils_Transform_UE::Add(FootEntity, FTransform{Origin}, ECk_Replication::DoesNotReplicate));
                Chains.Emplace(Id, Chain);
            }
            Rig.Reset(NewObject<UCk_ProceduralRig_Data>());
            Rig->Set_Legs(Specs);
            Preset.Reset(NewObject<UCk_ProceduralGait_Data>());
            auto Step = Preset->Get_Step();
            Step.Set_Threshold(1000.0f);
            Preset->Set_Step(Step);
            if (Get_IsBudgetScenario())
            {
                auto Timing = Preset->Get_Timing();
                Timing.Set_StepDuration(FCk_Time{4.0});
                Preset->Set_Timing(Timing);
            }
            Gait = UCk_Utils_ProceduralAnimation_UE::Add_Walker(Body,
                FCk_ProceduralWalker_Spec{Rig.Get(), Preset.Get()}.Set_Chains(Chains)).Get_Gait();
            if (NOT Require(ck::IsValid(Gait), TEXT("Public walker composes without private solver or plant injection")))
            { return; }
            Subject = UCk_Utils_ProceduralArticulation_UE::Cast(Body);
            for (auto Index = 0; Index < 4; ++Index)
            {
                const auto Leg = UCk_Utils_ProceduralLeg_UE::TryGet_Leg(Body,
                    FName{*FString::Printf(TEXT("SupportLeg%d"), Index)});
                if (NOT Require(ck::IsValid(Leg), TEXT("Every public walker leg is present")))
                { return; }
                Legs.Add(Leg);
            }
            Gait.Add<ck::FFragment_ProceduralBodyPreparation_Test>();
        }

        auto
        ObserveBefore(
            double InDt)
            -> void
        {
            if (Failed || Phase == EPhase::Complete
                || UCk_Utils_ProceduralGait_UE::Get_Status(Gait) != ECk_ProceduralAnimation_Status::Ready)
            { return; }
            if (Get_IsBudgetScenario())
            {
                if (NOT Require(FMath::IsNearlyEqual(InDt, 0.2, 1.0e-6),
                        TEXT("The public no-pose clock requests thirteen original integration substeps")))
                { return; }
            }
            else if (NOT Require(FMath::IsNearlyEqual(InDt, 1.0 / 90.0, 1.0e-6),
                    TEXT("The scoped fixed update has exactly one existing integration substep")))
            { return; }
            const auto Current = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
            if (Phase == EPhase::MatureFloor)
            {
                if (NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Floor) || NOT HasFloorPlants())
                { return; }
                RetainedFeet = ReadFeet();
                WallBody = FTransform{FQuat{FVector::RightVector, UE_PI * 0.5}, Origin + FVector{64.0, 0.0, 64.0}};
                UCk_Utils_Transform_UE::Request_SetTransform(Body, FCk_Request_Transform_SetTransform{WallBody}, {});
                Phase = EPhase::RotateBody;
                return;
            }
            if (Phase == EPhase::RotateBody)
            {
                if (NOT Current.Equals(WallBody, 1.0e-6))
                { return; }
                if (NOT AddBox({}, FVector{-5.0, 0.0, 100.0}, FVector{5.0, 200.0, 300.0}, WallEntity, Wall))
                { return; }
                if (Get_UsesPose())
                {
                    auto PresentationEntity = UCk_Utils_EntityLifetime_UE::Request_CreateEntity(Body);
                    const auto Presentation = UCk_Utils_Transform_UE::Add(PresentationEntity, WallBody,
                        ECk_Replication::DoesNotReplicate);
                    auto PoseSpec = FCk_ProceduralBodyPose_Spec{Presentation};
                    auto Spring = PoseSpec.Get_Spring();
                    Spring.Set_MaxAttitudeLag(0.0f);
                    PoseSpec.Set_Spring(Spring);
                    Pose = UCk_Utils_ProceduralBodyPose_UE::Add(Gait, PoseSpec);
                    if (NOT Require(ck::IsValid(Pose), TEXT("A fresh public presentation pose composes after the authored rotation")))
                    { return; }
                }
                Phase = EPhase::AddMotion;
                return;
            }
            if (Phase == EPhase::AddMotion)
            {
                if ((Get_UsesPose() && UCk_Utils_ProceduralBodyPose_UE::Get_Status(Pose) != ECk_ProceduralAnimation_Status::Ready)
                    || NOT UCk_Utils_JoltBody_UE::Get_IsBodyAdded(Wall))
                { return; }
                auto Spec = FCk_SurfaceMotion_Spec{};
                auto Contact = Spec.Get_Contact();
                Contact.Set_Clearance(64.0f).Set_ProbeReach(256.0f)
                    .Set_HeightSource(ECk_SurfaceMotion_HeightSource::PlantedFeet);
                if (Scenario == EScenario::ClaimedBody)
                { Contact.Set_ConfirmTime(FCk_Time{0.5}); }
                Spec.Set_Contact(Contact).Set_AdaptiveHeight(FCk_SurfaceMotion_AdaptiveHeight{}.Set_Enabled(true)
                    .Set_BodyHalfExtents(FVector{50.0, 28.0, 22.0}).Set_CollisionMargin(1.0f));
                Motion = UCk_Utils_SurfaceMotion_UE::Add(Body, Spec);
                if (NOT Require(ck::IsValid(Motion), TEXT("Surface motion is publicly added with its actual authored wall orientation")))
                { return; }
                Phase = EPhase::MatureMotion;
                return;
            }
            if (Phase == EPhase::MatureMotion)
            {
                if (UCk_Utils_SurfaceMotion_UE::Get_Status(Motion) != ECk_ProceduralAnimation_Status::Ready
                    || UCk_Utils_SurfaceMotion_UE::Get_Support(Motion) != ECk_SurfaceMotion_Support::Grounded
                    || NOT UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion).Equals(FVector::ForwardVector, 1.0e-3))
                { return; }
                auto LegMetadataReady = Legs.Num() == 4;
                for (auto Index = 0; Index < FMath::Min(Legs.Num(), 4); ++Index)
                {
                    if (ck::Is_NOT_Valid(Legs[Index]) || NOT UCk_Utils_ProceduralLeg_UE::Has(Legs[Index])
                        || NOT Legs[Index].Has<ck::FFragment_ProceduralLeg_Params>())
                    { LegMetadataReady = false; break; }
                }
                const auto GaitMetadataReady = ck::IsValid(Gait) && UCk_Utils_ProceduralGait_UE::Has(Gait)
                    && Gait.Has<ck::FFragment_ProceduralGait_Tunables>() && Gait.Has<ck::FFragment_ProceduralGait_FeetPlane>()
                    && UCk_Utils_ProceduralGait_UE::Get_Status(Gait) == ECk_ProceduralAnimation_Status::Ready;
                const auto FloorPlantsKnown = GaitMetadataReady && LegMetadataReady;
                const auto FloorPlants = FloorPlantsKnown && HasFloorPlants();
                auto CurrentFeet = TArray<FCk_ProceduralLeg_Foot>{};
                auto SameFeetKnown = false;
                auto ExactSameFeet = false;
                if (FloorPlants)
                {
                    CurrentFeet = ReadFeet();
                    SameFeetKnown = true;
                    ExactSameFeet = SameFeet(RetainedFeet, CurrentFeet);
                }
                if (NOT Require(FloorPlants && ExactSameFeet,
                        TEXT("The quarter-turned production walker retains all four genuine floor plants")))
                {
                    auto FailureCopy = FSetupFailureCopy{};
                    FailureCopy.Frame = GFrameCounter;
                    if (GaitMetadataReady)
                    {
                        FailureCopy.RootKnown = true;
                        FailureCopy.RootNumber = Gait.Get_Entity().Get_EntityNumber();
                        FailureCopy.RootVersion = Gait.Get_Entity().Get_VersionNumber();
                        FailureCopy.InputSolve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
                        if (LegMetadataReady)
                        {
                            FailureCopy.GaitMetadataKnown = true;
                            FailureCopy.TrustedCount = UCk_Utils_ProceduralGait_UE::Get_TrustedContactCount(Gait);
                            const auto Plane = UCk_Utils_ProceduralGait_UE::Get_FeetPlane(Gait);
                            FailureCopy.PlaneState = static_cast<int32>(Plane.Get_State());
                            FailureCopy.PlaneNormal = Plane.Get_Normal();
                        }
                    }
                    if (ck::IsValid(Body) && UCk_Utils_Transform_UE::Has(Body))
                    {
                        FailureCopy.BodyKnown = true;
                        FailureCopy.Body = UCk_Utils_Transform_UE::Get_EntityCurrentTransform(Body);
                    }
                    if (ck::IsValid(Motion) && UCk_Utils_SurfaceMotion_UE::Has(Motion)
                        && Motion.Has<ck::FFragment_SurfaceMotion_Params>() && Motion.Has<ck::FFragment_SurfaceMotion_Support>()
                        && UCk_Utils_SurfaceMotion_UE::Get_Status(Motion) == ECk_ProceduralAnimation_Status::Ready)
                    {
                        FailureCopy.UpKnown = true;
                        FailureCopy.Up = UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion);
                    }
                    for (auto Index = 0; Index < 4; ++Index)
                    {
                        if (RetainedFeet.IsValidIndex(Index))
                        {
                            FailureCopy.RetainedKnown[Index] = true;
                            FailureCopy.RetainedFeet[Index] = RetainedFeet[Index];
                        }
                        if (CurrentFeet.IsValidIndex(Index))
                        {
                            FailureCopy.CurrentKnown[Index] = true;
                            FailureCopy.CurrentFeet[Index] = CurrentFeet[Index];
                        }
                        else if (Legs.IsValidIndex(Index) && ck::IsValid(Legs[Index])
                            && UCk_Utils_ProceduralLeg_UE::Has(Legs[Index])
                            && Legs[Index].Has<ck::FFragment_ProceduralLeg_Params>())
                        {
                            FailureCopy.CurrentKnown[Index] = true;
                            FailureCopy.CurrentFeet[Index] = UCk_Utils_ProceduralLeg_UE::Get_Foot(Legs[Index]);
                        }
                    }
                    ReportMatureMotionFailure(FloorPlantsKnown, FloorPlants, SameFeetKnown, ExactSameFeet, FailureCopy);
                    return;
                }
                UCk_Utils_SurfaceMotion_UE::Request_Steering(Motion,
                    FCk_Request_SurfaceMotion_Steering{FVector::ForwardVector, 250.0f}, {});
                Phase = EPhase::Drive;
                return;
            }
            ++DriveFrames;
            if (NOT Require(UCk_Utils_SurfaceMotion_UE::Get_Status(Motion) == ECk_ProceduralAnimation_Status::Ready
                    && (NOT Get_UsesPose() || UCk_Utils_ProceduralBodyPose_UE::Get_Status(Pose) == ECk_ProceduralAnimation_Status::Ready),
                    TEXT("Native body and actual presentation remain ready through the support transaction")))
            { return; }
            if (Scenario == EScenario::ClaimedBody && PendingFrames > 0 && NOT ClaimTaken)
            {
                if (NOT Require(ck::IsValid(Subject)
                        && UCk_Utils_ProceduralArticulation_UE::Get_Status(Subject) == ECk_ProceduralAnimation_Status::Ready
                        && Subject.Has<ck::FFragment_ProceduralArticulation_Claims>(),
                        TEXT("The actual pending observation precedes a complete ready articulation Body claim")))
                { return; }
                RevisionBeforeClaim = Subject.Get<ck::FFragment_ProceduralArticulation_Claims>().Get_Revision();
                if (NOT Require(FCk_ProceduralArticulation_Control::TryReserveClaims(Subject, ClaimToken, BodyChannels)
                        == ECk_ArticulationControlResult::Accepted, TEXT("A real Body-only token reserves through the public claim protocol")))
                { return; }
                ClaimTaken = true;
                ClaimedRevision = Subject.Get<ck::FFragment_ProceduralArticulation_Claims>().Get_Revision();
                constexpr auto BodyChannel = true;
                if (NOT Require(ClaimedRevision > RevisionBeforeClaim
                        && FCk_ProceduralArticulation_Control::GetClaimant(Subject, NAME_None, BodyChannel) == ClaimToken,
                        TEXT("The Body owner and exact current ClaimsRevision really changed after native pending")))
                { return; }
            }
            BeforeBody = Current;
            BeforeFeet = ReadFeet();
            BeforeFrame = GFrameCounter;
            BeforeSolve = Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence();
        }

        auto
        ObserveAfter()
            -> void
        {
            if (Failed || Phase != EPhase::Drive || BeforeFrame != GFrameCounter)
            { return; }
            const auto& Receipt = Gait.Get<ck::FFragment_ProceduralBodyPreparation_Test>();
            const auto& Support = Gait.Get<ck::FFragment_SurfaceMotion_Support>();
            if (NOT Require(Receipt.Frame == BeforeFrame && Support.Get_EvaluatedFrame() == BeforeFrame
                    && Gait.Get<ck::FFragment_ProceduralGait>().Get_SolveSequence() == BeforeSolve,
                    TEXT("The receipt is the actual same-frame same-input-solve Surface update")))
            { return; }
            if (NOT Require(SameFeet(BeforeFeet, ReadFeet()) && SameFeet(RetainedFeet, ReadFeet()),
                    TEXT("Surface return never moves or replaces retained physical plants")))
            { return; }
            const auto FirstZero = Receipt.SupportZeroAccepted && NOT NativeZeroCopied;
            const auto FirstOutcome = NOT SupportOutcomeCopied && (Receipt.SupportBudgetDeclined
                || (Receipt.SupportEligible && NOT Receipt.SupportPending));
            if (FirstZero || FirstOutcome)
            {
                NativeZeroCopied |= FirstZero;
                SupportOutcomeCopied |= FirstOutcome;
                Test->AddInfo(FString::Printf(TEXT("SupportReturnNativeOutcome scenario%d frame%llu solve%llu drive%d firstZero%d firstOutcome%d zero%d eligible%d room%d pending%d pendingRetained%d committed%d physical%d landings%d envelope%d budgetDeclined%d claimTaken%d trials%d rays%d envelopes%d substeps%d remaining%d proofQueries%d"),
                    static_cast<int32>(Scenario), BeforeFrame, BeforeSolve, DriveFrames, FirstZero, FirstOutcome,
                    Receipt.SupportZeroAccepted, Receipt.SupportEligible, Receipt.SupportRoom, Receipt.SupportPending,
                    Receipt.SupportPendingRetained, Receipt.SupportCommitted, Receipt.SupportPhysical,
                    Receipt.SupportLandings, Receipt.SupportEnvelope, Receipt.SupportBudgetDeclined, ClaimTaken,
                    Receipt.SupportTrials, Receipt.SupportRays, Receipt.SupportEnvelopes,
                    Receipt.SupportAcceptedSubsteps, Receipt.SupportRemainingSubsteps, Receipt.SupportProofQueries));
                if (Receipt.SupportZeroAccepted)
                {
                    const auto Before = Receipt.SupportBefore.GetLocation();
                    const auto After = Receipt.SupportAfter.GetLocation();
                    const auto Rotation = Receipt.SupportBefore.GetRotation();
                    Test->AddInfo(FString::Printf(TEXT("SupportReturnNativeBody bodyKnown1 candidateKnown%d before(%.17g,%.17g,%.17g) beforeQuat(%.17g,%.17g,%.17g,%.17g) after(%.17g,%.17g,%.17g) hitKnown1 hit%d"),
                        Receipt.SupportCommitted, Before.X, Before.Y, Before.Z,
                        Rotation.X, Rotation.Y, Rotation.Z, Rotation.W, After.X, After.Y, After.Z,
                        Receipt.SupportSweep.Get_HasHit()));
                    if (Receipt.SupportSweep.Get_HasHit())
                    {
                        const auto Point = Receipt.SupportSweep.Get_Position();
                        const auto Normal = Receipt.SupportSweep.Get_Normal();
                        Test->AddInfo(FString::Printf(TEXT("SupportReturnNativeHit fraction%.17g floor%d point(%.17g,%.17g,%.17g) normal(%.17g,%.17g,%.17g)"),
                            Receipt.SupportSweep.Get_Fraction(), Receipt.SupportSweep.Get_Entity() == FloorEntity,
                            Point.X, Point.Y, Point.Z, Normal.X, Normal.Y, Normal.Z));
                    }
                }
            }
            if (Receipt.SupportPending)
            {
                ++PendingFrames;
                Require(Receipt.SupportEligible && Receipt.SupportRoom && NOT Receipt.SupportCommitted
                    && Receipt.SupportZeroAccepted && Receipt.SupportSweep.Get_Entity() == FloorEntity
                    && Receipt.SupportBefore.Equals(Receipt.SupportAfter, 1.0e-9)
                    && SameState(Receipt.SupportStateBefore, Receipt.SupportStateAfter)
                    && Support.Get_EvaluatedBody().Equals(BeforeBody, 1.0e-3)
                    && UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion).Equals(FVector::ForwardVector, 1.0e-3),
                    TEXT("Confirmation-only frames retain the actual accepted zero body and old support"));
            }
            if (Scenario == EScenario::OwnedFloor)
            {
                Require(NOT Receipt.SupportEligible && NOT Receipt.SupportPending && NOT Receipt.SupportCommitted,
                    TEXT("A walker-owned native collider grants no independent support-return authority"));
                if (UCk_Utils_SurfaceMotion_UE::Get_ReachPaceState(Motion) == ECk_SurfaceMotion_ReachPaceState::Blocked
                    && Receipt.SupportZeroAccepted && Receipt.SupportSweep.Get_HasHit()
                    && Receipt.SupportSweep.Get_Fraction() > 0.0f && Receipt.SupportSweep.Get_Fraction() <= 1.0f
                    && Receipt.SupportSweep.Get_Normal().Equals(FVector::UpVector, 1.0e-3)
                    && Receipt.SupportSweep.Get_Entity() == FloorEntity
                    && SameState(Receipt.SupportStateBefore, Receipt.SupportStateAfter)
                    && Receipt.SupportBefore.Equals(Receipt.SupportAfter, 1.0e-9)
                    && Support.Get_EvaluatedBody().Equals(BeforeBody, 1.0e-3))
                { ++NativeRejectedFrames; }
                if (NativeRejectedFrames >= 3)
                { Phase = EPhase::Complete; }
                return;
            }
            const auto ActualFloorZero = Receipt.SupportZeroAccepted && Receipt.SupportSweep.Get_HasHit()
                && Receipt.SupportSweep.Get_Fraction() > 0.0f && Receipt.SupportSweep.Get_Fraction() <= 1.0f
                && Receipt.SupportSweep.Get_Normal().Equals(FVector::UpVector, 1.0e-3)
                && Receipt.SupportSweep.Get_Entity() == FloorEntity
                && Receipt.SupportBefore.Equals(Receipt.SupportAfter, 1.0e-9)
                && SameState(Receipt.SupportStateBefore, Receipt.SupportStateAfter)
                && Receipt.SupportStateBefore.Get_Grounded() && Receipt.SupportStateBefore.Get_ContactTrusted()
                && Receipt.SupportStateBefore.Get_SupportNormal().Equals(FVector::ForwardVector, 1.0e-3)
                && Support.Get_EvaluatedBody().Equals(BeforeBody, 1.0e-3);
            if (Scenario == EScenario::ClaimedBody)
            {
                if (ClaimTaken)
                {
                    constexpr auto BodyChannel = true;
                    if (NOT Require(PendingFrames > 0 && NOT Receipt.SupportEligible && NOT Receipt.SupportPending
                            && NOT Receipt.SupportPendingRetained && NOT Receipt.SupportCommitted
                            && Receipt.SupportRoomCalls == 0 && Receipt.SupportTrials == 0
                            && ClaimedRevision > RevisionBeforeClaim
                            && Subject.Get<ck::FFragment_ProceduralArticulation_Claims>().Get_Revision() == ClaimedRevision
                            && FCk_ProceduralArticulation_Control::GetClaimant(Subject, NAME_None, BodyChannel) == ClaimToken,
                            TEXT("The changed real Body claim invalidates pending support without releasing its owner or taking new authority")))
                    { return; }
                    if (ActualFloorZero)
                    { ++NativeRejectedFrames; }
                    if (NativeRejectedFrames >= 3)
                    { Phase = EPhase::Complete; }
                }
                return;
            }
            if (Get_IsBudgetScenario())
            {
                if (NOT Require(NOT Receipt.SupportPending && NOT Receipt.SupportCommitted,
                        TEXT("The no-pose envelope-reserve control never publishes support authority")))
                { return; }
                if (Receipt.SupportBudgetDeclined)
                {
                    if (NOT Require(ActualFloorZero && NOT Receipt.SupportPendingRetained
                            && Receipt.SupportAcceptedSubsteps == 10 && Receipt.SupportRemainingSubsteps == 9
                            && Receipt.SupportBudgetEnvelopesBefore == 8 && Receipt.SupportProofQueries == 3
                            && Receipt.SupportBudgetEnvelopesBefore + Receipt.SupportRemainingSubsteps * 6
                                + Receipt.SupportProofQueries == 65
                            && Receipt.SupportRoomCalls == 0 && Receipt.SupportTrials == 0 && Receipt.SupportRays == 0
                            && UCk_Utils_SurfaceMotion_UE::Get_HeightPlanningTrials(Motion) == 0
                            && UCk_Utils_SurfaceMotion_UE::Get_HeightPlanningRays(Motion) == 0
                            && UCk_Utils_SurfaceMotion_UE::Get_HeightEnvelopeQueries(Motion) <= 64,
                            TEXT("The real qualifying zero reaches the unchanged 65-over-64 reserve bound and declines before any room ray or trial")))
                    { return; }
                    ++NativeRejectedFrames;
                    Phase = EPhase::Complete;
                }
                return;
            }
            if (Receipt.SupportCommitted)
            {
                if (NOT Require(CommittedFrame == 0 && PendingFrames > 0 && Receipt.SupportEligible && Receipt.SupportRoom
                        && Receipt.SupportPhysical && Receipt.SupportLandings && Receipt.SupportEnvelope
                        && Receipt.SupportNormal.Equals(FVector::UpVector, 1.0e-3)
                        && Receipt.SupportTrials == 1 && Receipt.SupportRays == 1
                        && Receipt.SupportEnvelopes > 0 && Receipt.SupportEnvelopes <= 64,
                        TEXT("One confirmed native transaction passes room, plants, physical, landing and both envelope proofs")))
                { return; }
                const auto Change = Receipt.SupportAfter.GetLocation() - Receipt.SupportBefore.GetLocation();
                const auto& Core = Receipt.SupportStateAfter;
                Require(Change.Z > 0.0 && FMath::Abs(Change.X) <= 1.0e-9 && FMath::Abs(Change.Y) <= 1.0e-9
                    && Support.Get_EvaluatedBody().Equals(Receipt.SupportAfter, 1.0e-9)
                    && UCk_Utils_SurfaceMotion_UE::Get_ContactSource(Motion) == ECk_SurfaceMotion_ContactSource::NativeEnvelope
                    && UCk_Utils_SurfaceMotion_UE::Get_ContactQuery(Motion) == ECk_SurfaceMotion_ContactQuery::Trusted
                    && UCk_Utils_SurfaceMotion_UE::Get_Obstruction(Motion) == ECk_SurfaceMotion_Obstruction::None
                    && UCk_Utils_SurfaceMotion_UE::Get_ObstructionNormal(Motion) == FVector::ZeroVector
                    && NOT UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightActive(Motion)
                    && NOT UCk_Utils_SurfaceMotion_UE::Get_AdaptiveHeightPreparing(Motion),
                    TEXT("The committed native support publishes only its correction and clears old wall/height authority"));
                Require(Receipt.SupportZeroAccepted && Receipt.SupportSweep.Get_Entity() == FloorEntity
                    && Core.Get_Grounded() && Core.Get_ContactTrusted() && Core.Get_MissingContact() == FCk_Time{}
                    && Core.Get_SupportNormal().Equals(Receipt.SupportSweep.Get_Normal(), 1.0e-9)
                    && Core.Get_SupportPoint().Equals(Receipt.SupportSweep.Get_Position(), 1.0e-9)
                    && Core.Get_ContactSource() == ck::EProceduralSurfaceContactSource::NativeEnvelope
                    && Receipt.SupportPublicSource == ECk_SurfaceMotion_ContactSource::NativeEnvelope
                    && Core.Get_StepForward() == Receipt.SupportStateBefore.Get_StepForward()
                    && Core.Get_Velocity().Equals(Change * 90.0, 1.0e-3)
                    && Core.Get_CandidateSeen() == FCk_Time{}
                    && Core.Get_CandidateNormal() == Core.Get_SupportNormal()
                    && Core.Get_CandidatePoint() == Core.Get_SupportPoint()
                    && Core.Get_Obstruction() == ck::EProceduralSurfaceObstruction::None
                    && Core.Get_ObstructionPoint() == FVector::ZeroVector
                    && Core.Get_ObstructionNormal() == FVector::ZeroVector
                    && Core.Get_ObstructionStandoff() == 0.0f && NOT Core.Get_ClimbAcquisition().IsSet()
                    && FMath::Abs(FVector::DotProduct(Core.Get_TravelTangent(), Core.Get_SupportNormal())) <= 1.0e-6,
                    TEXT("The real committed Core state has native provenance, exact correction velocity and no stale wall/acquisition fields"));
                Require(HasReflectedNativeEnvelopeSource(), TEXT("The reflected getter maps the actual native-envelope commit"));
#if WITH_ANGELSCRIPT_CK
                Require(HasAngelscriptNativeEnvelopeSource(), TEXT("AngelScript executes its real getter and NativeEnvelope enum on this same committed entity"));
#else
                Test->AddInfo(TEXT("AngelScript runtime proof is unavailable in this build: WITH_ANGELSCRIPT_CK is disabled."));
#endif
                CommittedFrame = GFrameCounter;
                CommittedBody = Support.Get_EvaluatedBody();
                CommittedUp = UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion);
                return;
            }
            if (CommittedFrame != 0 && GFrameCounter > CommittedFrame)
            {
                if (NOT Require(UCk_Utils_SurfaceMotion_UE::Get_Support(Motion) == ECk_SurfaceMotion_Support::Grounded
                        && UCk_Utils_SurfaceMotion_UE::Get_ContactQuery(Motion) == ECk_SurfaceMotion_ContactQuery::Trusted
                        && UCk_Utils_SurfaceMotion_UE::Get_SupportNormal(Motion).Equals(CommittedUp, 1.0e-3)
                        && Support.Get_EvaluatedBody().GetLocation().Z >= CommittedBody.GetLocation().Z - 1.0e-3
                        && UCk_Utils_SurfaceMotion_UE::Get_ReachPaceState(Motion) != ECk_SurfaceMotion_ReachPaceState::PhysicalOverride,
                        TEXT("The next ordinary production update retains supported Up without reversal or physical override")))
                { return; }
                ++NextAcceptedFrames;
                Phase = EPhase::Complete;
            }
        }
    };
}

namespace ck
{
    struct FFragment_TestSupportReturnNative
    {
        CK_GENERATED_BODY(FFragment_TestSupportReturnNative);
        TSharedPtr<ck_test_support_return_native::FState> State;
    };

    class FProcessor_TestSupportReturnBeforeSetup : public ck_exp::TProcessor<FProcessor_TestSupportReturnBeforeSetup,
        FCk_Handle_ProceduralGait, TReadWrite<FFragment_TestSupportReturnNative>, TReadOnly<FFragment_ProceduralGait>,
        TExclude<FTag_DestroyEntity_Initiate>, CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_Physics;
        using RunBefore = TDepList<FProcessor_SurfaceMotion_Setup>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType InDt,
            HandleType InGait,
            FFragment_TestSupportReturnNative& InObserver,
            const FFragment_ProceduralGait&)
            -> void
        {
            if (InObserver.State.IsValid() && InObserver.State->Phase == ck_test_support_return_native::EPhase::AddMotion
                && NOT InGait.Has<FFragment_SurfaceMotion_Params>() && NOT InGait.Has<FFragment_SurfaceMotion>()
                && NOT InGait.Has<FFragment_SurfaceMotion_Support>() && NOT InGait.Has<FTag_SurfaceMotion_NeedsSetup>())
            { InObserver.State->ObserveBefore(InDt.Get_Seconds()); }
        }
    };

    class FProcessor_TestSupportReturnBefore : public ck_exp::TProcessor<FProcessor_TestSupportReturnBefore,
        FCk_Handle_ProceduralGait, TReadWrite<FFragment_TestSupportReturnNative>, TReadOnly<FFragment_ProceduralGait>,
        TExclude<FTag_DestroyEntity_Initiate>, CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_HandleRequests>;
        using RunBefore = TDepList<FProcessor_SurfaceMotion_Update>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType InDt,
            HandleType,
            FFragment_TestSupportReturnNative& InObserver,
            const FFragment_ProceduralGait&)
            -> void
        {
            if (InObserver.State.IsValid() && InObserver.State->Phase != ck_test_support_return_native::EPhase::AddMotion)
            { InObserver.State->ObserveBefore(InDt.Get_Seconds()); }
        }
    };

    class FProcessor_TestSupportReturnAfter : public ck_exp::TProcessor<FProcessor_TestSupportReturnAfter,
        FCk_Handle_ProceduralGait, TReadWrite<FFragment_TestSupportReturnNative>, TReadOnly<FFragment_ProceduralGait>,
        TExclude<FTag_DestroyEntity_Initiate>, CK_IGNORE_PENDING_KILL>
    {
    public:
        using Group = FGroup_Physics;
        using RunAfter = TDepList<FProcessor_SurfaceMotion_Update, FProcessor_TestSupportReturnBefore>;
        using TProcessor::TProcessor;

        static auto
        ForEachEntity(
            TimeType,
            HandleType,
            FFragment_TestSupportReturnNative& InObserver,
            const FFragment_ProceduralGait&)
            -> void
        {
            if (InObserver.State.IsValid())
            { InObserver.State->ObserveAfter(); }
        }
    };
}

CK_REGISTER_PROCESSOR(ck::FProcessor_TestSupportReturnBeforeSetup);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestSupportReturnBefore);
CK_REGISTER_PROCESSOR(ck::FProcessor_TestSupportReturnAfter);

namespace ck_test_support_return_native
{
    auto
    Enqueue(
        FAutomationTestBase* InTest,
        EScenario InScenario)
        -> void
    {
        const auto State = MakeShared<FState>();
        State->Test = InTest;
        State->Scenario = InScenario;
        State->Origin = FVector{120000.0, 430000.0 + 1000.0 * static_cast<int32>(InScenario), 5000.0};
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_StartPIEMultiClient(1, TEXT("/Engine/Maps/Entry")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitForPIEReady(1, 30.0f));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [State](UWorld* InWorld)
            {
                State->World = InWorld;
                State->Compose();
                if (ck::IsValid(State->Gait))
                { State->Gait.Add<ck::FFragment_TestSupportReturnNative>().State = State; }
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_WaitUntil(InTest,
            FCk_NetAutoTest_Condition::CreateLambda([State] { return State->Phase == EPhase::Complete || State->Failed; }),
            15.0, TEXT("Publicly composed walker obtains mandatory actual native support-return coverage")));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_RunOnServer(FCk_NetAutoTest_ServerAction::CreateLambda(
            [State](UWorld*)
            {
                State->Require(State->Phase == EPhase::Complete && NOT State->Failed,
                    TEXT("The requested native outcome is mandatory; absence of the branch is a failed test"));
                if (State->Scenario == EScenario::IndependentFloor)
                { State->Require(State->PendingFrames > 0 && State->CommittedFrame > 0 && State->NextAcceptedFrames > 0,
                    TEXT("Native pending, commit and next ordinary support stamps were all observed")); }
                else if (State->Scenario == EScenario::OwnedFloor)
                { State->Require(State->NativeRejectedFrames >= 3 && State->CommittedFrame == 0,
                    TEXT("The owned-collider control preserves three real blocked zero outcomes")); }
                else if (State->Scenario == EScenario::ClaimedBody)
                { State->Require(State->PendingFrames > 0 && State->ClaimTaken && State->NativeRejectedFrames >= 3
                        && State->CommittedFrame == 0 && State->ClaimedRevision > State->RevisionBeforeClaim,
                    TEXT("Actual native pending is interrupted by a real Body claim and all subsequent zero outcomes reject support")); }
                else
                { State->Require(State->NativeRejectedFrames > 0 && State->PendingFrames == 0 && State->CommittedFrame == 0,
                    TEXT("The actual no-pose reserve preflight declined without pending or committed support")); }
                State->Test->AddInfo(FString::Printf(TEXT("SupportReturnNative scenario%d phase%d drive%d pending%d commit%llu next%d rejected%d"),
                    static_cast<int32>(State->Scenario), static_cast<int32>(State->Phase), State->DriveFrames,
                    State->PendingFrames, static_cast<unsigned long long>(State->CommittedFrame),
                    State->NextAcceptedFrames, State->NativeRejectedFrames));
                if (ck::IsValid(State->Gait) && State->Gait.Has<ck::FFragment_TestSupportReturnNative>())
                { State->Gait.Get<ck::FFragment_TestSupportReturnNative>().State.Reset(); }
                if (State->ClaimTaken)
                {
                    State->Require(ck::IsValid(State->Subject)
                            && FCk_ProceduralArticulation_Control::ReleaseClaims(State->Subject, State->ClaimToken, State->BodyChannels)
                                == ECk_ArticulationControlResult::Accepted,
                        TEXT("Cleanup releases exactly the real reserved Body claim through the public protocol"));
                    State->ClaimTaken = false;
                }
                if (ck::IsValid(State->WallEntity))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->WallEntity); }
                if (ck::IsValid(State->FloorEntity) && State->Scenario != EScenario::OwnedFloor)
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->FloorEntity); }
                if (ck::IsValid(State->Body))
                { UCk_Utils_EntityLifetime_UE::Request_DestroyEntity(State->Body); }
                State->AsProof.Reset();
                State->RestoreTime();
            })));
        ADD_LATENT_AUTOMATION_COMMAND(FCk_Latent_EndPIE());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportReturnNativeCommitTest,
    "Ck.ProceduralAnimation.SurfaceMotion.SupportReturn.NativeConfirmedCommitAndNextStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralSupportReturnNativeCommitTest::
RunTest(
    const FString&)
    -> bool
{
    ck_test_support_return_native::Enqueue(this, ck_test_support_return_native::EScenario::IndependentFloor);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportReturnOwnedColliderNativeTest,
    "Ck.ProceduralAnimation.SurfaceMotion.SupportReturn.NativeOwnedColliderRejects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralSupportReturnOwnedColliderNativeTest::
RunTest(
    const FString&)
    -> bool
{
    ck_test_support_return_native::Enqueue(this, ck_test_support_return_native::EScenario::OwnedFloor);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportReturnClaimInterruptNativeTest,
    "Ck.ProceduralAnimation.SurfaceMotion.SupportReturn.NativeBodyClaimInterruptsConfirmation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralSupportReturnClaimInterruptNativeTest::
RunTest(
    const FString&)
    -> bool
{
    ck_test_support_return_native::Enqueue(this, ck_test_support_return_native::EScenario::ClaimedBody);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkProceduralSupportReturnEnvelopeBudgetNativeTest,
    "Ck.ProceduralAnimation.SurfaceMotion.SupportReturn.NativeNoPoseEnvelopeReserveDeclines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

auto
FCkProceduralSupportReturnEnvelopeBudgetNativeTest::
RunTest(
    const FString&)
    -> bool
{
    ck_test_support_return_native::Enqueue(this, ck_test_support_return_native::EScenario::NoPoseEnvelopeBudget);
    return true;
}

#endif

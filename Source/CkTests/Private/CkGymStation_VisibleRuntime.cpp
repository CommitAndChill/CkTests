#include "CkGymStation_VisibleRuntime.h"

#include "CkEcs/EntityLifetime/CkEntityLifetime_Fragment.h"
#include "CkEcs/EntityLifetime/CkEntityLifetime_Utils.h"
#include "CkEcs/EntityScript/CkEntityScript.h"
#include "CkEcs/EntityScript/CkEntityScript_Fragment.h"
#include "CkEcs/Subsystem/CkEcsWorld_Subsystem.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Fragment.h"
#include "CkEcsExt/SceneNode/CkSceneNode_Utils.h"
#include "CkEcsExt/Transform/CkTransform_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Fragment.h"
#include "CkUnrealComponent/CkUnrealComponent_Utils.h"

#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace ck_gym_station_visible
{
    enum class EPieceKind : uint8 { BodyMesh, Text, Spotlight };

    auto TryGetRole(ECkGym_StationVisibleRole Role, FName& OutName, EPieceKind& OutKind) -> bool
    {
        switch (Role)
        {
            case ECkGym_StationVisibleRole::BackWall: OutName = TEXT("BackWall"); OutKind = EPieceKind::BodyMesh; return true;
            case ECkGym_StationVisibleRole::FloorStage: OutName = TEXT("FloorStage"); OutKind = EPieceKind::BodyMesh; return true;
            case ECkGym_StationVisibleRole::LeftTrim: OutName = TEXT("LeftTrim"); OutKind = EPieceKind::BodyMesh; return true;
            case ECkGym_StationVisibleRole::RightTrim: OutName = TEXT("RightTrim"); OutKind = EPieceKind::BodyMesh; return true;
            case ECkGym_StationVisibleRole::TopTrim: OutName = TEXT("TopTrim"); OutKind = EPieceKind::BodyMesh; return true;
            case ECkGym_StationVisibleRole::BottomTrim: OutName = TEXT("BottomTrim"); OutKind = EPieceKind::BodyMesh; return true;
            case ECkGym_StationVisibleRole::TitleText: OutName = TEXT("TitleText"); OutKind = EPieceKind::Text; return true;
            case ECkGym_StationVisibleRole::DescriptionText: OutName = TEXT("DescriptionText"); OutKind = EPieceKind::Text; return true;
            case ECkGym_StationVisibleRole::FloorDescriptionText: OutName = TEXT("FloorDescriptionText"); OutKind = EPieceKind::Text; return true;
            case ECkGym_StationVisibleRole::Spotlight: OutName = TEXT("Spotlight"); OutKind = EPieceKind::Spotlight; return true;
            default: return false;
        }
    }

    auto IsLive(const FCk_Handle& Entity) -> bool
    {
        return ck::IsValid(Entity) &&
            !UCk_Utils_EntityLifetime_UE::Get_IsPendingDestroy(
                Entity, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    auto IsFinite(const FTransform& Transform) -> bool
    {
        const auto P = Transform.GetTranslation();
        const auto S = Transform.GetScale3D();
        const auto Q = Transform.GetRotation();
        return FMath::IsFinite(P.X) && FMath::IsFinite(P.Y) && FMath::IsFinite(P.Z) &&
            FMath::IsFinite(S.X) && FMath::IsFinite(S.Y) && FMath::IsFinite(S.Z) &&
            FMath::IsFinite(Q.X) && FMath::IsFinite(Q.Y) &&
            FMath::IsFinite(Q.Z) && FMath::IsFinite(Q.W) &&
            FMath::IsFinite(Q.SizeSquared()) && Q.IsNormalized();
    }

    // GetMaterial() recursively follows Parent and can return the default material
    // for a missing/cyclic link. Validate the actual asset chain instead.
    auto TryGetBaseMaterial(const UMaterialInterface* First, const UMaterial*& OutBase) -> bool
    {
        OutBase = nullptr;
        TSet<const UMaterialInterface*> Seen;
        auto* Current = First;
        for (int32 Depth = 0; Depth < 64; ++Depth)
        {
            if (!IsValid(Current) || !Current->IsAsset() ||
                Current->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
                Seen.Contains(Current)) { return false; }
            Seen.Add(Current);
            if (const auto* Material = Cast<UMaterial>(Current))
            { OutBase = Material; return true; }
            const auto* Instance = Cast<UMaterialInstance>(Current);
            if (!Instance) { return false; }
            Current = Instance->Parent.Get();
        }
        return false;
    }

    // Validate the complete station-to-world chain without Get_WorldForEntity's
    // recursive/ensuring fallback on malformed lifetime graphs.
    auto HasWorldRoot(const FCk_Handle& Station, const FCk_Handle& Root) -> bool
    {
        auto Current = Station;
        TSet<FCk_Handle> Seen;
        for (int32 Depth = 0; Depth < 64; ++Depth)
        {
            if (!IsLive(Current) || Seen.Contains(Current) ||
                Current.Get_RegistryView().Get_RegistryHandle() !=
                    Root.Get_RegistryView().Get_RegistryHandle()) { return false; }
            if (Current == Root) { return true; }
            Seen.Add(Current);
            if (!Current.Has<ck::FFragment_LifetimeOwner>()) { return false; }
            Current = Current.Get<ck::FFragment_LifetimeOwner>().Get_Entity();
        }
        return false;
    }
}

FCk_Handle UCk_GymStation_VisibleRuntime_UE::TryGet_StationEntity(
    const UCk_EntityScript_UE* InScript)
{
    if (!IsValid(InScript) ||
        InScript->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject |
            RF_BeginDestroyed | RF_FinishDestroyed)) { return {}; }
    const auto Entity = InScript->Get_AssociatedEntity();
    if (!ck_gym_station_visible::IsLive(Entity) ||
        !Entity.Has<ck::FFragment_EntityScript>() ||
        Entity.Get<ck::FFragment_EntityScript>().Get_Script().Get() != InScript)
    { return {}; }
    return Entity;
}

bool UCk_GymStation_VisibleRuntime_UE::TryCapture_VisiblePiece(
    const FCk_Handle& InStation,
    const FCk_Handle_SceneNode& InNode,
    const FCk_Handle_UnrealComponent& InComponentHandle,
    ECkGym_StationVisibleRole InRole,
    UWorld* InWorld,
    FCkGym_StationVisiblePiece& OutPiece)
{
    const auto Station = InStation;
    const auto Node = InNode;
    const auto ComponentHandle = InComponentHandle;
    auto* const World = InWorld;
    OutPiece = {};

    FName ExpectedName;
    ck_gym_station_visible::EPieceKind Kind;
    if (!ck_gym_station_visible::TryGetRole(InRole, ExpectedName, Kind)) { return false; }
    if (!IsValid(World) || World->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !World->HasBegunPlay()) { return false; }
    const auto Root = UCk_Utils_EcsWorld_Subsystem_UE::TryGet_TransientEntityForWorld(*World);
    if (!ck_gym_station_visible::IsLive(Root) ||
        !Root.Has<TWeakObjectPtr<UWorld>>() ||
        Root.Get<TWeakObjectPtr<UWorld>>().Get() != World) { return false; }
    if (!ck_gym_station_visible::IsLive(Station) ||
        !ck_gym_station_visible::IsLive(Node) ||
        !ck_gym_station_visible::IsLive(ComponentHandle)) { return false; }
    const auto Registry = Root.Get_RegistryView().Get_RegistryHandle();
    if (Station.Get_RegistryView().Get_RegistryHandle() != Registry ||
        Node.Get_RegistryView().Get_RegistryHandle() != Registry ||
        ComponentHandle.Get_RegistryView().Get_RegistryHandle() != Registry ||
        Station == Node || Station == ComponentHandle || Node == ComponentHandle ||
        !Station.Has<ck::FFragment_Transform>() ||
        !ck_gym_station_visible::HasWorldRoot(Station, Root)) { return false; }

    if (!Node.Has<ck::FFragment_SceneNode>() ||
        !Node.Has<ck::FFragment_Transform>() ||
        !Node.Has<ck::FFragment_LifetimeOwner>() ||
        !ck::USceneNodeParent_Utils::Has(Node) ||
        Node.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Station ||
        UCk_Utils_SceneNode_UE::Get_Parent(Node) != Station) { return false; }
    if (!ComponentHandle.Has<ck::FFragment_UnrealComponent>() ||
        !ComponentHandle.Has<ck::FFragment_UnrealComponent_Params>() ||
        !ComponentHandle.Has<ck::FFragment_LifetimeOwner>() ||
        ComponentHandle.Has<ck::FTag_UnrealComponent_NeedsSetup>() ||
        ComponentHandle.Has<ck::FTag_UnrealComponent_TransformPushDisabled>() ||
        !ComponentHandle.Has<ck::FTag_UnrealComponent_IsScene>() ||
        ComponentHandle.Get<ck::FFragment_LifetimeOwner>().Get_Entity() != Node) { return false; }

    const auto& Params = ComponentHandle.Get<ck::FFragment_UnrealComponent_Params>();
    const auto& Current = ComponentHandle.Get<ck::FFragment_UnrealComponent>();
    UClass* ExpectedClass = nullptr;
    switch (Kind)
    {
        case ck_gym_station_visible::EPieceKind::BodyMesh:
            ExpectedClass = UStaticMeshComponent::StaticClass(); break;
        case ck_gym_station_visible::EPieceKind::Text:
            ExpectedClass = UTextRenderComponent::StaticClass(); break;
        case ck_gym_station_visible::EPieceKind::Spotlight:
            ExpectedClass = USpotLightComponent::StaticClass(); break;
    }
    if (Params.Get_DebugName() != ExpectedName ||
        Params.Get_ComponentClass().Get() != ExpectedClass ||
        Current.Get_OwningEntity() != Node)
    { return false; }
    auto* const Component = Current.Get_Component().Get();
    if (!IsValid(Component) || Component->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
        !Component->IsRegistered() || Component->GetWorld() != World ||
        UCk_Utils_UnrealComponent_UE::TryGet_OwningHandle_FromComponent(Component) !=
            ComponentHandle) { return false; }
    auto* const Scene = Cast<USceneComponent>(Component);
    if (!Scene) { return false; }
    if (Component->GetClass() != ExpectedClass) { return false; }

    FCkGym_StationVisiblePiece Candidate;
    Candidate.Role = InRole;
    Candidate.NodeWorldTransform = Node.Get<ck::FFragment_Transform>().Get_Transform();
    Candidate.ComponentWorldTransform = Scene->GetComponentTransform();
    if (!ck_gym_station_visible::IsFinite(Candidate.NodeWorldTransform) ||
        !ck_gym_station_visible::IsFinite(Candidate.ComponentWorldTransform)) { return false; }
    Candidate.Visible = Scene->IsVisible();
    Candidate.HiddenInGame = Scene->bHiddenInGame;

    if (Kind == ck_gym_station_visible::EPieceKind::BodyMesh)
    {
        auto* const Mesh = Cast<UStaticMeshComponent>(Component);
        auto* const Asset = Mesh->GetStaticMesh().Get();
        auto* const Material = Mesh->GetMaterial(0);
        auto* const MID = Cast<UMaterialInstanceDynamic>(Material);
        if (!IsValid(Asset) || !Asset->IsAsset() ||
            Asset->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            !IsValid(Material) || Material->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            !IsValid(MID) || MID->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            MID->GetOuter() != Mesh)
        { return false; }
        auto* const Parent = MID->Parent.Get();
        const UMaterial* Base = nullptr;
        if (!ck_gym_station_visible::TryGetBaseMaterial(Parent, Base)) { return false; }
        Candidate.MeshAssetPath = Asset->GetPathName();
        Candidate.ParentMaterialPath = Parent->GetPathName();
        Candidate.BaseMaterialPath = Base->GetPathName();
        Candidate.CollisionEnabled = static_cast<int32>(Mesh->GetCollisionEnabled());
        if (!MID->GetVectorParameterValue(
                FHashedMaterialParameterInfo{FName(TEXT("Color"))}, Candidate.MeshColor))
        { return false; }
        if (!FMath::IsFinite(Candidate.MeshColor.R) ||
            !FMath::IsFinite(Candidate.MeshColor.G) ||
            !FMath::IsFinite(Candidate.MeshColor.B) ||
            !FMath::IsFinite(Candidate.MeshColor.A)) { return false; }
    }
    else if (Kind == ck_gym_station_visible::EPieceKind::Text)
    {
        auto* const Text = Cast<UTextRenderComponent>(Component);
        if (!IsValid(Text->Font.Get()) || !Text->Font->IsAsset() ||
            Text->Font->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed) ||
            !IsValid(Text->TextMaterial.Get()) || !Text->TextMaterial->IsAsset() ||
            Text->TextMaterial->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        { return false; }
        Candidate.Text = Text->Text.ToString();
        Candidate.FontPath = Text->Font->GetPathName();
        Candidate.TextMaterialPath = Text->TextMaterial->GetPathName();
        Candidate.TextWorldSize = Text->WorldSize;
        Candidate.TextXScale = Text->XScale;
        Candidate.TextYScale = Text->YScale;
        Candidate.TextHorizontalSpacing = Text->HorizSpacingAdjust;
        Candidate.TextVerticalSpacing = Text->VertSpacingAdjust;
        Candidate.TextColor = Text->TextRenderColor;
        Candidate.HorizontalAlignment = static_cast<int32>(Text->HorizontalAlignment.GetValue());
        Candidate.VerticalAlignment = static_cast<int32>(Text->VerticalAlignment.GetValue());
        if (!FMath::IsFinite(Candidate.TextWorldSize) ||
            !FMath::IsFinite(Candidate.TextXScale) ||
            !FMath::IsFinite(Candidate.TextYScale) ||
            !FMath::IsFinite(Candidate.TextHorizontalSpacing) ||
            !FMath::IsFinite(Candidate.TextVerticalSpacing)) { return false; }
        const bool HasShownText = Candidate.Visible && !Candidate.HiddenInGame &&
            !Text->Text.IsEmpty();
        if (HasShownText)
        {
            const auto* Font = Text->Font.Get();
            if (Font->FontCacheType != EFontCacheType::Offline ||
                Font->Textures.IsEmpty()) { return false; }
            for (const auto& Texture : Font->Textures)
            {
                const auto* GlyphTexture = Texture.Get();
                if (!IsValid(GlyphTexture) ||
                    GlyphTexture->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
                { return false; }
                const auto Imported = GlyphTexture->GetImportedSize();
                if (Imported.X <= 0 || Imported.Y <= 0) { return false; }
            }
            Candidate.ShownTextLocalSize = Text->GetTextLocalSize();
        }
        const auto& B = Candidate.ShownTextLocalSize;
        if (!FMath::IsFinite(B.X) || !FMath::IsFinite(B.Y) || !FMath::IsFinite(B.Z) ||
            B.X < 0.0 || B.Y < 0.0 || B.Z < 0.0) { return false; }
    }
    else
    {
        auto* const Spot = Cast<USpotLightComponent>(Component);
        Candidate.SpotIntensity = Spot->Intensity;
        Candidate.SpotAttenuationRadius = Spot->AttenuationRadius;
        Candidate.SpotInnerConeAngle = Spot->InnerConeAngle;
        Candidate.SpotOuterConeAngle = Spot->OuterConeAngle;
        Candidate.SpotColor = Spot->LightColor;
        Candidate.SpotIntensityUnits = static_cast<int32>(Spot->GetLightUnits());
        if (!FMath::IsFinite(Candidate.SpotIntensity) ||
            !FMath::IsFinite(Candidate.SpotAttenuationRadius) ||
            !FMath::IsFinite(Candidate.SpotInnerConeAngle) ||
            !FMath::IsFinite(Candidate.SpotOuterConeAngle)) { return false; }
    }
    OutPiece = MoveTemp(Candidate);
    return true;
}

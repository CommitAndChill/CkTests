// Six authored station payloads and the base controller's one-row grid, as values.
// This catalog allocates no ECS entity, actor, component, or timer.
namespace CkSceneNodeGymCatalog
{
	const int32 StationCount = 6;
	const float StationSpacing = 800.0f;
	const float BaseXOffset = 500.0f;

	// These two panels keep their authored descriptions while the first four
	// panels receive per-frame source display values.
	TArray<FText> Get_StaticDescriptionLines(int32 InSlot)
	{
		auto Description = TArray<FText>();
		if (InSlot == 4)
		{
			Description.Add(FText::FromString("Solar system: only the root rotates."));
			Description.Add(FText::FromString("Planet + moon have fixed local offsets and no per-frame updates."));
			Description.Add(FText::FromString("Isolates whether scene-node propagation alone moves descendants."));
		}
		else if (InSlot == 5)
		{
			Description.Add(FText::FromString("SceneNode follows a moving Unreal mesh component at a relative offset."));
			Description.Add(FText::FromString("Composes an existing replicated Transform with an Unreal component anchor."));
		}
		return Description;
	}

	FString Get_StaticBody(int32 InSlot)
	{
		auto Lines = Get_StaticDescriptionLines(InSlot);
		auto Body = "";
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (Index > 0) { Body = f"{Body}\n"; }
			Body = f"{Body}{Lines[Index].ToString()}";
		}
		return Body;
	}

	TArray<FCkGym_Station_SpawnParams_Payload> Get_AuthoredStations()
	{
		auto Stations = TArray<FCkGym_Station_SpawnParams_Payload>();

		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.SceneNode.ParentChild");
			Station.Title = FText::FromString("PARENT-CHILD");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Child scene node orbits around a parent entity."));
			Description.Add(FText::FromString("Uses utils_scene_node::Create() and Request_UpdateOffset_Location()."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.SceneNode.OffsetUpdates");
			Station.Title = FText::FromString("OFFSET UPDATES");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Animates location, rotation, and scale offsets on a child node."));
			Description.Add(FText::FromString("Uses Request_UpdateOffset_Location/Rotation/Scale()."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.SceneNode.MultipleChildren");
			Station.Title = FText::FromString("MULTIPLE CHILDREN");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Parent rotates with 4 children at fixed offsets."));
			Description.Add(FText::FromString("Children follow parent automatically via hierarchy."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.SceneNode.Hierarchy");
			Station.Title = FText::FromString("HIERARCHY");
			auto Description = TArray<FText>();
			Description.Add(FText::FromString("Nested chain: root -> child -> grandchild (3 levels)."));
			Description.Add(FText::FromString("Shows compounding transforms through the hierarchy."));
			Station.Description = Description;
			Stations.Add(Station);
		}

		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.SceneNode.PropagateOnly");
			Station.Title = FText::FromString("PROPAGATE ONLY");
			Station.Description = Get_StaticDescriptionLines(4);
			Stations.Add(Station);
		}

		{
			auto Station = FCkGym_Station_SpawnParams_Payload();
			Station.Tags.Add(n"Gym.SceneNode.AnchorOffset");
			Station.Title = FText::FromString("ANCHOR OFFSET");
			Station.Description = Get_StaticDescriptionLines(5);
			Stations.Add(Station);
		}

		return Stations;
	}

	// Mirror ACk_Gym_Base_PlayerController's default grid for six identity-
	// transform stations. The authored payload itself remains unchanged.
	bool TryGet_LaidOutStation(int32 InSlot, FCkGym_Station_SpawnParams_Payload&out OutStation)
	{
		OutStation = FCkGym_Station_SpawnParams_Payload();
		if (InSlot < 0 || InSlot >= StationCount) { return false; }
		auto Stations = Get_AuthoredStations();
		if (Stations.Num() != StationCount) { return false; }
		auto Candidate = Stations[InSlot];
		if (Candidate.Tags.Num() != 1 || !Candidate.Transform.Equals(FTransform::Identity) ||
			!Math::IsFinite(Candidate.Width) || Candidate.Width <= 0.0f ||
			!Math::IsFinite(Candidate.Depth) || Candidate.Depth <= 0.0f ||
			!Math::IsFinite(Candidate.Height) || Candidate.Height <= 0.0f ||
			!Math::IsFinite(Candidate.Height * 100.0f)) { return false; }
		auto RowStartOffset = -(StationCount - 1) * StationSpacing * 0.5f;
		auto Y = RowStartOffset + InSlot * StationSpacing;
		if (!Math::IsFinite(Y) || !Math::IsFinite(BaseXOffset)) { return false; }
		auto Layout = FTransform::Identity;
		Layout.SetLocation(FVector(BaseXOffset, Y, 0.0f));
		Layout.SetRotation(FRotator(0.0f, 180.0f, 0.0f).Quaternion());
		if (!Layout.IsValid() || Layout.ContainsNaN()) { return false; }
		Candidate.Transform = Layout;
		OutStation = Candidate;
		return true;
	}

	// Match the station's PanelCenter anchor at default Height=5.0 (250 cm).
	bool TryGet_PanelCenter(int32 InSlot, FTransform&out OutAnchor)
	{
		OutAnchor = FTransform::Identity;
		FCkGym_Station_SpawnParams_Payload Station;
		if (!TryGet_LaidOutStation(InSlot, Station)) { return false; }
		auto HeightCm = Station.Height * 100.0f;
		if (!Math::IsFinite(HeightCm)) { return false; }
		auto Center = Station.Transform.TransformPosition(FVector(0.0f, 0.0f, HeightCm * 0.5f));
		if (!Math::IsFinite(Center.X) || !Math::IsFinite(Center.Y) || !Math::IsFinite(Center.Z)) { return false; }
		auto Candidate = FTransform(Station.Transform.Rotator(), Center, FVector(1.0f, 1.0f, 1.0f));
		if (!Candidate.IsValid() || Candidate.ContainsNaN()) { return false; }
		OutAnchor = Candidate;
		return true;
	}
}

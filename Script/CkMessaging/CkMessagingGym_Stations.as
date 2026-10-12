// Language=angelscript

namespace CkMessagingGym
{
    TArray<FCkGym_Station_SpawnParams_Payload> Get_Stations()
    {
        TArray<FCkGym_Station_SpawnParams_Payload> Stations;
        auto Tags = TArray<FName>();
        Tags.Add(n"Gym.Messaging.BasicBroadcast");
        Tags.Add(n"Gym.Messaging.MultiListener");
        Tags.Add(n"Gym.Messaging.OneShot");
        Tags.Add(n"Gym.Messaging.DynamicBind");
        Tags.Add(n"Gym.Messaging.MultiType");
        auto Titles = TArray<FString>();
        Titles.Add("BASIC BROADCAST & LISTEN");
        Titles.Add("MULTI-LISTENER FAN-OUT");
        Titles.Add("ONE-SHOT LISTENER");
        Titles.Add("DYNAMIC BIND / UNBIND");
        Titles.Add("MULTIPLE MESSAGE TYPES");
        for (int32 i = 0; i < 5; i++)
        {
            auto Station = FCkGym_Station_SpawnParams_Payload();
            Station.Tags.Add(Tags[i]);
            Station.Title = FText::FromString(Titles[i]);
            // The base PC's identity-transform fallback uses the same five-slot
            // default grid. Set it here so GameState and remote shells agree.
            Station.Transform = FTransform(FRotator(0.0, 180.0, 0.0),
                FVector(500.0, (i - 2) * 800.0, 0.0));
            Stations.Add(Station);
        }
        Stations[0].Description.Add(FText::FromString("Demonstrates basic message broadcast and listener binding."));
        Stations[0].Description.Add(FText::FromString("Tests payload extraction with sender and sequence data."));
        Stations[1].Description.Add(FText::FromString("Three delegates bound to the same message type on one entity."));
        Stations[1].Description.Add(FText::FromString("A single broadcast fires all three listeners simultaneously."));
        Stations[2].Description.Add(FText::FromString("Listener auto-unbinds after first fire using PostFireBehavior::Unbind."));
        Stations[2].Description.Add(FText::FromString("Subsequent broadcasts are ignored until reset re-arms the listener."));
        Stations[3].Description.Add(FText::FromString("Runtime bind and unbind of message listeners via the [B] panel row."));
        Stations[3].Description.Add(FText::FromString("Tests UnbindFrom_OnBroadcast and re-binding at runtime."));
        Stations[4].Description.Add(FText::FromString("Single entity listening to three different message types."));
        Stations[4].Description.Add(FText::FromString("Ping, Pong, and Alert messages tracked independently."));
        return Stations;
    }

    FName Get_StationTag(int32 InIndex)
    {
        auto Stations = Get_Stations();
        return InIndex >= 0 && InIndex < Stations.Num() ? Stations[InIndex].Tags[0] : n"";
    }

    FName Get_ChildTag(int32 InIndex)
    {
        if (InIndex == 0) { return n"TAG_MessagingGym_Basic"; }
        if (InIndex == 1) { return n"TAG_MessagingGym_MultiListener"; }
        if (InIndex == 2) { return n"TAG_MessagingGym_OneShot"; }
        if (InIndex == 3) { return n"TAG_MessagingGym_DynamicBind"; }
        if (InIndex == 4) { return n"TAG_MessagingGym_MultiType"; }
        return n"";
    }

    bool IsLive(FCk_Handle InHandle)
    {
        return ck::IsValid(InHandle) &&
            !utils_entity_lifetime::Get_IsPendingDestroy(
                InHandle, ECk_EntityLifetime_DestructionPhase::BeginDestroy);
    }

    bool IsDirectChild(FCk_Handle InChild, FCk_Handle InOwner)
    {
        return IsLive(InChild) && IsLive(InOwner) &&
            utils_entity_lifetime::Has_LifetimeOwner(InChild) &&
            utils_entity_lifetime::Get_LifetimeOwner(InChild) == InOwner;
    }

    ACk_MessagingGym_GameState Get_State()
    {
        TArray<ACk_MessagingGym_GameState> States;
        GetAllActorsOfClass(ACk_MessagingGym_GameState, States);
        return States.Num() == 1 && ck::IsValid(States[0]) ? States[0] : nullptr;
    }
}

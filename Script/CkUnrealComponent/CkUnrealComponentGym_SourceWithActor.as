// Language=angelscript
// The eight authority-only source actors have no actor channel. Declare the
// script's replication policy on its CDO so spawn admission and the eventual
// actor-aware instance agree before OwningActor adds its ECS bridge.
UCLASS()
class UCk_UnrealComponentGym_SourceWithActor : UCk_EntityScript_WithActor_UE
{
    default _Replication = ECk_Replication::DoesNotReplicate;
}

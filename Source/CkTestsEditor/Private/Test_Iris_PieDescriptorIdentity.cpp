#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS && UE_WITH_IRIS

#include "CkCore/Macros/CkMacros.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectHash.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace ck_test_iris_pie_descriptor
{
    struct FClassDescriptors
    {
        TRefCountPtr<const UE::Net::FReplicationStateDescriptor> _Authored;
        TRefCountPtr<const UE::Net::FReplicationStateDescriptor> _ActorCull;

        auto
            Get_IsValid() const
            -> bool
        {
            return _Authored.IsValid() && _ActorCull.IsValid();
        }
    };

    struct FOwnedPackages
    {
        TArray<TStrongObjectPtr<UPackage>> _Packages;

        ~FOwnedPackages()
        {
            for (const auto& Owner : _Packages)
            {
                auto* Package = Owner.Get();
                if (Package == nullptr)
                { continue; }
                constexpr auto IncludeNestedObjects = true;
                ForEachObjectWithPackage(Package, [](UObject* InObject)
                {
                    InObject->ClearFlags(RF_Public | RF_Standalone);
                    return true;
                }, IncludeNestedObjects);
                Package->ClearFlags(RF_Public | RF_Standalone);
                constexpr auto PackageIsDirty = false;
                Package->SetDirtyFlag(PackageIsDirty);
            }
        }

        auto
            Create(
                const FString& InPath)
            -> TStrongObjectPtr<UPackage>
        {
            auto Package = TStrongObjectPtr<UPackage>{CreatePackage(*InPath)};
            if (Package.IsValid())
            { _Packages.Emplace(Package.Get()); }
            return Package;
        }
    };

    auto
        MakeBlueprint(
            FAutomationTestBase& InTest,
            UPackage* InPackage,
            const FString& InDefault)
        -> TStrongObjectPtr<UBlueprint>
    {
        if (NOT InTest.TestNotNull(TEXT("Authored package exists"), InPackage))
        { return {}; }
        auto Blueprint = TStrongObjectPtr<UBlueprint>{FKismetEditorUtilities::CreateBlueprint(
            AActor::StaticClass(), InPackage, TEXT("Probe"), BPTYPE_Normal)};
        if (NOT InTest.TestNotNull(TEXT("Public editor authoring creates a complete Blueprint"), Blueprint.Get()))
        { return {}; }
        auto Type = FEdGraphPinType{};
        Type.PinCategory = UEdGraphSchema_K2::PC_Int;
        if (NOT InTest.TestTrue(TEXT("Author an integer with an explicit default"),
            FBlueprintEditorUtils::AddMemberVariable(Blueprint.Get(), TEXT("ReplicatedValue"), Type, InDefault)))
        { return {}; }
        const auto Index = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint.Get(), TEXT("ReplicatedValue"));
        if (NOT InTest.TestTrue(TEXT("Authored variable description exists before compilation"), Blueprint->NewVariables.IsValidIndex(Index)))
        { return {}; }
        Blueprint->NewVariables[Index].PropertyFlags |= CPF_Net;
        FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
        if (NOT InTest.TestTrue(TEXT("Authored Blueprint compiles"), Blueprint->Status != BS_Error))
        { return {}; }
        if (NOT InTest.TestNotNull(TEXT("Compiled Blueprint has a complete generated class"), Blueprint->GeneratedClass.Get()))
        { return {}; }
        return Blueprint;
    }

    auto
        DuplicateBlueprint(
            FAutomationTestBase& InTest,
            UBlueprint* InBlueprint,
            UPackage* InPackage)
        -> TStrongObjectPtr<UBlueprint>
    {
        if (NOT InTest.TestNotNull(TEXT("Source Blueprint exists"), InBlueprint))
        { return {}; }
        if (NOT InTest.TestNotNull(TEXT("Source Blueprint has a generated class"), InBlueprint->GeneratedClass.Get()))
        { return {}; }
        if (NOT InTest.TestNotNull(TEXT("Destination package exists"), InPackage))
        { return {}; }
        const auto* SourceDefault = InBlueprint->GeneratedClass->GetDefaultObject();
        if (NOT InTest.TestNotNull(TEXT("Source Blueprint has a default object"), SourceDefault))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Source CDO belongs to its compiled generated class"),
            SourceDefault->GetClass() == InBlueprint->GeneratedClass.Get()))
        { return {}; }
        const auto* SourceProperty = FindFProperty<FIntProperty>(InBlueprint->GeneratedClass.Get(), TEXT("ReplicatedValue"));
        if (NOT InTest.TestNotNull(TEXT("Source Blueprint has its authored integer"), SourceProperty))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Source generated class owns its authored integer"),
            SourceProperty->GetOwnerClass() == InBlueprint->GeneratedClass.Get()))
        { return {}; }
        const auto ExpectedDefault = SourceProperty->GetPropertyValue_InContainer(SourceDefault);
        // Editor copy/paste duplicates the authored Blueprint and compiles its generated class.
        // Class-only duplication omits the compiler's linked properties and owned CDO.
        auto Result = TStrongObjectPtr<UBlueprint>{DuplicateObject<UBlueprint>(
            InBlueprint, InPackage, InBlueprint->GetFName())};
        if (NOT InTest.TestNotNull(TEXT("Public editor duplication produces a Blueprint"), Result.Get()))
        { return {}; }
        FKismetEditorUtilities::CompileBlueprint(Result.Get());
        if (NOT InTest.TestTrue(TEXT("Duplicated Blueprint compiles"), Result->Status != BS_Error))
        { return {}; }
        const auto* GeneratedClass = Result->GeneratedClass.Get();
        if (NOT InTest.TestNotNull(TEXT("Duplicated Blueprint has a generated class"), GeneratedClass))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Compiled duplicate owns its distinct generated class in the destination package"),
            GeneratedClass != InBlueprint->GeneratedClass.Get() && GeneratedClass->ClassGeneratedBy == Result.Get()
                && GeneratedClass->GetOutermost() == InPackage && Result->GetOutermost() == InPackage))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Compiled duplicate preserves the Blueprint and generated-class names"),
            Result->GetFName() == InBlueprint->GetFName() && GeneratedClass->GetFName() == InBlueprint->GeneratedClass->GetFName()))
        { return {}; }
        const auto* DefaultObject = GeneratedClass->GetDefaultObject();
        if (NOT InTest.TestNotNull(TEXT("Compiled duplicate has a default object"), DefaultObject))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Compiled duplicate owns a distinct CDO of its generated class in its package"),
            DefaultObject != SourceDefault && DefaultObject->GetClass() == GeneratedClass
                && DefaultObject->GetOutermost() == InPackage))
        { return {}; }
        const auto* Property = FindFProperty<FIntProperty>(GeneratedClass, TEXT("ReplicatedValue"));
        if (NOT InTest.TestNotNull(TEXT("Compiled duplicate has its authored integer"), Property))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Compiled duplicate owns the authored integer"), Property->GetOwnerClass() == GeneratedClass))
        { return {}; }
        if (NOT InTest.TestEqual(TEXT("Compiled duplicate preserves the source authored default"),
            Property->GetPropertyValue_InContainer(DefaultObject), ExpectedDefault))
        { return {}; }
        return Result;
    }

    auto
        Describe(
            FAutomationTestBase& InTest,
            UClass* InClass,
            int32 InExpectedDefault)
        -> FClassDescriptors
    {
        if (NOT InTest.TestNotNull(TEXT("Descriptor input class exists"), InClass))
        { return {}; }
        const auto* Blueprint = Cast<UBlueprint>(InClass->ClassGeneratedBy);
        if (NOT InTest.TestNotNull(TEXT("Descriptor input is owned by an authored Blueprint"), Blueprint))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Blueprint owns this generated class in its package"),
            Blueprint->GeneratedClass.Get() == InClass && Blueprint->GetOutermost() == InClass->GetOutermost()))
        { return {}; }
        const auto* DefaultObject = InClass->GetDefaultObject();
        if (NOT InTest.TestNotNull(TEXT("Descriptor input owns a fully constructed CDO"), DefaultObject))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Descriptor CDO has the input class and belongs to its package"),
            DefaultObject->GetClass() == InClass && DefaultObject->GetOutermost() == InClass->GetOutermost()))
        { return {}; }
        const auto* Property = FindFProperty<FIntProperty>(InClass, TEXT("ReplicatedValue"));
        if (NOT InTest.TestNotNull(TEXT("Compiled reflected integer exists before descriptor construction"), Property))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Compiled reflected integer belongs to the input class"), Property->GetOwnerClass() == InClass))
        { return {}; }
        if (NOT InTest.TestEqual(TEXT("Compiled CDO contains the expected authored integer default"),
            Property->GetPropertyValue_InContainer(DefaultObject), InExpectedDefault))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Editor compilation installed the authored replication flag"), Property->HasAnyPropertyFlags(CPF_Net)))
        { return {}; }
        const auto* CullProperty = InClass->FindPropertyByName(TEXT("NetCullDistanceSquared"));
        if (NOT InTest.TestNotNull(TEXT("Complete Actor class exposes its special cull-distance property"), CullProperty))
        { return {}; }
        auto Parameters = UE::Net::FReplicationStateDescriptorBuilder::FParameters{};
        Parameters.IncludeSuper = false;
        auto Descriptors = UE::Net::FReplicationStateDescriptorBuilder::FResult{};
        UE::Net::FReplicationStateDescriptorBuilder::CreateDescriptorsForClass(Descriptors, InClass, Parameters);
        auto Result = FClassDescriptors{};
        for (const auto& Descriptor : Descriptors)
        {
            if (NOT InTest.TestTrue(TEXT("Every returned native Iris descriptor is valid"), Descriptor.IsValid()))
            { return {}; }
            if (NOT InTest.TestEqual(TEXT("Each expected state owns exactly one reflected member"), Descriptor->MemberCount, uint16{1}))
            { return {}; }
            if (NOT InTest.TestEqual(TEXT("Property-only fixture introduces no function state"), Descriptor->FunctionCount, uint16{0}))
            { return {}; }
            if (NOT InTest.TestNotNull(TEXT("Property state exposes its public member-property identities"), Descriptor->MemberProperties))
            { return {}; }
            const auto* MemberProperty = Descriptor->MemberProperties[0];
            if (MemberProperty == Property)
            {
                if (NOT InTest.TestFalse(TEXT("Authored property occurs in exactly one descriptor"), Result._Authored.IsValid()))
                { return {}; }
                if (NOT InTest.TestFalse(TEXT("Unconditional authored integer has no lifetime-condition state"),
                    EnumHasAnyFlags(Descriptor->Traits, UE::Net::EReplicationStateTraits::HasLifetimeConditionals)))
                { return {}; }
                Result._Authored = Descriptor;
            }
            else
            {
                if (NOT InTest.TestTrue(TEXT("Auxiliary state belongs to the Actor's actual cull-distance property"), MemberProperty == CullProperty))
                { return {}; }
                if (NOT InTest.TestFalse(TEXT("Actor cull property occurs in exactly one descriptor"), Result._ActorCull.IsValid()))
                { return {}; }
                if (NOT InTest.TestTrue(TEXT("Actor special cull state carries lifetime conditions"),
                    EnumHasAnyFlags(Descriptor->Traits, UE::Net::EReplicationStateTraits::HasLifetimeConditionals)))
                { return {}; }
                if (NOT InTest.TestNotNull(TEXT("Actor cull state exposes its lifetime-condition metadata"), Descriptor->MemberLifetimeConditionDescriptors))
                { return {}; }
                if (NOT InTest.TestEqual(TEXT("Actor's special cull member remains disabled for wire serialization"),
                    Descriptor->MemberLifetimeConditionDescriptors[0].Condition, static_cast<int8>(COND_Never)))
                { return {}; }
                Result._ActorCull = Descriptor;
            }
        }
        if (NOT InTest.TestTrue(TEXT("Compiled authored property has its unique selected descriptor"), Result._Authored.IsValid()))
        { return {}; }
        if (NOT InTest.TestTrue(TEXT("Complete Actor also has its unique special cull descriptor"), Result._ActorCull.IsValid()))
        { return {}; }
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkIrisPieDescriptorIdentity,
    "Ck.Iris.Net.DescriptorIdentity.PiePackageCanonicalization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

auto
    FCkIrisPieDescriptorIdentity::
    RunTest(
        const FString&)
    -> bool
{
    using namespace ck_test_iris_pie_descriptor;
    auto Owned = FOwnedPackages{};
    const auto Directory = TEXT("/Game/CkIrisIdentity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const auto Authored = Owned.Create(Directory / TEXT("Base"));
    const auto Other = Owned.Create(Directory / TEXT("Other"));
    const auto Source = MakeBlueprint(*this, Authored.Get(), TEXT("11"));
    const auto DifferentDefaults = MakeBlueprint(*this, Other.Get(), TEXT("17"));
    if (NOT Source.IsValid() || NOT DifferentDefaults.IsValid())
    { return false; }
    const auto Original = Describe(*this, Source->GeneratedClass.Get(), 11);
    const auto OtherDescriptor = Describe(*this, DifferentDefaults->GeneratedClass.Get(), 17);
    if (NOT Original.Get_IsValid() || NOT OtherDescriptor.Get_IsValid())
    { return false; }
    TestTrue(TEXT("Different authored packages remain distinct even with identical class names and layouts"),
        Original._Authored->DescriptorIdentifier.Value != OtherDescriptor._Authored->DescriptorIdentifier.Value);
    TestTrue(TEXT("Different authored packages also keep their Actor cull state identities distinct"),
        Original._ActorCull->DescriptorIdentifier.Value != OtherDescriptor._ActorCull->DescriptorIdentifier.Value);
    TestTrue(TEXT("Distinct authored defaults remain distinguishable in the default-state checksum"),
        Original._Authored->DescriptorIdentifier.DefaultStateHash != OtherDescriptor._Authored->DescriptorIdentifier.DefaultStateHash);

    for (const auto Instance : {0, 1})
    {
        const auto PackageName = Directory / FString::Printf(TEXT("UEDPIE_%d_Base"), Instance);
        const auto Package = Owned.Create(PackageName);
        if (NOT TestNotNull(TEXT("PIE package exists before assigning its instance"), Package.Get()))
        { return false; }
        const auto Copy = DuplicateBlueprint(*this, Source.Get(), Package.Get());
        if (NOT Copy.IsValid())
        { return false; }
        Package->SetPIEInstanceID(Instance);
        Package->SetPackageFlags(PKG_PlayInEditor);
        const auto Descriptor = Describe(*this, Copy->GeneratedClass.Get(), 11);
        if (NOT Descriptor.Get_IsValid())
        { return false; }
        TestTrue(TEXT("Compiled Blueprint reflection is distinct in its package with PIE metadata"),
            Copy->GeneratedClass.Get() != Source->GeneratedClass.Get() && Copy->GeneratedClass->GetOutermost() == Package.Get());
        TestEqual(TEXT("Compiled class with PIE instance metadata shares its authored descriptor identity"),
            Descriptor._Authored->DescriptorIdentifier.Value, Original._Authored->DescriptorIdentifier.Value);
        TestEqual(TEXT("Compiled class with PIE instance metadata preserves the authored default-state checksum"),
            Descriptor._Authored->DescriptorIdentifier.DefaultStateHash, Original._Authored->DescriptorIdentifier.DefaultStateHash);
        TestEqual(TEXT("Compiled class with PIE instance metadata also shares the Actor cull descriptor identity"),
            Descriptor._ActorCull->DescriptorIdentifier.Value, Original._ActorCull->DescriptorIdentifier.Value);
        TestEqual(TEXT("Compiled class with PIE instance metadata also preserves the Actor cull default-state checksum"),
            Descriptor._ActorCull->DescriptorIdentifier.DefaultStateHash, Original._ActorCull->DescriptorIdentifier.DefaultStateHash);
    }

    const auto LookingLikePie = Owned.Create(Directory / TEXT("UEDPIE_9_Base"));
    const auto OrdinaryCopy = DuplicateBlueprint(*this, Source.Get(), LookingLikePie.Get());
    if (NOT OrdinaryCopy.IsValid())
    { return false; }
    const auto OrdinaryDescriptor = Describe(*this, OrdinaryCopy->GeneratedClass.Get(), 11);
    if (NOT OrdinaryDescriptor.Get_IsValid())
    { return false; }
    TestEqual(TEXT("Authored PIE-looking package has no PIE metadata"), LookingLikePie->GetPIEInstanceID(), INDEX_NONE);
    TestTrue(TEXT("Unflagged authored PIE-looking name is never stripped"),
        OrdinaryDescriptor._Authored->DescriptorIdentifier.Value != Original._Authored->DescriptorIdentifier.Value);
    TestTrue(TEXT("Authored PIE-looking name also stays distinct for the Actor cull state"),
        OrdinaryDescriptor._ActorCull->DescriptorIdentifier.Value != Original._ActorCull->DescriptorIdentifier.Value);

    const auto Mismatched = Owned.Create(Directory / TEXT("UEDPIE_4_Base"));
    if (NOT TestNotNull(TEXT("Mismatched metadata test package exists"), Mismatched.Get()))
    { return false; }
    const auto MismatchedCopy = DuplicateBlueprint(*this, Source.Get(), Mismatched.Get());
    if (NOT MismatchedCopy.IsValid())
    { return false; }
    Mismatched->SetPIEInstanceID(5);
    Mismatched->SetPackageFlags(PKG_PlayInEditor);
    const auto MismatchedDescriptor = Describe(*this, MismatchedCopy->GeneratedClass.Get(), 11);
    if (NOT MismatchedDescriptor.Get_IsValid())
    { return false; }
    TestTrue(TEXT("PIE metadata mismatch never canonicalizes another instance's prefix"),
        MismatchedDescriptor._Authored->DescriptorIdentifier.Value != Original._Authored->DescriptorIdentifier.Value);
    TestTrue(TEXT("PIE metadata mismatch also preserves the Actor cull state's distinct identity"),
        MismatchedDescriptor._ActorCull->DescriptorIdentifier.Value != Original._ActorCull->DescriptorIdentifier.Value);
    return true;
}

#endif

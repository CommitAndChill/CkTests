#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

namespace
{

struct FCkIrisPIEDescriptorFixture
{
    UPackage* Package = nullptr;
    UScriptStruct* Struct = nullptr;
    TRefCountPtr<const UE::Net::FReplicationStateDescriptor> Descriptor;
};

FCkIrisPIEDescriptorFixture MakeCkIrisPIEDescriptorFixture(const FString& PackageName, int32 PIEInstanceID,
    const TCHAR* StructName)
{
    FCkIrisPIEDescriptorFixture Fixture;
    Fixture.Package = CreatePackage(*PackageName);
    if (Fixture.Package == nullptr)
    {
        return Fixture;
    }

    if (PIEInstanceID != INDEX_NONE)
    {
        Fixture.Package->SetPackageFlags(PKG_PlayInEditor);
        Fixture.Package->SetPIEInstanceID(PIEInstanceID);
    }

    Fixture.Struct = NewObject<UScriptStruct>(Fixture.Package, FName(StructName), RF_Public);
    if (Fixture.Struct == nullptr)
    {
        return Fixture;
    }

    Fixture.Struct->AddCppProperty(new FIntProperty(Fixture.Struct, FName(TEXT("Value")), RF_Public));
    Fixture.Struct->Bind();
    Fixture.Struct->PrepareCppStructOps();
    Fixture.Struct->StaticLink(/*bRelinkExistingProperties*/ true);
    Fixture.Descriptor = UE::Net::FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(Fixture.Struct);
    return Fixture;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCkIrisDescriptorPIEIdentity,
    "Ck.Gym.Catalog.PIEIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCkIrisDescriptorPIEIdentity::RunTest(const FString& Parameters)
{
    // A per-run root avoids reusing a package or reflected struct left by an earlier run.
    const FString Root = FString::Printf(TEXT("/Temp/CkIrisDescriptor_%s"),
        *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString Authored = Root / TEXT("Schema");
    const FString OtherAuthored = Root / TEXT("OtherSchema");

    const FCkIrisPIEDescriptorFixture Base = MakeCkIrisPIEDescriptorFixture(Authored, INDEX_NONE, TEXT("Identity"));
    const FCkIrisPIEDescriptorFixture PIE0 = MakeCkIrisPIEDescriptorFixture(Root / TEXT("UEDPIE_0_Schema"), 0, TEXT("Identity"));
    const FCkIrisPIEDescriptorFixture PIE1 = MakeCkIrisPIEDescriptorFixture(Root / TEXT("UEDPIE_1_Schema"), 1, TEXT("Identity"));
    const FCkIrisPIEDescriptorFixture PIE2 = MakeCkIrisPIEDescriptorFixture(Root / TEXT("UEDPIE_2_Schema"), 2, TEXT("Identity"));
    const FCkIrisPIEDescriptorFixture OtherPackage = MakeCkIrisPIEDescriptorFixture(OtherAuthored, INDEX_NONE, TEXT("Identity"));
    const FCkIrisPIEDescriptorFixture OtherStruct = MakeCkIrisPIEDescriptorFixture(Authored, INDEX_NONE, TEXT("OtherIdentity"));
    const FCkIrisPIEDescriptorFixture UnmarkedPrefix = MakeCkIrisPIEDescriptorFixture(
        Root / TEXT("UEDPIE_3_Schema"), INDEX_NONE, TEXT("Identity"));
    const FCkIrisPIEDescriptorFixture MismatchedID = MakeCkIrisPIEDescriptorFixture(
        Root / TEXT("UEDPIE_4_Schema"), 5, TEXT("Identity"));

    const FCkIrisPIEDescriptorFixture* Cases[] = {
        &Base, &PIE0, &PIE1, &PIE2, &OtherPackage, &OtherStruct,
        &UnmarkedPrefix, &MismatchedID
    };
    for (const FCkIrisPIEDescriptorFixture* Case : Cases)
    {
        if (!TestTrue(TEXT("each completed reflected struct has one production descriptor"),
            Case->Package != nullptr && Case->Struct != nullptr && Case->Descriptor.IsValid() &&
            Case->Struct->GetStructureSize() == sizeof(int32) &&
            Case->Descriptor->MemberCount == 1))
        {
            return false;
        }
    }

    const uint64 BaseID = Base.Descriptor->DescriptorIdentifier.Value;
    TestEqual(TEXT("PIE 0 preserves authored descriptor identity"),
        PIE0.Descriptor->DescriptorIdentifier.Value, BaseID);
    TestEqual(TEXT("PIE 1 preserves authored descriptor identity"),
        PIE1.Descriptor->DescriptorIdentifier.Value, BaseID);
    TestEqual(TEXT("PIE 2 preserves authored descriptor identity"),
        PIE2.Descriptor->DescriptorIdentifier.Value, BaseID);
    TestNotEqual(TEXT("another authored package remains distinct"),
        OtherPackage.Descriptor->DescriptorIdentifier.Value, BaseID);
    TestNotEqual(TEXT("another reflected struct name remains distinct"),
        OtherStruct.Descriptor->DescriptorIdentifier.Value, BaseID);
    TestNotEqual(TEXT("unmarked prefix-like package is not normalized"),
        UnmarkedPrefix.Descriptor->DescriptorIdentifier.Value, BaseID);
    TestNotEqual(TEXT("package prefix mismatched to PIE id is not normalized"),
        MismatchedID.Descriptor->DescriptorIdentifier.Value, BaseID);
    return true;
}

#endif

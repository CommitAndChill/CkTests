#include "CkTests.h"

#include "Private/HexGrid/CkHexGrid_ScaleProbe.h"
#include "Private/UnitTests/CkRuntimeMesh/CkRuntimeMesh_CookProbe.h"

#define LOCTEXT_NAMESPACE "FCkTestsModule"

void FCkTestsModule::StartupModule()
{
    ck::tests::hex_scale::Startup();
#if WITH_DEV_AUTOMATION_TESTS
	ck::tests::runtimemesh::StartCookProbeIfRequested();
#endif
}

void FCkTestsModule::ShutdownModule()
{
    ck::tests::hex_scale::Shutdown();
#if WITH_DEV_AUTOMATION_TESTS
	ck::tests::runtimemesh::StopCookProbe();
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCkTestsModule, CkTests)

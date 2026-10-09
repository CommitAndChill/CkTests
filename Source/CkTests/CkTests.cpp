#include "CkTests.h"

#include "Private/UnitTests/CkRuntimeMesh/CkRuntimeMesh_CookProbe.h"

#define LOCTEXT_NAMESPACE "FCkTestsModule"

void FCkTestsModule::StartupModule()
{
#if WITH_DEV_AUTOMATION_TESTS
	ck::tests::runtimemesh::StartCookProbeIfRequested();
#endif
}

void FCkTestsModule::ShutdownModule()
{
#if WITH_DEV_AUTOMATION_TESTS
	ck::tests::runtimemesh::StopCookProbe();
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCkTestsModule, CkTests)

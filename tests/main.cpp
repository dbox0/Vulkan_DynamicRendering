// Engine tests. No Vulkan or SDL: this target links glm and nlohmann only,
// so it builds and runs anywhere, including CI. Exit code is the failure
// count.

#include <cstdio>

#include "TestFramework.h"
#include "../src/assets/AssetTypes.h"
#include "../src/scene/SceneTypes.h"
#include "../src/reflect/Reflection.h"

void runReflectionTests();
void runEditRecorderTests();
void runUndoTests();
void runValidateTest();

int main()
{
    // The same registration the application does at startup.
    registerSceneTypes();
    registerAssetTypes();

    runReflectionTests();
    runEditRecorderTests();
    runUndoTests();
    runValidateTest();

    std::printf("\n%d checks, %d failed\n", test::checks, test::failures);
    return test::failures;
}

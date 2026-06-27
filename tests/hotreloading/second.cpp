#include <cstdio>
#include "player.hpp"
#include <Reflection/Reflection.hpp>
#include <Reflection/Registry.hpp>

using namespace Sew::Reflect;

Player player;

extern "C" __attribute__((visibility("default"))) Reflection* getReflection() {
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void start() {
    // V2 start — do not call this!
}

extern "C" __attribute__((visibility("default"))) void tick() {
    printf("Player health: %d\n", player.health);
}

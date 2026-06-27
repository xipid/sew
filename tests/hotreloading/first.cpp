#include <cstdio>
#include "player.hpp"
#include <Reflection/Reflection.hpp>
#include <Reflection/Registry.hpp>

using namespace Sew::Reflect;

Player player;

static Reflection* s_reflPointer = nullptr;

extern "C" __attribute__((visibility("default"))) Reflection* getReflection() {
    return s_reflPointer;
}

extern "C" __attribute__((visibility("default"))) void start() {
    player.health = 100;
    player.score  = 0;

    static Reflection refl = reflect(player);
    s_reflPointer = &refl;
}

extern "C" __attribute__((visibility("default"))) void tick() {
    printf("Player health: %d\n", player.health);
}

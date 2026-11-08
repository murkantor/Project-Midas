#include <stdio.h>
#include <stdlib.h>
#include <ultra64.h>
#include <bondtypes.h>
#include "game/chr.h"
#include "game/chraction.h"

static void expectAngle(const char *name, f32 actual, f32 expected)
{
    f32 difference = actual - expected;
    if (!(difference >= -0.0001f && difference <= 0.0001f)) {
        fprintf(stderr, "%s: got %.4f, expected %.4f\n", name, actual, expected);
        abort();
    }
}

static void testAim(struct weapon_firing_animation_table *table, s32 same, s32 swap,
    f32 aim, f32 left, f32 right, f32 back)
{
    ChrRecord chr = {0};
    chrlvUpdateAimendbackShoulders(&chr, table, same, swap, aim);
    expectAngle("left shoulder", chr.aimendlshoulder, left);
    expectAngle("right shoulder", chr.aimendrshoulder, right);
    expectAngle("back", chr.aimendback, back);
}

int main(void)
{
    struct weapon_firing_animation_table table = {0};
    table.aim_end_frame = 75.0f;
    table.max_up = 0.8f;
    table.max_down = -0.6f;
    table.max_left = 0.9f;
    table.max_right = -0.7f;
    table.free_arm_frac_up = 1.5f;
    table.free_arm_frac_down = 1.6f;

    testAim(&table, 0, 0, 0, 0, 0, 0);
    testAim(&table, 0, 0, 0.4f, 0.4f, 0.6f, 0);
    testAim(&table, 0, 0, -0.3f, -0.3f, -0.48f, 0);
    testAim(&table, 0, 0, 1.2f, 0.8f, 1.2f, 0.4f);
    testAim(&table, 0, 0, -1.2f, -0.6f, -0.96f, -0.6f);
    testAim(&table, 0, 1, 0.4f, 0.6f, 0.4f, 0);
    testAim(&table, 1, 1, 0.4f, 0.4f, 0.4f, 0);
    testAim(&table, 1, 0, 0.4f, 0.4f, 0.6f, 0);
    testAim(NULL, 0, 0, 0.5f, 0.5f, 0, 0);
    testAim(NULL, 1, 1, -0.5f, -0.5f, -0.5f, 0);
    puts("native aim tests passed");
    return 0;
}

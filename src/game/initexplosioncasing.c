#include <ultra64.h>
#include "game/lv.h"
#include "game/player.h"
#include <memp.h>
#include "initexplosioncasing.h"
#include "explosion.h"

#ifndef DEBUG
    #ifdef TARGET_N64
        #define osSyncPrintf()
    #else
        #define osSyncPrintf(...) /* PC: gcc requires variadic for arg'd calls */
    #endif
#endif

void alloc_explosion_smoke_casing_scorch_impact_buffers(void)
{
    s32 i;
    s32 j;

    g_NumExplosionEntries = 0;
#ifndef TARGET_N64
    {
        extern s32 g_PortExplosionPastSix;   /* explosion.c's once-per-stage log */
        g_PortExplosionPastSix = 0;
    }
#endif
    g_NumSmokeEntries = 0;
    g_NumParticleEntries = 0;
    g_NumScorchEntries = 0;
    g_NumImpactEntries = 0;
    g_SpExplosionDamageMult = 1.0f;

    osSyncPrintf("Allocating %d bytes for explosion data\n", EXPLOSION_BUFFER_LEN * sizeof(struct Explosion));
    g_ExplosionBuffer = (struct Explosion *)mempAllocBytesInBank(EXPLOSION_BUFFER_LEN * sizeof(struct Explosion), MEMPOOL_STAGE);

    for (i=0; i<EXPLOSION_BUFFER_LEN; i++)
    {
        g_ExplosionBuffer[i].prop = NULL;

        for (j=0; j<EXPLOSION_PARTS_LEN; j++)
        {
            g_ExplosionBuffer[i].parts[j].frame = 0;
        }
    }

    osSyncPrintf("Allocating %d bytes for smoke data\n", SMOKE_BUFFER_LEN * sizeof(struct Smoke));
    g_SmokeBuffer = (struct Smoke *)mempAllocBytesInBank(SMOKE_BUFFER_LEN * sizeof(struct Smoke), MEMPOOL_STAGE);

    for (i=0; i<SMOKE_BUFFER_LEN; i++)
    {
        g_SmokeBuffer[i].prop = NULL;

        for (j=0; j<SMOKE_PARTS_LEN; j++)
        {
            g_SmokeBuffer[i].parts[j].size = 0.0f;
        }
    }

    if (getPlayerCount() == 1)
    {
        osSyncPrintf("Allocating %d bytes for scorch data\n", SCORCH_BUFFER_LEN * sizeof(struct Scorch));
        // scorches are the circle burn marks left on the ground from explosions
        g_ScorchBuffer = (struct Scorch *)mempAllocBytesInBank(SCORCH_BUFFER_LEN * sizeof(struct Scorch), MEMPOOL_STAGE);

        for (i=0; i<SCORCH_BUFFER_LEN; i++)
        {
            g_ScorchBuffer[i].roomid = -1;
        }
    }

    osSyncPrintf("Allocating %d bytes for wallhit data\n", BULLET_IMPACT_BUFFER_LEN * sizeof(struct BulletImpact));
    g_BulletImpactBuffer = (struct BulletImpact *)mempAllocBytesInBank(BULLET_IMPACT_BUFFER_LEN * sizeof(struct BulletImpact), MEMPOOL_STAGE);

    for (i=0; i<BULLET_IMPACT_BUFFER_LEN; i++)
    {
        g_BulletImpactBuffer[i].room = -1;
    }

    max_particles = MAX_FLYING_PARTICLES / getPlayerCount();

    if ((lvlGetCurrentStageToLoad() == LEVELID_STREETS) || (lvlGetCurrentStageToLoad() == LEVELID_DEPOT))
    {
        max_particles = (s32) max_particles >> 1;
    }

    osSyncPrintf("Allocating %d bytes for debris data (%d bits)\n", max_particles * sizeof(struct FlyingParticles), max_particles);
    g_FlyingParticlesBuffer = (struct FlyingParticles *)mempAllocBytesInBank(((max_particles * sizeof(struct FlyingParticles)) + 0xF) & ~0xF, MEMPOOL_STAGE);

    for (i=0; i<max_particles; i++)
    {
        g_FlyingParticlesBuffer[i].unk00 = 0;
    }
}

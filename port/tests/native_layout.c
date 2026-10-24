#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ultra64.h>
#include <bondtypes.h>
#include <bondconstants.h>
#include "mema.h"
#include "romfile_model64.h"

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); } } while (0)

extern ALBankFile *portBankFileConvert(const u8 *, u32, const u8 *);

/* The allocator's diagnostics are independent of its allocation bookkeeping. */
void debTryAdd(void *data, const char *name) { (void)data; (void)name; }
s32 portRoomAuditEnabled(void) { return 0; }
void portRoomAuditOnHeapReset(void) {}
int portSprintf(char *dst, const char *fmt, ...) { (void)dst; (void)fmt; return 0; }

static void be16(u8 *p, u16 value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static void be32(u8 *p, u32 value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

static void testHeap(void)
{
    void *heap = malloc(4096);
    void *a, *b;
    CHECK(heap != NULL);
    CHECK((uintptr_t)heap > UINT32_MAX);
    memaReset(heap, 4096);
    CHECK(memaGetLongestFree() == 4096);
    a = memaAlloc(256);
    b = memaAlloc(512);
    CHECK(a == heap);
    CHECK(b == (u8 *)heap + 256);
    memaFree(a, 256);
    CHECK(memaRealloc((intptr_t)b, 512, 1024));
    memaFree(b, 1024);
    CHECK(memaGetLongestFree() == 4096);
    CHECK(memaAlloc(4096) == heap);
    CHECK(memaAlloc(16) == NULL);
    free(heap);

    /* No bytes are accessed: exercise a grow whose return address is exactly 4 GiB. */
    heap = (void *)(uintptr_t)0xffffff00;
    memaReset(heap, 4096);
    CHECK(memaAlloc(256) == heap);
    CHECK(memaRealloc((intptr_t)heap, 256, 512));
    CHECK(memaGetLongestFree() == 3584);
}

static void testModel(void)
{
    u8 raw[256] = {0};
    ModelFileHeader header = {0};
    ModelRoData_DisplayListPrimaryRecord *dl;
    u16 texel;

    header.numSwitches = 1;
    header.numtextures = 1;
    be32(raw, 0x05000010);
    be32(raw + 4, 0x050000e0);
    raw[8] = 2;
    raw[9] = 1;
    raw[12] = G_IM_SIZ_16b;
    be16(raw + 0x10, MODELNODE_OPCODE_DLPRIMARY);
    be32(raw + 0x14, 0x05000028);
    be32(raw + 0x28, 2); /* Star gunfire counts quads, not individual vertices. */
    be32(raw + 0x2c, 0x05000040);
    be32(raw + 0x30, 0x050000c0);
    for (s32 i = 0; i < 8; i++) {
        u8 *vertex = raw + 0x40 + i * 16;
        be16(vertex, (u16)(-2 - i));
        be16(vertex + 2, 3 + i);
        be16(vertex + 4, 4 + i);
        be16(vertex + 8, (u16)(-300 - i));
        be16(vertex + 10, 512 + i);
        vertex[12] = 0x12;
        vertex[13] = 0x34;
        vertex[14] = 0x56;
        vertex[15] = 0x78;
    }
    be32(raw + 0xc0, 0x04700080);
    be32(raw + 0xc4, 0x05000040);
    be32(raw + 0xc8, 0xfd100001);
    be32(raw + 0xcc, 0x050000e0);
    be32(raw + 0xd0, 0xb8000000);
    be16(raw + 0xe0, 0xf801);
    be16(raw + 0xe2, 0x07c1);

    modelConvertN64Binary(&header, raw, sizeof(raw));
    CHECK(header.RootNode != NULL);
    CHECK(header.Switches[0] == header.RootNode);
    dl = &header.RootNode->Data->DisplayListPrimary;
    CHECK(dl->numVertices == 2);
    for (s32 i = 0; i < 8; i++) {
        const Vertex *vertex = &dl->Vertices[i];
        CHECK(vertex->coord.x == -2 - i);
        CHECK(vertex->coord.y == 3 + i);
        CHECK(vertex->coord.z == 4 + i);
        CHECK(vertex->s == -300 - i);
        CHECK(vertex->t == 512 + i);
        CHECK(memcmp((const u8 *)vertex + 12, "\x12\x34\x56\x78", 4) == 0);
    }
    CHECK(dl->Primary[0].words.w1 == (uintptr_t)(raw + 0x40));
    CHECK(dl->Primary[1].words.w1 == (uintptr_t)(raw + 0xe0));
    CHECK(dl->Primary[2].words.w0 == 0xb8000000);
    memcpy(&texel, raw + 0xe0, 2);
    CHECK(texel == 0xf801);
    memcpy(&texel, raw + 0xe2, 2);
    CHECK(texel == 0x07c1);
    modelConvertFreeAll();
}

static void testBank(void)
{
    u8 raw[256] = {0}, samples[16] = {0};
    ALBankFile *file;
    ALInstrument *instrument;

    be16(raw, 0x4231);
    be16(raw + 2, 1);
    be32(raw + 4, 8);
    be16(raw + 8, 1);
    be32(raw + 12, 22050);
    be32(raw + 20, 24);
    raw[24] = 127;
    be16(raw + 38, 2);
    be32(raw + 40, 48);
    be32(raw + 44, 64);
    for (s32 i = 0; i < 2; i++) {
        u8 *sound = raw + 48 + i * 16;
        u8 *wave = raw + 104 + i * 20;
        be32(sound, 80);
        be32(sound + 4, 96);
        be32(sound + 8, 104 + i * 20);
        sound[12] = 64;
        sound[13] = 127 - i;
        be32(wave, i * 4);
        be32(wave + 4, 4);
        wave[8] = AL_ADPCM_WAVE;
        be32(wave + 12, 152);
        be32(wave + 16, 200);
    }
    be32(raw + 80, 123);
    raw[92] = 127;
    raw[100] = 60;
    be32(raw + 152, 2);
    be32(raw + 156, 16);
    be16(raw + 164, (u16)-123);
    be32(raw + 200, 2);
    be32(raw + 204, 1);
    be16(raw + 208, (u16)-456);

    CHECK(portBankFileConvert(raw, 3, samples) == NULL);
    file = portBankFileConvert(raw, sizeof(raw), samples);
    CHECK(file != NULL);
    CHECK(file->bankCount == 1);
    CHECK(file->bankArray[0]->sampleRate == 22050);
    instrument = file->bankArray[0]->instArray[0];
    CHECK(instrument->soundCount == 2);
    CHECK(instrument->soundArray[0]->sampleVolume == 127);
    CHECK(instrument->soundArray[1]->sampleVolume == 126);
    CHECK(instrument->soundArray[1]->wavetable->base == samples + 4);
    CHECK(instrument->soundArray[0]->envelope->attackTime == 123);
    CHECK(instrument->soundArray[0]->keyMap->keyBase == 60);
    CHECK(instrument->soundArray[0]->wavetable->waveInfo.adpcmWave.loop->state[0] == -123);
    CHECK(instrument->soundArray[0]->wavetable->waveInfo.adpcmWave.book->book[0] == -456);
    for (s32 i = 0; i < 2; i++) {
        ALSound *sound = instrument->soundArray[i];
        free(sound->wavetable->waveInfo.adpcmWave.loop);
        free(sound->wavetable->waveInfo.adpcmWave.book);
        free(sound->wavetable);
        free(sound->keyMap);
        free(sound->envelope);
        free(sound);
    }
    free(instrument);
    free(file->bankArray[0]);
    free(file);
}

static void testRawBank(void)
{
    u8 raw[192] = {0};
    const u8 samples[12] = {0, 0, 0, 0, 0x12, 0x34, 0xed, 0xcc, 0x7f, 0xff, 0x80, 0};
    const s16 expected[] = {0x1234, -0x1234, 32767, -32768};
    ALBankFile *file;
    ALInstrument *instrument;

    be16(raw, 0x4231);
    be16(raw + 2, 1);
    be32(raw + 4, 8);
    be16(raw + 8, 1);
    be32(raw + 12, 22050);
    be32(raw + 20, 24);
    be16(raw + 38, 2);
    be32(raw + 40, 48);
    be32(raw + 44, 64);
    be32(raw + 56, 104);
    be32(raw + 72, 104); /* Two instruments/sounds can share one sample. */
    be32(raw + 104, 4);
    be32(raw + 108, sizeof(expected));
    raw[112] = AL_RAW16_WAVE;
    be32(raw + 116, 128);
    be32(raw + 128, 1);
    be32(raw + 132, 4);
    be32(raw + 136, 0xffffffff);

    file = portBankFileConvert(raw, sizeof(raw), samples);
    CHECK(file != NULL);
    instrument = file->bankArray[0]->instArray[0];
    for (s32 i = 0; i < 2; i++) {
        ALSound *sound = instrument->soundArray[i];
        ALWaveTable *wave = sound->wavetable;
        CHECK(wave->len == sizeof(expected));
        CHECK(memcmp(wave->base, expected, sizeof(expected)) == 0);
        CHECK(wave->base != samples + 4);
        CHECK(wave->waveInfo.rawWave.loop->start == 1);
        CHECK(wave->waveInfo.rawWave.loop->end == 4);
        CHECK(wave->waveInfo.rawWave.loop->count == 0xffffffff);
        free(wave->base);
        free(wave->waveInfo.rawWave.loop);
        free(wave);
        free(sound);
    }
    CHECK(samples[4] == 0x12 && samples[5] == 0x34);
    free(instrument);
    free(file->bankArray[0]);
    free(file);
}

int main(void)
{
    testHeap();
    for (s32 i = 0; i < 3; i++) testModel();
    testBank();
    testRawBank();
    puts("native layout tests passed");
    return 0;
}

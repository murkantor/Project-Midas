/* Bank conversion adapted from MGB64 (MIT), commit 0d1d40b4cde18302ec9789853a50f5545e679062.
 * See tools/THIRD_PARTY_NOTICES.txt for the copyright and license. */
#ifdef GE_NATIVE_64
#include <ultra64.h>
#include <PR/libaudio.h>
#include <stdlib.h>
#include <string.h>
static int bank_ctl_has(u32 size, u32 offset, u32 count)
{
    return offset <= size && count <= size - offset;
}

static u16 bank_ctl_u16(const u8 *data)
{
    return ((u16)data[0] << 8) | (u16)data[1];
}

static s16 bank_ctl_s16(const u8 *data)
{
    return (s16)bank_ctl_u16(data);
}

static u32 bank_ctl_u32(const u8 *data)
{
    return ((u32)data[0] << 24) | ((u32)data[1] << 16) |
           ((u32)data[2] << 8) | (u32)data[3];
}

static s32 bank_ctl_s32(const u8 *data)
{
    return (s32)bank_ctl_u32(data);
}

static ALADPCMBook *bank_make_adpcm_book(const u8 *ctl, u32 ctl_size,
                                         u32 offset)
{
    s32 order;
    s32 predictor_count;
    s32 coefficient_count;
    ALADPCMBook *book;
    s32 i;

    if (!bank_ctl_has(ctl_size, offset, 8)) {
        return NULL;
    }

    order = bank_ctl_s32(ctl + offset);
    predictor_count = bank_ctl_s32(ctl + offset + 4);
    if (order <= 0 || order > 16 ||
        predictor_count <= 0 || predictor_count > 16) {
        return NULL;
    }

    coefficient_count = order * predictor_count * 8;
    if (!bank_ctl_has(ctl_size, offset + 8, (u32)coefficient_count * 2)) {
        return NULL;
    }

    book = calloc(1, sizeof(*book) +
                         (u32)(coefficient_count - 1) * sizeof(book->book[0]));
    if (book == NULL) {
        return NULL;
    }

    book->order = order;
    book->npredictors = predictor_count;
    for (i = 0; i < coefficient_count; i++) {
        book->book[i] = bank_ctl_s16(ctl + offset + 8 + (u32)i * 2);
    }

    return book;
}

static ALADPCMloop *bank_make_adpcm_loop(const u8 *ctl, u32 ctl_size,
                                         u32 offset)
{
    ALADPCMloop *loop;
    s32 i;

    if (!bank_ctl_has(ctl_size, offset, 44)) {
        return NULL;
    }

    loop = calloc(1, sizeof(*loop));
    if (loop == NULL) {
        return NULL;
    }

    loop->start = bank_ctl_u32(ctl + offset);
    loop->end = bank_ctl_u32(ctl + offset + 4);
    loop->count = bank_ctl_u32(ctl + offset + 8);
    for (i = 0; i < 16; i++) {
        loop->state[i] = bank_ctl_s16(ctl + offset + 12 + (u32)i * 2);
    }

    return loop;
}

static ALRawLoop *bank_make_raw_loop(const u8 *ctl, u32 ctl_size, u32 offset)
{
    ALRawLoop *loop;

    if (!bank_ctl_has(ctl_size, offset, 12)) {
        return NULL;
    }

    loop = calloc(1, sizeof(*loop));
    if (loop == NULL) {
        return NULL;
    }

    loop->start = bank_ctl_u32(ctl + offset);
    loop->end = bank_ctl_u32(ctl + offset + 4);
    loop->count = bank_ctl_u32(ctl + offset + 8);
    return loop;
}

static ALEnvelope *bank_make_envelope(const u8 *ctl, u32 ctl_size, u32 offset)
{
    ALEnvelope *envelope;

    if (!bank_ctl_has(ctl_size, offset, 14)) {
        return NULL;
    }

    envelope = calloc(1, sizeof(*envelope));
    if (envelope == NULL) {
        return NULL;
    }

    envelope->attackTime = bank_ctl_s32(ctl + offset);
    envelope->decayTime = bank_ctl_s32(ctl + offset + 4);
    envelope->releaseTime = bank_ctl_s32(ctl + offset + 8);
    envelope->attackVolume = ctl[offset + 12];
    envelope->decayVolume = ctl[offset + 13];
    return envelope;
}

static ALKeyMap *bank_make_keymap(const u8 *ctl, u32 ctl_size, u32 offset)
{
    ALKeyMap *keymap;

    if (!bank_ctl_has(ctl_size, offset, 6)) {
        return NULL;
    }

    keymap = calloc(1, sizeof(*keymap));
    if (keymap == NULL) {
        return NULL;
    }

    keymap->velocityMin = ctl[offset];
    keymap->velocityMax = ctl[offset + 1];
    keymap->keyMin = ctl[offset + 2];
    keymap->keyMax = ctl[offset + 3];
    keymap->keyBase = ctl[offset + 4];
    keymap->detune = (s8)ctl[offset + 5];
    return keymap;
}

static ALWaveTable *bank_make_wavetable(const u8 *ctl, u32 ctl_size,
                                        u32 offset, uintptr_t table_base)
{
    ALWaveTable *wave;
    u32 loop_offset;
    u32 book_offset;

    if (!bank_ctl_has(ctl_size, offset, 12)) {
        return NULL;
    }

    wave = calloc(1, sizeof(*wave));
    if (wave == NULL) {
        return NULL;
    }

    wave->base = (u8 *)(table_base + bank_ctl_u32(ctl + offset));
    wave->len = bank_ctl_s32(ctl + offset + 4);
    wave->type = ctl[offset + 8];
    wave->flags = 1;

    if (wave->type == AL_ADPCM_WAVE) {
        if (!bank_ctl_has(ctl_size, offset, 20)) {
            free(wave);
            return NULL;
        }
        loop_offset = bank_ctl_u32(ctl + offset + 12);
        book_offset = bank_ctl_u32(ctl + offset + 16);
        if (loop_offset != 0) {
            wave->waveInfo.adpcmWave.loop =
                bank_make_adpcm_loop(ctl, ctl_size, loop_offset);
        }
        if (book_offset != 0) {
            wave->waveInfo.adpcmWave.book =
                bank_make_adpcm_book(ctl, ctl_size, book_offset);
        }
    } else if (wave->type == AL_RAW16_WAVE) {
        const u8 *source = wave->base;
        s16 *samples;
        s32 i;

        if (!bank_ctl_has(ctl_size, offset, 16) ||
            wave->len < 0 || (wave->len & 1)) {
            free(wave);
            return NULL;
        }
        /* RAW16 is big-endian PCM; ADPCM stays a byte stream. Keep the ROM
         * immutable because multiple instruments can share the same sample.
         * The decoder rounds DMA reads to eight bytes, so retain tail padding. */
        samples = calloc(1, (size_t)wave->len + 8);
        if (samples == NULL) {
            free(wave);
            return NULL;
        }
        for (i = 0; i < wave->len / 2; i++) {
            samples[i] = bank_ctl_s16(source + i * 2);
        }
        wave->base = (u8 *)samples;
        loop_offset = bank_ctl_u32(ctl + offset + 12);
        if (loop_offset != 0) {
            wave->waveInfo.rawWave.loop =
                bank_make_raw_loop(ctl, ctl_size, loop_offset);
        }
    }

    return wave;
}

static ALSound *bank_make_sound(const u8 *ctl, u32 ctl_size, u32 offset,
                                uintptr_t table_base)
{
    ALSound *sound;
    u32 envelope_offset;
    u32 keymap_offset;
    u32 wavetable_offset;

    if (!bank_ctl_has(ctl_size, offset, 15)) {
        return NULL;
    }

    sound = calloc(1, sizeof(*sound));
    if (sound == NULL) {
        return NULL;
    }

    envelope_offset = bank_ctl_u32(ctl + offset);
    keymap_offset = bank_ctl_u32(ctl + offset + 4);
    wavetable_offset = bank_ctl_u32(ctl + offset + 8);
    if (envelope_offset != 0) {
        sound->envelope = bank_make_envelope(ctl, ctl_size, envelope_offset);
    }
    if (keymap_offset != 0) {
        sound->keyMap = bank_make_keymap(ctl, ctl_size, keymap_offset);
    }
    if (wavetable_offset != 0) {
        sound->wavetable =
            bank_make_wavetable(ctl, ctl_size, wavetable_offset, table_base);
    }
    sound->samplePan = ctl[offset + 12];
    sound->sampleVolume = ctl[offset + 13];
    sound->flags = 1;
    return sound;
}

static ALInstrument *bank_make_instrument(const u8 *ctl, u32 ctl_size,
                                          u32 offset, uintptr_t table_base)
{
    ALInstrument *instrument;
    s16 sound_count;
    u32 extra_sounds;
    s32 i;

    if (!bank_ctl_has(ctl_size, offset, 16)) {
        return NULL;
    }

    sound_count = bank_ctl_s16(ctl + offset + 14);
    if (sound_count < 0 ||
        !bank_ctl_has(ctl_size, offset + 16, (u32)sound_count * 4)) {
        return NULL;
    }

    extra_sounds = sound_count > 1 ? (u32)(sound_count - 1) : 0;
    instrument = calloc(1, sizeof(*instrument) +
                               extra_sounds *
                                   sizeof(instrument->soundArray[0]));
    if (instrument == NULL) {
        return NULL;
    }

    instrument->volume = ctl[offset];
    instrument->pan = ctl[offset + 1];
    instrument->priority = ctl[offset + 2];
    instrument->flags = 1;
    instrument->tremType = ctl[offset + 4];
    instrument->tremRate = ctl[offset + 5];
    instrument->tremDepth = ctl[offset + 6];
    instrument->tremDelay = ctl[offset + 7];
    instrument->vibType = ctl[offset + 8];
    instrument->vibRate = ctl[offset + 9];
    instrument->vibDepth = ctl[offset + 10];
    instrument->vibDelay = ctl[offset + 11];
    instrument->bendRange = bank_ctl_s16(ctl + offset + 12);
    instrument->soundCount = sound_count;

    for (i = 0; i < sound_count; i++) {
        u32 sound_offset = bank_ctl_u32(ctl + offset + 16 + (u32)i * 4);
        if (sound_offset != 0) {
            instrument->soundArray[i] =
                bank_make_sound(ctl, ctl_size, sound_offset, table_base);
        }
    }

    return instrument;
}

static ALBank *bank_make_bank(const u8 *ctl, u32 ctl_size, u32 offset,
                              uintptr_t table_base)
{
    ALBank *bank;
    s16 instrument_count;
    u32 percussion_offset;
    u32 extra_instruments;
    s32 i;

    if (!bank_ctl_has(ctl_size, offset, 12)) {
        return NULL;
    }

    instrument_count = bank_ctl_s16(ctl + offset);
    if (instrument_count < 0 ||
        !bank_ctl_has(ctl_size, offset + 12, (u32)instrument_count * 4)) {
        return NULL;
    }

    extra_instruments =
        instrument_count > 1 ? (u32)(instrument_count - 1) : 0;
    bank = calloc(1, sizeof(*bank) +
                         extra_instruments * sizeof(bank->instArray[0]));
    if (bank == NULL) {
        return NULL;
    }

    bank->instCount = instrument_count;
    bank->flags = 1;
    bank->sampleRate = bank_ctl_s32(ctl + offset + 4);
    percussion_offset = bank_ctl_u32(ctl + offset + 8);
    if (percussion_offset != 0) {
        bank->percussion =
            bank_make_instrument(ctl, ctl_size, percussion_offset, table_base);
    }

    for (i = 0; i < instrument_count; i++) {
        u32 instrument_offset = bank_ctl_u32(ctl + offset + 12 + (u32)i * 4);
        if (instrument_offset != 0) {
            bank->instArray[i] =
                bank_make_instrument(ctl, ctl_size, instrument_offset,
                                     table_base);
        }
    }

    return bank;
}


ALBankFile *portBankFileConvert(const u8 *ctl, u32 size, const u8 *samples)
{
    if (!ctl || !samples || size < 4) return NULL;
    s16 count = bank_ctl_s16(ctl + 2);
    if (count <= 0 || !bank_ctl_has(size, 4, (u32)count * 4)) return NULL;
    ALBankFile *file = calloc(1, sizeof(*file) + (count - 1) * sizeof(file->bankArray[0]));
    if (!file) return NULL;
    file->revision = bank_ctl_s16(ctl);
    file->bankCount = count;
    for (s32 i = 0; i < count; i++) {
        u32 offset = bank_ctl_u32(ctl + 4 + i * 4);
        file->bankArray[i] = offset ? bank_make_bank(ctl, size, offset, (uintptr_t)samples) : NULL;
    }
    return file;
}

#endif

#include <ultra64.h>
#include "ramrom.h"
#include "macro.h"
#include "ob.h"
#include <deb.h>
#include <memp.h>
#include <assets/obseg/obseg.h>
#include "decompress.h"
#include "indy_comms.h"
#include "assets/obseg/file_resource_id_enums.h"
#ifndef TARGET_N64
#include "romdata.h" /* ROM loader: romdataFilesApply / romdataFileServed / romdataFilePreprocess */
void portHwNoteFile(s32 index); /* frametiming.c: records load hitches for tickhz=hw */
#endif

//bss
//800888b0

 resource_lookup_data_entry resource_lookup_data_array[OBJ_INDEX_MAX];


// data
//D:80046050
s32 ob_c_debug_notice_list_entry = 0;


#include <assets/obseg/file_resource_table.inc.c>
/* struct fileentry file_resource_table[] =
   {
       blah;
    };
 */


//D:800482D4
s32 file_entry_max = OBJ_INDEX_END;




void load_resource(u8 *ptrdata, s32 bytes,  fileentry *srcfile,  resource_lookup_data_entry *lookupdata)
{
    u8 *source;
    u8  buffer[0x2100];
    s32 unused;


    if (bytes == 0)
    {
        romCopy(ptrdata, srcfile->hw_address, lookupdata->rom_size);
        return;
    }
    source = (ptrdata + bytes) - ((lookupdata->rom_size + 7) & -8);
    if ((u32) (source - ptrdata) < 8U)
    {
        lookupdata->poolRemaining = 0;
    }
    else
    {
#if DEBUG
        char sp54[128];
        u32  stack[2];
#endif

        romCopy(source, srcfile->hw_address, lookupdata->rom_size);
        lookupdata->poolRemaining = decompressdata(source, ptrdata, buffer);;
#if DEBUG
        if (result == 0)
        {
            sprintf(sp54, "DMA-Crash %s %d Ram: %02x%02x%02x%02x%02x%02x%02x%02x", "ob.c", 204, scratch[0], scratch[1], scratch[2], scratch[3], scratch[4], scratch[5], scratch[6], scratch[7]);
            crashSetMessage(sp54);
            CRASH();
        }
#endif

    }
}

void resource_load_from_indy(u8 *ptrdata, s32 bytes,  fileentry *srcfile,  resource_lookup_data_entry *lookupdata)
{
    u8 *pPayload;
    u8 buffer[8448];
    s32 size;
    static const u8 rz_header_1[] = {0x11, 0x72, 0x00, 0x00};
    static const u8 rz_header_2[] = {0x11, 0x72, 0x00, 0x00};

    if (!bytes)
    {
        indycommHostLoadFile(srcfile->filename, ptrdata);
        return;
    }
    indycommHostCheckFileExists(srcfile->filename, &lookupdata->pc_size);
    pPayload = (ptrdata + bytes) - ((lookupdata->pc_size + 7) & -8);
    if ((pPayload - ptrdata) < 8U)
    {
        lookupdata->poolRemaining = 0;
    }
    else
    {
        indycommHostLoadFile(srcfile->filename, pPayload);
        if ((pPayload[0] == rz_header_1[0]) && (pPayload[1] == rz_header_2[1]))
        {
            size = decompressdata(pPayload, ptrdata, &buffer);
        }
        else
        {
            bcopy(pPayload, ptrdata, lookupdata->pc_size);
            size = lookupdata->pc_size;
        }
        lookupdata->poolRemaining = size;
    }
}





void obInit(void)
{
    s32 size;
    s32 i;

    debTryAdd(&ob_c_debug_notice_list_entry, "ob_c_debug");
    
    for (i = 1; i < file_entry_max - 1; i++)
    {
#ifdef TARGET_N64
        size = (file_resource_table[i + 1].hw_address - file_resource_table[i].hw_address);
#else
        /* PORT: hw_address is a linked-data pointer, not a consecutive ROM
         * address, so adjacent-entry arithmetic is meaningless; loads return
         * the linked data directly and never consult rom_size, so report 0. */
        size = 0;
#endif

        resource_lookup_data_array[i].rom_size = size;
        resource_lookup_data_array[i].poolRemaining = 0;
        resource_lookup_data_array[i].pc_size = 0;
        resource_lookup_data_array[i].rom_remaining = 0;

        if (size);
    }

#ifndef TARGET_N64
    /* Files served from the ROM (port/src/romdata.c) get their ROM bytes and
     * rom_size here so the vanilla paths below run on them; everything else
     * keeps the compiled-in data. */
    romdataFilesApply();
#endif
}


#if !defined(LEFTOVERDEBUG)
/* VERSION_EU */
/* same as below version, shuffled in EU */
void obLoadBGFileBytesAtOffset(u8 *bgname, u8 *target, s32 offset, s32 len)
{
  s32 index;
   fileentry *fileentry;

  index = fileGetIndex(bgname);
  fileentry = &file_resource_table[index];

  if (resource_lookup_data_array[index].rom_size != 0)
  {
    //if the size of offset data would exceed file size, loop forever
    if ((resource_lookup_data_array[index].rom_size + 0xF) < (offset + len))
    {
      while (1){};
    }
    romCopy(target, &fileentry->hw_address[offset], len);
  }

}
#endif

#if defined(LEFTOVERDEBUG)
/* no VERSION_EU */
void *_fileIndexLoadToBank(s32 index, FILELOADMETHOD param_2, s32 size, u8 bank)
{
    return fileIndexLoadToBank(index, param_2, size, bank);
}
#endif

#if defined(LEFTOVERDEBUG)
/* no VERSION_EU */
void *_fileIndexLoadToAddr(int index, FILELOADMETHOD param_2, u8 *ptrdata, int size)
{
    return fileIndexLoadToAddr(index, param_2, ptrdata, size);
}
#endif

void *_fileNameLoadToBank(char *filename, FILELOADMETHOD loadMethod, s32 size, u8 bank)
{
    return fileIndexLoadToBank(fileGetIndex(filename), loadMethod, size, bank);
}

void * _fileNameLoadToAddr(char *filename, FILELOADMETHOD loadMethod, u8 *ptrdata, s32 size)
{
    return fileIndexLoadToAddr(fileGetIndex(filename), loadMethod, ptrdata, size);
}

#if defined(LEFTOVERDEBUG)
/* no VERSION_EU */
/**
 * 0F18AC 7F0BCD7C
 * loads data stored at an offset of a bg file
 */
#ifndef TARGET_N64
/**
 * PC: the compiled-in bg file used in place (typed C with real pointers -
 * no copy, no rebase; see load_bg_file's PC arm).
 */
u8 *obGetBGFileData(u8 *bgname)
{
    return file_resource_table[fileGetIndex(bgname)].hw_address;
}
#endif

void obLoadBGFileBytesAtOffset(u8 *bgname, u8 *target, s32 offset, s32 len)
{
  s32 index;
   fileentry *fileentry;

  index = fileGetIndex(bgname);
  fileentry = &file_resource_table[index];

#ifndef TARGET_N64
  { /* tickhz=hw load hitch: a room's compressed chunk off the cart (frametiming.c) */
    extern void portHwNoteLoad(u32 romBytes, s32 compressed);
    portHwNoteLoad((u32) len, 1);
  }
  /* PORT: compiled-in bg file (rom_size is 0), so skip the size gate and copy
   * straight out of the linked blob, undoing the 0xF1000000 segment strip the
   * room loaders apply so real host pointers read their data directly. */
  {
    u32 cand = (u32) offset + 0x0F000000u;

    /* pointer-vs-offset test must not assume a 0x10000000 address floor
     * (Windows image base is 0x400000) - same classification as
     * BG_SEG_IS_OFFSET: genuine offsets are small or 0x0F-tagged */
    if (cand >= 0x00400000u && (cand >> 24) != 0x0Fu)
    {
      /* The room binaries are u32 arrays of big-endian words, but the 1172/zlib
       * decompressors read bytes, so swap32-copy to rebuild the N64 byte image
       * (len is 16-aligned at every call site). */
      u32 *s = (u32 *) cand;
      u8 *d = target;
      s32 n = len >> 2;

      while (n-- > 0)
      {
        u32 w = *s++;
        *d++ = (u8) (w >> 24);
        *d++ = (u8) (w >> 16);
        *d++ = (u8) (w >> 8);
        *d++ = (u8) w;
      }
      return;
    }
  }
  romCopy(target, &fileentry->hw_address[offset], len);
  return;
#endif

  if (resource_lookup_data_array[index].rom_size != 0)
  {
    //if the size of offset data would exceed file size, loop forever
    if ((resource_lookup_data_array[index].rom_size + 0xF) < (offset + len))
    {
      while (1){};
    }
    romCopy(target, &fileentry->hw_address[offset], len);
  }

}
#endif





void *fileIndexLoadToBank(s32 index, FILELOADMETHOD loadMethod, s32 size, u8 bank) //#MATCH https://decomp.me/scratch/uqiBe
{
    resource_lookup_data_entry *info = &resource_lookup_data_array[index];
    s32                         bytes;
    void                       *ptrdata = NULL;

#ifndef TARGET_N64
    portHwNoteFile(index); /* tickhz=hw load hitch (frametiming.c) */
    /* PORT: compiled-in assets are returned directly (hw_address is the linked
     * data, no copy or decompress); ROM-served files and NULL hw_address fall
     * through to the vanilla load path below. */
    if (file_resource_table[index].hw_address != NULL && !romdataFileServed(index))
    {
        return file_resource_table[index].hw_address;
    }
    /* the fall-through below returns an UNINITIALIZED bank allocation for
     * any asset that never got compiled in - say so before the caller
     * walks garbage */
    if (file_resource_table[index].hw_address == NULL)
    {
        printf("ge: fileIndexLoadToBank: NO LINKED DATA for file index %d (bank %d)\n",
               index, bank);
    }
#endif

    if (loadMethod == FILELOADMETHOD_EXTRAMEM || loadMethod == FILELOADMETHOD_DEFAULT || loadMethod == 2)
    {
        // bytes = info->poolRemaining;
        if (info->poolRemaining == 0)
        { // verify pool remaining is 0
            info->poolRemaining = mempGetBankSizeLeft(bank);
            // info->poolRemaining = bytes;
        }
#ifndef TARGET_N64
        else if (romdataFileServed(index))
        {
            /* Reloading a served file: the buffer is exactly the inflated size
             * and the compressed bytes sit at its end, so inflating would
             * overwrite its own input and corrupt memory. Put the compressed
             * copy wholly after the output; the commit below shrinks the slack
             * back. */
            info->poolRemaining += (info->rom_size + 7) & ~7;
        }
#endif
        // bytes = info->poolRemaining;
        ptrdata             = mempAllocBytesInBank(info->poolRemaining, bank); // get pointer to allocated space in bank
        info->rom_remaining = info->poolRemaining;

        if (file_resource_table[index].hw_address == 0) //IF NULL, check indy
        {
            resource_load_from_indy(ptrdata, info->poolRemaining, &file_resource_table[index], info);
        }
        else
        {
            load_resource(ptrdata, info->poolRemaining, &file_resource_table[index], info);
        }
#ifndef TARGET_N64
        romdataFilePreprocess(index, ptrdata, info->poolRemaining); /* ROM-served files only */
#endif
        if (loadMethod != FILELOADMETHOD_EXTRAMEM)
        {
            // mempRealloc
            mempAddEntryOfSizeToBank(ptrdata, info->poolRemaining, bank);
        }
    }
    else // skipped in PD
    {
        if (info->poolRemaining == 0)
        {
            if (info->rom_size != 0)
            {
                info->poolRemaining = info->rom_size;
            }
            else
            {
                info->poolRemaining = info->pc_size;
            }
        }
        ptrdata             = mempAllocBytesInBank(info->poolRemaining, bank);
        info->rom_remaining = info->poolRemaining;

        if (file_resource_table[index].hw_address == 0)
        {
            resource_load_from_indy(ptrdata, 0, &file_resource_table[index], info);
        }
        else
        {
            load_resource(ptrdata, 0, &file_resource_table[index], info);
        }
#ifndef TARGET_N64
        romdataFilePreprocess(index, ptrdata, info->poolRemaining); /* ROM-served files only */
#endif
        if (size == 0)
        {
            info->loaded_bank = bank;
        }
    }
    return ptrdata;
}





void *fileIndexLoadToAddr(s32 index, FILELOADMETHOD loadMethod, void *ptrdata, s32 bytes) //#match https://decomp.me/scratch/YExRh
{
    resource_lookup_data_entry *info = &resource_lookup_data_array[index];

#ifndef TARGET_N64
    portHwNoteFile(index); /* tickhz=hw load hitch (frametiming.c) */
    /* PORT: compiled-in assets return the linked data and ignore the caller's
     * buffer (callers use the returned pointer); ROM-served files take the
     * vanilla path into the caller's buffer. */
    if (file_resource_table[index].hw_address != NULL && !romdataFileServed(index))
    {
        return file_resource_table[index].hw_address;
    }
#endif

    if (!info->poolRemaining)
    {
        if (info->rom_size)
        {
            info->poolRemaining = info->rom_size;
        }
        else
        {
            info->poolRemaining = info->pc_size;
        }
    }
    if (loadMethod == FILELOADMETHOD_EXTRAMEM || loadMethod == FILELOADMETHOD_DEFAULT || loadMethod == 2)
    {
        if (!file_resource_table[index].hw_address)
        {
            info->rom_remaining = bytes;
            resource_load_from_indy(ptrdata, bytes, &file_resource_table[index], &resource_lookup_data_array[index]);
        }
        else
        {
            info->rom_remaining = bytes;
            //fix a1/a0 inversion by manual pointer "info"
            load_resource(ptrdata, bytes, &file_resource_table[index], &resource_lookup_data_array[index]);
        }
    }
    else
    {
        if (!file_resource_table[index].hw_address)
        {
            resource_load_from_indy(ptrdata, 0, &file_resource_table[index], &resource_lookup_data_array[index]);
        }
        else
        {
            load_resource(ptrdata, 0, &file_resource_table[index], &resource_lookup_data_array[index]);
        }
    }
#ifndef TARGET_N64
    romdataFilePreprocess(index, ptrdata, info->poolRemaining); /* ROM-served files only */
#endif

    return ptrdata;
}






s32 get_pc_remaining_buffer_for_index(s32 index)
{
    return resource_lookup_data_array[index].poolRemaining;
}


s32 get_rom_remaining_buffer_for_index(s32 index)
{
    return resource_lookup_data_array[index].rom_remaining;
}

#ifndef TARGET_N64
/* The served bg's ROM span, for romfileBgConvert's bounds checks
 * (bg.c cannot see resource_lookup_data_array's type). */
u32 obGetFileRomSize(s32 index)
{
    return resource_lookup_data_array[index].rom_size;
}
#endif


void fileSetSize(s32 filenum, u8* ptr, u32 size, s32 reallocate)
{
    resource_lookup_data_array[filenum].poolRemaining = size;
    resource_lookup_data_array[filenum].rom_remaining = size;
    if (reallocate != 0)
    {
        mempAddEntryOfSizeToBank(ptr, resource_lookup_data_array[filenum].poolRemaining, MEMPOOL_STAGE);
    }
}


s32 get_pc_buffer_remaining_value(u8 *name)
{
    int index;

    index = fileGetIndex(name);
    return resource_lookup_data_array[index].poolRemaining;
}


void obBlankResourcesLoadedInBank(u8 bank)
{
    int i;
    for (i = 1; i < file_entry_max; i++) {
        if (resource_lookup_data_array[i].loaded_bank <= bank) {
            resource_lookup_data_array[i].loaded_bank = '\0';
        }
        if (bank == 4) {
            resource_lookup_data_array[i].poolRemaining = 0;
        }
    }
}

void obBlankResourcesInBank5(void) {
  obBlankResourcesLoadedInBank(MEMPOOL_ME);
}




s32 fileGetIndex(u8 *resname)
{
    s32 i;
    s32 stack;
    s32 size;
    struct resource_lookup_data_entry *lookup;

    for (i = 1; i < file_entry_max; i++)
    {
        if (file_resource_table[i].filename != NULL)
        {
            if (strcmp(resname, file_resource_table[i].filename) == 0)
            {
                return i;
            }
        }
    }
    
    i = file_entry_max;

#ifndef TARGET_N64
    /* a miss here routes to the dead indy path - always worth naming
     * (printf already declared via this TU's stdio) */
    printf("ge: fileGetIndex MISS \"%s\" (searched 1..%d)\n", resname, file_entry_max - 1);
#endif

    //too many files exist
    if (i >= OBJ_INDEX_MAX)
    {
        return 0;
    }
    
    file_entry_max++;

    if (indycommHostCheckFileExists(resname, &size) == 0)
    {
        return 0;
    }

    file_resource_table[i].index = i; // offset 0
    file_resource_table[i].filename = resname;  // offset 4

    lookup = &resource_lookup_data_array[i];
    
    if(1);

    lookup->unk_11 = 0;
    file_resource_table[i].hw_address = 0;  // offset 8
    lookup->rom_size = 0;
    lookup->poolRemaining = 0;
    lookup->pc_size = ALIGN16_a(size);
    lookup->rom_remaining = 0;
    lookup->loaded_bank = 0;

    return i;
}





void removed_handle_filetable_entry(u32 index)
{
    return;
}

void removed_loop_handle_filetable_entries(void)
{
    int i;
    for (i = 1; (i < file_entry_max); i++)
    {
        removed_handle_filetable_entry(i);
    }
}

void removed_loop_filetableentries(void)
{
    int i;

    for (i = 1; (i < file_entry_max); i++)
    {
        ;
    }
}




// removed
void sub_GAME_7F0BD410(void)
{
    s32 i;

    for (i = 1; i < file_entry_max; i++)
    {
        // unknown which property is referenced, just need to get the compiler to
        // correctly reference the parent object.
        if (resource_lookup_data_array[i].poolRemaining)
        {
            // removed
        }    
    }
}




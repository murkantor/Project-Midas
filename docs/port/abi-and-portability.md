# ABI and Portability Constraints

**Status:** second-opinion note, not maintainer documentation.

**Snapshot:** `6be5920` ("Project Midas 1.0.1"), branch `main`.

**Method:** static inspection of the build files and selected port sources. No build or runtime validation was performed. The `docs/port/*.md` files referenced by source comments are absent from this checkout; statements about intent are inferences from code and comments.

## 1. Summary

The PC port is deliberately built as a 32-bit i386 binary. The constraint is not localised to a compiler flag: it is present in the build flags, in converted asset data, in the display-list representation, and in the ELF/PE assembly and asset-layout pipeline.

The snapshot's own comment states the rationale: the build is "32-bit first, which defuses the codebase's pointer<->s32 casts and keeps Gfx 8 bytes." That is a coherent bring-up strategy. It also means a 64-bit or native macOS port is a data-model change, not a build-system fix.

Endianness is a separate axis. The ROM loader and asset converters normalise the big-endian N64 data they handle; `CMakeLists.txt` still flags some big-endian blobs (background interior point tables, `ANIM_DATA_*` streams, and oddtextures texel data). The portability constraint discussed here is pointer width and address representation, not byte order.

## 2. The 32-bit ABI Lock

`CMakeLists.txt` applies `-m32` to all C and C++ compilation and to the final link. The PC build does not spell out `-mabi=ilp32`; on x86, `-m32` selects the i386 ILP32 model. The practical consequences are `sizeof(void *) == 4`, `sizeof(long) == 4`, and `sizeof(uintptr_t) == 4`; `Gfx` and any pointer-bearing struct follow that width. The N64 build separately uses `-mabi=32` in its MIPS assembler flags. The effect is a 32-bit data model in both cases, reached by different mechanisms.

Related compilation settings that form part of the contract:

- `-msse2 -mfpmath=sse`: x86-specific scalar floating point, chosen to match N64 single-precision rounding.
- `-funsigned-char`, `-fwrapv`, `-fno-strict-aliasing`: decompiled-code behaviour and data interpretation, not pointer width, but part of the same ABI-sensitive build recipe.
- `-fno-omit-frame-pointer`: required by the crash handler's EBP-chain walk.
- `-no-pie` on ELF; `-Wl,--image-base=0x10000000` and `-Wl,--disable-dynamicbase` on PE. The image base is load-bearing for `seg_addr` (section 4).
- Asset translation units are compiled separately with `-m32 -malign-data=abi -fno-common -fno-zero-initialized-in-bss -fno-toplevel-reorder -fno-pic` and then processed with `objcopy -G` or `objcopy --add-symbol`. Their `.data` layout and symbol visibility are part of the load contract, not incidental compiler output.

The snapshot includes `IS_64_BIT` in `include/platform_info.h`, but it is used only to gate the typed members of the `Gfx` union. `DOUBLE_SIZE_ON_64_BIT()` is defined and unused. These read as scaffolding or inherited Perfect Dark-port accommodation, not as an implemented 64-bit path.

## 3. Data Model and Pointer Widths

The ROM is parsed as 32-bit big-endian data:

- `port/src/romdata.c` reads 32-bit words with `be32()`, parses 12-byte file-table entries (`{s32 index; char *filename; u8 *hw_address;}` in N64 layout), and converts offsets relative to `ROMDATA_CDATA_VADDR`.
- `port/include/romdata.h`'s host `struct romfile` uses native pointers and `u32` ROM offsets. The table is parsed field-by-field rather than overlaid as a host struct, so this particular path is not inherently 64-bit-hostile.
- `port/src/romfile_model.c` treats model files as big-endian N64 memory images with `0x05000000`-based offsets and display lists in big-endian words. It byte-swaps typed fields in place, then relies on the game's existing promote step to rebase offsets to pointers.
- `port/src/romfile_setup.c` and `romfile_stan.c` contain conversion paths that write host pointers into 32-bit fields with `(u32)(uintptr_t)`. That is lossless only in ILP32.
- `port/src/port_textures.c` stores a texture address in `entry.index` (a `u32`) via `(u32)(uintptr_t)px`, and explicitly refuses external textures when `sizeof(void *) > 4`.

The architectural issue is therefore field-by-field: some values are guest offsets or 32-bit words and should stay 32-bit; others are host pointers and must widen. The current snapshot can leave that distinction implicit because the host pointer is also 32-bit.

## 4. Gfx Streams

`include/PR/gbi.h` defines:

```c
typedef struct {
    uintptr_t w0;
    uintptr_t w1;
} Gwords;

typedef union {
    Gwords words;
    ...
} Gfx;
```

On i386, `Gfx` is 8 bytes; on a 64-bit host it is 16 bytes. The comment in the header is explicit: the display list is "by law, exactly 64 bits in size," with the parenthetical that it is "exactly 128 bit" on 64-bit. The typed union members are only enabled for big-endian, non-64-bit builds; on the little-endian i386 PC build and on any 64-bit host, only `.words` is used.

ROM model files contain 8-byte big-endian display-list words; compiled assets use the host layout, with the big-endian blobs noted above. `port/fast3d/gfx_pc.cpp`'s `seg_addr()` interprets a word as a segment reference when its top byte is 1..15 and `PORT_IS_HOST_ADDR()` is false. `port/include/port_hostptr.h` defines that test as `(unsigned int)(v) >= 0x10000000u`, a 32-bit comparison. The allocator places the ROM and arenas high; the Windows image is rebased at `0x10000000`, while the Linux image at `0x08048000` is tolerated because the colliding segment slots are never installed.

This is a 32-bit addressing convention. A 64-bit build either keeps display-list words at 32 bits and separates them from host pointers, or widens `Gfx` and converts every display list. Both are architectural changes to the stream representation; neither is a compiler-flag change.

## 5. Platform Constraints

The port has ELF and PE/COFF build routes, but no Mach-O route.

- `port/src/segments.c` and `port/src/ramrom_data.c` use top-level `__asm__` blocks with `.section .bss`/`.section .data`, `.balign`, `.globl`, `.space`, `.byte`, `.rept`, and `.incbin`. `port/include/asmsym.h` handles the i386 PE/COFF `_` symbol prefix through `__USER_LABEL_PREFIX__`.
- `.incbin` inputs are located with `-Wa,-I...` and tracked with `OBJECT_DEPENDS` in `CMakeLists.txt`.
- The asset objects rely on `-fno-pic` and the ELF link's `-no-pie`; the comment notes that a PIE final link would reject their `R_386_32` relocations. PE has no PIE concept and uses a fixed image base instead.
- `CMakeLists.txt` branches on `WIN32` and otherwise assumes an ELF/Linux toolchain. `cmake/mingw-i686.cmake` is a PE-specific cross-toolchain file. There is no `APPLE`/Mach-O build branch.
- `port/src/main.c` uses Win32 structured/vectored exception handling under `_WIN32` and POSIX `signal()` otherwise. The manual EBP-chain walk is x86-specific; an arm64 build would need a different unwinder.
- `port/src/sysstubs.c` guards CPUID and `__builtin_ia32_pause()` with x86 preprocessor checks; arm64 would skip those paths.

Mach-O is not just a different linker invocation. Section directives, symbol decoration, PIE defaults, and relocation handling differ; the `.incbin`/`objcopy` asset-layout contract would need revalidation. `ASM_SYM` is portable in principle because it follows `__USER_LABEL_PREFIX__`, but the surrounding section syntax and the `objcopy` step are not.

A review-host observation, not a repo fact: on Apple Silicon with a current macOS SDK, `clang -m32 -c` produced a Mach-O `arm_v4t` object, and the SDK's `libSystem.tbd` had no i386 slice. macOS has not supported 32-bit applications since 10.15. This is consistent with the snapshot having no macOS build path.

## 6. Porting Implications

A 64-bit port would change the host data model in at least four places:

1. `Gfx` grows from 8 to 16 bytes unless its words are deliberately kept 32-bit.
2. Host pointers stored in 32-bit slots (`(u32)(uintptr_t)`, `entry.index`, converted setup/stan data) would truncate.
3. The `0x10000000` host-address/segment-word heuristic and the high ROM mapping assume a 32-bit address space.
4. The asset-object layout and `objcopy` symbol-localisation pipeline are tied to the current ELF/PE object model.

A native macOS port would add Mach-O section/relocation handling, arm64 floating-point flags and validation, the x86 crash-backtrace replacement, and macOS packaging/signing. SDL2 and OpenGL are the comparatively portable parts; the ABI and asset/display-list representation are the architectural work.

The existing `IS_64_BIT` checks and the unused `DOUBLE_SIZE_ON_64_BIT()` suggest the maintainers were aware of the 64-bit boundary. They do not constitute a 64-bit path. "32-bit first" was a reasonable bring-up decision, but it deferred the pointer-width audit rather than performing it.

## 7. Evidence Map and Open Questions

Primary evidence:

- `CMakeLists.txt` — `-m32`, asset flags, link flags, `objcopy` pipeline.
- `include/platform_info.h` — `IS_64_BIT`, `IS_BIG_ENDIAN`, `DOUBLE_SIZE_ON_64_BIT`.
- `include/PR/gbi.h` — `Gwords`/`Gfx` definition and 64-bit note.
- `port/src/romdata.c`, `port/include/romdata.h` — ROM normalisation and file-table parsing.
- `port/src/romfile_model.c`, `romfile_setup.c`, `romfile_stan.c` — in-place asset conversion and pointer patching.
- `port/src/port_textures.c` — `u32` texture addresses and 64-bit guards.
- `port/include/port_hostptr.h`, `port/fast3d/gfx_pc.cpp` — host-address/segment-word heuristic.
- `port/src/segments.c`, `port/src/ramrom_data.c`, `port/include/asmsym.h` — inline asm, `.incbin`, symbol decoration.
- `port/src/main.c`, `port/src/sysstubs.c` — x86/Win32-specific crash and CPU paths.
- `cmake/mingw-i686.cmake` — PE/COFF cross-build.

Open questions this snapshot cannot answer:

- Whether `DOUBLE_SIZE_ON_64_BIT` is vestigial from the Perfect Dark port or an unfinished 64-bit plan.
- Whether a 64-bit or macOS branch exists outside this snapshot.
- Whether the `(u32)(uintptr_t)` conversions cover all asset classes or only the paths exercised here.
- Whether any "macOS support" statement refers to a native build, a Wine/CrossOver wrapper, or a Linux VM/container.

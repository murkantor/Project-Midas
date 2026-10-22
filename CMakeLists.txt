cmake_minimum_required(VERSION 3.16)
project(ge007 C)

# GoldenEye PC port build (docs/port/PORT-PLAN.md). Built 32-bit first, which
# defuses the codebase's pointer<->s32 casts and keeps Gfx 8 bytes.
# The N64 ROM build stays on the classic Makefile; this file is PC-only.

set(CMAKE_C_STANDARD 11)

add_compile_options(
    -m32
    # Use SSE scalar floats so every operation rounds to single precision like
    # the N64 FPU; x87's 80-bit excess precision flips borderline comparisons
    # (it broke the stan.c tile-walk edge test and room visibility in Egypt).
    -msse2
    -mfpmath=sse
    -fno-strict-aliasing
    -fwrapv
    -funsigned-char
    # crash-handler EBP-chain walk (port/src/main.c) needs real frame pointers
    -fno-omit-frame-pointer
    # DWARF in every build (incl. Windows PE): zero runtime cost, and it
    # upgrades crash symbolization from symbol+offset to file:line
    # (addr2line/gdb work on the exe directly)
    -g
    # -O2, keeping the flags that defuse decompiled-code UB (-fno-strict-aliasing
    # -fwrapv -funsigned-char -msse2 -mfpmath=sse); verified pixel-identical to
    # the old -Og build (see docs/port/optimisation-inventory.md).
    -O2
    # (Audit recipe: swap the -w below for -Wreturn-type and rebuild, since
    # falling off a non-void function is live UB at -O2. Keep the tree clean.)
    -w
    # C-only: game code is self-contained against repo-local headers;
    # fast3d C++ needs hosted stdlib and no ms-extensions quirks.
    $<$<COMPILE_LANGUAGE:C>:-ffreestanding>
    $<$<COMPILE_LANGUAGE:C>:-fms-extensions>
    $<$<COMPILE_LANGUAGE:C>:-fcommon>
)
# -no-pie: the compiled-in asset objects (below) are built -fno-pic so their
# pointer-laden .data needs no PIC indirection; a PIE final link would reject
# their R_386_32 relocations. MinGW PE has no PIE concept and its gcc driver
# rejects the flag, so it is ELF-only.
if(WIN32)
    # Statically link libstdc++/libgcc so the exe needs no extra DLLs, set the
    # image base above the N64 segment range so gfx_pc.cpp seg_addr can tell
    # addresses from segment refs, and use a 16MB stack (recursive room/portal
    # walks overflow 1MB).
    add_link_options(-m32 -static-libgcc -static-libstdc++
                     -Wl,--image-base=0x10000000 -Wl,--disable-dynamicbase
                     -Wl,--stack,16777216)
    link_libraries(dbghelp)
else()
    add_link_options(-m32 -no-pie)
endif()

add_compile_definitions(
    VERSION_US LANG_US REFRESH_NTSC
    LEFTOVERDEBUG LEFTOVERSPECTRUM BUGFIX_R0
    NONMATCHING AVOID_UB _LANGUAGE_C
)
if(WIN32)
    # MinGW's ANSI-stdio layer defines static inline sprintf/printf wrappers
    # that collide with GE's own sprintf (src/sprintf.c, bespoke semantics).
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=0)
endif()

# port/include: shim<->scheduler interface (port_libultra.h). src/libultra:
# lets vitbl.c resolve <io/viint.h>. Both appended LAST, so no include that
# resolved before can be shadowed.
include_directories(. include include/ultra64 include/PR src src/game src/inflate
                    port/include src/libultra)

# Game logic: everything in src/game.
file(GLOB GAME_SRC CONFIGURE_DEPENDS src/game/*.c)

# Engine layer: the src/*.c files the N64 build compiles, minus N64-only
# machinery the port deletes (boot chain, TLB pager, rmon, usb, crash handler).
file(GLOB ENGINE_SRC_ALL CONFIGURE_DEPENDS src/*.c)
set(ENGINE_EXCLUDE boot tlb_manage tlb_hardwire rmon usb debug crash motor)
# motor.c is GE's own osMotor* implementation over raw PIF-ram DMA - not
# backable by the shim; port/src/motor_stub.c provides the API.
set(ENGINE_SRC)
foreach(f ${ENGINE_SRC_ALL})
    get_filename_component(base ${f} NAME_WE)
    if(NOT base IN_LIST ENGINE_EXCLUDE)
        list(APPEND ENGINE_SRC ${f})
    endif()
endforeach()

# gu math: explicit list — only the files resolving actually-undefined gu/sins/
# coss symbols. Do NOT glob the dir: sqrtf.s/libm_vals.s are asm-backed, and
# sinf.c/cosf.c (fsin/fcos) are superseded by libm sinf/cosf (link -lm below).
set(GU_SRC
    src/libultra/gu/align.c        # guAlignF
    src/libultra/gu/coss.c         # coss (s16 sine-table cosine)
    src/libultra/gu/lookat.c       # guLookAt
    src/libultra/gu/lookatref.c    # guLookAtReflect
    src/libultra/gu/mtxutil.c      # guMtxF2L (+ guMtxIdentF used by ortho/perspective)
    src/libultra/gu/normalize.c    # guNormalize
    src/libultra/gu/ortho.c        # guOrtho
    src/libultra/gu/perspective.c  # guPerspective/guPerspectiveF
    src/libultra/gu/rotate.c       # guRotate/guRotateF
    src/libultra/gu/scale.c        # guScale/guScaleF
    src/libultra/gu/sins.c         # sins (s16 sine-table)
    src/libultra/gu/translate.c    # guTranslate
    # ---- AUDIO: the real AL synthesizer, which only builds Acmd lists that
    # port/src/audio_pc.c executes. bnkf.c is deliberately absent because
    # audio_stubs.c has the big-endian-aware alBnkfNew.
    src/libultra/audio/sl.c
    src/libultra/audio/synthesizer.c
    src/libultra/audio/event.c
    src/libultra/audio/heapinit.c
    src/libultra/audio/heapalloc.c
    src/libultra/audio/cents2ratio.c
    src/libultra/audio/copy.c
    src/libultra/audio/synaddplayer.c
    src/libultra/audio/synallocfx.c
    src/libultra/audio/synallocvoice.c
    src/libultra/audio/syndelete.c
    src/libultra/audio/synfreevoice.c
    src/libultra/audio/synsetfxmix.c
    src/libultra/audio/synsetpan.c
    src/libultra/audio/synsetpitch.c
    src/libultra/audio/synsetpriority.c
    src/libultra/audio/synsetvol.c
    src/libultra/audio/synstartvoice.c
    src/libultra/audio/synstartvoiceparam.c
    src/libultra/audio/synstopvoice.c
    src/libultra/audio/load.c
    src/libultra/audio/save.c
    src/libultra/audio/resample.c
    src/libultra/audio/filter.c
    src/libultra/audio/mainbus.c
    src/libultra/audio/auxbus.c
    src/libultrare/audio/drvrNew.c
    src/libultrare/audio/reverb.c
    src/libultrare/audio/env.c
    # MUSIC: the compressed-MIDI sequencer
    src/libultra/audio/cseq.c
    src/libultra/audio/csplayer.c
    src/libultra/audio/cspgetstate.c
    src/libultra/audio/cspplay.c
    src/libultra/audio/cspsetseq.c
    src/libultra/audio/cspsetvol.c
    src/libultra/audio/cspstop.c
    src/libultra/audio/seqpsetbank.c
    src/libultra/audio/seqplayer.c   # __lookupVoice/__vsVol/__initFromBank... shared voice machinery
    src/libultra/audio/seq.c
)

# ---------------------------------------------------------------------------
# Compiled-in assets.
#
# The N64 build compiles each asset .c into its own elf, so every Model.c reuses
# the same internal symbol names (proptextures, ModelNode_0x018, ...) and
# stan/setup/bg files reuse tile_0/padlist/header. Globbing them into one
# executable would cause thousands of duplicate definitions, so each asset TU is
# compiled separately and objcopy'd so only its file symbol (CarmourguardZ,
# Ump_setupameZ, ...) stays global:
#
#   DIRECT assets define their file symbol in the C; objcopy -G keeps just it.
#   SEG    assets (models, bg) don't define it anywhere (on N64 it's the
#          address of the linked blob). The TU is compiled with
#          -fno-toplevel-reorder -malign-data=abi -fno-common
#          -fno-zero-initialized-in-bss, which reproduces the N64 file layout
#          in .data byte-for-byte, and objcopy --add-symbol plants the file
#          symbol at .data offset 0. The layout must match because
#          load_object_fill_header finds Textures/RootNode by offset arithmetic.
#
# The per-file list is generated (port/generated/asset_manifest.cmake) by
# scripts/port/gen_asset_aliases.py. Rerun it if assets or the link surface
# change; it is not a configure-time glob.
#
# Known caveats: bg .c interior point tables, ANIM_DATA_* streams and
# oddtextures texel data are big-endian blobs.
set(ASSET_C_FLAGS
    -m32 -malign-data=abi
    -fms-extensions -ffreestanding -fno-strict-aliasing -fwrapv -funsigned-char
    -fno-common -fno-zero-initialized-in-bss -fno-toplevel-reorder -fno-pic
    -Og -w
    -DVERSION_US -DLANG_US -DREFRESH_NTSC
    -DLEFTOVERDEBUG -DLEFTOVERSPECTRUM -DBUGFIX_R0
    -DNONMATCHING -DAVOID_UB -D_LANGUAGE_C
    -I${CMAKE_SOURCE_DIR}
    -I${CMAKE_SOURCE_DIR}/include
    -I${CMAKE_SOURCE_DIR}/include/ultra64
    -I${CMAKE_SOURCE_DIR}/include/PR
    -I${CMAKE_SOURCE_DIR}/src
    -I${CMAKE_SOURCE_DIR}/src/game
    -I${CMAKE_SOURCE_DIR}/src/inflate
    -I${CMAKE_SOURCE_DIR}/port/include
    -I${CMAKE_SOURCE_DIR}/src/libultra)

if(NOT CMAKE_OBJCOPY)
    set(CMAKE_OBJCOPY objcopy)
endif()
set(ASSET_OBJ_DIR ${CMAKE_BINARY_DIR}/asset_obj)
file(MAKE_DIRECTORY ${ASSET_OBJ_DIR})

# i386 PE/COFF prefixes C symbols with `_` (ELF doesn't), so add it here to the
# manifest's C-level names before objcopy uses them (like
# port/include/asmsym.h).
if(WIN32)
    set(ASSET_SYM_PREFIX "_")
else()
    set(ASSET_SYM_PREFIX "")
endif()

# NOTE: setup ai-list bytes are built from aicommands2.h's macros, so it must
# stay in these DEPENDS; if an AI macro edit has no effect, check the asset
# object is stale.
# Asset TU whose C defines the file symbol itself (stan/setup/brief/text):
# localize everything else.
# Every asset comes from the user's ROM except three inline-texture menu models
# (PORT_ASSET_OBJS_KEEP); with GE_ROMDATA_FALLBACK=OFF the rest are dropped and
# their weak file symbols resolve to NULL (ob_end.seg always lives in ob_seg.s).
set(PORT_ASSET_KEEP_SYMS PlegalpageZ PnintendologoZ PgoldeneyelogoZ)
function(port_asset_record obj sym)
    if(${sym} IN_LIST PORT_ASSET_KEEP_SYMS)
        set_property(GLOBAL APPEND PROPERTY PORT_ASSET_OBJS_KEEP ${obj})
    else()
        set_property(GLOBAL APPEND PROPERTY PORT_ASSET_OBJS_SERVED ${obj})
    endif()
endfunction()

function(port_add_asset_direct src sym)
    set(obj ${ASSET_OBJ_DIR}/${sym}.o)
    add_custom_command(OUTPUT ${obj}
        COMMAND ${CMAKE_C_COMPILER} ${ASSET_C_FLAGS} -c ${CMAKE_SOURCE_DIR}/${src} -o ${obj}.raw
        COMMAND ${CMAKE_OBJCOPY} -G ${ASSET_SYM_PREFIX}${sym} ${obj}.raw ${obj}
        DEPENDS ${CMAKE_SOURCE_DIR}/${src}
                ${CMAKE_SOURCE_DIR}/src/bondtypes.h
                ${CMAKE_SOURCE_DIR}/src/bondconstants.h
                ${CMAKE_SOURCE_DIR}/src/bondaicommands.h
                ${CMAKE_SOURCE_DIR}/src/aicommands2.h
        COMMENT "asset ${sym}" VERBATIM)
    port_asset_record(${obj} ${sym})
endfunction()

# Asset TU with no file symbol of its own (Model.c, bg .c): localize all
# internals and plant the file symbol at .data offset 0 (the file start).
function(port_add_asset_seg src sym)
    set(obj ${ASSET_OBJ_DIR}/${sym}.o)
    add_custom_command(OUTPUT ${obj}
        COMMAND ${CMAKE_C_COMPILER} ${ASSET_C_FLAGS} -c ${CMAKE_SOURCE_DIR}/${src} -o ${obj}.raw
        COMMAND ${CMAKE_OBJCOPY} -G ${ASSET_SYM_PREFIX}${sym} --add-symbol ${ASSET_SYM_PREFIX}${sym}=.data:0,global,object ${obj}.raw ${obj}
        DEPENDS ${CMAKE_SOURCE_DIR}/${src}
                ${CMAKE_SOURCE_DIR}/src/bondtypes.h
                ${CMAKE_SOURCE_DIR}/src/bondconstants.h
                ${CMAKE_SOURCE_DIR}/src/bondaicommands.h
                ${CMAKE_SOURCE_DIR}/src/aicommands2.h
        COMMENT "asset ${sym}" VERBATIM)
    port_asset_record(${obj} ${sym})
endfunction()

include(${CMAKE_SOURCE_DIR}/port/generated/asset_manifest.cmake)
get_property(ASSET_OBJS_KEEP GLOBAL PROPERTY PORT_ASSET_OBJS_KEEP)
get_property(ASSET_OBJS_SERVED GLOBAL PROPERTY PORT_ASSET_OBJS_SERVED)
# GE_ROMDATA_FALLBACK OFF: drop the served asset objects (models/bg/setups/
# stans/text/briefs) - the exe then carries none of Rare's data and the ROM
# is mandatory. ON keeps them for the GE_ROMDATA=0 / missing-ROM fallback.
if(GE_ROMDATA_FALLBACK)
    set(ASSET_OBJS ${ASSET_OBJS_KEEP} ${ASSET_OBJS_SERVED})
else()
    set(ASSET_OBJS ${ASSET_OBJS_KEEP})
    message(STATUS "GE_ROMDATA_FALLBACK=OFF: dropping the served asset objects from the exe (ROM mandatory)")
endif()
set_source_files_properties(${ASSET_OBJS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

# Loose single-TU asset data (unique symbol names; compiled like game code):
set(ASSET_LOOSE_SRC
    assets/oddtextures.c         # s_*image + globalDL_* (texel data is BE: renders wrong until swapped; resolves image_bank externs)
    assets/rarewarelogo.c        # logo data; only reachable once _rarewarelogoSegment* aliases land (romdata wave)
    assets/font_dl.c             # dlBasicGeometry / dlZBufferGeometry / dlFastPipelineSetup: front.c/lv.c/title.c
                                 # gSPDisplayList(&dl...) them BY SYMBOL, so they stay in every build (the fontdl
                                 # SEGMENT copy comes off the ROM); 184 bytes
)
# ROM loading (docs/port/romdata.md): animation + fonts come off the
# user's ROM; these compiled copies stay only for GE_ROMDATA_FALLBACK (below).
set(ASSET_LOOSE_FALLBACK_SRC
    assets/animationtable_data.c    # animation_data segment blob (see animdata.c)
    assets/animationtable_entries.c # animation frame streams (see animdata.c)
    assets/font/fontBankGothic.c # TEXT WAVE: kerning + chartable + glyph bytes (composed by portFontBuildSegments)
    assets/font/fontZurichBold.c # TEXT WAVE: chartable + glyph bytes (kerning not extracted; zeros)
)
# animdata.c rebuilds the N64 animation segment images from these blobs by
# offset arithmetic - they must tile contiguously in declaration order.
# Kept at -Og with the tree at -O2: data-only TUs, layout is the contract.
set_source_files_properties(assets/animationtable_data.c assets/animationtable_entries.c
    PROPERTIES COMPILE_OPTIONS "-fno-toplevel-reorder;-malign-data=abi;-Og")
# ---------------------------------------------------------------------------

# Port layer (grows as PD-derived pieces land; see docs/port/).
file(GLOB PORT_SRC CONFIGURE_DEPENDS port/src/*.c)

# GE_HEADLESS: no SDL at all (null renderer, silent audio), for the Linux -m32
# ASan build where no 32-bit SDL exists. GE_SANITIZE adds ASan+UBSan (Linux gcc
# only).
option(GE_HEADLESS "Build without SDL (null renderer, no audio)" OFF)
option(GE_SANITIZE "AddressSanitizer + UBSan build (implies headless)" OFF)
if(GE_SANITIZE)
    # Only default to headless when GE_VIDEO wasn't explicitly requested -
    # a GE_VIDEO=ON + GE_SANITIZE=ON build (real fast3d/SDL2 renderer,
    # e.g. via Docker's from-source 32-bit SDL2) must NOT also pull in
    # headless_stubs.c, or duplicate-definition link errors result
    # (ge_sky_push, gfx_texture_cache_* defined in both gfx_pc.cpp and
    # headless_stubs.c).
    if(NOT GE_VIDEO)
        set(GE_HEADLESS ON)
    endif()
    # -g3 gives file:line in reports and no-sanitize-recover=all makes every UBSan
    # finding fatal; if deliberate decomp UB floods it, exempt specific checks here.
    add_compile_options(-g3 -fsanitize=address -fsanitize=undefined
                        -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address -fsanitize=undefined)
    # ASan redzones put gaps between globals and break animdata's contiguous
    # tiling, so exempt the data-only TUs (manifest assets are already
    # uninstrumented).
    set_source_files_properties(assets/animationtable_data.c assets/animationtable_entries.c
        PROPERTIES COMPILE_OPTIONS "-fno-toplevel-reorder;-malign-data=abi;-Og;-fno-sanitize=all")
    set_source_files_properties(assets/oddtextures.c assets/rarewarelogo.c
        assets/font_dl.c assets/font/fontBankGothic.c assets/font/fontZurichBold.c
        PROPERTIES COMPILE_OPTIONS "-fno-sanitize=all")
endif()
if(GE_HEADLESS)
    list(REMOVE_ITEM PORT_SRC
        ${CMAKE_SOURCE_DIR}/port/src/audio_pc.c
        ${CMAKE_SOURCE_DIR}/port/src/video.c
        ${CMAKE_SOURCE_DIR}/port/src/input_pc.c
        ${CMAKE_SOURCE_DIR}/port/src/raphnet.c)
else()
    list(REMOVE_ITEM PORT_SRC ${CMAKE_SOURCE_DIR}/port/src/headless_stubs.c)
endif()
# segments.c .incbin's the raw image bank (texture wave); ramrom_data.c
# .incbin's the gunbarrel swirl RLE blob and the 14 attract-demo
# recordings. Give the assembler the include paths and re-assemble when
# the blobs change.
set_source_files_properties(port/src/segments.c PROPERTIES
    COMPILE_OPTIONS "-Wa,-I${CMAKE_SOURCE_DIR}/assets/images/combined;-Wa,-I${CMAKE_SOURCE_DIR}/assets/music"
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/images/combined/combined.bin;${CMAKE_SOURCE_DIR}/assets/music/sfx.ctl;${CMAKE_SOURCE_DIR}/assets/music/sfx.tbl;${CMAKE_SOURCE_DIR}/assets/music/instruments.ctl;${CMAKE_SOURCE_DIR}/assets/music/instruments.tbl;${CMAKE_SOURCE_DIR}/assets/music/music.sbk")
set(PORT_RAMROM_DEMO_BINS
    ramrom_Dam_1 ramrom_Dam_2 ramrom_Facility_1 ramrom_Facility_2
    ramrom_Facility_3 ramrom_Runway_1 ramrom_Runway_2 ramrom_BunkerI_1
    ramrom_BunkerI_2 ramrom_Silo_1 ramrom_Silo_2 ramrom_Frigate_1
    ramrom_Frigate_2 ramrom_Train)
list(TRANSFORM PORT_RAMROM_DEMO_BINS PREPEND ${CMAKE_SOURCE_DIR}/assets/ramrom/)
list(TRANSFORM PORT_RAMROM_DEMO_BINS APPEND .bin)
set_source_files_properties(port/src/ramrom_data.c PROPERTIES
    COMPILE_OPTIONS "-Wa,-I${CMAKE_SOURCE_DIR}/assets"
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/ge007.u.2A4D50.usedby7F008DE4.bin;${PORT_RAMROM_DEMO_BINS}")
# osViModeTable[42]: compile the repo's REAL table (fr.c/sched.c read
# comRegs.hStart/width/xScale + fldRegs[].vStart/yScale/origin from it)
# instead of transcribing data into the shim.
list(APPEND PORT_SRC src/libultrare/io/vitbl.c)

# ---------------------------------------------------------------------------
# Video: fast3d interpreter + SDL2/OpenGL. OFF by default until the
# GE microcode opcode work lands (docs/port/ge-microcode.md); the null
# renderer in pcsched.c stays the headless default.
option(GE_VIDEO "Build the fast3d renderer (SDL2 + OpenGL)" OFF)

# Testing convenience: every solo mission, bonus stage and difficulty
# (incl. 007 mode) selectable regardless of save state. Progression logic
# untouched otherwise - saves still record normally.
option(GE_UNLOCK_ALL "Unlock all missions and difficulties (testing)" OFF)
if(GE_UNLOCK_ALL)
    add_compile_definitions(GE_UNLOCK_ALL=1)
endif()

# By default the exe carries none of Rare's data and exits without the user's ROM.
# -DGE_ROMDATA_FALLBACK=ON keeps compiled-in copies as a no-ROM fallback (see docs/port/romdata.md).
option(GE_ROMDATA_FALLBACK "Keep compiled-in copies of the ROM-served asset classes (GE_ROMDATA=0 / missing-ROM fallback)" OFF)
if(GE_ROMDATA_FALLBACK)
    set_source_files_properties(port/src/segments.c port/src/ramrom_data.c port/src/romdata.c port/src/animdata.c
        PROPERTIES COMPILE_DEFINITIONS GE_ROMDATA_FALLBACK=1)
    list(APPEND ASSET_LOOSE_SRC ${ASSET_LOOSE_FALLBACK_SRC})
endif()

if(GE_VIDEO)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 20) # gfx_cc.h uses operator<=> (PD builds C++20)
    set(FAST3D_SRC
        port/fast3d/gfx_pc.cpp
        port/fast3d/gfx_cc.cpp
        port/fast3d/gfx_opengl.cpp
        port/fast3d/gfx_sdl2.cpp
        port/fast3d/glad/glad.c
    )
    add_compile_definitions(GE_VIDEO=1)
else()
    set(FAST3D_SRC)
    # video.c/input_pc.c/raphnet.c are GE_VIDEO-only; exclude them from the port glob.
    list(REMOVE_ITEM PORT_SRC
        ${CMAKE_SOURCE_DIR}/port/src/video.c
        ${CMAKE_SOURCE_DIR}/port/src/input_pc.c
        ${CMAKE_SOURCE_DIR}/port/src/raphnet.c)
endif()

add_executable(ge ${GAME_SRC} ${ENGINE_SRC} ${GU_SRC} ${PORT_SRC}
               ${ASSET_LOOSE_SRC} ${ASSET_OBJS} ${FAST3D_SRC})
# The executable is named ProjectMidas, but the CMake target stays `ge` so build
# scripts keep working, and ge_direct.log/Profiles keep their names.
set_target_properties(ge PROPERTIES OUTPUT_NAME ProjectMidas)
# The exe's own icon (Explorer, a pinned taskbar entry): port/win/ProjectMidas.ico,
# generated with the window icon by scripts/port/make_icon.py.
if(WIN32)
    enable_language(RC)
    target_sources(ge PRIVATE ${CMAKE_SOURCE_DIR}/port/win/ProjectMidas.rc)
    # No console window: the GUI subsystem, keeping main() as the entry (unlike
    # -mwindows, which wants WinMain). The log goes to ge_direct.log instead
    # (main.c portDirectLaunchLog); redirected runs keep their output.
    target_link_options(ge PRIVATE -Wl,--subsystem,windows)
endif()

# Project Midas build stamp: <git commit count>-<YYYYMMDD>, regenerated on
# every build (scripts/port/buildinfo.cmake has the why). sysstubs.c owns the
# string; the legal screen and the `ge: sys: build` log line both read it.
add_custom_target(ge_buildinfo
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}
            -DOUT=${CMAKE_BINARY_DIR}/generated/port_buildinfo.h
            -P ${CMAKE_SOURCE_DIR}/scripts/port/buildinfo.cmake
    BYPRODUCTS ${CMAKE_BINARY_DIR}/generated/port_buildinfo.h
    COMMENT "build stamp"
    VERBATIM)
add_dependencies(ge ge_buildinfo)
target_include_directories(ge PRIVATE ${CMAKE_BINARY_DIR}/generated)

# libm: sinf/cosf/sqrtf (math_sincos.s and gu asm on N64; libm on PC).
target_link_libraries(ge m)

if(GE_VIDEO)
    if(WIN32)
        # the SDL2 MinGW development package: third_party/ by default, or
        # SDL2_MINGW_ROOT (the Docker image unpacks it in the home folder)
        set(SDL2_MINGW_ROOT "" CACHE PATH "SDL2 MinGW package's i686-w64-mingw32 folder (default: third_party/SDL2-2.30.11/i686-w64-mingw32)")
        if(SDL2_MINGW_ROOT)
            set(SDL2_ROOT ${SDL2_MINGW_ROOT})
        else()
            set(SDL2_ROOT ${CMAKE_SOURCE_DIR}/third_party/SDL2-2.30.11/i686-w64-mingw32)
        endif()
        target_include_directories(ge PRIVATE ${SDL2_ROOT}/include ${SDL2_ROOT}/include/SDL2)
        target_link_libraries(ge ${SDL2_ROOT}/lib/libSDL2.dll.a opengl32 gdi32 imm32 winmm setupapi hid version ole32 oleaut32)
        # the game needs SDL2.dll beside the exe
        add_custom_command(TARGET ge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SDL2_ROOT}/bin/SDL2.dll $<TARGET_FILE_DIR:ge>)
    else()
        # 32-bit SDL2: needs libsdl2-dev:i386 (+ libgl-dev:i386), or set
        # SDL2_LINUX_ROOT to a self-built prefix (e.g. CI's cached, user-writable
        # install, avoiding a system-wide /usr install).
        if(SDL2_LINUX_ROOT)
            target_include_directories(ge PRIVATE ${SDL2_LINUX_ROOT}/include/SDL2)
            target_link_libraries(ge ${SDL2_LINUX_ROOT}/lib/i386-linux-gnu/libSDL2.so GL dl pthread)
            set_target_properties(ge PROPERTIES BUILD_RPATH ${SDL2_LINUX_ROOT}/lib/i386-linux-gnu)
        else()
            target_include_directories(ge PRIVATE /usr/include/SDL2)
            target_link_libraries(ge SDL2 GL dl pthread)
        endif()
    endif()
    # the files a release ships beside the game, so a fresh build runs the
    # same: controller mappings and layouts (without them a new install has
    # no default binds), the ini help and the base shader list
    add_custom_command(TARGET ge POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/tools/gamecontrollerdb.txt
            ${CMAKE_SOURCE_DIR}/tools/gamecontrollerdefault.txt
            ${CMAKE_SOURCE_DIR}/tools/midas_ini_help.txt
            $<TARGET_FILE_DIR:ge>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ge>/Data
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/tools/shaders_base.txt $<TARGET_FILE_DIR:ge>/Data/shaders_base.txt)
endif()

# ---- ROM-free unit tests ---------------------------------------------------
# port/tests/ holds tests for logic that has been split out of the SDL/ROM
# layers precisely so it can be exercised without either. They build in a
# fraction of a second and need no cart, display or sound device, so they are
# safe for CI and useful in a tight edit/test loop.
#
#     cmake -B build -DGE_PORT_TESTS=ON
#     cmake --build build --target port_tests
#     ctest --test-dir build -R audio_pace
option(GE_PORT_TESTS "Build the ROM-free port unit tests" OFF)
if(GE_PORT_TESTS)
    enable_testing()

    # Render pacing policy (port/src/audio_pace.c): the synthesizer must keep
    # running with no audio device, and a live device must keep setting the
    # pace so the output backlog cannot drift upward into audible latency.
    #
    # Compiled with explicit flags rather than add_executable(), because the
    # directory-wide add_compile_options() at the top of this file carries
    # -m32/-msse2 for the x86-32 game build. This unit is plain C99 with no
    # architecture requirements, so it should build and run on any host that
    # can run the tests at all - including a 64-bit or ARM dev machine that
    # cannot link the game itself.
    set(AUDIO_PACE_TEST_BIN ${CMAKE_BINARY_DIR}/audio_pace_test)
    add_custom_command(
        OUTPUT ${AUDIO_PACE_TEST_BIN}
        COMMAND ${CMAKE_C_COMPILER} -std=c99 -Wall -Wextra
                -I${CMAKE_SOURCE_DIR}/port/include
                ${CMAKE_SOURCE_DIR}/port/tests/audio_pace.c
                ${CMAKE_SOURCE_DIR}/port/src/audio_pace.c
                -o ${AUDIO_PACE_TEST_BIN}
        DEPENDS ${CMAKE_SOURCE_DIR}/port/tests/audio_pace.c
                ${CMAKE_SOURCE_DIR}/port/src/audio_pace.c
                ${CMAKE_SOURCE_DIR}/port/include/audio_pace.h
        COMMENT "Building audio_pace_test")
    add_custom_target(port_tests ALL DEPENDS ${AUDIO_PACE_TEST_BIN})
    add_test(NAME audio_pace COMMAND ${AUDIO_PACE_TEST_BIN})
endif()

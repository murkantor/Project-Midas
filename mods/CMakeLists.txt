# Bundled mods. Each is a MODULE library linked against the game executable
# (which exports its symbols), compiled with the parent's flags, defines and
# include paths. Output: <build>/Data/Mods/<name>/<name>.so|.dll + mod.ini.
function(midas_add_mod name)
    add_library(${name} MODULE ${ARGN})
    set(out ${CMAKE_BINARY_DIR}/Data/Mods/${name})
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        C_VISIBILITY_PRESET hidden   # only MIDAS_EXPORT symbols leave the mod
        LIBRARY_OUTPUT_DIRECTORY ${out}
        RUNTIME_OUTPUT_DIRECTORY ${out})
    target_link_libraries(${name} PRIVATE ge m)
    if(WIN32)
        # off the exe's 0x10000000; port_mods.c refuses a DLL based below it
        target_link_options(${name} PRIVATE -Wl,--enable-auto-image-base)
    else()
        # the mod's own definitions win over same-named game symbols
        target_link_options(${name} PRIVATE -Wl,-Bsymbolic)
    endif()
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}/mod.ini ${out}/mod.ini)
endfunction()

midas_add_mod(bhop bhop/bhop.c)
midas_add_mod(funguns funguns/funguns.c funguns/decals.c)

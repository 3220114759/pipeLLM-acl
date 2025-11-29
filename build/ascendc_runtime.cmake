add_library(ascendc_runtime_obj OBJECT IMPORTED)
set_target_properties(ascendc_runtime_obj PROPERTIES
    IMPORTED_OBJECTS "/data/lzy/pipellm_acl/build/elf_tool.c.o;/data/lzy/pipellm_acl/build/ascendc_runtime.cpp.o"
)

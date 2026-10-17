toys_asm_lib = static_library(
    'toys_asm',
    sources: files(
        'asm.c',
        'sym.c',
        'instr.c',
        'data.c',
    ),
    include_directories: [
        inc,
    ],
    dependencies: [
        toys_vm_dep,
    ],
    install: false,
)

toys_asm_dep = declare_dependency(
    link_with: toys_asm_lib,
    include_directories: inc,
    dependencies: [
        toys_vm_dep,
    ],
)

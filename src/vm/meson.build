toys_vm_lib = static_library(
    'toys_vm',
    sources: files(
        'vm.c',
        'ops.c',
        'table.c',
        'prog.c',
    ),
    include_directories: [
        inc,
    ],
    dependencies: [
        m_dep,
        toys_common_dep,
    ],
    install: false,
)

toys_vm_dep = declare_dependency(
    link_with: toys_vm_lib,
    include_directories: inc,
    dependencies: [
        m_dep,
        toys_common_dep,
    ],
)

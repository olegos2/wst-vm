toys_vm_deps = [
    m_dep,
    toys_common_dep,
]

toys_vm_lib = static_library(
    'toys_vm',
    sources: files(
        'vm/vm.c',
        'vm/prog.c',
    ),
    include_directories: [
        inc,
    ],
    dependencies: toys_vm_deps,
    install: false,
)

toys_vm_dep = declare_dependency(
    link_with: toys_vm_lib,
    include_directories: inc,
    dependencies: toys_vm_deps,
)

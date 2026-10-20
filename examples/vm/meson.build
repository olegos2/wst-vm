raylib_dep = dependency('raylib')

executable(
    'vm',
    sources: files(
        'vm.c',
    ),
    include_directories: inc,
    dependencies: [
        toys_vm_dep,
        raylib_dep,
    ],
    install: true,
)

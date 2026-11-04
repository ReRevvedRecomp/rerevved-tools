include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG a74efa0d5628b74adc0426af4c5710e287fa7c2c
)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG fd13a1e8923a0a7077b404fc36fd063b25a0c0b5
)
FetchContent_Declare(
    dr_libs
    GIT_REPOSITORY https://github.com/mackron/dr_libs.git
    GIT_TAG 5690d4671d7ad07ae6021756d7222eb159745f06
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 013ac3beddff3dbffafd5177e7972067cd2b5083
)
FetchContent_Declare(
    inter
    URL https://github.com/rsms/inter/releases/download/v4.0/Inter-4.0.zip
    URL_HASH SHA256=ff970a5d4561a04f102a7cb781adbd6ac4e9b6c460914c7a101f15acb7f7d1a4
)
FetchContent_Declare(
    lucide
    URL https://registry.npmjs.org/lucide-static/-/lucide-static-0.460.0.tgz
    URL_HASH SHA256=24ec818481591e6795c48cd5d8466d1632c02050aa317801c207f91de9747ae4
)
FetchContent_MakeAvailable(glfw imgui dr_libs stb inter lucide)

file(GLOB_RECURSE KERNEL_SRCS
    arch/*.cpp
    debug/*.cpp
    driver/*.cpp
    filesys/*.cpp
    init/*.cpp
    kernel/*.cpp
    mm/*.cpp
    util/*.cpp
    include/*.h
)

add_executable(mykernel ${KERNEL_SRCS})

target_include_directories(mykernel PRIVATE include)

target_compile_options(mykernel PRIVATE
    -m64
    -ffreestanding
    -fno-rtti
    -fno-exceptions
    -fno-zero-initialized-in-bss
    -fno-common
    -masm=intel
    -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -mno-80387
    -msoft-float
    -mno-red-zone
    -Werror
    $<$<CONFIG:Debug>:-g;-O0>
    $<$<CONFIG:Release>:-O2>
)

target_link_options(mykernel PRIVATE
    -nostdlib
    -T ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
)

set_target_properties(mykernel PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
)

set(BOOT_EFI_DIR ${BOOT_OUT_DIR}/EFI/BOOT)

add_custom_command(TARGET mykernel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BOOT_EFI_DIR}
    COMMAND objcopy -O binary
        --only-section=.text --only-section=.rodata --only-section=.data
        $<TARGET_FILE:mykernel>
        ${BOOT_EFI_DIR}/os.bin
    VERBATIM
)

# stub/*.S → 각각 .o 로 컴파일 후 boot 파티션에 복사
file(GLOB STUB_SRCS stub/*.S)
foreach(STUB_SRC ${STUB_SRCS})
    get_filename_component(STUB_NAME ${STUB_SRC} NAME_WE)
    set(STUB_OBJ ${CMAKE_CURRENT_BINARY_DIR}/stub/${STUB_NAME}.o)

    add_custom_command(
        OUTPUT ${STUB_OBJ}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/stub
        COMMAND ${CMAKE_CXX_COMPILER} -m64 -c ${STUB_SRC} -o ${STUB_OBJ}
        DEPENDS ${STUB_SRC}
        VERBATIM
    )

    add_custom_target(stub_${STUB_NAME} ALL
        DEPENDS ${STUB_OBJ}
    )

    add_custom_command(TARGET stub_${STUB_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BOOT_EFI_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${STUB_OBJ} ${BOOT_EFI_DIR}/${STUB_NAME}.o
        VERBATIM
    )
endforeach()

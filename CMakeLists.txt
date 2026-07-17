# mykernel 아래 모든 .cpp 재귀 수집 (stub/ 은 .S 라 제외). 새 디렉터리 추가돼도 자동 포함.
# CONFIGURE_DEPENDS: 파일 추가/삭제 시 빌드에서 자동 재-glob.
file(GLOB_RECURSE KERNEL_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)
list(FILTER KERNEL_SRCS EXCLUDE REGEX "/stub/")

add_executable(mykernel ${KERNEL_SRCS})

target_include_directories(mykernel PRIVATE include)

# 공용 freestanding 플래그는 myos_flags(cmake/freestanding.cmake)로 일원화.
# 커널 전용 플래그(-Werror)만 여기서 추가.
target_link_libraries(mykernel PRIVATE myos_flags)

target_compile_options(mykernel PRIVATE -Werror)

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

    list(APPEND STUB_TARGETS stub_${STUB_NAME})
endforeach()

# makeimg 가 의존할 수 있도록 stub 타깃들을 하나로 집계
add_custom_target(kernel_stubs DEPENDS ${STUB_TARGETS})

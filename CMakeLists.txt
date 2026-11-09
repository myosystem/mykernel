# mykernel 아래 모든 .cpp 재귀 수집. 새 디렉터리 추가돼도 자동 포함.
# CONFIGURE_DEPENDS: 파일 추가/삭제 시 빌드에서 자동 재-glob.
file(GLOB_RECURSE KERNEL_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/include/*
)

add_executable(mykernel ${KERNEL_SRCS})

target_include_directories(mykernel PRIVATE include)

# 공용 freestanding 플래그는 myos_flags(cmake/freestanding.cmake)로 일원화.
# 커널 전용 플래그(-Werror)만 여기서 추가.
target_link_libraries(mykernel PRIVATE myos_flags)

target_compile_options(mykernel PRIVATE -Werror)

# 함수별 스택 프레임 크기 측정 (.su 파일 생성). 코드 생성엔 영향 없음.
target_compile_options(mykernel PRIVATE -fstack-usage)

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

# mykernel 아래 모든 .cpp 재귀 수집 (stub/ 은 .S 라 제외). 새 디렉터리 추가돼도 자동 포함.
# CONFIGURE_DEPENDS: 파일 추가/삭제 시 빌드에서 자동 재-glob.
file(GLOB_RECURSE KERNEL_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/include/*
)
list(FILTER KERNEL_SRCS EXCLUDE REGEX "/stub/")

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

# stub/*.S 전부 → .o 들 → ld 로 하나로 링크 → objcopy 로 평평한 바이너리 → boot 파티션의 signal.o
# 커널이 "#0/EFI/BOOT/signal.o" 라는 이름으로 읽어 신호 트램폴린 페이지에 적재한다.
file(GLOB STUB_SRCS stub/*.S)
if(STUB_SRCS)
    set(STUB_DIR ${CMAKE_CURRENT_BINARY_DIR}/stub)
    set(STUB_ELF ${STUB_DIR}/signal.elf)
    set(STUB_BIN ${STUB_DIR}/signal.bin)

    set(STUB_OBJS "")
    foreach(STUB_SRC ${STUB_SRCS})
        get_filename_component(STUB_NAME ${STUB_SRC} NAME_WE)
        set(STUB_OBJ ${STUB_DIR}/${STUB_NAME}.o)
        add_custom_command(
            OUTPUT ${STUB_OBJ}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${STUB_DIR}
            COMMAND ${CMAKE_CXX_COMPILER} -m64 -c ${STUB_SRC} -o ${STUB_OBJ}
            DEPENDS ${STUB_SRC}
            VERBATIM
        )
        list(APPEND STUB_OBJS ${STUB_OBJ})
    endforeach()

    add_custom_command(
        OUTPUT ${STUB_BIN}
        COMMAND ld -nostdlib -Ttext=0 -o ${STUB_ELF} ${STUB_OBJS}
        COMMAND objcopy -O binary
            --only-section=.text --only-section=.rodata --only-section=.data
            ${STUB_ELF} ${STUB_BIN}
        DEPENDS ${STUB_OBJS}
        VERBATIM
    )

    # makeimg 가 의존할 수 있도록 유지되는 타깃 이름
    add_custom_target(kernel_stubs ALL DEPENDS ${STUB_BIN})

    add_custom_command(TARGET kernel_stubs POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${BOOT_EFI_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${STUB_BIN} ${BOOT_EFI_DIR}/signal.o
        VERBATIM
    )
endif()

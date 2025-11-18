SECTIONS
{
    /* 커널이 실제 실행될 가상 주소 */
    . = 0xFFFFFFFF80000000;

    /* 코드 섹션 */
    .text : {
        KEEP(*(.entry))   /* _start: 반드시 바이너리 맨 앞 */
        *(.text*)         /* 나머지 모든 코드 */
    } :text

    /* 읽기 전용 데이터 */
    .rodata : {
        *(.rodata*)
    } :rodata

    /* 전역/정적 초기화 데이터 */
    .data : {
        *(.data*)
        *(.bss*)
        *(COMMON)
    } :data

}
PHDRS {
  text PT_LOAD FLAGS(5);   /* R + X */
  rodata PT_LOAD FLAGS(4); /* R */
  data PT_LOAD FLAGS(6);   /* R + W */
}
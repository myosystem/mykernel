#include "kernel/timer_handler.h"
#include "arch/lapic.h"
#include "debug/log.h"
#include "kernel/process.h"
#define min(a, b) ((a) < (b) ? (a) : (b))
char uart_buf[1000];
uint64_t next_process_time = 0;
extern "C" __attribute__((noinline)) uint64_t* c_timer_handler(context_t* frame) {
    if (!(now_process->state & PROCESS_STATE_WAITING)) {
        now_process->kernel_stack = (uint64_t*)frame;
    }
    if (!tsc_available) fake_tsc = fake_deadline;
	if (next_process_time == 0 || next_process_time <= tsc_get()) {  // 첫 타이머는 무조건 프로세스 스케줄링
        add_process(now_process->process_id);
        now_process = next_process();
        uint64_t nowtime = tsc_get();
		next_process_time = nowtime + ms_to_ticks(now_process->time_slice);
        uint64_t nexttime = next_process_time;
		if (time_event->isEmpty() == false) {
            nexttime = min(next_process_time, time_event->top().time);
        }
        tsc_deadline_set(nexttime);
        lapic_eoi();
        /*
        uint32_t lvt = *(volatile uint32_t*)(lapic_base + 0x320);
        uart_print("Current LVT Timer: ");
        uart_print_hex(lvt);
        uart_print("\n");
        */
        now_process->run_process();
    }
    else if (time_event->isEmpty() == false && time_event->top().time <= tsc_get()) {
        KEvent event = time_event->top();
        time_event->pop();
        //Process* target_process = ((Process*)PROCESS_QUEUE_BASE) + event.process_id; //언젠간 씀
        if (event.type == EVENT_TYPE_SLEEP) {
            add_process(event.process_id);
        }
        if (event.interval > 0) {
            event.time += event.interval;
            time_event->push(event);
        }
        uint64_t nexttime = next_process_time;
        if (time_event->isEmpty() == false) {
            nexttime = min(next_process_time, time_event->top().time);
        }
        tsc_deadline_set(nexttime);
    }
    else {
        lapic_tsc_deadline_set(1);
    }
    lapic_eoi();
    return (uint64_t*)0;
}
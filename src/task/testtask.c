#include <stdint.h>
#include "task.h"
#include "const.h"
#include "view/view.h"
#include "machine/cpu.h"
#include "mm/mm.h"
#include "string.h"
#include "usr/sysapi.h"

#define TEST_LOOP 1000000
#define LOCAL_STACK_SIZE 512

static void deep_stack(int depth)
{
    char buf[128];  // 制造栈占用
    memset(buf, depth, sizeof(buf));

    if (depth > 0)
        deep_stack(depth - 1);
}

void stress_thread(void)
{
    uint64_t i = 0;

    pcb_t *current = get_current();

    wb_printf("[stress] start: task=%p cpu=%d\n",
              current, get_logic_cpu_id());

    while (1)
    {
        /* 1️⃣ 制造栈压力 */
        deep_stack(4);

        /* 2️⃣ malloc / free */
        size_t sz = (get_ticks()) + 16;
        void *p = kmalloc(sz);
        if (!p)
        {
            wb_printf("[stress] malloc failed\n");
            continue;
        }

        memset(p, 0xAA, sz);

        /* 3️⃣ 打印当前状态 */
        if ((i % 1000) == 0)
        {
            wb_printf("[stress] i=%lu task=%p cpu=%d preempt=%d\n",i,current,get_logic_cpu_id(),current->preempt_count);
        }

        kfree(p);

        /* 4️⃣ 主动触发调度 */
        if ((i % 50) == 0)
        {
            yield();   // 强制让出 CPU
        }

        i++;
    }
}

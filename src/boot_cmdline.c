#include "multiboot.h"
#include "const.h"
#include "mm/mm.h"
#include "string.h"
#include "view/view.h"

extern char rootuuid[37];

static int extract_root_uuid(const char *cmdline, char *uuid_buf, size_t len);

void parse_cmd_line(MULTIBOOT_INFO *info){
    if (info->flags & MULTIBOOT_INFO_CMDLINE){
        const char *cmdline = easy_phy2linear(info->cmdline);
        if (extract_root_uuid(cmdline,(char *)rootuuid,37)){
            wb_printf("[CMDLINE] grub says that root is UUID=%s\n",rootuuid);
        }else{
            wb_printf("[CMDLINE] grub says that no root!!!");
            halt();
        }
    }else{
        wb_printf("[CMDLINE] grub give no cmdline!!!");
        halt();
    }
}

// 跳过空格
static const char* skip_spaces(const char *p) {
    while (*p == ' ') p++;
    return p;
}

// 从命令行中提取 UUID，存入 uuid_buf，最多 len 字节（包括 '\0'）
// 返回值：成功返回 1，未找到返回 0
static int extract_root_uuid(const char *cmdline, char *uuid_buf, size_t len) {
    const char *p = cmdline;
    const char *prefix = "root=UUID=";
    size_t prefix_len = strlen(prefix);

    while (*p) {
        p = skip_spaces(p);
        if (strncmp(p, prefix, prefix_len) == 0) {
            p += prefix_len;  // 指向 UUID 的起始位置
            // 复制 UUID，直到遇到空格或字符串结束
            size_t i = 0;
            while (p[i] && p[i] != ' ' && i < len - 1) {
                uuid_buf[i] = p[i];
                i++;
            }
            uuid_buf[i] = '\0';
            return 1;
        }
        // 跳过当前参数直到下一个空格或结束
        while (*p && *p != ' ') p++;
    }
    return 0; // 未找到
}

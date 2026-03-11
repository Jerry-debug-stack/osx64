#ifndef OS_EHCI_H
#define OS_EHCI_H

#include <stdint.h>
#include <stdbool.h>
#include "lib/wait_queue.h"
#include "lib/safelist.h"
#include "lib/atomic.h"

#define EHCI_CAPLENGTH       0x00    // 8位，操作寄存器偏移量
#define EHCI_HCIVERSION      0x02    // 16位，BCD版本号
#define EHCI_HCSPARAMS       0x04    // 32位，结构参数
#define EHCI_HCCPARAMS       0x08    // 32位，能力参数
#define EHCI_HCSP_PORTROUTE  0x0C    // 64位，端口路由（可选）

// 操作寄存器（Operational Registers）基址 = 能力基址 + CAPLENGTH
// 以下偏移相对于操作寄存器基址
#define EHCI_USBCMD          0x00    // USB 命令寄存器
#define EHCI_USBSTS          0x04    // USB 状态寄存器
#define EHCI_USBINTR         0x08    // USB 中断使能寄存器
#define EHCI_FRINDEX         0x0C    // 帧索引寄存器
#define EHCI_CTRLDSSEGMENT   0x10    // 控制数据结构段寄存器（64位寻址高32位）
#define EHCI_PERIODICLISTBASE 0x14   // 周期列表基址寄存器
#define EHCI_ASYNCLISTADDR   0x18    // 异步列表基址寄存器
#define EHCI_CONFIGFLAG      0x40    // 配置标志寄存器
#define EHCI_PORTSC          0x44    // 端口状态/控制寄存器（第一个端口，每端口占4字节）

// USBCMD 位
#define EHCI_CMD_RUN         (1 << 0)   // 运行/停止
#define EHCI_CMD_HCRESET     (1 << 1)   // 主机控制器复位
#define EHCI_CMD_FLSIZE_MASK (3 << 2)   // 帧列表大小（如果可编程）
#define EHCI_CMD_FLSIZE_1024 (0 << 2)   // 1024 元素（默认）
#define EHCI_CMD_FLSIZE_512  (1 << 2)
#define EHCI_CMD_FLSIZE_256  (2 << 2)
#define EHCI_CMD_PSE         (1 << 4)   // 周期调度使能
#define EHCI_CMD_ASE         (1 << 5)   // 异步调度使能
#define EHCI_CMD_IAAD        (1 << 6)   // 异步列表中断门控
#define EHCI_CMD_LIGHT_RESET (1 << 7)   // 轻量级复位（部分控制器支持）
#define EHCI_CMD_INT_THRESH_MASK (0xFF << 8) // 中断节流控制
#define EHCI_CMD_INT_THRESH_1MF (0x01 << 8)  // 1微帧

// USBSTS 位
#define EHCI_STS_USBINT      (1 << 0)   // USB 中断
#define EHCI_STS_USBERRINT   (1 << 1)   // USB 错误中断
#define EHCI_STS_PORT_CHG    (1 << 2)   // 端口改变
#define EHCI_STS_FRI         (1 << 3)   // 帧列表滚动
#define EHCI_STS_AA          (1 << 4)   // 异步列表门控中断
#define EHCI_STS_SYSERR      (1 << 5)   // 系统错误
#define EHCI_STS_IAA         (1 << 5)   // 门控中断（相同位，不同命名）
#define EHCI_STS_HALTED      (1 << 12)  // HC 停止
#define EHCI_STS_RECLAMATION  (1 << 13) // 异步列表状态

// USBINTR 位（与 USBSTS 对应）
#define EHCI_INTR_USBINT     EHCI_STS_USBINT
#define EHCI_INTR_USBERRINT  EHCI_STS_USBERRINT
#define EHCI_INTR_PORT_CHG   EHCI_STS_PORT_CHG
#define EHCI_INTR_FRI        EHCI_STS_FRI
#define EHCI_INTR_AA         EHCI_STS_AA
#define EHCI_INTR_IAA        EHCI_STS_IAA
#define EHCI_INTR_SYSERR     EHCI_STS_SYSERR

// CONFIGFLAG 位
#define EHCI_CFG_FLAG        (1 << 0)   // 配置标志（必须置1才能使用端口）

// 端口状态/控制寄存器 (PORTSC) 位定义
#define EHCI_PORTSC_CCS              (1 << 0)   // Current Connect Status - 当前连接状态
#define EHCI_PORTSC_CSC              (1 << 1)   // Connect Status Change - 连接状态改变
#define EHCI_PORTSC_PE               (1 << 2)   // Port Enable - 端口使能
#define EHCI_PORTSC_PEC              (1 << 3)   // Port Enable/Disable Change - 端口使能改变
#define EHCI_PORTSC_OCA              (1 << 4)   // Overcurrent Active - 过流激活
#define EHCI_PORTSC_OCC              (1 << 5)   // Overcurrent Change - 过流改变
#define EHCI_PORTSC_FPR              (1 << 6)   // Force Port Resume - 强制端口恢复
#define EHCI_PORTSC_SUSPEND          (1 << 7)   // Suspend - 挂起
#define EHCI_PORTSC_PR               (1 << 8)   // Port Reset - 端口复位
#define EHCI_PORTSC_RESERVED9        (1 << 9)   // 保留位（必须写0）
#define EHCI_PORTSC_LINESTATE_MASK   (3 << 10)  // 线路状态 (位11-10)
#define EHCI_PORTSC_LINESTATE_SE0    (0 << 10)  // SE0
#define EHCI_PORTSC_LINESTATE_J      (1 << 10)  // J 状态
#define EHCI_PORTSC_LINESTATE_K      (2 << 10)  // K 状态
#define EHCI_PORTSC_PP               (1 << 12)  // Port Power - 端口电源
#define EHCI_PORTSC_OWNER            (1 << 13)  // Port Owner - 端口所有者（1=Companion控制器）
#define EHCI_PORTSC_PIC_MASK         (3 << 14)  // Port Indicator Control (位15-14)
#define EHCI_PORTSC_PIC_OFF          (0 << 14)  // 关闭
#define EHCI_PORTSC_PIC_AMBER        (1 << 14)  // 琥珀色
#define EHCI_PORTSC_PIC_GREEN        (2 << 14)  // 绿色
#define EHCI_PORTSC_PTC_MASK         (0xF << 16) // Port Test Control (位19-16)
#define EHCI_PORTSC_PTC_SHIFT        16
#define EHCI_PORTSC_WKCNNT_E         (1 << 20)  // Wake on Connect Enable
#define EHCI_PORTSC_WKDSCNNT_E       (1 << 21)  // Wake on Disconnect Enable
#define EHCI_PORTSC_WKOC_E           (1 << 22)  // Wake on Overcurrent Enable

// 队列元素描述符（qTD） - 32 字节
typedef struct ehci_qtd {
    uint32_t next_qtd;          // 下一 qTD 指针（物理地址，低 5 位保留，必须为 0）
    uint32_t alt_next_qtd;      // 交替下一 qTD 指针（用于错误重试）
    uint32_t token;             // 令牌（包含状态、数据长度、PID、端点等）
    uint32_t buffer[5];         // 缓冲区指针数组（每个指针指向一个物理页，最多 5 页）
} __attribute__((aligned(32), packed)) ehci_qtd_t;

// qTD 令牌（token）字段的位定义
#define EHCI_QTD_STATUS_MASK    0xFF        // 状态字节（低 8 位）
#define EHCI_QTD_PID_MASK       (3 << 8)     // PID 编码（位 8-9）
#define EHCI_QTD_PID_OUT        (0 << 8)     // OUT
#define EHCI_QTD_PID_IN         (1 << 8)     // IN
#define EHCI_QTD_PID_SETUP      (2 << 8)     // SETUP
#define EHCI_QTD_CERR_MASK      (3 << 10)    // 错误计数（位 10-11）
#define EHCI_QTD_CERR(n)        (((n) & 3) << 10)
#define EHCI_QTD_CPAGE_MASK     (7 << 12)    // 当前页索引（位 12-14）
#define EHCI_QTD_IOC             (1 << 15)   // 中断完成
#define EHCI_QTD_LENGTH_MASK    (0x7FFF << 16) // 数据长度（位 16-30，最大 16384）
#define EHCI_QTD_TOGGLE          (1 << 31)   // 数据 toggle（如果使用数据 toggle 同步）

// qTD 状态位（位于 token 的低 8 位）
#define EHCI_QTD_ACTIVE         (1 << 7)     // 活跃
#define EHCI_QTD_HALTED         (1 << 6)     // 停止（错误）
#define EHCI_QTD_DATABUFFERERR  (1 << 5)     // 数据缓冲区错误
#define EHCI_QTD_BABBLE         (1 << 4)     // 溢出
#define EHCI_QTD_XACTERR        (1 << 3)     // 事务错误
#define EHCI_QTD_MISSEDMICRO    (1 << 2)     // 错过微帧（等时）
#define EHCI_QTD_SPLITXSTATE    (1 << 1)     // 分拆事务状态
#define EHCI_QTD_PINGSTATE      (1 << 0)     // Ping 状态

// 队列头（QH） - 32 字节
typedef struct ehci_qh {
    uint32_t horiz_link;
    uint32_t ep_char;
    uint32_t ep_cap;
    uint32_t current_qtd;
    uint32_t next_qtd;
    uint32_t alt_qtd;
} __attribute__((aligned(64), packed)) ehci_qh_t;

// 水平链接指针标志位（低 5 位）
#define EHCI_PTR_MASK           0xFFFFFFE0   // 地址掩码（32 字节对齐）
#define EHCI_PTR_TERM           0x00000001   // 终止（链表结束）
#define EHCI_PTR_TYPE_QH        0x00000002   // 指向 QH（位 1 为 1 表示 QH）
#define EHCI_PTR_TYPE_ITD       0x00000004   // 指向 iTD（位 2）
#define EHCI_PTR_TYPE_SITD      0x00000006   // 指向 siTD（位 1 和 2 同时为 1？实际 siTD 类型编码可能不同）
#define EHCI_PTR_ASY_TERM       0x00000000

// ep_char
// EHCI QH ep_char 字段掩码和宏
#define EHCI_EPCHAR_DEV_ADDR_MASK      0x0000007F  // 设备地址（位 0-6）
#define EHCI_EPCHAR_EP_NUM_MASK        0x00000F00  // 端点号（位 8-11）
#define EHCI_EPCHAR_EP_NUM_SHIFT       8           // 端点号左移位数
#define EHCI_EPCHAR_EPS_MASK           0x00003000  // 端点速度（位 12-13）
#define EHCI_EPCHAR_EPS_FULL           0x00000000  // 全速
#define EHCI_EPCHAR_EPS_LOW            0x00001000  // 低速
#define EHCI_EPCHAR_EPS_HIGH           0x00002000  // 高速
#define EHCI_EPCHAR_DTC                0x00004000  // 数据 toggle 控制（1=硬件管理）
#define EHCI_EPCHAR_HEAD               0x00008000  // 头列表标志
#define EHCI_EPCHAR_MAX_PKT_LEN_MASK   0xFFFF0000  // 最大包长（位 16-31）

// ep_cap
#define EHCI_CAP_INTR_SCHEDULE_MASK 0x000000FF
#define EHCI_CAP_SPLIT_COMPLE_MASK  0x0000FF00
#define EHCI_CAP_HUB_ADDR_MASK      0x007F0000
#define EHCI_CAP_PORT_NUM_MASK      0x3F800000
#define EHCI_CAP_HIGH_BANDWITH_MUL  0xC0000000

// 帧列表项（Periodic Frame List Element） - 32 位
typedef uint32_t ehci_frame_list_t[1024];  // 默认 1024 项，4KB 对齐

// 帧列表项指针标志（同水平链接指针）
#define EHCI_FRAME_PTR_MASK     0xFFFFFFF0   // 实际地址掩码（16 字节对齐，但 QH 需要 32 字节，所以实际可能是 32 字节）
// 由于 QH 要求 32 字节对齐，实际可用 0xFFFFFFE0，但规范说帧列表项低 4 位用于标志，但 QH 类型要求低 2 位为 00。
// 为了安全，使用 EHCI_PTR_* 标志。

// USB 标准请求类型（同 UHCI）
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_SET_CONFIGURATION   0x09

// 描述符类型
#define USB_DESC_DEVICE              1
#define USB_DESC_CONFIGURATION       2
#define USB_DESC_INTERFACE           4
#define USB_DESC_ENDPOINT            5

// Mass Storage 相关
#define USB_CLASS_MASS_STORAGE       0x08
#define USB_SUBCLASS_SCSI            0x06
#define USB_PROTOCOL_BULK_ONLY       0x50

// CBW 和 CSW 签名
#define CBW_SIGNATURE                0x43425355  // 'USBC' 小端
#define CSW_SIGNATURE                0x53425355  // 'USBS' 小端

// 方向常量
#define USB_DIR_OUT                  0
#define USB_DIR_IN                   1

// PID 值
#define EHCI_PID_OUT         0
#define EHCI_PID_IN          1
#define EHCI_PID_SETUP       2

typedef struct ehci_controller {
    list_head_t ehci_controller_list;
    uint8_t bus, dev, func;
    uintptr_t reg_base;                // 内存映射基址（虚拟地址）
    uint32_t cap_length;                // CAPLENGTH 值
    int port_num;

    // 异步列表
    struct ehci_qh *async_dummy_qh;      // 异步列表头 QH（循环链表）
    uint64_t async_dummy_qh_phy;

    // 空闲 qTD（用于 QH 初始垂直链接）
    struct ehci_qtd *idle_qtd;
    uint64_t idle_qtd_phy;

    struct ehci_qh *default_control_qh;
    uint64_t default_control_qh_phy;

    spinlock_t lock;
    struct list_head req_queue;          // 等待处理的请求队列
    struct ehci_request *active_req;     // 当前活动的请求（可扩展为每个端点一个）
    uint8_t next_addr;

    atomic_t next_tag;

    struct ehci_device *ports[];
} ehci_controller_t;

typedef enum {
    EHCI_REQ_CONTROL,   // 控制传输
    EHCI_REQ_BULK       // 批量传输（用于 U 盘读写）
} ehci_req_type_t;

// 请求结构（与 UHCI 类似，但使用 EHCI 的 qTD 和 QH）
typedef struct ehci_request {
    struct list_head list;
    ehci_req_type_t type;
    struct ehci_device *dev;
    ehci_controller_t *hc;
    volatile int finished;
    int status;
    wait_queue_t wq;

    // EHCI 特有字段
    struct ehci_qtd *qtd_head;       // qTD 链首（虚拟地址）
    struct ehci_qtd *qtd_last;       // 最后一个 qTD
    uint32_t page_num;               // 占用的物理页数
    struct ehci_qh *qh;               // 所属端点的 QH（请求提交时需绑定）
    uint64_t qh_phy;

    // 请求数据（同 UHCI）
    union {
        struct {
            uint8_t dev_addr;
            uint8_t setup[8];
            void *data;
            int data_len;
            int dir;
        } control;
        struct {
            int write;                       // 数据方向：0=主机到设备（OUT），1=设备到主机（IN）
            uint64_t lba;                     // 逻辑块地址
            uint32_t count;                    // 块数
            uint32_t data_len;                  // 数据长度（字节）
            void *buffer;                       // 数据缓冲区虚拟地址
            uint8_t cbw[31];                    // CBW 包
            uint8_t csw[13];                    // CSW 包
            uint64_t cr3;                        // 进程页表（用于地址转换）

            int phase;                           // 当前阶段：0=CBW, 1=DATA, 2=CSW, 3=已完成
            struct {
                struct ehci_qtd *head;            // 该阶段的qTD链头（虚拟地址）
                struct ehci_qtd *last;             // 该阶段的最后一个qTD
                struct ehci_qh *qh;                 // 该阶段使用的QH
                uint64_t qh_phy;                     // QH物理地址
            } phase_info[3];                        // 索引0: CBW, 1: DATA, 2: CSW
        } bulk;
    };
} ehci_request_t;

typedef struct ehci_device {
    list_head_t node;
    int port;
    uint8_t address;
    uint8_t speed;
    bool is_mass_storage;
    struct ehci_controller *hc;
    // 为每个端点保存对应的 QH
    struct ehci_qh *qh_control;      // 端点0 QH
    uint64_t qh_control_phy;
    struct ehci_qh *qh_bulk_in;       // 批量输入端点 QH
    uint64_t qh_bulk_in_phy;
    struct ehci_qh *qh_bulk_out;      // 批量输出端点 QH
    uint64_t qh_bulk_out_phy;

    uint32_t block_count;
    uint32_t block_size;
    uint16_t max_packet_size;

} ehci_device_t;

// 构建水平链接指针（带类型标志）
#define EHCI_LINK(addr, type)       (((uint32_t)(addr) & 0xFFFFFFE0) | ((type) & 0x1F))
#define EHCI_LINK_TERM              0x00000001

// qTD 令牌构建
#define EHCI_BUILD_TOKEN(length, ioc, toggle, pid, cerr) \
    ((((length) & 0x7FFF) << 16) | \
     ((ioc) ? EHCI_QTD_IOC : 0) | \
     (((toggle) ? 1 : 0) << 31) | \
     (((pid) & 3) << 8) | \
     (((cerr) & 3) << 10) | \
     EHCI_QTD_ACTIVE)

// 从 token 提取状态
#define EHCI_TOKEN_STATUS(token)    ((token) & 0xFF)

// 检查 qTD 是否完成（不再活跃且无错误）
#define EHCI_QTD_DONE(qtd)          (!((qtd)->token & EHCI_QTD_ACTIVE) && !((qtd)->token & EHCI_QTD_HALTED))

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

// 配置描述符头部
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

// 接口描述符
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

// 端点描述符
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

#endif

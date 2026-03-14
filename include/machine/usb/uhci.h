#ifndef OS_UHCI_H
#define OS_UHCI_H

#include "stdint.h"
#include "lib/safelist.h"
#include "lib/wait_queue.h"
#include "lib/atomic.h"
#include <stdbool.h>

// 帧列表 (1024 项，必须 4KB 对齐)
#define UHCI_FRAMES 1024

// UHCI 链接指针控制位
#define UHCI_PTR_TERM           0x00000001   // 终止位
#define UHCI_PTR_QH             0x00000002   // 指向 QH
#define UHCI_PTR_DEPTH          0x00000004   // 深度优先
#define UHCI_PTR_MASK           0xFFFFFFF0  // 地址掩码（低 4 位用于控制）

// TD Control Status bits (32-bit)
#define TD_CTL_SHORT_PACK           (1 << 29)
#define TD_CTL_ERR_CNT(cnt)         (((cnt) & 3) << 27)
#define TD_CTL_LOW_SPEED            (1 << 26)
#define TD_CTL_ISOCHRONOUS          (1 << 25)
#define TD_CTL_INTR_ON_COMPLETE     (1 << 24)
#define TD_CTL_ACTIVE               (1 << 23)
#define TD_CTL_STALLED              (1 << 22)
#define TD_CTL_DATA_BUFFER_ERR      (1 << 21)
#define TD_CTL_BUBBLE_DETECTED      (1 << 20)
#define TD_CTL_NON_ACC              (1 << 19)
#define TD_CTL_TIMEOUT_CRC          (1 << 18)
#define TD_CTL_STUFF_ERROR          (1 << 17)
#define TD_CTL_ACTUAL_LENGTH(status) ((status & 0x7FF) + 1)
#define TD_CTL_NORMAL_BULK_USE      (TD_CTL_ACTIVE | TD_CTL_SHORT_PACK | TD_CTL_ERR_CNT(3))
#define TD_CTL_NORMAL_CTRL_USE      (TD_CTL_ACTIVE | TD_CTL_ERR_CNT(3))

#define TD_PH_PID_SETUP             0x2D

#define COMBINE_EP_PID(ep, pid)  ((((ep) & 0xF) << 15) | ((pid) & 0xFF))
#define TD_PH(length, toggle, ep, pid, device) \
    ( ((((length) - 1) & 0x7FF) << 21) | \
      (((toggle) & 1) << 19) | \
      (((ep) & 0xF) << 15) | \
      ((pid) & 0xFF) | \
      (((device) & 0x7F) << 8) )

// 寄存器偏移 (I/O 空间)
#define UHCI_USBCMD      0x00    // 命令寄存器
#define UHCI_USBSTS      0x02    // 状态寄存器
#define UHCI_USBINTR     0x04    // 中断使能 (轮询模式不使用)
#define UHCI_FRNUM       0x06    // 帧号
#define UHCI_FLBASEADD   0x08    // 帧列表基址 (4KB 对齐)
#define UHCI_SOFMOD      0x0C    // SOF 调节
#define UHCI_PORTSC1     0x10    // 端口 1 状态/控制
#define UHCI_PORTSC2     0x12    // 端口 2 状态/控制

// USBCMD 命令寄存器位
#define CMD_RUN          0x0001  // 运行/停止
#define CMD_HCRESET      0x0002  // 主机控制器复位
#define CMD_MAX64        0x0080  // 最大包长 64 字节
#define CMD_CF           0x0040  // 配置标志 (设置为 1)

// USBSTS 状态寄存器位
#define STS_IOC         (1 << 0)   // Interrupt on Completion
#define STS_HCPE        (1 << 1)   // Host Controller Process Error
#define STS_HSE         (1 << 2)   // Host System Error
#define STS_RD          (1 << 3)   // Resume Detect
#define STS_USBERR      (1 << 4)   // USB Error Interrupt (Error Interrupt)
#define STS_HCHALTED    (1 << 5)   // 主机控制器停止

// PORTSC 端口寄存器位
#define PORT_CCS         0x0001  // 当前连接状态
#define PORT_CSC         0x0002  // 连接状态改变
#define PORT_PED         0x0004  // 端口使能
#define PORT_PEC         0x0008  // 端口使能改变
#define PORT_LS          0x0100  // 低速设备
#define PORT_PR          0x0200  // 端口复位

#define UHCI_PORTSC(base, port) ((base) + 0x10 + (port) * 2)
#define uhci_port_read(base, port) io_inword(UHCI_PORTSC(base, port))
#define uhci_port_write(base, port, val) io_outword(UHCI_PORTSC(base, port), (val))

// USB 请求类型 (bmRequestType)
#define USB_REQ_TYPE_STANDARD   0x00
#define USB_REQ_TYPE_CLASS      0x20
#define USB_REQ_TYPE_VENDOR     0x40
#define USB_REQ_TYPE_MASK       0x60
#define USB_REQ_TYPE_RECIP_DEVICE   0x00
#define USB_REQ_TYPE_RECIP_INTERFACE 0x01
#define USB_REQ_TYPE_RECIP_ENDPOINT  0x02
#define USB_REQ_TYPE_RECIP_OTHER 0x03

// USB 标准请求 (bRequest)
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B
#define USB_REQ_SYNCH_FRAME         0x0C

// 描述符类型
#define USB_DESC_DEVICE             0x01
#define USB_DESC_CONFIGURATION      0x02
#define USB_DESC_STRING             0x03
#define USB_DESC_INTERFACE          0x04
#define USB_DESC_ENDPOINT           0x05
#define USB_DESC_DEVICE_QUALIFIER   0x06
#define USB_DESC_OTHER_SPEED        0x07
#define USB_DESC_INTERFACE_POWER    0x08

#define TD_SIZE_ALIGNED             32
#define NUM_OF_TD_IN_4K_PAGE        (4096 / TD_SIZE_ALIGNED)

typedef struct uhci_frame_list {
    uint32_t link[UHCI_FRAMES];  // 每个指针指向 TD 或 QH
} __attribute__((packed)) uhci_frame_list_t;

typedef struct uhci_td {
    uint32_t link;
    uint32_t ctrl_status;
    uint32_t packed_header;
    uint32_t buffer;
    volatile uint32_t rsvd[4];
} __attribute__((packed)) uhci_td_t;

typedef struct uhci_qh {
    uint32_t horiz_link;    // 水平链接指针
    uint32_t vert_link;     // 垂直链接指针（指向第一个 TD）
    uint32_t pad[2];        // 保留（必须为 0）
} __attribute__((packed)) uhci_qh_t;

typedef struct uhci_controller {
    list_head_t uhci_controller_list;
    uint8_t bus, dev, func;
    uint16_t io_base;
    uintptr_t pci_config_addr;
    struct uhci_frame_list *frame_list;
    int port_num;
    struct usb_device *ports[2];
    
    struct uhci_qh *global_qh;
    struct uhci_td *idle_td;
    uint32_t idle_td_phy;
    
    spinlock_t lock;                 // 保护请求队列和活动请求
    struct list_head req_queue;      // 等待处理的请求队列
    struct uhci_request *active_req; // 当前正在处理的请求
    uint8_t next_addr;

    atomic_t next_tag;
} uhci_controller_t;

typedef struct usb_device {
    list_head_t node;

    // 所属控制器和端口
    struct uhci_controller *hc;
    int port;

    // 设备状态
    uint8_t address;                 // 分配的总线地址 (1-127)
    uint8_t speed;                    // 0 全速，1 低速 (这里我们只关心全速)
    uint8_t config_value;             // 激活的配置值

    bool is_mass_storage;

    // 批量端点信息 (用于 U 盘)
    uint8_t bulk_in_ep;               // 批量输入端点地址 (含方向)
    uint8_t bulk_out_ep;              // 批量输出端点地址
    uint16_t max_packet_size;         // 端点最大包长 (通常 64)

    // 存储设备容量 (简化)
    uint32_t block_count;
    uint32_t block_size;
} usb_device_t;

typedef enum {
    UHCI_REQ_CONTROL,   // 控制传输
    UHCI_REQ_BULK       // 批量传输（用于 U 盘读写）
} uhci_req_type_t;

typedef struct uhci_request {
    struct list_head list;           // 挂入控制器的请求队列
    uhci_req_type_t type;            // 请求类型
    struct usb_device *dev;          // 所属设备（控制传输时可能为 NULL 或默认地址）
    volatile int finished;           // 完成标志
    int status;                      // 0 成功，负值错误码
    wait_queue_t wq;                 // 等待队列
    struct uhci_td *td_head;         // TD 链首(同时也是第一个物理页对应的vir_addr)
    struct uhci_td *last_td;         // 最后一个 TD（用于快速检查完成）
    uint32_t page_num;               // td占用几个物理页

    // 针对不同请求类型的私有数据
    union {
        struct {
            uint8_t dev_addr;
            uint8_t setup[8];        // 设置包
            void *data;              // 数据缓冲区（可选）
            int data_len;            // 数据长度
            int dir;                 // 0 OUT, 1 IN（数据阶段方向）
        } control;
        struct {
            int write;                // 1 写，0 读
            uint64_t lba;
            uint32_t count;
            uint32_t data_len;
            void *buffer;             // 数据缓冲区
            uint8_t cbw[31];          // CBW 包
            uint8_t csw[13];          // CSW 包（接收状态）
            uint64_t cr3;
        } bulk;
    };
} uhci_request_t;

// 设备描述符结构（仅需部分字段）
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

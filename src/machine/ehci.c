#include "machine/usb/ehci.h"
#include "machine/pcie/pcie.h"
#include "machine/pcie/pcie_type0.h"
#include "mm/mm.h"
#include "lib/io.h"
#include "string.h"
#include "const.h"
#include "task.h"
#include "lib/timer.h"
#include "fs/block.h"

extern uint64_t *vir_ptable4;

static list_head_t ehci_controllers = LIST_HEAD_INIT(ehci_controllers);

static int ehci_submit_control_request(struct ehci_controller *hc, struct ehci_device *dev,uint8_t *setup, void *data, int len, int dir);
static int ehci_submit_scsi_command(struct ehci_device *dev, uint8_t *cdb, int cdb_len,void *data, int data_len, int dir,ehci_request_t **tmp);
static int ehci_submit_rw_request(struct ehci_device *dev, int write, uint64_t lba, uint32_t count, void *buffer);

static inline uint32_t ehci_read32(struct ehci_controller *hc, uint32_t reg) {
    return *(volatile uint32_t*)(hc->reg_base + reg);
}
static inline void ehci_write32(struct ehci_controller *hc, uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(hc->reg_base + reg) = val;
}

static struct ehci_controller *ehci_alloc_controller(uint8_t bus, uint8_t dev, uint8_t func) {
    struct ehci_controller *hc = NULL;
    uintptr_t pcie_addr = MAKE_PCIE_ADDR(bus, dev, func, 0);
    uint32_t bar0, bar1 = 0;
    uint64_t phys_base;
    uintptr_t reg_base;
    uint32_t hcsparams;
    int port_num;
    void *page;
    uint64_t page_phys;

    bar0 = read_pcie(MAKE_PCIE_ADDR_1(pcie_addr, 0, BAR0));
    if (bar0 & 1) {
        return NULL;
    }

    int bar_type = (bar0 >> 1) & 0x3;
    if (bar_type == 0x2) {
        bar1 = read_pcie(MAKE_PCIE_ADDR_1(pcie_addr, 0, BAR1));
        phys_base = ((uint64_t)bar1 << 32) | (bar0 & ~0xF);
    } else {
        phys_base = bar0 & ~0xF;
    }

    reg_base = (uintptr_t)io_remap(phys_base, 0x1000);
    if (!reg_base) {
        return NULL;
    }

    hcsparams = *(volatile uint32_t*)(reg_base + EHCI_HCSPARAMS);
    port_num = hcsparams & 0xF;
    if (port_num == 0) {
        io_unmap(reg_base,0x1000);
        return NULL;
    }

    hc = kmalloc(sizeof(struct ehci_controller) + 8 * port_num);
    if (!hc) {
        io_unmap(reg_base,0x1000);
        return NULL;
    }
    memset(hc, 0, sizeof(struct ehci_controller) + 8 * port_num);

    hc->bus = bus;
    hc->dev = dev;
    hc->func = func;
    hc->reg_base = reg_base;
    hc->cap_length = *(volatile uint8_t *)(reg_base + EHCI_CAPLENGTH);
    hc->port_num = port_num;

    uint16_t cmd = read_pcie(MAKE_PCIE_ADDR_1(pcie_addr, 0, STATUS_COMMAND));
    cmd |= PCI_CMD_BUS_MASTER | PCI_CMD_MEM_SPACE;
    write_pcie(MAKE_PCIE_ADDR_1(pcie_addr, 0, STATUS_COMMAND), cmd);

    page = kmalloc(4096);
    if (!page) {
        io_unmap(reg_base,0x1000);
        kfree(hc);
        return NULL;
    }
    page_phys = (uint64_t)easy_linear2phy(page);

    hc->async_dummy_qh = (struct ehci_qh *)page;
    hc->async_dummy_qh_phy = page_phys;
    hc->idle_qtd = (struct ehci_qtd *)(page + 64);
    hc->idle_qtd_phy = page_phys + 64;

    memset(hc->async_dummy_qh, 0, sizeof(struct ehci_qh));
    memset(hc->idle_qtd, 0, sizeof(struct ehci_qtd));

    hc->idle_qtd->next_qtd = EHCI_PTR_TERM;
    hc->idle_qtd->token = 0;

    spin_lock_init(&hc->lock);
    INIT_LIST_HEAD(&hc->req_queue);
    INIT_LIST_HEAD(&hc->ehci_controller_list);
    hc->active_req = NULL;
    hc->next_addr = 1;
    atomic_set(&hc->next_tag, 0);

    return hc;
}

static int ehci_hw_init(struct ehci_controller *hc) {
    volatile uint32_t *op_base = (void *)(hc->reg_base + hc->cap_length);  // 操作寄存器基址
    uint32_t cmd;
    int timeout;

    // 1. 复位主机控制器
    cmd = op_base[EHCI_USBCMD / 4];
    cmd |= EHCI_CMD_HCRESET;
    op_base[EHCI_USBCMD / 4] = cmd;

    // 等待复位完成（HCRESET 位自动清零）
    timeout = 10000;
    while ((op_base[EHCI_USBCMD / 4] & EHCI_CMD_HCRESET) && timeout--) {
        mdelay(1);
    }
    if (timeout <= 0) {
        // 复位超时
        return -1;
    }

    // 复位后需要延迟（规范建议至少 10ms）
    mdelay(10);

    memset(hc->async_dummy_qh, 0, 96);
    hc->async_dummy_qh->horiz_link = hc->async_dummy_qh_phy | EHCI_PTR_TYPE_QH;
    // 垂直链接：指向空闲 qTD（不活跃的 qTD）
    hc->async_dummy_qh->alt_qtd = hc->async_dummy_qh->current_qtd = EHCI_PTR_TERM;
    hc->async_dummy_qh->ep_char = EHCI_EPCHAR_HEAD;
    hc->async_dummy_qh->next_qtd = hc->idle_qtd_phy;
    
    hc->idle_qtd->next_qtd = EHCI_PTR_TERM;
    hc->idle_qtd->alt_next_qtd = EHCI_PTR_TERM;
    hc->idle_qtd->token = 0;

    op_base[EHCI_ASYNCLISTADDR / 4] = hc->async_dummy_qh_phy;

    op_base[EHCI_CONFIGFLAG / 4] = EHCI_CFG_FLAG;

    cmd = op_base[EHCI_USBCMD / 4];
    cmd |= EHCI_CMD_RUN | EHCI_CMD_ASE;
    cmd &= ~EHCI_CMD_PSE;
    cmd &= ~EHCI_CMD_INT_THRESH_MASK;
    op_base[EHCI_USBCMD / 4] = cmd;

    timeout = 10000;
    while ((op_base[EHCI_USBSTS / 4] & EHCI_STS_HALTED) && timeout--) {
        mdelay(1);
    }
    if (timeout <= 0) {
        // 启动失败
        return -1;
    }

    for (int i = 0; i < hc->port_num; i++) {
        uint32_t portsc = op_base[EHCI_PORTSC / 4 + i];
        if (!(portsc & EHCI_PORTSC_PP)) {
            portsc |= EHCI_PORTSC_PP;
            op_base[EHCI_PORTSC / 4 + i] = portsc;
        }
    }

    return 0;
}

int init_ehci_controller(uint8_t bus, uint8_t dev, uint8_t func) {
    struct ehci_controller *hc = ehci_alloc_controller(bus, dev, func);
    if (!hc) return -1;

    if (ehci_hw_init(hc) != 0) {
        kfree(hc);
        return -1;
    }

    list_add_tail(&ehci_controllers, &hc->ehci_controller_list);
    return 0;
}

static int ehci_port_reset(struct ehci_controller *hc, int port) {
    if (port < 0 || port >= hc->port_num)
        return -1;

    uint32_t op_base = hc->cap_length;  // 操作寄存器偏移
    uint32_t portsc_addr = op_base + EHCI_PORTSC + port * 4;
    uint32_t portsc;
    int timeout;

    // 1. 检查是否有设备连接
    portsc = ehci_read32(hc, portsc_addr);
    if (!(portsc & EHCI_PORTSC_CCS)) {
        // 无设备连接
        return -1;
    }

    // 2. 设置端口复位位
    portsc |= EHCI_PORTSC_PR;
    ehci_write32(hc, portsc_addr, portsc);

    // 3. 等待至少 50ms（规范要求最少 10ms，但建议 50ms）
    mdelay(50);

    // 4. 清除复位位（有些控制器会自动清零，但手动清除更安全）
    portsc = ehci_read32(hc, portsc_addr);
    portsc &= ~EHCI_PORTSC_PR;
    ehci_write32(hc, portsc_addr, portsc);

    // 5. 等待端口使能（PE）或直到复位完成（例如 CSC 置位）
    //    规范中，复位完成后，端口应自动使能（对于高速设备）
    timeout = 10000;  // 超时 10ms 左右（根据轮询粒度）
    while (timeout--) {
        portsc = ehci_read32(hc, portsc_addr);
        if (portsc & EHCI_PORTSC_PE) {
            // 端口使能，成功
            return 0;
        }
        // 如果连接状态改变，可能设备已断开
        if (portsc & EHCI_PORTSC_CSC) {
            // 连接状态改变，设备可能已断开
            break;
        }
        mdelay(1);
    }

    // 超时或设备断开
    return -1;
}

struct ehci_qh *ehci_create_qh(struct ehci_controller *hc, uint8_t dev_addr,uint8_t ep_num, uint16_t max_packet) {
    // 分配一页物理连续内存（4KB），作为 QH 的存储空间
    void *page = kmalloc(4096);
    if (!page)
        return NULL;

    uint64_t page_phys = (uint64_t)easy_linear2phy(page);
    struct ehci_qh *qh = (struct ehci_qh *)page;
    memset(qh, 0, sizeof(struct ehci_qh));   // 清零前 32 字节，剩余部分（填充区）忽略

    // ---- 设置端点特性字 ep_char[0] ----
    uint32_t ep_char0 = 0;
    ep_char0 |= (dev_addr & 0x7F);
    ep_char0 |= ((ep_num & 0x0F) << 8);
    ep_char0 |= (2 << 12);
    ep_char0 |= (1 << 14);
    ep_char0 |= ((uint32_t)max_packet << 16);
    qh->ep_char = ep_char0;

    // ---- 设置 ep_char[1] ----
    // 假设设备直接连接在主控制器，无需分拆事务，所以全部清零
    qh->ep_cap = 0;

    // ---- 垂直链接：指向空闲 qTD ----
    qh->next_qtd = hc->idle_qtd_phy;               // 初始无工作
    qh->current_qtd = qh->alt_qtd = EHCI_PTR_TERM;

    // ---- 插入异步列表（在 dummy QH 之后） ----
    spin_lock(&hc->lock);   // 需要保护链表操作
    struct ehci_qh *dummy_qh = hc->async_dummy_qh;

    // 新 QH 的水平链接指向 dummy 原来指向的下一个 QH
    qh->horiz_link = dummy_qh->horiz_link;

    // 确保新 QH 的物理地址加上类型标志（QH 类型：低两位为 00）
    uint32_t new_qh_phy = (uint32_t)page_phys;     // 物理地址在 32 位内
    dummy_qh->horiz_link = new_qh_phy | EHCI_PTR_TYPE_QH;

    spin_unlock(&hc->lock);

    // 返回虚拟地址
    return qh;
}

void ehci_destroy_qh(struct ehci_controller *hc, struct ehci_qh *qh) {
    if (!qh || !hc) return;

    spin_lock(&hc->lock);

    struct ehci_qh *prev = hc->async_dummy_qh;
    struct ehci_qh *curr;

    while (1) {
        uint32_t next_phy = prev->horiz_link & ~0x1F;
        if (next_phy == 0 || next_phy == EHCI_PTR_TERM) {
            break;
        }

        curr = (struct ehci_qh *)easy_phy2linear(next_phy);
        if (curr == qh) {
            prev->horiz_link = qh->horiz_link;
            break;
        }

        prev = curr;
        if (curr == hc->async_dummy_qh) {
            break;
        }
    }

    spin_unlock(&hc->lock);
    kfree(qh);
}

static struct ehci_device *ehci_enumerate_device(struct ehci_controller *hc, int port) {
    uint8_t setup[8];
    uint8_t buf[64];
    int ret;
    struct ehci_device *dev = NULL;
    uint16_t max_packet_size;
    uint8_t *conf_buf = NULL;
    int total_len;
    int pos;
    uint8_t config_value = 0;

    // 1. 确保端口已使能（复位端口）
    if (ehci_port_reset(hc, port) < 0) {
        return NULL;
    }

    // 2. 获取设备描述符前8字节（得到最大包长）
    setup[0] = 0x80;                     // 方向 IN
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0;
    setup[3] = USB_DESC_DEVICE;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 8;
    setup[7] = 0;

    ret = ehci_submit_control_request(hc, NULL, setup, buf, 8, 1); // dev=NULL 表示地址0
    if (ret < 0) {
        return NULL;
    }
    max_packet_size = buf[7];             // bMaxPacketSize0

    // 3. 分配设备结构
    dev = kmalloc(sizeof(struct ehci_device));
    if (!dev) return NULL;
    memset(dev, 0, sizeof(*dev));
    dev->hc = (void*)hc;
    dev->port = port;
    dev->max_packet_size = max_packet_size;
    dev->speed = 2;                        // 2 表示高速（根据你的定义）

    // 4. 分配新地址
    dev->address = hc->next_addr++;
    setup[0] = 0x00;                       // OUT
    setup[1] = USB_REQ_SET_ADDRESS;
    setup[2] = dev->address;
    setup[3] = 0;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 0;
    setup[7] = 0;
    ret = ehci_submit_control_request(hc, NULL, setup, NULL, 0, 0);
    if (ret < 0) {
        kfree(dev);
        hc->next_addr--;
        return NULL;
    }
    mdelay(2);                              // 等待地址生效

    // 5. 为设备创建控制端点 QH（使用新地址，端点0，最大包长）
    dev->qh_control = ehci_create_qh(hc, dev->address, 0, max_packet_size);
    if (!dev->qh_control) {
        kfree(dev);
        hc->next_addr--;
        return NULL;
    }

    // 6. 获取完整设备描述符（18字节）
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0;
    setup[3] = USB_DESC_DEVICE;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 18;
    setup[7] = 0;
    ret = ehci_submit_control_request(hc, dev, setup, buf, 18, 1);
    if (ret < 0) {
        goto err;
    }

    // 7. 获取配置描述符总长度
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0;
    setup[3] = USB_DESC_CONFIGURATION;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 4;
    setup[7] = 0;
    ret = ehci_submit_control_request(hc, dev, setup, buf, 4, 1);
    if (ret < 0) {
        goto err;
    }
    total_len = buf[2] | (buf[3] << 8);

    // 8. 读取完整配置描述符
    conf_buf = kmalloc(total_len);
    if (!conf_buf) {
        goto err;
    }
    setup[6] = total_len & 0xFF;
    setup[7] = total_len >> 8;
    ret = ehci_submit_control_request(hc, dev, setup, conf_buf, total_len, 1);
    if (ret < 0) {
        kfree(conf_buf);
        goto err;
    }

    // 9. 解析配置描述符，查找 Mass Storage 接口（类 0x08，子类 0x06，协议 0x50）
    pos = 0;
    while (pos < total_len) {
        uint8_t len = conf_buf[pos];
        uint8_t type = conf_buf[pos + 1];
        if (type == 4) { // 接口描述符
            usb_interface_descriptor_t *intf = (usb_interface_descriptor_t*)&conf_buf[pos];
            if (intf->bInterfaceClass == 0x08 &&
                intf->bInterfaceSubClass == 0x06 &&
                intf->bInterfaceProtocol == 0x50) {
                dev->is_mass_storage = 1;
                // 遍历该接口的端点描述符
                int ep_pos = pos + len;
                while (ep_pos < total_len && conf_buf[ep_pos] != 0) {
                    uint8_t ep_len = conf_buf[ep_pos];
                    uint8_t ep_type = conf_buf[ep_pos + 1];
                    if (ep_type == 5) { // 端点描述符
                        usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t*)&conf_buf[ep_pos];
                        if ((ep->bmAttributes & 0x03) == 2) { // 批量端点
                            if (ep->bEndpointAddress & 0x80) {
                                dev->qh_bulk_in = ehci_create_qh(hc, dev->address,ep->bEndpointAddress & 0x0F,ep->wMaxPacketSize);
                                dev->qh_bulk_in_phy = (uint64_t)easy_linear2phy(dev->qh_bulk_in);
                            } else {
                                dev->qh_bulk_out = ehci_create_qh(hc, dev->address,ep->bEndpointAddress & 0x0F,ep->wMaxPacketSize);
                                dev->qh_bulk_out_phy = (uint64_t)easy_linear2phy(dev->qh_bulk_out);
                            }
                            dev->max_packet_size = ep->wMaxPacketSize; // 更新为端点最大包长
                        }
                    }
                    ep_pos += ep_len;
                }
                usb_config_descriptor_t *cfg = (usb_config_descriptor_t*)conf_buf;
                config_value = cfg->bConfigurationValue;
                break;
            }
        }
        pos += len;
    }
    kfree(conf_buf);

    if (!dev->is_mass_storage) {
        goto err; // 不是 U 盘，释放资源
    }

    // 10. 设置配置
    setup[0] = 0x00;
    setup[1] = USB_REQ_SET_CONFIGURATION;
    setup[2] = config_value;
    setup[3] = 0;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 0;
    setup[7] = 0;
    ret = ehci_submit_control_request(hc, dev, setup, NULL, 0, 0);
    if (ret < 0) {
        goto err;
    }

    // 11. 读取容量 (SCSI READ CAPACITY 10) —— 需要批量传输函数，暂时占位
    
    uint8_t cdb10[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t capacity_data[8];
    ret = ehci_submit_scsi_command(dev, cdb10, 10, capacity_data, 8, 1, NULL);
    if (ret < 0) {
        goto err;
    }
    uint32_t last_lba = ((uint32_t)capacity_data[0] << 24) |
                        ((uint32_t)capacity_data[1] << 16) |
                        ((uint32_t)capacity_data[2] << 8)  |
                        capacity_data[3];
    uint32_t block_size = ((uint32_t)capacity_data[4] << 24) |
                          ((uint32_t)capacity_data[5] << 16) |
                          ((uint32_t)capacity_data[6] << 8)  |
                          capacity_data[7];
    dev->block_count = (uint64_t)last_lba + 1;
    dev->block_size = block_size;
    if (block_size != 512) {
        goto err;
    }

    return dev;

err:
    // 清理已创建的 QH 和设备结构
    if (dev->qh_control) {
        ehci_destroy_qh(hc, dev->qh_control);
    }
    if (dev->qh_bulk_in) {
        ehci_destroy_qh(hc, dev->qh_bulk_in);
    }
    if (dev->qh_bulk_out) {
        ehci_destroy_qh(hc, dev->qh_bulk_out);
    }
    kfree(dev);
    hc->next_addr--; // 回收地址
    return NULL;
}

static int ehci_block_read(struct block_device* dev,uint64_t lba,uint32_t count,void* buffer){
    struct ehci_device *edev = dev->private_data;
    return ehci_submit_rw_request(edev,0,lba,count,buffer);
}

static int ehci_block_write(struct block_device* dev,uint64_t lba,uint32_t count,const void* buffer){
    struct ehci_device *edev = dev->private_data;
    return ehci_submit_rw_request(edev,1,lba,count,(void *)buffer);
}

static void ehci_register_block_device(struct ehci_device *dev)
{
    block_device_t *bdev = kmalloc(sizeof(real_device_t));
    bdev->total_blocks = (uint64_t)dev->block_count;
    bdev->block_size = 512;
    bdev->private_data = dev;
    bdev->type = BLOCK_DISK;

    bdev->read  = ehci_block_read;
    bdev->write = ehci_block_write;

    block_register(bdev,false);
}

void ehci_initial_scan(void) {
    struct ehci_controller *hc;
    list_for_each_entry(hc, &ehci_controllers, ehci_controller_list) {
        /* step 1: initialize the default control qh for address 0 */
        // 分配一页物理内存用于存放默认控制 QH
        void *page = kmalloc(4096);
        if (!page) {
            // 分配失败，跳过此控制器
            continue;
        }
        uint64_t page_phys = (uint64_t)easy_linear2phy(page);
        struct ehci_qh *qh = (struct ehci_qh *)page;
        memset(qh, 0, sizeof(struct ehci_qh));

        // 设置端点特性：设备地址 0，端点 0，高速，硬件管理 toggle，最大包长 8（暂定）
        uint32_t max_packet = 8;
        qh->ep_char = EHCI_EPCHAR_EPS_HIGH | EHCI_EPCHAR_DTC | (max_packet << 16);
        qh->ep_cap = 0;

        // 垂直链接：指向空闲 qTD，确保无工作
        qh->next_qtd = hc->idle_qtd_phy;
        qh->current_qtd = qh->alt_qtd = EHCI_PTR_TERM;
        
        // 水平链接：插入到异步列表 dummy QH 之后
        struct ehci_qh *dummy_qh = hc->async_dummy_qh;

        // 新 QH 指向 dummy 原本指向的下一个
        qh->horiz_link = dummy_qh->horiz_link;
        // dummy 指向新 QH
        dummy_qh->horiz_link = page_phys | EHCI_PTR_TYPE_QH;   // 类型 QH（低两位为 00）

        // 保存到控制器结构
        hc->default_control_qh = qh;
        hc->default_control_qh_phy = page_phys;

        __asm__ __volatile__("mfence");

        /* step 2: enumerate all devices on ports */
        uint32_t op_base = hc->cap_length;  // 操作寄存器偏移
        for (int port = 0; port < hc->port_num; port++) {
            uint32_t portsc = ehci_read32(hc, op_base + EHCI_PORTSC + port * 4);
            if (portsc & EHCI_PORTSC_CCS) {
                __asm__ __volatile__("nop");
                ehci_port_reset(hc, port);

                // 枚举设备
                struct ehci_device *dev = ehci_enumerate_device(hc, port);
                if (dev) {
                    hc->ports[port] = dev;
                    if (dev->is_mass_storage) {
                        ehci_register_block_device(dev);
                    }
                }
            }
        }
    }
}

static int ehci_submit_control_request(struct ehci_controller *hc, struct ehci_device *dev,uint8_t *setup, void *data, int len, int dir) {
    ehci_request_t *req = kmalloc(sizeof(ehci_request_t));
    if (!req) return -1;

    memset(req, 0, sizeof(ehci_request_t));
    req->type = EHCI_REQ_CONTROL;
    req->hc = hc;
    req->dev = dev;
    memcpy(req->control.setup, setup, 8);
    req->control.data = data;
    req->control.data_len = len;
    req->control.dir = dir;
    req->finished = 0;

    // 确定使用的 QH
    if (dev == NULL) {
        // 地址 0 阶段，使用控制器的默认控制 QH
        req->qh = hc->default_control_qh;
    } else {
        // 使用设备的控制端点 QH
        req->qh = dev->qh_control;
    }
    if (!req->qh) {
        kfree(req);
        return -1;
    }

    init_wait_queue(&req->wq);

    spin_lock(&hc->lock);
    spin_lock(&req->wq.lock);
    list_add_tail(&req->list, &hc->req_queue);
    spin_unlock(&hc->lock);

    sleep_on_locked(&req->wq);

    int status = req->status;
    kfree(req);
    return status;
}

static int ehci_submit_scsi_command(struct ehci_device *dev, uint8_t *cdb, int cdb_len,void *data, int data_len, int dir,ehci_request_t **tmp) {
    struct ehci_controller *hc = dev->hc;
    ehci_request_t *req = kmalloc(sizeof(ehci_request_t));
    if (!req) return -1;

    memset(req, 0, sizeof(ehci_request_t));
    req->type = EHCI_REQ_BULK;
    req->dev = dev;
    req->bulk.write = (dir == 0);          // dir: 0=OUT (主机到设备), 1=IN (设备到主机)
    req->bulk.buffer = data;
    req->bulk.count = 0;                    // 不使用块读写，置0
    req->bulk.data_len = (uint32_t)data_len;
    req->bulk.cr3 = get_current()->cr3;     // 当前进程页表，用于地址转换
    req->finished = 0;
    init_wait_queue(&req->wq);

    // 构造 CBW (31 字节)
    uint32_t tag = atomic_inc_return(&hc->next_tag);
    uint8_t *cbw = req->bulk.cbw;
    memset(cbw, 0, 31);
    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43; // 'USBC'
    cbw[4] = tag & 0xFF;
    cbw[5] = (tag >> 8) & 0xFF;
    cbw[6] = (tag >> 16) & 0xFF;
    cbw[7] = (tag >> 24) & 0xFF;
    cbw[8]  = data_len & 0xFF;
    cbw[9]  = (data_len >> 8) & 0xFF;
    cbw[10] = (data_len >> 16) & 0xFF;
    cbw[11] = (data_len >> 24) & 0xFF;
    cbw[12] = dir ? 0x80 : 0x00;   // bmCBWFlags: 1=IN, 0=OUT
    cbw[13] = 0;                    // bCBWLUN
    cbw[14] = cdb_len;              // bCBWCBLength
    memcpy(&cbw[15], cdb, cdb_len); // 复制 CDB

    // CSW 清零
    memset(req->bulk.csw, 0, 13);

    // 将请求加入控制器队列
    spin_lock(&hc->lock);
    spin_lock(&req->wq.lock);
    list_add_tail(&req->list, &hc->req_queue);
    spin_unlock(&hc->lock);

    sleep_on_locked(&req->wq);   // 等待完成

    int status = req->status;
    if (tmp)
        *tmp = req;               // 返回请求指针，由调用者负责释放
    else
        kfree(req);
    return status;
}

static int ehci_submit_rw_request(struct ehci_device *dev, int write, uint64_t lba, uint32_t count, void *buffer) {
    struct ehci_controller *hc = dev->hc;
    ehci_request_t *req = kmalloc(sizeof(ehci_request_t));
    if (!req) return -1;

    memset(req, 0, sizeof(ehci_request_t));
    req->type = EHCI_REQ_BULK;
    req->dev = dev;
    req->bulk.write = write;
    req->bulk.lba = lba;
    req->bulk.count = count;
    req->bulk.buffer = buffer;
    req->bulk.cr3 = get_current()->cr3;
    req->bulk.data_len = 0;
    req->finished = 0;
    init_wait_queue(&req->wq);

    // 构造 CBW
    uint32_t data_len = count * 512;
    uint32_t tag = atomic_inc_return(&hc->next_tag);
    uint8_t *cbw = req->bulk.cbw;
    memset(cbw, 0, 31);

    // 签名 'USBC' (小端)
    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43;

    // dCBWTag
    cbw[4] = tag & 0xFF;
    cbw[5] = (tag >> 8) & 0xFF;
    cbw[6] = (tag >> 16) & 0xFF;
    cbw[7] = (tag >> 24) & 0xFF;

    // dCBWDataTransferLength (小端)
    cbw[8]  = data_len & 0xFF;
    cbw[9]  = (data_len >> 8) & 0xFF;
    cbw[10] = (data_len >> 16) & 0xFF;
    cbw[11] = (data_len >> 24) & 0xFF;

    // bmCBWFlags: 1=IN (设备到主机), 0=OUT (主机到设备)
    cbw[12] = write ? 0x00 : 0x80;

    // bCBWLUN
    cbw[13] = 0;

    // bCBWCBLength = 10
    cbw[14] = 10;

    // CDB 构造
    cbw[15] = write ? 0x2A : 0x28;  // WRITE(10) 或 READ(10)
    cbw[16] = 0;                     // 保留

    // LBA (4字节，大端)
    cbw[17] = (lba >> 24) & 0xFF;
    cbw[18] = (lba >> 16) & 0xFF;
    cbw[19] = (lba >> 8) & 0xFF;
    cbw[20] = lba & 0xFF;

    cbw[21] = 0;                     // 保留

    // 传输长度 (2字节，大端)
    cbw[22] = (count >> 8) & 0xFF;
    cbw[23] = count & 0xFF;

    cbw[24] = 0;                     // 保留
    // cbw[25-30] 已经是 0（由于 memset）

    // 加入队列并等待
    spin_lock(&hc->lock);
    spin_lock(&req->wq.lock);
    list_add_tail(&req->list, &hc->req_queue);
    spin_unlock(&hc->lock);

    sleep_on_locked(&req->wq);

    int status = req->status;
    kfree(req);
    return status;
}

static int ehci_build_request(ehci_request_t *req) {
    if (req->type == EHCI_REQ_CONTROL) {
        struct ehci_device *dev = req->dev;
        struct ehci_controller *hc = req->hc;

        // 获取该请求使用的控制端点 QH
        struct ehci_qh *qh;
        uint32_t qh_phy;
        if (dev) {
            qh = dev->qh_control;                 // 设备已有控制 QH
            qh_phy = dev->qh_control_phy;         // 物理地址
        } else {
            qh = hc->default_control_qh;           // 地址 0 阶段使用默认 QH
            qh_phy = hc->default_control_qh_phy;
        }
        if (!qh) return -1;

        int len = req->control.data_len;

        // 分配一页物理连续内存（4KB）存放 qTD 链
        uint64_t phy_mem = alloc_n_pages_4k(1);
        if (!phy_mem) return -1;
        uint64_t vir_mem = (uint64_t)easy_phy2linear(phy_mem);
        memset((void *)vir_mem, 0, 4096);

        // qTD 链布局：setup, data (if any), status
        struct ehci_qtd *qtd_setup = (struct ehci_qtd *)vir_mem;
        struct ehci_qtd *qtd_data = NULL;
        struct ehci_qtd *qtd_status;

        int offset = 32;  // 下一个 qTD 的偏移（32 字节对齐）
        if (len > 0) {
            qtd_data = (struct ehci_qtd *)(vir_mem + offset);
            offset += 32;
        }
        qtd_status = (struct ehci_qtd *)(vir_mem + offset);

        // ---- 1. 设置阶段 qTD ----
        memset(qtd_setup, 0, sizeof(struct ehci_qtd));
        // 令牌：长度 8，IOC=0，toggle=0，PID=SETUP，错误计数=3
        qtd_setup->token = EHCI_BUILD_TOKEN(8, 0, 0, EHCI_PID_SETUP, 3);
        // 缓冲区：setup 包在 request 结构中的物理地址
        uint32_t setup_phy = (uint32_t)mem_linear2phy_get((uint64_t)req->control.setup, (uint64_t)vir_ptable4);
        qtd_setup->buffer[0] = setup_phy;
        // 其余 buffer 保持 0
        // 链接：指向下一个 qTD（若有数据则指向 data，否则指向 status）
        qtd_setup->next_qtd = (len > 0) ? (uint32_t)(phy_mem + 32) : (uint32_t)(phy_mem + offset);

        // ---- 2. 数据阶段 qTD（如果有） ----
        if (len > 0) {
            memset(qtd_data, 0, sizeof(struct ehci_qtd));
            uint8_t pid = req->control.dir ? EHCI_PID_IN : EHCI_PID_OUT;
            qtd_data->token = EHCI_BUILD_TOKEN(len, 0, 1, pid, 3); // toggle=1
            uint32_t data_phy = (uint32_t)mem_linear2phy_get((uint64_t)req->control.data, (uint64_t)vir_ptable4);
            qtd_data->buffer[0] = data_phy;
            qtd_data->next_qtd = (uint32_t)(phy_mem + offset); // 指向 status
        }

        // ---- 3. 状态阶段 qTD ----
        memset(qtd_status, 0, sizeof(struct ehci_qtd));
        uint8_t status_pid;
        if (len == 0) {
            // 无数据阶段，状态为 IN
            status_pid = EHCI_PID_IN;
        } else {
            // 状态阶段方向与数据阶段相反
            status_pid = req->control.dir ? EHCI_PID_OUT : EHCI_PID_IN;
        }
        qtd_status->token = EHCI_BUILD_TOKEN(0, 1, 1, status_pid, 3); // IOC=1
        qtd_status->buffer[0] = 0;
        qtd_status->next_qtd = EHCI_PTR_TERM;   // 终止

        // ---- 记录请求信息 ----
        req->qtd_head = qtd_setup;          // 虚拟地址
        req->qtd_last = qtd_status;         // 虚拟地址
        req->page_num = 1;                  // 占用一页
        req->qh = qh;
        req->qh_phy = qh_phy;

        return 0;
    }
    else if (req->type == EHCI_REQ_BULK) {
        if (!req->dev) return -1;
        ehci_device_t *dev = (ehci_device_t*)req->dev;

        // 计算数据长度
        int data_len;
        if (req->bulk.count) {
            data_len = req->bulk.count * 512;          // 块读写
        } else {
            data_len = (int)req->bulk.data_len;        // SCSI 命令
        }
        int dir = req->bulk.write ? 0 : 1;              // 0=OUT (主机到设备), 1=IN (设备到主机)
        int max_packet = dev->max_packet_size;
        int remaining = data_len;

        // 计算所需数据包数量及总 qTD 数
        int data_packet_count = (remaining + max_packet - 1) / max_packet;
        int total_qtd = 1 + data_packet_count + 1;      // CBW + DATA + CSW
        int total_size = total_qtd * 32;                 // 每个 qTD 32 字节
        req->page_num = (total_size + 4095) / 4096;      // 所需页数

        // 分配物理连续页，并获取虚拟地址
        uint64_t phy_mem = alloc_n_pages_4k(req->page_num);
        if (!phy_mem) return -1;
        uint64_t vir_mem = (uint64_t)easy_phy2linear(phy_mem);
        memset((void*)vir_mem, 0, req->page_num << 12);

        int offset = 0;  // 当前 qTD 在内存中的偏移（字节）

        // ---------- 1. CBW 阶段 ----------
        struct ehci_qtd *qtd_cbw = (struct ehci_qtd*)(vir_mem + offset);
        memset(qtd_cbw, 0, sizeof(struct ehci_qtd));
        qtd_cbw->next_qtd = qtd_cbw->alt_next_qtd = EHCI_PTR_TERM;
        qtd_cbw->token = EHCI_BUILD_TOKEN(31, 0, 0, EHCI_PID_OUT, 3); // 长度31, 无IOC, toggle=0, OUT
        qtd_cbw->buffer[0] = (uint32_t)mem_linear2phy_get((uint64_t)req->bulk.cbw, (uint64_t)vir_ptable4);
        req->bulk.phase_info[0].head = qtd_cbw;
        req->bulk.phase_info[0].last = qtd_cbw;
        req->bulk.phase_info[0].qh = dev->qh_bulk_out;              // CBW 使用 bulk out 端点
        req->bulk.phase_info[0].qh_phy = dev->qh_bulk_out_phy;
        offset += 32;

        // ---------- 2. DATA 阶段（可能多个 qTD） ----------
        bool toggle = true;                                         // 数据阶段从 toggle 1 开始
        uint8_t pid = dir ? EHCI_PID_IN : EHCI_PID_OUT;
        uint64_t data_va = (uint64_t)req->bulk.buffer;
        uint32_t data_remaining = data_len;

        struct ehci_qtd *first_data = NULL;
        struct ehci_qtd *last_data = NULL;
        for (int i = 0; i < data_packet_count; i++) {
            int chunk = ((int)data_remaining > max_packet) ? max_packet : (int)data_remaining;
            struct ehci_qtd *qtd_data = (struct ehci_qtd*)(vir_mem + offset);
            memset(qtd_data, 0, sizeof(struct ehci_qtd));
            qtd_data->token = EHCI_BUILD_TOKEN(chunk, 0, toggle, pid, 3); // 无 IOC
            // 当前数据块的物理地址（假设缓冲区在一个物理页内，若跨页需扩展 buffer[1..4]）
            uint64_t data_pa = mem_linear2phy_get(data_va, req->bulk.cr3);
            qtd_data->buffer[0] = (uint32_t)data_pa;
            qtd_data->next_qtd = qtd_data->alt_next_qtd = EHCI_PTR_TERM;                    // 先设终止，后续链接

            if (!first_data) first_data = qtd_data;
            else last_data->next_qtd = (uint32_t)(phy_mem + offset); // 上一块指向当前块

            last_data = qtd_data;
            data_remaining -= chunk;
            data_va += chunk;
            toggle = !toggle;                                       // 翻转 toggle
            offset += 32;
        }
        req->bulk.phase_info[1].head = first_data;
        req->bulk.phase_info[1].last = last_data;
        req->bulk.phase_info[1].qh = dir ? dev->qh_bulk_in : dev->qh_bulk_out;
        req->bulk.phase_info[1].qh_phy = dir ? dev->qh_bulk_in_phy : dev->qh_bulk_out_phy;

        // ---------- 3. CSW 阶段 ----------
        struct ehci_qtd *qtd_csw = (struct ehci_qtd*)(vir_mem + offset);
        memset(qtd_csw, 0, sizeof(struct ehci_qtd));
        qtd_csw->next_qtd = qtd_csw->alt_next_qtd = EHCI_PTR_TERM;
        qtd_csw->token = EHCI_BUILD_TOKEN(13, 1, toggle, EHCI_PID_IN, 3); // IOC=1，使用当前 toggle
        qtd_csw->buffer[0] = (uint32_t)mem_linear2phy_get((uint64_t)req->bulk.csw, (uint64_t)vir_ptable4);
        req->bulk.phase_info[2].head = qtd_csw;
        req->bulk.phase_info[2].last = qtd_csw;
        req->bulk.phase_info[2].qh = dev->qh_bulk_in;               // CSW 总是使用 bulk in 端点
        req->bulk.phase_info[2].qh_phy = dev->qh_bulk_in_phy;
        offset += 32;  // 未使用

        // 初始化阶段为 0 (CBW)
        req->bulk.phase = 0;
        req->qtd_head = qtd_cbw;
        return 0;
    }
    return -1;
}

void ehci_kernel_thread(void) {
    while (1) {
        struct ehci_controller *hc;
        list_for_each_entry(hc, &ehci_controllers, ehci_controller_list) {
            spin_lock(&hc->lock);
            if (hc->active_req) {
                ehci_request_t *req = hc->active_req;
                int complete = 0;

                if (req->type == EHCI_REQ_CONTROL) {
                    // 控制传输：检查最后一个 qTD
                    struct ehci_qtd *last = req->qtd_last;
                    if (!(last->token & EHCI_QTD_ACTIVE)) {
                        if (last->token & (EHCI_QTD_HALTED | EHCI_QTD_DATABUFFERERR | EHCI_QTD_BABBLE | EHCI_QTD_XACTERR))
                            req->status = -1;
                        else
                            req->status = 0;
                        complete = 1;
                    }
                } else { // EHCI_REQ_BULK
                    int phase = req->bulk.phase;
                    if (phase < 3) {
                        struct ehci_qtd *last = req->bulk.phase_info[phase].last;
                        if (!(last->token & EHCI_QTD_ACTIVE)) {
                            // 当前阶段完成
                            if (last->token & (EHCI_QTD_HALTED | EHCI_QTD_DATABUFFERERR | EHCI_QTD_BABBLE | EHCI_QTD_XACTERR)) {
                                req->status = -1;
                                complete = 1; // 错误，终止整个请求
                            } else {
                                // 清除当前阶段的 QH
                                struct ehci_qh *qh = req->bulk.phase_info[phase].qh;
                                if (qh) {
                                    qh->next_qtd = hc->idle_qtd_phy;
                                    __asm__ __volatile__("mfence");
                                }
                                phase++;
                                if (phase < 3) {
                                    // 启动下一阶段
                                    struct ehci_qtd *next_head = req->bulk.phase_info[phase].head;
                                    uint32_t next_head_phy = (uint32_t)mem_linear2phy_get((uint64_t)next_head, (uint64_t)vir_ptable4);
                                    struct ehci_qh *next_qh = req->bulk.phase_info[phase].qh;
                                    if (next_qh) {
                                        next_qh->next_qtd = next_head_phy;
                                        __asm__ __volatile__("mfence");
                                        req->bulk.phase = phase;
                                    } else {
                                        req->status = -1;
                                        complete = 1;
                                    }
                                } else {
                                    // 所有阶段完成
                                    req->status = 0;
                                    complete = 1;
                                }
                            }
                        }
                    }
                }

                if (complete) {
                    req->finished = 1;
                    hc->active_req = NULL;

                    // 清理所有用过的 QH（使其指向空闲 qTD）
                    if (req->type == EHCI_REQ_CONTROL) {
                        if (req->qh)
                            req->qh->next_qtd = hc->idle_qtd_phy;
                    } else {
                        for (int i = 0; i < 3; i++) {
                            if (req->bulk.phase_info[i].qh)
                                req->bulk.phase_info[i].qh->next_qtd = hc->idle_qtd_phy;
                        }
                    }
                    __asm__ __volatile__("mfence");

                    // 释放请求占用的内存
                    free_n_pages_4k(req->page_num, (uint64_t)easy_linear2phy(req->qtd_head));
                    wake_up_all(&req->wq);
                }
            }

            // 如果没有活动请求，从队列中取出下一个
            if (!hc->active_req && !list_empty(&hc->req_queue)) {
                list_head_t *first = hc->req_queue.next;
                list_del_init(first);
                ehci_request_t *req = container_of(first, ehci_request_t, list);

                if (ehci_build_request(req) == 0) {
                    if (req->type == EHCI_REQ_CONTROL) {
                        uint32_t qtd_head_phy = (uint32_t)mem_linear2phy_get((uint64_t)req->qtd_head, (uint64_t)vir_ptable4);
                        if (req->qh) {
                            req->qh->next_qtd = qtd_head_phy;
                            __asm__ __volatile__("mfence");
                            hc->active_req = req;
                        } else {
                            goto fail;
                        }
                    } else { // 批量
                        // 启动第 0 阶段（CBW）
                        if (req->bulk.phase_info[0].qh && req->bulk.phase_info[0].head) {
                            uint32_t head_phy = (uint32_t)mem_linear2phy_get((uint64_t)req->bulk.phase_info[0].head, (uint64_t)vir_ptable4);
                            req->bulk.phase_info[0].qh->next_qtd = head_phy;
                            __asm__ __volatile__("mfence");
                            hc->active_req = req;
                        } else {
                            goto fail;
                        }
                    }
                } else {
                fail:
                    // 构建失败或缺少 QH
                    free_n_pages_4k(req->page_num, (uint64_t)easy_linear2phy(req->qtd_head));
                    req->finished = 1;
                    req->status = -1;
                    wake_up_all(&req->wq);
                }
            }
            spin_unlock(&hc->lock);
        }
        sys_yield();
    }
}

#include "machine/usb/uhci.h"
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

static list_head_t uhci_controllers = LIST_HEAD_INIT(uhci_controllers);

static int uhci_submit_control_request(struct uhci_controller *hc, uint8_t dev_addr,uint8_t *setup, void *data, int len, int dir);
static int uhci_submit_scsi_command(struct usb_device *dev, uint8_t *cdb, int cdb_len,void *data, int data_len, int dir,uhci_request_t **tmp);
static int uhci_submit_rw_request(struct usb_device *dev, int write, uint64_t lba, uint32_t count, void *buffer);

static int uhci_port_has_device(struct uhci_controller *hc, int port) {
    if (port < 0 || port >= hc->port_num)
        return -1;

    uint16_t portsc = io_inword(hc->io_base + UHCI_PORTSC1 + (port * 2));
    return (portsc & PORT_CCS) ? 1 : 0;
}

static struct uhci_controller *uhci_alloc_controller(uint8_t bus, uint8_t dev, uint8_t func) {
    struct uhci_controller *hc = kmalloc(sizeof(struct uhci_controller));
    if (!hc) return NULL;
    memset(hc, 0, sizeof(*hc));
    hc->bus = bus;
    hc->dev = dev;
    hc->func = func;
    hc->active_req = NULL;
    hc->next_addr = 1;
    INIT_LIST_HEAD(&hc->uhci_controller_list);
    INIT_LIST_HEAD(&hc->req_queue);
    atomic_set(&hc->next_tag,0);
    uintptr_t addr = MAKE_PCIE_ADDR(bus, dev, func, 0);
    uint32_t bar = read_pcie(MAKE_PCIE_ADDR(bus, dev, func, BAR4));
    if (!(bar & 1)) {
        kfree(hc);
        return NULL;
    }
    hc->io_base = (uint16_t)(bar & ~3);
    // 启用总线主控
    uint16_t cmd = read_pcie(MAKE_PCIE_ADDR_1(addr, 0, STATUS_COMMAND));
    cmd |= PCI_CMD_BUS_MASTER;
    write_pcie(MAKE_PCIE_ADDR_1(addr, 0, STATUS_COMMAND), cmd);
    return hc;
}

static void uhci_port_init(uint16_t io_base, int port)
{
    uint16_t v;

    v = uhci_port_read(io_base, port);
    if (!(v & PORT_CCS))
        return;

    uhci_port_write(io_base, port, v | PORT_PR);
    mdelay(50);

    v = uhci_port_read(io_base, port);
    uhci_port_write(io_base, port, v & ~PORT_PR);
    mdelay(10);

    v = uhci_port_read(io_base, port);
    uhci_port_write(io_base, port, v | PORT_PED);

    v = uhci_port_read(io_base, port);
    uhci_port_write(io_base, port, v | PORT_CSC | PORT_PEC);
}

static int uhci_hw_init(struct uhci_controller *hc) {
    uint16_t io = hc->io_base;
    int timeout;

    // 复位控制器
    io_outword(io + UHCI_USBCMD, CMD_HCRESET);
    
    // 等待复位完成
    timeout = 10000;
    while ((io_inword(io + UHCI_USBCMD) & CMD_HCRESET) && timeout--) {
        for (int i = 0; i < 100; i++) io_delay();
    }
    if (timeout <= 0) {
        return -1;
    }
    
    // 分配帧列表
    hc->frame_list = kmalloc(4096);
    if (!hc->frame_list) return -1;

    // === 新增：分配全局 QH 和空闲 TD ===
    hc->global_qh = kmalloc(sizeof(struct uhci_qh));      // 16字节对齐由kmalloc保证
    if (!hc->global_qh) {
        kfree(hc->frame_list);
        return -1;
    }
    hc->idle_td = kmalloc(sizeof(struct uhci_td));
    if (!hc->idle_td) {
        kfree(hc->global_qh);
        kfree(hc->frame_list);
        return -1;
    }

    // 初始化空闲 TD（不活动）
    memset(hc->idle_td, 0, sizeof(struct uhci_td));
    hc->idle_td->link = UHCI_PTR_TERM;               // 终止
    // ctrl_status 为 0，Active 位为 0，不会执行传输

    // 初始化全局 QH
    memset(hc->global_qh, 0, sizeof(struct uhci_qh));
    hc->global_qh->horiz_link = UHCI_PTR_TERM;       // 水平链表终止
    hc->idle_td_phy = hc->global_qh->vert_link   = (uint32_t)mem_linear2phy_get((uint64_t)hc->idle_td,(uint64_t)vir_ptable4); // 指向空闲 TD（不加 Q 位）

    // 将所有帧列表项指向 global_qh（带 Q 位）
    uint32_t qh_phys = (uint32_t)mem_linear2phy_get((uint64_t)hc->global_qh,(uint64_t)vir_ptable4) | UHCI_PTR_QH;
    for (int i = 0; i < UHCI_FRAMES; i++) {
        hc->frame_list->link[i] = qh_phys;
    }
    // === 新增结束 ===

    // 设置帧列表基址
    io_outdword(io + UHCI_FLBASEADD, (uint32_t)(uintptr_t)easy_linear2phy(hc->frame_list));

    io_outword(io + UHCI_USBCMD, CMD_MAX64 | CMD_CF | CMD_RUN);
    
    // 等待运行
    timeout = 10000;
    while ((io_inword(io + UHCI_USBSTS) & STS_HCHALTED) && timeout--) {
        for (int i = 0; i < 100; i++) io_delay();
    }
    if (timeout <= 0) {
        // 失败时释放新增资源
        kfree(hc->idle_td);
        kfree(hc->global_qh);
        kfree(hc->frame_list);
        return -1;
    }

    // 初始化端口设备指针为 NULL
    hc->ports[0] = hc->ports[1] = NULL;
    hc->port_num = 2;

    return 0;
}

static void uhci_destroy_request(uhci_request_t *req) {
    free_n_pages_4k(req->page_num,(uint64_t)easy_linear2phy(req->td_head));
}

static struct usb_device *uhci_enumerate_device(struct uhci_controller *hc, int port) {
    uint8_t setup[8];
    uint8_t buf[64]; // 临时缓冲区
    int ret;
    struct usb_device *dev = NULL;
    uint8_t dev_addr = 0; // 初始地址为0

    // 1. 确保端口已使能
    uhci_port_init(hc->io_base,port);

    // 2. 获取设备描述符前8字节（得到最大包长）
    setup[0] = 0x80; // 方向 IN，设备到主机
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0;                  // 高字节
    setup[3] = USB_DESC_DEVICE;    // 低字节
    setup[4] = 0;                  // 语言ID，0
    setup[5] = 0;
    setup[6] = 8;                  // 长度低字节
    setup[7] = 0;                  // 长度高字节

    ret = uhci_submit_control_request(hc, dev_addr, setup, buf, 8, 1); // dir=1 IN
    if (ret < 0) {
        return NULL;
    }
    uint8_t max_packet_size = buf[7]; // bMaxPacketSize0

    // 3. 分配设备结构
    dev = kmalloc(sizeof(struct usb_device));
    if (!dev) return NULL;
    memset(dev, 0, sizeof(*dev));
    dev->hc = hc;
    dev->port = port;
    dev->max_packet_size = max_packet_size;
    dev->speed = 0; // 全速（简化）

    // 4. 分配新地址
    dev->address = hc->next_addr++;
    setup[0] = 0x00; // 方向 OUT，主机到设备
    setup[1] = USB_REQ_SET_ADDRESS;
    setup[2] = dev->address;
    setup[3] = 0;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 0;
    setup[7] = 0;
    ret = uhci_submit_control_request(hc, dev_addr, setup, NULL, 0, 0); // dir=0 OUT
    if (ret < 0) {
        kfree(dev);
        return NULL;
    }
    // 等待地址生效（USB规范要求至少2ms）
    mdelay(2);
    dev_addr = dev->address; // 后续使用新地址

    // 5. 获取完整设备描述符（18字节）
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0;
    setup[3] = USB_DESC_DEVICE;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 18;
    setup[7] = 0;
    ret = uhci_submit_control_request(hc, dev_addr, setup, buf, 18, 1);
    if (ret < 0) {
        kfree(dev);
        return NULL;
    }
    // 可以解析设备描述符，但暂时不需要额外信息

    // 6. 获取配置描述符总长度
    setup[0] = 0x80;
    setup[1] = USB_REQ_GET_DESCRIPTOR;
    setup[2] = 0;
    setup[3] = USB_DESC_CONFIGURATION;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 4;  // 先读4字节得到总长度
    setup[7] = 0;
    ret = uhci_submit_control_request(hc, dev_addr, setup, buf, 4, 1);
    if (ret < 0) {
        kfree(dev);
        return NULL;
    }
    int total_len = buf[2] | (buf[3] << 8);

    // 7. 读取完整配置描述符
    uint8_t *conf_buf = kmalloc(total_len);
    if (!conf_buf) {
        kfree(dev);
        return NULL;
    }
    setup[6] = total_len & 0xFF;
    setup[7] = total_len >> 8;
    ret = uhci_submit_control_request(hc, dev_addr, setup, conf_buf, total_len, 1);
    if (ret < 0) {
        kfree(conf_buf);
        kfree(dev);
        return NULL;
    }

    // 8. 解析配置描述符，查找 Mass Storage 接口（类0x08，子类0x06，协议0x50）
    int pos = 0;
    uint8_t config_value = 0;
    while (pos < total_len) {
        uint8_t len = conf_buf[pos];
        uint8_t type = conf_buf[pos+1];
        if (type == 4) { // 接口描述符
            usb_interface_descriptor_t *intf = (usb_interface_descriptor_t*)&conf_buf[pos];
            if (intf->bInterfaceClass == 0x08 &&
                intf->bInterfaceSubClass == 0x06 &&
                intf->bInterfaceProtocol == 0x50) {
                // 找到 Mass Storage 接口
                dev->is_mass_storage = 1;
                // 接下来遍历该接口的端点描述符
                int ep_pos = pos + len;
                while (ep_pos < total_len && conf_buf[ep_pos] != 0) {
                    uint8_t ep_len = conf_buf[ep_pos];
                    uint8_t ep_type = conf_buf[ep_pos+1];
                    if (ep_type == 5) { // 端点描述符
                        usb_endpoint_descriptor_t *ep = (usb_endpoint_descriptor_t*)&conf_buf[ep_pos];
                        if ((ep->bmAttributes & 0x03) == 2) { // 批量端点
                            if (ep->bEndpointAddress & 0x80) {
                                dev->bulk_in_ep = ep->bEndpointAddress;
                            } else {
                                dev->bulk_out_ep = ep->bEndpointAddress;
                            }
                            dev->max_packet_size = ep->wMaxPacketSize;
                        }
                    }
                    ep_pos += ep_len;
                }
                // 记录配置值
                // 配置描述符在接口之前，但我们可以从整个缓冲区中获取配置值
                // 通常配置描述符在开头
                usb_config_descriptor_t *cfg = (usb_config_descriptor_t*)conf_buf;
                config_value = cfg->bConfigurationValue;
                break;
            }
        }
        pos += len;
    }
    kfree(conf_buf);

    if (!dev->is_mass_storage) {
        // 不是 U 盘，释放设备返回 NULL
        kfree(dev);
        return NULL;
    }

    // 9. 设置配置
    setup[0] = 0x00;
    setup[1] = USB_REQ_SET_CONFIGURATION;
    setup[2] = config_value;
    setup[3] = 0;
    setup[4] = 0;
    setup[5] = 0;
    setup[6] = 0;
    setup[7] = 0;
    ret = uhci_submit_control_request(hc, dev_addr, setup, NULL, 0, 0);
    if (ret < 0) {
        kfree(dev);
        return NULL;
    }

    // 10. 读取大小
    uint8_t cdb10[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t capacity_data[8];  // 返回 8 字节数据

    ret = uhci_submit_scsi_command(dev, cdb10, 10, capacity_data, 8, 1, NULL);
    if (ret < 0) {
        kfree(dev);
        return NULL;
    }

    // 解析数据（SCSI 多字节字段使用大端序）
    uint32_t last_lba = ((uint32_t)capacity_data[0] << 24) |
                        ((uint32_t)capacity_data[1] << 16) |
                        ((uint32_t)capacity_data[2] << 8)  |
                        capacity_data[3];
    uint32_t block_size = ((uint32_t)capacity_data[4] << 24) |
                          ((uint32_t)capacity_data[5] << 16) |
                          ((uint32_t)capacity_data[6] << 8)  |
                          capacity_data[7];

    dev->block_count = (uint64_t)last_lba + 1;  // 总块数 = 最后 LBA + 1
    dev->block_size = block_size;

    if (block_size != 512){
        kfree(dev);
        return NULL;
    }

    return dev;
}

int init_uhci_controller(uint8_t bus, uint8_t dev, uint8_t func) {
    struct uhci_controller *hc = uhci_alloc_controller(bus, dev, func);
    if (!hc) return -1;

    if (uhci_hw_init(hc) != 0) {
        kfree(hc);
        return -1;
    }

    list_add_tail(&uhci_controllers, &hc->uhci_controller_list);
    return 0;
}

static int uhci_block_read(struct block_device* dev,uint64_t lba,uint32_t count,void* buffer){
    struct usb_device *udev = dev->private_data;
    return uhci_submit_rw_request(udev,0,lba,count,buffer);
}

static int uhci_block_write(struct block_device* dev,uint64_t lba,uint32_t count,const void* buffer){
    struct usb_device *udev = dev->private_data;
    return uhci_submit_rw_request(udev,1,lba,count,(void *)buffer);
}

static void uhci_register_block_device(struct usb_device *udev)
{
    block_device_t *bdev = kmalloc(sizeof(real_device_t));
    bdev->total_blocks = (uint64_t)udev->block_count;
    bdev->block_size = 512;
    bdev->private_data = udev;
    bdev->type = BLOCK_DISK;

    bdev->read  = uhci_block_read;
    bdev->write = uhci_block_write;

    block_register(bdev,false);
}

void uhci_initial_scan(void) {
    struct uhci_controller *hc;
    list_for_each_entry(hc, &uhci_controllers, uhci_controller_list) {
        for (int port = 0; port < hc->port_num; port++) {
            if (uhci_port_has_device(hc, port)) {
                // 枚举该端口上的设备
                struct usb_device *dev = uhci_enumerate_device(hc, port);
                if (dev) {
                    hc->ports[port] = dev;
                    // 如果设备是 Mass Storage，可以进一步调用 block 层注册
                    if (dev->is_mass_storage) {
                        uhci_register_block_device(dev);
                    }
                }
            }
        }
    }
}

static int uhci_submit_control_request(struct uhci_controller *hc, uint8_t dev_addr,uint8_t *setup, void *data, int len, int dir) {
    uhci_request_t *req = kmalloc(sizeof(uhci_request_t));
    if (!req) return -1;

    memset(req, 0, sizeof(uhci_request_t));
    req->type = UHCI_REQ_CONTROL;
    req->control.dev_addr = dev_addr;
    memcpy(req->control.setup, setup, 8);
    req->control.data = data;
    req->control.data_len = len;
    req->control.dir = dir;
    req->finished = 0;
    req->dev = NULL;
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

static int uhci_submit_rw_request(struct usb_device *dev, int write, uint64_t lba, uint32_t count, void *buffer) {
    struct uhci_controller *hc = dev->hc;
    uhci_request_t *req = kmalloc(sizeof(uhci_request_t));
    if (!req) return -1;

    memset(req, 0, sizeof(uhci_request_t));
    req->type = UHCI_REQ_BULK;
    req->dev = dev;
    req->bulk.write = write;
    req->bulk.lba = lba;
    req->bulk.count = count;
    req->bulk.buffer = buffer;
    req->bulk.cr3 = get_current()->cr3;          // 保存当前进程的页表基址
    req->bulk.data_len = 0;
    req->finished = 0;

    init_wait_queue(&req->wq);

    // ----- 构造 CBW (31 字节) -----
    uint32_t data_len = count * 512;              // 总数据字节数
    uint32_t tag = atomic_inc_return(&hc->next_tag);

    uint8_t *cbw = req->bulk.cbw;                  // 指向 CBW 缓冲区
    memset(cbw, 0, 31);

    // dCBWSignature: 'USBC' = 0x43425355 (小端)
    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43;

    // dCBWTag
    cbw[4] = tag & 0xFF;
    cbw[5] = (tag >> 8) & 0xFF;
    cbw[6] = (tag >> 16) & 0xFF;
    cbw[7] = (tag >> 24) & 0xFF;

    // dCBWDataTransferLength
    cbw[8]  = data_len & 0xFF;
    cbw[9]  = (data_len >> 8) & 0xFF;
    cbw[10] = (data_len >> 16) & 0xFF;
    cbw[11] = (data_len >> 24) & 0xFF;

    // bmCBWFlags
    cbw[12] = write ? 0x00 : 0x80;

    // bCBWLUN
    cbw[13] = 0;

    // bCBWCBLength: 10
    cbw[14] = 10;

    // 操作码
    cbw[15] = write ? 0x2A : 0x28;
    cbw[16] = 0;

    // LBA (4字节，大端)
    cbw[17] = (lba >> 24) & 0xFF;
    cbw[18] = (lba >> 16) & 0xFF;
    cbw[19] = (lba >> 8) & 0xFF;
    cbw[20] = lba & 0xFF;

    // 保留
    cbw[21] = 0;

    // 传输长度 (2字节，大端)
    cbw[22] = (count >> 8) & 0xFF;
    cbw[23] = count & 0xFF;

    cbw[24] = 0;

    // 剩余字节 (cbw[28]~cbw[30]) 为保留/控制，已初始化为 0

    // ----- 将请求加入控制器队列并等待完成 -----
    spin_lock(&hc->lock);
    spin_lock(&req->wq.lock);
    list_add_tail(&req->list, &hc->req_queue);
    spin_unlock(&hc->lock);

    sleep_on_locked(&req->wq);      // 等待请求完成

    int status = req->status;
    kfree(req);
    return status;
}

static int uhci_submit_scsi_command(struct usb_device *dev, uint8_t *cdb, int cdb_len,void *data, int data_len, int dir,uhci_request_t **tmp)  // dir: 1=IN, 0=OUT
{
    struct uhci_controller *hc = dev->hc;
    uhci_request_t *req = kmalloc(sizeof(uhci_request_t));
    if (!req) return -1;

    memset(req, 0, sizeof(uhci_request_t));
    req->type = UHCI_REQ_BULK;           // 仍使用批量传输类型
    req->dev = dev;
    req->bulk.write = (dir == 0);        // 若dir=0，表示主机发送数据（写设备）；对于SCSI命令，数据方向由命令决定
    req->bulk.buffer = data;              // 数据缓冲区（可能为NULL）
    req->bulk.count = 0;                   // 不使用LBA和块数，这里保留但不用
    req->bulk.data_len = (uint32_t)data_len;
    req->bulk.cr3 = get_current()->cr3;    // 用于地址转换
    req->finished = 0;
    init_wait_queue(&req->wq);

    // 构造CBW
    uint32_t tag = atomic_inc_return(&hc->next_tag);

    uint8_t *cbw = req->bulk.cbw;
    memset(cbw, 0, 31);
    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43;  // 'USBC'
    // dCBWTag
    cbw[4] = tag & 0xFF;
    cbw[5] = (tag >> 8) & 0xFF;
    cbw[6] = (tag >> 16) & 0xFF;
    cbw[7] = (tag >> 24) & 0xFF;
    // dCBWDataTransferLength
    cbw[8]  = data_len & 0xFF;
    cbw[9]  = (data_len >> 8) & 0xFF;
    cbw[10] = (data_len >> 16) & 0xFF;
    cbw[11] = (data_len >> 24) & 0xFF;
    // bmCBWFlags: dir: 1=设备到主机(IN), 0=主机到设备(OUT)
    cbw[12] = dir ? 0x80 : 0x00;
    // bCBWLUN
    cbw[13] = 0;
    // bCBWCBLength
    cbw[14] = cdb_len;
    // CBWCB: 复制CDB
    memcpy(&cbw[15], cdb, cdb_len);

    // CSW 清零
    memset(req->bulk.csw, 0, 13);

    // 将请求加入队列
    spin_lock(&hc->lock);
    spin_lock(&req->wq.lock);
    list_add_tail(&req->list, &hc->req_queue);
    spin_unlock(&hc->lock);

    sleep_on_locked(&req->wq);   // 等待完成

    int status = req->status;
    if (tmp)
        *tmp = req;
    else
        kfree(req);
    return status;
}

static int uhci_build_request(uhci_request_t *req) {
    if (req->type == UHCI_REQ_CONTROL) {
        // 构建控制传输 TD 链
        struct usb_device *dev = req->dev; // 可能为 NULL（地址0）
        uint8_t dev_addr = dev ? dev->address : req->control.dev_addr;
        struct uhci_td *td_setup, *td_data = NULL, *td_status;
        int len = req->control.data_len;
        uint32_t phy_req_control_trunc = (uint32_t)mem_linear2phy_get((uint64_t)&req->control,(uint64_t)vir_ptable4);


        td_setup = kmalloc(4096);
        if (!td_setup) return -1;
            
        td_status = (void *)((uintptr_t)td_setup + 32);
        if (len > 0) {
            td_data = (void *)((uintptr_t)td_setup + 64);
        }

        // 设置阶段 TD
        memset(td_setup, 0, sizeof(*td_setup));
        td_setup->ctrl_status = TD_CTL_NORMAL_CTRL_USE;
        td_setup->buffer = phy_req_control_trunc + offsetof(uhci_request_t, control.setup) - offsetof(uhci_request_t, control);
        td_setup->packed_header = TD_PH(8,0,0,TD_PH_PID_SETUP,dev_addr);

        // 数据阶段 TD（如果有）
        if (len > 0) {
            memset(td_data, 0, sizeof(*td_data));
            td_data->link = UHCI_PTR_TERM;
            uint8_t pid = req->control.dir ? 0x69 : 0xE1; // IN or OUT
            td_data->ctrl_status = TD_CTL_NORMAL_CTRL_USE;
            td_data->packed_header = TD_PH(len + 1,1,0,pid,dev_addr);
            td_data->buffer = (uint32_t)mem_linear2phy_get((uint64_t)req->control.data,(uint64_t)vir_ptable4);
            td_setup->link = (uint32_t)(uintptr_t)easy_linear2phy(td_data);
        }

        // 状态阶段 TD
        memset(td_status, 0, sizeof(*td_status));
        td_status->link = UHCI_PTR_TERM;
        uint8_t status_pid;
        if (len == 0) {
            status_pid = 0x69; // IN
        } else {
            status_pid = req->control.dir ? 0xE1 : 0x69;
        }
        td_status->ctrl_status = TD_CTL_NORMAL_CTRL_USE;
        td_status->buffer = 0;
        td_status->packed_header = TD_PH(0,1,0,status_pid,dev_addr);

        if (td_data) {
            td_data->link = (uint32_t)(uintptr_t)easy_linear2phy(td_status);
        } else {
            td_setup->link = (uint32_t)(uintptr_t)easy_linear2phy(td_status);
        }

        req->td_head = td_setup;
        req->last_td = td_status;
        req->page_num = 1;
        return 0;

    } else if (req->type == UHCI_REQ_BULK) {
        if (!req->dev)
            return -1;
        struct usb_device *dev = req->dev;
        int data_len;
        if (req->bulk.count){
            data_len = req->bulk.count * 512;
        }else{
            data_len = (int)req->bulk.data_len;
        }
        int dir = req->bulk.write ? 0 : 1;
        int max_packet = dev->max_packet_size;
        int remaining = data_len;
        struct uhci_td *td_cbw, *td_csw, *prev_td = NULL;
        uint8_t dev_addr = dev->address;

        int td_count = (remaining + max_packet - 1) / max_packet; // 需要的数据 TD 数量
        req->page_num = (td_count + 2 + NUM_OF_TD_IN_4K_PAGE - 1) / NUM_OF_TD_IN_4K_PAGE;

        uint64_t phy_mem = alloc_n_pages_4k(req->page_num);
        if (!phy_mem)
            return -1;
        uint64_t vir_mem = (uint64_t)easy_phy2linear(phy_mem);
        memset((void *)vir_mem, 0, req->page_num << 12);

        prev_td = td_cbw = (void *)vir_mem;
        //td_cbw->link
        td_cbw->ctrl_status = TD_CTL_NORMAL_BULK_USE;
        td_cbw->packed_header = TD_PH(31,0,dev->bulk_out_ep,0xE1,dev_addr);
        td_cbw->buffer = (uint32_t)mem_linear2phy_get((uint64_t)req->bulk.cbw,(uint64_t)vir_ptable4);

        bool toggle = true;
        
        uint8_t pid = dir ? 0x69 : 0xE1;        // IN or OUT
        uint8_t bulk_ep = dir ? dev->bulk_in_ep : dev->bulk_out_ep;

        // 2. 数据 TD 链
        for (int i = 0; i < td_count; i++) {
            int chunk = (remaining > max_packet) ? max_packet : remaining;
            struct uhci_td *td_data = (void *)((uintptr_t)prev_td + TD_SIZE_ALIGNED);

            td_data->ctrl_status = TD_CTL_NORMAL_BULK_USE;
            td_data->packed_header = TD_PH(chunk,toggle,bulk_ep,pid,dev_addr);
            uint64_t data_va = (uint64_t)req->bulk.buffer + (data_len - remaining);
            td_data->buffer = (uint32_t)mem_linear2phy_get(data_va, req->bulk.cr3);
            prev_td->link = (uint32_t)(uintptr_t)easy_linear2phy(td_data);
            
            prev_td = td_data;
            remaining -= chunk;
            toggle = toggle ? false : true;
        }

        // 3. 填充 CSW TD
        td_csw = (void *)((uintptr_t)prev_td + TD_SIZE_ALIGNED);
        td_csw->link = UHCI_PTR_TERM;
        td_csw->ctrl_status = TD_CTL_NORMAL_BULK_USE;
        td_csw->packed_header = TD_PH(13,1,dev->bulk_in_ep,0x69,dev_addr);
        td_csw->buffer = (uint32_t)mem_linear2phy_get((uint64_t)req->bulk.csw,(uint64_t)vir_ptable4);
        prev_td->link = (uint32_t)(uintptr_t)easy_linear2phy(td_csw);
        
        // 设置请求的 TD 头和尾
        req->td_head = td_cbw;
        req->last_td = td_csw;
        return 0;
    }
    return -1;
}

void uhci_kernel_thread(void) {
    while (1) {
        struct uhci_controller *hc;
        // 遍历所有 UHCI 控制器
        list_for_each_entry(hc, &uhci_controllers, uhci_controller_list) {
            spin_lock(&hc->lock);

            // 1. 检查当前活动请求是否完成
            if (hc->active_req) {
                uhci_request_t *req = hc->active_req;
                struct uhci_td *last = req->last_td;  // 最后一个 TD

                // 如果最后一个 TD 的 Active 位已清零，表示传输完成
                if (!(last->ctrl_status & TD_CTL_ACTIVE)) {
                    req->finished = 1;
                    // 检查错误标志
                    if (last->ctrl_status & (TD_CTL_STALLED | TD_CTL_DATA_BUFFER_ERR | TD_CTL_BUBBLE_DETECTED | TD_CTL_TIMEOUT_CRC))
                        req->status = -1;
                    else
                        req->status = 0;

                    hc->active_req = NULL;
                    hc->global_qh->vert_link = hc->idle_td_phy;
                    uhci_destroy_request(req);
                    wake_up_all(&req->wq);
                }
            }

            // 2. 如果没有活动请求且队列非空，启动下一个
            if (!hc->active_req && !list_empty(&hc->req_queue)) {
                // 从队列头部取出一个请求
                list_head_t *first = hc->req_queue.next;
                list_del_init(first);
                uhci_request_t *req = container_of(first, uhci_request_t, list);

                // 构建 TD 链（内部会使用 req->cr3 转换用户缓冲区地址）
                if (uhci_build_request(req) == 0) {
                    uint32_t td_head_phys = (uint32_t)(uintptr_t)easy_linear2phy(req->td_head);
                    hc->global_qh->vert_link = td_head_phys & ~0xF;  // 确保低4位清零
                    hc->active_req = req;               // 设为活动请求
                } else {
                    // 构建失败，直接结束请求
                    req->finished = 1;
                    req->status = -1;
                    wake_up_all(&req->wq);
                }
            }

            spin_unlock(&hc->lock);
        }
        sys_yield();  // 让出 CPU，避免忙等
    }
}

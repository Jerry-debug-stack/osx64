#include "const.h"
#include "machine/pcie/pcie.h"
#include "machine/pcie/pcie_type0.h"
#include "machine/disk/ahci.h"
#include "machine/disk/diskoperation.h"
#include "machine/disk/disk_hard_ops.h"
#include "fs/block.h"
#include "view/view.h"
#include "mm/mm.h"
#include "lib/string.h"

static int check_type(hba_port_t *port);
void start_cmd(hba_port_t *port);
void stop_cmd(hba_port_t *port);
static void init_command_fis_list(hba_mem_t *hbamem, int i);
int ahci_send(hba_port_t *port, uint64_t lba48, uint32_t count, void *buf, char command, int slot, uint64_t pml4_vir);
int find_cmdslot(hba_port_t *port);
uint64_t ahci_device_uid(ahci_identify_t *data);
static void ahci_register_block_device(ahci_device_t *adev);
static ahci_manager_t ahci_mgr;

extern int mbr_scan(block_device_t* disk);

static void ahci_register_device(hba_mem_t *hba, int port_no)
{
    if (ahci_mgr.count >= AHCI_MAX_DEVICES)
        return;
    ahci_device_t *d = &ahci_mgr.dev[ahci_mgr.count];
    memset(d, 0, sizeof(*d));
    d->hba = hba;
    d->port = &hba->ports[port_no];
    d->port_no = port_no;
    
    ahci_send(&hba->ports[port_no], 0, 1, &d->indentify, COMMAND_IDENTIFY, 0, 0);
    while (1) {
        if ((hba->ports[port_no].ci & 1) == 0)
            break;
    }

    spin_lock_int_able_init(&d->lock);
    ahci_mgr.count++;
    init_command_fis_list(hba,port_no);
    wb_printf("[ AHCI  ] registered device at port %d\n", port_no);
    ahci_register_block_device(d);
}

void *init_ahci_disk(int pcie_addr)
{
    hba_mem_t *hbamem = easy_phy2linear(read_pcie(MAKE_PCIE_ADDR_2(pcie_addr, BAR5)) & (~0xF));

    wb_printf("[ AHCI  ] detecting ahci:bar5 0x%x ver 0x%x\n", hbamem, hbamem->vs);
    for (int i = 0; i < 32; i++)
    {
        if (hbamem->pi & (1 << i))
        {
            int dt = check_type(&hbamem->ports[i]);
            if (dt == AHCI_DEV_SATA)
            {
                wb_printf("[ AHCI  ] SATA drive found at port %d\n", i);
                ahci_register_device(hbamem, i);
            }
            else if (dt == AHCI_DEV_SATAPI)
                wb_printf("[ AHCI  ] SATAPI drive found at port %d\n", i);
            else if (dt == AHCI_DEV_SEMB)
                wb_printf("[ AHCI  ] SEMB drive found at port %d\n", i);
            else if (dt == AHCI_DEV_PM)
                wb_printf("[ AHCI  ] PM drive found at port %d\n", i);
        }
    }
    return (void *)0;
}

void ahci_kernel_thread(void)
{
    while (1)
    {
        for (int i = 0; i < ahci_mgr.count; i++)
        {
            ahci_device_t *dev = &ahci_mgr.dev[i];
            
            pcb_t *to_wake[32];
            int wakecnt = 0;
            
            spin_lock_int_able(&dev->lock);
            for (int s = 0; s < 32; s++)
            {
                ahci_request_t *req = dev->active[s];
                if (!req)
                    continue;
                if (!(dev->port->ci & (1 << s)))
                {
                    req->finished = 1;
                    req->status = 0;
                    dev->active[s] = NULL;
                    pcb_t *task = req->waiter;
                    task->state = TASK_STATE_READY;
                    if (req->waiter)
                        to_wake[wakecnt++] = req->waiter;
                }
            }
            for (int s = 0; s < 32; s++)
            {
                if (dev->active[s])
                    continue;
                if (!dev->req_head)
                    break;
                ahci_request_t *req = dev->req_head;
                dev->req_head = req->next;
                if (!dev->req_head)
                    dev->req_tail = NULL;
                req->slot = s;
                dev->active[s] = req;
                ahci_send(dev->port,
                          req->lba,
                          req->count,
                          req->buffer,
                          req->write ? COMMAND_WRITE_LBA48 : COMMAND_READ_LBA48,
                          s,
                          req->waiter->cr3);
            }
            spin_int_able_unlock(&dev->lock);
            for (int k = 0; k < wakecnt; k++)
            {
                pcb_t *task = to_wake[k];
                if (!task) continue;
                task->state = TASK_STATE_READY;
                put_to_ready_list_first(task);
            }
        }
        yield(); // 让出 CPU
    }
}

int ahci_submit(ahci_device_t *dev, uint64_t lba, uint32_t count, void *buf, int write)
{
    ahci_request_t *req = kmalloc(sizeof(*req));
    memset(req, 0, sizeof(*req));

    req->lba = lba;
    req->count = count;
    req->buffer = buf;
    req->write = write;
    req->waiter = get_current();
    req->finished = 0;

    spin_lock_int_able(&dev->lock);
    // 入队
    if (!dev->req_head)
        dev->req_head = dev->req_tail = req;
    else {
        dev->req_tail->next = req;
        dev->req_tail = req;
    }
    // 关键：在锁内标记睡眠
    req->waiter->state = TASK_STATE_SLEEP_NOT_INTR_ABLE;
    spin_int_able_unlock(&dev->lock);
    // condition wait
    while (!req->finished)
        schedule();
    int status = req->status;
    kfree(req);
    return status;
}

// Check device type
static int check_type(hba_port_t *port)
{
    uint32_t ssts = port->ssts;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT) // Check drive status
        return AHCI_DEV_NULL;
    if (ipm != HBA_PORT_IPM_ACTIVE)
        return AHCI_DEV_NULL;

    switch (port->sig)
    {
    case SATA_SIG_ATAPI:
        return AHCI_DEV_SATAPI;
    case SATA_SIG_SEMB:
        return AHCI_DEV_SEMB;
    case SATA_SIG_PM:
        return AHCI_DEV_PM;
    default:
        return AHCI_DEV_SATA;
    }
}

static void init_command_fis_list(hba_mem_t *hbamem, int i)
{
    /*
    内存消耗：cmd_list :32*32=1024---|->4k
            received_fis:256-------|
            cmd_table:32*256=8192(2个4k)
    */
    hba_port_t *port = &hbamem->ports[i];
    stop_cmd(port);
    char *addr = kmalloc(4096);
    memset(addr, 0, 4096);
    // cmd_list
    hba_cmd_header_t *cmd_list = (void *)addr;
    port->clb = (uint32_t)(uint64_t)easy_linear2phy(addr);
    port->clbu = (uint32_t)((uint64_t)easy_linear2phy(addr) >> 32);
    // received_fis
    port->fb = (uint32_t)(uint64_t)(easy_linear2phy(addr) + 1024);
    port->fbu = (uint32_t)(((uint64_t)easy_linear2phy(addr) + 1024) >> 32);
    // cmd_table and prdt
    hba_cmd_tbl_t *cmd_table = kmalloc(4096);
    for (int i = 0; i < 16; i++)
    {
        cmd_list[i].ctba = (uint32_t)(uint64_t)easy_linear2phy(cmd_table + i);
        cmd_list[i].ctbau = (uint32_t)((uint64_t)easy_linear2phy(cmd_table + i) >> 32);
        cmd_list[i].prdtl = 8;
    }
    cmd_table = kmalloc(4096);
    for (int i = 0; i < 16; i++)
    {
        cmd_list[i + 16].ctba = (uint32_t)(uint64_t)easy_linear2phy(cmd_table + i);
        cmd_list[i + 16].ctbau = (uint32_t)((uint64_t)easy_linear2phy(cmd_table + i) >> 32);
        cmd_list[i + 16].prdtl = 8;
    }
    __asm__ __volatile__("mfence");
    start_cmd(port);
}

// Start command engine
void start_cmd(hba_port_t *port)
{
    // Wait until CR (bit15) is cleared
    while (port->cmd & HBA_PxCMD_CR)
        ;
    // Set FRE (bit4) and ST (bit0)
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

// Stop command engine
void stop_cmd(hba_port_t *port)
{
    // Clear ST (bit0)
    port->cmd &= ~HBA_PxCMD_ST;
    // Clear FRE (bit4)
    port->cmd &= ~HBA_PxCMD_FRE;
    // Wait until FR (bit14), CR (bit15) are cleared
    while (1)
    {
        if (port->cmd & HBA_PxCMD_FR)
            continue;
        if (port->cmd & HBA_PxCMD_CR)
            continue;
        break;
    }
}

int ahci_send(hba_port_t *port, uint64_t lba48, uint32_t count, void *buf, char command, int slot, uint64_t pml4_phy)
{
    port->is = (uint32_t)-1; // Clear pending interrupt bits
    int spin = 0;            // Spin lock timeout counter
    if (slot >= 32)
        return -1;

    hba_cmd_header_t *cmdheader = easy_phy2linear(port->clb | ((uint64_t)port->clbu << 32));
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); // Command FIS size
    if (command == COMMAND_WRITE_LBA48)
        cmdheader->w = 1;
    else
        cmdheader->w = 0;
    cmdheader->prdtl = (uint16_t)((count - 1) >> 3) + 1; // PRDT entries count

    hba_cmd_tbl_t *cmdtbl = easy_phy2linear(cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32));
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t) + (cmdheader->prdtl - 1) * sizeof(hba_prdt_entry_t));
    int i;
    uint64_t addr = (uint64_t)buf;
    uint64_t tmp_addr;
    uint32_t tmp_count = count;
    // 4K bytes (8 sectors) per PRDT
    for (i = 0; i < cmdheader->prdtl - 1; i++)
    {
        tmp_addr = mem_linear2phy(addr, pml4_phy);
        cmdtbl->prdt_entry[i].dba = (uint32_t)tmp_addr;
        cmdtbl->prdt_entry[i].dbau = (uint32_t)(tmp_addr >> 32);
        // 8K bytes (this value should always be set to 1 less than the actual value)
        cmdtbl->prdt_entry[i].dbc = 4 * 1024 - 1;
        cmdtbl->prdt_entry[i].i = 1;
        addr += 4 * 1024; // 4K bytes
        tmp_count -= 8;   // 8 sectors
    }
    // Last entry
    tmp_addr = mem_linear2phy(addr, pml4_phy);
    cmdtbl->prdt_entry[i].dba = (uint32_t)tmp_addr;
    cmdtbl->prdt_entry[i].dbau = (uint32_t)(tmp_addr >> 32);
    // 512 bytes per sector
    cmdtbl->prdt_entry[i].dbc = (tmp_count << 9) - 1;
    cmdtbl->prdt_entry[i].i = 1;
    // Setup command
    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1; // Command
    cmdfis->command = command;
    cmdfis->lba0 = (uint8_t)lba48;
    cmdfis->lba1 = (uint8_t)(lba48 >> 8);
    cmdfis->lba2 = (uint8_t)(lba48 >> 16);
    cmdfis->device = 1 << 6; // LBA mode
    cmdfis->lba3 = (uint8_t)(lba48 >> 24);
    cmdfis->lba4 = (uint8_t)(lba48 >> 32);
    cmdfis->lba5 = (uint8_t)(lba48 >> 40);
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;
    __asm__ __volatile__("mfence");
    // The below loop waits until the port is no longer busy before issuing a new command
    while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
        spin++;
    if (spin == 1000000)
        return -1;
    port->ci |= 1 << slot; // Issue command
    __asm__ __volatile__("mfence");
    return 0;
}

// Find a free command list slot
int find_cmdslot(hba_port_t *port)
{
    // If not set in SACT and CI, the slot is free
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++)
    {
        if ((slots & 1) == 0)
            return i;
        slots >>= 1;
    }
    return -1;
}

uint64_t ahci_device_uid(ahci_identify_t *data)
{
    char serial[21] = {0};

    // 正确提取序列号（考虑字节序）
    for (int i = 0; i < 10; i++)
    {
        uint16_t word = ((uint16_t *)data->serial_number)[i];
        serial[i * 2] = (word >> 8) & 0xFF; // 高字节
        serial[i * 2 + 1] = word & 0xFF;    // 低字节
    }

    // 去除尾部空格
    int len = 20;
    while (len > 0 && serial[len - 1] == ' ')
    {
        len--;
    }

    // 生成UID（使用模型号增加熵值）
    uint64_t uid = 0xcbf29ce484222325UL;

    // 混合序列号
    for (int i = 0; i < len; i++)
    {
        uid ^= (uint64_t)serial[i];
        uid *= 0x100000001b3UL;
    }

    // 混合部分模型号增加唯一性
    for (int i = 0; i < 8; i++)
    {
        uid ^= (uint64_t)data->model_number[i];
        uid *= 0x100000001b3UL;
    }

    return uid;
}

void init_ahci_mem(void)
{
    memset(&ahci_mgr, 0, sizeof(ahci_mgr));
}

static int ahci_block_read(block_device_t *bdev,uint64_t lba,uint32_t count,void *buffer)
{
    ahci_device_t *adev = (ahci_device_t *)bdev->private_data;
    return ahci_submit(adev, lba, count, buffer, 0);
}

static int ahci_block_write(block_device_t *bdev,uint64_t lba,uint32_t count,const void *buffer)
{
    ahci_device_t *adev = (ahci_device_t *)bdev->private_data;
    return ahci_submit(adev, lba, count, (void*)buffer, 1);
}

static void ahci_register_block_device(ahci_device_t *adev)
{
    block_device_t *bdev = kmalloc(sizeof(real_device_t));
    bdev->total_blocks = adev->indentify.lba_sectors_48;
    bdev->block_size = 512;
    bdev->private_data = adev;
    bdev->type = BLOCK_DISK;

    bdev->read  = ahci_block_read;
    bdev->write = ahci_block_write;

    block_register(bdev);
}

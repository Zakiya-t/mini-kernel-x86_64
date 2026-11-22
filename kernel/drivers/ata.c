#include "drivers/ata.h"
#include "arch/x86_64/io.h"
#include "core/kprintf.h"
#include "string.h"

static ata_device_info_t primary_master = {0};
static ata_telemetry_t   ata_stats      = {0};

/* Brief 400ns delay reading Alternate Status register */
static inline void ata_delay(void) {
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
    inb(ATA_REG_ALTSTATUS);
}

/* Wait until BSY flag clears */
static int ata_wait_bsy(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_REG_ALTSTATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1; /* Timeout */
}

/* Wait until BSY clears and DRQ (Data Request) sets */
static int ata_wait_drq(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_REG_ALTSTATUS);
        if (!(status & ATA_SR_BSY)) {
            if (status & ATA_SR_ERR) {
                return -1; /* Hardware error */
            }
            if (status & ATA_SR_DF) {
                return -2; /* Drive write fault */
            }
            if (status & ATA_SR_DRQ) {
                return 0; /* Ready for data transfer */
            }
        }
    }
    return -3; /* Timeout */
}

int ata_init(void) {
    memset(&primary_master, 0, sizeof(primary_master));

    /* 1. Select Primary Master Drive (0xA0) */
    outb(ATA_REG_DRIVE_HEAD, 0xA0);
    ata_delay();

    /* 2. Reset sector count and LBA address registers */
    outb(ATA_REG_SECCOUNT, 0);
    outb(ATA_REG_LBA_LO, 0);
    outb(ATA_REG_LBA_MID, 0);
    outb(ATA_REG_LBA_HI, 0);

    /* 3. Send IDENTIFY Command (0xEC) */
    outb(ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay();

    /* 4. Check if drive exists */
    uint8_t status = inb(ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        KLOG_WARN("ATA: No primary master hard drive detected (Status: 0x%02x)", status);
        return -1;
    }

    /* 5. Wait for BSY to clear */
    if (ata_wait_bsy() != 0) {
        KLOG_ERR("ATA: IDENTIFY timeout waiting for BSY to clear");
        return -2;
    }

    /* 6. Check for non-ATA devices (ATAPI / SATA) */
    uint8_t mid = inb(ATA_REG_LBA_MID);
    uint8_t hi  = inb(ATA_REG_LBA_HI);
    if (mid != 0 || hi != 0) {
        KLOG_WARN("ATA: Device is not ATA hard disk (ATAPI mid=0x%02x, hi=0x%02x)", mid, hi);
        return -3;
    }

    /* 7. Wait for DRQ to be ready */
    if (ata_wait_drq() != 0) {
        KLOG_ERR("ATA: IDENTIFY failed waiting for DRQ");
        return -4;
    }

    /* 8. Read 256 words (512 bytes) of IDENTIFY information */
    uint16_t identify_buf[256];
    for (size_t i = 0; i < 256; i++) {
        identify_buf[i] = inw(ATA_REG_DATA);
    }

    /* Extract Model string (words 27..46, byte-swapped) */
    char model[41];
    for (size_t i = 0; i < 20; i++) {
        uint16_t w = identify_buf[27 + i];
        model[i * 2]     = (char)(w >> 8);
        model[i * 2 + 1] = (char)(w & 0xFF);
    }
    model[40] = '\0';

    /* Trim trailing spaces from model string */
    for (int i = 39; i >= 0 && model[i] == ' '; i--) {
        model[i] = '\0';
    }

    /* Extract Total LBA28 Sectors (words 60 and 61) */
    uint32_t total_sectors = (uint32_t)identify_buf[60] | ((uint32_t)identify_buf[61] << 16);
    uint32_t size_mb = total_sectors / 2048; /* 512 bytes * 2048 = 1 MiB */

    primary_master.present       = true;
    strncpy(primary_master.model, model, sizeof(primary_master.model) - 1);
    primary_master.total_sectors = total_sectors;
    primary_master.size_mb       = size_mb;

    KLOG_OK("ATA Hard Disk Initialized: \"%s\" (%u MiB, %u sectors, LBA28)",
            primary_master.model, primary_master.size_mb, primary_master.total_sectors);
    return 0;
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, void *buf) {
    if (!primary_master.present || !buf || sector_count == 0) {
        return -1;
    }

    if (ata_wait_bsy() != 0) {
        return -2;
    }

    /* Select drive with LBA mode: 0xE0 | upper 4 bits of LBA */
    outb(ATA_REG_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    outb(ATA_REG_SECCOUNT, sector_count);
    outb(ATA_REG_LBA_LO, (uint8_t)lba);
    outb(ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    ata_delay();

    uint16_t *target = (uint16_t *)buf;
    for (uint8_t s = 0; s < sector_count; s++) {
        if (ata_wait_drq() != 0) {
            KLOG_ERR("ata_read_sectors: Error reading sector %u (LBA %u)", s, lba + s);
            return -3;
        }

        /* Read 256 words (512 bytes) from data port */
        for (size_t i = 0; i < 256; i++) {
            *target++ = inw(ATA_REG_DATA);
        }
        ata_delay();
    }

    ata_stats.total_reads++;
    ata_stats.total_sectors_read += sector_count;
    ata_stats.last_lba = lba;
    ata_stats.last_sector_count = sector_count;
    ata_stats.last_command = ATA_CMD_READ_PIO;

    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const void *buf) {
    if (!primary_master.present || !buf || sector_count == 0) {
        return -1;
    }

    if (ata_wait_bsy() != 0) {
        return -2;
    }

    /* Select drive with LBA mode: 0xE0 | upper 4 bits of LBA */
    outb(ATA_REG_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay();

    outb(ATA_REG_SECCOUNT, sector_count);
    outb(ATA_REG_LBA_LO, (uint8_t)lba);
    outb(ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_REG_LBA_HI, (uint8_t)(lba >> 16));
    outb(ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *source = (const uint16_t *)buf;
    for (uint8_t s = 0; s < sector_count; s++) {
        if (ata_wait_drq() != 0) {
            KLOG_ERR("ata_write_sectors: Error writing sector %u (LBA %u)", s, lba + s);
            return -3;
        }

        /* Write 256 words (512 bytes) to data port */
        for (size_t i = 0; i < 256; i++) {
            outw(ATA_REG_DATA, *source++);
        }
        ata_delay();
    }

    /* Flush drive write cache to guarantee data persists to physical disk media */
    outb(ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();

    ata_stats.total_writes++;
    ata_stats.total_sectors_written += sector_count;
    ata_stats.last_lba = lba;
    ata_stats.last_sector_count = sector_count;
    ata_stats.last_command = ATA_CMD_WRITE_PIO;

    return 0;
}

void ata_get_info(ata_device_info_t *info) {
    if (info) {
        *info = primary_master;
    }
}

void ata_get_telemetry(ata_telemetry_t *info) {
    if (info) {
        *info = ata_stats;
    }
}

void ata_snapshot_telemetry(uint64_t *reads, uint64_t *sectors_read, uint64_t *writes, uint64_t *sectors_written) {
    if (reads) *reads = ata_stats.total_reads;
    if (sectors_read) *sectors_read = ata_stats.total_sectors_read;
    if (writes) *writes = ata_stats.total_writes;
    if (sectors_written) *sectors_written = ata_stats.total_sectors_written;
}

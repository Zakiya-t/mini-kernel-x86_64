#ifndef _KERNEL_DRIVERS_ATA_H
#define _KERNEL_DRIVERS_ATA_H

#include "types.h"

/* Primary ATA Bus I/O Ports */
#define ATA_REG_DATA          0x1F0
#define ATA_REG_ERROR         0x1F1
#define ATA_REG_FEATURES      0x1F1
#define ATA_REG_SECCOUNT      0x1F2
#define ATA_REG_LBA_LO        0x1F3
#define ATA_REG_LBA_MID       0x1F4
#define ATA_REG_LBA_HI        0x1F5
#define ATA_REG_DRIVE_HEAD    0x1F6
#define ATA_REG_STATUS        0x1F7
#define ATA_REG_COMMAND       0x1F7
#define ATA_REG_CONTROL       0x3F6
#define ATA_REG_ALTSTATUS     0x3F6

/* ATA Status Register Bits */
#define ATA_SR_BSY            0x80  /* Controller Busy */
#define ATA_SR_DRDY           0x40  /* Drive Ready */
#define ATA_SR_DF             0x20  /* Drive Write Fault */
#define ATA_SR_DSC            0x10  /* Drive Seek Complete */
#define ATA_SR_DRQ            0x08  /* Data Request Ready */
#define ATA_SR_CORR           0x04  /* Corrected Data */
#define ATA_SR_IDX            0x02  /* Index */
#define ATA_SR_ERR            0x01  /* Error */

/* ATA Commands */
#define ATA_CMD_READ_PIO      0x20
#define ATA_CMD_WRITE_PIO     0x30
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_IDENTIFY      0xEC

/* Sector size */
#define ATA_SECTOR_SIZE       512

/* Device info structure */
typedef struct {
    bool     present;
    char     model[41];
    char     serial[21];
    uint32_t total_sectors;
    uint32_t size_mb;
} ata_device_info_t;

/* ATA Hardware Telemetry */
typedef struct {
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_sectors_read;
    uint64_t total_sectors_written;
    uint32_t last_lba;
    uint8_t  last_sector_count;
    uint8_t  last_command;
} ata_telemetry_t;

/* ATA Driver Interface */
int  ata_init(void);
int  ata_read_sectors(uint32_t lba, uint8_t sector_count, void *buf);
int  ata_write_sectors(uint32_t lba, uint8_t sector_count, const void *buf);
void ata_get_info(ata_device_info_t *info);
void ata_get_telemetry(ata_telemetry_t *info);
void ata_snapshot_telemetry(uint64_t *reads, uint64_t *sectors_read, uint64_t *writes, uint64_t *sectors_written);

#endif /* _KERNEL_DRIVERS_ATA_H */

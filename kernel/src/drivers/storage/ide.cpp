#include <block/block.hpp>
#include <cstdint>
#include <drivers/storage/ide.hpp>
#include <hpet/hpet.hpp>
#include <logging/logger.hpp>
#include <memory/memory.hpp>
#include <panic/panic.hpp>
#include <pci/pci.hpp>
#include <ports/ports.hpp>

namespace drivers
{

namespace storage
{

namespace ide
{

// ATA status register bits
static constexpr std::uint8_t ATA_SR_BSY = 0x80;
static constexpr std::uint8_t ATA_SR_DRDY = 0x40;
static constexpr std::uint8_t ATA_SR_DF = 0x20;
static constexpr std::uint8_t ATA_SR_DRQ = 0x08;
static constexpr std::uint8_t ATA_SR_ERR = 0x01;

// ATA commands (LBA48 variants where applicable)
static constexpr std::uint8_t ATA_CMD_IDENTIFY = 0xEC;
static constexpr std::uint8_t ATA_CMD_READ_PIO = 0x20;
static constexpr std::uint8_t ATA_CMD_READ_PIO_EXT = 0x24;
static constexpr std::uint8_t ATA_CMD_WRITE_PIO = 0x30;
static constexpr std::uint8_t ATA_CMD_WRITE_PIO_EXT = 0x34;

static constexpr std::uint16_t ATA_DEV_LBA = 0x40;
static constexpr std::uint16_t ATA_DEV_SLAVE = 0x10;

// conventional (legacy) I/O base for each channel; used when the controller
// does not expose programmed native-mode BARs
static constexpr std::uint16_t CHANNEL_BASE[2] = {0x1F0, 0x170};
static constexpr std::uint16_t CHANNEL_CTRL[2] = {0x3F6, 0x376};

// register offsets from the channel data base
static constexpr std::uint16_t REG_DATA = 0;
static constexpr std::uint16_t REG_ERROR = 1;
static constexpr std::uint16_t REG_SECTOR_COUNT = 2;
static constexpr std::uint16_t REG_LBA_LOW = 3;
static constexpr std::uint16_t REG_LBA_MID = 4;
static constexpr std::uint16_t REG_LBA_HIGH = 5;
static constexpr std::uint16_t REG_DEVICE = 6;
static constexpr std::uint16_t REG_STATUS = 7;
// the alternate status register shares its I/O port with the device control
// register at the channel's control base
static constexpr std::uint16_t REG_ALT_STATUS = 0;

// the 48-bit LBA space fits a 16-bit sector count register pair per command
static constexpr std::uint32_t MAX_SECTORS_PER_CMD = 0xFFFF;

static constexpr std::uint64_t IDE_TIMEOUT_US = 5000000;

struct drive_state {
	bool present;
	bool lba48;
	std::uint16_t io_base;
	std::uint16_t ctrl_base;
	std::uint8_t drive; // 0 = master, 1 = slave
	std::uint64_t block_count;
	block::device bdev;
};

static drive_state drives[4];
static int drive_count = 0;

static bool wait_status(std::uint16_t status_port, std::uint8_t mask,
			std::uint8_t expect)
{
	std::uint64_t freq = timers::hpet::hpet.get_freq();
	if (freq == 0)
		return false;
	std::uint64_t ticks = freq / 1000000;
	if (ticks == 0)
		ticks = 1;

	std::uint64_t start = timers::hpet::hpet.read_counter();
	for (;;)
	{
		std::uint8_t status = ports::inb(status_port);
		if (!(status & ATA_SR_BSY) && (status & mask) == expect)
			return true;
		if (status & ATA_SR_ERR)
			return false;
		if (timers::hpet::hpet.read_counter() - start >=
		    IDE_TIMEOUT_US * ticks)
			return false;
		asm volatile("pause" : : : "memory");
	}
}

static bool wait_not_busy(std::uint16_t status_port)
{
	return wait_status(status_port, 0, 0);
}

static bool wait_drq(std::uint16_t status_port)
{
	return wait_status(status_port, ATA_SR_DRQ, ATA_SR_DRQ);
}

static bool ide_identify(drive_state* d)
{
	std::uint16_t io = d->io_base;
	std::uint16_t ctrl = d->ctrl_base;
	std::uint16_t status_port = ctrl + REG_ALT_STATUS;

	// select the drive; a nonexistent drive never asserts DRDY
	ports::outb(io + REG_DEVICE,
		    static_cast<std::uint8_t>(ATA_DEV_LBA |
					      (d->drive << 4)));
	if (!wait_not_busy(status_port))
		return false;

	// clear the interrupt/error state and set up the zero signature
	ports::outb(ctrl, 0x02); // nIEN not set; reset the control shadow
	ports::outb(io + REG_SECTOR_COUNT, 0);
	ports::outb(io + REG_LBA_LOW, 0);
	ports::outb(io + REG_LBA_MID, 0);
	ports::outb(io + REG_LBA_HIGH, 0);
	ports::outb(io + REG_STATUS, ATA_CMD_IDENTIFY);

	if (!wait_not_busy(status_port))
		return false;
	// a missing drive never asserts: the status port reads 0x00 or the
	// floating bus returns 0xFF
	std::uint8_t status = ports::inb(io + REG_STATUS);
	if (status == 0x00 || status == 0xFF)
		return false;

	std::uint8_t ident[512];
	for (int i = 0; i < 256; ++i)
	{
		std::uint16_t word = ports::inw(io + REG_DATA);
		ident[i * 2] = static_cast<std::uint8_t>(word);
		ident[i * 2 + 1] = static_cast<std::uint8_t>(word >> 8);
	}

	const std::uint16_t* w = reinterpret_cast<const std::uint16_t*>(ident);
	// word 0 bit 0: device type (0 = ATA, 1 = ATAPI); skip ATAPI for now
	if (w[0] & 1)
		return false;

	// word 83 bit 10: LBA48 addressing
	d->lba48 = (w[83] & (1 << 10)) != 0;
	if (d->lba48)
	{
		std::uint64_t count = 0;
		for (int i = 100; i <= 103; ++i)
			count |= static_cast<std::uint64_t>(w[i])
				 << (16 * (i - 100));
		if (count == 0)
			d->lba48 = false;
		else
			d->block_count = count;
	}
	if (!d->lba48)
		d->block_count = static_cast<std::uint64_t>(w[60]) |
				  (static_cast<std::uint64_t>(w[61]) << 16);

	// a real drive always reports a non-zero sector count
	return d->block_count != 0;
}

static void ide_select_drive(drive_state* d)
{
	ports::outb(d->io_base + REG_DEVICE,
		    static_cast<std::uint8_t>(ATA_DEV_LBA |
					      (d->drive << 4)));
}

// issue a PIO read command and transfer `count` sectors (each 512 bytes)
static bool ide_read_sectors(drive_state* d, std::uint64_t lba,
			     std::uint32_t count, void* buf)
{
	std::uint16_t io = d->io_base;
	std::uint16_t status_port = d->ctrl_base + REG_ALT_STATUS;

	if (count == 0)
		return true;

	ide_select_drive(d);
	if (!wait_not_busy(status_port))
		return false;

	// LBA48: write the high half of the registers first, then the low half
	ports::outb(io + REG_SECTOR_COUNT,
		    static_cast<std::uint8_t>(count >> 8));
	ports::outb(io + REG_LBA_LOW,
		    static_cast<std::uint8_t>(lba >> 24));
	ports::outb(io + REG_LBA_MID,
		    static_cast<std::uint8_t>(lba >> 32));
	ports::outb(io + REG_LBA_HIGH,
		    static_cast<std::uint8_t>(lba >> 40));
	ports::outb(io + REG_SECTOR_COUNT,
		    static_cast<std::uint8_t>(count));
	ports::outb(io + REG_LBA_LOW, static_cast<std::uint8_t>(lba));
	ports::outb(io + REG_LBA_MID,
		    static_cast<std::uint8_t>(lba >> 8));
	ports::outb(io + REG_LBA_HIGH,
		    static_cast<std::uint8_t>(lba >> 16));
	ports::outb(io + REG_STATUS,
		    d->lba48 ? ATA_CMD_READ_PIO_EXT : ATA_CMD_READ_PIO);

	auto* dst = static_cast<std::uint16_t*>(buf);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (!wait_drq(status_port))
		{
#ifdef DEBUG
			LOG("ide_read_no_drq; lba=%llu; i=%u; status=0x%x",
			    static_cast<std::uint64_t>(lba), i,
			    static_cast<std::uint32_t>(
				ports::inb(io + REG_STATUS)));
#endif
			return false;
		}
		for (int j = 0; j < 256; ++j)
			dst[i * 256 + j] = ports::inw(io + REG_DATA);
	}
	return true;
}

// issue a PIO write command and transfer `count` sectors
static bool ide_write_sectors(drive_state* d, std::uint64_t lba,
			      std::uint32_t count, const void* buf)
{
	std::uint16_t io = d->io_base;
	std::uint16_t status_port = d->ctrl_base + REG_ALT_STATUS;

	if (count == 0)
		return true;

	ide_select_drive(d);
	if (!wait_not_busy(status_port))
		return false;

	ports::outb(io + REG_SECTOR_COUNT,
		    static_cast<std::uint8_t>(count >> 8));
	ports::outb(io + REG_LBA_LOW,
		    static_cast<std::uint8_t>(lba >> 24));
	ports::outb(io + REG_LBA_MID,
		    static_cast<std::uint8_t>(lba >> 32));
	ports::outb(io + REG_LBA_HIGH,
		    static_cast<std::uint8_t>(lba >> 40));
	ports::outb(io + REG_SECTOR_COUNT,
		    static_cast<std::uint8_t>(count));
	ports::outb(io + REG_LBA_LOW, static_cast<std::uint8_t>(lba));
	ports::outb(io + REG_LBA_MID,
		    static_cast<std::uint8_t>(lba >> 8));
	ports::outb(io + REG_LBA_HIGH,
		    static_cast<std::uint8_t>(lba >> 16));
	ports::outb(io + REG_STATUS,
		    d->lba48 ? ATA_CMD_WRITE_PIO_EXT : ATA_CMD_WRITE_PIO);

	const auto* src = static_cast<const std::uint16_t*>(buf);
	for (std::uint32_t i = 0; i < count; ++i)
	{
		if (!wait_drq(status_port))
		{
#ifdef DEBUG
			LOG("ide_write_no_drq; lba=%llu; i=%u; status=0x%x",
			    static_cast<std::uint64_t>(lba), i,
			    static_cast<std::uint32_t>(
				ports::inb(io + REG_STATUS)));
#endif
			return false;
		}
		for (int j = 0; j < 256; ++j)
			ports::outw(io + REG_DATA, src[i * 256 + j]);
	}
	// the device needs a moment to commit the final sector to the platter
	if (!wait_not_busy(status_port))
		return false;
	return true;
}

static bool raw_read(block::device* dev, std::uint64_t lba,
		     std::uint32_t count, void* buf)
{
	auto* d = static_cast<drive_state*>(dev->priv);
	if (!d || !d->present)
		return false;

	std::uint64_t remaining = count;
	std::uint64_t cur = lba;
	std::uint64_t offset = 0;
	while (remaining > 0)
	{
		std::uint32_t chunk = remaining > MAX_SECTORS_PER_CMD
					 ? MAX_SECTORS_PER_CMD
					 : static_cast<std::uint32_t>(remaining);
		auto* dst = static_cast<std::uint8_t*>(buf) + offset * 512;
		if (!ide_read_sectors(d, cur, chunk, dst))
			return false;
		remaining -= chunk;
		cur += chunk;
		offset += chunk;
	}
	return true;
}

static bool raw_write(block::device* dev, std::uint64_t lba,
		      std::uint32_t count, const void* buf)
{
	auto* d = static_cast<drive_state*>(dev->priv);
	if (!d || !d->present)
		return false;

	std::uint64_t remaining = count;
	std::uint64_t cur = lba;
	std::uint64_t offset = 0;
	while (remaining > 0)
	{
		std::uint32_t chunk = remaining > MAX_SECTORS_PER_CMD
					 ? MAX_SECTORS_PER_CMD
					 : static_cast<std::uint32_t>(remaining);
		const auto* src =
		    static_cast<const std::uint8_t*>(buf) + offset * 512;
		if (!ide_write_sectors(d, cur, chunk, src))
			return false;
		remaining -= chunk;
		cur += chunk;
		offset += chunk;
	}
	return true;
}

static void probe_channel(std::uint16_t io_base, std::uint16_t ctrl_base)
{
	for (int drive = 0; drive < 2 && drive_count < 4; ++drive)
	{
		drive_state* d = &drives[drive_count];
		d->present = false;
		d->io_base = io_base;
		d->ctrl_base = ctrl_base;
		d->drive = static_cast<std::uint8_t>(drive);
		d->block_count = 0;

		if (!ide_identify(d))
			continue;

		d->present = true;
		d->bdev.block_count = d->block_count;
		d->bdev.block_size = 512;
		d->bdev.raw_read = &raw_read;
		d->bdev.raw_write = &raw_write;
		d->bdev.priv = d;

		block::device_register(&d->bdev);

#ifdef DEBUG
		LOG("ide_pio_device_registered; channel=%u; drive=%d; "
		    "blocks=%llu; lba48=%d",
		    static_cast<std::uint32_t>(io_base == 0x1F0 ? 0 : 1),
		    drive, d->block_count, d->lba48 ? 1 : 0);
#endif
		++drive_count;
	}
}

void init(void)
{
	if (drive_count > 0)
		return;

	// legacy I/O ports are the default; native-mode PCI IDE controllers
	// reprogram the channel bases into their BARs (command + control blocks)
	bool native[2] = {false, false};
	std::uint16_t cmd_base[2] = {0, 0};
	std::uint16_t ctrl_base[2] = {0, 0};

	int dev_count = pci::pci.device_count();
	for (int i = 0; i < dev_count; ++i)
	{
		const pci::device* device = pci::pci.get_device(i);
		if (!device)
			continue;
		// class 0x01 (mass storage), subclass 0x01 (IDE)
		if (device->class_code != PCI_CLASS_MASS_STORAGE ||
		    device->subclass != 0x01)
			continue;

		// enable bus mastering + I/O space
		std::uint32_t cmd =
		    pci::pci.read_config(device->bus, device->slot,
					 device->func, PCI_COMMAND);
		cmd |= 0x5; // I/O space, bus master
		pci::pci.write_config(device->bus, device->slot,
				      device->func, PCI_COMMAND, cmd);

		// prog-if bits 0-1 / 2-3 select primary/secondary channel
		// transfer mode: 0 = compatibility, 1 = native
		if (device->prog_if & 0x1)
		{
			pci::bar_info bar0 = pci::pci.read_bar_info(
			    device->bus, device->slot, device->func, 0);
			pci::bar_info bar1 = pci::pci.read_bar_info(
			    device->bus, device->slot, device->func, 1);
			if (bar0.is_io && bar0.address != 0 &&
			    bar1.is_io && bar1.address != 0)
			{
				native[0] = true;
				cmd_base[0] = static_cast<std::uint16_t>(
				    bar0.address & 0xFFF8);
				ctrl_base[0] = static_cast<std::uint16_t>(
				    bar1.address & 0xFFFC);
			}
		}
		if (device->prog_if & 0x4)
		{
			pci::bar_info bar2 = pci::pci.read_bar_info(
			    device->bus, device->slot, device->func, 2);
			pci::bar_info bar3 = pci::pci.read_bar_info(
			    device->bus, device->slot, device->func, 3);
			if (bar2.is_io && bar2.address != 0 &&
			    bar3.is_io && bar3.address != 0)
			{
				native[1] = true;
				cmd_base[1] = static_cast<std::uint16_t>(
				    bar2.address & 0xFFF8);
				ctrl_base[1] = static_cast<std::uint16_t>(
				    bar3.address & 0xFFFC);
			}
		}
	}

	for (int ch = 0; ch < 2; ++ch)
	{
		std::uint16_t io = native[ch] ? cmd_base[ch]
					     : CHANNEL_BASE[ch];
		std::uint16_t ctrl = native[ch] ? ctrl_base[ch]
						: CHANNEL_CTRL[ch];
		probe_channel(io, ctrl);
	}

	if (drive_count == 0)
		PANIC("no_ide_drive_found");
}

}

}

}

#pragma once

#include <block/block.hpp>
#include <cstdint>
#include <dma/dma.hpp>
#include <sync/spinlock.hpp>

namespace drivers
{

namespace storage
{

namespace ahci
{
#define SATA_SIG_ATA 0x00000101	  // SATA drive
#define SATA_SIG_ATAPI 0xEB140101 // SATAPI drive
#define SATA_SIG_SEMB 0xC33C0101  // Enclosure management bridge
#define SATA_SIG_PM 0x96690101	  // Port multiplier

#define AHCI_DEV_NULL 0
#define AHCI_DEV_SATA 1
#define AHCI_DEV_SEMB 2
#define AHCI_DEV_PM 3
#define AHCI_DEV_SATAPI 4

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#define HBA_PxCMD_ST 0x0001
#define HBA_PxCMD_IE 0x0002
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR 0x4000
#define HBA_PxCMD_CR 0x8000

#define HBA_GHC_HR 0x00000001
#define HBA_GHC_IE 0x00000002
#define HBA_GHC_AE 0x80000000

#define HBA_PxIS_D2H 0x00000001
#define HBA_PxIS_TFES 0x40000000

#define ATA_CMD_READ_DMA_EXT 0x25

// fixed IDT vector used for the controller (MSI or INTx). It lives inside the
// device-IRQ window (0x40..0x5F) that the IDT wires up as dispatchable gates.
static constexpr std::uint8_t DEVICE_IRQ_VECTOR = 0x50;

// timeout for controller-level operations (reset, BOHC handoff)
static constexpr std::uint64_t CONTROLLER_TIMEOUT_US = 1000000;
// timeout for per-port command-list state transitions
static constexpr std::uint64_t PORT_TIMEOUT_US = 100000;

enum class fis_type {
	FIS_TYPE_REG_H2D = 0x27,
	FIS_TYPE_REG_D2H = 0x34,
	FIS_TYPE_DMA_ACT = 0x39,
	FIS_TYPE_DMA_SETUP = 0x41,
	FIS_TYPE_DATA = 0x46,
	FIS_TYPE_BIST = 0x58,
	FIS_TYPE_PIO_SETUP = 0x5F,
	FIS_TYPE_DEV_BITS = 0xA1,
};

struct reg_h2d {
	std::uint8_t fis_type;
	std::uint8_t pmport : 4;
	std::uint8_t rsv0 : 3;
	std::uint8_t c : 1;
	std::uint8_t command;
	std::uint8_t featurel;
	std::uint8_t lba0;
	std::uint8_t lba1;
	std::uint8_t lba2;
	std::uint8_t device;
	std::uint8_t lba3;
	std::uint8_t lba4;
	std::uint8_t lba5;
	std::uint8_t featureh;
	std::uint8_t countl;
	std::uint8_t counth;
	std::uint8_t icc;
	std::uint8_t control;
	std::uint8_t rsv1[4];
};

struct reg_d2h {
	std::uint8_t fis_type;
	std::uint8_t pmport : 4;
	std::uint8_t rsv0 : 2;
	std::uint8_t i : 1;
	std::uint8_t rsv1 : 1;
	std::uint8_t status;
	std::uint8_t error;
	std::uint8_t lba0;
	std::uint8_t lba1;
	std::uint8_t lba2;
	std::uint8_t device;
	std::uint8_t lba3;
	std::uint8_t lba4;
	std::uint8_t lba5;
	std::uint8_t rsv2;
	std::uint8_t countl;
	std::uint8_t counth;
	std::uint8_t rsv3[2];
	std::uint8_t rsv4[4];
};

struct fis_data {
	std::uint8_t fis_type;
	std::uint8_t pmport : 4;
	std::uint8_t rsv0 : 4;
	std::uint8_t rsv1[2];
	std::uint32_t data[1];
};

struct pio_setup {
	std::uint8_t fis_type;
	std::uint8_t pmport : 4;
	std::uint8_t rsv0 : 1;
	std::uint8_t d : 1;
	std::uint8_t i : 1;
	std::uint8_t rsv1 : 1;
	std::uint8_t status;
	std::uint8_t error;
	std::uint8_t lba0;
	std::uint8_t lba1;
	std::uint8_t lba2;
	std::uint8_t device;
	std::uint8_t lba3;
	std::uint8_t lba4;
	std::uint8_t lba5;
	std::uint8_t rsv2;
	std::uint8_t countl;
	std::uint8_t counth;
	std::uint8_t rsv3;
	std::uint8_t e_status;
	std::uint16_t tc;
	std::uint8_t rsv4[2];
};

struct dma_setup {
	std::uint8_t fis_type;
	std::uint8_t pmport : 4;
	std::uint8_t rsv0 : 1;
	std::uint8_t d : 1;
	std::uint8_t i : 1;
	std::uint8_t a : 1;
	std::uint8_t rsved[2];
	std::uint64_t DMAbufferID;
	std::uint32_t rsvd;
	std::uint32_t DMAbufOffset;
	std::uint32_t TransferCount;
	std::uint32_t resvd;
};

struct dev_bits {
	std::uint8_t fis_type;
	std::uint8_t pmport : 4;
	std::uint8_t rsv0 : 2;
	std::uint8_t i : 1;
	std::uint8_t n : 1;
	std::uint8_t status;
	std::uint8_t error;
	std::uint32_t protocol;
};

struct hba_port {
	std::uint32_t clb;
	std::uint32_t clbu;
	std::uint32_t fb;
	std::uint32_t fbu;
	std::uint32_t is;
	std::uint32_t ie;
	std::uint32_t cmd;
	std::uint32_t rsv0;
	std::uint32_t tfd;
	std::uint32_t sig;
	std::uint32_t ssts;
	std::uint32_t sctl;
	std::uint32_t serr;
	std::uint32_t sact;
	std::uint32_t ci;
	std::uint32_t sntf;
	std::uint32_t fbs;
	std::uint32_t rsv1[11];
	std::uint32_t vendor[4];
};

struct hba_mem {
	std::uint32_t cap;
	std::uint32_t ghc;
	std::uint32_t is;
	std::uint32_t pi;
	std::uint32_t vs;
	std::uint32_t ccc_ctl;
	std::uint32_t ccc_pts;
	std::uint32_t em_loc;
	std::uint32_t em_ctl;
	std::uint32_t cap2;
	std::uint32_t bohc;
	std::uint8_t rsv[0xA0 - 0x2C];
	std::uint8_t vendor[0x100 - 0xA0];
	hba_port ports[1];
};

struct hba_fis {
	dma_setup dsfis;
	std::uint8_t pad0[4];
	pio_setup psfis;
	std::uint8_t pad1[12];
	reg_d2h rfis;
	std::uint8_t pad2[4];
	dev_bits sdbfis;
	std::uint8_t ufis[64];
	std::uint8_t rsv[0x100 - 0xA0];
};

struct hba_prdt_entry {
	std::uint32_t dba;
	std::uint32_t dbau;
	std::uint32_t rsv0;
	std::uint32_t dbc : 22;
	std::uint32_t rsv1 : 9;
	std::uint32_t i : 1;
};

struct hba_cmd_header {
	std::uint8_t cfl : 5;
	std::uint8_t a : 1;
	std::uint8_t w : 1;
	std::uint8_t p : 1;
	std::uint8_t r : 1;
	std::uint8_t b : 1;
	std::uint8_t c : 1;
	std::uint8_t rsv0 : 1;
	std::uint8_t pmp : 4;
	std::uint16_t prdtl;
	volatile std::uint32_t prdbc;
	std::uint32_t ctba;
	std::uint32_t ctbau;
	std::uint32_t rsv1[4];
};

struct hba_cmd_tbl {
	std::uint8_t cfis[64];
	std::uint8_t acmd[16];
	std::uint8_t rsv[48];
	struct hba_prdt_entry prdt_entry[1];
};

class Controller;

struct port_state {
	bool present;
	int port_num;
	volatile hba_port* regs;
	Controller* ahci;
	dma::addr cmd_list;
	dma::addr recv_fis;
	dma::addr cmd_tbl;
	std::uint64_t block_count;
	std::uint32_t block_size;
	block::device bdev;
	std::uint32_t slots_issued;
	std::uint32_t slots_done;
	sync::Spinlock lock;
};

class Controller {
private:
	void find_controller(void);
	enum class MODE {
		ATA,
		IDE
	};

	MODE controller_mode;

	std::uint8_t pci_bus;
	std::uint8_t pci_slot;
	std::uint8_t pci_func;

	std::uint32_t irq_line;

	// ATA
	std::uint64_t controller_address;
	std::uint64_t controller_size;

	// MSI/INTx
	bool use_msi;
	std::uint8_t irq_vector;

	// set by the ISR when a port interrupt arrives while a command is in flight
	volatile bool irq_fired;

	// IDE
	std::uint64_t primary_io_base;
	std::uint64_t primary_ctrl_base;
	std::uint64_t secondary_io_base;
	std::uint64_t secondary_ctrl_base;
	std::uint64_t bus_master_base;

	static constexpr std::uint8_t AHCI_CLASS = 0x01;
	static constexpr std::uint8_t AHCI_ATA_SUBCLASS = 0x06;
	static constexpr std::uint8_t AHCI_IDE_SUBCLASS = 0x01;
	static constexpr std::uint8_t AHCI_PROG_IF = 0x01;

	void bohc_handoff(volatile hba_mem* abar);
	void probe_port(volatile hba_mem* abar);
	int check_type(volatile hba_port* port);
	void port_rebase(volatile hba_port* port, int port_number);
	void start_cmd(volatile hba_port* port);
	void stop_cmd(volatile hba_port* port);
	void reset_controller(volatile hba_mem* abar);

	bool msi_enable(void);
	void intx_setup(void);

public:
	void init(void);
	void irq(void);
	// read one 512-byte sector from a SATA port into buf; blocks until the
	// completion interrupt fires (or a timeout), proving IRQ delivery
	bool read_sector(int port_num, std::uint64_t lba, void* buf);
};

extern Controller controller;

} // namespace ahci

} // namespace storage

} // namespace drivers

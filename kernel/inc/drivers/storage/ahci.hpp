#pragma once

#include <block/block.hpp>
#include <cstdint>
#include <dma/dma.hpp>
#include <pci/pci.hpp>
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

// PxSCTL: serial-ATA control (SCR2)
#define HBA_PxSCTL_DET_MASK 0x0000000F
#define HBA_PxSCTL_DET_RESET 0x00000001 // force COMRESET
#define HBA_PxSCTL_IPM_MASK 0x00000F00
#define HBA_PxSCTL_IPM_NO_PARTIAL 0x00000100
#define HBA_PxSCTL_IPM_NO_SLUMBER 0x00000200

// PxCMD: port command and status
#define HBA_PxCMD_ST 0x00000001
#define HBA_PxCMD_SUD 0x00000002
#define HBA_PxCMD_POD 0x00000004
#define HBA_PxCMD_CLO 0x00000008
#define HBA_PxCMD_FRE 0x00000010
#define HBA_PxCMD_FR 0x00004000
#define HBA_PxCMD_CR 0x00008000
#define HBA_PxCMD_PMA 0x00020000
#define HBA_PxCMD_FBSCP 0x00400000
#define HBA_PxCMD_APSTE 0x00800000
#define HBA_PxCMD_ATAPI 0x01000000
#define HBA_PxCMD_ALPE 0x04000000
#define HBA_PxCMD_ASP 0x08000000
#define HBA_PxCMD_ICC_MASK 0xF0000000
#define HBA_PxCMD_ICC_ACTIVE 0x10000000
#define HBA_PxCMD_ICC_PARTIAL 0x20000000
#define HBA_PxCMD_ICC_SLUMBER 0x60000000
#define HBA_PxCMD_ICC_DEVSLP 0x80000000

// GHC: global host control
#define HBA_GHC_HR 0x00000001
#define HBA_GHC_IE 0x00000002
#define HBA_GHC_AE 0x80000000

// CAP: HBA capabilities (AHCI 1.3.1 section 3.3.1; positions match Linux
// drivers/ata/ahci.h HOST_CAP_* and are validated on real silicon)
#define HBA_CAP_NP_MASK 0x0000001F
#define HBA_CAP_SXS 0x00000020	   // supports external SATA
#define HBA_CAP_EMS 0x00000040	   // enclosure management supported
#define HBA_CAP_CCCS 0x00000080	   // command completion coalescing
#define HBA_CAP_NCS_SHIFT 8
#define HBA_CAP_NCS_MASK 0x00001F00 // bits 12:8, 0-based number of slots
#define HBA_CAP_PSC 0x00002000	   // partial state capable
#define HBA_CAP_SSC 0x00004000	   // slumber state capable
#define HBA_CAP_PMD 0x00008000	   // PIO multiple DRQ block
#define HBA_CAP_FBSS 0x00010000	   // FIS-based switching supported
#define HBA_CAP_SPM 0x00020000	   // port multiplier supported
#define HBA_CAP_SAM 0x00040000	   // supports AHCI mode only
#define HBA_CAP_SCLO 0x01000000	   // command list override supported
#define HBA_CAP_SAL 0x02000000	   // supports activity LED
#define HBA_CAP_SALP 0x04000000	   // aggressive link power management
#define HBA_CAP_SSS 0x08000000	   // staggered spin-up
#define HBA_CAP_SMPS 0x10000000	   // mechanical presence switch
#define HBA_CAP_SSNTF 0x20000000   // SNotification register
#define HBA_CAP_SNCQ 0x40000000	   // native command queuing
#define HBA_CAP_S64A 0x80000000	   // 64-bit addressing

// CAP2: HBA capabilities extended
#define HBA_CAP2_BOH 0x00000001 // BIOS/OS handoff supported
#define HBA_CAP2_SDS 0x00000008 // device sleep supported
#define HBA_CAP2_SADM 0x00000010 // aggressive device sleep management
#define HBA_CAP2_DESO 0x00000020

// BOHC: BIOS/OS handoff control and status
#define HBA_BOHC_BOS 0x00000001
#define HBA_BOHC_OOS 0x00000002
#define HBA_BOHC_OOC 0x00000008
#define HBA_BOHC_BB 0x00000010

// PxIS / PxIE bit layout (PxIE is symmetric with PxIS). Bit positions are per
// AHCI 1.3.1 section 3.3.6 and match the bit map used by QEMU's AHCI model
// (hw/ide/ahci-internal.h enum AHCIPortIRQ).
#define HBA_PxIS_DHRS 0x00000001 // D2H register FIS received
#define HBA_PxIS_PSS 0x00000002  // PIO setup FIS received
#define HBA_PxIS_DSS 0x00000004  // DMA setup FIS received
#define HBA_PxIS_SDBS 0x00000008 // set device bits FIS received
#define HBA_PxIS_UFS 0x00000010  // unknown FIS
#define HBA_PxIS_DPS 0x00000020  // descriptor processed
#define HBA_PxIS_PCS 0x00000040  // port connect change
#define HBA_PxIS_DMPS 0x00000080 // device mechanical presence
#define HBA_PxIS_PRCS 0x00400000 // PhyRdy change
#define HBA_PxIS_IPMS 0x00800000 // incorrect port multiplier status
#define HBA_PxIS_OFS 0x01000000  // overflow
#define HBA_PxIS_INFS 0x04000000 // interface non-fatal error
#define HBA_PxIS_IFS 0x08000000  // interface fatal error
#define HBA_PxIS_HBDS 0x10000000 // host bus data error
#define HBA_PxIS_HBFS 0x20000000 // host bus fatal error
#define HBA_PxIS_TFES 0x40000000 // task file error
#define HBA_PxIS_CPDS 0x80000000 // cold port detect status

// keep the historical names used by the block of code that predates this file
#define HBA_PxIS_D2H HBA_PxIS_DHRS

#define HBA_PxIS_ERROR                                                          \
	(HBA_PxIS_TFES | HBA_PxIS_HBFS | HBA_PxIS_HBDS | HBA_PxIS_IFS |          \
	 HBA_PxIS_INFS | HBA_PxIS_OFS | HBA_PxIS_UFS | HBA_PxIS_IPMS)
#define HBA_PxIS_HOTPLUG                                                       \
	(HBA_PxIS_PCS | HBA_PxIS_PRCS | HBA_PxIS_CPDS)

// PxFBS: FIS-based switching control (AHCI 1.3.1 section 3.3.16)
#define HBA_PxFBS_EN 0x00000001
#define HBA_PxFBS_DEC 0x00000002 // RW1: clear device error condition
#define HBA_PxFBS_SDE 0x00000004 // RO: single device error
#define HBA_PxFBS_DEV_SHIFT 8	  // DEV field, bits 11:8
#define HBA_PxFBS_ADO_SHIFT 12	  // ADO field, bits 15:12
#define HBA_PxFBS_DWE_SHIFT 16	  // DWE field, bits 19:16

// PxDEVSLP: device sleep (AHCI 1.3.1 section 3.3.17)
#define HBA_PxDEVSLP_ADSE 0x00000001
#define HBA_PxDEVSLP_DSP 0x00000002
#define HBA_PxDEVSLP_DETO_SHIFT 2	// bits 9:2
#define HBA_PxDEVSLP_MDAT_SHIFT 10	// bits 14:10
#define HBA_PxDEVSLP_DITO_SHIFT 15	// bits 24:15
#define HBA_PxDEVSLP_DM_SHIFT 25	// bits 28:25

#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_READ_FPDMA 0x60
#define ATA_CMD_WRITE_FPDMA 0x61
#define ATA_CMD_DSM 0x06
#define ATA_CMD_SMART 0xB0
#define ATA_CMD_PACKET 0xA0
#define ATA_CMD_IDENTIFY_DEVICE 0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1

#define ATA_SMART_READ_DATA 0xD0

#define ATAPI_READ10 0x28
#define ATAPI_WRITE10 0x2A
#define ATAPI_READ_CAPACITY10 0x25
#define ATAPI_INQUIRY 0x12

// fixed IDT vector used for the controllers (MSI, MSI-X or INTx). It lives
// inside the device-IRQ window (0x40..0x5F) that the IDT wires up as
// dispatchable gates; all controllers share the vector and the module-level
// handler walks every controller, which also covers shared INTx lines.
static constexpr std::uint8_t DEVICE_IRQ_VECTOR = 0x50;

// timeout for controller-level operations (reset, BOHC handoff)
static constexpr std::uint64_t CONTROLLER_TIMEOUT_US = 1000000;
// timeout for per-port command-list state transitions
static constexpr std::uint64_t PORT_TIMEOUT_US = 100000;
// number of times a command is re-issued before the port is fully reset
static constexpr int MAX_RETRIES = 3;
static constexpr std::uint64_t CMD_TIMEOUT_US = 5000000;

static constexpr int MAX_CONTROLLERS = 8;
static constexpr int MAX_PMP_DEVS = 16; // pmp ports 0..15; 0 = directly attached

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
	std::uint32_t devslp;
	std::uint32_t rsv1[10];
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

struct port_state;

// one logical device (directly attached, or behind a port multiplier). block
// device callbacks carry a pointer to this in dev->priv.
struct device_state {
	bool present;
	int type; // AHCI_DEV_SATA / AHCI_DEV_SATAPI
	port_state* port;
	int pmp; // 0 for a directly attached device, 1..15 behind a PMP
	std::uint64_t block_count;
	std::uint32_t block_size;
	bool ncq; // FPDMA queued supported by both HBA and device
	std::uint32_t queue_depth;
	bool read_only;
	bool trim_supported;
	block::device bdev;
	std::uint8_t model[41];
	std::uint8_t serial[21];
};

// everything a command needs to build its command-table entry
struct issue_desc {
	std::uint64_t lba;
	std::uint32_t sectors; // FIS sector-count field (except NCQ/ATAPI)
	std::uint32_t data_bytes; // bytes to transfer through the PRD table
	dma::addr buf;
	std::uint8_t command;
	std::uint8_t feature;
	bool to_device;
	bool atapi;
	bool ncq;
	const std::uint8_t* packet; // ATAPI packet command, 12 or 16 bytes
	std::uint8_t packet_len;
};

struct port_state {
	bool present;
	int port_num;
	volatile hba_port* regs;
	Controller* ahci;
	dma::addr cmd_list;
	dma::addr recv_fis;
	dma::addr cmd_tbl;
	int num_slots;
	volatile std::uint32_t slots_issued; // non-NCQ commands in flight (CI)
	volatile std::uint32_t sact_issued;	 // NCQ commands in flight (SACT)
	volatile std::uint32_t slots_done;
	volatile bool tfes;
	std::uint32_t last_error; // PxIS bits captured at error time
	std::uint8_t last_err_reg; // ATA error register from the received FIS
	sync::Spinlock lock;
	bool has_pmp;
	bool fbs_enabled;
	device_state devices[MAX_PMP_DEVS];
};

class Controller {
public:
	static constexpr int MAX_PORTS = 32;

private:
	// one constructor-installable PCI identity
	std::uint8_t pci_bus;
	std::uint8_t pci_slot;
	std::uint8_t pci_func;
	std::uint8_t irq_line;
	std::uint32_t irq_vector;

	// MSI/MSI-X state
	bool use_msi;
	bool use_msix;
	dma::addr msix_table; // mapped MSI-X table (if MSI-X in use)

	std::uint64_t controller_address;
	std::uint64_t controller_size;

	// per-port state for all 32 possible ports
	port_state ports[MAX_PORTS];

	// capabilities read from CAP/CAP2 once at init
	bool s64a;
	bool sncq;
	bool salp;
	bool ssc;
	bool spm;
	bool fbss;
	int num_slots;
	bool cap2_boh;
	bool cap2_sds;
	bool cap2_sadm;

	void init_one(const pci::device* device);
	void setup_interrupts(volatile hba_mem* abar);
	bool msi_enable(void);
	bool msix_enable(void);
	void intx_setup(void);

	void bohc_handoff(volatile hba_mem* abar);
	void reset_controller(volatile hba_mem* abar);
	void probe_port(volatile hba_mem* abar);
	int check_type(volatile hba_port* port);
	void port_rebase(port_state* p);
	void start_cmd(volatile hba_port* port);
	void stop_cmd(volatile hba_port* port);
	void port_rearm(port_state* p);
	void port_recover(port_state* p);
	bool port_reset(port_state* p);
	void init_power_management(port_state* p);
	void handle_hotplug(port_state* p);
	void handle_error(port_state* p);

	void register_device(port_state* p, int pmp, int type);
	void register_devices(port_state* p);
	bool identify(port_state* p, int pmp, device_state* d, int type);
	void identify_parse(device_state* d, const std::uint8_t* buf);
	int atapi_capacity(device_state* d, std::uint64_t& blocks,
			   std::uint32_t& block_size);

	int alloc_slot(port_state* p);
	bool issue_command(port_state* p, int pmp, const issue_desc& desc);
	bool issue_device_command(device_state* d, std::uint64_t lba,
				  std::uint32_t sectors, dma::addr buf,
				  bool to_device);
	bool transfer(device_state* d, std::uint64_t lba, std::uint32_t sectors,
		      void* buf, bool to_device);
	bool wait_slot(port_state* p, std::uint32_t mask,
		       std::uint64_t timeout_us);
	port_state* port_for_device(device_state* d);

public:
	Controller();

	void init(void);
	bool irq(void);
	device_state* find_device(int port_num);
	bool read(device_state* d, std::uint64_t lba, std::uint32_t count,
		  void* buf);
	bool write(device_state* d, std::uint64_t lba, std::uint32_t count,
		   const void* buf);
	bool trim(device_state* d, const std::uint64_t* lbas,
		  const std::uint64_t* counts, int n);
	bool smart_read_data(device_state* d, std::uint8_t* out);
	// read one 512-byte sector from a SATA port into buf; blocks until the
	// completion interrupt fires (or a timeout), proving IRQ delivery
	bool read_sector(int port_num, std::uint64_t lba, void* buf);
	// true once init_all() has scanned for and configured at least one
	// controller
	bool initialized(void) const
	{
		return this->controller_address != 0;
	}

	friend void init_all(void);
};

// primary controller (first one found); scan/init everything via init_all()
extern Controller& controller;

void init_all(void);

}

}

}

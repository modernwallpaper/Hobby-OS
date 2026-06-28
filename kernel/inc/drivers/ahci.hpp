#pragma once

#include <cstdint>

namespace drivers
{

namespace storage
{

namespace ahci
{

enum class FIS_TYPE {
	FIS_TYPE_REG_H2D = 0x27,   // Register FIS - host to device
	FIS_TYPE_REG_D2H = 0x34,   // Register FIS - device to host
	FIS_TYPE_DMA_ACT = 0x39,   // DMA activate FIS - device to host
	FIS_TYPE_DMA_SETUP = 0x41, // DMA setup FIS - bidirectional
	FIS_TYPE_DATA = 0x46,	   // Data FIS - bidirectional
	FIS_TYPE_BIST = 0x58,	   // BIST activate FIS - bidirectional
	FIS_TYPE_PIO_SETUP = 0x5F, // PIO setup FIS - device to host
	FIS_TYPE_DEV_BITS = 0xA1,  // Set device bits FIS - device to host
};

struct FIS_REG_H2D {
	// DWORD 0
	std::uint8_t fis_type; // FIS_TYPE_REG_H2D

	std::uint8_t pmport : 4; // Port multiplier
	std::uint8_t rsv0 : 3;	 // Reserved
	std::uint8_t c : 1;	 // 1: Command, 0: Control

	std::uint8_t command;  // Command register
	std::uint8_t featurel; // Feature register, 7:0

	// DWORD 1
	std::uint8_t lba0;   // LBA low register, 7:0
	std::uint8_t lba1;   // LBA mid register, 15:8
	std::uint8_t lba2;   // LBA high register, 23:16
	std::uint8_t device; // Device register

	// DWORD 2
	std::uint8_t lba3;     // LBA register, 31:24
	std::uint8_t lba4;     // LBA register, 39:32
	std::uint8_t lba5;     // LBA register, 47:40
	std::uint8_t featureh; // Feature register, 15:8

	// DWORD 3
	std::uint8_t countl;  // Count register, 7:0
	std::uint8_t counth;  // Count register, 15:8
	std::uint8_t icc;     // Isochronous command completion
	std::uint8_t control; // Control register

	// DWORD 4
	std::uint8_t rsv1[4]; // Reserved
} __attribute__((packed));

class Ahci {
private:
public:
	void init(void);
};

extern Ahci ahci;

} // namespace ahci

} // namespace storage

} // namespace drivers

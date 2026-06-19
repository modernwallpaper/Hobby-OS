### Okay, i dont know what i did; were on commit "what the actual fuck"
So the problem is, that it tripple faults right after waking up AP2. But you would asume that because of the tripple fault,
the system reboots and then tripple faults again at the exact same point, ending up in an infinite loop of crashing and booting, right? WRONG! 
I dont know what the fuck i did, but on the second boot (the on after the tripple fault) it just magically works. HOW? I dont know. Ask a real
systems engineer, not me.

```bash
❯ make run-hdd DEBUG=1 QEMUFLAGS="-accel kvm -cpu host -m 12G -smp 4 -serial stdio"
make -C kernel DEBUG=1
make[1]: Entering directory '/home/jakob/Coding/OS/kernel'
make[1]: Nothing to be done for 'all'.
make[1]: Leaving directory '/home/jakob/Coding/OS/kernel'
rm -f template-x86_64.hdd
dd if=/dev/zero bs=1M count=0 seek=64 of=template-x86_64.hdd
0+0 records in
0+0 records out
0 bytes copied, 2.2911e-05 s, 0.0 kB/s
PATH=$PATH:/usr/sbin:/sbin sgdisk template-x86_64.hdd -n 1:2048 -t 1:ef00 -m 1
Creating new GPT entries in memory.
Warning: The kernel is still using the old partition table.
The new table will be used at the next reboot or after you
run partprobe(8) or kpartx(8)
GPT data structures destroyed! You may now partition the disk using fdisk or
other utilities.
./limine-binary/limine bios-install template-x86_64.hdd
Physical block size of 512 bytes.
No active partition found, some systems may not boot.
Setting partition 1 as active to work around the issue...
Installing to MBR.
Stage 2 to be located at byte offset 0x200.
Reminder: Remember to copy the limine-bios.sys file in either
          the root, /boot, /limine, or /boot/limine directories of
          one of the partitions on the device, or boot will fail!
Limine BIOS stages installed successfully.
mformat -i template-x86_64.hdd@@1M
mmd -i template-x86_64.hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i template-x86_64.hdd@@1M kernel/bin-x86_64/kernel ::/boot
mcopy -i template-x86_64.hdd@@1M limine.conf ::/boot/limine
mcopy -i template-x86_64.hdd@@1M limine-binary/limine-bios.sys ::/boot/limine
mcopy -i template-x86_64.hdd@@1M limine-binary/BOOTX64.EFI ::/EFI/BOOT
mcopy -i template-x86_64.hdd@@1M limine-binary/BOOTIA32.EFI ::/EFI/BOOT
qemu-system-x86_64 \
	-M q35 \
	-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
	-hda template-x86_64.hdd \
	-accel kvm -cpu host -m 12G -smp 4 -serial stdio
WARNING: Image format was not specified for 'template-x86_64.hdd' and probing guessed raw.
         Automatically detecting the format is dangerous for raw images, write operations on block 0 will be restricted.
         Specify the 'raw' format explicitly to remove the restrictions.
BdsDxe: loading Boot0002 "UEFI QEMU HARDDISK QM00001 " from PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x0,0xFFFF,0x0)
BdsDxe: starting Boot0002 "UEFI QEMU HARDDISK QM00001 " from PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x0,0xFFFF,0x0)

[logger.cpp] [void logger::init()] initialized
[main.cpp] [void kmain()] hhdm_offset=0xffff800000000000
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=0; range=0x0000000000200000-0x0000000000800000; space=6_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=1; range=0x0000000000808000-0x000000000080b000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=2; range=0x000000000080c000-0x0000000000812000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=3; range=0x0000000000813000-0x0000000000814000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=4; range=0x0000000001780000-0x000000007bebe000; space=1959_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=5; range=0x000000007bede000-0x000000007e1f6000; space=35_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=6; range=0x000000007e266000-0x000000007e26a000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=7; range=0x000000007e2a9000-0x000000007e2b1000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=8; range=0x000000007e4e1000-0x000000007e4e2000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=9; range=0x000000007fe00000-0x000000007fe05000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=10; range=0x000000007fe0b000-0x000000007fe0c000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=11; range=0x0000000100000000-0x0000000380000000; space=10240_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] amount_pages=~3133555; available_space=12240_mib
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 0; size=   4_kib; blocks=7
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 1; size=   8_kib; blocks=8
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 2; size=  16_kib; blocks=5
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 3; size=  32_kib; blocks=1
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 4; size=  64_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 5; size= 128_kib; blocks=3
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 6; size= 256_kib; blocks=1
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 7; size= 512_kib; blocks=3
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 8; size=1024_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 9; size=2048_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=10; size=4096_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=11; size=8192_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=12; size=16384_kib; blocks=1
[buddy.cpp] [void memory::Buddy::log_stats() const] order=13; size=32768_kib; blocks=3
[buddy.cpp] [void memory::Buddy::log_stats() const] order=14; size=65536_kib; blocks=1
[buddy.cpp] [void memory::Buddy::log_stats() const] order=15; size=131072_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=16; size=262144_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=17; size=524288_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=19; size=2097152_kib; blocks=5
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=0; base=0; limit=0; access=0; granularity=0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=1; base=0; limit=0; access=9a; granularity=a0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=2; base=0; limit=0; access=92; granularity=c0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=3; base=0; limit=0; access=fa; granularity=a0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=4; base=0; limit=0; access=f2; granularity=c0
[gdt.cpp] [void gdt::GDT::set_tss_gate(std::uint32_t, std::uint64_t, std::uint32_t)] num=5; base=0xffffffff800071b0; limit=67
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=5; base=800071b0; limit=67; access=89; granularity=0
[gdt.cpp] [void gdt::GDT::init(std::uint64_t, std::uint8_t*, std::uint64_t, std::uint8_t*, std::uint8_t*, std::uint8_t*, std::uint8_t*)] limit=55
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=0; handler_address=0xffffffff80003f70; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=1; handler_address=0xffffffff80003f79; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=2; handler_address=0xffffffff80003f82; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=3; handler_address=0xffffffff80003f8b; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=4; handler_address=0xffffffff80003f94; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=5; handler_address=0xffffffff80003f9d; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=6; handler_address=0xffffffff80003fa6; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=7; handler_address=0xffffffff80003faf; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=8; handler_address=0xffffffff80003fb8; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=9; handler_address=0xffffffff80003fbf; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=10; handler_address=0xffffffff80003fc8; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=11; handler_address=0xffffffff80003fcf; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=12; handler_address=0xffffffff80003fd6; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=13; handler_address=0xffffffff80003fdd; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=14; handler_address=0xffffffff80003fe4; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=15; handler_address=0xffffffff80003feb; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=16; handler_address=0xffffffff80003ff4; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=17; handler_address=0xffffffff80003ffd; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=18; handler_address=0xffffffff80004004; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=19; handler_address=0xffffffff8000400d; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=20; handler_address=0xffffffff80004016; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=21; handler_address=0xffffffff8000401f; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=22; handler_address=0xffffffff80004026; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=23; handler_address=0xffffffff8000402f; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=24; handler_address=0xffffffff80004038; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=25; handler_address=0xffffffff80004041; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=26; handler_address=0xffffffff8000404a; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=27; handler_address=0xffffffff80004053; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=28; handler_address=0xffffffff8000405c; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=29; handler_address=0xffffffff80004062; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=30; handler_address=0xffffffff80004066; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=31; handler_address=0xffffffff8000406a; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=32; handler_address=0xffffffff8000407f; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=33; handler_address=0xffffffff80004085; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=34; handler_address=0xffffffff8000408b; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=35; handler_address=0xffffffff80004091; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=36; handler_address=0xffffffff80004097; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=37; handler_address=0xffffffff8000409d; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=38; handler_address=0xffffffff800040a3; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=39; handler_address=0xffffffff800040a9; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=40; handler_address=0xffffffff800040af; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=41; handler_address=0xffffffff800040b5; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=42; handler_address=0xffffffff800040bb; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=43; handler_address=0xffffffff800040c1; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=44; handler_address=0xffffffff800040c7; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=45; handler_address=0xffffffff800040cd; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=46; handler_address=0xffffffff800040d3; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=47; handler_address=0xffffffff800040d9; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=48; handler_address=0xffffffff80004079; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=49; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=50; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=51; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=52; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=53; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=54; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=55; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=56; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=57; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=58; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=59; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=60; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=61; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=62; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=63; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=64; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=65; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=66; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=67; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=68; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=69; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=70; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=71; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=72; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=73; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=74; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=75; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=76; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=77; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=78; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=79; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=80; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=81; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=82; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=83; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=84; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=85; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=86; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=87; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=88; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=89; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=90; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=91; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=92; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=93; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=94; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=95; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=96; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=97; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=98; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=99; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=100; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=101; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=102; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=103; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=104; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=105; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=106; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=107; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=108; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=109; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=110; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=111; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=112; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=113; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=114; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=115; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=116; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=117; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=118; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=119; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=120; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=121; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=122; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=123; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=124; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=125; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=126; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=127; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=128; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=129; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=130; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=131; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=132; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=133; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=134; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=135; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=136; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=137; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=138; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=139; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=140; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=141; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=142; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=143; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=144; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=145; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=146; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=147; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=148; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=149; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=150; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=151; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=152; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=153; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=154; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=155; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=156; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=157; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=158; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=159; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=160; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=161; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=162; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=163; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=164; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=165; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=166; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=167; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=168; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=169; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=170; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=171; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=172; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=173; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=174; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=175; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=176; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=177; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=178; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=179; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=180; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=181; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=182; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=183; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=184; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=185; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=186; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=187; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=188; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=189; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=190; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=191; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=192; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=193; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=194; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=195; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=196; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=197; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=198; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=199; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=200; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=201; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=202; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=203; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=204; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=205; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=206; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=207; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=208; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=209; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=210; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=211; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=212; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=213; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=214; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=215; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=216; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=217; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=218; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=219; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=220; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=221; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=222; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=223; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=224; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=225; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=226; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=227; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=228; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=229; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=230; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=231; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=232; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=233; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=234; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=235; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=236; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=237; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=238; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=239; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=240; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=241; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=242; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=243; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=244; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=245; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=246; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=247; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=248; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=249; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=250; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=251; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=252; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=253; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=254; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=255; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::init()] initialized
[slub.cpp] [void memory::SlubAllocator::init()] num_caches=9
[slub.cpp] [void memory::SlubAllocator::init()] cache=0; obj_size=8; objs_per_slab=506
[slub.cpp] [void memory::SlubAllocator::init()] cache=1; obj_size=16; objs_per_slab=253
[slub.cpp] [void memory::SlubAllocator::init()] cache=2; obj_size=32; objs_per_slab=126
[slub.cpp] [void memory::SlubAllocator::init()] cache=3; obj_size=64; objs_per_slab=63
[slub.cpp] [void memory::SlubAllocator::init()] cache=4; obj_size=128; objs_per_slab=31
[slub.cpp] [void memory::SlubAllocator::init()] cache=5; obj_size=256; objs_per_slab=15
[slub.cpp] [void memory::SlubAllocator::init()] cache=6; obj_size=512; objs_per_slab=7
[slub.cpp] [void memory::SlubAllocator::init()] cache=7; obj_size=1024; objs_per_slab=3
[slub.cpp] [void memory::SlubAllocator::init()] cache=8; obj_size=2048; objs_per_slab=1
[acpi.cpp] [void acpi::ACPI::init(void*)] revision=2; oem=%.6s
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_0: apic_id=0x0; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_1: apic_id=0x1; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_2: apic_id=0x2; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_3: apic_id=0x3; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] io_apic_id=0; addr=0xfec00000; gsi=0
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_count=4
[acpi.cpp] [void acpi::ACPI::init(void*)] lapic=0xfee00000; ioapic=0xfec00000; x2apic=no
[hpet.cpp] [void timers::hpet::HPET::init()] base_address=fed00000
[hpet.cpp] [void timers::hpet::HPET::init()] enabled
[pic.hpp] [void interrupts::pic::disable_pic()] disabled
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] IA32_APIC_BASE=0x00000000fee00900
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] base=0xffff8000fee00000; version=0x50014; id=0x0
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] svr=0x000001ff
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] initialized
[ioapic.cpp] [void interrupts::ioapic::IOAPIC::init(std::uint32_t)] id=0x00000000; version=0x00170011; max_entries=23;
[ioapic.cpp] [void interrupts::ioapic::IOAPIC::init(std::uint32_t)] entries_masked=24
[ioapic.cpp] [void interrupts::ioapic::IOAPIC::init(std::uint32_t)] initialzed
[apic.cpp] [void interrupts::apic::APIC::timer_calibrate()] bus_frequency=1001308800; consumed=625818
[apic.cpp] [bool interrupts::apic::APIC::enable_x2apic()] enabling_x2apic
[apic.cpp] [bool interrupts::apic::APIC::enable_x2apic()] x2apic_enabled; lapic_id=0x0
[smp.cpp] [void smp::wake_aps(limine_mp_response*)] cpu_count=4
[smp.cpp] [void smp::wake_aps(limine_mp_response*)] waking_ap_2
[smp.cp
BdsDxe: loading Boot0002 "UEFI QEMU HARDDISK QM00001 " from PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x0,0xFFFF,0x0)
BdsDxe: starting Boot0002 "UEFI QEMU HARDDISK QM00001 " from PciRoot(0x0)/Pci(0x1F,0x2)/Sata(0x0,0xFFFF,0x0)

[logger.cpp] [void logger::init()] initialized
[main.cpp] [void kmain()] hhdm_offset=0xffff800000000000
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=0; range=0x0000000000200000-0x0000000000800000; space=6_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=1; range=0x0000000000808000-0x000000000080b000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=2; range=0x000000000080c000-0x0000000000812000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=3; range=0x0000000000813000-0x0000000000814000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=4; range=0x0000000001780000-0x000000007bebe000; space=1959_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=5; range=0x000000007bede000-0x000000007e1ff000; space=35_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=6; range=0x000000007e26e000-0x000000007e26f000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=7; range=0x000000007e2ae000-0x000000007e2b6000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=8; range=0x000000007fe00000-0x000000007fe05000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=9; range=0x000000007fe0b000-0x000000007fe0c000; space=0_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] buddy_region=10; range=0x0000000100000000-0x0000000380000000; space=10240_mib
[buddy.cpp] [void memory::Buddy::init(limine_memmap_entry**, std::uint64_t)] amount_pages=~3133560; available_space=12240_mib
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 0; size=   4_kib; blocks=6
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 1; size=   8_kib; blocks=7
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 2; size=  16_kib; blocks=5
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 3; size=  32_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 4; size=  64_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 5; size= 128_kib; blocks=3
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 6; size= 256_kib; blocks=1
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 7; size= 512_kib; blocks=3
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 8; size=1024_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order= 9; size=2048_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=10; size=4096_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=11; size=8192_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=12; size=16384_kib; blocks=1
[buddy.cpp] [void memory::Buddy::log_stats() const] order=13; size=32768_kib; blocks=3
[buddy.cpp] [void memory::Buddy::log_stats() const] order=14; size=65536_kib; blocks=1
[buddy.cpp] [void memory::Buddy::log_stats() const] order=15; size=131072_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=16; size=262144_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=17; size=524288_kib; blocks=2
[buddy.cpp] [void memory::Buddy::log_stats() const] order=19; size=2097152_kib; blocks=5
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=0; base=0; limit=0; access=0; granularity=0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=1; base=0; limit=0; access=9a; granularity=a0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=2; base=0; limit=0; access=92; granularity=c0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=3; base=0; limit=0; access=fa; granularity=a0
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=4; base=0; limit=0; access=f2; granularity=c0
[gdt.cpp] [void gdt::GDT::set_tss_gate(std::uint32_t, std::uint64_t, std::uint32_t)] num=5; base=0xffffffff800071b0; limit=67
[gdt.cpp] [void gdt::GDT::set_gate(std::uint32_t, std::uint32_t, std::uint32_t, std::uint8_t, std::uint8_t)] num=5; base=800071b0; limit=67; access=89; granularity=0
[gdt.cpp] [void gdt::GDT::init(std::uint64_t, std::uint8_t*, std::uint64_t, std::uint8_t*, std::uint8_t*, std::uint8_t*, std::uint8_t*)] limit=55
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=0; handler_address=0xffffffff80003f70; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=1; handler_address=0xffffffff80003f79; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=2; handler_address=0xffffffff80003f82; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=3; handler_address=0xffffffff80003f8b; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=4; handler_address=0xffffffff80003f94; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=5; handler_address=0xffffffff80003f9d; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=6; handler_address=0xffffffff80003fa6; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=7; handler_address=0xffffffff80003faf; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=8; handler_address=0xffffffff80003fb8; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=9; handler_address=0xffffffff80003fbf; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=10; handler_address=0xffffffff80003fc8; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=11; handler_address=0xffffffff80003fcf; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=12; handler_address=0xffffffff80003fd6; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=13; handler_address=0xffffffff80003fdd; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=14; handler_address=0xffffffff80003fe4; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=15; handler_address=0xffffffff80003feb; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=16; handler_address=0xffffffff80003ff4; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=17; handler_address=0xffffffff80003ffd; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=18; handler_address=0xffffffff80004004; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=19; handler_address=0xffffffff8000400d; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=20; handler_address=0xffffffff80004016; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=21; handler_address=0xffffffff8000401f; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=22; handler_address=0xffffffff80004026; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=23; handler_address=0xffffffff8000402f; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=24; handler_address=0xffffffff80004038; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=25; handler_address=0xffffffff80004041; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=26; handler_address=0xffffffff8000404a; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=27; handler_address=0xffffffff80004053; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=28; handler_address=0xffffffff8000405c; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=29; handler_address=0xffffffff80004062; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=30; handler_address=0xffffffff80004066; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=31; handler_address=0xffffffff8000406a; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=32; handler_address=0xffffffff8000407f; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=33; handler_address=0xffffffff80004085; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=34; handler_address=0xffffffff8000408b; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=35; handler_address=0xffffffff80004091; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=36; handler_address=0xffffffff80004097; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=37; handler_address=0xffffffff8000409d; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=38; handler_address=0xffffffff800040a3; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=39; handler_address=0xffffffff800040a9; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=40; handler_address=0xffffffff800040af; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=41; handler_address=0xffffffff800040b5; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=42; handler_address=0xffffffff800040bb; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=43; handler_address=0xffffffff800040c1; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=44; handler_address=0xffffffff800040c7; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=45; handler_address=0xffffffff800040cd; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=46; handler_address=0xffffffff800040d3; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=47; handler_address=0xffffffff800040d9; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=48; handler_address=0xffffffff80004079; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=49; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=50; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=51; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=52; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=53; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=54; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=55; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=56; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=57; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=58; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=59; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=60; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=61; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=62; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=63; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=64; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=65; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=66; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=67; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=68; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=69; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=70; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=71; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=72; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=73; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=74; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=75; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=76; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=77; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=78; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=79; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=80; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=81; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=82; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=83; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=84; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=85; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=86; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=87; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=88; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=89; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=90; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=91; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=92; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=93; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=94; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=95; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=96; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=97; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=98; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=99; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=100; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=101; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=102; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=103; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=104; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=105; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=106; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=107; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=108; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=109; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=110; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=111; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=112; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=113; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=114; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=115; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=116; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=117; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=118; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=119; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=120; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=121; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=122; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=123; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=124; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=125; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=126; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=127; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=128; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=129; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=130; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=131; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=132; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=133; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=134; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=135; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=136; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=137; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=138; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=139; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=140; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=141; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=142; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=143; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=144; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=145; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=146; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=147; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=148; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=149; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=150; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=151; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=152; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=153; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=154; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=155; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=156; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=157; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=158; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=159; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=160; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=161; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=162; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=163; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=164; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=165; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=166; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=167; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=168; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=169; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=170; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=171; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=172; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=173; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=174; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=175; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=176; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=177; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=178; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=179; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=180; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=181; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=182; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=183; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=184; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=185; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=186; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=187; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=188; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=189; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=190; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=191; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=192; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=193; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=194; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=195; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=196; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=197; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=198; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=199; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=200; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=201; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=202; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=203; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=204; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=205; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=206; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=207; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=208; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=209; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=210; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=211; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=212; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=213; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=214; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=215; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=216; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=217; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=218; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=219; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=220; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=221; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=222; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=223; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=224; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=225; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=226; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=227; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=228; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=229; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=230; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=231; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=232; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=233; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=234; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=235; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=236; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=237; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=238; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=239; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=240; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=241; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=242; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=243; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=244; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=245; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=246; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=247; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=248; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=249; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=250; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=251; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=252; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=253; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=254; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::set_gate(int, void*, std::uint8_t)] n=255; handler_address=0xffffffff80004070; ist=0
[idt.cpp] [void interrupts::idt::IDT::init()] initialized
[slub.cpp] [void memory::SlubAllocator::init()] num_caches=9
[slub.cpp] [void memory::SlubAllocator::init()] cache=0; obj_size=8; objs_per_slab=506
[slub.cpp] [void memory::SlubAllocator::init()] cache=1; obj_size=16; objs_per_slab=253
[slub.cpp] [void memory::SlubAllocator::init()] cache=2; obj_size=32; objs_per_slab=126
[slub.cpp] [void memory::SlubAllocator::init()] cache=3; obj_size=64; objs_per_slab=63
[slub.cpp] [void memory::SlubAllocator::init()] cache=4; obj_size=128; objs_per_slab=31
[slub.cpp] [void memory::SlubAllocator::init()] cache=5; obj_size=256; objs_per_slab=15
[slub.cpp] [void memory::SlubAllocator::init()] cache=6; obj_size=512; objs_per_slab=7
[slub.cpp] [void memory::SlubAllocator::init()] cache=7; obj_size=1024; objs_per_slab=3
[slub.cpp] [void memory::SlubAllocator::init()] cache=8; obj_size=2048; objs_per_slab=1
[acpi.cpp] [void acpi::ACPI::init(void*)] revision=2; oem=%.6s
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_0: apic_id=0x0; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_1: apic_id=0x1; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_2: apic_id=0x2; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_3: apic_id=0x3; flags=0x1
[acpi.cpp] [void acpi::ACPI::init(void*)] io_apic_id=0; addr=0xfec00000; gsi=0
[acpi.cpp] [void acpi::ACPI::init(void*)] cpu_count=4
[acpi.cpp] [void acpi::ACPI::init(void*)] lapic=0xfee00000; ioapic=0xfec00000; x2apic=no
[hpet.cpp] [void timers::hpet::HPET::init()] base_address=fed00000
[hpet.cpp] [void timers::hpet::HPET::init()] enabled
[pic.hpp] [void interrupts::pic::disable_pic()] disabled
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] IA32_APIC_BASE=0x00000000fee00900
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] base=0xffff8000fee00000; version=0x50014; id=0x0
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] svr=0x000001ff
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] initialized
[ioapic.cpp] [void interrupts::ioapic::IOAPIC::init(std::uint32_t)] id=0x00000000; version=0x00170011; max_entries=23;
[ioapic.cpp] [void interrupts::ioapic::IOAPIC::init(std::uint32_t)] entries_masked=24
[ioapic.cpp] [void interrupts::ioapic::IOAPIC::init(std::uint32_t)] initialzed
[apic.cpp] [void interrupts::apic::APIC::timer_calibrate()] bus_frequency=1001641600; consumed=626026
[apic.cpp] [bool interrupts::apic::APIC::enable_x2apic()] enabling_x2apic
[apic.cpp] [bool interrupts::apic::APIC::enable_x2apic()] x2apic_enabled; lapic_id=0x0
[smp.cpp] [void smp::wake_aps(limine_mp_response*)] cpu_count=4
[smp.cpp] [void smp::wake_aps(limine_mp_response*)] waking_ap_2
[smp.cpp] [void smp::wake_aps(limine_mp_response*)] woken_ap_2
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] IA32_APIC_BASE=0x00000000fee00800
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] base=0xffff8000fee00000; version=0x50014; id=0x2000000
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] svr=0x000001ff
[apic.cpp] [void interrupts::apic::APIC::init(std::uint32_t)] initialized
[apic.cpp] [bool interrupts::apic::APIC::enable_x2apic()] enabling_x2apic
[apic.cpp] [bool interrupts::apic::APIC::enable_x2apic()] x2apic_enabled; lapic_id=0x2
[smp.cpp] [void smp::ap_entry(limine_mp_info*)] ap=2; apic_id=0x2; online=true
[smp.cpp] [void smp::wake_aps(limine_mp_response*)] online=2; count=4
[main.cpp] [void kmain()] OH MY FUCKING GOD WE DID NOT TRIPPLE FAULT
```

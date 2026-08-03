  - Scheduler locking and interrupt-state handling need extra care. tick() takes a global scheduler lock from timer interrupt context, while parts of the scheduler call enqueue() under the same lock path.
    That kind of thing can become deadlock-prone as the scheduler grows.

  - The allocators are useful but still fragile. SLUB metadata lives in slab pages, and kfree() detects large allocations by reading page-base magic. That can work, but it needs very strict invariants,
    poisoning, double-free checks, and alignment validation.

  - PCI/AHCI is currently QEMU-friendly more than hardware-ready. That’s normal, but real hardware will punish assumptions around BAR sizing, MSI/MSI-X, IRQ routing, cache coherency, DMA buffers, and
    timeouts.

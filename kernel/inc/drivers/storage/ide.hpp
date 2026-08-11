#pragma once

namespace drivers
{

namespace storage
{

namespace ide
{

// probe legacy (class 0x01 / subclass 0x01) IDE controllers and register any
// found drives with the block layer using programmed I/O. called as a
// fallback when no AHCI controller is present.
void init(void);

}

}

}

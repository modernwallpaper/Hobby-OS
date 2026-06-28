#pragma once

namespace drivers
{

namespace storage
{

namespace ahci
{

class Ahci {
private:
public:
	void init(void);
};

extern Ahci ahci;

} // namespace ahci

} // namespace storage

} // namespace drivers

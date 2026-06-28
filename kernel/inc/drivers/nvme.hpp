#pragma once

namespace drivers
{

namespace storage
{

namespace nvme
{

class Nvme {
private:
public:
	void init(void);
};

extern Nvme nvme;

} // namespace nvme

} // namespace storage

} // namespace drivers

#pragma once

namespace tests
{

void run_all(void);

class Test {
public:
	virtual ~Test() = default;
	virtual const char* name() const = 0;
	virtual bool run() = 0;
};

} // namespace tests

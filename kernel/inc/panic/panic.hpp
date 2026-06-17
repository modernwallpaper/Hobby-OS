#pragma once

#include <logging/logger.hpp>

#define PANIC(fmt, ...)                                                        \
	do                                                                     \
	{                                                                      \
		logger::printf("[PANIC] [%s] [%s] " fmt "\n", __FILENAME__,    \
			       __PRETTY_FUNCTION__, ##__VA_ARGS__);            \
		for (;;)                                                       \
			asm volatile("hlt");                                   \
	} while (0)

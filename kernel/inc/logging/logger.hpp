#pragma once
#include <cstdarg>

#define PORT_COM 0x3F8

#define __FILENAME__                                                           \
	(__builtin_strrchr(__FILE__, '/')                                      \
	     ? __builtin_strrchr(__FILE__, '/') + 1                            \
	     : __FILE__)
#define LOG(fmt, ...)                                                          \
	logger::logf(__FILENAME__, __PRETTY_FUNCTION__, fmt, ##__VA_ARGS__)

namespace logger
{
/**
 * COM Port initialization
 */
void init();

/**
 * Formatted loggging with filename and function name
 * @param module        filename
 * @param func          function name
 * @param fmt           String to format
 * @param ...           Args
 */
void logf(const char* module, const char* func, const char* fmt, ...);

/**
 * Formatted loggging
 * @param fmt           String to format
 * @param ...           Args
 */
void printf(const char* fmt, ...);
}

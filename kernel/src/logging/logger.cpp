#include <logging/logger.hpp>
#include <ports/ports.hpp>
#include <sync/spinlock.hpp>

void put(char c)
{
	ports::outb(PORT_COM, static_cast<std::uint8_t>(c));
}

void puts(const char* s)
{
	while (*s)
		put(*s++);
}

void print_dec(std::uint64_t val, int base, int width, char pad)
{
	char buf[64];
	char* ptr = buf + sizeof(buf);
	*--ptr = '\0';
	int len = 0;
	if (val == 0)
	{
		*--ptr = '0';
		++len;
	}
	while (val > 0)
	{
		int digit = val % base;
		*--ptr = digit < 10 ? '0' + digit : 'a' + digit - 10;
		++len;
		val /= base;
	}
	while (len < width)
	{
		*--ptr = pad;
		++len;
	}
	puts(ptr);
}

void vprintf(const char* fmt, std::va_list args)
{
	for (; *fmt; ++fmt)
	{
		if (*fmt != '%')
		{
			put(*fmt);
			continue;
		}
		++fmt;
		int width = 0;
		char pad = ' ';
		if (*fmt == '0')
		{
			pad = '0';
			++fmt;
		}
		while (*fmt >= '0' && *fmt <= '9')
		{
			width = width * 10 + (*fmt - '0');
			++fmt;
		}
		if (width > 60)
			width = 60;
		int long_count = 0;
		while (*fmt == 'l')
		{
			++long_count;
			++fmt;
		}
		switch (*fmt)
		{
		case 's': {
			const char* s = va_arg(args, const char*);
			if (!s)
				s = "(null)";
			int len = 0;
			for (const char* p = s; *p; ++p)
				++len;
			for (int i = len; i < width; ++i)
				put(pad);
			puts(s);
			break;
		}
		case 'd':
		case 'i': {
			std::uint64_t raw = va_arg(args, std::uint64_t);
			std::int64_t val = long_count
					       ? static_cast<std::int64_t>(raw)
					       : static_cast<std::int32_t>(raw);
			std::uint64_t mag;
			if (val < 0)
			{
				put('-');
				mag =
				    static_cast<std::uint64_t>(-(val + 1)) + 1;
			}
			else
			{
				mag = static_cast<std::uint64_t>(val);
			}
			print_dec(mag, 10, width, pad);
			break;
		}
		case 'u': {
			print_dec(va_arg(args, std::uint64_t), 10, width, pad);
			break;
		}
		case 'x': {
			print_dec(va_arg(args, std::uint64_t), 16, width, pad);
			break;
		}
		case 'p': {
			puts("0x");
			print_dec(va_arg(args, std::uintptr_t), 16, width, pad);
			break;
		}
		case 'c': {
			put(static_cast<char>(va_arg(args, int)));
			break;
		}
		case '%': {
			put('%');
			break;
		}
		default: {
			put('%');
			put(*fmt);
			break;
		}
		}
	}
}

namespace logger
{
static sync::IrqSpinlock log_lock;
void init()
{
	ports::outb(PORT_COM + 1, 0x00); // deactivate interrupts
	ports::outb(PORT_COM + 3, 0x80); // activate DLAB
	ports::outb(PORT_COM + 0, 0x01); // Divisor low
	ports::outb(PORT_COM + 1, 0x00); // Divisor high
	ports::outb(PORT_COM + 3, 0x03); // 8 bit, no parity
	ports::outb(PORT_COM + 2, 0xC7); // activate FIFO
	ports::outb(PORT_COM + 4, 0x0B); // set DTR/RTS
	for (int i = 0; i < 5; ++i)
		puts("\n"); // Skip Qemu logs
	LOG("initialized");
}

void printf(const char* fmt, ...)
{
	std::uint64_t flags;
	log_lock.lock_save(flags);
	std::va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	log_lock.unlock_restore(flags);
}

void logf(const char* module, const char* func, const char* fmt, ...)
{
	std::uint64_t flags;
	log_lock.lock_save(flags);
	put('[');
	puts(module);
	puts("] [");
	puts(func);
	put(']');
	put(' ');
	std::va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
	put('\n');
	log_lock.unlock_restore(flags);
}

}

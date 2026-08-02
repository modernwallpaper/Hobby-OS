#include <cstddef>

void operator delete(void*) noexcept
{
}

void operator delete(void*, std::size_t) noexcept
{
}

void operator delete[](void*) noexcept
{
}

void operator delete[](void*, std::size_t) noexcept
{
}

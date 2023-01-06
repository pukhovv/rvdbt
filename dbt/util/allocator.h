#pragma once

#include <sys/mman.h>

namespace dbt
{

void *host_mmap(void *addr, size_t len, int prot, int flags, int fd, __off_t offset);

} // namespace dbt

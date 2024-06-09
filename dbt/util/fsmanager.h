#pragma once

#include <mutex>
#include <optional>
#include <utility>

namespace dbt::fsmanager
{

void Init(char const *cache_path);
void Destroy();

enum class CacheState {
	NO_FILE,
	RDONLY,
	RDWR_OLD,
	RDWR_NEW,
};

char const *CacheStateToStr(CacheState s);

std::pair<void *, CacheState> OpenCacheFile(char const *path, size_t size, bool wmode);

extern std::recursive_mutex dbtfslk;
#define DBT_FS_LOCK() std::lock_guard lk(dbt::fsmanager::dbtfslk)

void *CreateDumpFile(char const *path, size_t size);

} // namespace dbt::fsmanager

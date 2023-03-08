#pragma once

#include "dbt/arena.h"
#include "dbt/mmu.h"
#include "dbt/tcache/tcache.h"
#include "dbt/util/logger.h"

#include <array>
#include <bitset>
#include <map>

#include "md5.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "unistd.h"

namespace dbt
{
LOG_STREAM(prof);

// Set it before ukernel chroots
void InitCacheDir(char const *path);
static std::string g_dbt_cache_dir;

struct FileChecksum {
	uint8_t data[16]{};
	bool operator==(FileChecksum const &) const = default;

	static FileChecksum FromFile(int fd);

	std::string ToString() const
	{
		return MakeHexStr(data, sizeof(data));
	}
};

struct profile_storage {
	struct FilePageData {
		inline static constexpr u32 po2idx(u32 ip)
		{
			return ip >> 2; // insn size
		}

		using PageBitset = std::bitset<(mmu::PAGE_SIZE >> 2)>;

		u32 pageno{}; // currently it's vaddr
		PageBitset executed{};
		PageBitset brind{};
	} __attribute__((packed));

	struct FileHeader {
		FileChecksum csum{};
		u32 n_pages{};
		FilePageData pages[];
	} __attribute__((packed));

	struct ElfProfile {
		std::map<u32, u32> page2idx;
		FileHeader *fmap{};
		size_t fsize{};
	};

	static void OpenProfile(int elf_fd);

	static void Destroy();

	// Walk all pages in tcache
	static void UpdateProfile();

	// Walk specific page
	// static void UpdatePageProfile(u32 vaddr);

private:
	profile_storage() = delete;

	// Walk tcache until page boundary
	static tcache::MapType::iterator UpdatePageProfile(tcache::MapType::iterator it);

	static u32 AllocatePageData(u32 pageno);
	static FilePageData *GetOrCreatePageData(u32 pageno);

	// Resolution method depends on ukernel elf loader capabilities
	// Current implementation requires only one ElfProfile
	static ElfProfile elf_prof;
};

} // namespace dbt

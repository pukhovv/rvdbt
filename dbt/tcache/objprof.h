#pragma once

#include "dbt/arena.h"
#include "dbt/mmu.h"
#include "dbt/tcache/tcache.h"
#include "dbt/util/logger.h"

#include <array>
#include <bitset>
#include <map>
#include <span>

#include <openssl/evp.h>

#include "sys/stat.h"
#include "sys/types.h"
#include "unistd.h"

namespace dbt
{
LOG_STREAM(prof);

struct FileChecksum {
	uint8_t data[16]{};
	bool operator==(FileChecksum const &) const = default;

	static FileChecksum FromFile(int fd);

	std::string ToString() const
	{
		return MakeHexStr(data, sizeof(data));
	}
};

struct objprof {
	struct PageData {
		static constexpr uint idx_bits = 2; // insn size

		static constexpr u32 po2idx(u32 ip)
		{
			return ip >> idx_bits;
		}

		static constexpr u32 idx2po(u32 idx)
		{
			return idx << idx_bits;
		}

		using PageBitset = std::bitset<(mmu::PAGE_SIZE >> idx_bits)>;

		u32 pageno{}; // currently it's vaddr
		PageBitset executed{};
		PageBitset brind_target{};
		PageBitset segment_entry{};
	} __attribute__((packed));

	// Set it before ukernel chroots
	static void Init(char const *cache_path, bool use_aot_);

	static void Announce(int elf_fd, bool jit_mode);
	static void Destroy();

	// Access profile data
	static bool HasProfile();
	static std::span<const PageData> const GetProfile();

	// Walk all pages in tcache
	static void UpdateProfile();

	// Walk specific page
	// static void UpdatePageProfile(u32 vaddr);

	// For clients: get associated file path
	static std::string GetCachePath(char const *extension);

private:
	objprof() = delete;

	struct FileHeader {
		FileChecksum csum{};
		u32 n_pages{};
		PageData pages[];
	} __attribute__((packed));

	struct ElfProfile {
		std::map<u32, u32> page2idx;
		FileHeader *fmap{};
		size_t fsize{};
	};

	// Walk tcache until page boundary
	static tcache::MapType::iterator UpdatePageProfile(tcache::MapType::iterator it);

	static u32 AllocatePageData(u32 pageno);
	static PageData *GetOrCreatePageData(u32 pageno);

	// Resolution method depends on ukernel elf loader capabilities
	// Current implementation requires only one ElfProfile
	static ElfProfile elf_prof;

	static bool use_aot_files;
};

} // namespace dbt

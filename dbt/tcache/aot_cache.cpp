#include "dbt/tcache/aot_cache.h"
#include <fcntl.h>

namespace dbt
{

FileChecksum FileChecksum::FromFile(int fd)
{
	struct stat st;
	int res;
	if (res = fstat(fd, &st); res < 0) {
		Panic();
	}

	void *fmap;
	if (fmap = dbt::host_mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0); fmap == MAP_FAILED) {
		Panic();
	}

	MD5_CTX ctx;
	MD5Init(&ctx);
	MD5Update(&ctx, (uint8_t const *)fmap, st.st_size);
	FileChecksum sum;
	MD5Final(sum.data, &ctx);
	return sum;
}

void InitCacheDir(char const *path)
{
	char buf[PATH_MAX];
	if (!realpath(path, buf)) {
		Panic(std::string("failed to resolve ") + path);
	}
	g_dbt_cache_dir = std::string(buf) + "/";
}

static std::string MakeCachePath(FileChecksum const &csum, char const *extension)
{
	return g_dbt_cache_dir + csum.ToString() + extension;
}

profile_storage::ElfProfile profile_storage::elf_prof{};

void profile_storage::OpenProfile(int elf_fd)
{
	auto csum = FileChecksum::FromFile(elf_fd);
	auto path = MakeCachePath(csum, ".prof");

	auto &pfile = elf_prof;
	int rc, fd;
	bool new_file = false;

	log_prof("OpenProfile at %s", path.c_str());
	if (fd = open(path.c_str(), O_RDWR, 0666); fd < 0) {
		if (errno != ENOENT) {
			Panic("failed to open " + path);
		}
		new_file = true;
	}
	pfile.fsize = 64_MB;

	if (new_file) {
		if (fd = open(path.c_str(), O_RDWR | O_CREAT, 0666); fd < 0) {
			Panic("failed to creat " + path);
		}
		if (rc = ftruncate(fd, pfile.fsize); rc != 0) {
			Panic();
		}
	}

	void *fmap = host_mmap(NULL, pfile.fsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fmap == MAP_FAILED) {
		Panic();
	}
	close(fd);
	pfile.fmap = (FileHeader *)fmap;

	if (new_file) {
		pfile.fmap->csum = csum;
		pfile.fmap->n_pages = 0;
	} else {
		if (pfile.fmap->csum != csum) {
			Panic("bad checksum " + path);
		}
		for (u32 idx = 0; idx < pfile.fmap->n_pages; ++idx) {
			auto pageno = pfile.fmap->pages[idx].pageno;
			log_prof("Found PageData for pageno=%u", pageno);
			pfile.page2idx.insert({pageno, idx});
		}
	}
}

void profile_storage::Destroy()
{
	auto &pfile = elf_prof;
	int rc;

	if (!elf_prof.fmap) {
		return;
	}
	if (rc = munmap(pfile.fmap, pfile.fsize); rc != 0) {
		Panic();
	}
}

u32 profile_storage::AllocatePageData(u32 pageno)
{
	auto &pfile = elf_prof;
	auto fmap = pfile.fmap;
	auto idx = fmap->n_pages++;
	if ((uptr)&fmap->pages[fmap->n_pages] - (uptr)fmap > pfile.fsize) {
		Panic();
	}
	auto page_data = &fmap->pages[idx];
	page_data->pageno = pageno;
	return idx;
}

profile_storage::FilePageData *profile_storage::GetOrCreatePageData(u32 pageno)
{
	auto &pfile = elf_prof;
	auto it = pfile.page2idx.insert_or_assign(pageno, 0);
	if (it.second) {
		it.first->second = AllocatePageData(pageno);
	}
	return &pfile.fmap->pages[it.first->second];
}

void profile_storage::UpdateProfile()
{
	auto &tmap = tcache::tcache_map;

	for (auto it = tmap.begin(); it != tmap.end();) {
		it = UpdatePageProfile(it);
	}
}

tcache::MapType::iterator profile_storage::UpdatePageProfile(tcache::MapType::iterator it)
{
	u32 const pageno = it->first >> mmu::PAGE_BITS;
	auto *const page_data = GetOrCreatePageData(pageno);
	log_prof("Update PageData for pageno=%u", pageno);

	for (; it->first >> mmu::PAGE_BITS == pageno; ++it) {
		auto tb = it->second;
		auto po_idx = FilePageData::po2idx(it->first & ~mmu::PAGE_MASK);
		page_data->executed.set(po_idx, true);
		page_data->brind.set(po_idx, tb->flags.is_brind_target);
	}
	return it;
}

} // namespace dbt

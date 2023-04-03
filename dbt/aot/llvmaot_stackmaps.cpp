#include "dbt/aot/aot.h"
#include "dbt/aot/aot_module.h"

#include "dbt/qmc/qcg/jitabi.h"
#include "elfio/elfio.hpp"
namespace elfio = ELFIO;

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
}

namespace dbt
{
LOG_STREAM(aot)

struct Stackmap {
	// llvm.stackmap Header
	struct Header {
		u8 ver;
		u8 _res1;
		u16 _res2;
		u32 num_functions;
		u32 num_constants;
		u32 num_maps;
	} __attribute__((packed));

	// llvm.stackmap StkSizeRecord[NumFunctions]
	struct FuncRecord {
		u64 fn_addr;
		u64 stk_size;
		u64 map_count;
	} __attribute__((packed));

	// llvm.stackmap Constants[NumConstants]
	struct Constant {
		u64 large_const;
	} __attribute__((packed));

	// llvm.stackmap StkMapRecord[NumRecords]
	// Padding (only if required to align to 8 byte)
	struct MapRecord {
		// llvm.stackmap Location[NumLocations]
		struct LocationsRecord {
			struct Location {
				u8 type;
				u8 _res1;
				u16 loc_size;
				u16 dwarf_regn;
				u16 _res2;
				i32 data;
			} __attribute__((packed));

			u16 num_locs;
			Location locs[];
		} __attribute__((packed));

		// llvm.stackmap LiveOuts[NumLiveOuts]
		// Padding (only if required to align to 8 byte)
		struct LiveOutsRecord {
			struct LiveOut {
				u16 dwarf_regn;
				u8 _res;
				u8 size;
			} __attribute__((packed));

			u16 _pad;
			u16 num_liveouts;
			LiveOut liveouts[];
		} __attribute__((packed));

		LocationsRecord *GetLocationsRecord() const
		{
			return (LocationsRecord *)((uptr)this + sizeof(MapRecord));
		}

		LiveOutsRecord *GetLiveOutsRecord() const
		{
			auto locsr = GetLocationsRecord();
			return (LiveOutsRecord *)roundup((uptr)&locsr->locs[locsr->num_locs], 8);
		}

		MapRecord *GetNext() const
		{
			auto livsr = GetLiveOutsRecord();
			return (MapRecord *)roundup((uptr)&livsr->liveouts[livsr->num_liveouts], 8);
		}

	public:
		u64 patchpoint_id;
		u32 instr_offs;
		u16 _res1;
	} __attribute__((packed));

	Header *GetHeader() const
	{
		return reinterpret_cast<Header *>(data);
	}

	FuncRecord *GetFuncRecord(u32 idx) const
	{
		return &func_records[idx];
	}

	MapRecord *GetFirstMapRecord() const
	{
		return first_map;
	}

	Stackmap(u8 *data_) : data(data_)
	{
		auto header = GetHeader();
		assert(header->ver == 3);
		func_records = (FuncRecord *)(data + sizeof(Header));
		constants = (Constant *)(func_records + header->num_functions);
		first_map = (MapRecord *)(constants + header->num_constants);
	}

private:
	u8 *data{};
	FuncRecord *func_records{};
	Constant *constants{};
	MapRecord *first_map{};
};

void ProcessLLVMStackmaps(std::vector<AOTSymbol> &aot_symbols)
{
	auto obj_path = objprof::GetCachePath(AOT_O_EXTENSION);

	elfio::elfio elf;
	if (!elf.load(obj_path)) {
		log_aot("no such file: %s", obj_path.c_str());
	}

	elfio::section *sym_sec = nullptr;
	elfio::section *stackmaps_sec = nullptr;
	elfio::section *rela_stackmaps_sec = nullptr;

	for (const auto &section : elf.sections) {
		auto test_sec = [&section](elfio::section **res, elfio::Elf_Word type, char const *name) {
			log_aot("test section %s", section->get_name().c_str());
			if (section->get_type() == type &&
			    std::string(section->get_name()) == std::string(name)) {
				*res = section.get();
			}
		};
		test_sec(&sym_sec, elfio::SHT_SYMTAB, ".symtab");
		test_sec(&stackmaps_sec, elfio::SHT_PROGBITS, ".llvm_stackmaps");
		test_sec(&rela_stackmaps_sec, elfio::SHT_RELA, ".rela.llvm_stackmaps");
	}
	if (!stackmaps_sec || !rela_stackmaps_sec) {
		Panic("stackmaps section not found");
	}

	int fd = open(obj_path.c_str(), O_RDWR);
	if (fd < 0) {
		Panic();
	}
	struct stat st;
	if (fstat(fd, &st) < 0) {
		Panic();
	}
	void *fmap = host_mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fmap == MAP_FAILED) {
		Panic();
	}

	{
		// Resolve addresses in .rela.llvm_stackmaps
		auto syma = (elfio::Elf64_Sym *)sym_sec->get_data();
		auto rela = (elfio::Elf64_Rela *)rela_stackmaps_sec->get_data();
		auto rela_num = rela_stackmaps_sec->get_size() / sizeof(rela[0]);
		for (elfio::Elf_Xword i = 0; i < rela_num; ++i) {
			auto r = &rela[i];
			if (ELF64_R_TYPE(r->r_info) != elfio::R_X86_64_64) {
				Panic("unexpected stackmap relocation");
			}
			auto s = &syma[ELF64_R_SYM(r->r_info)];
			elfio::Elf_Xword calc =
			    elf.sections[s->st_shndx]->get_offset() + s->st_value + r->r_addend;
			log_aot("apply rela: %016x %016x", r->r_offset, calc);
			*(u64 *)(stackmaps_sec->get_offset() + (uptr)fmap + r->r_offset) = calc;
		}
	}

	auto stkmap = Stackmap((u8 *)fmap + stackmaps_sec->get_offset());
	auto hdr = stkmap.GetHeader();
	auto map = stkmap.GetFirstMapRecord();
	for (u32 fn_idx = 0; fn_idx < hdr->num_functions; fn_idx++) {
		auto fn_rec = stkmap.GetFuncRecord(fn_idx);
		log_aot("stackmap func: %016x", fn_rec->fn_addr);

		for (u32 idx = 0; idx < fn_rec->map_count; ++idx) {
			auto pp_offs = fn_rec->fn_addr + map->instr_offs;
			log_aot("patchpoint: %u %016x", map->patchpoint_id, pp_offs);
			*(std::array<u8, 3> *)((uptr)fmap + pp_offs) = {0x4c, 0x89, 0xe4};
			auto slot = (jitabi::ppoint::BranchSlot *)((uptr)fmap + pp_offs + 3);
			slot->LinkLazyAOT(offsetof(CPUState, stub_tab));
			slot->gip = map->patchpoint_id;
			assert(slot->gip = map->patchpoint_id);
			map = map->GetNext();
		}
	}

	munmap(fmap, st.st_size);
	close(fd);
}

} // namespace dbt

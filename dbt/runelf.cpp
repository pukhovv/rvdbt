#include "dbt/core.h"
#include "dbt/guest/rv32_cpu.h"
#include "dbt/tcache/tcache.h"
#include "dbt/ukernel.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "usage: runelf <elf-path>\n";
		return 1;
	}

	dbt::mmu::Init();
	dbt::ukernel::ElfImage elf;
	dbt::ukernel::LoadElf(argv[1], &elf);
	dbt::CPUState state{};
	dbt::ukernel::InitThread(&state, &elf);
	state.ip = elf.entry;

	dbt::tcache::Init();
	dbt::ukernel::Execute(&state);
	return 0;
}

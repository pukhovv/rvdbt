#include "dbt/core.h"
#include "dbt/guest/rv32_cpu.h"
#include "dbt/tcache/tcache.h"
#include "dbt/ukernel.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: runelf <elf-path>\n");
		return 1;
	}
	dbt::Logger::enable("ukernel");

	dbt::mmu::Init();
	dbt::ukernel uk{};
	dbt::ukernel::ElfImage elf;
	uk.LoadElf(argv[1], &elf);
#ifdef CONFIG_LINUX_GUEST
	uk.InitAVectors(&elf);
#endif

	dbt::CPUState state{};
	dbt::ukernel::InitThread(&state, &elf);
	state.ip = elf.entry;

	dbt::tcache::Init();
	uk.Execute(&state);
	return 0;
}

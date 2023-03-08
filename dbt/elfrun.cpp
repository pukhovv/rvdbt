#include "dbt/guest/rv32_cpu.h"
#include "dbt/tcache/aot_cache.h"
#include "dbt/tcache/tcache.h"
#include "dbt/ukernel.h"
#include <boost/any.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <iostream>

namespace bpo = boost::program_options;

int main(int argc, char **argv)
{
	int dbt_argc = -1;
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--")) {
			dbt_argc = i;
			break;
		}
	}
	if (dbt_argc == -1) {
		std::cout << "args must contain \"--\"\n";
		return 1;
	}
	int guest_argc = argc - (dbt_argc + 1);
	char **guest_argv = argv + (dbt_argc + 1);
	if (guest_argc < 1) {
		std::cout << "empty guest args\n";
		return 1;
	}

	bpo::options_description adesc("options");
	adesc.add_options()("help", "help")("logs", bpo::value<std::string>())(
	    "fsroot", bpo::value<std::string>()->required(), "isolated path for emulated process")(
	    "cache", bpo::value<std::string>()->required(), "directory for dbt cache files");
	bpo::variables_map adesc_vm;
	bpo::store(bpo::parse_command_line(dbt_argc, argv, adesc), adesc_vm);
	bpo::notify(adesc_vm);
	if (adesc_vm.count("help")) {
		std::cout << adesc << "\n";
		return 0;
	}
	if (adesc_vm.count("logs")) {
		auto const *logs = boost::unsafe_any_cast<std::string>(&adesc_vm["logs"].value());
		boost::char_separator sep(":");
		boost::tokenizer tok(*logs, sep);
		for (auto const &e : tok) {
			dbt::Logger::enable(e.c_str());
		}
	}
	std::string fsroot = *boost::unsafe_any_cast<std::string>(&adesc_vm["fsroot"].value());
	std::string cachedir = *boost::unsafe_any_cast<std::string>(&adesc_vm["cache"].value());

	dbt::InitCacheDir(cachedir.c_str());

	dbt::mmu::Init();
	dbt::tcache::Init();
	dbt::ukernel uk{};
	uk.SetFSRoot(fsroot.c_str());
	dbt::ukernel::ElfImage elf;
	{
		std::string elf_path = fsroot + '/' + guest_argv[0];
		uk.LoadElf(elf_path.c_str(), &elf);
	}
#ifdef CONFIG_LINUX_GUEST
	uk.InitAVectors(&elf, guest_argc, guest_argv);
#endif

	dbt::CPUState state{};
	dbt::ukernel::InitThread(&state, &elf);
	state.ip = elf.entry;
	uk.Execute(&state);

	dbt::profile_storage::UpdateProfile();
	dbt::profile_storage::Destroy();

#ifndef NDEBUG
	dbt::tcache::Destroy();
	dbt::mmu::Destroy();
#endif
	return 0;
}

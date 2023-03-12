#include "dbt/aot/aot.h"
#include "dbt/guest/rv32_cpu.h"
#include "dbt/tcache/objprof.h"
#include "dbt/tcache/tcache.h"
#include "dbt/ukernel.h"
#include <boost/any.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <iostream>

namespace bpo = boost::program_options;

int main(int argc, char **argv)
{
	bpo::options_description adesc("options");
	adesc.add_options()("help", "help")("logs", bpo::value<std::string>())(
	    "cache", bpo::value<std::string>()->required(),
	    "directory for dbt cache files")("elf", bpo::value<std::string>()->required(), "elf aot target");
	bpo::variables_map adesc_vm;
	bpo::store(bpo::parse_command_line(argc, argv, adesc), adesc_vm);
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
	std::string cachedir = *boost::unsafe_any_cast<std::string>(&adesc_vm["cache"].value());
	std::string elfpath = *boost::unsafe_any_cast<std::string>(&adesc_vm["elf"].value());

	dbt::objprof::Init(cachedir.c_str(), false);
	dbt::mmu::Init();

	auto elf = &dbt::ukernel::exe_elf_image;
	dbt::ukernel::ReproduceElf(elfpath.c_str(), elf);

	dbt::AOTCompileElf();

#ifndef NDEBUG
	dbt::objprof::Destroy();
	dbt::mmu::Destroy();
#endif
	return 0;
}

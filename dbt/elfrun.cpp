#include "dbt/guest/rv32_cpu.h"
#include "dbt/tcache/objprof.h"
#include "dbt/tcache/tcache.h"
#include "dbt/ukernel.h"
#include <boost/any.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <iostream>

namespace bpo = boost::program_options;

struct ElfRunOptions {
	std::span<char *> guest_args{};

	std::string fsroot{};
	std::string cache{};
	bool use_aot{};
	std::string logs{};
};

static std::pair<std::span<char *>, std::span<char *>> SplitArgs(unsigned argc, char **argv)
{
	std::span<char *> args{argv, argc};

	std::optional<unsigned> split_pos;
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--")) {
			split_pos = i;
			break;
		}
	}
	if (!split_pos.has_value()) {
		return {args, {}};
	}

	auto dbt_args = args.subspan(0, split_pos.value());
	auto guest_args = args.subspan(dbt_args.size() + 1);
	return {dbt_args, guest_args};
}

static void PrintHelp(bpo::options_description &adesc)
{
	std::cout << "usage: [options] -- [guest argv]\n";
	std::cout << adesc << "\n";
}

static bool ParseOptions(ElfRunOptions &o, int argc, char **argv)
{
	assert(argc > 0);
	auto [dbt_args, guest_args] = SplitArgs(static_cast<unsigned>(argc), argv);
	o.guest_args = guest_args;

	bpo::options_description adesc("options");
	// clang-format off
	adesc.add_options()
	    ("help",   "help")
	    ("logs",   bpo::value(&o.logs)->default_value(""), "enabled log streams separated by :")
	    ("fsroot", bpo::value(&o.fsroot)->required(), "isolated path for emulated process")
	    ("cache",  bpo::value(&o.cache)->required(), "dbt cache path")
	    ("aot",    bpo::value(&o.use_aot)->default_value(false), "boot aot file if available");
	// clang-format on

	try {
		bpo::variables_map vmap;
		bpo::store(bpo::parse_command_line(dbt_args.size(), dbt_args.data(), adesc), vmap);
		if (vmap.count("help")) {
			PrintHelp(adesc);
			return false;
		}
		if (guest_args.empty()) {
			std::cout << "Invalid arguments\n";
			PrintHelp(adesc);
			return false;
		}
		bpo::notify(vmap);
	} catch (std::exception &e) {
		std::cerr << "Bad options: " << e.what() << "\n";
		PrintHelp(adesc);
		return false;
	}
	return true;
}

static void SetupLogger(std::string const &logopt)
{
	boost::char_separator sep(":");
	boost::tokenizer tok(logopt, sep);
	for (auto const &e : tok) {
		dbt::Logger::enable(e.c_str());
	}
}

int main(int argc, char **argv)
{
	ElfRunOptions opts;
	if (!ParseOptions(opts, argc, argv)) {
		return 1;
	}
	auto gargs = opts.guest_args;

	SetupLogger(opts.logs);

	dbt::objprof::Init(opts.cache.c_str(), opts.use_aot);
	dbt::mmu::Init();
	dbt::tcache::Init();

	dbt::ukernel::SetFSRoot(opts.fsroot.c_str());
	dbt::ukernel::MainThreadBoot(static_cast<int>(gargs.size()), gargs.data());
	int guest_rc = dbt::ukernel::MainThreadExecute();

	dbt::objprof::UpdateProfile();

	if constexpr (dbt::config::debug) {
		dbt::objprof::Destroy();
		dbt::tcache::Destroy();
		dbt::mmu::Destroy();
	}
	return guest_rc;
}

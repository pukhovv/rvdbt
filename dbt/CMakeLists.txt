add_subdirectory(third_party)

add_library(dbtstatic STATIC
	util/logger.cpp

	arena.cpp
	mmu.cpp
	execute.cpp
	ukernel.cpp

	qjit/qcompile.cpp
	qjit/qir_opt.cpp
	qjit/qir_printer.cpp
	qjit/qjit_stubs.cpp
	qjit/qcg.cpp
	qjit/qemit.cpp
	qjit/qra.cpp
	tcache/tcache.cpp

	guest/rv32_insn.cpp
	guest/rv32_interp.cpp
	guest/rv32_qir.cpp
)
target_include_directories(dbtstatic PUBLIC "${PROJECT_SOURCE_DIR}")

target_include_directories(dbtstatic SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/dbt/third_party/asmjit/src")

add_executable(runelf runelf.cpp)
target_link_libraries(runelf PUBLIC dbtstatic asmjit::asmjit ${Boost_LIBRARIES})
target_include_directories(runelf PUBLIC "${PROJECT_SOURCE_DIR}")

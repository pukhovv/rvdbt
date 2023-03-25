add_subdirectory(third_party)

add_library(dbtstatic STATIC
	util/common.cpp
	util/logger.cpp

	aot/aot.cpp
	aot/aot_module.cpp
	aot/aot_file.cpp
	aot/aot_boot.cpp
	arena.cpp
	mmu.cpp
	execute.cpp
	ukernel.cpp

	tcache/tcache.cpp
	tcache/objprof.cpp

	qmc/compile.cpp
	qmc/qir.cpp
	qmc/qir_opt.cpp
	qmc/qir_printer.cpp
	qmc/runtime_stubs.cpp
	qmc/qcg/arch_traits.cpp
	qmc/qcg/jitabi.cpp
	qmc/qcg/qcg.cpp
	qmc/qcg/qemit.cpp
	qmc/qcg/qra.cpp
	qmc/qcg/qsel.cpp

	guest/rv32_analyser.cpp
	guest/rv32_insn.cpp
	guest/rv32_interp.cpp
	guest/rv32_qir.cpp
)
target_include_directories(dbtstatic PUBLIC "${PROJECT_SOURCE_DIR}")

target_include_directories(dbtstatic SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/dbt/third_party/asmjit/src")
target_link_libraries(dbtstatic PUBLIC asmjit::asmjit ${Boost_LIBRARIES} m md)

target_include_directories(dbtstatic SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/dbt/third_party/elfio")

add_executable(elfrun elfrun.cpp)
target_include_directories(elfrun PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(elfrun PUBLIC dbtstatic)

add_executable(elfaot elfaot.cpp)
target_include_directories(elfaot PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(elfaot PUBLIC dbtstatic)

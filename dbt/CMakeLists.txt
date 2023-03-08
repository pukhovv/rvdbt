add_subdirectory(third_party)

add_library(dbtstatic STATIC
	util/common.cpp
	util/logger.cpp

	arena.cpp
	mmu.cpp
	execute.cpp
	ukernel.cpp

	tcache/tcache.cpp
	tcache/aot_cache.cpp

	qjit/qcompile.cpp
	qjit/qir.cpp
	qjit/qir_opt.cpp
	qjit/qir_printer.cpp
	qjit/runtime_stubs.cpp
	qjit/qcg/arch_traits.cpp
	qjit/qcg/jitabi.cpp
	qjit/qcg/qcg.cpp
	qjit/qcg/qemit.cpp
	qjit/qcg/qra.cpp
	qjit/qcg/qsel.cpp

	guest/rv32_insn.cpp
	guest/rv32_interp.cpp
	guest/rv32_qir.cpp
)
target_include_directories(dbtstatic PUBLIC "${PROJECT_SOURCE_DIR}")

target_include_directories(dbtstatic SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/dbt/third_party/asmjit/src")
target_link_libraries(dbtstatic PUBLIC asmjit::asmjit ${Boost_LIBRARIES} m md)

add_executable(elfrun elfrun.cpp)
target_include_directories(elfrun PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(elfrun PUBLIC dbtstatic)

add_executable(elfaot elfaot.cpp)
target_include_directories(elfaot PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(elfaot PUBLIC dbtstatic)

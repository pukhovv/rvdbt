add_subdirectory(third_party)

add_library(dbtstatic STATIC
	arena.cpp
	core.cpp
	execute.cpp
	logger.cpp
	ukernel.cpp

	qjit/qjit.cpp
	qjit/regalloc.cpp
	tcache/tcache.cpp

	guest/rv32_insn.cpp
	guest/rv32_interp.cpp
	guest/rv32_qjit.cpp
)
target_include_directories(dbtstatic PUBLIC "${PROJECT_SOURCE_DIR}")

target_include_directories(dbtstatic SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/dbt/third_party/asmjit/src")

add_executable(runelf runelf.cpp)
target_link_libraries(runelf PUBLIC dbtstatic asmjit::asmjit ${Boost_LIBRARIES})
target_include_directories(runelf PUBLIC "${PROJECT_SOURCE_DIR}")

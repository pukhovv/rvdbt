add_subdirectory(third_party)

add_library(dbtstatic STATIC
	arena.cpp
	core.cpp
	execute.cpp
	regalloc.cpp
	translate.cpp
	ukernel.cpp
	rv32i_decode.cpp
	rv32i_build.cpp
	rv32i_interp.cpp
)
target_include_directories(dbtstatic PUBLIC "${PROJECT_SOURCE_DIR}")

target_include_directories(dbtstatic SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/dbt/third_party/asmjit/src")

#add_executable(tests tests.cpp)
#target_link_libraries(tests dbtstatic asmjit::asmjit)
#target_include_directories(tests PUBLIC "${PROJECT_SOURCE_DIR}")

add_executable(runelf runelf.cpp)
target_link_libraries(runelf dbtstatic asmjit::asmjit)
target_include_directories(runelf PUBLIC "${PROJECT_SOURCE_DIR}")

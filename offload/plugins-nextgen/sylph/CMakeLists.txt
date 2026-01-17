add_target_library(omptarget.rtl.sylph SYLPH)

target_sources(omptarget.rtl.sylph PRIVATE src/rtl.cpp)

if ((OMPT_TARGET_DEFAULT) AND (LIBOMPTARGET_OMPT_SUPPORT))
  target_link_libraries(omptarget.rtl.sylph PRIVATE OMPT)
endif()

find_package(MPI REQUIRED)
target_link_libraries(omptarget.rtl.sylph PRIVATE MPI::MPI_CXX)

target_include_directories(omptarget.rtl.sylph PRIVATE dynamic_sylph)
target_sources(omptarget.rtl.sylph PRIVATE dynamic_sylph/sylph.cpp dynamic_sylph/trace.o)
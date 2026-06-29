if(WELDER_BUILD_PYBIND11)
  if(WELDER_BUILD_MODULE)
    add_subdirectory(python_poc) # consumes `import welder;`
  endif()
  add_subdirectory(python_poc_headeronly) # consumes welder header-only
endif()

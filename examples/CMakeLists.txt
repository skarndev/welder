if(WELDER_BUILD_PYTHON)
  if(WELDER_BUILD_MODULE)
    add_subdirectory(python_poc) # consumes `import welder;`
  endif()
  add_subdirectory(python_poc_headeronly) # consumes welder header-only
endif()

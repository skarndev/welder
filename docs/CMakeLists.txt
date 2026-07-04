# The documentation site: a mkdocs-material guide + a Doxygen C++ reference,
# presented as one site. Two toolchains, cleanly separated:
#
#   * Doxygen renders src/welder/** (through welder's    -> docs/content/api/
#     own INPUT_FILTER, themed with doxygen-awesome-css)   (generated; gitignored)
#   * mkdocs-material renders docs/content/*.md + copies -> <bin>/site/
#     the reference above as static files                    (guide + /api/)
#
# Doxygen writes into content/api (inside docs_dir) so mkdocs serves the reference
# in both `build` and `serve` — hence it runs before mkdocs. Both run out of an
# isolated uv environment (docs/pyproject.toml), mirroring tests/. Everything is
# opt-in behind WELDER_BUILD_DOCS and never touches the library build. Targets:
#
#   welder-docs        build the whole site (guide + reference) into <bin>/site
#   welder-docs-serve  serve the docs (guide live-reloads; reference is a snapshot)

find_program(WELDER_DOXYGEN doxygen)
find_program(WELDER_DOCS_UV uv)
find_program(WELDER_DOCS_GIT git)

if(NOT WELDER_DOCS_UV)
  message(WARNING "welder: 'uv' not found; docs targets skipped.")
  return()
endif()
if(NOT WELDER_DOXYGEN)
  message(WARNING "welder: 'doxygen' not found; docs targets skipped.")
  return()
endif()

set(_docs_src ${CMAKE_CURRENT_SOURCE_DIR})
set(_docs_bin ${CMAKE_CURRENT_BINARY_DIR})
set(_site ${_docs_bin}/site)
set(_filter ${CMAKE_SOURCE_DIR}/tools/welder_doxygen_filter.py)

# --- 1. Provision the docs environment (mkdocs-material + lark) via uv --------
# uv's managed CPython (like tests/), so the interpreter is predictable and has
# both mkdocs and lark (the latter for the Doxygen INPUT_FILTER).
message(STATUS "welder: syncing uv docs environment...")
execute_process(
  COMMAND ${WELDER_DOCS_UV} sync --project ${_docs_src}
          --python 3.14 --python-preference only-managed
  RESULT_VARIABLE _uv_res
  OUTPUT_VARIABLE _uv_out ERROR_VARIABLE _uv_out)
if(NOT _uv_res EQUAL 0)
  message(WARNING "welder: 'uv sync' failed; docs targets skipped:\n${_uv_out}")
  return()
endif()
set(WELDER_DOCS_PYTHON ${_docs_src}/.venv/bin/python)
set(WELDER_FILTER_PYTHON ${WELDER_DOCS_PYTHON})
set(WELDER_FILTER ${_filter})
set(WELDER_SRC ${CMAKE_SOURCE_DIR}/src/welder)

# --- 2. Fetch doxygen-awesome-css (the modern theme), gracefully optional -----
# A plain asset repo; we clone it ourselves (rather than FetchContent) so a
# network failure degrades to Doxygen's stock theme instead of aborting configure.
# v2.4.2 supports Doxygen's new page-nav navigation (Doxygen 1.11+); older tags
# style only the legacy #nav-tree and leave the real 1.17 sidebar unthemed. The
# version is in the dir name so a bump re-clones into a fresh directory.
set(_awesome_ver v2.4.2)
set(_awesome_dir ${_docs_bin}/doxygen-awesome-css-${_awesome_ver})
set(_awesome_ok FALSE)
if(NOT EXISTS ${_awesome_dir}/doxygen-awesome.css AND WELDER_DOCS_GIT)
  message(STATUS "welder: fetching doxygen-awesome-css ${_awesome_ver}...")
  execute_process(
    COMMAND ${WELDER_DOCS_GIT} clone --depth 1 --branch ${_awesome_ver}
            https://github.com/jothepro/doxygen-awesome-css.git ${_awesome_dir}
    RESULT_VARIABLE _awesome_res OUTPUT_QUIET ERROR_VARIABLE _awesome_err)
  if(NOT _awesome_res EQUAL 0)
    message(WARNING "welder: could not fetch doxygen-awesome-css (using stock "
                    "Doxygen theme):\n${_awesome_err}")
  endif()
endif()
if(EXISTS ${_awesome_dir}/doxygen-awesome.css)
  set(_awesome_ok TRUE)
endif()

# --- 3. Theme wiring: stylesheets, extension JS, and a patched header --------
set(DOXYGEN_HTML_HEADER "")
set(DOXYGEN_HTML_EXTRA_STYLESHEET "")
set(DOXYGEN_HTML_EXTRA_FILES "")
if(_awesome_ok)
  # Base theme + our spark-palette tweaks (loaded last, so it wins). We do NOT use
  # the sidebar-only add-on: with Doxygen's new page-nav the base theme already is
  # a sidebar layout, and sidebar-only targets the legacy #nav-tree.
  # Doxygen accepts a space-separated list on one line (paths have no spaces here).
  set(DOXYGEN_HTML_EXTRA_STYLESHEET
      "${_awesome_dir}/doxygen-awesome.css \
${_docs_src}/doxygen-extra.css")
  # No doxygen-awesome JS: all its extensions are init()-based and need jQuery,
  # which Doxygen 1.17 no longer ships. We manage dark mode ourselves in the header
  # (patch_doxygen_header.py) against the theme's `html.dark-mode` CSS.
  set(DOXYGEN_HTML_EXTRA_FILES "")

  # Generate Doxygen's default header, then inject the extension scripts. Any
  # failure here leaves DOXYGEN_HTML_HEADER empty -> stock header + base theme.
  execute_process(
    COMMAND ${WELDER_DOXYGEN} -w html
            ${_docs_bin}/header.generated.html
            ${_docs_bin}/footer.generated.html
            ${_docs_bin}/style.generated.css
    WORKING_DIRECTORY ${_docs_bin}
    RESULT_VARIABLE _hdr_res OUTPUT_QUIET ERROR_QUIET)
  if(_hdr_res EQUAL 0)
    execute_process(
      COMMAND ${WELDER_DOCS_PYTHON} ${_docs_src}/patch_doxygen_header.py
              ${_docs_bin}/header.generated.html ${_docs_bin}/header.html
      RESULT_VARIABLE _patch_res)
    if(_patch_res EQUAL 0 AND EXISTS ${_docs_bin}/header.html)
      set(DOXYGEN_HTML_HEADER ${_docs_bin}/header.html)
    endif()
  endif()
endif()

# --- 4. Graphviz (nice class/collaboration graphs) is optional ---------------
find_program(WELDER_DOT dot)
if(WELDER_DOT)
  set(DOXYGEN_HAVE_DOT YES)
else()
  set(DOXYGEN_HAVE_DOT NO)
endif()

# --- 5. Configure the Doxyfile ----------------------------------------------
# Doxygen writes into content/api (inside mkdocs' docs_dir), so mkdocs picks the
# reference up as static files in *both* `build` and `serve` — no dead link when
# serving. content/api is generated (gitignored); Doxygen must therefore run
# *before* mkdocs so those files exist to copy.
set(_docs_content ${_docs_src}/content)
set(DOXYGEN_OUTPUT_DIR ${_docs_content})   # HTML_OUTPUT = api -> content/api
set(DOXYGEN_MAINPAGE ${_docs_src}/api_mainpage.md)
set(WELDER_LOGO ${_docs_src}/welder-logo.svg)
configure_file(${_docs_src}/Doxyfile.in ${_docs_bin}/Doxyfile @ONLY)

# --- 6. Targets --------------------------------------------------------------
# welder-docs: Doxygen reference into content/api *first*, then mkdocs build,
# which copies content/api into the site alongside the guide (order matters, so
# both are steps of one target).
add_custom_target(welder-docs
  COMMAND ${CMAKE_COMMAND} -E echo "welder: building Doxygen reference -> ${_docs_content}/api"
  COMMAND ${WELDER_DOXYGEN} ${_docs_bin}/Doxyfile
  COMMAND ${CMAKE_COMMAND} -E echo "welder: building mkdocs site (guide + reference) -> ${_site}"
  COMMAND ${WELDER_DOCS_PYTHON} -m mkdocs build
          --config-file ${_docs_src}/mkdocs.yml --site-dir ${_site} --clean
  WORKING_DIRECTORY ${_docs_bin}
  COMMENT "welder: documentation site -> ${_site}/index.html"
  USES_TERMINAL VERBATIM)

# Live-reload the guide while writing prose. The Doxygen reference is built once
# up front (into content/api) so its link resolves while serving; it is a static
# snapshot (rerun the target to refresh it).
add_custom_target(welder-docs-serve
  COMMAND ${CMAKE_COMMAND} -E echo "welder: building Doxygen reference -> ${_docs_content}/api"
  COMMAND ${WELDER_DOXYGEN} ${_docs_bin}/Doxyfile
  COMMAND ${WELDER_DOCS_PYTHON} -m mkdocs serve --config-file ${_docs_src}/mkdocs.yml
  WORKING_DIRECTORY ${_docs_bin}
  COMMENT "welder: serving the docs at http://127.0.0.1:8000/ (guide live-reloads)"
  USES_TERMINAL VERBATIM)

message(STATUS "welder: docs targets ready — build 'welder-docs' (theme: "
        "${_awesome_ok}=doxygen-awesome, dot=${DOXYGEN_HAVE_DOT}).")

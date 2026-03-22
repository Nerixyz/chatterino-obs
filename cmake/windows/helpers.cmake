# CMake Windows helper functions module

include_guard(GLOBAL)

include(helpers_common)

include(arch)

function(_win_deploy_shared_libs target)
  set(_bin_dir "${OpenSSL_DIR}/bin")
  if(NOT EXISTS "${_bin_dir}")
    message(FATAL_ERROR "Missing conan binary directory. Expected at '${_bin_dir}'")
  endif()
  message(STATUS "Using conan bin directory '${_bin_dir}'")

  file(GLOB _conan_bin_files "${_bin_dir}/*.dll")

  install(FILES ${_conan_bin_files} DESTINATION "${target}/bin/64bit")

  # Qt6_DIR = qt-dir/lib/cmake/Qt6
  find_package(Qt6)
  set(_qt_plugin_dir "${Qt6_DIR}/../../../plugins")
  cmake_path(NORMAL_PATH _qt_plugin_dir)
  if(NOT EXISTS "${_qt_plugin_dir}")
    message(FATAL_ERROR "Missing Qt plugin directory directory. Expected at '${_qt_plugin_dir}'")
  endif()
  message(STATUS "Qt plugin directory '${_qt_plugin_dir}'")
  set(_schannel_base "${_qt_plugin_dir}/tls/qschannelbackend")
  if(NOT EXISTS "${_schannel_base}.dll")
    message(FATAL_ERROR "Missing SChannel plugin. Expected at '${_schannel_base}.dll'")
  endif()
  if(EXISTS "${_qt_plugin_dir}/tls/qopensslbackend.dll")
    set(_ossl_backend "\"${_qt_plugin_dir}/tls/qopensslbackend$<$<CONFIG:Debug>:d>.dll\"")
  else()
    set(_ossl_backend "")
  endif()

  install(
    FILES
      "${_schannel_base}$<$<CONFIG:Debug>:d>.dll"
      "${_qt_plugin_dir}/tls/qcertonlybackend$<$<CONFIG:Debug>:d>.dll"
      ${_ossl_backend}
    DESTINATION ${target}/data
  )
endfunction()

# set_target_properties_plugin: Set target properties for use in obs-studio
function(set_target_properties_plugin target)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs PROPERTIES)
  cmake_parse_arguments(PARSE_ARGV 0 _STPO "${options}" "${oneValueArgs}" "${multiValueArgs}")

  message(DEBUG "Setting additional properties for target ${target}...")

  while(_STPO_PROPERTIES)
    list(POP_FRONT _STPO_PROPERTIES key value)
    set_property(TARGET ${target} PROPERTY ${key} "${value}")
  endwhile()

  string(TIMESTAMP CURRENT_YEAR "%Y")

  set_target_properties(${target} PROPERTIES VERSION 0 SOVERSION ${PLUGIN_VERSION})

  install(TARGETS ${target} RUNTIME DESTINATION "${target}/bin/64bit" LIBRARY DESTINATION "${target}/bin/64bit")

  install(
    FILES "$<TARGET_PDB_FILE:${target}>"
    CONFIGURATIONS RelWithDebInfo Debug Release
    DESTINATION "${target}/bin/64bit"
    OPTIONAL
  )

  if(TARGET plugin-support)
    target_link_libraries(${target} PRIVATE plugin-support)
  endif()

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target}>"
      "$<$<CONFIG:Debug,RelWithDebInfo,Release>:$<TARGET_PDB_FILE:${target}>>"
      "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>"
    COMMENT "Copy ${target} to rundir"
    VERBATIM
  )

  target_install_resources(${target})
  _win_deploy_shared_libs(${target})

  get_target_property(target_sources ${target} SOURCES)
  set(target_ui_files ${target_sources})
  list(FILTER target_ui_files INCLUDE REGEX ".+\\.(ui|qrc)")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "UI Files" FILES ${target_ui_files})

  configure_file(cmake/windows/resources/resource.rc.in "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc")
  configure_file(cmake/windows/resources/installer.iss.in "${CMAKE_CURRENT_BINARY_DIR}/installer.iss")
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.rc")
endfunction()

# Helper function to add resources into bundle
function(target_install_resources target)
  message(DEBUG "Installing resources for target ${target}...")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    file(GLOB_RECURSE data_files "${CMAKE_CURRENT_SOURCE_DIR}/data/*")
    foreach(data_file IN LISTS data_files)
      cmake_path(
        RELATIVE_PATH
        data_file
        BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
        OUTPUT_VARIABLE relative_path
      )
      cmake_path(GET relative_path PARENT_PATH relative_path)
      target_sources(${target} PRIVATE "${data_file}")
      source_group("Resources/${relative_path}" FILES "${data_file}")
    endforeach()

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION "${target}/data" USE_SOURCE_PERMISSIONS)

    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
      COMMAND
        "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
      COMMENT "Copy ${target} resources to rundir"
      VERBATIM
    )
  endif()
endfunction()

# Helper function to add a specific resource to a bundle
function(target_add_resource target resource)
  message(DEBUG "Add resource '${resource}' to target ${target} at destination '${target_destination}'...")

  install(FILES "${resource}" DESTINATION "${target}/data" COMPONENT Runtime)

  add_custom_command(
    TARGET ${target}
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${resource}" "${CMAKE_CURRENT_BINARY_DIR}/rundir/$<CONFIG>/${target}"
    COMMENT "Copy ${target} resource ${resource} to rundir"
    VERBATIM
  )
  source_group("Resources" FILES "${resource}")
endfunction()

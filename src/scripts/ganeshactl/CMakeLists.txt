# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
# Building and packaging DBUS scripts for ganesha

# Python3 was tested for in top level

# PyQT based class modules
set(GANESHA_BASE_SRCS
  Ganesha/__init__.py
  Ganesha/admin.py
  Ganesha/client_mgr.py
  Ganesha/QtUI/clients_table.py
  Ganesha/export_mgr.py
  Ganesha/QtUI/exports_table.py
  Ganesha/log_mgr.py
  Ganesha/QtUI/log_settings.py
  Ganesha/io_stats.py
  Ganesha/glib_dbus_stats.py
  Ganesha/ganesha_mgr_utils.py
  Ganesha/config_editor.py
  )

# Command line scripts
set(SCRIPT_SRC
  fake_recall.py
  get_clientids.py
  grace_period.py
  ganesha_mgr.py
  ganesha_stats.py
  ganesha_conf.py
  ganesha_logrotate_mgr.py
  )

set(GUI_SCRIPT_SRC
  ganeshactl.py
  ganesha-admin.py
  manage_clients.py
  manage_logger.py
  manage_exports.py
  )

# Qt Designer files that get compiled into .py scripts
set(UI_SRC
  Ganesha/QtUI/ui_main_window.ui
  Ganesha/QtUI/ui_log_dialog.ui
  )

if(Python3_FOUND)
  set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
  set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
  set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/build/ganeshactl_timestamp")

  # Always depend on the Ganesha package modules (stats, mgr, etc.).
  # Previously these were only listed when USE_GUI_ADMIN_TOOLS was ON, so
  # edits to e.g. glib_dbus_stats.py never rebuilt the wheel and ninja
  # install kept reinstalling a stale ganeshactl-*.whl.
  set(GANESHA_SRCS ${GANESHA_BASE_SRCS})
  set(UI_PY_FILES)
  if(USE_GUI_ADMIN_TOOLS)
    foreach(ui_file ${UI_SRC})
      string(REPLACE ".ui" ".py" py_file ${ui_file})
      add_custom_command(
        OUTPUT ${py_file}
        COMMAND mkdir -p build/lib/Ganesha/QtUI
        COMMAND ${PYUIC} "${CMAKE_CURRENT_SOURCE_DIR}/${ui_file}" -o "build/lib/${py_file}"
        DEPENDS ${ui_file}
        )
      set(UI_PY_FILES ${UI_PY_FILES} "${py_file}")
      list(APPEND GANESHA_SRCS ${py_file})
    endforeach()
  endif()

  # Generate rules to copy command line scripts from src to build,
  # stripping .py along the way
  set(SCRIPTS)
  foreach(src_file ${SCRIPT_SRC})
    string(REPLACE ".py" "" script_file ${src_file})
    add_custom_command(
      OUTPUT ${script_file}
      COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}" ${script_file}
      DEPENDS ${src_file}
      )
    list(APPEND SCRIPTS ${script_file})
  endforeach()

  if(USE_GUI_ADMIN_TOOLS)
    foreach(src_file ${GUI_SCRIPT_SRC})
      string(REPLACE ".py" "" script_file ${src_file})
      add_custom_command(
        OUTPUT ${script_file}
        COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}" ${script_file}
        DEPENDS ${src_file}
        )
      list(APPEND SCRIPTS ${script_file})
    endforeach()
  endif()

  # Build up the string for the configure substitution in setup.py
  set(SCRIPTS_STRING)
  foreach(script_py ${SCRIPT_SRC})
    string(REPLACE ".py" "" script ${script_py})
    if("${SCRIPTS_STRING}" STREQUAL "")
      set(SCRIPTS_STRING "'${script}'")
    else()
      set(SCRIPTS_STRING "${SCRIPTS_STRING}, '${script}'")
    endif()
  endforeach()

  if(USE_GUI_ADMIN_TOOLS)
    foreach(script_py ${GUI_SCRIPT_SRC})
      string(REPLACE ".py" "" script ${script_py})
      if("${SCRIPTS_STRING}" STREQUAL "")
        set(SCRIPTS_STRING "'${script}'")
      else()
        set(SCRIPTS_STRING "${SCRIPTS_STRING}, '${script}'")
      endif()
    endforeach()
  endif()

  configure_file(${SETUP_PY_IN} ${SETUP_PY})

  if(USE_LEGACY_PYTHON_INSTALL)
    add_custom_command(
      OUTPUT ${OUTPUT}
      COMMAND ${Python3_EXECUTABLE} "${SETUP_PY}"  build
      COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
      DEPENDS ${GANESHA_SRCS} ${UI_PY_FILES} ${SCRIPTS}
      )
  else()
    add_custom_command(
      OUTPUT ${OUTPUT}
      COMMAND ${Python3_EXECUTABLE}  -m build --wheel --no-isolation .
      COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
      DEPENDS ${GANESHA_SRCS} ${UI_PY_FILES} ${SCRIPTS}
      )
  endif()

  add_custom_target(python_ganeshactl ALL DEPENDS ${OUTPUT})

  if(USE_LEGACY_PYTHON_INSTALL)
    install(
      CODE
      "execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} install --skip-build --no-compile --prefix=\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX})"
      )
  else()
    # python -m installer refuses to overwrite; remove prior install first
    set(_ganeshactl_site
      "${CMAKE_INSTALL_PREFIX}/lib/python${Python3_VERSION_MAJOR}.${Python3_VERSION_MINOR}/site-packages")
    set(_ganeshactl_wheel
      "dist/ganeshactl-${GANESHA_MAJOR_VERSION}${GANESHA_MINOR_VERSION}-py3-none-any.whl")
    install(
      CODE
      "set(_dest \"\$ENV{DESTDIR}\")
       set(_site \"\${_dest}${_ganeshactl_site}\")
       set(_bindir \"\${_dest}${CMAKE_INSTALL_PREFIX}/bin\")
       file(GLOB _distinfo \"\${_site}/ganeshactl-*.dist-info\")
       file(REMOVE_RECURSE \"\${_site}/Ganesha\" \${_distinfo})
       foreach(_s IN ITEMS fake_recall get_clientids grace_period ganesha_mgr ganesha_stats ganesha_conf ganesha_logrotate_mgr ganeshactl ganesha-admin manage_clients manage_logger manage_exports client_stats_9pOps export_stats_9pOps)
         file(REMOVE \"\${_bindir}/\${_s}\")
       endforeach()
       execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
         COMMAND ${Python3_EXECUTABLE} -m installer --destdir=\${_dest}/ --prefix=${CMAKE_INSTALL_PREFIX} ${_ganeshactl_wheel}
         RESULT_VARIABLE _rc)
       if(NOT _rc EQUAL 0)
         message(FATAL_ERROR \"Failed to install ${_ganeshactl_wheel}\")
       endif()"
      )
  endif()
endif(Python3_FOUND)

# Man page
set(man8_file ganesha_conf.man)
set(man8_page ganesha_conf.8)
add_custom_command(
	OUTPUT ${man8_page}
	COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/${man8_file}" ${man8_page}
	DEPENDS ${man8_file}
	)
add_custom_target(man ALL DEPENDS ${man8_page})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${man8_page}
	DESTINATION ${CMAKE_INSTALL_PREFIX}/share/man/man8/)

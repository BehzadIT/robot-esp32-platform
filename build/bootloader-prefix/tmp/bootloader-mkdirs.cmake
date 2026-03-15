# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/behzad/Projects/esp-idf/components/bootloader/subproject"
  "/home/behzad/Projects/robot-esp32-platform/build/bootloader"
  "/home/behzad/Projects/robot-esp32-platform/build/bootloader-prefix"
  "/home/behzad/Projects/robot-esp32-platform/build/bootloader-prefix/tmp"
  "/home/behzad/Projects/robot-esp32-platform/build/bootloader-prefix/src/bootloader-stamp"
  "/home/behzad/Projects/robot-esp32-platform/build/bootloader-prefix/src"
  "/home/behzad/Projects/robot-esp32-platform/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/behzad/Projects/robot-esp32-platform/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/behzad/Projects/robot-esp32-platform/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()

# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Release")
  file(REMOVE_RECURSE
  "Air-PollutionApp_autogen"
  "CMakeFiles\\Air-PollutionApp_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\Air-PollutionApp_autogen.dir\\ParseCache.txt"
  )
endif()

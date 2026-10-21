add_subdirectory(common)

if (${TARGET} STREQUAL "max32657")
	add_subdirectory(max32657)
elseif (${TARGET} STREQUAL "max32655")
	add_subdirectory(max32655)
elseif (${TARGET} STREQUAL "max32690")
	add_subdirectory(max32690)
endif()

# - Find Agent
# ATHENA_AGENT_DIR	- where to find athenapluginapi.h core_api.h
# ATHENA_AGENT_DIR_FOUND - True if Agent found.

# Look for the headers file.
FIND_PATH(ATHENA_AGENT_DIR NAMES athenapluginapi.h core_api.h)

# Handle the QUIETLY and REQUIRED arguments and set SQLITE3_FOUND to TRUE if all listed variables are TRUE.
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(ATHENA_AGENT DEFAULT_MSG ATHENA_AGENT_DIR)

# Copy the results to the output variables.
IF(ATHENA_AGENT_FOUND)
        SET(ATHENA_AGENT_INCLUDE_DIRS ${ATHENA_AGENT_DIR})
ELSE(ATHENA_AGENT_FOUND)
        SET(ATHENA_AGENT_INCLUDE_DIRS)
ENDIF(ATHENA_AGENT_FOUND)

MARK_AS_ADVANCED(ATHENA_AGENT_INCLUDE_DIRS)

### Required cmake version
CMAKE_MINIMUM_REQUIRED(VERSION 2.4.0)

SET(CMAKE_COLOR_MAKEFILE ON)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

### Let me see the command line
SET (CMAKE_VERBOSE_MAKEFILE OFF)

### Debug build please
set(CMAKE_BUILD_TYPE Debug)

# Put the include dirs which are in the source or build tree
# before all other include dirs, so the headers in the sources
# are prefered over the already installed ones
# since cmake 2.4.1
set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)

project(res-rsa C)
set(VERSION "2.9.3")

OPTION(WITH_ANSI "enforce strict ISO C++ conforming" ON)
OPTION(VERBOSE "show cmake command line" OFF)

IF (VERBOSE)
	SET (CMAKE_VERBOSE_MAKEFILE ON)
	MESSAGE (STATUS "Show cmake command line")
ENDIF (VERBOSE)

### Our own modules
set(CMAKE_MODULE_PATH
  ${CMAKE_SOURCE_DIR}/cmake/Modules
)

### Include modules
INCLUDE (DefineInstallationPaths)
INCLUDE (CheckIncludeFile)
INCLUDE (CheckTypeSize) 
INCLUDE (CheckLibraryExists)
INCLUDE (CheckFunctionExists)
INCLUDE (FindGMP)

### Platform specific stuff
IF (UNIX)
	MESSAGE (STATUS "Unix")
	IF (APPLE)
		MESSAGE (STATUS "Apple")
		
		# create a C static library see http://permalink.gmane.org/gmane.games.netrek.devel/2676 for details
	  	SET(CMAKE_C_CREATE_STATIC_LIBRARY
	      "<CMAKE_AR> cr <TARGET> <LINK_FLAGS> <OBJECTS> "
	      "<CMAKE_RANLIB> -c <TARGET> "
		)
	
		SET (CMAKE_C_LINK_FLAGS 
			-flat-namespace
			${CMAKE_C_LINK_FLAGS}
		)
	ENDIF (APPLE)
ELSE (UNIX)
	IF (WIN32)
		MESSAGE(STATUS "Good luck!")
	ENDIF (WIN32)
ENDIF (UNIX)

### Setup short variable path names
set(BINDIR ${BIN_INSTALL_DIR})
set(SBINDIR ${SBIN_INSTALL_DIR})
set(SYSCONFDIR ${SYSCONF_INSTALL_DIR})
set(LOCALSTATEDIR ${LOCALSTATE_INSTALL_DIR})
set(MANDIR ${MAN_INSTALL_DIR})
set(DATADIR ${DATA_INSTALL_DIR})
set(LIBDIR ${LIB_INSTALL_DIR})

### Type checks
CHECK_TYPE_SIZE ("int" SIZEOF_SIGNED_INT)
CHECK_TYPE_SIZE ("unsigned int" SIZEOF_UNSIGNED_INT)
CHECK_TYPE_SIZE ("signed long" SIZEOF_SIGNED_LONG)
CHECK_TYPE_SIZE ("unsigned long" SIZEOF_UNSIGNED_LONG)
CHECK_TYPE_SIZE ("unsigned long long" SIZEOF_UNSIGNED_LONG_LONG)

### Find headers 
CHECK_INCLUDE_FILE (time.h HAVE_TIME_H)

### Find functions
CHECK_FUNCTION_EXISTS(strftime HAVE_STRFTIME) 

### Library checks
#FIND_LIBRARY(MATH_LIBRARIES m)

### Find packages
FIND_PACKAGE(GMP)

### Add -D defines
ADD_DEFINITIONS()

## Add package include directories
INCLUDE_DIRECTORIES (
	${CMAKE_BINARY_DIR} 
	${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
	${GMP_INCLUDE_DIR}
)
CONFIGURE_FILE (${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)

### Build static library for NON-gmp version of RSA
ADD_LIBRARY (rsa STATIC
	rsa_math.c 
	rsa_encode.c
)

### Build static library of gmp version of RSA
ADD_LIBRARY (rsa-gmp STATIC
	rsa_gmp.c 
	rsa_encode_gmp.c
)

### Link and create executable mkkey
ADD_EXECUTABLE (
	mkkey
	mkkey.c
)

TARGET_LINK_LIBRARIES (
	mkkey
	${GMP_LIBRARY}
)

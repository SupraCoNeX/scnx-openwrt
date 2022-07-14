cmake_minimum_required(VERSION 2.6)

PROJECT(minstrel-rcd C)

SET(SOURCES main.c phy.c server.c client.c config.c)

ADD_DEFINITIONS(-Wall -Werror)
IF(CMAKE_C_COMPILER_VERSION VERSION_GREATER 6)
	ADD_DEFINITIONS(-Wextra -Werror=implicit-function-declaration)
	ADD_DEFINITIONS(-Wformat -Werror=format-security -Werror=format-nonliteral)
ENDIF()
ADD_DEFINITIONS(-Os -std=gnu99 -g3 -Wmissing-declarations -Wno-unused-parameter)

FIND_LIBRARY(ubox_library NAMES ubox)
FIND_PATH(ubox_include_dir libubox/usock.h)
INCLUDE_DIRECTORIES(${ubox_include_dir})

FIND_LIBRARY(uci_library NAMES uci)
FIND_PATH(uci_include_dir uci.h)
INCLUDE_DIRECTORIES(${uci_include_dir})
SET(LIBS ${ubox_library} ${uci_library})

IF(DEFINED CMAKE_CONFIG_MQTT)
	FIND_LIBRARY(mosquitto_library NAMES mosquitto)
	FIND_PATH(mosquitto_include_dir mosquitto.h)
	INCLUDE_DIRECTORIES(${mosquitto_include_dir})
	SET(SOURCES ${SOURCES} mqtt.c)
	SET(LIBS ${LIBS} ${mosquitto_library})
	ADD_DEFINITIONS(-DCONFIG_MQTT -D_GNU_SOURCE)
ENDIF(DEFINED CMAKE_CONFIG_MQTT)

IF(DEFINED CMAKE_CONFIG_ZSTD)
	FIND_LIBRARY(zstd_library NAMES zstd)
	FIND_PATH(zstd_include_dir zstd.h)
	INCLUDE_DIRECTORIES(${zstd_include_dir})
	SET(SOURCES ${SOURCES} zstd.c)
	SET(LIBS ${LIBS} ${zstd_library})
	ADD_DEFINITIONS(-DCONFIG_ZSTD)
ENDIF(DEFINED CMAKE_CONFIG_ZSTD)

ADD_EXECUTABLE(minstrel-rcd ${SOURCES})
TARGET_LINK_LIBRARIES(minstrel-rcd ${LIBS})

INSTALL(TARGETS minstrel-rcd
	RUNTIME DESTINATION sbin
)

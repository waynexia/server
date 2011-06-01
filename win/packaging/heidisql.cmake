SET(HEIDISQL_BASE_NAME "HeidiSQL_6.0_Portable")
SET(HEIDISQL_ZIP "${HEIDISQL_BASE_NAME}.zip")
SET(HEIDISQL_URL "http://heidisql.googlecode.com/files/${HEIDISQL_ZIP}")
IF(NOT EXISTS ${THIRD_PARTY_DOWNLOAD_LOCATION}/HeidiSQL/${HEIDISQL_ZIP})
  MAKE_DIRECTORY(${THIRD_PARTY_DOWNLOAD_LOCATION}/HeidiSQL) 
  MESSAGE(STATUS "Downloading ${HEIDISQL_URL} to ${THIRD_PARTY_DOWNLOAD_LOCATION}/HeidiSQL/${HEIDISQL_ZIP}")
  FILE(DOWNLOAD ${HEIDISQL_URL} ${THIRD_PARTY_DOWNLOAD_LOCATION}/HeidiSQL/${HEIDISQL_ZIP} TIMEOUT 60)
  EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E chdir ${THIRD_PARTY_DOWNLOAD_LOCATION}/HeidiSQL
    ${CMAKE_COMMAND} -E tar xfz ${THIRD_PARTY_DOWNLOAD_LOCATION}/HeidiSQL/${HEIDISQL_ZIP}
  )
ENDIF()
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/heidisql.wxi.in ${CMAKE_CURRENT_BINARY_DIR}/heidisql.wxi)
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/heidisql_feature.wxi.in ${CMAKE_CURRENT_BINARY_DIR}/heidisql_feature.wxi)

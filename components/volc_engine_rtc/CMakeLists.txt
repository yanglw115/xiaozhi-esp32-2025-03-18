

set(COMPONENT_ADD_INCLUDEDIRS  ./include)

# Edit following two lines to set component requirements (see docs)
set(COMPONENT_SRCS)

register_component()
# idf_component_register(INCLUDE_DIRS "include"
#                        REQUIRES ""  # 根据需要添加依赖项
#                        PRIV_REQUIRES "")  # 根据需要添加私有依赖项



IF ((CONFIG_IDF_TARGET STREQUAL "esp32s3"))
IF (IDF_VERSION_MAJOR EQUAL 5)
add_prebuilt_library(VolcEngineRTCLite "${CMAKE_CURRENT_LIST_DIR}/libs/${CONFIG_IDF_TARGET}/libVolcEngineRTCLite.a"
                    REQUIRES mbedtls espressif__zlib json lwip)
target_link_libraries(${COMPONENT_LIB} INTERFACE VolcEngineRTCLite)
ENDIF()
ENDIF()


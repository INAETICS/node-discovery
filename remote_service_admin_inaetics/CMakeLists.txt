#
# Licensed under Apache License v2. See LICENSE for more information.
#

find_package(CURL REQUIRED)
find_package(Jansson REQUIRED)

include_directories(${CURL_INCLUDE_DIRS})
include_directories(${JANSSON_INCLUDE_DIRS})
include_directories("${CELIX_INCLUDE_DIR}/utils/public/include")
include_directories("${CELIX_INCLUDE_DIR}/remote_services/utils/private/include")
include_directories("${CELIX_INCLUDE_DIR}/remote_services/utils/public/include")
include_directories("${CELIX_INCLUDE_DIR}/endpoint_listener")
include_directories("${CELIX_INCLUDE_DIR}/log_service")
include_directories("${CELIX_INCLUDE_DIR}/remote_service_admin")
include_directories("${PROJECT_SOURCE_DIR}/remote_service_admin_inaetics/public/include")
include_directories("${PROJECT_SOURCE_DIR}/remote_service_admin/private/include")
include_directories("${PROJECT_SOURCE_DIR}/wiring_common/private/include")
include_directories("${PROJECT_SOURCE_DIR}/wiring_common/public/include")

include_directories("private/include")
include_directories("dynamic_function_interface")
include_directories("${CELIX_INCLUDE_DIR}/remote_services/endpoint_listener/public/include")

SET(BUNDLE_SYMBOLICNAME "apache_celix_wiring_remote_service_admin")
SET(BUNDLE_VERSION "0.0.1")
SET(BUNDLE_NAME "apache_celix_wiring_remote_service_admin")

bundle(org.inaetics.remote_service_admin SOURCES 
	private/src/remote_service_admin_impl
	private/src/remote_service_admin_activator
	${CELIX_DIR}/share/celix/log_service/log_helper
    ${PROJECT_SOURCE_DIR}/remote_service_admin/private/src/export_registration_dfi
    ${PROJECT_SOURCE_DIR}/remote_service_admin/private/src/import_registration_dfi
	${PROJECT_SOURCE_DIR}/wiring_common/private/src/civetweb.c
)

install_bundle(org.inaetics.remote_service_admin)
    
target_link_libraries(org.inaetics.remote_service_admin ${CURL_LIBRARIES}  ${JANSSON_LIBRARIES})

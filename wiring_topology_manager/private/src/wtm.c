/**
 * Licensed under Apache License v2. See LICENSE for more information.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bundle_context.h"
#include "constants.h"
#include "module.h"
#include "bundle.h"
#include "filter.h"
#include "utils.h"
#include "service_reference.h"
#include "service_registration.h"
#include "wiring_topology_manager_impl.h"
#include "wiring_admin.h"
#include "wiring_endpoint_description.h"

typedef struct wiring_endpoint_registration {
    wiring_endpoint_description_pt wiringEndpointDescription;
    wiring_admin_service_pt wiringAdminService;
}* wiring_endpoint_registration_pt;

unsigned int wiringTopologyManager_srvcProperties_hash(void* properties);
int wiringTopologyManager_srvcProperties_equals(void* properties, void * toCompare);

bool properties_match(properties_pt properties, properties_pt reference);

celix_status_t wiringTopologyManager_WiringAdminServiceExportWiringEndpoint(wiring_topology_manager_pt manager, wiring_admin_service_pt wiringAdminService, properties_pt srvcProperties,
        wiring_endpoint_description_pt* wEndpoint);
celix_status_t wiringTopologyManager_WiringAdminServiceImportWiringEndpoint(wiring_topology_manager_pt manager, wiring_admin_service_pt wiringAdminService, wiring_endpoint_description_pt wEndpoint);

celix_status_t wiringTopologyManager_create(bundle_context_pt context, wiring_topology_manager_pt *manager) {
    celix_status_t status = CELIX_SUCCESS;

    *manager = malloc(sizeof(**manager));
    if (!*manager) {
        return CELIX_ENOMEM;
    }

    (*manager)->context = context;

    arrayList_create(&((*manager)->waList));
    arrayList_create(&((*manager)->waitingForExport));

    celixThreadMutex_create(&((*manager)->waListLock), NULL);
    celixThreadMutex_create(&((*manager)->importedWiringEndpointsLock), NULL);
    celixThreadMutex_create(&((*manager)->exportedWiringEndpointsLock), NULL);
    celixThreadMutex_create(&(*manager)->listenerListLock, NULL);

    (*manager)->listenerList = hashMap_create(serviceReference_hashCode, NULL, serviceReference_equals2, NULL);
    (*manager)->importedWiringEndpoints = hashMap_create(wiringEndpointDescription_hash, NULL, wiringEndpointDescription_equals, NULL); // key=wiring_endpoint_description_pt, value=array_list_pt wadmins
    (*manager)->exportedWiringEndpoints = hashMap_create(wiringTopologyManager_srvcProperties_hash, NULL, wiringTopologyManager_srvcProperties_equals, NULL); // key=properties_pt, value=(hash_map_pt  key=wadmin, value=wendpoint)

    return status;
}

celix_status_t wiringTopologyManager_destroy(wiring_topology_manager_pt manager) {
    celix_status_t status = CELIX_SUCCESS;

    celixThreadMutex_lock(&manager->listenerListLock);

    hashMap_destroy(manager->listenerList, false, false);

    celixThreadMutex_unlock(&manager->listenerListLock);
    celixThreadMutex_destroy(&manager->listenerListLock);

    celixThreadMutex_lock(&manager->waListLock);

    arrayList_destroy(manager->waList);

    celixThreadMutex_unlock(&manager->waListLock);
    celixThreadMutex_destroy(&manager->waListLock);

    celixThreadMutex_lock(&manager->importedWiringEndpointsLock);

    hashMap_destroy(manager->importedWiringEndpoints, false, false);
    celixThreadMutex_unlock(&manager->importedWiringEndpointsLock);
    celixThreadMutex_destroy(&manager->importedWiringEndpointsLock);

    celixThreadMutex_lock(&manager->exportedWiringEndpointsLock);

    hash_map_iterator_pt iter = hashMapIterator_create(manager->exportedWiringEndpoints);

    while (hashMapIterator_hasNext(iter)) {
        hash_map_entry_pt entry = hashMapIterator_nextEntry(iter);
        properties_pt srvcProperties = hashMapEntry_getKey(entry);
        hash_map_pt wiringAdminList = hashMapEntry_getValue(entry);

        properties_destroy(srvcProperties);
        hashMap_destroy(wiringAdminList, false, false);

    }
    hashMapIterator_destroy(iter);

    hashMap_destroy(manager->exportedWiringEndpoints, false, false);

    celixThreadMutex_unlock(&manager->exportedWiringEndpointsLock);
    celixThreadMutex_destroy(&manager->exportedWiringEndpointsLock);

    free(manager);

    return status;
}

celix_status_t wiringTopologyManager_WiringAdminServiceImportWiringEndpoint(wiring_topology_manager_pt manager, wiring_admin_service_pt wiringAdminService, wiring_endpoint_description_pt wEndpoint) {
    celix_status_t status = CELIX_BUNDLE_EXCEPTION;
    properties_pt adminProperties = NULL;

    wiringAdminService->getWiringAdminProperties(wiringAdminService->admin, &adminProperties);

    if (adminProperties != NULL) {

        /* only a wiringAdmin which provides the same config can import the endpoint */
        char* wiringConfigEndpoint = properties_get(wEndpoint->properties, WIRING_ADMIN_PROPERTIES_CONFIG_KEY);
        char* wiringConfigAdmin = properties_get(adminProperties, WIRING_ADMIN_PROPERTIES_CONFIG_KEY);

        if ((wiringConfigEndpoint != NULL) && (wiringConfigAdmin != NULL) && (strcmp(wiringConfigEndpoint, wiringConfigAdmin) == 0)) {
            status = wiringAdminService->importWiringEndpoint(wiringAdminService->admin, wEndpoint);

            if (status != CELIX_SUCCESS) {
                printf("WTM: importWiringEndpoint failed\n");
            }
        } else {
            printf("WTM: Wiring Admin does not match requirements (%s=%s)\n", wiringConfigEndpoint, wiringConfigAdmin);
        }
    }

    return status;
}

/* Functions for wiring endpoint listener */
celix_status_t wiringTopologyManager_WiringEndpointAdded(void *handle, wiring_endpoint_description_pt wEndpoint, char *matchedFilter) {
    celix_status_t status = CELIX_SUCCESS;
    wiring_topology_manager_pt manager = (wiring_topology_manager_pt) handle;

    celixThreadMutex_lock(&manager->importedWiringEndpointsLock);

    array_list_pt wiringAdminList = hashMap_get(manager->importedWiringEndpoints, wEndpoint);

    if (wiringAdminList == NULL) {
        arrayList_create(&wiringAdminList);
        hashMap_put(manager->importedWiringEndpoints, wEndpoint, wiringAdminList);
    }

    celixThreadMutex_unlock(&manager->importedWiringEndpointsLock);

    return status;
}

celix_status_t wiringTopologyManager_WiringEndpointRemoved(void *handle, wiring_endpoint_description_pt wEndpoint, char *matchedFilter) {
    celix_status_t status = CELIX_SUCCESS;
    wiring_topology_manager_pt manager = (wiring_topology_manager_pt) handle;

    celixThreadMutex_lock(&manager->importedWiringEndpointsLock);
    char* wireId = properties_get(wEndpoint->properties, WIRING_ENDPOINT_DESCRIPTION_WIRE_ID_KEY);

    array_list_pt wiringAdminList = hashMap_remove(manager->importedWiringEndpoints, wEndpoint);

    if (wiringAdminList != NULL) {

        int i = 0;
        int size = arrayList_size(wiringAdminList);

        for (; i < size; ++i) {
            wiring_admin_service_pt wiringAdminService = (wiring_admin_service_pt) arrayList_get(wiringAdminList, i);

            wiringAdminService->removeImportedWiringEndpoint(wiringAdminService->admin, wEndpoint);
        }

        arrayList_destroy(wiringAdminList);

        printf("WTM: Removing imported wiring endpoint (%s).\n", wireId);
    } else {
        printf("WTM: Removing of imported wiring endpoint (%s) failed.\n", wireId);
    }

    celixThreadMutex_unlock(&manager->importedWiringEndpointsLock);

    return status;
}

/* Return true if all pairs in properties are contained in reference */
unsigned int wiringTopologyManager_srvcProperties_hash(void* properties) {
    bool matching = true;
    unsigned int hash = 1216721012;
    hash_map_iterator_pt iter = hashMapIterator_create((properties_pt) properties);

    while (hashMapIterator_hasNext(iter) && matching) {
        hash_map_entry_pt prop_pair = hashMapIterator_nextEntry(iter);
        char* prop_key = (char*) hashMapEntry_getKey(prop_pair);
        char* prop_value = (char*) hashMapEntry_getValue(prop_pair);

        // we do not consider service properties
        if (!(strcmp(prop_key, OSGI_RSA_ENDPOINT_FRAMEWORK_UUID) == 0 || strcmp(prop_key, OSGI_FRAMEWORK_SERVICE_ID) == 0 || strcmp(prop_key, OSGI_FRAMEWORK_OBJECTCLASS) == 0
                || strcmp(prop_key, "service.exported.interfaces") == 0)) {
            hash ^= utils_stringHash(prop_key);
            hash ^= utils_stringHash(prop_value);
        }
    }

    hashMapIterator_destroy(iter);

    return hash;
}

int wiringTopologyManager_srvcProperties_equals(void* properties, void * toCompare) {
    bool matching = true;
    hash_map_iterator_pt iter = hashMapIterator_create(properties);

    while (hashMapIterator_hasNext(iter) && matching) {
        hash_map_entry_pt prop_pair = hashMapIterator_nextEntry(iter);
        char* prop_key = (char*) hashMapEntry_getKey(prop_pair);
        char* prop_value = (char*) hashMapEntry_getValue(prop_pair);

        // we do not consider service properties
        if (!(strcmp(prop_key, OSGI_RSA_ENDPOINT_FRAMEWORK_UUID) == 0 || strcmp(prop_key, OSGI_FRAMEWORK_SERVICE_ID) == 0 || strcmp(prop_key, OSGI_FRAMEWORK_OBJECTCLASS) == 0
                || strcmp(prop_key, "service.exported.interfaces") == 0)) {
            char* ref_value = (char*) hashMap_get(toCompare, prop_key);
            if (ref_value == NULL || (strcmp(ref_value, prop_value) != 0)) {
                matching = false; // We found a pair in properties not included in reference
            }
        }
    }
    hashMapIterator_destroy(iter);

    return matching;
}

bool properties_match(properties_pt properties, properties_pt reference) {

    bool matching = true;

    hash_map_iterator_pt iter = hashMapIterator_create(properties);
    while (hashMapIterator_hasNext(iter) && matching) {
        hash_map_entry_pt prop_pair = hashMapIterator_nextEntry(iter);
        char* prop_key = (char*) hashMapEntry_getKey(prop_pair);
        char* prop_value = (char*) hashMapEntry_getValue(prop_pair);

        // we do not consider service properties
        if (strcmp(prop_key, OSGI_RSA_ENDPOINT_FRAMEWORK_UUID) == 0 || strcmp(prop_key, OSGI_FRAMEWORK_SERVICE_ID) == 0 || strcmp(prop_key, OSGI_FRAMEWORK_OBJECTCLASS) == 0
                || strcmp(prop_key, "service.exported.interfaces") == 0) {
            continue;
        }

        char* ref_value = (char*) hashMap_get(reference, prop_key);
        if (ref_value == NULL || (strcmp(ref_value, prop_value) != 0)) {
            matching = false; // We found a pair in properties not included in reference
        }
    }
    hashMapIterator_destroy(iter);

    return matching;
}

celix_status_t wiringTopologyManager_WiringAdminServiceExportWiringEndpoint(wiring_topology_manager_pt manager, wiring_admin_service_pt wiringAdminService, properties_pt srvcProperties,
        wiring_endpoint_description_pt* wEndpoint) {
    celix_status_t status = CELIX_BUNDLE_EXCEPTION;

    properties_pt adminProperties = NULL;

    /* retrieve capabilities of wiringAdmin */
    wiringAdminService->getWiringAdminProperties(wiringAdminService->admin, &adminProperties);

    if (adminProperties != NULL) {

        /* check whether the wiringAdmin can fulfill what is requested by the service */
        if (properties_match(srvcProperties, adminProperties) == true) {

            status = wiringAdminService->exportWiringEndpoint(wiringAdminService->admin, wEndpoint);

            if (status != CELIX_SUCCESS) {
                printf("WTM: export of WiringAdmin failed\n");
            } else {

                char* serviceId = properties_get(srvcProperties, "service.id");
                properties_set((*wEndpoint)->properties, "requested.service.id", serviceId);
                status = wiringTopologyManager_notifyListenersWiringEndpointAdded(manager, *wEndpoint);
            }
        }
    }

    return status;
}

celix_status_t wiringTopologyManager_exportWiringEndpoint(wiring_topology_manager_pt manager, properties_pt srvcProperties) {
    celix_status_t status = CELIX_BUNDLE_EXCEPTION;

    if (srvcProperties == NULL) {
        status = CELIX_ILLEGAL_ARGUMENT;
    } else {

        char* serviceId = properties_get(srvcProperties, "service.id");
        printf("WTM: wiringTopologyManager_exportWiringEndpoint for serviceId %s\n", serviceId);

        array_list_pt wiringAdmins = NULL;
        wiring_endpoint_description_pt wEndpoint = NULL;
        hash_map_pt wiringAdminList = NULL;

        celixThreadMutex_lock(&manager->exportedWiringEndpointsLock);

        // hmm...
        /*
         das problem ist dass srvcProperties auch die serviceId beinhaltet. wenn wir
         nun spaeter einen wiringAdmin hinzufuegen, werden nur die srvcProperties
         von ersterem gertiggeet. ich denke wir brachen eine weitere datenstruktur hier,
         welche sich alle serviceIds merkt
         */

        wiringAdminList = hashMap_get(manager->exportedWiringEndpoints, srvcProperties);

        if (wiringAdminList == NULL) {

            printf("WTM: serviceId %s needs new wire.\n", serviceId);

            wiringTopologyManager_getWAs(manager, &wiringAdmins);

            int listCnt = 0;
            int listSize = arrayList_size(wiringAdmins);

            if (listSize > 0) {

                wiringAdminList = hashMap_create(NULL, NULL, NULL, NULL);
                hashMap_put(manager->exportedWiringEndpoints, srvcProperties, wiringAdminList);

                for (; listCnt < listSize && (wEndpoint == NULL); ++listCnt) {

                    wiring_admin_service_pt wiringAdminService = (wiring_admin_service_pt) arrayList_get(wiringAdmins, listCnt);

                    status = wiringTopologyManager_WiringAdminServiceExportWiringEndpoint(manager, wiringAdminService, srvcProperties, &wEndpoint);
                    if (status == CELIX_SUCCESS) {
                        hashMap_put(wiringAdminList, wiringAdminService, wEndpoint);
                    }
                }

            } else {
                arrayList_add(manager->waitingForExport, srvcProperties);
            }
            arrayList_destroy(wiringAdmins);

        } else {
            status = CELIX_SUCCESS;

            printf("WTM: serviceId %s can re-use wire.\n", serviceId);

            hash_map_iterator_pt wiringAdminIter = hashMapIterator_create(wiringAdminList);

            while ((hashMapIterator_hasNext(wiringAdminIter) == true) && (status == CELIX_SUCCESS)) {
                wiring_endpoint_description_pt wEndpoint = (wiring_endpoint_description_pt) hashMapIterator_nextValue(wiringAdminIter);

                // set something like requested serviceId?
                char* serviceId = properties_get(srvcProperties, "service.id");
                properties_set(wEndpoint->properties, "requested.service.id", serviceId);
                status = wiringTopologyManager_notifyListenersWiringEndpointAdded(manager, wEndpoint);
            }

            hashMapIterator_destroy(wiringAdminIter);

            properties_destroy(srvcProperties);
        }

        celixThreadMutex_unlock(&manager->exportedWiringEndpointsLock);

        if (status != CELIX_SUCCESS) {
            printf("WTM: Could not install callback to any Wiring Endpoint\n");
        }
    }

    return status;
}

celix_status_t wiringTopologyManager_removeExportedWiringEndpoint(wiring_topology_manager_pt manager, properties_pt properties) {
    celix_status_t status = CELIX_SUCCESS;

    if (properties == NULL) {
        status = CELIX_ILLEGAL_ARGUMENT;
    } else {
        celixThreadMutex_lock(&manager->exportedWiringEndpointsLock);

        hash_map_pt wiringAdminList = hashMap_remove(manager->exportedWiringEndpoints, properties);

        if (wiringAdminList != NULL) {
            hash_map_iterator_pt wiringAdminIter = hashMapIterator_create(wiringAdminList);

            while ((hashMapIterator_hasNext(wiringAdminIter) == true) && (status == CELIX_SUCCESS)) {
                hash_map_entry_pt wiringAdminEntry = hashMapIterator_nextEntry(wiringAdminIter);

                wiring_admin_service_pt wiringAdminService = hashMapEntry_getKey(wiringAdminEntry);
                wiring_endpoint_description_pt wEndpoint = hashMapEntry_getValue(wiringAdminEntry);

                if (wiringAdminService->removeExportedWiringEndpoint(wiringAdminService->admin, wEndpoint) != CELIX_SUCCESS) {
                    status = CELIX_BUNDLE_EXCEPTION;
                }
            }

            hashMapIterator_destroy(wiringAdminIter);
        } else {
            status = CELIX_ILLEGAL_STATE;
        }

        celixThreadMutex_unlock(&manager->exportedWiringEndpointsLock);
    }

    return status;
}

celix_status_t wiringTopologyManager_importWiringEndpoint(wiring_topology_manager_pt manager, properties_pt rsaProperties) {
    celix_status_t status = CELIX_SUCCESS;
    hash_map_iterator_pt iter = NULL;

    celixThreadMutex_lock(&manager->importedWiringEndpointsLock);
    iter = hashMapIterator_create(manager->importedWiringEndpoints);

    while (hashMapIterator_hasNext(iter)) {
        hash_map_entry_pt importedWiringEndpointEntry = (hash_map_entry_pt) hashMapIterator_nextEntry(iter);
        wiring_endpoint_description_pt wiringEndpointDesc = (wiring_endpoint_description_pt) hashMapEntry_getKey(importedWiringEndpointEntry);
        array_list_pt wiringAdminList = (array_list_pt) hashMapEntry_getValue(importedWiringEndpointEntry);

        // do we have a matching wiring endpoint
        if (properties_match(rsaProperties, wiringEndpointDesc->properties)) {
            array_list_pt localWAs = NULL;
            wiringTopologyManager_getWAs(manager, &localWAs);

            int listCnt = 0;
            int listSize = arrayList_size(localWAs);

            for (; listCnt < listSize; ++listCnt) {
                wiring_admin_service_pt wiringAdminService = (wiring_admin_service_pt) arrayList_get(localWAs, listCnt);

                if (arrayList_contains(wiringAdminList, wiringAdminService)) {
                    char* wireId = properties_get(wiringEndpointDesc->properties, WIRING_ENDPOINT_DESCRIPTION_WIRE_ID_KEY);
                    printf("WTM: WiringEndpoint %s is already imported by WiringAdminService %p\n", wireId, wiringAdminService);
                } else {
                    status = wiringTopologyManager_WiringAdminServiceImportWiringEndpoint(manager, wiringAdminService, wiringEndpointDesc);

                    if (status == CELIX_SUCCESS) {
                        arrayList_add(wiringAdminList, wiringAdminService);
                    }
                }
            }

            arrayList_destroy(localWAs);
        } else {
            printf("WTM: rsaProperties do not match imported Endpoint\n");
        }
    }

    hashMapIterator_destroy(iter);
    celixThreadMutex_unlock(&manager->importedWiringEndpointsLock);

    return status;
}

celix_status_t wiringTopologyManager_removeImportedWiringEndpoint(wiring_topology_manager_pt manager, properties_pt properties) {
    celix_status_t status = CELIX_SUCCESS;
    hash_map_iterator_pt iter = NULL;

    celixThreadMutex_lock(&manager->importedWiringEndpointsLock);
    iter = hashMapIterator_create(manager->importedWiringEndpoints);

    while (hashMapIterator_hasNext(iter)) {
        hash_map_entry_pt importedWiringEndpointEntry = (hash_map_entry_pt) hashMapIterator_nextEntry(iter);
        wiring_endpoint_description_pt wiringEndpointDesc = (wiring_endpoint_description_pt) hashMapEntry_getKey(importedWiringEndpointEntry);
        array_list_pt wiringAdminList = (array_list_pt) hashMapEntry_getValue(importedWiringEndpointEntry);

        // do we have a matching wiring endpoint
        if (properties_match(properties, wiringEndpointDesc->properties)) {

            int listCnt = 0;
            int listSize = arrayList_size(wiringAdminList);
            char* wireId = properties_get(wiringEndpointDesc->properties, WIRING_ENDPOINT_DESCRIPTION_WIRE_ID_KEY);

            for (; listCnt < listSize; ++listCnt) {
                wiring_admin_service_pt wiringAdminService = (wiring_admin_service_pt) arrayList_remove(wiringAdminList, listCnt);

                if (wiringAdminService->removeImportedWiringEndpoint(wiringAdminService->admin, wiringEndpointDesc) != CELIX_SUCCESS) {
                    status = CELIX_BUNDLE_EXCEPTION;
                }
            }

            printf("WTM: imported wiring endpoint %s removed\n", wireId);
        } else {
            printf("WTM: given properties do not match imported Endpoint\n");
        }
    }

    hashMapIterator_destroy(iter);
    celixThreadMutex_unlock(&manager->importedWiringEndpointsLock);

    return status;
}

celix_status_t wiringTopologyManager_notifyListenersWiringEndpointAdded(wiring_topology_manager_pt manager, wiring_endpoint_description_pt wEndpoint) {
    celix_status_t status = CELIX_SUCCESS;

    status = celixThreadMutex_lock(&manager->listenerListLock);

    if (status == CELIX_SUCCESS) {
        hash_map_iterator_pt iter = hashMapIterator_create(manager->listenerList);
        while (hashMapIterator_hasNext(iter)) {
            char *scope = NULL;
            wiring_endpoint_listener_pt listener = NULL;
            service_reference_pt reference = hashMapIterator_nextKey(iter);

            serviceReference_getProperty(reference, (char *) INAETICS_WIRING_ENDPOINT_LISTENER_SCOPE, &scope);

            status = bundleContext_getService(manager->context, reference, (void **) &listener);
            if (status == CELIX_SUCCESS) {
                filter_pt filter = filter_create(scope);

                bool matchResult = false;
                filter_match(filter, wEndpoint->properties, &matchResult);

                if (matchResult) {
                    status = listener->wiringEndpointAdded(listener->handle, wEndpoint, scope);
                }

                filter_destroy(filter);
            }
        }
        hashMapIterator_destroy(iter);

        status = celixThreadMutex_unlock(&manager->listenerListLock);
    }

    return status;
}

celix_status_t wiringTopologyManager_notifyListenersWiringEndpointRemoved(wiring_topology_manager_pt manager, wiring_endpoint_description_pt wEndpoint) {
    celix_status_t status = CELIX_SUCCESS;

    status = celixThreadMutex_lock(&manager->listenerListLock);

    if (status == CELIX_SUCCESS) {
        hash_map_iterator_pt iter = hashMapIterator_create(manager->listenerList);
        while (hashMapIterator_hasNext(iter)) {
            char *scope = NULL;
            wiring_endpoint_listener_pt listener = NULL;
            service_reference_pt reference = hashMapIterator_nextKey(iter);

            serviceReference_getProperty(reference, (char *) INAETICS_WIRING_ENDPOINT_LISTENER_SCOPE, &scope);

            status = bundleContext_getService(manager->context, reference, (void **) &listener);
            if (status == CELIX_SUCCESS) {

                filter_pt filter = filter_create(scope);

                bool matchResult = false;
                filter_match(filter, wEndpoint->properties, &matchResult);

                if (matchResult) {
                    status = listener->wiringEndpointRemoved(listener->handle, wEndpoint, scope);
                }

                filter_destroy(filter);
            }
        }

        hashMapIterator_destroy(iter);

        status = celixThreadMutex_unlock(&manager->listenerListLock);
    }

    return status;
}

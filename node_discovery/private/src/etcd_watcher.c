#include <stdbool.h>
#include <stdlib.h>

#include "celix_log.h"
#include "constants.h"
#include "node_discovery.h"
#include "node_discovery_impl.h"
#include "node_description_impl.h"

#include "etcd.h"
#include "etcd_watcher.h"

struct etcd_watcher {
	node_discovery_pt node_discovery;

	celix_thread_mutex_t watcherLock;
	celix_thread_t watcherThread;

	volatile bool running;
};

#define MAX_ROOTNODE_LENGTH		 64
#define MAX_LOCALNODE_LENGTH 	4096

#define CFG_ETCD_ROOT_PATH		"NODE_DISCOVERY_ETCD_ROOT_PATH"
#define DEFAULT_ETCD_ROOTPATH	"inaetics/discovery"

#define CFG_ETCD_SERVER_IP		"NODE_DISCOVERY_ETCD_SERVER_IP"
#define DEFAULT_ETCD_SERVER_IP	"127.0.0.1"

#define CFG_ETCD_SERVER_PORT	"NODE_DISCOVERY_ETCD_SERVER_PORT"
#define DEFAULT_ETCD_SERVER_PORT 4001

// be careful - this should be higher than the curl timeout
#define CFG_ETCD_TTL   "DISCOVERY_ETCD_TTL"
#define DEFAULT_ETCD_TTL 30

// note that the rootNode shouldn't have a leading slash
static celix_status_t etcdWatcher_getRootPath(bundle_context_pt context, char* rootNode) {
	celix_status_t status = CELIX_SUCCESS;
	char* rootPath = NULL;

	if (((bundleContext_getProperty(context, CFG_ETCD_ROOT_PATH, &rootPath)) != CELIX_SUCCESS) || (!rootPath)) {
		strncpy(rootNode, DEFAULT_ETCD_ROOTPATH, MAX_ROOTNODE_LENGTH);
	} else {
		strncpy(rootNode, rootPath, MAX_ROOTNODE_LENGTH);
	}

	return status;
}

static celix_status_t etcdWatcher_getLocalNodePath(bundle_context_pt context, node_description_pt ownNodeDescription, char* localNodePath) {
	celix_status_t status = CELIX_SUCCESS;
	char rootPath[MAX_ROOTNODE_LENGTH];

	char* zoneId = properties_get(ownNodeDescription->properties, NODE_DESCRIPTION_ZONE_IDENTIFIER_KEY);
	char* nodeId = properties_get(ownNodeDescription->properties, NODE_DESCRIPTION_NODE_IDENTIFIER_KEY);

	if (zoneId == NULL || nodeId == NULL) {
		status = CELIX_ILLEGAL_STATE;
	} else if ((etcdWatcher_getRootPath(context, &rootPath[0]) != CELIX_SUCCESS)) {
		status = CELIX_ILLEGAL_STATE;
	} else if (rootPath[strlen(&rootPath[0]) - 1] == '/') {
		snprintf(localNodePath, MAX_LOCALNODE_LENGTH, "%s%s/%s", &rootPath[0], zoneId, nodeId);
	} else {
		snprintf(localNodePath, MAX_LOCALNODE_LENGTH, "%s/%s/%s", &rootPath[0], zoneId, nodeId);
	}

	return status;
}

static celix_status_t etcdWatcher_addAlreadyExistingNodes(node_discovery_pt node_discovery, int* highestModified) {
	celix_status_t status = CELIX_SUCCESS;
	char** endpointArray = calloc(MAX_NODES, sizeof(*endpointArray));
	char rootPath[MAX_ROOTNODE_LENGTH];
	int i, size;

	*highestModified = -1;

	if (!endpointArray) {
		status = CELIX_ENOMEM;
	} else {

		for (i = 0; i < MAX_NODES; i++) {
			endpointArray[i] = calloc(MAX_KEY_LENGTH, sizeof(*endpointArray[i]));
		}

		// we need to go though all nodes and get the highest modifiedIndex
		if (((status = etcdWatcher_getRootPath(node_discovery->context, &rootPath[0])) == CELIX_SUCCESS) && (etcd_getEndpoints(rootPath, endpointArray, &size) == true)) {
			for (i = 0; i < size; i++) {
				node_description_pt nodeDescription = NULL;

				char* key = endpointArray[i];
				int modIndex = 0;

				status = etcdWatcher_getWiringEndpointFromKey(node_discovery, key, &nodeDescription, &modIndex);

				if (status == CELIX_SUCCESS) {
					node_discovery_addNode(node_discovery, &key[0], nodeDescription);
				} else {
					printf("ERROR while retrieving endpoint from %s\n", key);
				}

				// TODO: fixed highestModifiedIndex
				*highestModified = modIndex;
			}
		}

		for (i = 0; i < MAX_NODES; i++) {
			free(endpointArray[i]);
		}
	}

	free(endpointArray);

	return status;
}

celix_status_t etcdWatcher_addOwnNode(etcd_watcher_pt watcher) {
	celix_status_t status = CELIX_BUNDLE_EXCEPTION;
	char localNodePath[MAX_LOCALNODE_LENGTH];
	char* ttlStr = NULL;
	int ttl;

	node_discovery_pt node_discovery = watcher->node_discovery;
	bundle_context_pt context = node_discovery->context;
	node_description_pt ownNodeDescription = node_discovery->ownNode;

	// register own framework
	status = etcdWatcher_getLocalNodePath(context, ownNodeDescription, &localNodePath[0]);

	// determine ttl
	if ((bundleContext_getProperty(context, CFG_ETCD_TTL, &ttlStr) != CELIX_SUCCESS) || !ttlStr) {
		ttl = DEFAULT_ETCD_TTL;
	} else {
		char* endptr = ttlStr;
		errno = 0;
		ttl = strtol(ttlStr, &endptr, 10);
		if (*endptr || errno != 0) {
			ttl = DEFAULT_ETCD_TTL;
		}
	}

	// register every wiring endpoint
	array_list_iterator_pt endpointIter = arrayListIterator_create(ownNodeDescription->wiring_ep_descriptions_list);

	while (status == CELIX_SUCCESS && arrayListIterator_hasNext(endpointIter)) {
		wiring_endpoint_description_pt wiringEndpointDesc = arrayListIterator_next(endpointIter);

		char* protocol = properties_get(wiringEndpointDesc->properties, WIRING_ENDPOINT_DESCRIPTION_PROTOCOL_KEY);
		char* user = properties_get(wiringEndpointDesc->properties, WIRING_ENDPOINT_DESCRIPTION_USER_KEY);

		if (protocol == NULL || user == NULL) {
			status = CELIX_ILLEGAL_ARGUMENT;
		} else {
			char etcdKeyUrl[MAX_LOCALNODE_LENGTH];
			char etcdValueUrl[MAX_LOCALNODE_LENGTH];
			char etcdKeyMetadata[MAX_LOCALNODE_LENGTH];
			char etcdValueMetadata[MAX_LOCALNODE_LENGTH];
			char etcdKeyComplete[MAX_LOCALNODE_LENGTH];
			char etcdValueComplete[MAX_LOCALNODE_LENGTH];

			snprintf(etcdKeyUrl, MAX_LOCALNODE_LENGTH, "%s/%s/%s/%s", localNodePath, user, protocol, ETCD_KEY_SUFFIX_URL);
			snprintf(etcdValueUrl, MAX_LOCALNODE_LENGTH, "%s", wiringEndpointDesc->url);

			snprintf(etcdKeyMetadata, MAX_LOCALNODE_LENGTH, "%s/%s/%s/%s", localNodePath, user, protocol, ETCD_KEY_SUFFIX_METADATA);
			// TODO put properties in here
			snprintf(etcdValueMetadata, MAX_LOCALNODE_LENGTH, "{test: test}");

			snprintf(etcdKeyComplete, MAX_LOCALNODE_LENGTH, "%s/%s/%s/%s", localNodePath, user, protocol, ETCD_KEY_SUFFIX_COMPLETE);
			snprintf(etcdValueComplete, MAX_LOCALNODE_LENGTH, "true");

			/// TODO: implement update
			etcd_set(etcdKeyUrl, etcdValueUrl, ttl, false);
			etcd_set(etcdKeyMetadata, etcdValueMetadata, ttl, false);
			etcd_set(etcdKeyComplete, etcdValueComplete, ttl, false);
		}
	}

	arrayListIterator_destroy(endpointIter);

	return status;
}

// gets everything from provided key
celix_status_t etcdWatcher_getWiringEndpointFromKey(node_discovery_pt node_discovery, char* key, node_description_pt* nodeDescription, int* modIndex) {

	celix_status_t status = CELIX_SUCCESS;

	char etcdKeyUrl[MAX_LOCALNODE_LENGTH];
	char etcdValueUrl[MAX_LOCALNODE_LENGTH];
	char etcdKeyComplete[MAX_LOCALNODE_LENGTH];

	char etcdKeyMetadata[MAX_LOCALNODE_LENGTH];
	char etcdValueMetadata[MAX_LOCALNODE_LENGTH];
	char etcdValueComplete[MAX_LOCALNODE_LENGTH];

	char action[MAX_ACTION_LENGTH];
	int lclIndex;

	snprintf(&etcdKeyUrl[0], MAX_LOCALNODE_LENGTH, "%s/%s", key, ETCD_KEY_SUFFIX_URL);
	snprintf(&etcdKeyMetadata[0], MAX_LOCALNODE_LENGTH, "%s/%s", key, ETCD_KEY_SUFFIX_METADATA);
	snprintf(&etcdKeyComplete[0], MAX_LOCALNODE_LENGTH, "%s/%s", key, ETCD_KEY_SUFFIX_COMPLETE);

	// get url
	if (etcd_get(&etcdKeyUrl[0], &etcdValueUrl[0], &action[0], &lclIndex) != true) {
		printf("Could not retrieve URL value for %s\n", (&etcdKeyUrl[0]));
		status = CELIX_ILLEGAL_STATE;
	} else if (etcd_get(&etcdKeyMetadata[0], &etcdValueMetadata[0], &action[0], &lclIndex) != true) {
		printf("Could not retrieve Metadata value for %s\n", (&etcdKeyMetadata[0]));
		status = CELIX_ILLEGAL_STATE;
	} else if (etcd_get(&etcdKeyComplete[0], &etcdValueComplete[0], &action[0], modIndex) != true) {
		printf("Could not retrieve Complete value for %s\n", (&etcdKeyComplete[0]));
		status = CELIX_ILLEGAL_STATE;
	} else {
		char rootPath[MAX_ROOTNODE_LENGTH];
		char expr[MAX_LOCALNODE_LENGTH];
		char zoneId[4096];
		char nodeId[4096];

		etcdWatcher_getRootPath(node_discovery->context, &rootPath[0]);

		snprintf(expr, MAX_LOCALNODE_LENGTH, "/%s/%%[^/]/%%[^/]/.*", rootPath);

		int foundItems = sscanf(key, expr, &zoneId[0], &nodeId[0]);

		if (foundItems != 2) {
			printf("Could not find zone/node (key: %s) \n", key);
			status = CELIX_ILLEGAL_STATE;
		} else {
			properties_pt nodeDescProperties = properties_create();
			properties_set(nodeDescProperties, NODE_DESCRIPTION_NODE_IDENTIFIER_KEY, nodeId);
			properties_set(nodeDescProperties, NODE_DESCRIPTION_ZONE_IDENTIFIER_KEY, zoneId);

			status = nodeDescription_create(nodeId, nodeDescProperties, nodeDescription);
		}

		if (status == CELIX_SUCCESS) {
			wiring_endpoint_description_pt wiringEndpointDescription = NULL;
			properties_pt wiringDescProperties = properties_create();

			status = wiringEndpointDescription_create(nodeId, wiringDescProperties, &wiringEndpointDescription);
			wiringEndpointDescription->url = strdup(&etcdValueUrl[0]);

			if (status == CELIX_SUCCESS) {
				celixThreadMutex_lock(&(*nodeDescription)->wiring_ep_desc_list_lock);
				arrayList_add((*nodeDescription)->wiring_ep_descriptions_list, wiringEndpointDescription);
				celixThreadMutex_unlock(&(*nodeDescription)->wiring_ep_desc_list_lock);
			}
		}
	}

	return status;
}

/*
 * performs (blocking) etcd_watch calls to check for
 * changing discovery endpoint information within etcd.
 */
static void* etcdWatcher_run(void* data) {
	etcd_watcher_pt watcher = (etcd_watcher_pt) data;
	time_t timeBeforeWatch = time(NULL);
	char rootPath[MAX_ROOTNODE_LENGTH];
	int highestModified = 0;

	node_discovery_pt node_discovery = watcher->node_discovery;
	bundle_context_pt context = node_discovery->context;

	memset(rootPath, 0, MAX_ROOTNODE_LENGTH);

	etcdWatcher_addAlreadyExistingNodes(node_discovery, &highestModified);
	etcdWatcher_getRootPath(context, rootPath);

	while ((celixThreadMutex_lock(&watcher->watcherLock) == CELIX_SUCCESS) && watcher->running) {

		char rkey[MAX_KEY_LENGTH];
		char value[MAX_VALUE_LENGTH];
		char preValue[MAX_VALUE_LENGTH];
		char action[MAX_ACTION_LENGTH];

		celixThreadMutex_unlock(&watcher->watcherLock);

		if (etcd_watch(rootPath, highestModified + 1, &action[0], &preValue[0], &value[0], &rkey[0]) == true) {

			if (strcmp(action, "set") == 0) {
				// check for complete key
				size_t lengthSuffix = strlen(ETCD_KEY_SUFFIX_COMPLETE);
				size_t lengthKey = strlen(rkey);

				if ((lengthSuffix < lengthKey) && (strncmp(rkey + lengthKey - lengthSuffix, ETCD_KEY_SUFFIX_COMPLETE, lengthSuffix) == 0) && (strcmp(value, "true") == 0)) {
					// complete key found and set to true
					node_description_pt nodeDescription = NULL;

					char key[MAX_KEY_LENGTH];
					snprintf(key, lengthKey - lengthSuffix, "%s", rkey);

					celix_status_t status = etcdWatcher_getWiringEndpointFromKey(node_discovery, key, &nodeDescription, &highestModified);

					if (status == CELIX_SUCCESS) {
						node_discovery_addNode(node_discovery, &key[0], nodeDescription);
					}
				}
			} else if (strcmp(action, "delete") == 0) {
				node_discovery_removeNode(node_discovery, &rkey[0]);
			} else if (strcmp(action, "expire") == 0) {
				node_discovery_removeNode(node_discovery, &rkey[0]);
			} else if (strcmp(action, "update") == 0) {
				// TODO
			} else {
				fw_log(logger, OSGI_FRAMEWORK_LOG_INFO, "Unexpected action: %s", action);
			}
			highestModified++;
		}

		// update own framework uuid
		if (time(NULL) - timeBeforeWatch > (DEFAULT_ETCD_TTL / 2)) {
			etcdWatcher_addOwnNode(watcher);
			timeBeforeWatch = time(NULL);
		}
	}

	if (watcher->running == false) {
		celixThreadMutex_unlock(&watcher->watcherLock);
	}

	return NULL;
}

celix_status_t etcdWatcher_create(node_discovery_pt node_discovery, bundle_context_pt context, etcd_watcher_pt *watcher) {
	celix_status_t status = CELIX_SUCCESS;

	char* etcd_server = NULL;
	char* etcd_port_string = NULL;
	int etcd_port = 0;

	if (node_discovery == NULL) {
		return CELIX_BUNDLE_EXCEPTION;
	}

	(*watcher) = calloc(1, sizeof(struct etcd_watcher));
	if (!watcher) {
		return CELIX_ENOMEM;
	} else {
		(*watcher)->node_discovery = node_discovery;
	}

	if ((bundleContext_getProperty(context, CFG_ETCD_SERVER_IP, &etcd_server) != CELIX_SUCCESS) || !etcd_server) {
		etcd_server = DEFAULT_ETCD_SERVER_IP;
	}

	if ((bundleContext_getProperty(context, CFG_ETCD_SERVER_PORT, &etcd_port_string) != CELIX_SUCCESS) || !etcd_port_string) {
		etcd_port = DEFAULT_ETCD_SERVER_PORT;
	} else {
		char* endptr = etcd_port_string;
		errno = 0;
		etcd_port = strtol(etcd_port_string, &endptr, 10);
		if (*endptr || errno != 0) {
			etcd_port = DEFAULT_ETCD_SERVER_PORT;
		}
	}

	if (etcd_init(etcd_server, etcd_port) == false) {
		return CELIX_BUNDLE_EXCEPTION;
	}

	etcdWatcher_addOwnNode(*watcher);

	if ((status = celixThreadMutex_create(&(*watcher)->watcherLock, NULL)) != CELIX_SUCCESS) {
		return status;
	}

	if ((status = celixThreadMutex_lock(&(*watcher)->watcherLock)) != CELIX_SUCCESS) {
		return status;
	}

	if ((status = celixThread_create(&(*watcher)->watcherThread, NULL, etcdWatcher_run, *watcher)) != CELIX_SUCCESS) {
		return status;
	}


	(*watcher)->running = true;

	if ((status = celixThreadMutex_unlock(&(*watcher)->watcherLock)) != CELIX_SUCCESS) {
		return status;
	}

	return status;
}

celix_status_t etcdWatcher_destroy(etcd_watcher_pt watcher) {
	celix_status_t status = CELIX_SUCCESS;
	char localNodePath[MAX_LOCALNODE_LENGTH];


	celixThreadMutex_lock(&(watcher->watcherLock));
	watcher->running = false;
	celixThreadMutex_unlock(&(watcher->watcherLock));

	watcher->running = false;

	celixThread_join(watcher->watcherThread, NULL);
	celixThreadMutex_destroy(&(watcher->watcherLock));

	// remove own registration
	status = etcdWatcher_getLocalNodePath(watcher->node_discovery->context, watcher->node_discovery->ownNode, &localNodePath[0]);

	if (status != CELIX_SUCCESS || etcd_del(localNodePath) == false) {
		printf("Cannot remove local discovery registration.");
	}



	free(watcher);

	return status;
}


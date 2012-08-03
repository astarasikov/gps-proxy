#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <cutils/sockets.h>

/* ANDROID-specific headers */
#define LOG_TAG "GPS-PROXY-LIB"
#include <utils/Log.h>

/* ANDROID libhardware headers */
#include <hardware/gps.h>
#include "gps-proxy.h"

#define GPS_EVENT_TIMEOUT 5000
#define GPS_SOCKET_RETRY_COUNT 5

static struct {
	int client_fd;
	pthread_t thread;
	pthread_mutex_t mutex;

	GpsXtraCallbacks *xtraCallbacks;
	AGpsCallbacks *aGpsCallbacks;
	GpsCallbacks *gpsCallbacks;
} proxy_lib_state = {
	.client_fd = -1,
	.mutex = PTHREAD_MUTEX_INITIALIZER,
};

/* Socket Interface */

static void gps_proxy_teardown(void) {
	pthread_join(proxy_lib_state.thread, NULL);
}

static int gps_proxy_socket_open(void) {
	int rc;
	int retry = GPS_SOCKET_RETRY_COUNT;
	int fd;

	gps_proxy_teardown();
	
	pthread_mutex_lock(&proxy_lib_state.mutex);
	while (retry--) {
		fd = socket_local_client(
			GPS_PROXY_SOCKET_NAME,
			ANDROID_SOCKET_NAMESPACE_ABSTRACT,
			SOCK_STREAM);
		if (fd >= 0) {
			proxy_lib_state.client_fd = fd;
			break;
		}
		else {
			LOGE("%s: fd %d, errno %d, err %s",
				__func__, fd, errno, strerror(errno));
		}

		usleep(500);
	}

	pthread_mutex_unlock(&proxy_lib_state.mutex);
	
	if (fd < 0) {
		return fd;
	}

	return 0;
}

/* XTRA Interface */
static int gps_xtra_init(GpsXtraCallbacks *callbacks) {
	pthread_mutex_lock(&proxy_lib_state.mutex);
	proxy_lib_state.xtraCallbacks = callbacks;

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_XTRA_INIT,
	};
	int rc = gps_rpc_call(&rpc);

	pthread_mutex_unlock(&proxy_lib_state.mutex);
	return rc;
}

static int inject_xtra_data(char *data, int length) {
	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_XTRA_INJECT_XTRA_DATA,
		.data = data,
		.data_size = length,
	};
	return gps_rpc_call(&rpc);
}

static const GpsXtraInterface sGpsXtraInterface = {
	.size = sizeof(GpsXtraInterface),
	.init = gps_xtra_init,
	.inject_xtra_data = inject_xtra_data,
};

/* AGPS Interface */
static void agps_init(AGpsCallbacks *callbacks) {
	pthread_mutex_lock(&proxy_lib_state.mutex);
	proxy_lib_state.aGpsCallbacks = callbacks;

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_AGPS_INIT,
	};
	gps_rpc_call(&rpc);
	
	pthread_mutex_unlock(&proxy_lib_state.mutex);
}

static int agps_data_conn_open(const char *apn) {
	int len = 0;
	if (!apn) {
		LOGE("%s: apn is NULL", __func__);
		goto fail;
	}
	
	len = strlen(apn);
	if (len > PAYLOAD_MAX) {
		LOGE("%s: apn lenght %d is too big", __func__, len);
		goto fail;
	}

	gps_rpc_agps_data_open_t data = {
		.length = strlen(apn),
	};
	memcpy(data.name, apn, data.length);

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_AGPS_DATA_CONN_OPEN,
		.data = (void*)&data,
		.data_size = sizeof(data),
	};

	return gps_rpc_call(&rpc);

fail:
	return -1;
}

static int agps_data_conn_closed(void) {
	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_AGPS_DATA_CONN_CLOSED,
	};
	return gps_rpc_call(&rpc);
}

static int agps_data_conn_failed(void) {
	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_AGPS_DATA_CONN_FAILED,
	};
	return gps_rpc_call(&rpc);
}

static int agps_set_server(AGpsType type, const char *hostname, int port) {
	int len = 0;
	if (!hostname) {
		LOGE("%s: hostname is NULL", __func__);
		goto fail;
	}
	
	len = strlen(hostname);
	if (len > PAYLOAD_MAX) {
		LOGE("%s: hostname lenght %d is too big", __func__, len);
		goto fail;
	}

	gps_rpc_agps_set_server_t data = {
		.type = type,
		.hostname_size = len,
		.port = port,
	};
	memcpy(data.hostname, hostname, data.hostname_size);

	gps_rpc_request_t rpc = {
		.code = GPS_PROXY_AGPS_AGPS_SET_SERVER,
		.data = (void*)&data,
		.data_size = sizeof(data),
	};

	return gps_rpc_call(&rpc);

fail:
	return -1;
}

static const AGpsInterface  sAGpsInterface = {
	.size = sizeof(AGpsInterface),
	.init = agps_init,
	.data_conn_open = agps_data_conn_open,
	.data_conn_closed = agps_data_conn_closed,
	.data_conn_failed = agps_data_conn_failed,
	.set_server = agps_set_server,
};

/* GPS Interface */

static int gps_init(GpsCallbacks *callbacks) {
	pthread_mutex_lock(&proxy_lib_state.mutex);
	proxy_lib_state.gpsCallbacks = callbacks;
	pthread_mutex_unlock(&proxy_lib_state.mutex);

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_INIT,
	};
	return gps_rpc_call(&rpc);
}

static int gps_start(void) {
	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_START,
	};
	return gps_rpc_call(&rpc);
}

static int gps_stop(void) {
	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_STOP,
	};
	return gps_rpc_call(&rpc);
}

static void gps_cleanup(void) {
	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_CLEANUP,
	};
	gps_rpc_call(&rpc);
}

static int gps_inject_time(GpsUtcTime time, int64_t timeReference,
	int uncertainty)
{
	struct gps_rpc_inject_time_t g = {
		.time = time,
		.timeReference = timeReference,
		.uncertainty = uncertainty,
	};

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_INJECT_TIME,
		.data = (void*)&g,
		.data_size = sizeof(g),
	};
	return gps_rpc_call(&rpc);
}

static int gps_inject_location(
	double latitude,
	double longitude,
	float accuracy)
{
	struct gps_rpc_inject_location_t g = {
		.latitude = latitude,
		.longitude = longitude,
		.accuracy = accuracy,
	};

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_INJECT_LOCATION,
		.data = (void*)&g,
		.data_size = sizeof(g),
	};
	return gps_rpc_call(&rpc);
}

static void gps_delete_aiding_data(GpsAidingData flags) {
	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_DELETE_AIDING_DATA,
		.data = (void*)&flags,
		.data_size = sizeof(flags),
	};
	gps_rpc_call(&rpc);
}

static int gps_set_position_mode(
	GpsPositionMode mode,
	GpsPositionRecurrence recurrence,
	uint32_t min_interval,
	uint32_t preferred_accuracy,
	uint32_t preferred_time)
{
	struct gps_rpc_position_mode_t pos = {
		.mode = mode,
		.recurrence = recurrence,
		.min_interval = min_interval,
		.preferred_accuracy = preferred_accuracy,
		.preferred_time = preferred_time,
	};

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_SET_POSITION_MODE,
		.data = (void*)&pos,
		.data_size = sizeof(pos),
	};

	return gps_rpc_call(&rpc);
}

static const void* gps_get_extension(const char* name) {
	int len = 0;
	if (!name) {
		LOGE("%s: name is NULL", __func__);
		goto fail;
	}
	
	len = strlen(name);
	if (len > PAYLOAD_MAX) {
		LOGE("%s: name lenght %d is too big", __func__, len);
		goto fail;
	}

	gps_rpc_get_extension_t data = {
		.length = strlen(name),
	};
	memcpy(data.name, name, data.length);

	struct gps_rpc_request_t rpc = {
		.code = GPS_PROXY_GPS_GET_EXTENSION,
		.data = (void*)&data,
		.data_size = sizeof(data),
	};

	gps_rpc_call(&rpc);
	//XXX: return value

fail:
	return NULL;
}

static const GpsInterface hardwareGpsInterface = {
	.size = sizeof(GpsInterface),
	.init = gps_init,
	.start = gps_start,
	.stop = gps_stop,
	.cleanup = gps_cleanup,
	.inject_time = gps_inject_time,
	.inject_location = gps_inject_location,
	.delete_aiding_data = gps_delete_aiding_data,
	.set_position_mode = gps_set_position_mode,
	.get_extension = gps_get_extension,
};

const GpsInterface * gps_get_gps_interface(struct gps_device_t *dev) {
	return &hardwareGpsInterface;
}

static int open_gps(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
	struct gps_device_t *dev = malloc(sizeof(struct gps_device_t));
	if (!dev) {
		goto fail;
	}
	memset(dev, 0, sizeof(*dev));
	
	dev->common.tag = HARDWARE_DEVICE_TAG;
	dev->common.version = 0;
	dev->common.module = (struct hw_module_t*)module;
	dev->get_gps_interface = gps_get_gps_interface;

	*device = (struct hw_device_t*)dev;
	return 0;

fail:
	*device = NULL;
	return -1;
}

static struct hw_module_methods_t gps_module_methods = {
	.open = open_gps
};

const struct hw_module_t HAL_MODULE_INFO_SYM = {
	.tag = HARDWARE_MODULE_TAG,
	.version_major = 1,
	.version_minor = 0,
	.id = GPS_HARDWARE_MODULE_ID,
	.name = "GPS HAL Proxy Module",
	.author = "Alexander Tarasikov",
	.methods = &gps_module_methods,
};


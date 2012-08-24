/**
 * This file is part of gps-proxy.
 *
 * Copyright (C) 2012 Alexander Tarasikov <alexander.tarasikov@gmail.com>
 *
 * gps-proxy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gps-proxy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with gps-proxy.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

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
#define LOG_TAG "[GPS-PROXY-LIB] "
#include <utils/Log.h>

/* ANDROID libhardware headers */
#include <hardware/gps.h>

#include <stc_rpc.h>
#include <stc_log.h>

#include "gps-rpc.h"

/******************************************************************************
 * Global Library State
 *****************************************************************************/

static int client_fd = -1;
static pthread_mutex_t gps_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gps_cond = PTHREAD_COND_INITIALIZER;

static GpsXtraCallbacks *xtraCallbacks = NULL;
static AGpsCallbacks *aGpsCallbacks = NULL;
static GpsCallbacks *gpsCallbacks = NULL;
static GpsNiCallbacks *niCallbacks = NULL;

static rpc_t *gps_rpc = NULL;

static pthread_t gps_rpc_thread;

static pthread_t gps_cb_thread;
static pthread_t ni_cb_thread;
static pthread_t agps_cb_thread;
static pthread_t xtra_cb_thread;

enum {
	READ_END = 0,
	WRITE_END = 1,
};

static int pipe_gps[2] = {-1, -1};
static int pipe_ni[2] = {-1, -1};
static int pipe_agps[2] = {-1, -1};
static int pipe_xtra[2] = {-1, -1};

#define CHECK_CLOSE(fd) \
do {\
	if (fd >= 0) {\
		close(fd); \
		fd = -1;\
	}\
} while (0)

/******************************************************************************
 * RPC Socket Interface
 *****************************************************************************/
static void gps_cb_thread_func(void* unused) {
	while (pipe_gps[0] >= 0) {
		struct rpc_request_hdr_t hdr;
		memset(&hdr, 0, sizeof(hdr));
		if (read(pipe_gps[READ_END], &hdr, sizeof(hdr)) != sizeof(hdr)) {
			RPC_ERROR("failed to read request header");
			break;
		}
		
		char *buf = hdr.buffer;
		size_t idx = 0;

		RPC_DEBUG("%s: request code %d", __func__, hdr.code);

		switch (hdr.code) {
		case GPS_LOC_CB:
			if (gpsCallbacks) {
				GpsLocation location;
				memset(&location, 0, sizeof(location));
				RPC_UNPACK(buf, idx, location);
				gpsCallbacks->location_cb(&location);
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case GPS_STATUS_CB:
			if (gpsCallbacks) {
				GpsStatus status;
				memset(&status, 0, sizeof(status));
				RPC_UNPACK(buf, idx, status);
				gpsCallbacks->status_cb(&status);
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case GPS_SV_STATUS_CB:
			if (gpsCallbacks) {
				GpsSvStatus status;
				memset(&status, 0, sizeof(status));
				RPC_UNPACK(buf, idx, status);
				gpsCallbacks->sv_status_cb(&status);
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case GPS_NMEA_CB:
			if (gpsCallbacks) {
				char nmea[RPC_PAYLOAD_MAX] = {};
				GpsUtcTime timestamp;
				int length;

				RPC_UNPACK(buf, idx, timestamp);
				RPC_UNPACK(buf, idx, length);
				RPC_UNPACK_RAW(buf, idx, nmea, length);
			
				gpsCallbacks->nmea_cb(timestamp,
					nmea, length);
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case GPS_SET_CAPABILITIES_CB:
			if (gpsCallbacks) {
				uint32_t caps = 0;
				RPC_UNPACK(buf, idx, caps);
				RPC_DEBUG("SET_CAPABILITIEST %x", caps);
				gpsCallbacks->set_capabilities_cb(caps);
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case GPS_ACQUIRE_LOCK_CB:
			if (gpsCallbacks) {
				gpsCallbacks->acquire_wakelock_cb();
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case GPS_RELEASE_LOCK_CB:
			if (gpsCallbacks) {
				gpsCallbacks->release_wakelock_cb();
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case GPS_REQUEST_UTC_TIME_CB:
			if (gpsCallbacks) {
				gpsCallbacks->request_utc_time_cb();
			}
			else {
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;
		}
fail:
		continue;
	}
}

static void agps_cb_thread_func(void* unused) {
	LOG_ENTRY;
	while (pipe_agps[0] >= 0) {
		struct rpc_request_hdr_t hdr;
		memset(&hdr, 0, sizeof(hdr));
		if (read(pipe_agps[READ_END], &hdr, sizeof(hdr)) != sizeof(hdr)) {
			RPC_ERROR("failed to read request header");
			break;
		}
		
		char *buf = hdr.buffer;
		size_t idx = 0;
		
		RPC_DEBUG("%s: request code %d", __func__, hdr.code);

		switch (hdr.code) {
		case AGPS_STATUS_CB:
			if (aGpsCallbacks) {
				AGpsStatus status;
				memset(&status, 0, sizeof(status));
				RPC_UNPACK(buf, idx, status);
				aGpsCallbacks->status_cb(&status);
			}
			else {
				RPC_ERROR("aGpsCallbacks == NULL");
			}
			break;
		}
fail:
		continue;
	}
	LOG_EXIT;
}

static void ni_cb_thread_func(void* unused) {
	LOG_ENTRY;
	while (pipe_ni[0] >= 0) {
		struct rpc_request_hdr_t hdr;
		memset(&hdr, 0, sizeof(hdr));
		if (read(pipe_ni[READ_END], &hdr, sizeof(hdr)) != sizeof(hdr)) {
			RPC_ERROR("failed to read request header");
			break;
		}
		
		char *buf = hdr.buffer;
		size_t idx = 0;
		
		RPC_DEBUG("%s: request code %d", __func__, hdr.code);

		switch (hdr.code) {
		case NI_NOTIFY_CB:
			if (niCallbacks) {
				GpsNiNotification nfy;
				memset(&nfy, 0, sizeof(nfy));
				RPC_UNPACK(buf, idx, nfy);
				niCallbacks->notify_cb(&nfy);
			}
			else {
				RPC_ERROR("niCallbacks == NULL");
			}
			break;
		}
fail:
		continue;
	}
	LOG_EXIT;
}

static void xtra_cb_thread_func(void* unused) {
	LOG_ENTRY;
	while (pipe_xtra[0] >= 0) {
		struct rpc_request_hdr_t hdr;
		memset(&hdr, 0, sizeof(hdr));
		if (read(pipe_xtra[READ_END], &hdr, sizeof(hdr)) != sizeof(hdr)) {
			RPC_ERROR("failed to read request header");
			break;
		}
		
		char *buf = hdr.buffer;
		size_t idx = 0;
		
		RPC_DEBUG("%s: request code %d", __func__, hdr.code);

		switch (hdr.code) {
		case XTRA_REQUEST_CB:
			if (xtraCallbacks) {
				xtraCallbacks->download_request_cb();
			}
			else {
				RPC_ERROR("xtraCallbacks == NULL");
			}
			break;
		}
fail:
		continue;
	}
	LOG_EXIT;
}

static int gps_rpc_handler(rpc_request_hdr_t *hdr, rpc_reply_t *reply) {
	LOG_ENTRY;
	
	int rc = -1;
	if (!hdr) {
		RPC_ERROR("hdr is NULL");
		goto fail;
	}

	if (!reply) {
		RPC_ERROR("reply is NULL");
		goto fail;
	}
	
	rc = 0;
	RPC_INFO("rpc handler code %x : %s", hdr->code,	gps_rpc_to_s(hdr->code));
	reply->code = hdr->code;
	
	char *buf = hdr->buffer;
	size_t idx = 0;

	switch (hdr->code) {
		case GPS_LOC_CB:
		case GPS_STATUS_CB:
		case GPS_SV_STATUS_CB:
		case GPS_NMEA_CB:
		case GPS_SET_CAPABILITIES_CB:
		case GPS_ACQUIRE_LOCK_CB:
		case GPS_RELEASE_LOCK_CB:
		case GPS_REQUEST_UTC_TIME_CB:
			if (gpsCallbacks) {
				write(pipe_gps[WRITE_END], hdr, sizeof(rpc_request_hdr_t));
			}
			else {
				rc = -1;
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;

		case AGPS_STATUS_CB:
			if (aGpsCallbacks) {
				write(pipe_agps[WRITE_END], hdr, sizeof(rpc_request_hdr_t));
			}
			else {
				rc = -1;
				RPC_ERROR("aGpsCallbacks == NULL");
			}
			break;

		case NI_NOTIFY_CB:
			if (niCallbacks) {
				write(pipe_ni[WRITE_END], hdr, sizeof(rpc_request_hdr_t));
			}
			else {
				rc = -1;
				RPC_ERROR("niCallbacks == NULL");
			}
			break;

		case XTRA_REQUEST_CB:
			if (xtraCallbacks) {
				write(pipe_xtra[WRITE_END], hdr, sizeof(rpc_request_hdr_t));
			}
			else {
				rc = -1;
				RPC_ERROR("xtraCallbacks == NULL");
			}
			break;
		
		case AGPS_CREATE_THREAD_CB:
			if (aGpsCallbacks) {
				agps_cb_thread =
					aGpsCallbacks->create_thread_cb("agps",
					agps_cb_thread_func, NULL);
			}
			else {
				rc = -1;
				RPC_ERROR("aGpsCallbacks == NULL");
			}
			break;
		case NI_CREATE_THREAD_CB:
			if (niCallbacks) {
				ni_cb_thread = niCallbacks->create_thread_cb("ni",
					ni_cb_thread_func, NULL);
			}
			else {
				rc = -1;
				RPC_ERROR("niCallbacks == NULL");
			}
			break;
		case GPS_CREATE_THREAD_CB:
			if (gpsCallbacks) {
				gps_cb_thread = gpsCallbacks->create_thread_cb("gps",
					gps_cb_thread_func, NULL);
			}
			else {
				rc = -1;
				RPC_ERROR("gpsCallbacks == NULL");
			}
			break;
		case XTRA_CREATE_THREAD_CB:
			if (xtraCallbacks) {
				xtra_cb_thread = xtraCallbacks->create_thread_cb("xtra",
					xtra_cb_thread_func, NULL);
			}
			else {
				rc = -1;
				RPC_ERROR("xtraCallbacks == NULL");
			}
			break;
		
		default:
			RPC_ERROR("unknown code %x", hdr->code);
			break;
	}

fail:
	LOG_EXIT;
	return rc;
}

static int rpc_call_result(rpc_t *rpc, rpc_request_t *req)
{
	LOG_ENTRY;
	int rc = -1;

	if (!rpc) {
		RPC_ERROR("rpc is NULL");
		goto fail;
	}

	if (!req) {
		RPC_ERROR("request is NULL");
		goto fail;
	}

	rc = rpc_call(rpc, req);
	if (rc < 0) {
		RPC_ERROR("rpc_call failed %d", rc);
		goto fail;
	}
	RPC_DEBUG("%s: rpc_call done", __func__);

	size_t idx = 0;
	RPC_UNPACK(req->reply.buffer, idx, rc);
fail:
	LOG_EXIT;
	return rc;
}

/******************************************************************************
 * RPC Transport Setup
 *****************************************************************************/
static void close_pipes(void) {
	CHECK_CLOSE(pipe_gps[READ_END]);
	CHECK_CLOSE(pipe_gps[WRITE_END]);
	CHECK_CLOSE(pipe_agps[READ_END]);
	CHECK_CLOSE(pipe_agps[WRITE_END]);
	CHECK_CLOSE(pipe_ni[READ_END]);
	CHECK_CLOSE(pipe_ni[WRITE_END]);
	CHECK_CLOSE(pipe_xtra[READ_END]);
	CHECK_CLOSE(pipe_xtra[WRITE_END]);
}

static void gps_proxy_teardown(void) {
	LOG_ENTRY;
	if (gps_rpc) {
		rpc_join(gps_rpc);
		rpc_free(gps_rpc);
		gps_rpc = NULL;
	}

	CHECK_CLOSE(client_fd);

	pthread_kill(gps_cb_thread, SIGKILL);
	pthread_kill(ni_cb_thread, SIGKILL);
	pthread_kill(agps_cb_thread, SIGKILL);
	pthread_kill(xtra_cb_thread, SIGKILL);

	xtraCallbacks = NULL;
	aGpsCallbacks = NULL;
	gpsCallbacks = NULL;
	niCallbacks = NULL;

	close_pipes();

	LOG_EXIT;
}

static int start_rpc(int fd) {
	struct rpc *rpc = NULL;
	
	rpc = rpc_alloc();
	if (!rpc) {
		RPC_ERROR("out of memory");
		goto fail;
	}

	if (rpc_init(fd, gps_rpc_handler, rpc)) {
		RPC_ERROR("failed to init RPC");
		goto fail;
	}

	if (rpc_start(rpc)) {
		RPC_ERROR("failed to start RPC");
		goto fail;
	}

	gps_rpc = rpc;

	return 0;
fail:
	if (rpc) {
		rpc_free(rpc);
	}
	return -1;
}

static int gps_proxy_socket_open(void) {
	int rc;
	int retry = GPS_SOCKET_RETRY_COUNT;
	int fd = -1;

	LOG_ENTRY;

	while (retry--) {
		fd = socket_local_client(
			GPS_RPC_SOCKET_NAME,
			ANDROID_SOCKET_NAMESPACE_ABSTRACT,
			SOCK_STREAM);
		if (fd >= 0) {
			break;
		}
		else {
			RPC_ERROR("%s: fd %d, errno %d, err %s",
				__func__, fd, errno, strerror(errno));
		}

		usleep(500);
	}

	LOG_EXIT;

	return fd;
}

static void* gps_client(void* unused) {
	int client_fd = -1;

	LOG_ENTRY;

	client_fd = gps_proxy_socket_open();
	if (client_fd < 0) {
		RPC_ERROR("failed to open the socket");
		goto fail;
	}
		
	if (start_rpc(client_fd)) {
		RPC_ERROR("failed to connect to the RPC server");
		goto fail;
	}

	goto done;

fail:
	if (client_fd >= 0) {
		close(client_fd);
	}

done:
	pthread_cond_broadcast(&gps_cond);
	LOG_EXIT;
	return NULL;
}

static int start_gps_client(void) {
	LOG_ENTRY;
	int rc = -1;

	if (pipe(pipe_gps) < 0) {
		RPC_ERROR("failed to create GPS pipe");
		goto fail;
	}

	if (pipe(pipe_ni) < 0) {
		RPC_ERROR("fail to create NI pipe");
		goto fail;
	}

	if (pipe(pipe_agps) < 0) {
		RPC_ERROR("failed to create AGPS pipe");
		goto fail;
	}

	if (pipe(pipe_xtra) < 0) {
		RPC_ERROR("failed to create XTRA pipe");
		goto fail;
	}
	
	pthread_create(&gps_rpc_thread, NULL, gps_client, NULL);
	pthread_mutex_lock(&gps_mutex);
	pthread_cond_wait(&gps_cond, NULL);
	pthread_mutex_unlock(&gps_mutex);

	if (!gps_rpc) {
		goto fail;
	}
	
	rc = 0;
	goto done;
	
fail:
	close_pipes();
done:
	LOG_EXIT;
	return rc;
}

/******************************************************************************
 * XTRA Interface
 *****************************************************************************/
static int gps_xtra_init(GpsXtraCallbacks *callbacks) {
	xtraCallbacks = callbacks;
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_XTRA_INIT,
		},
	};
	
	LOG_ENTRY;
	int rc = rpc_call_result(gps_rpc, &req);
	LOG_EXIT;
	return rc;
}

static int inject_xtra_data(char *data, int length) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_XTRA_INJECT_XTRA_DATA,
		},
	};

	LOG_ENTRY;

	int rc = -1;
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK(buf, idx, length);
	RPC_PACK_RAW(buf, idx, data, length);

	rc = rpc_call_result(gps_rpc, &req);

fail:
	LOG_EXIT;
	return rc;
}

static GpsXtraInterface sGpsXtraInterface = {
	.size = sizeof(GpsXtraInterface),
	.init = gps_xtra_init,
	.inject_xtra_data = inject_xtra_data,
};

/******************************************************************************
 * AGPS Interface
 *****************************************************************************/
static void agps_init(AGpsCallbacks *callbacks) {
	aGpsCallbacks = callbacks;
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_AGPS_INIT,
		},
	};

	LOG_ENTRY;
	rpc_call(gps_rpc, &req);
fail:
	LOG_EXIT;
	return;
}

static int agps_data_conn_open(const char *apn) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_AGPS_DATA_CONN_OPEN,
		},
	};

	LOG_ENTRY;
	int rc = -1;
	char *buf = req.header.buffer;
	size_t idx = 0;

	if (!apn) {
		RPC_DEBUG("%s: apn is NULL", __func__);
		goto fail;
	}

	RPC_PACK_S(buf, idx, apn);

	rc = rpc_call_result(gps_rpc, &req);
fail:
	LOG_EXIT;
	return rc;
}

static int agps_data_conn_closed(void) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_AGPS_DATA_CONN_CLOSED,
		},
	};

	LOG_ENTRY;
	int rc = rpc_call_result(gps_rpc, &req);
	LOG_EXIT;
	return rc;
}

static int agps_data_conn_failed(void) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_AGPS_DATA_CONN_FAILED,
		},
	};

	LOG_ENTRY;
	int rc = rpc_call_result(gps_rpc, &req);
	LOG_EXIT;
	return rc;
}

static int agps_set_server(AGpsType type, const char *hostname, int port) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_AGPS_AGPS_SET_SERVER,
		},
	};
	
	LOG_ENTRY;

	int rc = -1;
	char *buf = req.header.buffer;
	size_t idx = 0;

	if (!hostname) {
		RPC_ERROR("%s: hostname is NULL", __func__);
		goto fail;
	}

	RPC_PACK(buf, idx, type);
	RPC_PACK(buf, idx, port);
	RPC_PACK_S(buf, idx, hostname);

	rc = rpc_call_result(gps_rpc, &req);
fail:
	LOG_EXIT;
	return rc;
}

static AGpsInterface sAGpsInterface = {
	.size = sizeof(AGpsInterface),
	.init = agps_init,
	.data_conn_open = agps_data_conn_open,
	.data_conn_closed = agps_data_conn_closed,
	.data_conn_failed = agps_data_conn_failed,
	.set_server = agps_set_server,
};

/******************************************************************************
 * NI Interface
 *****************************************************************************/
static void ni_init(GpsNiCallbacks *callbacks) {
	niCallbacks = callbacks;
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_NI_INIT,
		},
	};

	LOG_ENTRY;
	rpc_call(gps_rpc, &req);
	LOG_EXIT;
}

static void ni_respond(int notif_id, GpsUserResponseType user_response) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_NI_RESPOND,
		},
	};

	LOG_ENTRY;

	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK(buf, idx, notif_id);
	RPC_PACK(buf, idx, user_response);

	rpc_call(gps_rpc, &req);
fail:
	LOG_EXIT;
	return;
}

static const GpsNiInterface sGpsNiInterface = {
	.size = sizeof(GpsNiInterface),
	.init = ni_init,
	.respond = ni_respond,
};

/******************************************************************************
 * GPS Interface
 *****************************************************************************/
static int gps_init(GpsCallbacks *callbacks) {
	gpsCallbacks = callbacks;
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_INIT,
		},
	};
	
	LOG_ENTRY;
	int rc = rpc_call_result(gps_rpc, &req);
	LOG_EXIT;
fail:
	return rc;
}

static int gps_start(void) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_START,
		},
	};

	LOG_ENTRY;
	int rc = rpc_call_result(gps_rpc, &req);
	LOG_EXIT;
	return rc;
}

static int gps_stop(void) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_STOP,
		},
	};

	LOG_ENTRY;
	int rc = rpc_call_result(gps_rpc, &req);
	LOG_EXIT;
	return rc;
}

static void gps_cleanup(void) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_CLEANUP,
		},
	};

	LOG_ENTRY;
	rpc_call(gps_rpc, &req);
	//gps_proxy_teardown();
	LOG_EXIT;
}

static int gps_inject_time(GpsUtcTime time, int64_t timeReference,
	int uncertainty)
{
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_INJECT_TIME,
		},
	};
	LOG_ENTRY;

	int rc = -1;
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK(buf, idx, time);
	RPC_PACK(buf, idx, timeReference);
	RPC_PACK(buf, idx, uncertainty);

	rc = rpc_call_result(gps_rpc, &req);
fail:
	LOG_EXIT;
	return rc;
}

static int gps_inject_location(
	double latitude,
	double longitude,
	float accuracy)
{
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_INJECT_LOCATION,
		},
	};
	LOG_ENTRY;

	int rc = -1;
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK(buf, idx, latitude);
	RPC_PACK(buf, idx, longitude);
	RPC_PACK(buf, idx, accuracy);

	rc = rpc_call_result(gps_rpc, &req);
fail:
	LOG_EXIT;
	return rc;
}

static void gps_delete_aiding_data(GpsAidingData flags) {
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_DELETE_AIDING_DATA,
		},
	};
	LOG_ENTRY;

	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK(buf, idx, flags);

	rpc_call(gps_rpc, &req);
fail:
	LOG_EXIT;
	return;
}

static int gps_set_position_mode(
	GpsPositionMode mode,
	GpsPositionRecurrence recurrence,
	uint32_t min_interval,
	uint32_t preferred_accuracy,
	uint32_t preferred_time)
{	
	struct rpc_request_t req = {
		.header = {
			.code = GPS_PROXY_GPS_SET_POSITION_MODE,
		},
	};
	LOG_ENTRY;

	int rc = -1;
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK(buf, idx, mode);
	RPC_PACK(buf, idx, recurrence);
	RPC_PACK(buf, idx, min_interval);
	RPC_PACK(buf, idx, preferred_accuracy);
	RPC_PACK(buf, idx, preferred_time);

	rc = rpc_call_result(gps_rpc, &req);
fail:
	LOG_EXIT;
	return rc;
}

static const void *gps_get_extension(const char *name) {
	RPC_DEBUG("%s:%s", __func__, name);
	if (!strcmp(name, GPS_XTRA_INTERFACE)) {
		return &sGpsXtraInterface;
	}
	else if (!strcmp(name, AGPS_INTERFACE)) {
		return &sAGpsInterface;
	}
	else if (!strcmp(name, GPS_NI_INTERFACE)) {
		return &sGpsNiInterface;
	}
	return NULL;
}

static GpsInterface hardwareGpsInterface = {
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

/******************************************************************************
 * Library Interface
 *****************************************************************************/
GpsInterface* gps_get_hardware_interface() {
	LOG_ENTRY;
	return &hardwareGpsInterface;
}

static int open_gps(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
	LOG_ENTRY;
	struct gps_device_t *dev = malloc(sizeof(struct gps_device_t));
	if (!dev) {
		goto fail;
	}
	memset(dev, 0, sizeof(*dev));
	
	dev->common.tag = HARDWARE_DEVICE_TAG;
	dev->common.version = 0;
	dev->common.module = (struct hw_module_t*)module;
	dev->get_gps_interface = gps_get_hardware_interface;

	if (start_gps_client()) {
		RPC_ERROR("failed to start rpc gps client thread");
		goto fail;
	}
	
	*device = (struct hw_device_t*)dev;
	LOG_EXIT;
	return 0;

fail:
	if (dev) {
		free(dev);
	}

	*device = NULL;
	LOG_EXIT;
	return -1;
}

static struct hw_module_methods_t gps_module_methods = {
	.open = open_gps
};

struct hw_module_t HAL_MODULE_INFO_SYM = {
	.tag = HARDWARE_MODULE_TAG,
	.version_major = 1,
	.version_minor = 0,
	.id = GPS_HARDWARE_MODULE_ID,
	.name = "GPS HAL Proxy Module",
	.author = "Alexander Tarasikov",
	.methods = &gps_module_methods,
};

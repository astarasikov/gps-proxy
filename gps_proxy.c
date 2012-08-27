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
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>

/* ANDROID local sockets */
#include <sys/socket.h>
#include <sys/un.h>
#include <cutils/sockets.h>

/* ANDROID libhardware headers */
#include <hardware/gps.h>

#define LOG_TAG "[GPS-PROXY-SRV]"
#include <stc_rpc.h>
#include <stc_log.h>

#include "gps-rpc.h"

#define GPS_LIBRARY_NAME "/system/vendor/lib/hw/gps.blob.so"

static rpc_t *g_rpc = NULL;
static void *lib_handle = NULL;

static GpsInterface *origGpsInterface = NULL;
static GpsXtraInterface *origGpsXtraInterface = NULL;
static AGpsInterface *origAGpsInterface = NULL;
static GpsNiInterface *origNiInterface = NULL;
static AGpsRilInterface *origRilInterface = NULL;

/******************************************************************************
 * Function prototypes
 *****************************************************************************/
static int load_gps_library(void);
static void free_gps_library(void);

static int gps_inited = 0;

/******************************************************************************
 * Outgoing RPC Interface
 *****************************************************************************/

#define MAX_THREADS 10
static pthread_t lib_threads[MAX_THREADS];
static int num_lib_threads = 0;

static pthread_t create_thread_cb(
	const char *name,
	void (*start)(void *),
	void *arg
)
{
	LOG_ENTRY;
	RPC_DEBUG("%s: name %s", __func__, name);
	int rc = -1;
	
	if (!start) {
		RPC_ERROR("NULL func pointer");
		goto fail;
	}

	if (num_lib_threads >= MAX_THREADS) {
		RPC_ERROR("%s: already created maximal number of threads", __func__);
		goto fail;
	}
	
	int idx = num_lib_threads;
	num_lib_threads++;
	pthread_create(lib_threads + idx, NULL, start, arg);

	LOG_EXIT;
	return lib_threads[idx];

fail:
	return 0;
}

/******************************************************************************
 * XTRA Interface
 *****************************************************************************/
static void gps_xtra_download_request_cb(void) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = XTRA_REQUEST_CB,
		},
	};
	
	rpc_call_noreply(g_rpc, &req);
fail:
	LOG_EXIT;
}

static pthread_t xtra_create_thread_cb(
	const char *name,
	void (*start)(void *),
	void *arg
)
{
	LOG_ENTRY;
	rpc_request_t req = {
		.header = {
			.code =	XTRA_CREATE_THREAD_CB,
		},
	};

	rpc_call_noreply(g_rpc, &req);

	pthread_t ret = create_thread_cb(name, start, arg);

	LOG_EXIT;
	return ret;
}


static GpsXtraCallbacks gpsXtraCallbacks = {
	.download_request_cb = gps_xtra_download_request_cb,
	.create_thread_cb = xtra_create_thread_cb,
};

/******************************************************************************
 * NI Interface
 *****************************************************************************/
static pthread_t ni_create_thread_cb(
	const char *name,
	void (*start)(void *),
	void *arg
)
{
	LOG_ENTRY;
	rpc_request_t req = {
		.header = {
			.code = NI_CREATE_THREAD_CB,
		},
	};

	rpc_call_noreply(g_rpc, &req);

	pthread_t ret = create_thread_cb(name, start, arg);

	LOG_EXIT;
	return ret;
}

static void gps_ni_notify_cb(GpsNiNotification *notification) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = NI_NOTIFY_CB,
		},
	};

	if (!notification) {
		RPC_ERROR("%s: notification is NULL", __func__);
		goto fail;
	}
	
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK_RAW(buf, idx, notification, sizeof(GpsNiNotification));
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static GpsNiCallbacks gpsNiCallbacks = {
	.notify_cb = gps_ni_notify_cb,
	.create_thread_cb = ni_create_thread_cb,
};

/******************************************************************************
 * GPS Interface
 *****************************************************************************/
static pthread_t gps_create_thread_cb(
	const char *name,
	void (*start)(void *),
	void *arg
)
{
	LOG_ENTRY;
	rpc_request_t req = {
		.header = {
			.code = GPS_CREATE_THREAD_CB,
		},
	};

	rpc_call_noreply(g_rpc, &req);

	pthread_t ret = create_thread_cb(name, start, arg);

	LOG_EXIT;
	return ret;
}

static void gps_location_cb(GpsLocation *location) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = GPS_LOC_CB,
		},
	};

	if (!location) {
		RPC_ERROR("%s: location is NULL", __func__);
		goto fail;
	}
	
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK_RAW(buf, idx, location, sizeof(GpsLocation));
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static void gps_status_cb(GpsStatus *status) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = GPS_STATUS_CB,
		},
	};

	if (!status) {
		RPC_ERROR("%s: status is NULL", __func__);
		goto fail;
	}
	
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK_RAW(buf, idx, status, sizeof(GpsStatus));
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static void gps_sv_status_cb(GpsSvStatus *sv_info) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = GPS_SV_STATUS_CB,
		},
	};

	if (!sv_info) {
		RPC_ERROR("%s: sv_info is NULL", __func__);
		goto fail;
	}
	
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK_RAW(buf, idx, sv_info, sizeof(GpsSvStatus));
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static void gps_nmea_cb(GpsUtcTime timestamp,
	const char *nmea, int length)
{
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = GPS_NMEA_CB,
		},
	};

	if (!nmea || !length) {
		RPC_ERROR("%s: nmea is NULL", __func__);
		goto fail;
	}
	
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK(buf, idx, timestamp);
	RPC_PACK(buf, idx, length);
	RPC_PACK_RAW(buf, idx, nmea, length);
	req.header.buffer[RPC_PAYLOAD_MAX - 1] = '\0';
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static void gps_set_capabilities_cb(uint32_t capabilities) {
	LOG_ENTRY;

	if (!gps_inited) {
		RPC_DEBUG("%s: not yet ready", __func__);
		goto fail;
	}

	rpc_request_t req = {
		.header = {
			.code = GPS_SET_CAPABILITIES_CB,
		},
	};
	
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_DEBUG("%s: caps=%x", __func__, capabilities);

	RPC_PACK(buf, idx, capabilities);
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static void gps_acquire_wakelock_cb(void) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = GPS_ACQUIRE_LOCK_CB,
		},
	};
	
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static void gps_release_wakelock_cb(void) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = GPS_RELEASE_LOCK_CB,
		},
	};
	
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static void gps_request_utc_time_cb(void) {
	LOG_ENTRY;
	
	rpc_request_t req = {
		.header = {
			.code = GPS_REQUEST_UTC_TIME_CB,
		},
	};
	
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static GpsCallbacks gpsCallbacks = {
	.size = sizeof(GpsCallbacks),
	.location_cb = gps_location_cb,
	.status_cb = gps_status_cb,
	.sv_status_cb = gps_sv_status_cb,
	.nmea_cb = gps_nmea_cb,
	.set_capabilities_cb = gps_set_capabilities_cb,
	.acquire_wakelock_cb = gps_acquire_wakelock_cb,
	.release_wakelock_cb = gps_release_wakelock_cb,
	.create_thread_cb = gps_create_thread_cb,
	.request_utc_time_cb = gps_request_utc_time_cb,
};

/******************************************************************************
 * AGPS Interface
 *****************************************************************************/
static pthread_t agps_create_thread_cb(
	const char *name,
	void (*start)(void *),
	void *arg
)
{
	LOG_ENTRY;
	rpc_request_t req = {
		.header = {
			.code =	AGPS_CREATE_THREAD_CB,
		},
	};

	rpc_call_noreply(g_rpc, &req);

	pthread_t ret = create_thread_cb(name, start, arg);

	LOG_EXIT;
	return ret;
}

static void gps_agps_status_cb(AGpsStatus *status) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = AGPS_STATUS_CB,
		},
	};

	if (!status) {
		RPC_ERROR("%s: status is NULL", __func__);
		goto fail;
	}
	
	char *buf = req.header.buffer;
	size_t idx = 0;

	RPC_PACK_RAW(buf, idx, status, sizeof(AGpsStatus));
	rpc_call_noreply(g_rpc, &req);

fail:
	LOG_EXIT;
}

static AGpsCallbacks aGpsCallbacks = {
	.status_cb = gps_agps_status_cb,
	.create_thread_cb = agps_create_thread_cb,
};

/******************************************************************************
 * RIL Interface
 *****************************************************************************/
static pthread_t ril_create_thread_cb(
	const char *name,
	void (*start)(void *),
	void *arg
)
{
	LOG_ENTRY;
	rpc_request_t req = {
		.header = {
			.code =	RIL_CREATE_THREAD_CB,
		},
	};

	rpc_call_noreply(g_rpc, &req);

	pthread_t ret = create_thread_cb(name, start, arg);

	LOG_EXIT;
	return ret;
}

static void ril_request_set_id(uint32_t flags) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = RIL_SET_ID_CB,
		},
	};
	
	char *buf = req.header.buffer;
	size_t idx = 0;
	
	RPC_PACK(buf, idx, flags);
	rpc_call_noreply(g_rpc, &req);
fail:
	LOG_EXIT;
}

static void ril_request_ref_loc(uint32_t flags) {
	LOG_ENTRY;

	rpc_request_t req = {
		.header = {
			.code = RIL_REF_LOC_CB,
		},
	};
	
	char *buf = req.header.buffer;
	size_t idx = 0;
	
	RPC_PACK(buf, idx, flags);
	rpc_call_noreply(g_rpc, &req);
fail:
	LOG_EXIT;
}

static AGpsRilCallbacks rilCallbacks = {
	.create_thread_cb = ril_create_thread_cb,
	.request_setid = ril_request_set_id,
	.request_refloc = ril_request_ref_loc,
};

/******************************************************************************
 * Incoming RPC Interface
 *****************************************************************************/
static int gps_srv_rpc_handler(rpc_request_hdr_t *hdr, rpc_reply_t *reply) {
	int rc = 0;
	if (!hdr) {
		RPC_ERROR("hdr is NULL");
		goto fail;
	}

	if (!reply) {
		RPC_ERROR("reply is NULL");
		goto fail;
	}

	RPC_DEBUG("+request code %x : %s", hdr->code, gps_rpc_to_s(hdr->code));
	reply->code = hdr->code;
	
	char *buf = hdr->buffer;
	size_t idx = 0;

	char *rbuf = reply->buffer;
	size_t ridx = 0;

	switch (hdr->code) {
		case RIL_INIT:
			if (origRilInterface && origRilInterface->init) {
				origRilInterface->init(&rilCallbacks);
			}
			else {
				RPC_ERROR("origRilInterface == NULL");
				rc = -1;
			}
			break;
		case RIL_SET_REF_LOC:
			if (origRilInterface && origRilInterface->set_ref_location) {
				AGpsRefLocation loc;
				size_t sz_struct;
				RPC_UNPACK(buf, idx, sz_struct);
				RPC_UNPACK_RAW(buf, idx, &loc, sz_struct);
				origRilInterface->set_ref_location(&loc, sz_struct);
			}
			else {
				RPC_ERROR("origRilInterface == NULL");
				rc = -1;
			}
			break;
		case RIL_SET_SET_ID:
			if (origRilInterface && origRilInterface->set_set_id) {
				AGpsSetIDType type;
				char setid[RPC_PAYLOAD_MAX] = {};
				RPC_UNPACK(buf, idx, type);
				RPC_UNPACK_S(buf, idx, setid);
				origRilInterface->set_set_id(type, setid);
			}
			else {
				RPC_ERROR("origRilInterface == NULL");
				rc = -1;
			}
			break;
		case RIL_UPDATE_NET_STATE:
			if (origRilInterface && origRilInterface->update_network_state) {
				int connected, type, roaming;
				char extra[RPC_PAYLOAD_MAX] = {};

				RPC_UNPACK(buf, idx, connected);
				RPC_UNPACK(buf, idx, type);
				RPC_UNPACK(buf, idx, roaming);
				RPC_UNPACK_S(buf, idx, extra);

				origRilInterface->update_network_state(connected, type, roaming,
					extra);
			}
			else {
				RPC_ERROR("origRilInterface == NULL");
				rc = -1;
			}
		case RIL_NI_MSG:
			break;
			if (origRilInterface && origRilInterface->ni_message) {
				uint8_t msg[RPC_PAYLOAD_MAX] = {};
				size_t len;
				RPC_UNPACK(buf, idx, len);
				RPC_UNPACK_RAW(buf, idx, msg, len);
				origRilInterface->ni_message(msg, len);
			}
			else {
				RPC_ERROR("origRilInterface == NULL");
				rc = -1;
			}
			break;
		case RIL_UPDATE_NET_AVAILABILITY:
			if (origRilInterface && origRilInterface->update_network_availability) {
				char apn[RPC_PAYLOAD_MAX] = {};
				int available;
				RPC_UNPACK(buf, idx, available);
				RPC_UNPACK_S(buf, idx, apn);
				origRilInterface->update_network_availability(available, apn);
			}
			else {
				RPC_ERROR("origRilInterface == NULL");
				rc = -1;
			}
			break;

		case GPS_PROXY_XTRA_INIT:
			if (origGpsXtraInterface && origGpsXtraInterface->init) {
				rc = origGpsXtraInterface->init(&gpsXtraCallbacks);
			}
			else {
				RPC_ERROR("origGpsXtraInterface == NULL");
				rc = -1;
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_XTRA_INJECT_XTRA_DATA:
			{
				int length;
				char data[RPC_PAYLOAD_MAX - 4];

				RPC_UNPACK(buf, idx, length);
				RPC_UNPACK_RAW(buf, idx, data, length);
				
				if (origGpsXtraInterface && origGpsXtraInterface->inject_xtra_data) {
					rc = origGpsXtraInterface->inject_xtra_data(data, length);
				}
				else {
					RPC_ERROR("origGpsXtraInterface == NULL");
					rc = -1;
				}
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_AGPS_INIT:
			if (origAGpsInterface && origAGpsInterface->init) {
				origAGpsInterface->init(&aGpsCallbacks);
			}
			else {
				RPC_ERROR("origAGpsInterface == NULL");
				rc = -1;
			}
			break;
		case GPS_PROXY_AGPS_DATA_CONN_OPEN:
			{
				char str[RPC_PAYLOAD_MAX] = {};
				RPC_UNPACK_S(buf, idx, str);
				
				if (origAGpsInterface && origAGpsInterface->data_conn_open) {
					rc = origAGpsInterface->data_conn_open(str);
				}
				else {
					RPC_ERROR("origAGpsInterface == NULL");
					rc = -1;
				}
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_AGPS_DATA_CONN_CLOSED:
			if (origAGpsInterface && origAGpsInterface->data_conn_closed) {
				rc = origAGpsInterface->data_conn_closed();
			}
			else {
				RPC_ERROR("origAGpsInterface == NULL");
				rc = -1;
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_AGPS_DATA_CONN_FAILED:
			if (origAGpsInterface && origAGpsInterface->data_conn_failed) {
				rc = origAGpsInterface->data_conn_failed();
			}
			else {
				RPC_ERROR("origAGpsInterface == NULL");
				rc = -1;
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_AGPS_AGPS_SET_SERVER:
			{
				char hostname[RPC_PAYLOAD_MAX] = {};
				AGpsType type;
				int port;

				RPC_UNPACK(buf, idx, type);
				RPC_UNPACK(buf, idx, port);
				RPC_UNPACK_S(buf, idx, hostname);
	
		
				if (origAGpsInterface && origAGpsInterface->set_server) {
					rc = origAGpsInterface->set_server(
						type, hostname, port
					);
				}
				else {
					RPC_ERROR("origAGpsInterface == NULL");
					rc = -1;
				}
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_NI_INIT:
			if (origNiInterface && origNiInterface->init) {
				origNiInterface->init(&gpsNiCallbacks);
			}
			else {
				RPC_ERROR("origNiInterface == NULL");
				rc = -1;
			}
			break;
		case GPS_PROXY_NI_RESPOND:
			{
				int notif_id;
				GpsUserResponseType user_response;

				RPC_UNPACK(buf, idx, notif_id);
				RPC_UNPACK(buf, idx, user_response);

				if (origNiInterface && origNiInterface->respond) {
					origNiInterface->respond(notif_id,
						user_response);
				}
				else {
					RPC_ERROR("origNiInterface == NULL");
					rc = -1;
				}
			}
			break;
		case GPS_PROXY_GPS_INIT:
			if (origGpsInterface && origGpsInterface->init) {
				RPC_DEBUG("calling GPS_INIT");
				rc = origGpsInterface->init(&gpsCallbacks);
				RPC_INFO("GPS_INIT rc %d", rc);

				gps_inited = 1;
			}
			else {
				RPC_ERROR("origGpsInterface == NULL");
				rc = -1;
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_GPS_START:
			if (origGpsInterface && origGpsInterface->start) {
				rc = origGpsInterface->start();
			}
			else {
				RPC_ERROR("origGpsInterface == NULL");
				rc = -1;
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_GPS_STOP:
			if (origGpsInterface && origGpsInterface->stop) {
				rc = origGpsInterface->stop();
			}
			else {
				RPC_ERROR("origGpsInterface == NULL");
				rc = -1;
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_GPS_CLEANUP:
			if (origGpsInterface && origGpsInterface->cleanup) {
				origGpsInterface->cleanup();

				gps_inited = 0;
			}
			else {
				RPC_ERROR("origGpsInterface == NULL");
				rc = -1;
			}
			break;
		case GPS_PROXY_GPS_INJECT_TIME:
			{
				GpsUtcTime time;
				int64_t timeReference;
				int uncertainty;

				RPC_UNPACK(buf, idx, time);
				RPC_UNPACK(buf, idx, timeReference);
				RPC_UNPACK(buf, idx, uncertainty);

				if (origGpsInterface && origGpsInterface->inject_time) {
					rc = origGpsInterface->inject_time(time, timeReference,
						uncertainty);
				}
				else {
					RPC_ERROR("origGpsInterface == NULL");
					rc = -1;
				}
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_GPS_INJECT_LOCATION:
			{
				double latitude;
				double longitude;
				float accuracy;

				RPC_UNPACK(buf, idx, latitude);
				RPC_UNPACK(buf, idx, longitude);
				RPC_UNPACK(buf, idx, accuracy);
				
				if (origGpsInterface && origGpsInterface->inject_location) {
					rc = origGpsInterface->inject_location(latitude, longitude,
						accuracy);
				}
				else {
					RPC_ERROR("origGpsInterface == NULL");
					rc = -1;
				}
			}
			RPC_PACK(rbuf, ridx, rc);
			break;
		case GPS_PROXY_GPS_DELETE_AIDING_DATA:
			{
				GpsAidingData flags;
				RPC_UNPACK(buf, idx, flags);
				
				if (origGpsInterface && origGpsInterface->delete_aiding_data) {
					origGpsInterface->delete_aiding_data(flags);
				}
				else {
					RPC_ERROR("origGpsInterface == NULL");
					rc = -1;
				}
			}
			break;
		case GPS_PROXY_GPS_SET_POSITION_MODE:
			{
				GpsPositionMode mode;
				GpsPositionRecurrence recurrence;
				uint32_t min_interval;
				uint32_t preferred_accuracy;
				uint32_t preferred_time;

				RPC_UNPACK(buf, idx, mode);
				RPC_UNPACK(buf, idx, recurrence);
				RPC_UNPACK(buf, idx, min_interval);
				RPC_UNPACK(buf, idx, preferred_accuracy);
				RPC_UNPACK(buf, idx, preferred_time);

				if (origGpsInterface && origGpsInterface->set_position_mode) {
					rc = origGpsInterface->set_position_mode(
						mode, recurrence, min_interval,
						preferred_accuracy, preferred_time
					);
				}
				else {
					RPC_ERROR("origGpsInterface == NULL");
					rc = -1;
				}
			}
			RPC_PACK(rbuf, idx, rc);
			break;
	}
	RPC_DEBUG("-request code %x : %s", hdr->code, gps_rpc_to_s(hdr->code));
	
fail:
	return 0;
}

/******************************************************************************
 * RPC Transport Setup
 *****************************************************************************/
static int handle_rpc(int fd) {
	struct rpc *rpc = NULL;
	
	rpc = rpc_alloc();
	if (!rpc) {
		RPC_ERROR("out of memory");
		goto fail;
	}

	if (rpc_init(fd, gps_srv_rpc_handler, rpc)) {
		RPC_ERROR("failed to init RPC");
		goto fail;
	}
	
	g_rpc = rpc;

	if (rpc_start(rpc)) {
		RPC_ERROR("failed to start RPC");
		goto fail;
	}

	if (rpc_join(rpc)) {
		RPC_ERROR("failed to wait for RPC completion");
		goto fail;
	}
	
	return 0;
fail:
	g_rpc = NULL;

	if (rpc) {
		rpc_free(rpc);
	}
	return -1;
}

static int server_socket_open(void) {
	int fd = -1;
	int retry_count = 5;

	while (retry_count--) {
		unlink(GPS_RPC_SOCKET_NAME);
		fd = socket_local_server(GPS_RPC_SOCKET_NAME,
			ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);

		if (fd >= 0) {
			break;
		}
	}

	return fd;
}

static int gps_server(void) {
	int fd = -1;
	int ret = -1;
	int client_fd = -1;

	LOG_ENTRY;

	fd = server_socket_open();
	if (fd < 0) {
		RPC_ERROR("failed to open the socket");
		goto fail;
	}

//	while (1) {
		struct sockaddr_un client_addr;
		int client_addr_len;

		client_fd = accept(fd, (struct sockaddr*)&client_addr,
			&client_addr_len);

		if (client_fd <= 0) {
			RPC_ERROR("failed to accept the client");
			goto fail;
		}
	
		if (load_gps_library()) {
			RPC_ERROR("failed to load gps library and symbols");
			goto done;
		}

		if (handle_rpc(client_fd)) {
			RPC_ERROR("failed to serve the RPC client");
		}

done:
		if (client_fd >= 0) { 
			close(client_fd);
		}

		while (--num_lib_threads >= 0) {
			pthread_kill(lib_threads[num_lib_threads], SIGKILL);
		}

		free_gps_library();
//	}

	ret = 0;

fail:

	if (fd >= 0) {
		close(fd);
	}

	LOG_EXIT;
	return ret;
}

/******************************************************************************
 * Target library loader
 *****************************************************************************/
static int setup_gps_interface(void) {
	hw_module_t *module_tag = NULL;

	if (!lib_handle) {
		RPC_ERROR("library is uninitialized");
		goto fail;
	}

	module_tag = (hw_module_t*)dlsym(lib_handle,
		HAL_MODULE_INFO_SYM_AS_STR);
	if (!module_tag) {
		RPC_ERROR("failed to find HAL module info for GPS");
		goto fail;
	}

	if (module_tag->tag != HARDWARE_MODULE_TAG) {
		RPC_ERROR("module tag %x is not HARDWARE_MODULE_TAG",
			module_tag->tag);
		goto fail;
	}

	if (strcmp(module_tag->id, GPS_HARDWARE_MODULE_ID)) {
		RPC_ERROR("loaded module id '%s' is not '%s' as expected",
			module_tag->id,
			GPS_HARDWARE_MODULE_ID);
		goto fail;
	}

	if (!module_tag->methods) {
		RPC_ERROR("hw_module_t contains no methods");
		goto fail;
	}

	if (!module_tag->methods->open) {
		RPC_ERROR("hw_module_t contains no open method");
		goto fail;
	}

	RPC_INFO("GPS Module Name:'%s' Author:'%s'",
		module_tag->name,
		module_tag->author);

	struct gps_device_t *device = NULL;
	if (module_tag->methods->open(module_tag, module_tag->name,
		(hw_device_t**)&device))
	{
		RPC_ERROR("failed to open GPS Interface");
		goto fail;
	}

	if (!device || !device->get_gps_interface) {
		RPC_ERROR("failed to get GPS device");
		goto fail;
	}

	origGpsInterface = device->get_gps_interface(device);
	if (!origGpsInterface) {
		RPC_ERROR("failed to get original GPS interface");
		goto fail;
	}

	origGpsXtraInterface =
		origGpsInterface->get_extension(GPS_XTRA_INTERFACE);
	origNiInterface =
		origGpsInterface->get_extension(GPS_NI_INTERFACE);
	origAGpsInterface =
		origGpsInterface->get_extension(AGPS_INTERFACE);
	origRilInterface =
		origGpsInterface->get_extension(AGPS_RIL_INTERFACE);

	return 0;

fail:
	return -1;
}

static int load_gps_library(void) {
	lib_handle = dlopen(GPS_LIBRARY_NAME, 0);
	if (!lib_handle) {
		RPC_ERROR("failed to load gps library %s", GPS_LIBRARY_NAME);
		goto fail;
	}

	setup_gps_interface();

	RPC_INFO("loaded GPS library successfully");
	
	return 0;

fail:
	RPC_ERROR("failed to load GPS library");

	if (lib_handle) {
		dlclose(lib_handle);
		lib_handle = NULL;
	}

	return -1;
}

static void free_gps_library(void) {
	if (lib_handle) {
		dlclose(lib_handle);
		lib_handle = NULL;
	}
}

int main(int argc, char** argv) {
	int rc = 0;
	
	if ((rc = gps_server()) < 0) {
		RPC_ERROR("failed to start gps proxy server, error code %d",
			rc);
	}
	RPC_INFO("exiting");

	return rc;
}

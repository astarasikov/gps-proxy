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

#ifndef __GPS_RPC_H__
#define __GPS_RPC_H__

#define GPS_RPC_SOCKET_NAME "gps-rpc-socket"
#define GPS_SOCKET_RETRY_COUNT 5

enum gps_rpc_code {
	/* reserved for debugging */
	GPS_PROXY_NOP,

	GPS_PROXY_OPEN,

	/* XTRA Interface */
	GPS_PROXY_XTRA_INIT,
	GPS_PROXY_XTRA_INJECT_XTRA_DATA,

	/* AGPS Interface */
	GPS_PROXY_AGPS_INIT,
	GPS_PROXY_AGPS_DATA_CONN_OPEN,
	GPS_PROXY_AGPS_DATA_CONN_CLOSED,
	GPS_PROXY_AGPS_DATA_CONN_FAILED,
	GPS_PROXY_AGPS_AGPS_SET_SERVER,

	/* NI Interface */
	GPS_PROXY_NI_INIT,
	GPS_PROXY_NI_RESPOND,

	/* GPS Interface */
	GPS_PROXY_GPS_INIT,
	GPS_PROXY_GPS_START,
	GPS_PROXY_GPS_STOP,
	GPS_PROXY_GPS_CLEANUP,
	GPS_PROXY_GPS_INJECT_TIME,
	GPS_PROXY_GPS_INJECT_LOCATION,
	GPS_PROXY_GPS_DELETE_AIDING_DATA,
	GPS_PROXY_GPS_SET_POSITION_MODE,
	GPS_PROXY_GPS_GET_EXTENSION,

	/* GPS Callbacks */
	GPS_LOC_CB,
	GPS_STATUS_CB,
	GPS_SV_STATUS_CB,
	GPS_NMEA_CB,
	GPS_SET_CAPABILITIES_CB,
	GPS_ACQUIRE_LOCK_CB,
	GPS_RELEASE_LOCK_CB,
	GPS_CREATE_THREAD_CB,
	GPS_REQUEST_UTC_TIME_CB,

	/* XTRA Callbacks */
	XTRA_REQUEST_CB,
	XTRA_CREATE_THREAD_CB,

	/* AGPS Callbacks */
	AGPS_STATUS_CB,
	AGPS_CREATE_THREAD_CB,

	/* NI Callbacks */
	NI_NOTIFY_CB,
	NI_CREATE_THREAD_CB,
	
	GPS_RPC_MAX,
};


static inline char* gps_rpc_to_s(enum gps_rpc_code code) {
	#define TT_ENTRY(x) [x] = #x
	char *ttbl[GPS_RPC_MAX] = {
		TT_ENTRY(GPS_PROXY_NOP),
		TT_ENTRY(GPS_PROXY_OPEN),
		TT_ENTRY(GPS_PROXY_XTRA_INIT),
		TT_ENTRY(GPS_PROXY_XTRA_INJECT_XTRA_DATA),
		TT_ENTRY(GPS_PROXY_AGPS_INIT),
		TT_ENTRY(GPS_PROXY_AGPS_DATA_CONN_OPEN),
		TT_ENTRY(GPS_PROXY_AGPS_DATA_CONN_CLOSED),
		TT_ENTRY(GPS_PROXY_AGPS_DATA_CONN_FAILED),
		TT_ENTRY(GPS_PROXY_AGPS_AGPS_SET_SERVER),
		TT_ENTRY(GPS_PROXY_NI_INIT),
		TT_ENTRY(GPS_PROXY_NI_RESPOND),
		TT_ENTRY(GPS_PROXY_GPS_INIT),
		TT_ENTRY(GPS_PROXY_GPS_START),
		TT_ENTRY(GPS_PROXY_GPS_STOP),
		TT_ENTRY(GPS_PROXY_GPS_CLEANUP),
		TT_ENTRY(GPS_PROXY_GPS_INJECT_TIME),
		TT_ENTRY(GPS_PROXY_GPS_INJECT_LOCATION),
		TT_ENTRY(GPS_PROXY_GPS_DELETE_AIDING_DATA),
		TT_ENTRY(GPS_PROXY_GPS_SET_POSITION_MODE),
		TT_ENTRY(GPS_PROXY_GPS_GET_EXTENSION),
		TT_ENTRY(GPS_LOC_CB),
		TT_ENTRY(GPS_STATUS_CB),
		TT_ENTRY(GPS_SV_STATUS_CB),
		TT_ENTRY(GPS_NMEA_CB),
		TT_ENTRY(GPS_SET_CAPABILITIES_CB),
		TT_ENTRY(GPS_ACQUIRE_LOCK_CB),
		TT_ENTRY(GPS_RELEASE_LOCK_CB),
		TT_ENTRY(GPS_CREATE_THREAD_CB),
		TT_ENTRY(GPS_REQUEST_UTC_TIME_CB),
		TT_ENTRY(XTRA_REQUEST_CB),
		TT_ENTRY(XTRA_CREATE_THREAD_CB),
		TT_ENTRY(AGPS_STATUS_CB),
		TT_ENTRY(AGPS_CREATE_THREAD_CB),
		TT_ENTRY(NI_NOTIFY_CB),
		TT_ENTRY(NI_CREATE_THREAD_CB),
	};
	#undef TT_ENTRY

	if (code >= GPS_RPC_MAX) {
		return NULL;
	}
	return ttbl[code];
}

#endif //__GPS_RPC_H__

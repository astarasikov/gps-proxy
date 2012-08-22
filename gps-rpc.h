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
};

#endif //__GPS_RPC_H__

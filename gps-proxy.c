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

#include <stc_rpc.h>
#include <stc_log.h>

#include "gps-rpc.h"

#define GPS_LIBRARY_NAME "/system/vendor/lib/hw/gps.omap4.so"

typedef struct gps_library_context {
	void *lib_handle;
} gps_lib_ctx;

typedef struct gps_server_context {
	pthread_mutex_t mutex;
	gps_lib_ctx *lib_ctx;
} gps_srv_ctx;

static int gps_srv_rpc_handler(rpc_request_hdr_t *hdr, rpc_reply_t *reply) {
	if (!hdr) {
		RPC_ERROR("hdr is NULL");
		goto fail;
	}

	if (!reply) {
		RPC_ERROR("reply is NULL");
		goto fail;
	}

	RPC_INFO("request code %x", hdr->code);

	reply->code = hdr->code;

	return 0;

fail:
	return -1;
}

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

static int gps_server(gps_srv_ctx *gps_ctx) {
	int fd = -1;
	int ret = -1;
	int client_fd = -1;

	if (!gps_ctx) {
		RPC_ERROR("GPS context is NULL");
		goto fail;
	}

	fd = server_socket_open();
	if (fd < 0) {
		RPC_ERROR("failed to open the socket");
		goto fail;
	}

	while (1) {
		struct sockaddr_un client_addr;
		int client_addr_len;

		client_fd = accept(fd, (struct sockaddr*)&client_addr,
			&client_addr_len);

		if (client_fd <= 0) {
			RPC_ERROR("failed to accept the client");
			continue;
		}

		if (handle_rpc(client_fd)) {
			RPC_ERROR("failed to serve the RPC client");
		}

		close(client_fd);
	}

	ret = 0;

fail:
	if (client_fd >= 0) {
		close(client_fd);
	}

	if (fd >= 0) {
		close(fd);
	}

	return ret;
}

static int start_gps_server(gps_lib_ctx *lib_ctx) {
	int rc = -1;
	gps_srv_ctx *gps_ctx = NULL;

	if (!lib_ctx) {
		goto fail;
	}
	gps_ctx = (gps_srv_ctx*)malloc(sizeof(gps_srv_ctx));
	if (!gps_ctx) {
		RPC_ERROR("failed to allocate memory for the gps context");
		goto fail_malloc;
	}

	if (pthread_mutex_init(&gps_ctx->mutex, NULL)) {
		RPC_ERROR("failed to initialize gps mutex");
		goto fail;
	}

	if (gps_server(gps_ctx)) {
		RPC_ERROR("failed to handle GPS");
		goto fail;
	}
	
	rc = 0;

fail:
	pthread_mutex_destroy(&gps_ctx->mutex);

fail_malloc:
	if (gps_ctx) {
		free(gps_ctx);
	}

	return rc;
}

static int setup_gps_interface(gps_lib_ctx *ctx) {
	hw_module_t *module_tag = NULL;

	if (!ctx || !ctx->lib_handle) {
		RPC_ERROR("library is uninitialized");
		goto fail;
	}

	module_tag = (hw_module_t*)dlsym(ctx->lib_handle,
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

	RPC_INFO("GPS Module Name:'%s' Author:'%s'",
		module_tag->name,
		module_tag->author);

	return 0;

fail:
	return -1;
}

static gps_lib_ctx *load_gps_library(void) {
	gps_lib_ctx *ctx = (gps_lib_ctx*)malloc(sizeof(gps_lib_ctx));
	if (!ctx) {
		RPC_ERROR("failed to allocate memory for the library context");
		goto fail;
	}
	memset(ctx, 0, sizeof(gps_lib_ctx));

	ctx->lib_handle = dlopen(GPS_LIBRARY_NAME, 0);
	if (!ctx->lib_handle) {
		RPC_ERROR("failed to load gps library %s", GPS_LIBRARY_NAME);
		goto fail;
	}

	setup_gps_interface(ctx);
	//dlsym(ctx->lib_handle, "foo");

	RPC_INFO("loaded GPS library successfully");
	return ctx;

fail:
	RPC_ERROR("failed to load GPS library");

	if (ctx && ctx->lib_handle) {
		dlclose(ctx->lib_handle);
	}

	if (ctx) {
		free(ctx);
	}

	return NULL;
}

static void free_gps_library(gps_lib_ctx *lib_ctx) {
	if (!lib_ctx) {
		return;
	}
	
	if (lib_ctx->lib_handle) {
		dlclose(lib_ctx->lib_handle);
	}

	free(lib_ctx);
}

int main(int argc, char** argv) {
	int rc = 0;
	gps_lib_ctx *lib_ctx = load_gps_library();
	if (!lib_ctx) {
		RPC_ERROR("failed to load gps library and symbols");
		rc = EXIT_FAILURE;
		goto done;
	}

	if ((rc = start_gps_server(lib_ctx)) < 0) {
		RPC_ERROR("failed to start gps proxy server, error code %d",
			rc);
	}
	RPC_INFO("exiting");

done:
	if (lib_ctx) {
		free_gps_library(lib_ctx);
	}

	return rc;
}

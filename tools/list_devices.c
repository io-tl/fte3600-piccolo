// SPDX-License-Identifier: GPL-2.0-only

#include <stdio.h>

#include <fprint.h>

int main(void)
{
	FpContext *context;
	GPtrArray *devices;
	guint device_count;
	guint i;

	context = fp_context_new();
	if (!context) {
		fputs("failed to create libfprint context\n", stderr);
		return 1;
	}

	devices = fp_context_get_devices(context);
	device_count = devices->len;
	printf("devices=%u\n", device_count);

	for (i = 0; i < devices->len; i++) {
		FpDevice *device = g_ptr_array_index(devices, i);
		GError *error = NULL;

		printf("device[%u]: driver=%s name=%s id=%s\n", i,
		       fp_device_get_driver(device),
		       fp_device_get_name(device),
		       fp_device_get_device_id(device));

		if (!fp_device_open_sync(device, NULL, &error)) {
			fprintf(stderr, "device[%u]: open failed: %s\n", i,
				error->message);
			g_clear_error(&error);
			g_object_unref(context);
			return 3;
		}
		printf("device[%u]: open=ok\n", i);

		if (!fp_device_close_sync(device, NULL, &error)) {
			fprintf(stderr, "device[%u]: close failed: %s\n", i,
				error->message);
			g_clear_error(&error);
			g_object_unref(context);
			return 4;
		}
		printf("device[%u]: close=ok\n", i);
	}

	g_object_unref(context);
	return device_count ? 0 : 2;
}

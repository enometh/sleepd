/* Not particularly good interface to hal, for programs that used to use
 * apm.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <upower.h>
#include "apm.h"

static UpClient * up = NULL;

GPtrArray * devices = NULL;

int num_batteries = 0;

struct context {
	int current;
	int needed;
	guint state;
	int percentage;
};

static void get_devinfo(gpointer device, gpointer result)
{
	gboolean is_rechargeable;
	gdouble percentage;
	guint state;
	struct context * ctx = result;

	g_object_get(G_OBJECT(device), "percentage", &percentage,
		"is-rechargeable", &is_rechargeable,
		"state", &state,
		NULL);
	if (is_rechargeable) {
		if (ctx->current == ctx->needed) {
			ctx->percentage = (int)percentage;
			ctx->state = state;
		}
		ctx->current++;
	}
}

static void find_devices (void) {
	if (!up) return;
	if (devices)
		g_ptr_array_free(devices, TRUE);
	up_client_enumerate_devices_sync(up, NULL, NULL);

	devices = up_client_get_devices(up);
}

int upower_supported (void) {
	if (up) {
		g_object_unref(up);
		up = NULL;
	}
	up = up_client_new();
	if (!up) {
		return 0;
	}
	else {
		find_devices();
		return 1;
	}
}

/* Fill the passed apm_info struct. */
int upower_read (int battery, apm_info *info) {
	/* Allow a battery that was not present before to appear. */
	find_devices();

	info->battery_flags = 0;
	info->using_minutes = 0;

	info->ac_line_status = !up_client_get_on_battery(up);

	struct context ctx = {
		.current = 0,
		.needed = 0,
		.state = UP_DEVICE_STATE_UNKNOWN,
		.percentage = -1
	};
	g_ptr_array_foreach(devices, &get_devinfo, &ctx);
	if (ctx.percentage != -1) {
		printf("percentage is %d\n", ctx.percentage);
	}
	if (ctx.percentage < 0) {
		info->battery_percentage = 0;
		info->battery_time = 0;
		info->battery_status = BATTERY_STATUS_ABSENT;
		return 0;
	}

	/* remaining_time and charge_level.percentage are not a mandatory
	 * keys, so if not present, -1 will be returned */
	info->battery_time = 0;
	info->battery_percentage = ctx.percentage;
	if (ctx.state == UP_DEVICE_STATE_DISCHARGING) {
		info->battery_status = BATTERY_STATUS_CHARGING;
		/* charge_level.warning and charge_level.low are not
		 * required to be available; this is good enough */
		if (info->battery_percentage < 1) {
			info->battery_status = BATTERY_STATUS_CRITICAL;
		}
		else if (info->battery_percentage < 10) {
			info->battery_status = BATTERY_STATUS_LOW;
		}
	}
	else if (info->ac_line_status && ctx.state == UP_DEVICE_STATE_CHARGING) {
		info->battery_status = BATTERY_STATUS_CHARGING;
		info->battery_flags = info->battery_flags | BATTERY_FLAGS_CHARGING;
	}
	else if (info->ac_line_status) {
		/* Must be fully charged. */
		info->battery_status = BATTERY_STATUS_HIGH;
	}
	else {
		fprintf(stderr, "unknown battery state\n");
	}

	return 0;
}

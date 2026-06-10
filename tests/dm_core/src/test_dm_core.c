#include <zephyr/ztest.h>

#include <string.h>

#include "dm_api.h"
#include "dm_types.h"

static void test_dm_init_defaults(void)
{
	int rc = dm_init();
	zassert_equal(rc, 0, "dm_init failed: %d", rc);

	dm_snapshot_t snap;
	memset(&snap, 0, sizeof(snap));
	dm_get_snapshot(&snap);

	zassert_equal(snap.config.schema_version, 1, "schema version mismatch");
	zassert_equal(snap.update.state, DM_UPDATE_STATE_IDLE, "update state mismatch");
	zassert_equal(snap.update.last_error, 0, "update last_error mismatch");
	zassert_equal(snap.update.progress_percent, 0, "update progress mismatch");
	zassert_equal(strcmp(snap.comm.device_id, "pdm01"), 0, "device id mismatch");
	zassert_equal(strcmp(snap.comm.state, "operational"), 0, "comm state mismatch");
	zassert_equal(strcmp(snap.comm.last_reset_reason, "power_on"), 0, "reset reason mismatch");
}

static void test_dm_runtime_and_update_fields(void)
{
	dm_init();

	dm_runtime_set_uptime_ms(1234);
	dm_update_set_state(DM_UPDATE_STATE_DOWNLOADING, -5, 42);
	dm_update_set_uri("http://192.0.2.2:8080/fw.bin");

	dm_snapshot_t snap;
	dm_get_snapshot(&snap);

	zassert_equal(dm_get_uptime_ms(), 1234, "uptime mismatch");
	zassert_equal(snap.runtime.uptime_ms, 1234, "snapshot uptime mismatch");
	zassert_equal(snap.update.state, DM_UPDATE_STATE_DOWNLOADING, "update state mismatch");
	zassert_equal(snap.update.last_error, -5, "update error mismatch");
	zassert_equal(snap.update.progress_percent, 42, "update progress mismatch");
	zassert_equal(strcmp(snap.update.package_uri, "http://192.0.2.2:8080/fw.bin"), 0, "update uri mismatch");
}

static void test_dm_diagnostics_fields(void)
{
	dm_init();

	dm_diag_set_fault_bits(0x3U, true, true);
	dm_diag_clear(0x1U, true);
	dm_diag_add_event(0x2001, DM_DIAG_SEV_WARN, 7);

	dm_snapshot_t snap;
	dm_get_snapshot(&snap);

	zassert_equal(snap.diag.active_faults, 0x2U, "active faults mismatch");
	zassert_equal(snap.diag.latched_faults, 0x2U, "latched faults mismatch");
	zassert_equal(snap.diag.event_count, 2U, "event count mismatch");
	zassert_equal(snap.diag.events[0].code, 0x1000U, "boot event mismatch");
	zassert_equal(snap.diag.events[1].code, 0x2001U, "diag event mismatch");
	zassert_equal(snap.diag.events[1].sev, DM_DIAG_SEV_WARN, "diag severity mismatch");
	zassert_equal(snap.diag.events[1].aux, 7U, "diag aux mismatch");
}

static void test_dm_request_queues(void)
{
	dm_request_t ctrl_req = {
		.id = DM_REQ_CTRL_ACTION,
		.p.ctrl_action = {
			.action = DM_CTRL_ACT_LED_SET,
			.index = 1,
			.on = true,
			.value = 0,
		},
	};

	dm_request_t diag_req = {
		.id = DM_REQ_DIAG_CLEAR,
		.p.diag_clear = {
			.mask = 0xAAU,
			.clear_latched = true,
		},
	};

	dm_request_t update_req = {
		.id = DM_REQ_UPDATE_START,
		.p.update_start = {
			.uri = "http://192.0.2.2:8080/fw.bin",
		},
	};

	dm_request_t out;
	int rc;

	rc = dm_ctrl_req_submit(&ctrl_req);
	zassert_equal(rc, 0, "ctrl submit failed: %d", rc);
	rc = dm_ctrl_req_receive(&out, K_NO_WAIT);
	zassert_equal(rc, 0, "ctrl receive failed: %d", rc);
	zassert_equal(out.id, ctrl_req.id, "ctrl id mismatch");
	zassert_equal(out.p.ctrl_action.action, ctrl_req.p.ctrl_action.action, "ctrl action mismatch");

	rc = dm_diag_req_submit(&diag_req);
	zassert_equal(rc, 0, "diag submit failed: %d", rc);
	rc = dm_diag_req_receive(&out, K_NO_WAIT);
	zassert_equal(rc, 0, "diag receive failed: %d", rc);
	zassert_equal(out.id, diag_req.id, "diag id mismatch");
	zassert_equal(out.p.diag_clear.mask, diag_req.p.diag_clear.mask, "diag mask mismatch");

	rc = dm_update_req_submit(&update_req);
	zassert_equal(rc, 0, "update submit failed: %d", rc);
	rc = dm_update_req_receive(&out, K_NO_WAIT);
	zassert_equal(rc, 0, "update receive failed: %d", rc);
	zassert_equal(out.id, update_req.id, "update id mismatch");
	zassert_equal(strcmp(out.p.update_start.uri, update_req.p.update_start.uri), 0, "update uri mismatch");
}

ZTEST_SUITE(dm_core_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(dm_core_suite, test_dm_init_defaults)
{
	test_dm_init_defaults();
}

ZTEST(dm_core_suite, test_dm_runtime_and_update_fields)
{
	test_dm_runtime_and_update_fields();
}

ZTEST(dm_core_suite, test_dm_diagnostics_fields)
{
	test_dm_diagnostics_fields();
}

ZTEST(dm_core_suite, test_dm_request_queues)
{
	test_dm_request_queues();
}
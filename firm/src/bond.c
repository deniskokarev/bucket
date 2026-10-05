#include "bond.h"
#include "adv.h"

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(bond, LOG_LEVEL_INF);

#define BOND_WINDOW_SECONDS 60

static atomic_t window_open; /* 1 while new pairings are accepted */
static bond_event_cb_t event_cb;

static void notify(enum bond_event ev)
{
	if (event_cb) {
		event_cb(ev);
	}
}

/* Close the window if it is open; true if this call closed it. Callers race
 * (timeout vs. pairing result), so exactly one of them reports. */
static bool close_window(void)
{
	if (!atomic_cas(&window_open, 1, 0)) {
		return false;
	}
	bt_set_bonding_adv(false);
	return true;
}

/* Runs in the system workqueue (thread context) so BT APIs are safe. */
static void window_timeout(struct k_work *work)
{
	ARG_UNUSED(work);
	if (close_window()) {
		LOG_INF("bonding window closed (timeout)");
		notify(BOND_EV_WINDOW_TIMEOUT);
	}
}

static K_WORK_DELAYABLE_DEFINE(window_work, window_timeout);

static void count_bond(const struct bt_bond_info *info, void *user_data)
{
	ARG_UNUSED(info);
	(*(int *)user_data)++;
}

bool bond_exists(void)
{
	int n = 0;

	bt_foreach_bond(BT_ID_DEFAULT, count_bond, &n);
	return n > 0;
}

bool bond_window_is_open(void)
{
	return atomic_get(&window_open) != 0;
}

void bond_rebond(void)
{
	/* Clear first so the board always re-pairs from scratch: a phone that
	 * dropped its keys (OS-side unpair) can pair again, and the window only
	 * ever ends with exactly the newly paired phone bonded. Connected peers
	 * are disconnected by bt_unpair(). */
	int err = bt_unpair(BT_ID_DEFAULT, NULL);

	LOG_INF("cleared all bonds (%d)", err);
	atomic_set(&window_open, 1);
	bt_set_bonding_adv(true); /* advertise the marker so the app auto-pairs */
	k_work_reschedule(&window_work, K_SECONDS(BOND_WINDOW_SECONDS));
	LOG_INF("bonding mode: accepting a new pairing for %d s "
		"(read the secure Pump attribute from the client)",
		BOND_WINDOW_SECONDS);
	notify(BOND_EV_WINDOW_OPEN);
}

/* ---- pairing callbacks ---- */

#if defined(CONFIG_BT_SMP_APP_PAIRING_ACCEPT)
static enum bt_security_err pairing_accept(struct bt_conn *conn,
					   const struct bt_conn_pairing_feat *const feat)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(feat);

	if (atomic_get(&window_open)) {
		LOG_INF("pairing request accepted");
		return BT_SECURITY_ERR_SUCCESS;
	}

	LOG_WRN("pairing rejected: not in bonding mode (SW1 short-short-long or `bond`)");
	return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
}
#endif

static const struct bt_conn_auth_cb auth_cb = {
#if defined(CONFIG_BT_SMP_APP_PAIRING_ACCEPT)
	.pairing_accept = pairing_accept,
#endif
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("pairing complete: %s (bonded=%d)", addr, bonded);
	/* One successful bond per window. */
	if (close_window()) {
		k_work_cancel_delayable(&window_work);
		notify(BOND_EV_PAIRED);
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	ARG_UNUSED(conn);
	LOG_WRN("pairing failed (reason %d)", reason);
	/* Only a failure inside the window is the user's pairing attempt; outside
	 * it, it's a rejected stranger and must not change the UI. */
	if (close_window()) {
		k_work_cancel_delayable(&window_work);
		notify(BOND_EV_FAILED);
	}
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

int bond_init(bond_event_cb_t cb)
{
	int err;

	event_cb = cb;
	err = bt_conn_auth_cb_register(&auth_cb);
	if (err) {
		return err;
	}
	return bt_conn_auth_info_cb_register(&auth_info_cb);
}

/* ---- shell ---- */

static int cmd_bond(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "clear") == 0) {
		int err = bt_unpair(BT_ID_DEFAULT, NULL); /* remove all bonds (recovery) */
		shell_print(sh, "cleared all bonds (%d)", err);
		return 0;
	}
	bond_rebond();
	shell_print(sh, "bonding mode ON for %d s — read the secure Pump attribute to pair",
		    BOND_WINDOW_SECONDS);
	return 0;
}

SHELL_CMD_REGISTER(bond, NULL,
		   "Clear bonds and open the bonding window for 60s (same as SW1 "
		   "short-short-long); `bond clear` only drops all bonds",
		   cmd_bond);

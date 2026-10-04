#include "bond.h"
#include "adv.h"

#include <string.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>

LOG_MODULE_REGISTER(bond, LOG_LEVEL_INF);

#define BOND_WINDOW_SECONDS 60

static atomic_t window_open; /* 1 while new pairings are accepted */

/* Runs in the system workqueue (thread context) so BT APIs are safe. */
static void window_close(struct k_work *work)
{
	ARG_UNUSED(work);
	atomic_set(&window_open, 0);
	bt_set_bonding_adv(false);
	LOG_INF("bonding window closed");
}

static K_WORK_DELAYABLE_DEFINE(window_work, window_close);

void bond_start_window(void)
{
	/* Open the window WITHOUT clearing existing bonds, so a return visit from
	 * an already-paired phone reconnects seamlessly (no new OS pairing prompt).
	 * Use `bond clear` to drop bonds when switching to a different phone. */
	atomic_set(&window_open, 1);
	bt_set_bonding_adv(true); /* advertise the marker so the app auto-pairs */
	k_work_reschedule(&window_work, K_SECONDS(BOND_WINDOW_SECONDS));
	LOG_INF("bonding mode: accepting new pairings for %d s "
		"(read the secure Pump attribute from the client)",
		BOND_WINDOW_SECONDS);
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

	LOG_WRN("pairing rejected: not in bonding mode (run `bond` or press SW1)");
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
	ARG_UNUSED(conn);
	LOG_INF("pairing complete (bonded=%d)", bonded);
	/* One successful bond per window. */
	atomic_set(&window_open, 0);
	k_work_cancel_delayable(&window_work);
	bt_set_bonding_adv(false);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	ARG_UNUSED(conn);
	LOG_WRN("pairing failed (reason %d)", reason);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

/* ---- bond button (SW1 / PC4, via EXTI) ---- */

static const struct gpio_dt_spec bond_btn =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), bond_gpios);
static struct gpio_callback btn_cb_data;

static void btn_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	bond_start_window();
}

static K_WORK_DEFINE(btn_work, btn_work_handler);

static void btn_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	/* Defer out of the EXTI ISR. */
	k_work_submit(&btn_work);
}

static int setup_button(void)
{
	if (!gpio_is_ready_dt(&bond_btn)) {
		LOG_ERR("bond button GPIO not ready");
		return -ENODEV;
	}

	int ret = gpio_pin_configure_dt(&bond_btn, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&bond_btn, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		return ret;
	}
	gpio_init_callback(&btn_cb_data, btn_isr, BIT(bond_btn.pin));
	gpio_add_callback(bond_btn.port, &btn_cb_data);
	LOG_INF("bond button (EXTI) ready on %s pin %d", bond_btn.port->name, bond_btn.pin);
	return 0;
}

int bond_init(void)
{
	bt_conn_auth_cb_register(&auth_cb);
	bt_conn_auth_info_cb_register(&auth_info_cb);
	return setup_button();
}

/* ---- shell ---- */

static int cmd_bond(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "clear") == 0) {
		int err = bt_unpair(BT_ID_DEFAULT, NULL); /* remove all bonds (recovery) */
		shell_print(sh, "cleared all bonds (%d)", err);
		return 0;
	}
	bond_start_window();
	shell_print(sh, "bonding mode ON for %d s — read the secure Pump attribute to pair",
		    BOND_WINDOW_SECONDS);
	return 0;
}

SHELL_CMD_REGISTER(bond, NULL,
		   "Open the bonding window for 60s (same as SW1); `bond clear` drops all bonds",
		   cmd_bond);

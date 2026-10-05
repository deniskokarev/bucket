/*
 * Morse-code button decoder (input-subsystem filter).
 *
 * Press/release events of one key are timed into dots and dashes. A sequence
 * that matches a code fires on the completing release unless a longer code
 * starts with it; then it fires once the button has been idle for
 * reset-delay-ms. Idle time always ends the sequence, so a stray press can't
 * chain into a code typed later. A sequence that can no longer match stays
 * "dead" until the idle reset, for the same reason.
 */
#define DT_DRV_COMPAT dkv_morse_button

#include <drivers/morse_button.h>

#include <string.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

LOG_MODULE_REGISTER(morse_button, CONFIG_MORSE_BUTTON_LOG_LEVEL);

#define MAX_SYMBOLS CONFIG_MORSE_BUTTON_MAX_SYMBOLS
#define NO_MATCH    SIZE_MAX

struct morse_entry {
	const char *code;
	const char *label;
};

struct morse_handler {
	morse_button_handler_t fn;
	void *user_data;
};

struct morse_button_config {
	uint16_t input_code;
	uint32_t long_press_ms;
	uint32_t reset_delay_ms;
	const struct morse_entry *entries;
	struct morse_handler *handlers;
	size_t num_entries;
};

struct morse_button_data {
	const struct device *dev;
	struct k_work_delayable reset_work;
	struct k_work dispatch_work;
	struct k_spinlock lock;
	int64_t pressed_at;
	size_t pending; /* entry matched on release, dispatched from the workqueue */
	bool pressed;
	bool dead;
	size_t len;
	char seq[MAX_SYMBOLS + 1];
};

static size_t find_exact(const struct morse_button_config *cfg, const char *seq)
{
	for (size_t i = 0; i < cfg->num_entries; i++) {
		if (strcmp(cfg->entries[i].code, seq) == 0) {
			return i;
		}
	}
	return NO_MATCH;
}

/* Does any code start with seq and continue past it (strictly longer)? */
static bool is_strict_prefix(const struct morse_button_config *cfg, const char *seq, size_t len)
{
	for (size_t i = 0; i < cfg->num_entries; i++) {
		const char *code = cfg->entries[i].code;

		if (strlen(code) > len && strncmp(code, seq, len) == 0) {
			return true;
		}
	}
	return false;
}

static void reset_locked(struct morse_button_data *data)
{
	data->len = 0;
	data->seq[0] = '\0';
	data->dead = false;
}

static void dispatch(const struct device *dev, size_t entry)
{
	const struct morse_button_config *cfg = dev->config;
	const struct morse_entry *e = &cfg->entries[entry];
	struct morse_handler h = cfg->handlers[entry];

	LOG_INF("%s: \"%s\" -> %s", dev->name, e->code, e->label ? e->label : "(no label)");
	if (h.fn) {
		h.fn(dev, entry, h.user_data);
	} else {
		LOG_WRN("%s: no handler for \"%s\"", dev->name, e->code);
	}
}

static void dispatch_work_handler(struct k_work *work)
{
	struct morse_button_data *data = CONTAINER_OF(work, struct morse_button_data, dispatch_work);
	size_t entry = NO_MATCH;

	K_SPINLOCK(&data->lock) {
		entry = data->pending;
		data->pending = NO_MATCH;
	}
	if (entry != NO_MATCH) {
		dispatch(data->dev, entry);
	}
}

static void reset_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct morse_button_data *data = CONTAINER_OF(dwork, struct morse_button_data, reset_work);
	const struct device *dev = data->dev;
	const struct morse_button_config *cfg = dev->config;
	size_t entry = NO_MATCH;
	char seq[MAX_SYMBOLS + 1] = "";
	bool dead = false;

	K_SPINLOCK(&data->lock) {
		if (data->pressed) {
			K_SPINLOCK_BREAK; /* a press cancels the reset; raced with it */
		}
		memcpy(seq, data->seq, sizeof(seq));
		dead = data->dead;
		if (!dead && data->len > 0) {
			entry = find_exact(cfg, data->seq);
		}
		reset_locked(data);
	}

	if (entry != NO_MATCH) {
		dispatch(dev, entry);
	} else if (seq[0] != '\0') {
		LOG_INF("%s: unmatched \"%s\"", dev->name, seq);
	}
}

static void on_release(const struct device *dev, int64_t held_ms)
{
	const struct morse_button_config *cfg = dev->config;
	struct morse_button_data *data = dev->data;
	char sym = held_ms >= cfg->long_press_ms ? '-' : '.';
	size_t entry = NO_MATCH;

	K_SPINLOCK(&data->lock) {
		if (data->len == MAX_SYMBOLS) {
			data->dead = true;
		} else {
			data->seq[data->len++] = sym;
			data->seq[data->len] = '\0';
		}
		LOG_DBG("%s: %c (%d ms) -> \"%s\"%s", dev->name, sym, (int)held_ms, data->seq,
			data->dead ? " (dead)" : "");

		if (!data->dead) {
			bool longer = is_strict_prefix(cfg, data->seq, data->len);
			size_t exact = find_exact(cfg, data->seq);

			if (exact != NO_MATCH && !longer) {
				data->pending = exact;
				entry = exact;
				reset_locked(data);
			} else if (exact == NO_MATCH && !longer) {
				data->dead = true; /* can't match anymore; wait for idle */
			}
		}
	}

	if (entry != NO_MATCH) {
		k_work_cancel_delayable(&data->reset_work);
		k_work_submit(&data->dispatch_work);
	} else {
		k_work_reschedule(&data->reset_work, K_MSEC(cfg->reset_delay_ms));
	}
}

static void morse_input_cb(struct input_event *evt, void *user_data)
{
	const struct device *dev = user_data;
	const struct morse_button_config *cfg = dev->config;
	struct morse_button_data *data = dev->data;
	int64_t now = k_uptime_get();
	int64_t held_ms = -1;

	if (evt->type != INPUT_EV_KEY || evt->code != cfg->input_code) {
		return;
	}

	K_SPINLOCK(&data->lock) {
		if (evt->value && !data->pressed) {
			data->pressed = true;
			data->pressed_at = now;
		} else if (!evt->value && data->pressed) {
			data->pressed = false;
			held_ms = now - data->pressed_at;
		}
	}

	if (evt->value) {
		k_work_cancel_delayable(&data->reset_work);
	} else if (held_ms >= 0) {
		on_release(dev, held_ms);
	}
}

int morse_button_set_handler(const struct device *dev, size_t entry, morse_button_handler_t handler,
			     void *user_data)
{
	const struct morse_button_config *cfg = dev->config;
	struct morse_button_data *data = dev->data;

	if (entry >= cfg->num_entries) {
		return -EINVAL;
	}
	K_SPINLOCK(&data->lock) {
		cfg->handlers[entry].fn = handler;
		cfg->handlers[entry].user_data = user_data;
	}
	return 0;
}

const char *morse_button_code(const struct device *dev, size_t entry)
{
	const struct morse_button_config *cfg = dev->config;

	return entry < cfg->num_entries ? cfg->entries[entry].code : NULL;
}

static int morse_button_init(const struct device *dev)
{
	const struct morse_button_config *cfg = dev->config;
	struct morse_button_data *data = dev->data;

	for (size_t i = 0; i < cfg->num_entries; i++) {
		const char *code = cfg->entries[i].code;
		size_t len = strlen(code);

		if (len == 0 || len > MAX_SYMBOLS || strspn(code, ".-") != len) {
			LOG_ERR("%s: bad code \"%s\" (use 1..%d of '.' and '-')", dev->name, code,
				MAX_SYMBOLS);
			return -EINVAL;
		}
		for (size_t j = 0; j < i; j++) {
			if (strcmp(code, cfg->entries[j].code) == 0) {
				LOG_ERR("%s: duplicate code \"%s\"", dev->name, code);
				return -EINVAL;
			}
		}
	}

	data->dev = dev;
	data->pending = NO_MATCH;
	reset_locked(data);
	k_work_init_delayable(&data->reset_work, reset_work_handler);
	k_work_init(&data->dispatch_work, dispatch_work_handler);
	return 0;
}

#define MORSE_ENTRY(node_id)                                                                       \
	{                                                                                          \
		.code = DT_PROP(node_id, code),                                                    \
		.label = DT_PROP_OR(node_id, label, NULL),                                         \
	}

#define MORSE_INPUT_DEV(inst)                                                                      \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, input),                                            \
		    (DEVICE_DT_GET(DT_INST_PHANDLE(inst, input))), (NULL))

#define MORSE_BUTTON_DEFINE(inst)                                                                  \
	BUILD_ASSERT(DT_INST_CHILD_NUM(inst) > 0, "morse-button needs at least one code node");   \
	static const struct morse_entry morse_entries_##inst[] = {                                 \
		DT_INST_FOREACH_CHILD_SEP(inst, MORSE_ENTRY, (,))};                                \
	static struct morse_handler morse_handlers_##inst[ARRAY_SIZE(morse_entries_##inst)];       \
	static const struct morse_button_config morse_config_##inst = {                            \
		.input_code = DT_INST_PROP(inst, input_code),                                      \
		.long_press_ms = DT_INST_PROP(inst, long_press_ms),                                \
		.reset_delay_ms = DT_INST_PROP(inst, reset_delay_ms),                              \
		.entries = morse_entries_##inst,                                                   \
		.handlers = morse_handlers_##inst,                                                 \
		.num_entries = ARRAY_SIZE(morse_entries_##inst),                                   \
	};                                                                                         \
	static struct morse_button_data morse_data_##inst;                                         \
	DEVICE_DT_INST_DEFINE(inst, morse_button_init, NULL, &morse_data_##inst,                   \
			      &morse_config_##inst, POST_KERNEL,                                   \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);                           \
	INPUT_CALLBACK_DEFINE_NAMED(MORSE_INPUT_DEV(inst), morse_input_cb,                         \
				    (void *)DEVICE_DT_INST_GET(inst), morse_cb_##inst);

DT_INST_FOREACH_STATUS_OKAY(MORSE_BUTTON_DEFINE)

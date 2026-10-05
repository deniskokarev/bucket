/*
 * Signal LED: off / solid / blink / sin^2 glow, optionally time-limited.
 *
 * Brightness 0..255 goes to a hardware PWM channel (`pwms`) or, for a plain
 * GPIO LED (`gpios`), to a two-phase k_timer software PWM. The soft PWM only
 * runs at intermediate levels (GLOW); OFF, SOLID and BLINK drive the pin
 * directly, so they cost no periodic interrupts.
 *
 * The glow math is from ~/work/batmet/zfirm/src/led.c.
 */
#define DT_DRV_COMPAT dkv_signal_led

#include <drivers/dkv_wq.h>
#include <drivers/signal_led.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(signal_led, CONFIG_SIGNAL_LED_LOG_LEVEL);

#define LEVEL_MAX      255U
#define GLOW_UPDATE_MS 30

#define M_PI   3.14159265358979323846f
#define M_PI_2 1.57079632679489661923f

struct signal_led_config {
	bool use_pwm;
	struct pwm_dt_spec pwm;
	struct gpio_dt_spec gpio;
	uint32_t soft_period_us;
};

struct signal_led_data {
	const struct device *dev;
	struct k_mutex lock;
	struct k_work_delayable pattern_work;
	struct k_work_delayable stop_work;
	enum signal_led_mode mode;
	uint32_t period_ms;
	int64_t t0;
	bool blink_on;
	struct k_timer soft_timer;
	volatile uint8_t level; /* read by the soft-PWM timer ISR */
	bool soft_on_phase;
	bool soft_running;
};

#define SIN_PI2_SZ 16

/* libm-free sine: quarter-wave table (saves ~3 KB of libm). */
static float crude_sinf(float x)
{
	static const float sin_pi2[SIN_PI2_SZ + 1] = {
		0.0f,
		0.0980171403295606f,
		0.19509032201612825f,
		0.29028467725446233f,
		0.3826834323650898f,
		0.47139673682599764f,
		0.5555702330196022f,
		0.6343932841636455f,
		0.7071067811865475f,
		0.7730104533627369f,
		0.8314696123025451f,
		0.8819212643483549f,
		0.9238795325112867f,
		0.9569403357322089f,
		0.9807852804032304f,
		0.9951847266721968f,
		1.0f,
	};
	static const float f = M_PI_2 / SIN_PI2_SZ;
	int p = x / (2 * M_PI);

	x -= p * (2 * M_PI);
	p = x / M_PI_2;
	x -= p * M_PI_2;
	int i = x / f;

	switch (p) {
	case 0:
		return sin_pi2[i];
	case 1:
		return sin_pi2[SIN_PI2_SZ - i];
	case 2:
		return -sin_pi2[i];
	case 3:
		return -sin_pi2[SIN_PI2_SZ - i];
	default:
		return 0.0f;
	}
}

static void soft_pwm_expiry(struct k_timer *timer)
{
	struct signal_led_data *data = CONTAINER_OF(timer, struct signal_led_data, soft_timer);
	const struct signal_led_config *cfg = data->dev->config;
	uint8_t level = data->level;

	if (level == 0 || level == LEVEL_MAX) {
		return; /* set_level() switched to a static level */
	}

	uint32_t on_us = cfg->soft_period_us * level / LEVEL_MAX;

	data->soft_on_phase = !data->soft_on_phase;
	gpio_pin_set_dt(&cfg->gpio, data->soft_on_phase);
	k_timer_start(timer, K_USEC(data->soft_on_phase ? on_us : cfg->soft_period_us - on_us),
		      K_NO_WAIT);
}

/* Caller holds data->lock. */
static int set_level(const struct device *dev, uint8_t level)
{
	const struct signal_led_config *cfg = dev->config;
	struct signal_led_data *data = dev->data;

	if (cfg->use_pwm) {
		uint32_t pulse = (uint64_t)cfg->pwm.period * level / LEVEL_MAX;

		return pwm_set_dt(&cfg->pwm, cfg->pwm.period, pulse);
	}

	data->level = level;
	if (level == 0 || level == LEVEL_MAX) {
		k_timer_stop(&data->soft_timer);
		data->soft_running = false;
		return gpio_pin_set_dt(&cfg->gpio, level ? 1 : 0);
	}
	if (!data->soft_running) {
		data->soft_running = true;
		data->soft_on_phase = false;
		k_timer_start(&data->soft_timer, K_NO_WAIT, K_NO_WAIT);
	}
	return 0;
}

static void pattern_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct signal_led_data *data = CONTAINER_OF(dwork, struct signal_led_data, pattern_work);
	const struct device *dev = data->dev;
	int rc = 0;

	k_mutex_lock(&data->lock, K_FOREVER);
	switch (data->mode) {
	case SIGNAL_LED_BLINK:
		data->blink_on = !data->blink_on;
		rc = set_level(dev, data->blink_on ? LEVEL_MAX : 0);
		k_work_reschedule_for_queue(&dkv_wq, dwork, K_MSEC(data->period_ms / 2));
		break;
	case SIGNAL_LED_GLOW: {
		/* sin^2 has period pi: dark -> full -> dark once per period_ms */
		float t = (float)(k_uptime_get() - data->t0);
		float s = crude_sinf(M_PI * t / data->period_ms);

		rc = set_level(dev, (uint8_t)(s * s * LEVEL_MAX));
		k_work_reschedule_for_queue(&dkv_wq, dwork, K_MSEC(GLOW_UPDATE_MS));
		break;
	}
	default:
		break; /* pattern changed while we waited for the lock */
	}
	k_mutex_unlock(&data->lock);

	if (rc) {
		LOG_ERR("%s: set level failed (%d)", dev->name, rc);
	}
}

static void stop_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct signal_led_data *data = CONTAINER_OF(dwork, struct signal_led_data, stop_work);

	(void)signal_led_set(data->dev, SIGNAL_LED_OFF, 0, K_FOREVER);
}

int signal_led_set(const struct device *dev, enum signal_led_mode mode, uint32_t period_ms,
		   k_timeout_t duration)
{
	struct signal_led_data *data = dev->data;
	int rc;

	if ((mode == SIGNAL_LED_BLINK && period_ms < 2) || (mode == SIGNAL_LED_GLOW && period_ms == 0) ||
	    mode > SIGNAL_LED_GLOW) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->mode = mode;
	data->period_ms = period_ms;
	k_work_cancel_delayable(&data->pattern_work);

	switch (mode) {
	case SIGNAL_LED_SOLID:
		rc = set_level(dev, LEVEL_MAX);
		break;
	case SIGNAL_LED_BLINK:
		data->blink_on = true;
		rc = set_level(dev, LEVEL_MAX);
		k_work_reschedule_for_queue(&dkv_wq, &data->pattern_work, K_MSEC(period_ms / 2));
		break;
	case SIGNAL_LED_GLOW:
		data->t0 = k_uptime_get();
		rc = set_level(dev, 0);
		k_work_reschedule_for_queue(&dkv_wq, &data->pattern_work, K_MSEC(GLOW_UPDATE_MS));
		break;
	case SIGNAL_LED_OFF:
	default:
		rc = set_level(dev, 0);
		break;
	}

	if (K_TIMEOUT_EQ(duration, K_FOREVER) || mode == SIGNAL_LED_OFF) {
		k_work_cancel_delayable(&data->stop_work);
	} else {
		k_work_reschedule_for_queue(&dkv_wq, &data->stop_work, duration);
	}
	k_mutex_unlock(&data->lock);

	LOG_DBG("%s: mode %d period %u ms", dev->name, mode, period_ms);
	return rc;
}

static int signal_led_init(const struct device *dev)
{
	const struct signal_led_config *cfg = dev->config;
	struct signal_led_data *data = dev->data;
	int rc;

	if (cfg->use_pwm) {
		rc = pwm_is_ready_dt(&cfg->pwm) ? 0 : -ENODEV;
	} else {
		rc = gpio_is_ready_dt(&cfg->gpio)
			     ? gpio_pin_configure_dt(&cfg->gpio, GPIO_OUTPUT_INACTIVE)
			     : -ENODEV;
		k_timer_init(&data->soft_timer, soft_pwm_expiry, NULL);
	}
	if (rc) {
		LOG_ERR("%s: output not ready (%d)", dev->name, rc);
		return rc;
	}

	data->dev = dev;
	data->mode = SIGNAL_LED_OFF;
	k_mutex_init(&data->lock);
	k_work_init_delayable(&data->pattern_work, pattern_work_handler);
	k_work_init_delayable(&data->stop_work, stop_work_handler);
	return 0;
}

#define SIGNAL_LED_DEFINE(inst)                                                                    \
	BUILD_ASSERT(DT_INST_NODE_HAS_PROP(inst, pwms) != DT_INST_NODE_HAS_PROP(inst, gpios),      \
		     "signal-led needs exactly one of pwms or gpios");                             \
	static const struct signal_led_config signal_led_config_##inst = {                         \
		.use_pwm = DT_INST_NODE_HAS_PROP(inst, pwms),                                      \
		.pwm = PWM_DT_SPEC_INST_GET_OR(inst, {0}),                                         \
		.gpio = GPIO_DT_SPEC_INST_GET_OR(inst, gpios, {0}),                                \
		.soft_period_us = USEC_PER_SEC / DT_INST_PROP(inst, soft_pwm_hz),                  \
	};                                                                                         \
	static struct signal_led_data signal_led_data_##inst;                                      \
	DEVICE_DT_INST_DEFINE(inst, signal_led_init, NULL, &signal_led_data_##inst,                \
			      &signal_led_config_##inst, POST_KERNEL,                              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(SIGNAL_LED_DEFINE)

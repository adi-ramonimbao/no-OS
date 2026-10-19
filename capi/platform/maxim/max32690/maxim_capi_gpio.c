/***************************************************************************//**
 *   @file   maxim_capi_gpio.c
 *   @brief  Implementation of GPIO functions with CAPI.
 *   @author Edelweise Escala (edelweise.escala@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <errno.h>
#include "gpio.h"
#include "maxim_capi_gpio.h"
#include "maxim_capi_gpio_priv.h"
#include "mxc_errors.h"
#include "max32690.h"
#include "capi_alloc.h"

/** Static variables **********************************************************/

static struct capi_gpio_port_handle *gpio[MXC_CFG_GPIO_INSTANCES] = {NULL};

/** Helper functions **********************************************************/

/**
 * @brief Report the number of usable pins on a GPIO port
 *
 * Every port exposes MXC_CFG_GPIO_PINS_PORT pins except port 4, which only
 * brings out P4.0 and P4.1. See MAX_CAPI_GPIO4_NUM_PINS.
 *
 * @param id The GPIO port identifier
 * @return The number of pins the port actually implements
 */
static uint8_t _max_capi_gpio_port_pins(uint32_t id)
{
	if (id == 4)
		return MAX_CAPI_GPIO4_NUM_PINS;

	return MXC_CFG_GPIO_PINS_PORT;
}

/**
 * @brief Set/unset the EN0 register
 *
 * Port 4 is skipped: it has no en0 register of its own (its base address is a
 * placeholder and its state lives in MXC_MCR), and MXC_GPIO_Config() already
 * selects the GPIO function for it. Writing en0 here would hit the placeholder
 * address.
 *
 * @param regs The GPIO register
 * @param mask The bitmask to set/unset
 * @param is_enabled Whether to set or unset
 */
static void _max_capi_gpio_set_enable(mxc_gpio_regs_t *regs, uint32_t mask,
				      uint8_t is_enabled)
{
	if (regs == MXC_GPIO4)
		return;

	if (is_enabled)
		regs->en0 |= mask;
	else
		regs->en0 &= ~mask;
}

static uint32_t _max_capi_gpio_logical_to_physical(
	const struct max_capi_gpio_port_priv *priv, uint32_t logical_mask)
{
	return ((logical_mask & priv->pin_mask) << priv->extra.pin_offset) &
	       priv->phys_pin_mask;
}

static uint32_t _max_capi_gpio_physical_to_logical(
	const struct max_capi_gpio_port_priv *priv, uint32_t physical_mask)
{
	return (physical_mask & priv->phys_pin_mask) >> priv->extra.pin_offset;
}

/** GPIO functions implemenation **********************************************/
/**
 * @brief Initialize a GPIO port
 * @param handle - the GPIO port handle
 * @param config - the GPIO port configuration
 * @return 0 if successful, negative error code otherwise
 */
static int max_capi_gpio_port_init(struct capi_gpio_port_handle **handle,
				   const struct capi_gpio_port_config *config)
{
	int ret;
	struct capi_gpio_port_handle *port_handle;
	struct max_capi_gpio_port_priv *priv;

	if (!handle || !config)
		return -EINVAL;

	if (config->identifier >= MXC_CFG_GPIO_INSTANCES)
		return -EINVAL;

	if (config->num_pins == 0 ||
	    config->num_pins > _max_capi_gpio_port_pins(config->identifier))
		return -EINVAL;

	if (*handle == NULL) {
		port_handle = capi_calloc(1, sizeof(*port_handle));
		if (!port_handle)
			return -ENOMEM;

		priv = capi_calloc(1, sizeof(*priv));
		if (!priv) {
			capi_free(port_handle);
			return -ENOMEM;
		}

		port_handle->priv = priv;
		port_handle->init_allocated = true;
	} else {
		port_handle = *handle;

		if (!port_handle->priv)
			return -EINVAL;

		priv = port_handle->priv;

		port_handle->init_allocated = false;
	}

	port_handle->ops = config->ops;

	priv->id = config->identifier;
	priv->port = MXC_GPIO_GET_GPIO(config->identifier);
	priv->num_pins = config->num_pins;
	priv->pin_mask = (uint32_t)((1ULL << config->num_pins) - 1);
	priv->flags = config->flags;
	priv->direction_mask = priv->pin_mask; /* All inputs by default */

	/* Copy user config or set defaults */
	if (config->extra) {
		priv->extra = *(struct max_capi_gpio_extra_config *)config->extra;
	} else {
		priv->extra.vssel = MAX_CAPI_GPIO_VSSEL_VDDIO;
		priv->extra.func = MAX_CAPI_GPIO_FUNC_IN;
		priv->extra.pad = MAX_CAPI_GPIO_PAD_NONE;
		priv->extra.pin_offset = 0U;
	}

	if ((uint32_t)priv->extra.pin_offset + config->num_pins >
	    _max_capi_gpio_port_pins(config->identifier)) {
		ret = -EINVAL;
		goto free_handle;
	}

	priv->phys_pin_mask = (uint32_t)(priv->pin_mask << priv->extra.pin_offset);

	/* MXC_GPIO_Init() takes a mask of ports, not a port index. */
	ret = MXC_GPIO_Init(1U << config->identifier);
	if (ret != E_NO_ERROR)
		goto free_handle;

	gpio[config->identifier] = port_handle;
	*handle = port_handle;

	return 0;

free_handle:
	if (port_handle->init_allocated) {
		capi_free(priv);
		capi_free(port_handle);
	}

	gpio[config->identifier] = NULL;

	return ret;
}

/**
 * @brief Deinitialize a GPIO port
 * @param handle - the GPIO port handle
 * @return 0 if successful, negative error code otherwise
 */
static int max_capi_gpio_port_deinit(struct capi_gpio_port_handle **handle)
{
	const struct max_capi_gpio_port_priv *gpio_priv;
	uint8_t id;

	if (!handle || !*handle || !(*handle)->priv)
		return -EINVAL;

	gpio_priv = (*handle)->priv;
	id = gpio_priv->id;

	if ((*handle)->init_allocated) {
		capi_free((*handle)->priv);
		capi_free(*handle);
	}

	gpio[id] = NULL;
	*handle = NULL;

	return 0;
}

/**
 * @brief Set direction for all pins in the port
 * @param handle - the GPIO handle
 * @param direction_bitmask - Direction bitmask (1=input, 0=output)
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_port_set_direction(struct capi_gpio_port_handle
		*handle,
		uint64_t direction_bitmask)
{
	struct max_capi_gpio_port_priv *priv;
	mxc_gpio_cfg_t gpio_config;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;

	/* Common configuration */
	gpio_config = (mxc_gpio_cfg_t) {
		.port = priv->port,
		.pad = (mxc_gpio_pad_t)priv->extra.pad,
		.vssel = (mxc_gpio_vssel_t)priv->extra.vssel,
	};

	/* Set the output pins */
	gpio_config.func = MXC_GPIO_FUNC_OUT;
	gpio_config.mask = _max_capi_gpio_logical_to_physical(priv,
					      (uint32_t)~direction_bitmask);
	MXC_GPIO_Config(&gpio_config);
	_max_capi_gpio_set_enable(priv->port, gpio_config.mask,
				  true);
	MXC_GPIO_OutClr(priv->port, gpio_config.mask);

	/* Set the input pins */
	gpio_config.func = MXC_GPIO_FUNC_IN;
	gpio_config.mask = _max_capi_gpio_logical_to_physical(priv,
					      (uint32_t)direction_bitmask);
	MXC_GPIO_Config(&gpio_config);

	priv->direction_mask = (uint32_t)direction_bitmask & priv->pin_mask;
	return 0;
}

/**
 * @brief Get direction for all pins in the port
 * @param handle - the GPIO handle
 * @param direction_bitmask - pointer to where the bitmask will be stored
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_port_get_direction(struct capi_gpio_port_handle
					    *handle,
					    uint64_t *direction_bitmask)
{
	const struct max_capi_gpio_port_priv *priv;

	if (!handle || !handle->priv || !direction_bitmask)
		return -EINVAL;

	priv = handle->priv;

	*direction_bitmask = priv->direction_mask;

	return 0;
}

/**
 * @brief Set raw value for all pins in the port (ignoring ACTIVE_LOW flag)
 * @param handle - the GPIO handle
 * @param value_bitmask - the value bitmask
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_port_set_raw_value(struct capi_gpio_port_handle
					    *handle,
					    uint64_t value_bitmask)
{
	struct max_capi_gpio_port_priv *priv;
	uint32_t set_pins, reset_pins;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;

	set_pins = _max_capi_gpio_logical_to_physical(priv,
				      (uint32_t)value_bitmask);
	reset_pins = _max_capi_gpio_logical_to_physical(priv,
					(uint32_t)~value_bitmask);

	_max_capi_gpio_set_enable(priv->port, priv->phys_pin_mask, true);
	MXC_GPIO_OutSet(priv->port, set_pins);
	MXC_GPIO_OutClr(priv->port, reset_pins);

	return 0;
}

/**
 * @brief Get raw value for all pins in the port (ignoring ACTIVE_LOW flag)
 * @param handle - the GPIO handle
 * @param value_bitmask - pointer to where the value bitmask will be stored
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_port_get_raw_value(struct capi_gpio_port_handle
					    *handle,
					    uint64_t *value_bitmask)
{
	struct max_capi_gpio_port_priv *priv;
	uint32_t in_pins, out_pins;

	if (!handle || !handle->priv || !value_bitmask)
		return -EINVAL;

	priv = handle->priv;

	in_pins = MXC_GPIO_InGet(priv->port, priv->phys_pin_mask);
	out_pins = MXC_GPIO_OutGet(priv->port, priv->phys_pin_mask);
	*value_bitmask = _max_capi_gpio_physical_to_logical(priv,
					    in_pins | out_pins);

	return 0;
}

/**
 * @brief Set value for all pins in the port
 * @param handle - the GPIO handle
 * @param value_bitmask - the value bitmask
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_port_set_value(struct capi_gpio_port_handle *handle,
					uint64_t value_bitmask)
{
	struct max_capi_gpio_port_priv *priv;
	uint32_t raw_value;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;
	raw_value = (uint32_t)value_bitmask & priv->pin_mask;

	if (priv->flags) {
		for (uint8_t pin = 0U; pin < priv->num_pins; pin++) {
			if (priv->flags[pin] & CAPI_GPIO_ACTIVE_LOW)
				raw_value ^= (1U << pin);
		}
	}

	return max_capi_gpio_port_set_raw_value(handle, raw_value);
}

/**
 * @brief Get value for all pins in the port
 * @param handle - the GPIO handle
 * @param value_bitmask - pointer to where the value bitmask will be stored
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_port_get_value(struct capi_gpio_port_handle *handle,
					uint64_t *value_bitmask)
{
	struct max_capi_gpio_port_priv *priv;
	uint64_t raw_value;
	int ret;

	if (!handle || !handle->priv || !value_bitmask)
		return -EINVAL;

	priv = handle->priv;

	ret = max_capi_gpio_port_get_raw_value(handle, &raw_value);
	if (ret)
		return ret;

	if (priv->flags) {
		uint32_t logical = (uint32_t)raw_value & priv->pin_mask;

		for (uint8_t pin = 0U; pin < priv->num_pins; pin++) {
			if (priv->flags[pin] & CAPI_GPIO_ACTIVE_LOW)
				logical ^= (1U << pin);
		}

		*value_bitmask = logical;
	} else {
		*value_bitmask = raw_value;
	}

	return 0;
}

/**
 * @brief Set direction for a single pin
 * @param pin - the pin handle
 * @param direction - the direction to set the pin to
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_pin_set_direction(struct capi_gpio_pin *pin,
		uint8_t direction)
{
	struct max_capi_gpio_port_priv *priv;
	mxc_gpio_cfg_t gpio_config;
	uint8_t physical_pin;

	if (!pin || !pin->port_handle || !pin->port_handle->priv)
		return -EINVAL;

	priv = pin->port_handle->priv;

	if (pin->number >= priv->num_pins)
		return -EINVAL;

	physical_pin = pin->number + priv->extra.pin_offset;

	gpio_config = (mxc_gpio_cfg_t) {
		.port = priv->port,
		.mask = (1U << physical_pin),
		.pad = (mxc_gpio_pad_t)priv->extra.pad,
		.vssel = (mxc_gpio_vssel_t)priv->extra.vssel,
	};

	switch (direction) {
	case CAPI_GPIO_OUTPUT:
		gpio_config.func = MXC_GPIO_FUNC_OUT;
		MXC_GPIO_Config(&gpio_config);
		priv->direction_mask &= ~(1 << pin->number);
		break;
	case CAPI_GPIO_INPUT:
		gpio_config.func = MXC_GPIO_FUNC_IN;
		MXC_GPIO_Config(&gpio_config);
		priv->direction_mask |= (1 << pin->number);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Get direction for a single pin
 * @param pin - the pin handle
 * @param direction - pointer to where the direction will be stored
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_pin_get_direction(struct capi_gpio_pin *pin,
		uint8_t *direction)
{
	const struct max_capi_gpio_port_priv *priv;

	if (!pin || !pin->port_handle || !pin->port_handle->priv || !direction)
		return -EINVAL;

	priv = pin->port_handle->priv;

	if (pin->number >= priv->num_pins)
		return -EINVAL;

	if (priv->direction_mask & ((uint32_t)(1 << pin->number)))
		*direction = CAPI_GPIO_INPUT;
	else
		*direction = CAPI_GPIO_OUTPUT;

	return 0;
}

/**
 * @brief Set the raw value of a single pin (ignoring the ACITVE_LOW flag)
 * @param pin - the pin handle
 * @param value - the value (CAPI_GPIO_LOW or CAPI_GPIO_HIGH)
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_pin_set_raw_value(struct capi_gpio_pin *pin,
		uint8_t value)
{
	struct max_capi_gpio_port_priv *priv;
	uint8_t physical_pin;

	if (!pin || !pin->port_handle || !pin->port_handle->priv)
		return -EINVAL;

	priv = pin->port_handle->priv;

	if (pin->number >= priv->num_pins)
		return -EINVAL;

	physical_pin = pin->number + priv->extra.pin_offset;

	_max_capi_gpio_set_enable(priv->port, (1 << physical_pin), true);
	switch (value) {
	case CAPI_GPIO_LOW:
		MXC_GPIO_OutClr(priv->port, (1 << physical_pin));
		break;
	case CAPI_GPIO_HIGH:
		MXC_GPIO_OutSet(priv->port, (1 << physical_pin));
		break;
	/**
	 * In the maxim_gpio implementation, there is a HIGH_Z option that seems to
	 * be missing from CAPI. See: capi/capi_gpio.h.
	 */
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * @brief Get the raw value of a single pin (ignoring the ACTIVE_LOW flag)
 * @param pin - the pin handle
 * @param value - pointer to where value will be stored
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_pin_get_raw_value(struct capi_gpio_pin *pin,
		uint8_t *value)
{
	struct max_capi_gpio_port_priv *priv;
	uint8_t physical_pin;

	if (!pin || !pin->port_handle || !pin->port_handle->priv || !value)
		return -EINVAL;

	priv = pin->port_handle->priv;

	if (pin->number >= priv->num_pins)
		return -EINVAL;

	physical_pin = pin->number + priv->extra.pin_offset;

	_max_capi_gpio_set_enable(priv->port, (1 << physical_pin), true);
	if (priv->direction_mask & (1U << pin->number)) /* Input */
		*value = MXC_GPIO_InGet(priv->port,
					(1U << physical_pin)) >> physical_pin;
	else
		*value = MXC_GPIO_OutGet(priv->port,
					 (1U << physical_pin)) >> physical_pin;

	return 0;
}

/**
 * @brief Apply logical value considering ACTIVE_LOW flag
 * @param raw_value - Raw hardware value
 * @param flags - Pin flags
 * @return Logical value
 */
static inline uint8_t apply_active_low(uint8_t raw_value, uint32_t flags)
{
	if (flags & CAPI_GPIO_ACTIVE_LOW)
		return raw_value ? CAPI_GPIO_LOW : CAPI_GPIO_HIGH;
	return raw_value;
}

/**
 * @brief Convert logical value to raw considering ACTIVE_LOW flag
 * @param logical_value - Logical value
 * @param flags - Pin flags
 * @return Raw hardware value
 */
static inline uint8_t to_raw_value(uint8_t logical_value, uint32_t flags)
{
	if (flags & CAPI_GPIO_ACTIVE_LOW)
		return logical_value ? CAPI_GPIO_LOW : CAPI_GPIO_HIGH;
	return logical_value;
}

/**
 * @brief Set the value of a single pin (considering the ACTIVE_LOW flag)
 * @param pin - the pin handle
 * @param value - the value (CAPI_GPIO_LOW or CAPI_GPIO_HIGH)
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_pin_set_value(struct capi_gpio_pin *pin, uint8_t value)
{
	uint8_t raw_value;

	if (!pin)
		return -EINVAL;

	raw_value = to_raw_value(value, pin->flags);

	return max_capi_gpio_pin_set_raw_value(pin, raw_value);
}

/**
 * @brief Get the value of a single pin (considering the ACTIVE_LOW flag)
 * @param pin - the pin handle
 * @param value - pointer where to store the value
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_pin_get_value(struct capi_gpio_pin *pin,
				       uint8_t *value)
{
	uint8_t raw_value;
	int ret;

	if (!pin || !value)
		return -EINVAL;

	ret = max_capi_gpio_pin_get_raw_value(pin, &raw_value);
	if (ret)
		return ret;

	*value = apply_active_low(raw_value, pin->flags);

	return 0;
}

/**
 * @brief Toggle the value of the specified pins in a port
 * @param handle - the GPIO port handle
 * @param pins_bitmask - bitmask of pins to toggle
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_port_toggle(struct capi_gpio_port_handle *handle,
				     uint64_t pins_bitmask)
{
	struct max_capi_gpio_port_priv *priv;
	uint32_t masked_pins;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;
	masked_pins = _max_capi_gpio_logical_to_physical(priv,
					 (uint32_t)pins_bitmask);

	_max_capi_gpio_set_enable(priv->port, masked_pins, true);
	MXC_GPIO_OutToggle(priv->port, masked_pins);

	return 0;
}

/**
 * @brief Toggle the output value of the specified GPIO pin
 * @param pin - the GPIO pin
 * @return 0 on success, negative error code otherwise
 */
static int max_capi_gpio_pin_toggle(struct capi_gpio_pin *pin)
{
	struct max_capi_gpio_port_priv *priv;
	uint8_t physical_pin;

	if (!pin || !pin->port_handle || !pin->port_handle->priv)
		return -EINVAL;

	priv = pin->port_handle->priv;

	if (pin->number >= priv->num_pins)
		return -EINVAL;

	physical_pin = pin->number + priv->extra.pin_offset;

	_max_capi_gpio_set_enable(priv->port, (1U << physical_pin), true);
	MXC_GPIO_OutToggle(priv->port, (1U << physical_pin));

	return 0;
}

const struct capi_gpio_ops max_capi_gpio_ops = {
	.port_init = &max_capi_gpio_port_init,
	.port_deinit = &max_capi_gpio_port_deinit,
	.port_set_direction = &max_capi_gpio_port_set_direction,
	.port_get_direction = &max_capi_gpio_port_get_direction,
	.port_set_value = &max_capi_gpio_port_set_value,
	.port_get_value = &max_capi_gpio_port_get_value,
	.port_set_raw_value = &max_capi_gpio_port_set_raw_value,
	.port_get_raw_value = &max_capi_gpio_port_get_raw_value,
	.pin_set_direction = &max_capi_gpio_pin_set_direction,
	.pin_get_direction = &max_capi_gpio_pin_get_direction,
	.pin_set_value = &max_capi_gpio_pin_set_value,
	.pin_get_value = &max_capi_gpio_pin_get_value,
	.pin_set_raw_value = &max_capi_gpio_pin_set_raw_value,
	.pin_get_raw_value = &max_capi_gpio_pin_get_raw_value,
	.port_toggle = &max_capi_gpio_port_toggle,
	.pin_toggle = &max_capi_gpio_pin_toggle,
};

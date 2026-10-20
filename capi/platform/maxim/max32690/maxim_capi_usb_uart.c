/***************************************************************************//**
 *   @file   maxim_capi_usb_uart.c
 *   @brief  CAPI UART backend over the MAX32690 USB CDC-ACM device class.
 *   @author Darius Berghe (darius.berghe@analog.com)
 *   @author Ramon Miguel Imbao (ramonmiguel.imbao@analog.com)
********************************************************************************
 * Copyright 2026(c) Analog Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "capi_alloc.h"
#include "capi_irq.h"
#include "capi_uart.h"
#include "maxim_capi_uart_priv.h"
#include "maxim_capi_usb_uart.h"
#include "maxim_capi_usb_uart_priv.h"
#include "mxc_device.h"
#include "mxc_errors.h"
#include "mxc_delay.h"
#include "mxc_sys.h"
#include "mcr_regs.h"
#include "usb.h"
#include "usb_event.h"
#include "enumerate.h"
#include "cdc_acm.h"

/** USB descriptors ***********************************************************/

static MXC_USB_device_descriptor_t __attribute__((aligned(
			4))) device_descriptor = {
	0x12, /* bLength = 18                     */
	0x01, /* bDescriptorType = Device         */
	0x0200, /* bcdUSB USB spec rev (BCD)        */
	0x02, /* bDeviceClass = comm class (2)    */
	0x00, /* bDeviceSubClass                  */
	0x00, /* bDeviceProtocol                  */
	0x40, /* bMaxPacketSize0 is 64 bytes      */
	0x0B6A, /* idVendor (Maxim Integrated)      */
	0x003C, /* idProduct                        */
	0x0100, /* bcdDevice                        */
	0x01, /* iManufacturer Descriptor ID      */
	0x02, /* iProduct Descriptor ID           */
	0x00, /* iSerialNumber = (0) No string    */
	0x01 /* bNumConfigurations               */
};

/* Device qualifier needed for high-speed operation */
static MXC_USB_device_qualifier_descriptor_t __attribute__((aligned(
			4))) device_qualifier_descriptor = {
	0x0A, /* bLength = 10                       */
	0x06, /* bDescriptorType = Device Qualifier */
	0x0200, /* bcdUSB USB spec rev (BCD)          */
	0x02, /* bDeviceClass = Unspecified         */
	0x00, /* bDeviceSubClass                    */
	0x00, /* bDeviceProtocol                    */
	0x40, /* bMaxPacketSize0 is 64 bytes        */
	0x01, /* bNumConfigurations                 */
	0x00 /* Reserved, must be 0                */
};

static __attribute__((aligned(4))) struct __attribute__((packed))
{
	MXC_USB_configuration_descriptor_t config_descriptor;
	/* Interface #1 CDCACM Device */
	MXC_USB_interface_descriptor_t comm_interface_descriptor;
	uint8_t header_functional_descriptor[5];
	uint8_t call_management_descriptor[5];
	uint8_t acm_functional_descriptor[4];
	uint8_t union_functional_descriptor[5];
	/* Interface #2 CDC Data*/
	MXC_USB_endpoint_descriptor_t endpoint_descriptor_3;
	MXC_USB_interface_descriptor_t data_interface_descriptor;
	MXC_USB_endpoint_descriptor_t endpoint_descriptor_4;
	MXC_USB_endpoint_descriptor_t endpoint_descriptor_5;
}

config_descriptor = {
	{
		0x09, /*  bLength = 9                     */
		0x02, /*  bDescriptorType = Config (2)    */
		0x0043, /*  wTotalLength(L/H)               */
		0x02, /*  bNumInterfaces                  */
		0x01, /*  bConfigValue                    */
		0x02, /*  iConfiguration                  */
		0xE0, /*  bmAttributes (self-powered, remote wakeup) */
		0x01, /*  MaxPower is 2ma (units are 2ma/bit) */
	},
	/********** Interface #1 : COMM Interface **********/
	{
		/*  First Interface Descriptor For Comm Class Interface */
		0x09, /*  bLength = 9                     */
		0x04, /*  bDescriptorType = Interface (4) */
		0x00, /*  bInterfaceNumber                */
		0x00, /*  bAlternateSetting               */
		0x01, /*  bNumEndpoints (one for OUT)     */
		0x02, /*  bInterfaceClass = Communications Interface Class (2) */
		0x02, /*  bInterfaceSubClass = Abstract Control Model (2) */
		0x01, /*  bInterfaceProtocol = Common "AT" commands (1), no class specific protocol (0) */
		0x04, /*  iInterface                      */
	},
	{
		/*  Header Functional Descriptor */
		0x05, /*  bFunctionalLength = 5           */
		0x24, /*  bDescriptorType                 */
		0x00, /*  bDescriptorSubtype              */
		0x10, 0x01, /*  bcdCDC                          */
	},
	{
		/*  Call Management Descriptor */
		0x05, /*  bFunctionalLength = 5           */
		0x24, /*  bDescriptorType                 */
		0x01, /*  bDescriptorSubtype              */
		0x03, /*  bmCapabilities = Device handles call management itself (0x01), management over data class (0x02) */
		0x01, /*  bmDataInterface                 */
	},
	{
		/*  Abstract Control Management Functional Descriptor */
		0x04, /*  bFunctionalLength = 4           */
		0x24, /*  bDescriptorType                 */
		0x02, /*  bDescriptorSubtype              */
		0x02, /*  bmCapabilities                  */
	},
	{
		/*  Union Functional Descriptor */
		0x05, /*  bFunctionalLength = 5           */
		0x24, /*  bDescriptorType                 */
		0x06, /*  bDescriptorSubtype              */
		0x00, /*  bmMasterInterface               */
		0x01, /*  bmSlaveInterface0               */
	},
	{
		/*  IN Endpoint 3 (Descriptor #1) */
		0x07, /*  bLength                          */
		0x05, /*  bDescriptorType (Endpoint)       */
		0x84, /*  bEndpointAddress (EP3-IN)        */
		0x03, /*  bmAttributes (interrupt)         */
		0x0040, /*  wMaxPacketSize                   */
		0xff, /*  bInterval (milliseconds)         */
	},
	{
		/*  Second Interface Descriptor For Data Interface */
		0x09, /*  bLength                          */
		0x04, /*  bDescriptorType (Interface)      */
		0x01, /*  bInterfaceNumber                 */
		0x00, /*  bAlternateSetting                */
		0x02, /*  bNumEndpoints                    */
		0x0a, /*  bInterfaceClass = Data Interface (10) */
		0x00, /*  bInterfaceSubClass = none (0)    */
		0x00, /*  bInterfaceProtocol = No class specific protocol (0) */
		0x04, /*  biInterface = No Text String (0) */
	},
	{
		/*  OUT Endpoint 1 (Descriptor #2) */
		0x07, /*  bLength                          */
		0x05, /*  bDescriptorType (Endpoint)       */
		0x05, /*  bEndpointAddress (EP1-OUT)       */
		0x02, /*  bmAttributes (bulk)              */
		0x0040, /*  wMaxPacketSize                   */
		0x00, /*  bInterval (N/A)                  */
	},
	{
		/*  IN Endpoint 2 (Descriptor #3) */
		0x07, /*  bLength                          */
		0x05, /*  bDescriptorType (Endpoint)       */
		0x83, /*  bEndpointAddress (EP2-IN)        */
		0x02, /*  bmAttributes (bulk)              */
		0x0040, /*  wMaxPacketSize                   */
		0x00 /*  bInterval (N/A)                  */
	}
};

static __attribute__((aligned(4))) struct __attribute__((packed))
{
	MXC_USB_configuration_descriptor_t config_descriptor;
	/* Interface #1 CDCACM Device */
	MXC_USB_interface_descriptor_t comm_interface_descriptor;
	uint8_t header_functional_descriptor[5];
	uint8_t call_management_descriptor[5];
	uint8_t acm_functional_descriptor[4];
	uint8_t union_functional_descriptor[5];
	MXC_USB_endpoint_descriptor_t endpoint_descriptor_3;
	/* Interface #2 CDCACM Data */
	MXC_USB_interface_descriptor_t data_interface_descriptor;
	MXC_USB_endpoint_descriptor_t endpoint_descriptor_4;
	MXC_USB_endpoint_descriptor_t endpoint_descriptor_5;
}

config_descriptor_hs = {
	{
		0x09, /*  bLength                          */
		0x02, /*  bDescriptorType = Config         */
		0x0043, /*  wTotalLength(L/H)                */
		0x02, /*  bNumInterfaces                   */
		0x01, /*  bConfigurationValue              */
		0x02, /*  iConfiguration                   */
		0xE0, /*  bmAttributes (bus-powered, remote wakeup) */
		0x32, /*  MaxPower is 100ma (units are 2ma/bit) */
	},
	/********** Interface #1 : COMM Interface **********/
	{
		/*  First Interface Descriptor For Comm Class Interface */
		0x09, /*  bLength = 9                     */
		0x04, /*  bDescriptorType = Interface (4) */
		0x00, /*  bInterfaceNumber                */
		0x00, /*  bAlternateSetting               */
		0x01, /*  bNumEndpoints (one for OUT)     */
		0x02, /*  bInterfaceClass = Communications Interface Class (2) */
		0x02, /*  bInterfaceSubClass = Abstract Control Model (2) */
		0x01, /*  bInterfaceProtocol = Common "AT" commands (1), no class specific protocol (0) */
		0x04, /*  iInterface                      */
	},
	{
		/*  Header Functional Descriptor */
		0x05, /*  bFunctionalLength = 5           */
		0x24, /*  bDescriptorType                 */
		0x00, /*  bDescriptorSubtype              */
		0x10, 0x01, /*  bcdCDC                          */
	},
	{
		/*  Call Management Descriptor */
		0x05, /*  bFunctionalLength = 5           */
		0x24, /*  bDescriptorType                 */
		0x01, /*  bDescriptorSubtype              */
		0x03, /*  bmCapabilities = Device handles call management itself (0x01), management over data class (0x02) */
		0x01, /*  bmDataInterface                 */
	},
	{
		/*  Abstract Control Management Functional Descriptor */
		0x04, /*  bFunctionalLength = 4           */
		0x24, /*  bDescriptorType                 */
		0x02, /*  bDescriptorSubtype              */
		0x02, /*  bmCapabilities                  */
	},
	{
		/*  Union Functional Descriptor */
		0x05, /*  bFunctionalLength = 5           */
		0x24, /*  bDescriptorType                 */
		0x06, /*  bDescriptorSubtype              */
		0x00, /*  bmMasterInterface               */
		0x01, /*  bmSlaveInterface0               */
	},
	{
		/*  IN Endpoint 3 (Descriptor #1) */
		0x07, /*  bLength                          */
		0x05, /*  bDescriptorType (Endpoint)       */
		0x84, /*  bEndpointAddress (EP3-IN)        */
		0x03, /*  bmAttributes (interrupt)         */
		0x0200, /*  wMaxPacketSize                   */
		0xff, /*  bInterval (milliseconds)         */
	},
	{
		/*  Second Interface Descriptor For Data Interface */
		0x09, /*  bLength                          */
		0x04, /*  bDescriptorType (Interface)      */
		0x01, /*  bInterfaceNumber                 */
		0x00, /*  bAlternateSetting                */
		0x02, /*  bNumEndpoints                    */
		0x0a, /*  bInterfaceClass = Data Interface (10) */
		0x00, /*  bInterfaceSubClass = none (0)    */
		0x00, /*  bInterfaceProtocol = No class specific protocol (0) */
		0x04, /*  biInterface = No Text String (0) */
	},
	{
		/*  OUT Endpoint 1 (Descriptor #2) */
		0x07, /*  bLength                          */
		0x05, /*  bDescriptorType (Endpoint)       */
		0x05, /*  bEndpointAddress (EP1-OUT)       */
		0x02, /*  bmAttributes (bulk)              */
		0x0200, /*  wMaxPacketSize                   */
		0x00, /*  bInterval (N/A)                  */
	},
	{
		/*  IN Endpoint 2 (Descriptor #3) */
		0x07, /*  bLength                          */
		0x05, /*  bDescriptorType (Endpoint)       */
		0x83, /*  bEndpointAddress (EP2-IN)        */
		0x02, /*  bmAttributes (bulk)              */
		0x0200, /*  wMaxPacketSize                   */
		0x00, /*  bInterval (N/A)                  */
	},
};

static __attribute__((aligned(4))) uint8_t lang_id_desc[] = {
	0x04, /* bLength */
	0x03, /* bDescriptorType */
	0x09, 0x04 /* bString = wLANGID (see usb_20.pdf 9.6.7 String) */
};

static __attribute__((aligned(4))) uint8_t mfg_id_desc[] = {
	0x22, /* bLength */
	0x03, /* bDescriptorType */
	'M',  0, 'a', 0, 'x', 0, 'i', 0, 'm', 0, ' ', 0, 'I', 0, 'n', 0,
	't',  0, 'e', 0, 'g', 0, 'r', 0, 'a', 0, 't', 0, 'e', 0, 'd', 0,
};

static __attribute__((aligned(4))) uint8_t prod_id_desc[] = {
	0x22, /* bLength */
	0x03, /* bDescriptorType */
	'M',  0, 'A', 0, 'X', 0, '3', 0, '2', 0, '6', 0, '9', 0, '0', 0,
	' ',  0, 'C', 0, 'D', 0, 'C', 0, '-', 0, 'A', 0, 'C', 0, 'M', 0,
};

/* Not currently used (see device descriptor), but could be enabled if desired */
static __attribute__((aligned(4))) uint8_t serial_id_desc[] = { 0x14, /* bLength */
								0x03, /* bDescriptorType */
								'0',  0, '0', 0, '0', 0, '0', 0, '0', 0,
								'0',  0, '0', 0, '0', 0, '1', 0
							      };

static __attribute__((aligned(4))) uint8_t cdcacm_func_desc[] = {
	0x20, /* bLength */
	0x03, /* bDescriptorType */
	'M',  0,   'A', 0,   'X', 0,   '3', 0,   '2', 0,   '6', 0,   '9', 0,   '0',
	0,    ' ', 0,   'C', 0,   'D', 0,   'C', 0,   'A', 0,   'C', 0,   'M', 0,
};

/** Static declarations *******************************************************/

/* MAXUSB, the enumeration layer and the CDC-ACM class are all singletons. */
static struct capi_uart_handle *usb_uart = NULL;

/* This EP assignment must match the configuration descriptor */
static acm_cfg_t acm_cfg = {
	2,			/* EP OUT */
	MXC_USBHS_MAX_PACKET,	/* OUT max packet size */
	3,			/* EP IN */
	MXC_USBHS_MAX_PACKET,	/* IN max packet size */
	4,			/* EP Notify */
	MXC_USBHS_MAX_PACKET,	/* Notify max packet size */
};

static volatile bool remote_wake_en = false;

/** USB stack glue ************************************************************/

/**
 * @brief Delay hook required by MAXUSB.
 * @param usec - Delay in microseconds.
 */
static void _max_capi_usb_delay_us(unsigned int usec)
{
	MXC_Delay(usec);
}

/**
 * @brief Power up the USB block, called by MXC_USB_Init().
 * @return E_NO_ERROR
 */
static int _max_capi_usb_startup(void)
{
	MXC_SYS_ClockSourceEnable(MXC_SYS_CLOCK_IPO);
	MXC_MCR->ldoctrl |= MXC_F_MCR_LDOCTRL_0P9EN;
	MXC_SYS_ClockEnable(MXC_SYS_PERIPH_CLOCK_USB);
	MXC_SYS_Reset_Periph(MXC_SYS_RESET0_USB);

	return E_NO_ERROR;
}

/**
 * @brief Gate the USB clock, called by MXC_USB_Shutdown().
 * @return E_NO_ERROR
 */
static int _max_capi_usb_shutdown(void)
{
	MXC_SYS_ClockDisable(MXC_SYS_PERIPH_CLOCK_USB);

	return E_NO_ERROR;
}

/**
 * @brief SET_CONFIGURATION handler: configure or tear down the CDC-ACM class.
 * @param sud - Setup packet.
 * @param cbdata - Unused.
 * @return 0 on success, non-zero otherwise.
 */
static int _max_capi_usb_setconfig(MXC_USB_SetupPkt *sud, void *cbdata)
{
	(void)cbdata;

	if (sud->wValue == config_descriptor.config_descriptor.bConfigurationValue) {
		acm_cfg.out_ep =
			config_descriptor.endpoint_descriptor_4.bEndpointAddress & 0x7;
		acm_cfg.out_maxpacket =
			config_descriptor.endpoint_descriptor_4.wMaxPacketSize;
		acm_cfg.in_ep =
			config_descriptor.endpoint_descriptor_5.bEndpointAddress & 0x7;
		acm_cfg.in_maxpacket =
			config_descriptor.endpoint_descriptor_5.wMaxPacketSize;
		acm_cfg.notify_ep =
			config_descriptor.endpoint_descriptor_3.bEndpointAddress & 0x7;
		acm_cfg.notify_maxpacket =
			config_descriptor.endpoint_descriptor_3.wMaxPacketSize;

		return acm_configure(&acm_cfg);
	} else if (sud->wValue == 0) {
		return acm_deconfigure();
	}

	return -1;
}

/**
 * @brief SET_FEATURE handler.
 * @param sud - Setup packet.
 * @param cbdata - Unused.
 * @return 0 if the feature is supported, -1 otherwise.
 */
static int _max_capi_usb_setfeature(MXC_USB_SetupPkt *sud, void *cbdata)
{
	(void)cbdata;

	if (sud->wValue != FEAT_REMOTE_WAKE)
		return -1;

	remote_wake_en = true;

	return 0;
}

/**
 * @brief CLEAR_FEATURE handler.
 * @param sud - Setup packet.
 * @param cbdata - Unused.
 * @return 0 if the feature is supported, -1 otherwise.
 */
static int _max_capi_usb_clrfeature(MXC_USB_SetupPkt *sud, void *cbdata)
{
	(void)cbdata;

	if (sud->wValue != FEAT_REMOTE_WAKE)
		return -1;

	remote_wake_en = false;

	return 0;
}

/**
 * @brief USB bus event handler (VBUS, reset, suspend, ...).
 * @param evt - Event.
 * @param data - Unused.
 * @return 0
 */
static int _max_capi_usb_event(maxusb_event_t evt, void *data)
{
	(void)data;

	switch (evt) {
	case MAXUSB_EVENT_NOVBUS:
		MXC_USB_EventDisable(MAXUSB_EVENT_BRST);
		MXC_USB_EventDisable(MAXUSB_EVENT_BRSTDN);
		MXC_USB_EventDisable(MAXUSB_EVENT_SUSP);
		MXC_USB_EventDisable(MAXUSB_EVENT_DPACT);
		MXC_USB_Disconnect();
		enum_clearconfig();
		acm_deconfigure();
		break;

	case MAXUSB_EVENT_VBUS:
		MXC_USB_EventClear(MAXUSB_EVENT_BRST);
		MXC_USB_EventEnable(MAXUSB_EVENT_BRST, _max_capi_usb_event, NULL);
		MXC_USB_EventClear(MAXUSB_EVENT_BRSTDN);
		MXC_USB_EventEnable(MAXUSB_EVENT_BRSTDN, _max_capi_usb_event, NULL);
		MXC_USB_EventClear(MAXUSB_EVENT_SUSP);
		MXC_USB_EventEnable(MAXUSB_EVENT_SUSP, _max_capi_usb_event, NULL);
		MXC_USB_Connect();
		break;

	case MAXUSB_EVENT_BRST:
		enum_clearconfig();
		acm_deconfigure();
		break;

	case MAXUSB_EVENT_BRSTDN:
		/* Swap the active/other configuration to match the bus speed */
		if (MXC_USB_GetStatus() & MAXUSB_STATUS_HIGH_SPEED) {
			enum_register_descriptor(ENUM_DESC_CONFIG,
						 (uint8_t *)&config_descriptor_hs, 0);
			enum_register_descriptor(ENUM_DESC_OTHER,
						 (uint8_t *)&config_descriptor, 0);
		} else {
			enum_register_descriptor(ENUM_DESC_CONFIG,
						 (uint8_t *)&config_descriptor, 0);
			enum_register_descriptor(ENUM_DESC_OTHER,
						 (uint8_t *)&config_descriptor_hs, 0);
		}
		break;

	default:
		break;
	}

	return 0;
}

/** Async RX helpers **********************************************************/

/**
 * @brief Mask the USB interrupt while thread code touches CDC-ACM state that
 *	  the USB ISR also manipulates.
 */
static inline void _max_capi_usb_uart_lock(void)
{
	NVIC_DisableIRQ(USB_IRQn);
}

/**
 * @brief Unmask the USB interrupt.
 */
static inline void _max_capi_usb_uart_unlock(void)
{
	NVIC_EnableIRQ(USB_IRQn);
}

/**
 * @brief Move already-received bytes into the pending receive_async() buffer.
 *
 * Must run either from the USB ISR or with the USB IRQ masked. acm_canread()
 * itself fires ACM_CB_READ_READY when it pulls a new packet, which re-enters
 * here, hence the guard.
 *
 * @param priv - Driver state.
 * @return true when the pending request has been completely filled.
 */
static bool _max_capi_usb_uart_rx_drain(struct max_capi_usb_uart_priv *priv)
{
	uint32_t chunk;
	int avail;

	if (priv->rx_draining)
		return false;

	priv->rx_draining = true;

	while (priv->rx_count < priv->rx_len) {
		avail = acm_canread();
		if (avail <= 0)
			break;

		chunk = priv->rx_len - priv->rx_count;
		if ((uint32_t)avail < chunk)
			chunk = (uint32_t)avail;

		/* Data is already buffered, so this never blocks */
		if (acm_read(priv->rx_buf + priv->rx_count, chunk) != (int)chunk)
			break;

		priv->rx_count += chunk;
	}

	priv->rx_draining = false;

	return priv->rx_count == priv->rx_len;
}

/**
 * @brief Notify the user that the receive_async() request completed.
 *
 * async_rx_in_progress must already have been cleared by the caller, under
 * the same protection that observed completion, so that a racing ISR cannot
 * complete the same request a second time.
 *
 * @param priv - Driver state.
 */
static void _max_capi_usb_uart_rx_notify(struct max_capi_usb_uart_priv *priv)
{
	if (priv->callback)
		priv->callback(CAPI_UART_EVENT_RX_DONE, priv->callback_arg, 0);
}

/**
 * @brief ACM_CB_READ_READY handler, runs in the USB ISR.
 * @return 0
 */
static int _max_capi_usb_uart_read_ready(void)
{
	struct max_capi_usb_uart_priv *priv;

	if (!usb_uart || !usb_uart->priv)
		return 0;

	priv = usb_uart->priv;
	if (!priv->async_rx_in_progress)
		return 0;

	if (_max_capi_usb_uart_rx_drain(priv)) {
		priv->async_rx_in_progress = false;
		_max_capi_usb_uart_rx_notify(priv);
	}

	return 0;
}

/** CAPI UART operations ******************************************************/

/**
 * @brief USB interrupt service routine, connected to USB_IRQn via CAPI IRQ.
 * @param handle - The UART handle.
 */
static void max_capi_usb_uart_isr(void *handle)
{
	(void)handle;

	MXC_USB_EventHandler();
}

/**
 * @brief Initialize the USB CDC-ACM device and connect its interrupt.
 * @param handle - Pointer to the handle. If *handle is NULL it is allocated.
 * @param config - UART configuration. identifier is ignored (single USB
 *		   controller).
 * @return 0 on success, negative error code otherwise.
 */
static int max_capi_usb_uart_init(struct capi_uart_handle **handle,
				  const struct capi_uart_config *config)
{
	struct max_capi_usb_uart_extra *extra;
	struct max_capi_usb_uart_priv *priv;
	struct capi_uart_handle *uart_handle;
	maxusb_cfg_options_t usb_opts = {0};
	int ret;

	if (!handle || !config)
		return -EINVAL;

	if (config->dma_handle)
		return -ENOTSUP;

	if (usb_uart) {
		*handle = usb_uart;
		return 0;
	}

	if (*handle == NULL) {
		uart_handle = capi_calloc(1, sizeof(*uart_handle));
		if (!uart_handle)
			return -ENOMEM;

		priv = capi_calloc(1, sizeof(*priv));
		if (!priv) {
			capi_free(uart_handle);
			return -ENOMEM;
		}

		uart_handle->priv = priv;
		uart_handle->init_allocated = true;
	} else {
		uart_handle = *handle;
		if (!uart_handle->priv)
			return -EINVAL;

		priv = uart_handle->priv;
		uart_handle->init_allocated = false;
	}

	uart_handle->ops = config->ops;
	*priv = (struct max_capi_usb_uart_priv) {
		0
	};

	extra = config->extra;
	device_descriptor.idVendor = extra ? extra->vid :
				     MAX_CAPI_USB_UART_DEFAULT_VID;
	device_descriptor.idProduct = extra ? extra->pid :
				      MAX_CAPI_USB_UART_DEFAULT_PID;
	remote_wake_en = false;

	usb_opts.enable_hs = 1;
	usb_opts.delay_us = _max_capi_usb_delay_us;
	usb_opts.init_callback = _max_capi_usb_startup;
	usb_opts.shutdown_callback = _max_capi_usb_shutdown;

	if (MXC_USB_Init(&usb_opts)) {
		ret = -EIO;
		goto free_handle;
	}

	if (enum_init()) {
		ret = -EIO;
		goto shutdown_usb;
	}

	enum_register_descriptor(ENUM_DESC_DEVICE, (uint8_t *)&device_descriptor, 0);
	enum_register_descriptor(ENUM_DESC_CONFIG, (uint8_t *)&config_descriptor, 0);
	enum_register_descriptor(ENUM_DESC_OTHER, (uint8_t *)&config_descriptor_hs, 0);
	enum_register_descriptor(ENUM_DESC_QUAL,
				 (uint8_t *)&device_qualifier_descriptor, 0);
	enum_register_descriptor(ENUM_DESC_STRING, lang_id_desc, 0);
	enum_register_descriptor(ENUM_DESC_STRING, mfg_id_desc, 1);
	enum_register_descriptor(ENUM_DESC_STRING, prod_id_desc, 2);
	enum_register_descriptor(ENUM_DESC_STRING, serial_id_desc, 3);
	enum_register_descriptor(ENUM_DESC_STRING, cdcacm_func_desc, 4);

	enum_register_callback(ENUM_SETCONFIG, _max_capi_usb_setconfig, NULL);
	enum_register_callback(ENUM_SETFEATURE, _max_capi_usb_setfeature, NULL);
	enum_register_callback(ENUM_CLRFEATURE, _max_capi_usb_clrfeature, NULL);

	if (acm_init(&config_descriptor.comm_interface_descriptor)) {
		ret = -EIO;
		goto shutdown_usb;
	}

	acm_register_callback(ACM_CB_READ_READY, _max_capi_usb_uart_read_ready);

	/* Publish the handle before the first USB interrupt can fire */
	usb_uart = uart_handle;

	ret = capi_irq_connect(USB_IRQn, max_capi_usb_uart_isr, uart_handle);
	if (ret)
		goto unregister;

	MXC_USB_EventEnable(MAXUSB_EVENT_NOVBUS, _max_capi_usb_event, NULL);
	MXC_USB_EventEnable(MAXUSB_EVENT_VBUS, _max_capi_usb_event, NULL);

	ret = capi_irq_enable(USB_IRQn);
	if (ret)
		goto disable_events;

	*handle = uart_handle;

	return 0;

disable_events:
	MXC_USB_EventDisable(MAXUSB_EVENT_NOVBUS);
	MXC_USB_EventDisable(MAXUSB_EVENT_VBUS);
unregister:
	acm_register_callback(ACM_CB_READ_READY, NULL);
	usb_uart = NULL;
shutdown_usb:
	MXC_USB_Shutdown();
free_handle:
	if (uart_handle->init_allocated) {
		capi_free(priv);
		capi_free(uart_handle);
	}

	return ret;
}

/**
 * @brief Disconnect from the host and power down the USB block.
 * @param handle - The UART handle.
 * @return 0 on success, negative error code otherwise.
 */
static int max_capi_usb_uart_deinit(struct capi_uart_handle *handle)
{
	if (!handle || !handle->priv || handle != usb_uart)
		return -EINVAL;

	capi_irq_disable(USB_IRQn);

	acm_register_callback(ACM_CB_READ_READY, NULL);
	MXC_USB_EventDisable(MAXUSB_EVENT_NOVBUS);
	MXC_USB_EventDisable(MAXUSB_EVENT_VBUS);
	MXC_USB_EventDisable(MAXUSB_EVENT_BRST);
	MXC_USB_EventDisable(MAXUSB_EVENT_BRSTDN);
	MXC_USB_EventDisable(MAXUSB_EVENT_SUSP);
	MXC_USB_EventDisable(MAXUSB_EVENT_DPACT);
	MXC_USB_Disconnect();
	enum_clearconfig();
	acm_deconfigure();
	MXC_USB_Shutdown();

	_max_capi_uart_stdio_release(handle);
	usb_uart = NULL;

	if (handle->init_allocated) {
		capi_free(handle->priv);
		capi_free(handle);
	}

	return 0;
}

/**
 * @brief Write data to the host. Blocks until queued to the USB stack.
 *
 * Data is queued one packet at a time, re-checking the host before each
 * packet. A disconnect or bus reset empties the CDC-ACM write FIFO, so the
 * packet in progress always fits and the next check reports -ENODEV instead
 * of spinning forever on a FIFO nobody drains.
 *
 * @param handle - The UART handle.
 * @param buf - Data to send.
 * @param len - Number of bytes to send.
 * @return 0 on success, -ENODEV if no host terminal is (or remains) attached,
 *	   negative error code otherwise.
 */
static int max_capi_usb_uart_transmit(struct capi_uart_handle *handle,
				      uint8_t *buf, uint32_t len)
{
	uint32_t chunk;

	if (!handle || !handle->priv || !buf || !len)
		return -EINVAL;

	while (len) {
		if (!acm_present())
			return -ENODEV;

		chunk = len < ACM_MAX_PACKET ? len : ACM_MAX_PACKET;
		if (acm_write(buf, chunk) != (int)chunk)
			return -EIO;

		buf += chunk;
		len -= chunk;
	}

	return 0;
}

/**
 * @brief Read data from the host. Blocks until len bytes are received.
 * @param handle - The UART handle.
 * @param buf - Destination buffer.
 * @param len - Number of bytes to read.
 * @return 0 on success, -ENODEV if the host terminal is (or gets) detached,
 *	   -EIO on BREAK, -EBUSY if a receive_async() is pending.
 */
static int max_capi_usb_uart_receive(struct capi_uart_handle *handle,
				     uint8_t *buf, uint32_t len)
{
	struct max_capi_usb_uart_priv *priv;
	int ret;

	if (!handle || !handle->priv || !buf || !len)
		return -EINVAL;

	priv = handle->priv;
	if (priv->async_rx_in_progress)
		return -EBUSY;

	ret = acm_read(buf, len);
	if (ret == (int)len)
		return 0;

	if (ret == 0)
		return -ENODEV;

	if (ret == -2)
		priv->break_detected = true;

	return -EIO;
}

/**
 * @brief Register the callback for async completion events.
 * @param handle - The UART handle.
 * @param callback - Callback, invoked from the USB ISR for RX_DONE.
 * @param callback_arg - Callback argument.
 * @return 0 on success, negative error code otherwise.
 */
static int max_capi_usb_uart_register_callback(struct capi_uart_handle *handle,
		capi_uart_callback const callback,
		void *const callback_arg)
{
	struct max_capi_usb_uart_priv *priv;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;

	_max_capi_usb_uart_lock();
	priv->callback = callback;
	priv->callback_arg = callback_arg;
	_max_capi_usb_uart_unlock();

	return 0;
}

/**
 * @brief Send data to the host and report CAPI_UART_EVENT_TX_DONE.
 *
 * CDC-ACM writes only queue data into the USB stack, so the request
 * completes, and the callback fires, before this function returns.
 *
 * @param handle - The UART handle.
 * @param buf - Data to send.
 * @param len - Number of bytes to send.
 * @return 0 on success, negative error code otherwise.
 */
static int max_capi_usb_uart_transmit_async(struct capi_uart_handle *handle,
		uint8_t *buf, uint32_t len)
{
	struct max_capi_usb_uart_priv *priv;
	int ret;

	ret = max_capi_usb_uart_transmit(handle, buf, len);
	if (ret)
		return ret;

	priv = handle->priv;
	if (priv->callback)
		priv->callback(CAPI_UART_EVENT_TX_DONE, priv->callback_arg, 0);

	return 0;
}

/**
 * @brief Receive len bytes in the background and report
 *	  CAPI_UART_EVENT_RX_DONE.
 *
 * Bytes already buffered are consumed immediately; if they satisfy the
 * request the callback fires before this function returns.
 *
 * @param handle - The UART handle.
 * @param buf - Destination buffer, must stay valid until RX_DONE.
 * @param len - Number of bytes to receive.
 * @return 0 on success, -EBUSY if a request is already pending,
 *	   negative error code otherwise.
 */
static int max_capi_usb_uart_receive_async(struct capi_uart_handle *handle,
		uint8_t *buf, uint32_t len)
{
	struct max_capi_usb_uart_priv *priv;
	bool done;

	if (!handle || !handle->priv || !buf || !len)
		return -EINVAL;

	priv = handle->priv;

	_max_capi_usb_uart_lock();
	if (priv->async_rx_in_progress) {
		_max_capi_usb_uart_unlock();
		return -EBUSY;
	}

	priv->rx_buf = buf;
	priv->rx_len = len;
	priv->rx_count = 0;
	priv->async_rx_in_progress = true;

	done = _max_capi_usb_uart_rx_drain(priv);
	if (done)
		priv->async_rx_in_progress = false;
	_max_capi_usb_uart_unlock();

	if (done)
		_max_capi_usb_uart_rx_notify(priv);

	return 0;
}

/**
 * @brief Report the line coding most recently set by the host.
 * @param handle - The UART handle.
 * @param line_config - Output line configuration.
 * @return 0 on success, negative error code otherwise.
 */
static int max_capi_usb_uart_get_line_config(struct capi_uart_handle *handle,
		struct capi_uart_line_config *line_config)
{
	const acm_line_t *coding;

	if (!handle || !handle->priv || !line_config)
		return -EINVAL;

	coding = acm_line_coding();

	*line_config = (struct capi_uart_line_config) {
		.baudrate = coding->speed,
		.flow_control = CAPI_UART_FLOW_CONTROL_NONE,
		.address_mode = CAPI_UART_ADDRESS_MODE_DISABLED,
	};

	switch (coding->databits) {
	case 5:
		line_config->size = CAPI_UART_DATA_BITS_5;
		break;
	case 6:
		line_config->size = CAPI_UART_DATA_BITS_6;
		break;
	case 7:
		line_config->size = CAPI_UART_DATA_BITS_7;
		break;
	default:
		line_config->size = CAPI_UART_DATA_BITS_8;
		break;
	}

	/* CDC PSTN bParityType: 0 none, 1 odd, 2 even, 3 mark, 4 space */
	switch (coding->parity) {
	case 1:
		line_config->parity = CAPI_UART_PARITY_ODD;
		break;
	case 2:
		line_config->parity = CAPI_UART_PARITY_EVEN;
		break;
	case 3:
		line_config->parity = CAPI_UART_PARITY_MARK;
		break;
	case 4:
		line_config->parity = CAPI_UART_PARITY_SPACE;
		break;
	default:
		line_config->parity = CAPI_UART_PARITY_NONE;
		break;
	}

	/* CDC PSTN bCharFormat: 0 = 1, 1 = 1.5, 2 = 2 stop bits */
	line_config->stop_bits = coding->stopbits ? CAPI_UART_STOP_2_BIT :
				 CAPI_UART_STOP_1_BIT;

	return 0;
}

/**
 * @brief Not supported: the USB host owns the line coding.
 * @param handle - The UART handle.
 * @param line_config - Unused.
 * @return -ENOTSUP
 */
static int max_capi_usb_uart_set_line_config(struct capi_uart_handle *handle,
		struct capi_uart_line_config *line_config)
{
	(void)handle;
	(void)line_config;

	return -ENOTSUP;
}

/**
 * @brief Number of received bytes waiting to be read.
 * @param handle - The UART handle.
 * @param count - Output byte count, saturated to UINT16_MAX.
 * @return 0 on success, negative error code otherwise.
 */
static int max_capi_usb_uart_get_rx_fifo_count(struct capi_uart_handle
		*handle, uint16_t *count)
{
	int avail;

	if (!handle || !handle->priv || !count)
		return -EINVAL;

	_max_capi_usb_uart_lock();
	avail = acm_canread();
	_max_capi_usb_uart_unlock();

	if (avail < 0)
		avail = 0;

	*count = avail > UINT16_MAX ? UINT16_MAX : (uint16_t)avail;

	return 0;
}

/**
 * @brief Discard every received byte not yet read.
 * @param handle - The UART handle.
 * @return 0 on success, -EBUSY if a receive_async() is pending.
 */
static int max_capi_usb_uart_flush_rx_fifo(struct capi_uart_handle *handle)
{
	struct max_capi_usb_uart_priv *priv;
	uint8_t scratch[32];
	int avail;

	if (!handle || !handle->priv)
		return -EINVAL;

	priv = handle->priv;
	if (priv->async_rx_in_progress)
		return -EBUSY;

	_max_capi_usb_uart_lock();
	while ((avail = acm_canread()) > 0) {
		if (avail > (int)sizeof(scratch))
			avail = sizeof(scratch);

		if (acm_read(scratch, avail) != avail)
			break;
	}
	_max_capi_usb_uart_unlock();

	return 0;
}

/**
 * @brief Report and clear latched line status flags.
 * @param handle - The UART handle.
 * @param status_flags - Output, CAPI_UART_LINE_STAT_BREAK_IND if the host
 *			 sent a BREAK since the last call.
 * @return 0 on success, negative error code otherwise.
 */
static int max_capi_usb_uart_get_line_status(struct capi_uart_handle *handle,
		uint32_t *status_flags)
{
	struct max_capi_usb_uart_priv *priv;

	if (!handle || !handle->priv || !status_flags)
		return -EINVAL;

	priv = handle->priv;

	*status_flags = priv->break_detected ? CAPI_UART_LINE_STAT_BREAK_IND : 0;
	priv->break_detected = false;

	return 0;
}

/** Unsupported operations ****************************************************/

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_enable_fifo(struct capi_uart_handle *handle,
		bool enable)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_flush_tx_fifo(struct capi_uart_handle *handle)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_get_tx_fifo_count(struct capi_uart_handle
		*handle, uint16_t *count)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_get_interrupt_reason(struct capi_uart_handle
		*handle, enum capi_uart_interrupt_reason *reason)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_transmit_9bit(struct capi_uart_handle *handle,
		uint16_t data, bool is_address)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_receive_9bit(struct capi_uart_handle *handle,
		uint16_t *data, bool *is_address)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_set_flow_control_state(struct capi_uart_handle
		*handle, bool rts_state, bool cts_state)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_get_flow_control_state(struct capi_uart_handle
		*handle, bool *rts_state, bool *cts_state)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return 0 bytes read
 */
static uint32_t max_capi_usb_uart_read_byte(struct capi_uart_handle *handle,
		uint8_t *byte)
{
	return 0;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return 0 bytes written
 */
static uint32_t max_capi_usb_uart_write_byte(struct capi_uart_handle *handle,
		uint8_t byte)
{
	return 0;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_set_irq(struct capi_uart_handle *handle,
				     bool enable)
{
	return -ENOSYS;
}

/**
 * @brief Not supported on USB CDC-ACM.
 * @return -ENOSYS
 */
static int max_capi_usb_uart_get_irq_flag(struct capi_uart_handle *handle,
		bool *flag)
{
	return -ENOSYS;
}

/**
 * @brief MAX32690 USB CDC-ACM CAPI UART operations
 */
const struct capi_uart_ops max_capi_usb_uart_ops = {
	.init = max_capi_usb_uart_init,
	.deinit = max_capi_usb_uart_deinit,
	.transmit = max_capi_usb_uart_transmit,
	.receive = max_capi_usb_uart_receive,
	.transmit_async = max_capi_usb_uart_transmit_async,
	.receive_async = max_capi_usb_uart_receive_async,
	.register_callback = max_capi_usb_uart_register_callback,
	.isr = max_capi_usb_uart_isr,
	.get_line_config = max_capi_usb_uart_get_line_config,
	.set_line_config = max_capi_usb_uart_set_line_config,
	.get_rx_fifo_count = max_capi_usb_uart_get_rx_fifo_count,
	.flush_rx_fifo = max_capi_usb_uart_flush_rx_fifo,
	.get_line_status = max_capi_usb_uart_get_line_status,
	/* Everything below has no CDC-ACM equivalent */
	.enable_fifo = max_capi_usb_uart_enable_fifo,
	.flush_tx_fifo = max_capi_usb_uart_flush_tx_fifo,
	.get_tx_fifo_count = max_capi_usb_uart_get_tx_fifo_count,
	.get_interrupt_reason = max_capi_usb_uart_get_interrupt_reason,
	.transmit_9bit = max_capi_usb_uart_transmit_9bit,
	.receive_9bit = max_capi_usb_uart_receive_9bit,
	.set_flow_control_state = max_capi_usb_uart_set_flow_control_state,
	.get_flow_control_state = max_capi_usb_uart_get_flow_control_state,
	.read_byte = max_capi_usb_uart_read_byte,
	.write_byte = max_capi_usb_uart_write_byte,
	.set_irq_tx = max_capi_usb_uart_set_irq,
	.irq_tx_ready = max_capi_usb_uart_get_irq_flag,
	.irq_tx_complete = max_capi_usb_uart_get_irq_flag,
	.set_irq_rx = max_capi_usb_uart_set_irq,
	.irq_rx_ready = max_capi_usb_uart_get_irq_flag,
	.set_irq_err = max_capi_usb_uart_set_irq,
	.is_irq_pending = max_capi_usb_uart_get_irq_flag,
};

/** Platform-specific functions ***********************************************/

int max_capi_usb_uart_stdio_enable(struct capi_uart_handle *handle)
{
	if (!handle || handle != usb_uart)
		return -EINVAL;

	_max_capi_uart_stdio_set(handle);

	return 0;
}

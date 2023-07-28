#pragma once

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_attr.h"
#include "esp_private/usb_phy.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"


void show_dev_desc(const usb_device_desc_t *dev_desc)
{
	ESP_LOGI("", "bLength: %d", dev_desc->bLength);
	ESP_LOGI("", "bDescriptorType(device): %d", dev_desc->bDescriptorType);
	ESP_LOGI("", "bcdUSB: 0x%x", dev_desc->bcdUSB);
	ESP_LOGI("", "bDeviceClass: 0x%02x", dev_desc->bDeviceClass);
	ESP_LOGI("", "bDeviceSubClass: 0x%02x", dev_desc->bDeviceSubClass);
	ESP_LOGI("", "bDeviceProtocol: 0x%02x", dev_desc->bDeviceProtocol);
	ESP_LOGI("", "bMaxPacketSize0: %d", dev_desc->bMaxPacketSize0);
	ESP_LOGI("", "idVendor: 0x%x", dev_desc->idVendor);
	ESP_LOGI("", "idProduct: 0x%x", dev_desc->idProduct);
	ESP_LOGI("", "bcdDevice: 0x%x", dev_desc->bcdDevice);
	ESP_LOGI("", "iManufacturer: %d", dev_desc->iManufacturer);
	ESP_LOGI("", "iProduct: %d", dev_desc->iProduct);
	ESP_LOGI("", "iSerialNumber: %d", dev_desc->iSerialNumber);
	ESP_LOGI("", "bNumConfigurations: %d", dev_desc->bNumConfigurations);
}

void show_endpoint_desc(const void *p)
{
	const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;
	const char *XFER_TYPE_NAMES[] = {
		"Control", "Isochronous", "Bulk", "Interrupt"
	};
	ESP_LOGI("", "bLength: %d", endpoint->bLength);
	ESP_LOGI("", "bDescriptorType (endpoint): %d", endpoint->bDescriptorType);
	ESP_LOGI("", "bEndpointAddress(%s): 0x%02x",
		(endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK)?"In":"Out",
		endpoint->bEndpointAddress);
	ESP_LOGI("", "bmAttributes(%s): 0x%02x",
		XFER_TYPE_NAMES[endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK],
		endpoint->bmAttributes);
	ESP_LOGI("", "wMaxPacketSize: %d", endpoint->wMaxPacketSize);
	ESP_LOGI("", "bInterval: %d", endpoint->bInterval);
}

void show_config_desc(const void *p)
{
	const usb_config_desc_t *config_desc = (const usb_config_desc_t *)p;

	ESP_LOGI("", "bLength: %d", config_desc->bLength);
	ESP_LOGI("", "bDescriptorType(config): %d", config_desc->bDescriptorType);
	ESP_LOGI("", "wTotalLength: %d", config_desc->wTotalLength);
	ESP_LOGI("", "bNumInterfaces: %d", config_desc->bNumInterfaces);
	ESP_LOGI("", "bConfigurationValue: %d", config_desc->bConfigurationValue);
	ESP_LOGI("", "iConfiguration: %d", config_desc->iConfiguration);
	ESP_LOGI("", "bmAttributes(%s%s%s): 0x%02x",
		(config_desc->bmAttributes & USB_BM_ATTRIBUTES_SELFPOWER)?"Self Powered":"",
		(config_desc->bmAttributes & USB_BM_ATTRIBUTES_WAKEUP)?", Remote Wakeup":"",
		(config_desc->bmAttributes & USB_BM_ATTRIBUTES_BATTERY)?", Battery Powered":"",
		config_desc->bmAttributes);
	ESP_LOGI("", "bMaxPower: %d = %d mA", config_desc->bMaxPower, config_desc->bMaxPower*2);
}

uint8_t show_interface_desc(const void *p)
{
	const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;

	ESP_LOGI("", "bLength: %d", intf->bLength);
	ESP_LOGI("", "bDescriptorType (interface): %d", intf->bDescriptorType);
	ESP_LOGI("", "bInterfaceNumber: %d", intf->bInterfaceNumber);
	ESP_LOGI("", "bAlternateSetting: %d", intf->bAlternateSetting);
	ESP_LOGI("", "bNumEndpoints: %d", intf->bNumEndpoints);
	ESP_LOGI("", "bInterfaceClass: 0x%02x", intf->bInterfaceClass);
	ESP_LOGI("", "bInterfaceSubClass: 0x%02x", intf->bInterfaceSubClass);
	ESP_LOGI("", "bInterfaceProtocol: 0x%02x", intf->bInterfaceProtocol);
	ESP_LOGI("", "iInterface: %d", intf->iInterface);
	return intf->bInterfaceClass;
}


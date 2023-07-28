#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#include "esp_attr.h"
#include "esp_private/usb_phy.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "libusb.h"
#include "libuvc.h"
#include "libuvc_internal.h"
#include "descriptor_log.h"

struct class_driver_control {
	uint32_t actions;
	uint8_t dev_addr;
	usb_host_client_handle_t client_hdl;
	usb_device_handle_t dev_hdl;
};

#define CLASS_DRIVER_ACTION_OPEN_DEV    0x01
#define CLASS_DRIVER_ACTION_TRANSFER    0x02
#define CLASS_DRIVER_ACTION_CLOSE_DEV   0x03

typedef struct _USBCB  /* USB command block */
{
	unsigned short u32_Command;  /* command to execute */
	unsigned short u32_Data;  /* generic data field */
	unsigned int u32_Count;  /* number of bytes to transfer */
} USBCB, *PUSBCB;

static void transfer_cb(usb_transfer_t *transfer)
{
	//This is function is called from within usb_host_client_handle_events(). Don't block and try to keep it short
	struct class_driver_control *class_driver_obj = (struct class_driver_control *)transfer->context;
	ESP_LOGI("", "Transfer status %d, actual number of bytes transferred %d\n", transfer->status, transfer->actual_num_bytes);
	//class_driver_obj->actions |= CLASS_DRIVER_ACTION_CLOSE_DEV;
}

#if 0
/** Converts an unaligned four-byte little-endian integer into an int32 */
#define DW_TO_INT(p) ((p)[0] | ((p)[1] << 8) | ((p)[2] << 16) | ((p)[3] << 24))
/** Converts an unaligned two-byte little-endian integer into an int16 */
#define SW_TO_SHORT(p) ((p)[0] | ((p)[1] << 8))
/** Converts an int16 into an unaligned two-byte little-endian integer */
#define SHORT_TO_SW(s, p) \
  (p)[0] = (s); \
  (p)[1] = (s) >> 8;
/** Converts an int32 into an unaligned four-byte little-endian integer */
#define INT_TO_DW(i, p) \
  (p)[0] = (i); \
  (p)[1] = (i) >> 8; \
  (p)[2] = (i) >> 16; \
  (p)[3] = (i) >> 24;

void uvc_parse_vc_header(const uint8_t* block, size_t block_size)
{
	uint16_t bcdUVC = SW_TO_SHORT(&block[3]);

	uint32_t dwClockFrequency = 0;

	switch (bcdUVC)
	{
		case 0x0100:
			dwClockFrequency = DW_TO_INT(block + 7);
			break;
		case 0x010a:
			dwClockFrequency = DW_TO_INT(block + 7);
			break;
		case 0x0110:
			break;
	}

	ESP_LOGI("", "bcdUVC: %x", bcdUVC);
	ESP_LOGI("", "dwClockFrequency: %d", (int)dwClockFrequency);
	ESP_LOGI("", "block size: %d", block_size);

	for (size_t i = 12; i < block_size; ++i)
	{
		int interface_idx = block[i];

		ESP_LOGI("", "found interface: of header: %d", interface_idx);
	}
}
#endif

uvc_error_t uvc_scan_control(uvc_device_info_t *info);

void _client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
	struct class_driver_control *class_driver_obj = (struct class_driver_control *)arg;
	esp_err_t err;

	switch (event_msg->event) {
		case USB_HOST_CLIENT_EVENT_NEW_DEV:
			ESP_LOGI("", "new dev %d", event_msg->new_dev.address);
			class_driver_obj->actions |= CLASS_DRIVER_ACTION_OPEN_DEV;
			class_driver_obj->dev_addr = event_msg->new_dev.address; //Store the address of the new device

			err = usb_host_device_open(class_driver_obj->client_hdl, class_driver_obj->dev_addr, &class_driver_obj->dev_hdl);

			if (err != ESP_OK) ESP_LOGI("", "Could not open device: %s", esp_err_to_name(err));

			usb_device_info_t dev_info;
			err = usb_host_device_info(class_driver_obj->dev_hdl, &dev_info);
			if (err != ESP_OK) ESP_LOGI("", "usb_host_device_info: %x", err);
			if (err != ESP_OK) ESP_LOGI("", "usb_host_device_info: %s", esp_err_to_name(err));
			ESP_LOGI("", "speed: %d dev_addr %d vMaxPacketSize0 %d bConfigurationValue %d",
				dev_info.speed, dev_info.dev_addr, dev_info.bMaxPacketSize0,
				dev_info.bConfigurationValue);

			const usb_device_desc_t *dev_desc;
			err = usb_host_get_device_descriptor(class_driver_obj->dev_hdl, &dev_desc);
			if (err != ESP_OK) ESP_LOGI("", "usb_host_get_device_desc: %x", err);
			if (err != ESP_OK) ESP_LOGI("", "usb_host_get_device_desc: %d", err);
			show_dev_desc(dev_desc);

			const usb_config_desc_t *config_desc;
			err = usb_host_get_active_config_descriptor(class_driver_obj->dev_hdl, &config_desc);
			if (err != ESP_OK) ESP_LOGI("", "usb_host_get_config_desc: %s", esp_err_to_name(err));

			ESP_LOGI("", "-------------------");

			const uint8_t* buffer = &config_desc->val[0];
			libusb_config_descriptor* libusbConfigDescriptor = NULL;
			raw_desc_to_config(buffer, config_desc->wTotalLength, &libusbConfigDescriptor);

			uvc_device_info_t *info = (uvc_device_info_t*) malloc(sizeof(uvc_device_info_t));
			info->config = libusbConfigDescriptor;
			info->stream_ifs = NULL;
			uvc_scan_control(info);

			bool foundVideo = false;

#if 0
			{
				const uint8_t* p = &config_desc->val[0];
				uint8_t bLength;

				for (int i = 0; i < config_desc->wTotalLength; i += bLength, p += bLength)
				{
					bLength = *p;
					if ((i + bLength) <= config_desc->wTotalLength)
					{
						const uint8_t bDescriptorType = *(p + 1);

						switch (bDescriptorType)
						{
							case USB_B_DESCRIPTOR_TYPE_DEVICE:
								ESP_LOGI("", "USB Device Descriptor should not appear in config");
								break;
							case USB_B_DESCRIPTOR_TYPE_CONFIGURATION:
								show_config_desc(p);
								ESP_LOGI("", "-------------------");
								break;
							case USB_B_DESCRIPTOR_TYPE_STRING:
								ESP_LOGI("", "USB string desc TBD");
								break;
							case USB_B_DESCRIPTOR_TYPE_INTERFACE:
							{
								show_interface_desc(p);
								ESP_LOGI("", "-------------------");
								//check_interface_desc_printer(p);

								const usb_intf_desc_t* intf = (const usb_intf_desc_t *)p;

								if (intf->bInterfaceClass == USB_CLASS_VIDEO && intf->bInterfaceSubClass == VIDEO_SUBCLASS_CONTROL) {
									ESP_LOGD("", "Found Video Control interface");
									foundVideo = true;
								}
								else
								{
									foundVideo = false;
								}

								break;
							}
							case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
								show_endpoint_desc(p);
								ESP_LOGI("", "-------------------");
								//prepare_endpoints(p);
								break;

							case CS_INTERFACE_DESC:
								ESP_LOGI("", "CS_INTERFACE_DESC");
								if (foundVideo)
								{
									int descriptor_subtype = p[2];

									switch (descriptor_subtype)
									{
										case UVC_VC_HEADER:
											ESP_LOGI("", "UVC_VC_HEADER");
											uvc_parse_vc_header(p, bLength);
											break;
										case UVC_VC_INPUT_TERMINAL:
											ESP_LOGI("", "UVC_VC_INPUT_TERMINAL");
											break;
										case UVC_VC_OUTPUT_TERMINAL:
											ESP_LOGI("", "UVC_VC_OUTPUT_TERMINAL");
											break;
										case UVC_VC_SELECTOR_UNIT:
											ESP_LOGI("", "UVC_VC_SELECTOR_UNIT");
											break;
										case UVC_VC_PROCESSING_UNIT:
											ESP_LOGI("", "UVC_VC_PROCESSING_UNIT");
											break;
										case UVC_VC_EXTENSION_UNIT:
											ESP_LOGI("", "UVC_VC_EXTENSION_UNIT");
											break;
									}
								}
								break;
							default:
								ESP_LOGI("", "Unknown USB Descriptor Type: 0x%x", *p);
								break;
						}
					}
				}
			}
#endif

			break;
		case USB_HOST_CLIENT_EVENT_DEV_GONE:
			class_driver_obj->actions |= CLASS_DRIVER_ACTION_CLOSE_DEV;
			break;
		default:
			break;
	}
}

void app_main(void)
{
	struct class_driver_control class_driver_obj = {0};

	esp_err_t ret = ESP_OK;
	usb_phy_config_t phy_config = {
		.controller = USB_PHY_CTRL_OTG,
		.target = USB_PHY_TARGET_INT,
		.otg_mode = USB_OTG_MODE_HOST,
		.otg_speed = USB_PHY_SPEED_UNDEFINED,   //In Host mode, the speed is determined by the connected device
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
		.otg_io_conf = NULL,
#else
		.gpio_conf = NULL,
#endif
	};

	usb_phy_handle_t phy_handle = NULL;
	ret = usb_new_phy(&phy_config, &phy_handle);
	//UVC_CHECK(ESP_OK == ret, "USB PHY init failed", NULL);

	const usb_host_config_t config = {
		.skip_phy_setup = true,
		.intr_flags = ESP_INTR_FLAG_LEVEL1,
	};
	ret = usb_host_install(&config);

	const usb_host_client_config_t client_config = {
		.is_synchronous = false,
		.max_num_event_msg = 10,
		.async = {
			.client_event_callback = _client_event_callback,
			.callback_arg = &class_driver_obj
		}
	};

	ret = usb_host_client_register(&client_config, &class_driver_obj.client_hdl);
	///////////////

	uint32_t event_flags;
	while (1)
	{
		usb_host_client_handle_events(class_driver_obj.client_hdl, 50);

		if (class_driver_obj.actions & CLASS_DRIVER_ACTION_TRANSFER)
		{
			usb_transfer_t *transfer;
			usb_host_transfer_alloc(sizeof(USBCB), 0, &transfer);

			USBCB usbcb;
			usbcb.u32_Command =0x0001|(0x0b<<8) ;//USB_TEMP_TRANS;	// command
			usbcb.u32_Count = 0;		// number of bytes
			usbcb.u32_Data = 0x0;		// doesn't matter, device determines location

			xthal_memcpy(transfer->data_buffer, &usbcb, sizeof(USBCB));

			transfer->num_bytes = sizeof(USBCB);
			transfer->device_handle = class_driver_obj.dev_hdl;
			transfer->bEndpointAddress = class_driver_obj.dev_addr;
			transfer->callback = transfer_cb;
			transfer->context = (void *)&class_driver_obj;
			transfer->flags |= USB_TRANSFER_FLAG_ZERO_PACK;
			esp_err_t err = usb_host_transfer_submit(transfer);

			if (err != ESP_OK)
			{
				ESP_LOGI("", "usb_host_transfer_submit In fail: %x", err);
				ESP_LOGI("", "usb_host_transfer_submit In fail: %s", esp_err_to_name(err));
			}

			//class_driver_obj.actions = 0;
		}

		if (class_driver_obj.actions & CLASS_DRIVER_ACTION_OPEN_DEV) {
			//Open the device and claim interface 1

			usb_host_interface_claim(class_driver_obj.client_hdl, class_driver_obj.dev_hdl, 0, 0);

			class_driver_obj.actions = 0;
		}



		if (ESP_OK == usb_host_lib_handle_events(5, &event_flags))
		{
			if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
			{
				usb_host_device_free_all();
			}
			if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
			{
				break;
			}
		}
	}
}
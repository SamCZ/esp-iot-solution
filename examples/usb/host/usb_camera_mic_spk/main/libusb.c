#include "libusb.h"
#include <string.h>
#include "esp_log.h"

#define LOG_TAG "libusb"

#define DESC_HEADER_LENGTH	2

#define READ_LE16(p) ((uint16_t)	\
	(((uint16_t)((p)[1]) << 8) |	\
	 ((uint16_t)((p)[0]))))

#define READ_LE32(p) ((uint32_t)	\
	(((uint32_t)((p)[3]) << 24) |	\
	 ((uint32_t)((p)[2]) << 16) |	\
	 ((uint32_t)((p)[1]) <<  8) |	\
	 ((uint32_t)((p)[0]))))


static void parse_descriptor(const void *source, const char *descriptor, void *dest)
{
	const uint8_t *sp = source;
	uint8_t *dp = dest;
	char field_type;

	while (*descriptor) {
		field_type = *descriptor++;
		switch (field_type) {
			case 'b':	/* 8-bit byte */
				*dp++ = *sp++;
				break;
			case 'w':	/* 16-bit word, convert from little endian to CPU */
				dp += ((uintptr_t)dp & 1);	/* Align to 16-bit word boundary */

				*((uint16_t *)dp) = READ_LE16(sp);
				sp += 2;
				dp += 2;
				break;
			case 'd':	/* 32-bit word, convert from little endian to CPU */
				dp += 4 - ((uintptr_t)dp & 3);	/* Align to 32-bit word boundary */

				*((uint32_t *)dp) = READ_LE32(sp);
				sp += 4;
				dp += 4;
				break;
			case 'u':	/* 16 byte UUID */
				memcpy(dp, sp, 16);
				sp += 16;
				dp += 16;
				break;
		}
	}
}

static void clear_endpoint(libusb_endpoint_descriptor *endpoint)
{
	free((void *)endpoint->extra);
}

static void clear_interface(libusb_interface *usb_interface)
{
	int i;

	if (usb_interface->altsetting) {
		for (i = 0; i < usb_interface->num_altsetting; i++) {
			libusb_interface_descriptor *ifp =
				(libusb_interface_descriptor *)
					usb_interface->altsetting + i;

			free((void *)ifp->extra);
			if (ifp->endpoint) {
				uint8_t j;

				for (j = 0; j < ifp->bNumEndpoints; j++)
					clear_endpoint((libusb_endpoint_descriptor *)
						ifp->endpoint + j);
			}
			free((void *)ifp->endpoint);
		}
	}
	free((void *)usb_interface->altsetting);
	usb_interface->altsetting = NULL;
}


static int parse_endpoint(libusb_endpoint_descriptor *endpoint, const uint8_t *buffer, int size)
{
	const usbi_descriptor_header *header;
	const uint8_t *begin;
	void *extra;
	int parsed = 0;
	int len;

	if (size < DESC_HEADER_LENGTH) {
		ESP_LOGE(LOG_TAG, "short endpoint descriptor read %d/%d",
			size, DESC_HEADER_LENGTH);
		return LIBUSB_ERROR_IO;
	}

	header = (const usbi_descriptor_header *)buffer;
	if (header->bDescriptorType != LIBUSB_DT_ENDPOINT) {
		ESP_LOGE(LOG_TAG, "unexpected descriptor 0x%x (expected 0x%x)",
			header->bDescriptorType, LIBUSB_DT_ENDPOINT);
		return parsed;
	} else if (header->bLength < LIBUSB_DT_ENDPOINT_SIZE) {
		ESP_LOGE(LOG_TAG, "invalid endpoint bLength (%u)", header->bLength);
		return LIBUSB_ERROR_IO;
	} else if (header->bLength > size) {
		ESP_LOGW(LOG_TAG, "short endpoint descriptor read %d/%u",
			size, header->bLength);
		return parsed;
	}

	if (header->bLength >= LIBUSB_DT_ENDPOINT_AUDIO_SIZE)
		parse_descriptor(buffer, "bbbbwbbb", endpoint);
	else
		parse_descriptor(buffer, "bbbbwb", endpoint);

	buffer += header->bLength;
	size -= header->bLength;
	parsed += header->bLength;

	/* Skip over the rest of the Class Specific or Vendor Specific */
	/*  descriptors */
	begin = buffer;
	while (size >= DESC_HEADER_LENGTH) {
		header = (const usbi_descriptor_header *)buffer;
		if (header->bLength < DESC_HEADER_LENGTH) {
			ESP_LOGE(LOG_TAG, "invalid extra ep desc len (%u)",
				header->bLength);
			return LIBUSB_ERROR_IO;
		} else if (header->bLength > size) {
			ESP_LOGW(LOG_TAG, "short extra ep desc read %d/%u",
				size, header->bLength);
			return parsed;
		}

		/* If we find another "proper" descriptor then we're done  */
		if (header->bDescriptorType == LIBUSB_DT_ENDPOINT ||
			header->bDescriptorType == LIBUSB_DT_INTERFACE ||
			header->bDescriptorType == LIBUSB_DT_CONFIG ||
			header->bDescriptorType == LIBUSB_DT_DEVICE)
			break;

		ESP_LOGI(LOG_TAG, "skipping descriptor 0x%x", header->bDescriptorType);
		buffer += header->bLength;
		size -= header->bLength;
		parsed += header->bLength;
	}

	/* Copy any unknown descriptors into a storage area for drivers */
	/*  to later parse */
	len = (int)(buffer - begin);
	if (len <= 0)
		return parsed;

	extra = malloc((size_t)len);
	if (!extra)
		return LIBUSB_ERROR_NO_MEM;

	memcpy(extra, begin, len);
	endpoint->extra = extra;
	endpoint->extra_length = len;

	return parsed;
}

static int parse_interface(libusb_interface *usb_interface, const uint8_t *buffer, int size)
{
	int len;
	int r;
	int parsed = 0;
	int interface_number = -1;
	const usbi_descriptor_header *header;
	const usbi_interface_descriptor *if_desc;
	libusb_interface_descriptor *ifp;
	const uint8_t *begin;

	while (size >= LIBUSB_DT_INTERFACE_SIZE)
	{
		libusb_interface_descriptor *altsetting;

		altsetting = realloc((void *)usb_interface->altsetting,
			sizeof(*altsetting) * (size_t)(usb_interface->num_altsetting + 1));

		if (!altsetting) {
			r = LIBUSB_ERROR_NO_MEM;
			goto err;
		}

		usb_interface->altsetting = altsetting;

		ifp = altsetting + usb_interface->num_altsetting;
		parse_descriptor(buffer, "bbbbbbbbb", ifp);
		if (ifp->bDescriptorType != LIBUSB_DT_INTERFACE) {
			ESP_LOGE(LOG_TAG, "unexpected descriptor 0x%x (expected 0x%x)",
				ifp->bDescriptorType, LIBUSB_DT_INTERFACE);
			return parsed;
		} else if (ifp->bLength < LIBUSB_DT_INTERFACE_SIZE) {
			ESP_LOGE(LOG_TAG, "invalid interface bLength (%u)",
				ifp->bLength);
			r = LIBUSB_ERROR_IO;
			goto err;
		} else if (ifp->bLength > size) {
			ESP_LOGW(LOG_TAG, "short intf descriptor read %d/%u",
				size, ifp->bLength);
			return parsed;
		} else if (ifp->bNumEndpoints > USB_MAXENDPOINTS) {
			ESP_LOGE(LOG_TAG, "too many endpoints (%u)", ifp->bNumEndpoints);
			r = LIBUSB_ERROR_IO;
			goto err;
		}

		usb_interface->num_altsetting++;
		ifp->extra = NULL;
		ifp->extra_length = 0;
		ifp->endpoint = NULL;

		if (interface_number == -1)
			interface_number = ifp->bInterfaceNumber;

		/* Skip over the interface */
		buffer += ifp->bLength;
		parsed += ifp->bLength;
		size -= ifp->bLength;

		begin = buffer;

		/* Skip over any interface, class or vendor descriptors */
		while (size >= DESC_HEADER_LENGTH) {
			header = (const usbi_descriptor_header *)buffer;
			if (header->bLength < DESC_HEADER_LENGTH) {
				ESP_LOGE(LOG_TAG,
					"invalid extra intf desc len (%u)",
					header->bLength);
				r = LIBUSB_ERROR_IO;
				goto err;
			} else if (header->bLength > size) {
				ESP_LOGW(LOG_TAG,
					"short extra intf desc read %d/%u",
					size, header->bLength);
				return parsed;
			}

			/* If we find another "proper" descriptor then we're done */
			if (header->bDescriptorType == LIBUSB_DT_INTERFACE ||
				header->bDescriptorType == LIBUSB_DT_ENDPOINT ||
				header->bDescriptorType == LIBUSB_DT_CONFIG ||
				header->bDescriptorType == LIBUSB_DT_DEVICE)
				break;

			buffer += header->bLength;
			parsed += header->bLength;
			size -= header->bLength;
		}

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = (int)(buffer - begin);
		if (len > 0) {
			void *extra = malloc((size_t)len);

			if (!extra) {
				r = LIBUSB_ERROR_NO_MEM;
				goto err;
			}

			memcpy(extra, begin, len);
			ifp->extra = extra;
			ifp->extra_length = len;
		}

		if (ifp->bNumEndpoints > 0) {
			libusb_endpoint_descriptor *endpoint;
			uint8_t i;

			endpoint = calloc(ifp->bNumEndpoints, sizeof(*endpoint));
			if (!endpoint) {
				r = LIBUSB_ERROR_NO_MEM;
				goto err;
			}

			ifp->endpoint = endpoint;
			for (i = 0; i < ifp->bNumEndpoints; i++) {
				r = parse_endpoint(endpoint + i, buffer, size);
				if (r < 0)
					goto err;
				if (r == 0) {
					ifp->bNumEndpoints = i;
					break;
				}

				buffer += r;
				parsed += r;
				size -= r;
			}
		}

		/* We check to see if it's an alternate to this one */
		if_desc = (const usbi_interface_descriptor *)buffer;
		if (size < LIBUSB_DT_INTERFACE_SIZE ||
			if_desc->bDescriptorType != LIBUSB_DT_INTERFACE ||
			if_desc->bInterfaceNumber != interface_number)
			return parsed;
	}

	return parsed;
err:
	clear_interface(usb_interface);
	return r;
}

static int parse_configuration(libusb_config_descriptor *config, const uint8_t *buffer, int size)
{
	uint8_t i;
	int r;
	const usbi_descriptor_header *header;
	libusb_interface *usb_interface;

	if (size < LIBUSB_DT_CONFIG_SIZE) {
		ESP_LOGE(LOG_TAG, "short config descriptor read %d/%d",
			size, LIBUSB_DT_CONFIG_SIZE);
		return LIBUSB_ERROR_IO;
	}

	parse_descriptor(buffer, "bbwbbbbb", config);

	if (config->bDescriptorType != LIBUSB_DT_CONFIG) {
		ESP_LOGE(LOG_TAG, "unexpected descriptor 0x%x (expected 0x%x)",
			config->bDescriptorType, LIBUSB_DT_CONFIG);
		return LIBUSB_ERROR_IO;
	} else if (config->bLength < LIBUSB_DT_CONFIG_SIZE) {
		ESP_LOGE(LOG_TAG, "invalid config bLength (%u)", config->bLength);
		return LIBUSB_ERROR_IO;
	} else if (config->bLength > size) {
		ESP_LOGE(LOG_TAG, "short config descriptor read %d/%u",
			size, config->bLength);
		return LIBUSB_ERROR_IO;
	} else if (config->bNumInterfaces > USB_MAXINTERFACES) {
		ESP_LOGE(LOG_TAG, "too many interfaces (%u)", config->bNumInterfaces);
		return LIBUSB_ERROR_IO;
	}

	usb_interface = calloc(config->bNumInterfaces, sizeof(*usb_interface));
	if (!usb_interface)
		return LIBUSB_ERROR_NO_MEM;

	config->interface = usb_interface;

	buffer += config->bLength;
	size -= config->bLength;

	for (i = 0; i < config->bNumInterfaces; i++)
	{
		int len;
		const uint8_t *begin;

		/* Skip over the rest of the Class Specific or Vendor */
		/*  Specific descriptors */
		begin = buffer;
		while (size >= DESC_HEADER_LENGTH)
		{
			header = (const usbi_descriptor_header *)buffer;
			if (header->bLength < DESC_HEADER_LENGTH) {
				ESP_LOGE(LOG_TAG,
					"invalid extra config desc len (%u)",
					header->bLength);
				r = LIBUSB_ERROR_IO;
				goto err;
			} else if (header->bLength > size) {
				ESP_LOGE(LOG_TAG,
					"short extra config desc read %d/%u",
					size, header->bLength);
				config->bNumInterfaces = i;
				return size;
			}

			/* If we find another "proper" descriptor then we're done */
			if (header->bDescriptorType == LIBUSB_DT_ENDPOINT ||
				header->bDescriptorType == LIBUSB_DT_INTERFACE ||
				header->bDescriptorType == LIBUSB_DT_CONFIG ||
				header->bDescriptorType == LIBUSB_DT_DEVICE)
				break;

			ESP_LOGI(LOG_TAG, "skipping descriptor 0x%x", header->bDescriptorType);
			buffer += header->bLength;
			size -= header->bLength;
		}

		/* Copy any unknown descriptors into a storage area for */
		/*  drivers to later parse */
		len = (int)(buffer - begin);
		if (len > 0) {
			uint8_t *extra = realloc((void *)config->extra,
				(size_t)(config->extra_length + len));

			if (!extra) {
				r = LIBUSB_ERROR_NO_MEM;
				goto err;
			}

			memcpy(extra + config->extra_length, begin, len);
			config->extra = extra;
			config->extra_length += len;
		}

		r = parse_interface(usb_interface + i, buffer, size);
		if (r < 0)
			goto err;
		if (r == 0) {
			config->bNumInterfaces = i;
			break;
		}

		buffer += r;
		size -= r;
	}

	ESP_LOGI(LOG_TAG, "parse_configuration success");

	return size;

err:
	return -1;
}

int raw_desc_to_config(const uint8_t *buf, int size, libusb_config_descriptor **config)
{
	libusb_config_descriptor *_config = calloc(1, sizeof(libusb_config_descriptor));
	int r;

	r = parse_configuration(_config, buf, size);
	if (r < 0)
	{
		ESP_LOGE(LOG_TAG, "parse_configuration failed with error %d", r);
		free(_config);
		return r;
	}
	else if (r > 0)
	{
		ESP_LOGE(LOG_TAG, "still %d bytes of descriptor data left", r);
	}

	*config = _config;
	return 0;
}
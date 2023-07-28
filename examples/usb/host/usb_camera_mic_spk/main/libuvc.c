#include "libuvc.h"
#include "libuvc_internal.h"
#include "uvcutil.h"
#include "esp_log.h"
#include <string.h>

#define LOG_TAG "libuvc"

/** @internal
 * @brief Parse a VideoStreaming header block.
 * @ingroup device
 */
uvc_error_t uvc_parse_vs_input_header(uvc_streaming_interface_t *stream_if,
	const unsigned char *block,
	size_t block_size)
{

	stream_if->bEndpointAddress = block[6] & 0x8f;
	stream_if->bTerminalLink = block[8];
	stream_if->bStillCaptureMethod = block[9];

	return UVC_SUCCESS;
}

/** @internal
 * @brief Parse a VideoStreaming still iamge frame
 * @ingroup device
 */
uvc_error_t uvc_parse_vs_still_image_frame(uvc_streaming_interface_t *stream_if,
	const unsigned char *block,
	size_t block_size) {

	struct uvc_still_frame_desc* frame;
	uvc_format_desc_t *format;

	const unsigned char *p;
	int i;

	format = stream_if->format_descs->prev;
	frame = calloc(1, sizeof(*frame));

	frame->parent = format;

	frame->bDescriptorSubtype = block[2];
	frame->bEndPointAddress   = block[3];
	uint8_t numImageSizePatterns = block[4];

	frame->imageSizePatterns = NULL;

	p = &block[5];

	for (i = 1; i <= numImageSizePatterns; ++i) {
		uvc_still_frame_res_t* res = calloc(1, sizeof(uvc_still_frame_res_t));
		res->bResolutionIndex = i;
		res->wWidth = SW_TO_SHORT(p);
		p += 2;
		res->wHeight = SW_TO_SHORT(p);
		p += 2;

		ESP_LOGI(LOG_TAG, "StillImage w=%d, h=%d", res->wWidth, res->wHeight);

		DL_APPEND(frame->imageSizePatterns, res);
	}

	p = &block[5+4*numImageSizePatterns];
	frame->bNumCompressionPattern = *p;

	if(frame->bNumCompressionPattern)
	{
		frame->bCompression = calloc(frame->bNumCompressionPattern, sizeof(frame->bCompression[0]));
		for(i = 0; i < frame->bNumCompressionPattern; ++i)
		{
			++p;
			frame->bCompression[i] = *p;
		}
	}
	else
	{
		frame->bCompression = NULL;
	}

	DL_APPEND(format->still_frame_desc, frame);

	return UVC_SUCCESS;
}

/** @internal
 * @brief Parse a VideoStreaming uncompressed format block.
 * @ingroup device
 */
uvc_error_t uvc_parse_vs_format_uncompressed(uvc_streaming_interface_t *stream_if,
	const unsigned char *block,
	size_t block_size) {

	uvc_format_desc_t *format = calloc(1, sizeof(*format));

	format->parent = stream_if;
	format->bDescriptorSubtype = block[2];
	format->bFormatIndex = block[3];
	//format->bmCapabilities = block[4];
	//format->bmFlags = block[5];
	memcpy(format->guidFormat, &block[5], 16);
	format->bBitsPerPixel = block[21];
	format->bDefaultFrameIndex = block[22];
	format->bAspectRatioX = block[23];
	format->bAspectRatioY = block[24];
	format->bmInterlaceFlags = block[25];
	format->bCopyProtect = block[26];

	DL_APPEND(stream_if->format_descs, format);

	return UVC_SUCCESS;
}

/** @internal
 * @brief Parse a VideoStreaming uncompressed frame block.
 * @ingroup device
 */
uvc_error_t uvc_parse_vs_frame_uncompressed(uvc_streaming_interface_t *stream_if,
	const unsigned char *block,
	size_t block_size) {
	uvc_format_desc_t *format;
	uvc_frame_desc_t *frame;

	const unsigned char *p;
	int i;

	format = stream_if->format_descs->prev;
	frame = calloc(1, sizeof(*frame));

	frame->parent = format;

	frame->bDescriptorSubtype = block[2];
	frame->bFrameIndex = block[3];
	frame->bmCapabilities = block[4];
	frame->wWidth = block[5] + (block[6] << 8);
	frame->wHeight = block[7] + (block[8] << 8);
	frame->dwMinBitRate = DW_TO_INT(&block[9]);
	frame->dwMaxBitRate = DW_TO_INT(&block[13]);
	frame->dwMaxVideoFrameBufferSize = DW_TO_INT(&block[17]);
	frame->dwDefaultFrameInterval = DW_TO_INT(&block[21]);
	frame->bFrameIntervalType = block[25];

	if (block[25] == 0) {
		frame->dwMinFrameInterval = DW_TO_INT(&block[26]);
		frame->dwMaxFrameInterval = DW_TO_INT(&block[30]);
		frame->dwFrameIntervalStep = DW_TO_INT(&block[34]);
	} else {
		frame->intervals = calloc(block[25] + 1, sizeof(frame->intervals[0]));
		p = &block[26];

		for (i = 0; i < block[25]; ++i) {
			frame->intervals[i] = DW_TO_INT(p);
			p += 4;
		}
		frame->intervals[block[25]] = 0;
	}

	DL_APPEND(format->frame_descs, frame);

	return UVC_SUCCESS;
}

uvc_error_t uvc_parse_vs(
	uvc_device_info_t *info,
	uvc_streaming_interface_t *stream_if,
	const unsigned char *block, size_t block_size)
{
	uvc_error_t ret;
	int descriptor_subtype;

	ret = UVC_SUCCESS;
	descriptor_subtype = block[2];

	ESP_LOGI(LOG_TAG, "UVC Subtype 0x%x", descriptor_subtype);

	switch (descriptor_subtype) {
		case UVC_VS_INPUT_HEADER:
			ret = uvc_parse_vs_input_header(stream_if, block, block_size);
			break;
		case UVC_VS_OUTPUT_HEADER:
			//UVC_DEBUG("unsupported descriptor subtype VS_OUTPUT_HEADER");
			break;
		case UVC_VS_STILL_IMAGE_FRAME:
			ret = uvc_parse_vs_still_image_frame(stream_if, block, block_size);
			break;
		case UVC_VS_FORMAT_UNCOMPRESSED:
			ret = uvc_parse_vs_format_uncompressed(stream_if, block, block_size);
			break;
		case UVC_VS_FORMAT_MJPEG:
			//ret = uvc_parse_vs_format_mjpeg(stream_if, block, block_size);
			break;
		case UVC_VS_FRAME_UNCOMPRESSED:
		case UVC_VS_FRAME_MJPEG:
			ret = uvc_parse_vs_frame_uncompressed(stream_if, block, block_size);
			break;
		case UVC_VS_FORMAT_MPEG2TS:
			//UVC_DEBUG("unsupported descriptor subtype VS_FORMAT_MPEG2TS");
			break;
		case UVC_VS_FORMAT_DV:
			//UVC_DEBUG("unsupported descriptor subtype VS_FORMAT_DV");
			break;
		case UVC_VS_COLORFORMAT:
			ESP_LOGW(LOG_TAG, "unsupported descriptor subtype VS_COLORFORMAT");
			break;
		case UVC_VS_FORMAT_FRAME_BASED:
			//ret = uvc_parse_vs_frame_format ( stream_if, block, block_size );
			break;
		case UVC_VS_FRAME_FRAME_BASED:
			//ret = uvc_parse_vs_frame_frame ( stream_if, block, block_size );
			break;
		case UVC_VS_FORMAT_STREAM_BASED:
			ESP_LOGW(LOG_TAG, "unsupported descriptor subtype VS_FORMAT_STREAM_BASED");
			break;
		default:
			/** @todo handle JPEG and maybe still frames or even DV... */
			ESP_LOGW(LOG_TAG, "unsupported descriptor subtype: %d",descriptor_subtype);
			break;
	}

	return ret;
}

uvc_error_t uvc_scan_streaming(
	uvc_device_info_t *info,
	int interface_idx)
{
	const libusb_interface_descriptor *if_desc;
	const unsigned char *buffer;
	size_t buffer_left, block_size;
	uvc_error_t ret, parse_ret;
	uvc_streaming_interface_t *stream_if;

	ret = UVC_SUCCESS;

	if_desc = &(info->config->interface[interface_idx].altsetting[0]);
	buffer = if_desc->extra;
	buffer_left = if_desc->extra_length;

	stream_if = calloc(1, sizeof(*stream_if));
	stream_if->parent = info;
	stream_if->bInterfaceNumber = if_desc->bInterfaceNumber;
	DL_APPEND(info->stream_ifs, stream_if);

	while (buffer_left >= 3) {
		block_size = buffer[0];
		parse_ret = uvc_parse_vs(info, stream_if, buffer, block_size);

		if (parse_ret != UVC_SUCCESS) {
			ret = parse_ret;
			break;
		}

		buffer_left -= block_size;
		buffer += block_size;
	}

	return ret;
}

uvc_error_t uvc_parse_vc_header(
	uvc_device_info_t *info,
	const unsigned char *block, size_t block_size)
{
	size_t i;
	uvc_error_t scan_ret, ret = UVC_SUCCESS;

	info->ctrl_if.bcdUVC = SW_TO_SHORT(&block[3]);

	switch (info->ctrl_if.bcdUVC) {
		case 0x0100:
			info->ctrl_if.dwClockFrequency = DW_TO_INT(block + 7);
			break;
		case 0x010a:
			info->ctrl_if.dwClockFrequency = DW_TO_INT(block + 7);
			break;
		case 0x0110:
			break;
		default:
			return UVC_ERROR_NOT_SUPPORTED;
	}

	for (i = 12; i < block_size; ++i)
	{
		scan_ret = uvc_scan_streaming(info, block[i]);
		if (scan_ret != UVC_SUCCESS) {
			ret = scan_ret;
			break;
		}
	}

	return ret;
}

/** @internal
 * Process a single VideoControl descriptor block
 * @ingroup device
 */
uvc_error_t uvc_parse_vc(
	uvc_device_info_t *info,
	const unsigned char *block, size_t block_size)
{
	int descriptor_subtype;
	uvc_error_t ret = UVC_SUCCESS;

	if (block[1] != 36)
	{ // not a CS_INTERFACE descriptor??
		return UVC_SUCCESS; // UVC_ERROR_INVALID_DEVICE;
	}

	descriptor_subtype = block[2];

	switch (descriptor_subtype) {
		case UVC_VC_HEADER:
			ret = uvc_parse_vc_header(info, block, block_size);
			break;
		case UVC_VC_INPUT_TERMINAL:
			//ret = uvc_parse_vc_input_terminal(dev, info, block, block_size);
			break;
		case UVC_VC_OUTPUT_TERMINAL:
			break;
		case UVC_VC_SELECTOR_UNIT:
			//ret = uvc_parse_vc_selector_unit(dev, info, block, block_size);
			break;
		case UVC_VC_PROCESSING_UNIT:
			//ret = uvc_parse_vc_processing_unit(dev, info, block, block_size);
			break;
		case UVC_VC_EXTENSION_UNIT:
			//ret = uvc_parse_vc_extension_unit(dev, info, block, block_size);
			break;
		default:
			ret = UVC_ERROR_INVALID_DEVICE;
	}

	return ret;
}

uvc_error_t uvc_scan_control(uvc_device_info_t *info)
{
	const libusb_interface_descriptor *if_desc;
	uvc_error_t parse_ret, ret;
	int interface_idx;
	const unsigned char *buffer;
	size_t buffer_left, block_size;

	ret = UVC_SUCCESS;
	if_desc = NULL;

	int haveTISCamera = 0;

	for (interface_idx = 0; interface_idx < info->config->bNumInterfaces; ++interface_idx) {
		if_desc = &info->config->interface[interface_idx].altsetting[0];

		if ( haveTISCamera && if_desc->bInterfaceClass == 255 && if_desc->bInterfaceSubClass == 1) // Video, Control
			break;

		if (if_desc->bInterfaceClass == 14 && if_desc->bInterfaceSubClass == 1) // Video, Control
			break;

		if_desc = NULL;
	}

	if (if_desc == NULL)
	{
		return UVC_ERROR_INVALID_DEVICE;
	}

	info->ctrl_if.bInterfaceNumber = interface_idx;
	if (if_desc->bNumEndpoints != 0)
	{
		info->ctrl_if.bEndpointAddress = if_desc->endpoint[0].bEndpointAddress;
	}

	buffer = if_desc->extra;
	buffer_left = if_desc->extra_length;

	while (buffer_left >= 3) { // parseX needs to see buf[0,2] = length,type
		block_size = buffer[0];
		parse_ret = uvc_parse_vc(info, buffer, block_size);

		if (parse_ret != UVC_SUCCESS) {
			ret = parse_ret;
			break;
		}

		buffer_left -= block_size;
		buffer += block_size;
	}

	return ret;
}
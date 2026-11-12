#include "ngp_protocol.h"

#pragma GCC diagnostic ignored "-Woverride-init"
static const struct {
	unsigned length;
	const char* name;
} ngp_message_types_info[256] = {
	[0 ... 255] = { .length = -1, .name = "Invalid Message"},
	[NGP_MESSAGE_HELLO] = { .length = sizeof(HelloMessage), .name = "Hello"},
	[NGP_MESSAGE_PASSWORD] = { .length = sizeof(PasswordMessage), .name = "Password"},
	[NGP_MESSAGE_ABSINFO] = {.length = sizeof(ABSInfoMessage), .name = "AbsInfo"},
	[NGP_MESSAGE_DEVICE] = { .length = sizeof(DeviceMessage), .name = "Device"},
	[NGP_MESSAGE_REQUEST_EVENT] = {.length = sizeof(RequestEventMessage), .name = "EventEnableRequest"},
	[NGP_MESSAGE_SETUP_END] = { .length = 1, .name = "SetupDone"},
	[NGP_MESSAGE_DATA] =  { .length = sizeof(DataMessage), .name = "Data"},
	[NGP_MESSAGE_SUCCESS] = { .length = sizeof(SuccessMessage), .name = "Success"},
	[NGP_MESSAGE_VERSION_MISMATCH] = { .length = sizeof(VersionMismatchMessage), .name = "VersionMismatch"},
	[NGP_MESSAGE_INVALID_PASSWORD] = { .length = 1, .name = "PasswordInvalid"},
	[NGP_MESSAGE_INVALID_CLIENT_SLOT] = { .length = 1, .name = "SlotInvalid"},
	[NGP_MESSAGE_INVALID] = { .length = 1, .name = "ProtocolError"},
	[NGP_MESSAGE_PASSWORD_REQUIRED] = { .length = 1, .name = "PasswordRequest"},
	[NGP_MESSAGE_SETUP_REQUIRED] = { .length = 1, .name = "SetupRequest"},
	[NGP_MESSAGE_CLIENT_SLOT_IN_USE] = { .length = 1, .name = "Occupied"},
	[NGP_MESSAGE_CLIENT_SLOTS_EXHAUSTED] = { .length = 1, .name = "Exhausted"},
	[NGP_MESSAGE_QUIT] = { .length = 1, .name = "Quit"},
};

const char* get_message_name(uint8_t msg) {
	return ngp_message_types_info[msg].name;
}

int get_size_from_command(const uint8_t* buf, unsigned len) {
	if (buf[0] == NGP_MESSAGE_PASSWORD || buf[0] == NGP_MESSAGE_DEVICE) {
		if (len > 1) {
			return ngp_message_types_info[buf[0]].length + buf[1];
		} else {
			return 0;
		}
	} else {
		return ngp_message_types_info[buf[0]].length;
	}
}

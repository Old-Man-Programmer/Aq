#pragma once

#include <stdint.h>

#define NGP_PROTOCOL_VERSION 0x05
#define NGP_INPUT_BUFFER_SIZE 1024
#define NGP_DEFAULT_PASSWORD "foobar"
#define NGP_DEFAULT_HOST "::"
#define NGP_DEFAULT_PORT "9292"

#pragma pack(1)

enum NGP_MESSAGE_TYPES {
	NGP_MESSAGE_RESERVED_UNCONN = 0x00,
	NGP_MESSAGE_HELLO = 0x01,
	NGP_MESSAGE_PASSWORD = 0x02,
	NGP_MESSAGE_ABSINFO = 0x03,
	NGP_MESSAGE_DEVICE = 0x04,
	NGP_MESSAGE_SETUP_END = 0x05,
	NGP_MESSAGE_REQUEST_EVENT = 0x06,
	NGP_MESSAGE_DATA = 0x10,
	NGP_MESSAGE_SUCCESS = 0xF0,
	NGP_MESSAGE_VERSION_MISMATCH = 0xF1,
	NGP_MESSAGE_INVALID_PASSWORD = 0xF2,
	NGP_MESSAGE_INVALID_CLIENT_SLOT = 0xF3,
	NGP_MESSAGE_INVALID = 0xF4,
	NGP_MESSAGE_PASSWORD_REQUIRED = 0xF5,
	NGP_MESSAGE_SETUP_REQUIRED = 0xF6,
	NGP_MESSAGE_CLIENT_SLOT_IN_USE = 0xF7,
	NGP_MESSAGE_CLIENT_SLOTS_EXHAUSTED = 0xF8,
	NGP_MESSAGE_QUIT = 0xF9
};

typedef struct {
	uint8_t msg_type;
	uint8_t version;
	uint8_t slot;
} HelloMessage;

typedef struct {
	uint8_t msg_type;
	uint8_t length;
	char password[];
} PasswordMessage;

struct input_absinfo {
	int32_t value;
	int32_t minimum;
	int32_t maximum;
	int32_t fuzz;
	int32_t flat;
	int32_t resolution;
};

typedef struct {
	uint8_t msg_type;
	uint8_t axis;
	struct input_absinfo info;
} ABSInfoMessage;

typedef struct {
	uint8_t msg_type;
	uint16_t type;
	uint16_t code;
} RequestEventMessage;

struct input_id {
        uint16_t bustype;
        uint16_t vendor;
        uint16_t product;
        uint16_t version;
};

typedef struct {
	uint8_t msg_type;
	uint8_t length;
	struct input_id id;
	char name[];
} DeviceMessage;

typedef struct {
	uint8_t msg_type;
	uint16_t type;
	uint16_t code;
	int32_t value;
} DataMessage;

typedef struct {
	uint8_t msg_type;
	uint8_t version;
} VersionMismatchMessage;

typedef struct {
	uint8_t msg_type;
	uint8_t slot;
} SuccessMessage;

const char* get_message_name(uint8_t msg);
int get_size_from_command(const uint8_t* buf, unsigned len);

#pragma pack(0)

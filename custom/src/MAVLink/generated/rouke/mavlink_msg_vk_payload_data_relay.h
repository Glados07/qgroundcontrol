#pragma once
// MESSAGE VK_PAYLOAD_DATA_RELAY PACKING

#define MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY 53022


typedef struct __mavlink_vk_payload_data_relay_t {
 uint16_t payload_addr; /*<  payload address code*/
 uint8_t reserve; /*<  reserve*/
 uint8_t data_len; /*<  data field length in bytes*/
 uint8_t data[128]; /*<  data to relay*/
} mavlink_vk_payload_data_relay_t;

#define MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN 132
#define MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN 132
#define MAVLINK_MSG_ID_53022_LEN 132
#define MAVLINK_MSG_ID_53022_MIN_LEN 132

#define MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC 37
#define MAVLINK_MSG_ID_53022_CRC 37

#define MAVLINK_MSG_VK_PAYLOAD_DATA_RELAY_FIELD_DATA_LEN 128

#if MAVLINK_COMMAND_24BIT
#define MAVLINK_MESSAGE_INFO_VK_PAYLOAD_DATA_RELAY { \
    53022, \
    "VK_PAYLOAD_DATA_RELAY", \
    4, \
    {  { "payload_addr", NULL, MAVLINK_TYPE_UINT16_T, 0, 0, offsetof(mavlink_vk_payload_data_relay_t, payload_addr) }, \
         { "reserve", NULL, MAVLINK_TYPE_UINT8_T, 0, 2, offsetof(mavlink_vk_payload_data_relay_t, reserve) }, \
         { "data_len", NULL, MAVLINK_TYPE_UINT8_T, 0, 3, offsetof(mavlink_vk_payload_data_relay_t, data_len) }, \
         { "data", NULL, MAVLINK_TYPE_UINT8_T, 128, 4, offsetof(mavlink_vk_payload_data_relay_t, data) }, \
         } \
}
#else
#define MAVLINK_MESSAGE_INFO_VK_PAYLOAD_DATA_RELAY { \
    "VK_PAYLOAD_DATA_RELAY", \
    4, \
    {  { "payload_addr", NULL, MAVLINK_TYPE_UINT16_T, 0, 0, offsetof(mavlink_vk_payload_data_relay_t, payload_addr) }, \
         { "reserve", NULL, MAVLINK_TYPE_UINT8_T, 0, 2, offsetof(mavlink_vk_payload_data_relay_t, reserve) }, \
         { "data_len", NULL, MAVLINK_TYPE_UINT8_T, 0, 3, offsetof(mavlink_vk_payload_data_relay_t, data_len) }, \
         { "data", NULL, MAVLINK_TYPE_UINT8_T, 128, 4, offsetof(mavlink_vk_payload_data_relay_t, data) }, \
         } \
}
#endif

/**
 * @brief Pack a vk_payload_data_relay message
 * @param system_id ID of this system
 * @param component_id ID of this component (e.g. 200 for IMU)
 * @param msg The MAVLink message to compress the data into
 *
 * @param payload_addr  payload address code
 * @param reserve  reserve
 * @param data_len  data field length in bytes
 * @param data  data to relay
 * @return length of the message in bytes (excluding serial stream start sign)
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_pack(uint8_t system_id, uint8_t component_id, mavlink_message_t* msg,
                               uint16_t payload_addr, uint8_t reserve, uint8_t data_len, const uint8_t *data)
{
#if MAVLINK_NEED_BYTE_SWAP || !MAVLINK_ALIGNED_FIELDS
    char buf[MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN];
    _mav_put_uint16_t(buf, 0, payload_addr);
    _mav_put_uint8_t(buf, 2, reserve);
    _mav_put_uint8_t(buf, 3, data_len);
    _mav_put_uint8_t_array(buf, 4, data, 128);
        memcpy(_MAV_PAYLOAD_NON_CONST(msg), buf, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
#else
    mavlink_vk_payload_data_relay_t packet;
    packet.payload_addr = payload_addr;
    packet.reserve = reserve;
    packet.data_len = data_len;
    mav_array_memcpy(packet.data, data, sizeof(uint8_t)*128);
        memcpy(_MAV_PAYLOAD_NON_CONST(msg), &packet, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
#endif

    msg->msgid = MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY;
    return mavlink_finalize_message(msg, system_id, component_id, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
}

/**
 * @brief Pack a vk_payload_data_relay message
 * @param system_id ID of this system
 * @param component_id ID of this component (e.g. 200 for IMU)
 * @param status MAVLink status structure
 * @param msg The MAVLink message to compress the data into
 *
 * @param payload_addr  payload address code
 * @param reserve  reserve
 * @param data_len  data field length in bytes
 * @param data  data to relay
 * @return length of the message in bytes (excluding serial stream start sign)
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_pack_status(uint8_t system_id, uint8_t component_id, mavlink_status_t *_status, mavlink_message_t* msg,
                               uint16_t payload_addr, uint8_t reserve, uint8_t data_len, const uint8_t *data)
{
#if MAVLINK_NEED_BYTE_SWAP || !MAVLINK_ALIGNED_FIELDS
    char buf[MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN];
    _mav_put_uint16_t(buf, 0, payload_addr);
    _mav_put_uint8_t(buf, 2, reserve);
    _mav_put_uint8_t(buf, 3, data_len);
    _mav_put_uint8_t_array(buf, 4, data, 128);
        memcpy(_MAV_PAYLOAD_NON_CONST(msg), buf, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
#else
    mavlink_vk_payload_data_relay_t packet;
    packet.payload_addr = payload_addr;
    packet.reserve = reserve;
    packet.data_len = data_len;
    mav_array_memcpy(packet.data, data, sizeof(uint8_t)*128);
        memcpy(_MAV_PAYLOAD_NON_CONST(msg), &packet, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
#endif

    msg->msgid = MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY;
#if MAVLINK_CRC_EXTRA
    return mavlink_finalize_message_buffer(msg, system_id, component_id, _status, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
#else
    return mavlink_finalize_message_buffer(msg, system_id, component_id, _status, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
#endif
}

/**
 * @brief Pack a vk_payload_data_relay message on a channel
 * @param system_id ID of this system
 * @param component_id ID of this component (e.g. 200 for IMU)
 * @param chan The MAVLink channel this message will be sent over
 * @param msg The MAVLink message to compress the data into
 * @param payload_addr  payload address code
 * @param reserve  reserve
 * @param data_len  data field length in bytes
 * @param data  data to relay
 * @return length of the message in bytes (excluding serial stream start sign)
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_pack_chan(uint8_t system_id, uint8_t component_id, uint8_t chan,
                               mavlink_message_t* msg,
                                   uint16_t payload_addr,uint8_t reserve,uint8_t data_len,const uint8_t *data)
{
#if MAVLINK_NEED_BYTE_SWAP || !MAVLINK_ALIGNED_FIELDS
    char buf[MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN];
    _mav_put_uint16_t(buf, 0, payload_addr);
    _mav_put_uint8_t(buf, 2, reserve);
    _mav_put_uint8_t(buf, 3, data_len);
    _mav_put_uint8_t_array(buf, 4, data, 128);
        memcpy(_MAV_PAYLOAD_NON_CONST(msg), buf, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
#else
    mavlink_vk_payload_data_relay_t packet;
    packet.payload_addr = payload_addr;
    packet.reserve = reserve;
    packet.data_len = data_len;
    mav_array_memcpy(packet.data, data, sizeof(uint8_t)*128);
        memcpy(_MAV_PAYLOAD_NON_CONST(msg), &packet, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
#endif

    msg->msgid = MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY;
    return mavlink_finalize_message_chan(msg, system_id, component_id, chan, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
}

/**
 * @brief Encode a vk_payload_data_relay struct
 *
 * @param system_id ID of this system
 * @param component_id ID of this component (e.g. 200 for IMU)
 * @param msg The MAVLink message to compress the data into
 * @param vk_payload_data_relay C-struct to read the message contents from
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_encode(uint8_t system_id, uint8_t component_id, mavlink_message_t* msg, const mavlink_vk_payload_data_relay_t* vk_payload_data_relay)
{
    return mavlink_msg_vk_payload_data_relay_pack(system_id, component_id, msg, vk_payload_data_relay->payload_addr, vk_payload_data_relay->reserve, vk_payload_data_relay->data_len, vk_payload_data_relay->data);
}

/**
 * @brief Encode a vk_payload_data_relay struct on a channel
 *
 * @param system_id ID of this system
 * @param component_id ID of this component (e.g. 200 for IMU)
 * @param chan The MAVLink channel this message will be sent over
 * @param msg The MAVLink message to compress the data into
 * @param vk_payload_data_relay C-struct to read the message contents from
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_encode_chan(uint8_t system_id, uint8_t component_id, uint8_t chan, mavlink_message_t* msg, const mavlink_vk_payload_data_relay_t* vk_payload_data_relay)
{
    return mavlink_msg_vk_payload_data_relay_pack_chan(system_id, component_id, chan, msg, vk_payload_data_relay->payload_addr, vk_payload_data_relay->reserve, vk_payload_data_relay->data_len, vk_payload_data_relay->data);
}

/**
 * @brief Encode a vk_payload_data_relay struct with provided status structure
 *
 * @param system_id ID of this system
 * @param component_id ID of this component (e.g. 200 for IMU)
 * @param status MAVLink status structure
 * @param msg The MAVLink message to compress the data into
 * @param vk_payload_data_relay C-struct to read the message contents from
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_encode_status(uint8_t system_id, uint8_t component_id, mavlink_status_t* _status, mavlink_message_t* msg, const mavlink_vk_payload_data_relay_t* vk_payload_data_relay)
{
    return mavlink_msg_vk_payload_data_relay_pack_status(system_id, component_id, _status, msg,  vk_payload_data_relay->payload_addr, vk_payload_data_relay->reserve, vk_payload_data_relay->data_len, vk_payload_data_relay->data);
}

/**
 * @brief Send a vk_payload_data_relay message
 * @param chan MAVLink channel to send the message
 *
 * @param payload_addr  payload address code
 * @param reserve  reserve
 * @param data_len  data field length in bytes
 * @param data  data to relay
 */
#ifdef MAVLINK_USE_CONVENIENCE_FUNCTIONS

static inline void mavlink_msg_vk_payload_data_relay_send(mavlink_channel_t chan, uint16_t payload_addr, uint8_t reserve, uint8_t data_len, const uint8_t *data)
{
#if MAVLINK_NEED_BYTE_SWAP || !MAVLINK_ALIGNED_FIELDS
    char buf[MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN];
    _mav_put_uint16_t(buf, 0, payload_addr);
    _mav_put_uint8_t(buf, 2, reserve);
    _mav_put_uint8_t(buf, 3, data_len);
    _mav_put_uint8_t_array(buf, 4, data, 128);
    _mav_finalize_message_chan_send(chan, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY, buf, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
#else
    mavlink_vk_payload_data_relay_t packet;
    packet.payload_addr = payload_addr;
    packet.reserve = reserve;
    packet.data_len = data_len;
    mav_array_memcpy(packet.data, data, sizeof(uint8_t)*128);
    _mav_finalize_message_chan_send(chan, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY, (const char *)&packet, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
#endif
}

/**
 * @brief Send a vk_payload_data_relay message
 * @param chan MAVLink channel to send the message
 * @param struct The MAVLink struct to serialize
 */
static inline void mavlink_msg_vk_payload_data_relay_send_struct(mavlink_channel_t chan, const mavlink_vk_payload_data_relay_t* vk_payload_data_relay)
{
#if MAVLINK_NEED_BYTE_SWAP || !MAVLINK_ALIGNED_FIELDS
    mavlink_msg_vk_payload_data_relay_send(chan, vk_payload_data_relay->payload_addr, vk_payload_data_relay->reserve, vk_payload_data_relay->data_len, vk_payload_data_relay->data);
#else
    _mav_finalize_message_chan_send(chan, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY, (const char *)vk_payload_data_relay, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
#endif
}

#if MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN <= MAVLINK_MAX_PAYLOAD_LEN
/*
  This variant of _send() can be used to save stack space by re-using
  memory from the receive buffer.  The caller provides a
  mavlink_message_t which is the size of a full mavlink message. This
  is usually the receive buffer for the channel, and allows a reply to an
  incoming message with minimum stack space usage.
 */
static inline void mavlink_msg_vk_payload_data_relay_send_buf(mavlink_message_t *msgbuf, mavlink_channel_t chan,  uint16_t payload_addr, uint8_t reserve, uint8_t data_len, const uint8_t *data)
{
#if MAVLINK_NEED_BYTE_SWAP || !MAVLINK_ALIGNED_FIELDS
    char *buf = (char *)msgbuf;
    _mav_put_uint16_t(buf, 0, payload_addr);
    _mav_put_uint8_t(buf, 2, reserve);
    _mav_put_uint8_t(buf, 3, data_len);
    _mav_put_uint8_t_array(buf, 4, data, 128);
    _mav_finalize_message_chan_send(chan, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY, buf, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
#else
    mavlink_vk_payload_data_relay_t *packet = (mavlink_vk_payload_data_relay_t *)msgbuf;
    packet->payload_addr = payload_addr;
    packet->reserve = reserve;
    packet->data_len = data_len;
    mav_array_memcpy(packet->data, data, sizeof(uint8_t)*128);
    _mav_finalize_message_chan_send(chan, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY, (const char *)packet, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC);
#endif
}
#endif

#endif

// MESSAGE VK_PAYLOAD_DATA_RELAY UNPACKING


/**
 * @brief Get field payload_addr from vk_payload_data_relay message
 *
 * @return  payload address code
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_get_payload_addr(const mavlink_message_t* msg)
{
    return _MAV_RETURN_uint16_t(msg,  0);
}

/**
 * @brief Get field reserve from vk_payload_data_relay message
 *
 * @return  reserve
 */
static inline uint8_t mavlink_msg_vk_payload_data_relay_get_reserve(const mavlink_message_t* msg)
{
    return _MAV_RETURN_uint8_t(msg,  2);
}

/**
 * @brief Get field data_len from vk_payload_data_relay message
 *
 * @return  data field length in bytes
 */
static inline uint8_t mavlink_msg_vk_payload_data_relay_get_data_len(const mavlink_message_t* msg)
{
    return _MAV_RETURN_uint8_t(msg,  3);
}

/**
 * @brief Get field data from vk_payload_data_relay message
 *
 * @return  data to relay
 */
static inline uint16_t mavlink_msg_vk_payload_data_relay_get_data(const mavlink_message_t* msg, uint8_t *data)
{
    return _MAV_RETURN_uint8_t_array(msg, data, 128,  4);
}

/**
 * @brief Decode a vk_payload_data_relay message into a struct
 *
 * @param msg The message to decode
 * @param vk_payload_data_relay C-struct to decode the message contents into
 */
static inline void mavlink_msg_vk_payload_data_relay_decode(const mavlink_message_t* msg, mavlink_vk_payload_data_relay_t* vk_payload_data_relay)
{
#if MAVLINK_NEED_BYTE_SWAP || !MAVLINK_ALIGNED_FIELDS
    vk_payload_data_relay->payload_addr = mavlink_msg_vk_payload_data_relay_get_payload_addr(msg);
    vk_payload_data_relay->reserve = mavlink_msg_vk_payload_data_relay_get_reserve(msg);
    vk_payload_data_relay->data_len = mavlink_msg_vk_payload_data_relay_get_data_len(msg);
    mavlink_msg_vk_payload_data_relay_get_data(msg, vk_payload_data_relay->data);
#else
        uint8_t len = msg->len < MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN? msg->len : MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN;
        memset(vk_payload_data_relay, 0, MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN);
    memcpy(vk_payload_data_relay, _MAV_PAYLOAD(msg), len);
#endif
}

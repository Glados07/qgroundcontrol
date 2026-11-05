// Exercise the real QGC wrapper and generated dialect, including byte parsing.
#include "MAVLinkLib.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

mavlink_status_t m_mavlink_status[MAVLINK_COMM_NUM_BUFFERS]{};
mavlink_status_t* mavlink_get_channel_status(uint8_t channel)
{
    return &m_mavlink_status[channel];
}

namespace {
void require(bool condition, const char* description)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", description);
        std::exit(1);
    }
}

bool parse(const uint8_t* bytes, uint16_t size, mavlink_message_t& result)
{
    mavlink_reset_channel_status(MAVLINK_COMM_1);
    mavlink_status_t status{};
    bool complete = false;
    for (uint16_t i = 0; i < size; ++i) {
        complete = mavlink_parse_char(MAVLINK_COMM_1, bytes[i], &result, &status) || complete;
    }
    return complete;
}

void roundTrip(uint8_t count)
{
    std::array<uint8_t, 128> data{};
    for (uint16_t i = 0; i < count; ++i) {
        data[i] = static_cast<uint8_t>(i + 1);
    }
    mavlink_message_t sent{};
    const auto packed = mavlink_msg_vk_payload_data_relay_pack_chan(
        255, 190, MAVLINK_COMM_0, &sent, 0x1234, 0, count, data.data());
    require(packed != 0 && sent.magic == MAVLINK_STX, "MAVLink 2 relay packs");
    require(sent.msgid == 53022, "24-bit message ID retained");
    require(sent.len == (count ? count + 4 : 2), "trailing zero truncation follows MAVLink 2");
    std::array<uint8_t, MAVLINK_MAX_PACKET_LEN> bytes{};
    const auto size = mavlink_msg_to_send_buffer(bytes.data(), &sent);
    require(bytes[10] == 0x34 && bytes[11] == 0x12, "address uses little-endian wire order");
    mavlink_message_t received{};
    require(parse(bytes.data(), size, received), "native parser recognizes relay CRC entry");
    mavlink_vk_payload_data_relay_t decoded{};
    mavlink_msg_vk_payload_data_relay_decode(&received, &decoded);
    require(decoded.payload_addr == 0x1234 && decoded.reserve == 0 && decoded.data_len == count,
            "relay scalar fields round trip");
    require(std::memcmp(decoded.data, data.data(), data.size()) == 0,
            "data round trip includes zero-filled truncated tail");
    bytes[10] ^= 0x01;
    require(!parse(bytes.data(), size, received), "corrupt wire payload is rejected");
}

void gripperRoundTrip(GRIPPER_ACTIONS action, float expectedWireAction)
{
    mavlink_message_t sent{};
    mavlink_msg_command_long_pack_chan(255, 190, MAVLINK_COMM_0, &sent,
                                      1, 1, MAV_CMD_DO_GRIPPER, 0,
                                      1, static_cast<float>(action), 0, 0, 0, 0, 0);
    std::array<uint8_t, MAVLINK_MAX_PACKET_LEN> bytes{};
    const auto size = mavlink_msg_to_send_buffer(bytes.data(), &sent);
    mavlink_message_t received{};
    require(parse(bytes.data(), size, received)
                && received.msgid == MAVLINK_MSG_ID_COMMAND_LONG,
            "native parser recognizes gripper command");
    mavlink_command_long_t decoded{};
    mavlink_msg_command_long_decode(&received, &decoded);
    require(decoded.command == MAV_CMD_DO_GRIPPER
                && decoded.target_system == 1 && decoded.target_component == 1
                && decoded.param1 == 1 && decoded.param2 == expectedWireAction,
            "legacy gripper action retains its wire value");
}
} // namespace

int main()
{
    // Use the same legacy names as QGCMAVLink, Vehicle and Joystick. The team
    // dialect renamed them; a wrong alias would compile but change the action.
    static_assert(GRIPPER_ACTION_RELEASE == 0);
    static_assert(GRIPPER_ACTION_RELEASE == GRIPPER_ACTION_OPEN);
    static_assert(GRIPPER_ACTION_GRAB == 1);
    static_assert(GRIPPER_ACTION_GRAB == GRIPPER_ACTION_CLOSE);
    static_assert(GRIPPER_ACTION_STOP == 2);
    static_assert(GRIPPER_ACTIONS_ENUM_END > GRIPPER_ACTION_STOP);
    gripperRoundTrip(GRIPPER_ACTION_RELEASE, 0);
    gripperRoundTrip(GRIPPER_ACTION_GRAB, 1);
    gripperRoundTrip(GRIPPER_ACTION_STOP, 2);

    static_assert(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY == 53022);
    static_assert(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_LEN == 132);
    static_assert(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_MIN_LEN == 132);
    static_assert(MAVLINK_MSG_ID_VK_PAYLOAD_DATA_RELAY_CRC == 37);
    static_assert(MAVLINK_MSG_VK_PAYLOAD_DATA_RELAY_FIELD_DATA_LEN == 128);
    const auto* entry = mavlink_get_msg_entry(53022);
    require(entry && entry->crc_extra == 37 && entry->max_msg_len == 132,
            "global CRC table contains relay metadata");
    const auto* byId = mavlink_get_message_info_by_id(53022);
    const auto* byName = mavlink_get_message_info_by_name("VK_PAYLOAD_DATA_RELAY");
    require(byId && byName && byId == byName && byId->num_fields == 4,
            "inspector resolves message by ID and name");
    roundTrip(0);
    roundTrip(7);
    roundTrip(128);

    mavlink_message_t heartbeat{};
    mavlink_msg_heartbeat_pack_chan(1, 1, MAVLINK_COMM_0, &heartbeat,
                                  MAV_TYPE_QUADROTOR, MAV_AUTOPILOT_PX4, 0, 0, MAV_STATE_ACTIVE);
    std::array<uint8_t, MAVLINK_MAX_PACKET_LEN> bytes{};
    const auto size = mavlink_msg_to_send_buffer(bytes.data(), &heartbeat);
    mavlink_message_t received{};
    require(parse(bytes.data(), size, received) && received.msgid == MAVLINK_MSG_ID_HEARTBEAT,
            "native flight messages still parse");

    mavlink_get_channel_status(MAVLINK_COMM_0)->flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
    std::array<uint8_t, 128> data{};
    mavlink_status_t privateTx{};
    require(mavlink_msg_vk_payload_data_relay_pack_status(255, 190, &privateTx,
                &received, 1, 0, 1, data.data()) != 0
                && received.magic == MAVLINK_STX && received.msgid == 53022,
            "private TX status keeps relay in MAVLink 2 when QGC channel is MAVLink 1");
    const auto privateSize = mavlink_msg_to_send_buffer(bytes.data(), &received);
    mavlink_message_t privateBuffer{};
    mavlink_status_t privateRx{};
    mavlink_status_t reportedStatus{};
    uint8_t result = MAVLINK_FRAMING_INCOMPLETE;
    for (uint16_t i = 0; i < privateSize; ++i) {
        result = mavlink_frame_char_buffer(&privateBuffer, &privateRx, bytes[i],
                                          &received, &reportedStatus);
    }
    require(result == MAVLINK_FRAMING_OK && received.msgid == 53022,
            "private receive parser recognizes relay");
    require(mavlink_get_channel_status(MAVLINK_COMM_0)->flags & MAVLINK_STATUS_FLAG_OUT_MAVLINK1,
            "private relay status leaves QGC channel flags unchanged");
    std::puts("Rouke MAVLink: legacy gripper actions, CRC table, inspector, short/full frames, bad CRC, heartbeat and private MAVLink 2 status checks passed.");
}

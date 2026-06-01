/* Host unit test: Meshtastic proto byte-compat for the channel advertise path
 * (doc 13 §7, step 5a). The app-facing wire format MUST match upstream
 * meshtastic/protobufs (Channel / ChannelSettings / FromRadio.channel), so this
 * decodes a HAND-CONSTRUCTED upstream-format byte buffer (not one our own
 * encoder produced) and asserts our generated nanopb code reads every field
 * correctly. A regen/field-number drift would put data in the wrong field or
 * fail the decode here. Also checks an encode->decode round-trip + that the
 * encoder emits the FromRadio.channel tag (field 10). Run: pio test -e native -f test_proto */
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include "waymesh_mesh.pb.h"

void setUp(void) {}
void tearDown(void) {}

/* Upstream-format bytes for:
 *   FromRadio { channel = Channel {
 *       index = 1,
 *       settings = ChannelSettings { psk = {0x01}, name = "LF" },
 *       role = 1 (PRIMARY) } }
 * Field tags are hardcoded to the UPSTREAM numbers, so if our .proto drifts the
 * decode mismaps and the asserts fail. */
static const uint8_t kUpstreamFromRadioChannel[] = {
    0x52, 0x0D,                   /* FromRadio.channel: field 10, LEN, 13 bytes */
      0x08, 0x01,                 /*   Channel.index = 1   (field 1, varint)    */
      0x12, 0x07,                 /*   Channel.settings: field 2, LEN, 7 bytes  */
        0x12, 0x01, 0x01,         /*     ChannelSettings.psk = {0x01} (field 2) */
        0x1A, 0x02, 0x4C, 0x46,   /*     ChannelSettings.name = "LF"  (field 3) */
      0x18, 0x01,                 /*   Channel.role = 1    (field 3, varint)    */
};

/* Our generated decoder reads the upstream-format Channel advertise frame. */
static void test_decode_upstream_channel(void)
{
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    pb_istream_t is = pb_istream_from_buffer(kUpstreamFromRadioChannel,
                                             sizeof(kUpstreamFromRadioChannel));
    TEST_ASSERT_TRUE(pb_decode(&is, meshtastic_FromRadio_fields, &fr));

    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_channel_tag,
                           fr.which_payload_variant);
    const meshtastic_Channel *ch = &fr.payload_variant.channel;
    TEST_ASSERT_EQUAL_UINT32(1, ch->index);
    TEST_ASSERT_EQUAL_UINT32(1, ch->role);
    TEST_ASSERT_TRUE(ch->has_settings);
    TEST_ASSERT_EQUAL_UINT(1, ch->settings.psk.size);
    TEST_ASSERT_EQUAL_HEX8(0x01, ch->settings.psk.bytes[0]);
    TEST_ASSERT_EQUAL_STRING("LF", ch->settings.name);
}

/* The field NUMBERS are pinned: encoding a FromRadio{channel} must put the
 * channel under field 10 (tag byte 0x52) with id=0 omitted (proto3 default). */
static void test_encode_channel_field_numbers(void)
{
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_channel_tag;
    meshtastic_Channel *ch = &fr.payload_variant.channel;
    ch->index = 3;
    ch->role = 2; /* SECONDARY */
    ch->has_settings = true;
    ch->settings.psk.size = 1;
    ch->settings.psk.bytes[0] = 0x01;
    snprintf(ch->settings.name, sizeof(ch->settings.name), "LongFast");

    uint8_t buf[meshtastic_FromRadio_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE(pb_encode(&os, meshtastic_FromRadio_fields, &fr));
    TEST_ASSERT_GREATER_THAN_UINT(0, os.bytes_written);
    /* first emitted field is the oneof channel = field 10, wiretype 2 -> 0x52 */
    TEST_ASSERT_EQUAL_HEX8(0x52, buf[0]);
}

/* Full round-trip: build -> encode -> decode preserves every field. */
static void test_channel_roundtrip(void)
{
    meshtastic_FromRadio fr = meshtastic_FromRadio_init_zero;
    fr.which_payload_variant = meshtastic_FromRadio_channel_tag;
    meshtastic_Channel *ch = &fr.payload_variant.channel;
    ch->index = 5;
    ch->role = 2;
    ch->has_settings = true;
    uint8_t key[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    memcpy(ch->settings.psk.bytes, key, sizeof(key));
    ch->settings.psk.size = sizeof(key);
    snprintf(ch->settings.name, sizeof(ch->settings.name), "team-blue");

    uint8_t buf[meshtastic_FromRadio_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE(pb_encode(&os, meshtastic_FromRadio_fields, &fr));

    meshtastic_FromRadio got = meshtastic_FromRadio_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE(pb_decode(&is, meshtastic_FromRadio_fields, &got));
    TEST_ASSERT_EQUAL_UINT(meshtastic_FromRadio_channel_tag,
                           got.which_payload_variant);
    TEST_ASSERT_EQUAL_UINT32(5, got.payload_variant.channel.index);
    TEST_ASSERT_EQUAL_UINT32(2, got.payload_variant.channel.role);
    TEST_ASSERT_EQUAL_UINT(16, got.payload_variant.channel.settings.psk.size);
    TEST_ASSERT_EQUAL_MEMORY(key, got.payload_variant.channel.settings.psk.bytes,
                             16);
    TEST_ASSERT_EQUAL_STRING("team-blue",
                             got.payload_variant.channel.settings.name);
}

/* Upstream-format AdminMessage{ set_channel = Channel{index=2},
 *                               session_passkey = {0xDE,0xAD} }.
 * set_channel is field 33 (the high tag a naive impl gets wrong): key =
 * (33<<3)|2 = 266 -> varint 8A 02. session_passkey is the top-level field 101:
 * key = (101<<3)|2 = 810 -> varint AA 06. Decoding this with our generated code
 * locks both numbers against upstream admin.proto. */
static const uint8_t kUpstreamAdminSetChannel[] = {
    0x8A, 0x02, 0x02,       /* set_channel (field 33, LEN, 2 bytes)        */
      0x08, 0x02,           /*   Channel.index = 2                          */
    0xAA, 0x06, 0x02,       /* session_passkey (field 101, LEN, 2 bytes)   */
      0xDE, 0xAD,
};

static void test_decode_upstream_admin(void)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    pb_istream_t is = pb_istream_from_buffer(kUpstreamAdminSetChannel,
                                             sizeof(kUpstreamAdminSetChannel));
    TEST_ASSERT_TRUE(pb_decode(&is, meshtastic_AdminMessage_fields, &am));
    TEST_ASSERT_EQUAL_UINT(meshtastic_AdminMessage_set_channel_tag,
                           am.which_payload_variant);
    TEST_ASSERT_EQUAL_UINT32(2, am.payload_variant.set_channel.index);
    TEST_ASSERT_EQUAL_UINT(2, am.session_passkey.size);
    TEST_ASSERT_EQUAL_HEX8(0xDE, am.session_passkey.bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAD, am.session_passkey.bytes[1]);
}

/* get_channel_request (field 1) + set_channel (field 33) round-trip. */
static void test_admin_roundtrip(void)
{
    meshtastic_AdminMessage am = meshtastic_AdminMessage_init_zero;
    am.which_payload_variant = meshtastic_AdminMessage_set_channel_tag;
    am.payload_variant.set_channel.index = 1;
    am.payload_variant.set_channel.role = 1;  /* PRIMARY */
    am.payload_variant.set_channel.has_settings = true;
    am.payload_variant.set_channel.settings.psk.size = 1;
    am.payload_variant.set_channel.settings.psk.bytes[0] = 0x01;
    snprintf(am.payload_variant.set_channel.settings.name,
             sizeof(am.payload_variant.set_channel.settings.name), "Gold");
    am.session_passkey.size = 8;
    for (int i = 0; i < 8; i++) am.session_passkey.bytes[i] = (uint8_t)(i + 1);

    uint8_t buf[meshtastic_AdminMessage_size];
    pb_ostream_t os = pb_ostream_from_buffer(buf, sizeof(buf));
    TEST_ASSERT_TRUE(pb_encode(&os, meshtastic_AdminMessage_fields, &am));

    meshtastic_AdminMessage got = meshtastic_AdminMessage_init_zero;
    pb_istream_t is = pb_istream_from_buffer(buf, os.bytes_written);
    TEST_ASSERT_TRUE(pb_decode(&is, meshtastic_AdminMessage_fields, &got));
    TEST_ASSERT_EQUAL_UINT(meshtastic_AdminMessage_set_channel_tag,
                           got.which_payload_variant);
    TEST_ASSERT_EQUAL_UINT32(1, got.payload_variant.set_channel.index);
    TEST_ASSERT_EQUAL_STRING("Gold",
                             got.payload_variant.set_channel.settings.name);
    TEST_ASSERT_EQUAL_UINT(8, got.session_passkey.size);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_decode_upstream_channel);
    RUN_TEST(test_encode_channel_field_numbers);
    RUN_TEST(test_channel_roundtrip);
    RUN_TEST(test_decode_upstream_admin);
    RUN_TEST(test_admin_roundtrip);
    return UNITY_END();
}

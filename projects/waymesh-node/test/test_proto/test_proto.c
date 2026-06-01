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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_decode_upstream_channel);
    RUN_TEST(test_encode_channel_field_numbers);
    RUN_TEST(test_channel_roundtrip);
    return UNITY_END();
}

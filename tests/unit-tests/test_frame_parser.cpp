#include "CppUTest/TestHarness.h"
#include <string.h>

extern "C"
{
#include "frame_parser.h"
}

TEST_GROUP(FrameParser)
{
    command_frame_t frame;

    void setup() override
    {
        frame = command_frame_t(); // zero-initializes every field, including state = SOF (0)
    }
};

/* --- frame_parser_feed --- */

// A NULL frame pointer should be rejected rather than dereferenced.
TEST(FrameParser, FeedRejectsNullFrame)
{
    LONGS_EQUAL(FRAME_ERROR, frame_parser_feed(NULL, 0xA5));
}

// Bytes that aren't the 0xA5 SOF marker should be ignored (FRAME_PENDING)
// and leave the parser waiting in the SOF state, rather than desyncing.
TEST(FrameParser, FeedStaysPendingUntilSofByteReceived)
{
    LONGS_EQUAL(FRAME_PENDING, frame_parser_feed(&frame, 0x00));
    LONGS_EQUAL(SOF, frame.state);

    LONGS_EQUAL(FRAME_PENDING, frame_parser_feed(&frame, 0xFF));
    LONGS_EQUAL(SOF, frame.state);

    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0xA5));
    LONGS_EQUAL(OPCODE, frame.state);
}

// A length byte greater than RX_MAX_PAYLOAD must be rejected with FRAME_ERROR
// and reset back to SOF - this is the guard that stops a bogus length from
// driving PAYLOAD state into writing past the fixed-size payload[] array.
TEST(FrameParser, FeedRejectsLengthExceedingMaxPayloadAndResyncs)
{
    frame_parser_feed(&frame, 0xA5);
    frame_parser_feed(&frame, 0x10);

    LONGS_EQUAL(FRAME_ERROR, frame_parser_feed(&frame, RX_MAX_PAYLOAD + 1));
    LONGS_EQUAL(SOF, frame.state);

    // parser should cleanly accept a new frame after the error
    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0xA5));
    LONGS_EQUAL(OPCODE, frame.state);
}

// A length exactly at RX_MAX_PAYLOAD is the boundary case and must be accepted,
// not rejected - pairs with the exceeding-length test above.
TEST(FrameParser, FeedAcceptsLengthAtMaxPayloadBoundary)
{
    frame_parser_feed(&frame, 0xA5);
    frame_parser_feed(&frame, 0x10);

    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, RX_MAX_PAYLOAD));
    LONGS_EQUAL(PAYLOAD, frame.state);
}

// A zero-length frame has no payload bytes to consume, so the parser should
// skip PAYLOAD entirely and go straight from LENGTH to CRC_LOW.
TEST(FrameParser, FeedHandlesZeroLengthPayload)
{
    frame_parser_feed(&frame, 0xA5);
    frame_parser_feed(&frame, 0x10);

    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0));
    LONGS_EQUAL(CRC_LOW, frame.state);

    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0x34));
    LONGS_EQUAL(FRAME_READY, frame_parser_feed(&frame, 0x12));
}

// End-to-end happy path: a full valid frame should reach FRAME_READY with
// every field (opcode, length, payload bytes, crc_low, crc_high) populated
// correctly - this is the test that proves the module actually works, not
// just that individual transitions don't crash.
TEST(FrameParser, FeedParsesCompleteFrameHappyPath)
{
    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0xA5)); // SOF
    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0x10)); // OPCODE
    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 3));    // LENGTH

    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0xAA)); // PAYLOAD[0]
    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0xBB)); // PAYLOAD[1]
    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0xCC)); // PAYLOAD[2]

    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0x34)); // CRC_LOW
    LONGS_EQUAL(FRAME_READY, frame_parser_feed(&frame, 0x12));       // CRC_HIGH

    BYTES_EQUAL(0x10, frame.opcode);
    LONGS_EQUAL(3, frame.length);
    BYTES_EQUAL(0xAA, frame.payload[0]);
    BYTES_EQUAL(0xBB, frame.payload[1]);
    BYTES_EQUAL(0xCC, frame.payload[2]);
    BYTES_EQUAL(0x34, frame.crc_low);
    BYTES_EQUAL(0x12, frame.crc_high);
    LONGS_EQUAL(SOF, frame.state);
}

// After a frame completes (FRAME_READY resets state to SOF), the parser
// should accept a brand new frame immediately, proving it's genuinely reset
// rather than stuck.
TEST(FrameParser, FeedResetsAndAcceptsNewFrameAfterCompletion)
{
    frame_parser_feed(&frame, 0xA5);
    frame_parser_feed(&frame, 0x10);
    frame_parser_feed(&frame, 0); // zero-length payload
    frame_parser_feed(&frame, 0x34);
    LONGS_EQUAL(FRAME_READY, frame_parser_feed(&frame, 0x12));

    LONGS_EQUAL(FRAME_IN_PROGRESS, frame_parser_feed(&frame, 0xA5));
    LONGS_EQUAL(OPCODE, frame.state);
}

/* --- frame_parser_get_crc --- */

// A NULL frame pointer should be rejected.
TEST(FrameParser, GetCrcRejectsNullFrame)
{
    uint16_t crc = 0;
    LONGS_EQUAL(-1, frame_parser_get_crc(NULL, &crc));
}

// A NULL output pointer should be rejected.
TEST(FrameParser, GetCrcRejectsNullOutput)
{
    LONGS_EQUAL(-1, frame_parser_get_crc(&frame, NULL));
}

// crc_high must land in the upper byte and crc_low in the lower byte - the
// easiest place for a silent byte-order bug to hide.
TEST(FrameParser, GetCrcCombinesHighAndLowBytesInCorrectOrder)
{
    frame.crc_high = 0x12;
    frame.crc_low  = 0x34;

    uint16_t crc = 0;
    LONGS_EQUAL(0, frame_parser_get_crc(&frame, &crc));
    LONGS_EQUAL(0x1234, crc);
}

/* --- frame_parser_serialize --- */

// NULL frame or NULL output buffer should both be rejected.
TEST(FrameParser, SerializeRejectsNullFrameOrBuffer)
{
    uint8_t buf[10];
    LONGS_EQUAL(-1, frame_parser_serialize(NULL, buf, sizeof(buf)));
    LONGS_EQUAL(-1, frame_parser_serialize(&frame, NULL, sizeof(buf)));
}

// A destination buffer too small to hold opcode + length + payload should
// be rejected rather than overflowing.
TEST(FrameParser, SerializeRejectsBufferTooSmall)
{
    frame.length = 3;
    uint8_t buf[4]; // needs 2 + 3 = 5

    LONGS_EQUAL(-1, frame_parser_serialize(&frame, buf, sizeof(buf)));
}

// Happy path: opcode, length, and payload bytes should land at the
// documented indices, and the returned size should equal 2 + length.
TEST(FrameParser, SerializeWritesOpcodeLengthAndPayloadCorrectly)
{
    frame.opcode     = 0x10;
    frame.length     = 3;
    frame.payload[0] = 0xAA;
    frame.payload[1] = 0xBB;
    frame.payload[2] = 0xCC;

    uint8_t buf[5] = {0};
    LONGS_EQUAL(5, frame_parser_serialize(&frame, buf, sizeof(buf)));

    BYTES_EQUAL(0x10, buf[RX_OPCODE_IDX]);
    BYTES_EQUAL(3, buf[RX_LENGTH_IDX]);
    BYTES_EQUAL(0xAA, buf[RX_PAYLOAD_IDX + 0]);
    BYTES_EQUAL(0xBB, buf[RX_PAYLOAD_IDX + 1]);
    BYTES_EQUAL(0xCC, buf[RX_PAYLOAD_IDX + 2]);
}

/* --- frame_parser_serialize_response --- */

// NULL response or NULL output buffer should both be rejected.
TEST(FrameParser, SerializeResponseRejectsNullResponseOrBuffer)
{
    response_frame_t resp = response_frame_t();
    uint8_t buf[TX_FRAME_MAX]; // SOF + LEN + ACK/NACK + up to TX_DATA_MAX data + CRC_L + CRC_H

    LONGS_EQUAL(-1, frame_parser_serialize_response(NULL, buf, sizeof(buf)));
    LONGS_EQUAL(-1, frame_parser_serialize_response(&resp, NULL, sizeof(buf)));
}

// A buffer too small to hold the frame should be rejected rather than
// overflowed. The size needed depends on data_len, so a fixed requirement
// would pass one of these and fail the others.
TEST(FrameParser, SerializeResponseRejectsBufferTooSmall)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = true;

    uint8_t buf[TX_FRAME_MAX] = {0}; // oversized on purpose - a buggy write lands here, not on the stack

    LONGS_EQUAL(-1, frame_parser_serialize_response(&resp, buf, 2)); // needs 3: SOF + LEN + ACK

    resp.data_len = 1;
    LONGS_EQUAL(-1, frame_parser_serialize_response(&resp, buf, 3)); // needs 4: + 1 data byte

    resp.data_len = 4;
    LONGS_EQUAL(-1, frame_parser_serialize_response(&resp, buf, 6)); // needs 7: + 4 data bytes
}

// A buffer of exactly the needed size must be accepted - the off-by-one
// counterpart to the too-small test, and it pins the returned byte count.
TEST(FrameParser, SerializeResponseAcceptsBufferThatFitsExactly)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = true;

    uint8_t buf[TX_FRAME_MAX] = {0};

    LONGS_EQUAL(3, frame_parser_serialize_response(&resp, buf, 3));

    resp.data_len = 1;
    LONGS_EQUAL(4, frame_parser_serialize_response(&resp, buf, 4));

    resp.data_len = 4;
    LONGS_EQUAL(7, frame_parser_serialize_response(&resp, buf, 7));
}

// A data_len past the size of the data array must be rejected, not trusted:
// it is the only field that can drive the copy length out of bounds.
TEST(FrameParser, SerializeResponseRejectsDataLenAboveMax)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = true;
    resp.data_len         = TX_DATA_MAX + 1;

    uint8_t buf[TX_FRAME_MAX] = {0};

    LONGS_EQUAL(-1, frame_parser_serialize_response(&resp, buf, sizeof(buf)));
}

// Happy path for a bare ACK: SOF, LEN counting only the body byte, then the
// ACK byte itself - A5 01 01 on the wire.
TEST(FrameParser, SerializeResponseWritesBareAckBytes)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = true;

    uint8_t buf[TX_FRAME_MAX] = {0};
    LONGS_EQUAL(3, frame_parser_serialize_response(&resp, buf, sizeof(buf)));

    BYTES_EQUAL(0xA5, buf[TX_SOF_IDX]);
    BYTES_EQUAL(0x01, buf[TX_LEN_IDX]); // one body byte
    BYTES_EQUAL(0x01, buf[TX_ACK_IDX]);
}

// A NACK differs from an ACK only in the body byte, so this pins that the ack
// flag actually reaches the buffer instead of a hardcoded 0x01. The buffer is
// pre-filled with 0xFF because a zero-filled one can't tell "wrote 0x00" apart
// from "never wrote anything".
TEST(FrameParser, SerializeResponseWritesBareNackBytes)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = false;

    uint8_t buf[TX_FRAME_MAX];
    memset(buf, 0xFF, sizeof(buf));
    LONGS_EQUAL(3, frame_parser_serialize_response(&resp, buf, sizeof(buf)));

    BYTES_EQUAL(0xA5, buf[TX_SOF_IDX]);
    BYTES_EQUAL(0x01, buf[TX_LEN_IDX]);
    BYTES_EQUAL(0x00, buf[TX_ACK_IDX]);
}

// A single data byte (GPIO_READ's pin state) makes LEN 2 and appends it -
// A5 02 01 01 on the wire. The second case uses data 0 with ack 1 so a data
// byte accidentally sourced from the ack flag can't pass.
TEST(FrameParser, SerializeResponseWritesOneDataByte)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = true;
    resp.data[0]          = 1;
    resp.data_len         = 1;

    uint8_t buf[TX_FRAME_MAX];
    memset(buf, 0xFF, sizeof(buf));
    LONGS_EQUAL(4, frame_parser_serialize_response(&resp, buf, sizeof(buf)));

    BYTES_EQUAL(0xA5, buf[TX_SOF_IDX]);
    BYTES_EQUAL(0x02, buf[TX_LEN_IDX]); // ACK + one data byte
    BYTES_EQUAL(0x01, buf[TX_ACK_IDX]);
    BYTES_EQUAL(0x01, buf[TX_DATA_IDX]);

    resp.data[0] = 0;
    LONGS_EQUAL(4, frame_parser_serialize_response(&resp, buf, sizeof(buf)));

    BYTES_EQUAL(0x01, buf[TX_ACK_IDX]);
    BYTES_EQUAL(0x00, buf[TX_DATA_IDX]);
}

// A multi-byte payload (PWM_GROUP_GET's 32-bit frequency) must land in order
// and set LEN to 1 + data_len - A5 05 01 E8 03 00 00 on the wire.
TEST(FrameParser, SerializeResponseWritesMultipleDataBytesInOrder)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = true;
    resp.data[0]          = 0xE8;
    resp.data[1]          = 0x03;
    resp.data[2]          = 0x00;
    resp.data[3]          = 0x00;
    resp.data_len         = 4;

    uint8_t buf[TX_FRAME_MAX];
    memset(buf, 0xFF, sizeof(buf));
    LONGS_EQUAL(7, frame_parser_serialize_response(&resp, buf, sizeof(buf)));

    BYTES_EQUAL(0xA5, buf[TX_SOF_IDX]);
    BYTES_EQUAL(0x05, buf[TX_LEN_IDX]); // ACK + four data bytes
    BYTES_EQUAL(0x01, buf[TX_ACK_IDX]);
    BYTES_EQUAL(0xE8, buf[TX_DATA_IDX + 0]);
    BYTES_EQUAL(0x03, buf[TX_DATA_IDX + 1]);
    BYTES_EQUAL(0x00, buf[TX_DATA_IDX + 2]);
    BYTES_EQUAL(0x00, buf[TX_DATA_IDX + 3]);
}

// data is populated but data_len is 0, so the 4th byte must be left alone.
// Writing it anyway emits A5 01 01 00 - LEN claims one body byte while two
// follow, and the host reads the stray byte as CRC_L and desyncs.
TEST(FrameParser, SerializeResponseLeavesDataByteUntouchedWhenDataLenZero)
{
    response_frame_t resp = response_frame_t();
    resp.ack              = true;
    resp.data[0]          = 1;
    resp.data_len         = 0;

    uint8_t buf[TX_FRAME_MAX];
    memset(buf, 0xFF, sizeof(buf));
    LONGS_EQUAL(3, frame_parser_serialize_response(&resp, buf, sizeof(buf)));

    BYTES_EQUAL(0x01, buf[TX_LEN_IDX]);
    BYTES_EQUAL(0xFF, buf[TX_DATA_IDX]);
}

/* --- frame_parser_append_crc --- */

// A NULL buffer should be rejected rather than dereferenced.
TEST(FrameParser, AppendCrcRejectsNullBuffer)
{
    LONGS_EQUAL(-1, frame_parser_append_crc(NULL, 3, 6, 0x3E1F));
}

// A buffer without room for two more bytes at current_length must be rejected.
// The second case is the overflow trap: (uint8_t)(254 + 2) wraps to 0, so a
// same-width comparison sees "0 bytes needed", passes, and writes past the end.
TEST(FrameParser, AppendCrcRejectsBufferTooSmall)
{
    // deliberately larger than any buffer_size passed below, so a write that
    // slips through the bounds check lands here instead of off the stack
    uint8_t buf[300] = {0};

    LONGS_EQUAL(-1, frame_parser_append_crc(buf, 5, 6, 0x3E1F));     // needs 7
    LONGS_EQUAL(-1, frame_parser_append_crc(buf, 254, 255, 0x3E1F)); // needs 256
}

// The CRC goes on little-endian - low byte at current_length, high byte after -
// and the return accounts for both. Uses the verified bare-ACK vector:
// A5 01 01 with CRC 0x3E1F becomes A5 01 01 1F 3E on the wire.
TEST(FrameParser, AppendCrcWritesLowByteThenHighByte)
{
    uint8_t buf[6] = {0xA5, 0x01, 0x01, 0xFF, 0xFF, 0xFF};

    LONGS_EQUAL(5, frame_parser_append_crc(buf, 3, sizeof(buf), 0x3E1F));

    BYTES_EQUAL(0x1F, buf[3]);
    BYTES_EQUAL(0x3E, buf[4]);
}

// Appending must only touch the two bytes at current_length. The frame already
// serialized ahead of it stays byte-for-byte intact - overwrite any of it and
// the CRC no longer matches the bytes the host receives - and nothing lands
// past the CRC either. Uses the 4-byte one-data-byte vector: A5 02 01 01 EC 81.
TEST(FrameParser, AppendCrcLeavesSurroundingBytesUntouched)
{
    uint8_t buf[6] = {0xA5, 0x02, 0x01, 0x01, 0xFF, 0xFF};

    LONGS_EQUAL(6, frame_parser_append_crc(buf, 4, sizeof(buf), 0x81EC));

    BYTES_EQUAL(0xA5, buf[TX_SOF_IDX]);
    BYTES_EQUAL(0x02, buf[TX_LEN_IDX]);
    BYTES_EQUAL(0x01, buf[TX_ACK_IDX]);
    BYTES_EQUAL(0x01, buf[TX_DATA_IDX]);

    BYTES_EQUAL(0xEC, buf[4]);
    BYTES_EQUAL(0x81, buf[5]);
}

#include <zephyr/ztest.h>

#include <stdint.h>

#include "message_queue.h"

ZTEST(message_queue_suite, test_message_queue_args_validation)
{
    messageQueue queue = {0};
    uint32_t value = 0x12345678U;

    zassert_equal(msgQueuePush(NULL, &value), MSGQ_ARGS, "push NULL queue");
    zassert_equal(msgQueuePush(&queue, NULL), MSGQ_ARGS, "push NULL data");
    zassert_equal(msgQueuePop(NULL, &value), MSGQ_ARGS, "pop NULL queue");
    zassert_equal(msgQueuePop(&queue, NULL), MSGQ_ARGS, "pop NULL data");
}

ZTEST(message_queue_suite, test_message_queue_fifo_and_empty)
{
    uint32_t storage[3] = {0};
    messageQueue queue = {
        .rdIdx = 0,
        .wrIdx = 0,
        .elSize = sizeof(uint32_t),
        .elNum = 3,
        .data = storage,
    };
    uint32_t in0 = 11U;
    uint32_t in1 = 22U;
    uint32_t out = 0U;

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_EMPTY, "empty queue should report empty");

    zassert_equal(msgQueuePush(&queue, &in0), MSGQ_OK, "push 0 failed");
    zassert_equal(msgQueuePush(&queue, &in1), MSGQ_OK, "push 1 failed");

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_OK, "pop 0 failed");
    zassert_equal(out, in0, "fifo order mismatch for first item");

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_OK, "pop 1 failed");
    zassert_equal(out, in1, "fifo order mismatch for second item");

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_EMPTY, "queue should be empty again");
}

ZTEST(message_queue_suite, test_message_queue_full_and_wraparound)
{
    uint8_t storage[3] = {0};
    messageQueue queue = {
        .rdIdx = 0,
        .wrIdx = 0,
        .elSize = sizeof(uint8_t),
        .elNum = 3,
        .data = storage,
    };
    uint8_t out = 0U;

    for (uint8_t value = 1U; value <= 3U; ++value) {
        zassert_equal(msgQueuePush(&queue, &value), MSGQ_OK, "initial fill failed");
    }

    {
        uint8_t overflow = 4U;
        zassert_equal(msgQueuePush(&queue, &overflow), MSGQ_FULL, "queue should report full");
    }

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_OK, "first pop failed");
    zassert_equal(out, 1U, "unexpected first popped value");

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_OK, "second pop failed");
    zassert_equal(out, 2U, "unexpected second popped value");

    {
        uint8_t wrap0 = 4U;
        uint8_t wrap1 = 5U;
        zassert_equal(msgQueuePush(&queue, &wrap0), MSGQ_OK, "first wrap push failed");
        zassert_equal(msgQueuePush(&queue, &wrap1), MSGQ_OK, "second wrap push failed");
    }

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_OK, "third pop failed");
    zassert_equal(out, 3U, "unexpected third popped value");

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_OK, "fourth pop failed");
    zassert_equal(out, 4U, "unexpected fourth popped value");

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_OK, "fifth pop failed");
    zassert_equal(out, 5U, "unexpected fifth popped value");

    zassert_equal(msgQueuePop(&queue, &out), MSGQ_EMPTY, "queue should be empty after wraparound");
}

ZTEST_SUITE(message_queue_suite, NULL, NULL, NULL, NULL, NULL);
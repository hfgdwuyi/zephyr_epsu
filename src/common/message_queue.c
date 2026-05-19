/*!
 * Copyright � Siemens Healthcare GmbH 2022, All Rights Reserved
 *
 * Project: Building Block Low End MCU
 *
 *  @file
 *  @brief     Hanldes the UART queue interrupt procedure
 */
/*----------------------------------------------------------------------------*/
// Standard includes
#include <stdint.h>
#include <string.h>
// Project includes
#include "message_queue.h"

/*----------------------------------------------------------------------------*/
/*!
 * @brief  Adds data from UART queue
 * @param  queue - pointer to UART array
 * @param  data  - pointer to received character from UART
 * @retval Status of operation
 */
/*----------------------------------------------------------------------------*/
msgQueueStatus msgQueuePush(messageQueue *queue, const void *data)
{
    if ((queue == NULL) || (data == NULL)) {
        return MSGQ_ARGS;
    }
    /* Use modular arithmetic to avoid 32-bit overflow on wrIdx/rdIdx */
    uint32_t used = queue->wrIdx - queue->rdIdx;
    if (used >= queue->elNum) {
        return MSGQ_FULL;
    }
    uint32_t offset = (queue->wrIdx % queue->elNum) * queue->elSize;
    queue->wrIdx = (queue->wrIdx + 1) % (queue->elNum * 2); /* bounded to elNum*2 to preserve "used" math */

    uint8_t *buf = (uint8_t *)queue->data;
    memcpy((buf + offset), data, queue->elSize);
    return MSGQ_OK;
}

/*----------------------------------------------------------------------------*/
/*!
 * @brief  Deletes oone character from UART queue
 * @param  queue - pointer to UART array
 * @param  data  - pointer to received character from UART
 * @retval Status of operation
 */
/*----------------------------------------------------------------------------*/
msgQueueStatus msgQueuePop(messageQueue *queue, void *data)
{
    if ((queue == NULL) || (data == NULL)) {
        return MSGQ_ARGS;
    }
    if (queue->wrIdx == queue->rdIdx) {
        return MSGQ_EMPTY;
    }
    uint32_t offset = (queue->rdIdx % queue->elNum) * queue->elSize;
    queue->rdIdx = (queue->rdIdx + 1) % (queue->elNum * 2);

    const uint8_t *buf = (const uint8_t *)queue->data;
    memcpy(data, (buf + offset), queue->elSize);
    return MSGQ_OK;
}

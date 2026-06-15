#include "coproc_protocol_types.h"

const char *CoprocStatus_ToString(CoprocStatus status)
{
    switch (status) {
        case COPROC_STATUS_OK:
            return "ok";
        case COPROC_STATUS_EMPTY:
            return "empty";
        case COPROC_STATUS_INVALID_ARG:
            return "invalid_arg";
        case COPROC_STATUS_NO_MEM:
            return "no_mem";
        case COPROC_STATUS_INVALID_SIZE:
            return "invalid_size";
        case COPROC_STATUS_INVALID_CRC:
            return "invalid_crc";
        case COPROC_STATUS_UNSUPPORTED:
            return "unsupported";
        case COPROC_STATUS_INVALID_STATE:
            return "invalid_state";
        default:
            return "unknown";
    }
}

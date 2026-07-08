#include "status.h"

const char *status_to_str(status_t status)
{
    switch (status) {
    case STATUS_OK:
        return "OK";
    case STATUS_ERR_INVALID_ARG:
        return "INVALID_ARG";
    case STATUS_ERR_INVALID_PIN:
        return "INVALID_PIN";
    case STATUS_ERR_INVALID_STATE:
        return "INVALID_STATE";
    case STATUS_ERR_NOT_INIT:
        return "NOT_INIT";
    case STATUS_ERR_BUSY:
        return "BUSY";
    case STATUS_ERR_TIMEOUT:
        return "TIMEOUT";
    case STATUS_ERR_UNSUPPORTED:
        return "UNSUPPORTED";
    case STATUS_ERR_EMPTY:
        return "EMPTY";
    case STATUS_ERR_FULL:
        return "FULL";
    default:
        return "?";
    }
}

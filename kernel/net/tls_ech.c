/*
 * ECH scaffold: config store + fail-closed when required without keys.
 * HPKE outer ClientHello encryption lands in a follow-up; until then,
 * tls_ech_prepare_client_hello returns -2 if a config is present (not yet
 * encodable) and -1 if required with no config.
 */
#include "tls_ech.h"
#include "util.h"

static uint8_t ech_cfg[TLS_ECH_CONFIG_MAX];
static size_t ech_cfg_len;
static int ech_required;

int tls_ech_set_config(const uint8_t *cfg, size_t len) {
    if (!cfg || len == 0 || len > TLS_ECH_CONFIG_MAX)
        return -1;
    memcpy(ech_cfg, cfg, len);
    ech_cfg_len = len;
    return 0;
}

void tls_ech_clear_config(void) {
    ech_cfg_len = 0;
    memset(ech_cfg, 0, sizeof(ech_cfg));
}

int tls_ech_have_config(void) {
    return ech_cfg_len > 0;
}

void tls_ech_set_required(int on) {
    ech_required = on ? 1 : 0;
}

int tls_ech_required(void) {
    return ech_required;
}

int tls_ech_prepare_client_hello(void) {
    if (ech_required && ech_cfg_len == 0)
        return -1; /* fail closed: ECH required, no keys/config */
    if (ech_required && ech_cfg_len > 0)
        return -2; /* required + config present but HPKE encode not implemented */
    return 0;      /* ECH not required (config ignored until HPKE lands) */
}

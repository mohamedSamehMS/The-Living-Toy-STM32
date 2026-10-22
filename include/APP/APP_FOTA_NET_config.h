/*
 * APP_FOTA_NET_config.h — configuration of the FOTA orchestrator
 * (FOTA seat, v1.2 — the consent deltas).
 *
 *  Revision history:
 *    v1.2 — POLICY 1 un-blocked (the compare exists; the lexical
 *           trap closed). POLICY guidance updated: the compare
 *           gates BOTH modes in code (structural); this knob is
 *           now the escape hatch for lab/reinstall work only.
 *    v1.0 — first delivery.
 */

#ifndef APP_FOTA_NET_CONFIG_H_
#define APP_FOTA_NET_CONFIG_H_

/* AUTO-BEGIN: 1 = the bench shape — a qualifying notify starts the
 * update automatically. 0 = THE HUB-DAY SHAPE — the notify is
 * surfaced via the callback; the update proceeds only on
 * APP_FOTA_NET_enumStart(). */
#define APP_FOTA_NET_AUTO_BEGIN         0u

/* VERSION POLICY — v1.2 NOTE: the compare now gates BOTH modes in
 * code (the NOTIFIED transition requires a strictly-newer offer,
 * whichever mode runs; see APP_FOTA_NET.c). This knob therefore
 * exists ONLY as the lab escape hatch:
 *   1 = the delivered default: only strictly newer offers accepted
 *       (the product rule; the auto-accept loop's structural closure).
 *   0 = accept ANY offer (lab/reinstall work: re-sending the same
 *       version deliberately, e.g. a corrupted-staging re-download).
 * USE 0 ONLY ON THE BENCH, WITH THE LOOP EDGE IN MIND (C12's re-
 * offer + auto-begin + any-offer = the update loop — FOTA-49). */
#define APP_FOTA_NET_VERSION_POLICY     1u

/* BEGIN <-> BUSY retry (the early-BEGIN race: the ESP still staging
 * answers BUSY; the hub retries after a spacing). */
#define APP_FOTA_NET_BEGIN_BUSY_RETRIES 3u
#define APP_FOTA_NET_BEGIN_RETRY_MS     5000u

#if (APP_FOTA_NET_AUTO_BEGIN != 0) && (APP_FOTA_NET_AUTO_BEGIN != 1)
#error "APP_FOTA_NET: APP_FOTA_NET_AUTO_BEGIN must be 0 or 1"
#endif
#if (APP_FOTA_NET_VERSION_POLICY != 0) && (APP_FOTA_NET_VERSION_POLICY != 1)
#error "APP_FOTA_NET: APP_FOTA_NET_VERSION_POLICY must be 0 or 1"
#endif
#if (APP_FOTA_NET_BEGIN_BUSY_RETRIES == 0u)
#error "APP_FOTA_NET: APP_FOTA_NET_BEGIN_BUSY_RETRIES must be >= 1"
#endif

#endif /* APP_FOTA_NET_CONFIG_H_ */

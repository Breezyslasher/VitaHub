/**
 * VitaABS - update artifact signature verification (see update_verify.hpp).
 */

#include "utils/update_verify.hpp"

// mbedcrypto is linked on every platform that installs updates in place:
// the consoles (Vita/PS4/Switch), Android and desktop Linux/macOS. Windows
// (Schannel, no mbedcrypto) and iOS/tvOS (browser-only updates) fall through
// to the stub at the bottom. CMake defines VITAABS_HAVE_MBEDCRYPTO for the
// former set.
#if defined(VITAABS_HAVE_MBEDCRYPTO)

#include <cstdio>
#include <cstring>

#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#ifdef __PSV__
#include <psp2/io/fcntl.h>
#endif

namespace {

// ── The project's update-signing PUBLIC key ──────────────────────────────
// EC P-256, SPKI PEM. This is the PUBLIC half only — safe to ship. The
// matching PRIVATE key lives solely as the CI secret UPDATE_SIGNING_KEY and
// must never be committed; CI uses it to emit a "<asset>.sig" beside every
// release asset (scripts/ci/sign_release_assets.sh).
//
// Enforcement is ON while this is non-empty: an update whose signature is
// missing or does not verify is discarded before install. To rotate, publish
// a build carrying the NEW public key before signing releases with the new
// private key — clients only trust the key compiled into their own binary.
// Blanking this string reverts the module to an inert pass-through.
const char kUpdatePublicKeyPem[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE0joJIU5CULO9JBXv3ovNak7bdG0T\n"
    "BYy+sl+zcMuS6l/44drhbmCog9JOSSObuIh6NsznP4eIrCMZCCA5Wi0q1w==\n"
    "-----END PUBLIC KEY-----\n";

// SHA-256 the file at `path` into `out`. Vita's newlib fopen is unreliable for
// the ux0: data paths the download writes to, so read via sceIo there (the
// same primitive the download used); every other target uses stdio.
bool sha256Path(const std::string& path, unsigned char out[32]) {
#ifdef __PSV__
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd < 0) return false;
#else
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_starts(&ctx, 0);
#else
    mbedtls_sha256_starts_ret(&ctx, 0);
#endif

    unsigned char buf[65536];
    bool ok = true;
    for (;;) {
#ifdef __PSV__
        int n = sceIoRead(fd, buf, sizeof(buf));
        if (n < 0) { ok = false; break; }
        if (n == 0) break;
        size_t got = (size_t)n;
#else
        size_t got = std::fread(buf, 1, sizeof(buf), f);
        if (got == 0) break;
#endif
#if MBEDTLS_VERSION_MAJOR >= 3
        mbedtls_sha256_update(&ctx, buf, got);
#else
        mbedtls_sha256_update_ret(&ctx, buf, got);
#endif
    }

#ifdef __PSV__
    sceIoClose(fd);
#else
    std::fclose(f);
#endif

#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_finish(&ctx, out);
#else
    mbedtls_sha256_finish_ret(&ctx, out);
#endif
    mbedtls_sha256_free(&ctx);
    return ok;
}

}  // namespace

namespace vitaabs {

bool updateSignatureEnforced() {
    return kUpdatePublicKeyPem[0] != '\0';
}

bool verifyUpdateFile(const std::string& filePath,
                      const std::string& signatureDer,
                      std::string& err) {
    if (kUpdatePublicKeyPem[0] == '\0') return true;   // inert: no key compiled in

    if (signatureDer.empty()) { err = "empty signature"; return false; }

    unsigned char hash[32];
    if (!sha256Path(filePath, hash)) { err = "cannot read downloaded file"; return false; }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    // The length passed to the PEM parser must include the terminating NUL,
    // which sizeof() on the char[] literal accounts for.
    int rc = mbedtls_pk_parse_public_key(
        &pk,
        reinterpret_cast<const unsigned char*>(kUpdatePublicKeyPem),
        sizeof(kUpdatePublicKeyPem));
    if (rc != 0) { err = "malformed update public key"; mbedtls_pk_free(&pk); return false; }

    // ECDSA verification is deterministic and needs no RNG.
    rc = mbedtls_pk_verify(
        &pk, MBEDTLS_MD_SHA256, hash, sizeof(hash),
        reinterpret_cast<const unsigned char*>(signatureDer.data()),
        signatureDer.size());
    mbedtls_pk_free(&pk);

    if (rc != 0) { err = "signature does not match this artifact"; return false; }
    return true;
}

}  // namespace vitaabs

#else  // no mbedcrypto: Windows (verified Schannel TLS) / iOS / tvOS (browser)

namespace vitaabs {

bool updateSignatureEnforced() { return false; }

bool verifyUpdateFile(const std::string&, const std::string&, std::string&) {
    return true;   // rely on TLS-verified transport / browser install flow
}

}  // namespace vitaabs

#endif  // VITAABS_HAVE_MBEDCRYPTO

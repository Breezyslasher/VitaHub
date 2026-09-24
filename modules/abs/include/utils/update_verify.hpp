/**
 * VitaABS - update artifact signature verification
 *
 * Defense-in-depth for the in-app updater. Transport is already TLS-verified
 * (see http_client.cpp), which stops a network attacker swapping the download.
 * This adds authenticity: the downloaded artifact is checked against an
 * ECDSA-P256 / SHA-256 signature made with a key only the project's CI holds,
 * so even a compromised GitHub release — or any future TLS regression — can't
 * push an artifact this build will install.
 *
 * ACTIVE in this build: a signing public key is compiled into
 * update_verify.cpp (kUpdatePublicKeyPem), so updateSignatureEnforced() is
 * true and every in-app update is verified fail-closed on the platforms
 * below. Releases must carry a "<asset>.sig" produced by CI with the matching
 * private key (the UPDATE_SIGNING_KEY secret) or the client refuses them.
 * Blanking the key reverts this module to an inert pass-through.
 *
 * Crypto backend is mbedtls (mbedcrypto), already linked on every platform
 * that installs updates in place — the consoles, Android and desktop
 * Linux/macOS. Windows (Schannel, no mbedcrypto) and iOS/tvOS (browser-only
 * updates) compile a stub and lean on verified TLS / the browser flow.
 */

#pragma once

#include <string>

namespace vitaabs {

// True when this build both has a signing public key compiled in AND has the
// crypto to check it. Callers use this to decide whether to fetch and require
// the signature; when false they skip that step.
bool updateSignatureEnforced();

// Verify that the file at `filePath` matches `signatureDer` (a raw DER
// ECDSA-P256 signature over the file's SHA-256) under the compiled-in update
// public key. Returns true on a valid signature. Returns false with `err` set
// on any mismatch, an unreadable file, or a malformed key/signature. When
// enforcement is off (no key compiled in, or a stub platform) it returns true
// without reading the file.
bool verifyUpdateFile(const std::string& filePath,
                      const std::string& signatureDer,
                      std::string& err);

}  // namespace vitaabs

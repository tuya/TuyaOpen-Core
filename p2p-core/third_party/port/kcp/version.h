#ifndef TUYAOPEN_KCP_VERSION_H
#define TUYAOPEN_KCP_VERSION_H

/*
 * Upstream KCP baseline used by the build-time patch.
 *
 * KCP has no version macro in its source, so the version is recorded here.
 * gen_sbom.py reads this constant (instead of hard-coding the version) when it
 * builds the KCP component entry.
 */
#define TUYAOPEN_KCP_VERSION "1.7"

#endif /* TUYAOPEN_KCP_VERSION_H */

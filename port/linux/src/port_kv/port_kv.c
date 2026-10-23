/**
 * @file port_kv.c
 * @brief core_kv platform ops card: encrypted key-value storage on littlefs.
 *
 * The public KV flow is independent from the storage implementation. This
 * file provides the Linux storage backend: a flash file simulated with
 * standard C IO (./tuyadb/tuyadb) holding a littlefs filesystem on its UF
 * partition. Each record is the plaintext encrypted with AES-128-CBC; the IV
 * and key are each derived from seed/key via SHA256 at init time, and the
 * PKCS#7 padding is verified byte by byte when reading back. Read, write and
 * delete all hold s_lfs_mutex (this layer's port_mutex_*); encryption and
 * decryption run outside the lock.
 *
 * Crypto (AES-128-CBC / SHA-256) uses core's mbedtls-backed interfaces
 * (core_symmetry / core_hash): the port now lives inside the core repository
 * and links tuya_transport, so no separate crypto library is needed. The
 * stored ciphertext is the AES-128-CBC output of the PKCS#7-padded plaintext
 * under the derived key and IV.
 *
 * Note: lfs_util.h picks up lfs_config.h through -DLFS_CONFIG=lfs_config.h
 * (injected by CMake), and memory/mutex/logging all come from this layer
 * (port_system_malloc / port_mutex_* / PORT_LOG_ERR). The KV card contract is
 * still resolved from this layer's interface/.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "core_kv_ops.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core_hash.h"
#include "core_symmetry.h"

#include "port_log.h"
#include "port_memory.h"
#include "port_mutex.h"

#include "lfs.h"

/* ===== flash simulation (file-backed; standard C IO only)
 *
 * Layout:
 *   ./tuyadb/tuyadb (256MB, pre-filled with 0xFF on first creation), UF partition inside
 *   UF_PARTITION_START=0x9000, UF_PARTITION_SIZE=0x18000 (96KB)
 *
 * Scope: this block implements file-backed flash simulation only. On real
 * flash, __flash_* maps onto the actual partition and __flash_erase must issue
 * the block erase command (writing 0xFF is a no-op on NOR flash).
 * ===== */
/* -DFLASH_FILE_PATH=... redirects the directory (the #ifndef guard keeps the
 * build able to override it) */
#ifndef FLASH_FILE_PATH
#define FLASH_FILE_PATH    "./tuyadb"
#endif
#define FLASH_FILE_NAME    FLASH_FILE_PATH "/tuyadb"
#define FLASH_FILE_SIZE    (256 * 1024 * 1024)
#define FLASH_PARTITION_SZ (1 << 12)                                /* 4KB: littlefs block/read/prog granularity */
#define SIMPLE_FLASH_START (FLASH_PARTITION_SZ)                     /* 0x1000 */
#define SIMPLE_FLASH_SIZE  (0x8000)                                 /* 32K */
#define UF_PARTITION_START (SIMPLE_FLASH_START + SIMPLE_FLASH_SIZE) /* 0x9000 */
#define UF_PARTITION_SIZE  (0x18000)                                /* 96K */

static FILE *s_flash_file;

/* Pre-fill with 0xFF on first creation (simulates the erased state; littlefs expects unwritten areas to be 0xFF) */
static OPERATE_RET __flash_file_prefill(void)
{
    char *data = port_system_malloc(FLASH_PARTITION_SZ);
    if (NULL == data) {
        return OPRT_MALLOC_FAILED;
    }

    memset(data, 0xff, FLASH_PARTITION_SZ);
    for (uint32_t offset = 0; offset < FLASH_FILE_SIZE; offset += FLASH_PARTITION_SZ) {
        if (FLASH_PARTITION_SZ != fwrite(data, 1, FLASH_PARTITION_SZ, s_flash_file)) {
            /* ENOSPC is the most common cause: the first pre-fill writes FLASH_FILE_SIZE (256MB) */
            PORT_LOG_ERR("flash prefill failed at offset %u/%u: %s (out of disk space? %u MB needed initially)", offset,
                   (unsigned int)FLASH_FILE_SIZE, strerror(errno),
                   (unsigned int)(FLASH_FILE_SIZE / (1024 * 1024)));
            port_system_free(data);
            return OPRT_FILE_WRITE_FAILED;
        }
    }
    port_system_free(data);

    fflush(s_flash_file);
    int fd = fileno(s_flash_file);
    if (fd >= 0) {
        fsync(fd);
    }
    return OPRT_OK;
}

/* Open/create the flash file (idempotent). Unusable storage (read-only dir etc.) returns an error code instead of crashing */
static OPERATE_RET __flash_open(void)
{
    if (NULL != s_flash_file) {
        return OPRT_OK;
    }

    if (0 != access(FLASH_FILE_NAME, F_OK)) {
        /* First time: create directory + file + pre-fill */
        if (0 != access(FLASH_FILE_PATH, F_OK) && 0 != mkdir(FLASH_FILE_PATH, 0755) && EEXIST != errno) {
            PORT_LOG_ERR("flash dir create failed: %s: %s", FLASH_FILE_PATH, strerror(errno));
            return OPRT_FILE_OPEN_FAILED;
        }
        s_flash_file = fopen(FLASH_FILE_NAME, "wb+");
        if (NULL == s_flash_file) {
            PORT_LOG_ERR("flash file create failed: %s: %s", FLASH_FILE_NAME, strerror(errno));
            return OPRT_FILE_OPEN_FAILED;
        }
        if (OPRT_OK != __flash_file_prefill()) {
            fclose(s_flash_file);
            s_flash_file = NULL;
            remove(FLASH_FILE_NAME); /* drop the half-written file: otherwise the next access hits it and never pre-fills */
            return OPRT_FILE_WRITE_FAILED;
        }
    } else {
        s_flash_file = fopen(FLASH_FILE_NAME, "rb+");
        if (NULL == s_flash_file) {
            PORT_LOG_ERR("flash file open failed: %s", FLASH_FILE_NAME);
            return OPRT_FILE_OPEN_FAILED;
        }
    }

    int fd = fileno(s_flash_file);
    if (fd >= 0) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    return OPRT_OK;
}

static OPERATE_RET __flash_read(uint32_t addr, uint8_t *dst, uint32_t size)
{
    if (NULL == s_flash_file) {
        return OPRT_RESOURCE_NOT_READY;
    }
    if (0 != fseek(s_flash_file, (long)addr, SEEK_SET)) {
        return OPRT_FILE_OPEN_FAILED;
    }
    if (size != (uint32_t)fread(dst, 1, size, s_flash_file)) {
        return OPRT_FILE_READ_FAILED;
    }
    return OPRT_OK;
}

static OPERATE_RET __flash_write(uint32_t addr, const uint8_t *src, uint32_t size)
{
    if (NULL == s_flash_file) {
        return OPRT_RESOURCE_NOT_READY;
    }
    if (0 != fseek(s_flash_file, (long)addr, SEEK_SET)) {
        return OPRT_FILE_OPEN_FAILED;
    }
    if (size != (uint32_t)fwrite(src, 1, size, s_flash_file)) {
        return OPRT_FILE_WRITE_FAILED;
    }
    fflush(s_flash_file);
    int fd = fileno(s_flash_file);
    if (fd >= 0) {
        fsync(fd);
    }
    return OPRT_OK;
}

/* File backend only: nothing to erase - files are overwritable and littlefs
 * CRC commit semantics keep consistency; real flash must erase blocks here */
static OPERATE_RET __flash_erase(uint32_t addr, uint32_t size)
{
    (void)addr;
    (void)size;
    return OPRT_OK;
}

static lfs_t s_lfs;
static lfs_size_t s_lfs_flash_addr;

static core_kv_cfg_t s_lfs_kv_cfg;
static PORT_MUTEX_HANDLE s_lfs_mutex;
static BOOL_T s_lfs_mounted = FALSE;

static int user_provided_block_device_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
                                           lfs_size_t size)
{
    OPERATE_RET ret = __flash_read(s_lfs_flash_addr + c->block_size * block + off, buffer, size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                                           const void *buffer, lfs_size_t size)
{
    OPERATE_RET ret = __flash_write(s_lfs_flash_addr + c->block_size * block + off, buffer, size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_erase(const struct lfs_config *c, lfs_block_t block)
{
    OPERATE_RET ret = __flash_erase(s_lfs_flash_addr + c->block_size * block, c->block_size);
    return (OPRT_OK == ret) ? LFS_ERR_OK : LFS_ERR_IO;
}

static int user_provided_block_device_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

/* Storage helpers: callers hold s_lfs_mutex for read, write, and remove. */
static OPERATE_RET kv_storage_init(void)
{
    /* lfs keeps the cfg pointer (lfs->cfg), so the config struct must outlive the mount */
    static struct lfs_config lfs_cfg;
    int result;

    if (OPRT_OK != __flash_open()) {
        return OPRT_COM_ERROR;
    }

    s_lfs_flash_addr = UF_PARTITION_START;
    memset(&lfs_cfg, 0, sizeof(lfs_cfg));
    lfs_cfg.read = user_provided_block_device_read;
    lfs_cfg.prog = user_provided_block_device_prog;
    lfs_cfg.erase = user_provided_block_device_erase;
    lfs_cfg.sync = user_provided_block_device_sync;
    lfs_cfg.read_size = FLASH_PARTITION_SZ;
    lfs_cfg.prog_size = FLASH_PARTITION_SZ;
    lfs_cfg.block_size = FLASH_PARTITION_SZ;
    lfs_cfg.block_count = UF_PARTITION_SIZE / FLASH_PARTITION_SZ;
    lfs_cfg.cache_size = FLASH_PARTITION_SZ;
    /* littlefs in-memory lookahead buffer (never written to disk and
     * independent of the layout): the expression evaluates to 8. */
    lfs_cfg.lookahead_size = lfs_cfg.block_count / 8 + (8 - (lfs_cfg.block_count / 8));
    lfs_cfg.block_cycles = 500;

    result = lfs_mount(&s_lfs, &lfs_cfg);
    if (LFS_ERR_OK != result) {
        result = lfs_format(&s_lfs, &lfs_cfg);
        if (LFS_ERR_OK == result) {
            result = lfs_mount(&s_lfs, &lfs_cfg);
        }
    }

    return (LFS_ERR_OK == result) ? OPRT_OK : OPRT_COM_ERROR;
}

static OPERATE_RET kv_storage_write(const char *key, const uint8_t *data, uint32_t length)
{
    lfs_file_t file;
    int result;
    int sync_result;
    int close_result;

    result = lfs_file_open(&s_lfs, &file, key, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (LFS_ERR_OK != result) {
        PORT_LOG_ERR("lfs open %s failed: %d", key, result);
        return OPRT_FILE_OPEN_FAILED;
    }

    result = lfs_file_write(&s_lfs, &file, data, length);
    sync_result = lfs_file_sync(&s_lfs, &file);
    close_result = lfs_file_close(&s_lfs, &file);
    if (result != (int)length || LFS_ERR_OK != sync_result || LFS_ERR_OK != close_result) {
        PORT_LOG_ERR("kv write failed write:%d sync:%d close:%d", result, sync_result, close_result);
        return OPRT_KVS_WR_FAIL;
    }

    return OPRT_OK;
}

static OPERATE_RET kv_storage_read(const char *key, uint8_t **data, uint32_t *length)
{
    lfs_file_t file;
    lfs_soff_t stored_length;
    int result;
    int close_result;

    result = lfs_file_open(&s_lfs, &file, key, LFS_O_RDONLY);
    if (LFS_ERR_OK != result) {
        return (LFS_ERR_NOENT == result) ? OPRT_NOT_FOUND : OPRT_FILE_OPEN_FAILED;
    }

    stored_length = lfs_file_size(&s_lfs, &file);
    if (stored_length < 0) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_KVS_RD_FAIL;
    }
    if (0 == stored_length) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_NOT_FOUND;
    }
    if (stored_length % 16 != 0) {
        PORT_LOG_ERR("invalid encrypted KV length %d for %s", (int)stored_length, key);
        lfs_file_close(&s_lfs, &file);
        return OPRT_KVS_RD_FAIL;
    }

    *data = port_system_malloc((size_t)stored_length + 1U);
    if (NULL == *data) {
        lfs_file_close(&s_lfs, &file);
        return OPRT_MALLOC_FAILED;
    }

    result = lfs_file_read(&s_lfs, &file, *data, (lfs_size_t)stored_length);
    close_result = lfs_file_close(&s_lfs, &file);
    if (result != (int)stored_length || LFS_ERR_OK != close_result) {
        PORT_LOG_ERR("kv read failed read:%d close:%d", result, close_result);
        port_system_free(*data);
        *data = NULL;
        return OPRT_KVS_RD_FAIL;
    }

    *length = (uint32_t)stored_length;
    return OPRT_OK;
}

static OPERATE_RET kv_storage_remove(const char *key)
{
    int result = lfs_remove(&s_lfs, key);

    /* Contract (core_kv.h): a missing key counts as success */
    return (LFS_ERR_OK == result || LFS_ERR_NOENT == result) ? OPRT_OK : OPRT_COM_ERROR;
}

static OPERATE_RET kv_encrypt(const uint8_t *value, size_t length, uint8_t **encrypted, uint32_t *encrypted_length)
{
    uint8_t iv[16];

    if (length > UINT32_MAX - 16U) {
        return OPRT_INVALID_PARM;
    }

    memcpy(iv, s_lfs_kv_cfg.seed, sizeof(iv));
    return core_aes128_cbc_encode((uint8_t *)value, (uint32_t)length, (uint8_t *)s_lfs_kv_cfg.key, iv, encrypted,
                                  encrypted_length);
}

static OPERATE_RET kv_decrypt(uint8_t *encrypted, uint32_t encrypted_length, uint8_t **value, size_t *value_length)
{
    uint8_t iv[16];
    uint8_t *decrypted = NULL;
    uint32_t decrypted_length = 0;
    uint8_t padding_length;
    uint32_t i;
    OPERATE_RET result;

    if (0 == encrypted_length || encrypted_length % 16U != 0U) {
        return OPRT_KVS_RD_FAIL;
    }

    memcpy(iv, s_lfs_kv_cfg.seed, sizeof(iv));
    result = core_aes128_cbc_decode(encrypted, encrypted_length, (uint8_t *)s_lfs_kv_cfg.key, iv, &decrypted,
                                    &decrypted_length);
    if (OPRT_OK != result) {
        return result;
    }

    if (0 == decrypted_length || decrypted_length % 16U != 0U) {
        core_aes_free_data(decrypted);
        return OPRT_KVS_RD_FAIL;
    }

    padding_length = decrypted[decrypted_length - 1U];
    if (0 == padding_length || padding_length > 16U || padding_length >= decrypted_length) {
        core_aes_free_data(decrypted);
        return OPRT_KVS_RD_FAIL;
    }
    for (i = 0; i < padding_length; ++i) {
        if (decrypted[decrypted_length - 1U - i] != padding_length) {
            core_aes_free_data(decrypted);
            return OPRT_KVS_RD_FAIL;
        }
    }

    *value_length = decrypted_length - padding_length;
    decrypted[*value_length] = 0;
    *value = decrypted;
    return OPRT_OK;
}

static OPERATE_RET __kv_init(core_kv_cfg_t *kv_cfg)
{
    uint8_t sha256_ret[32];
    OPERATE_RET result;

    if (NULL == kv_cfg) {
        return OPRT_INVALID_PARM;
    }

    /* Key derivation: refreshed from the passed config on every init */
    memset(&s_lfs_kv_cfg, 0, sizeof(s_lfs_kv_cfg));
    core_sha256_ret((const uint8_t *)kv_cfg->seed, CORE_LV_KEY_LEN, sha256_ret, 0);
    memcpy(s_lfs_kv_cfg.seed, sha256_ret, CORE_LV_KEY_LEN);
    core_sha256_ret((const uint8_t *)kv_cfg->key, CORE_LV_KEY_LEN, sha256_ret, 0);
    memcpy(s_lfs_kv_cfg.key, sha256_ret, CORE_LV_KEY_LEN);

    if (NULL == s_lfs_mutex) {
        result = port_mutex_create_init(&s_lfs_mutex);
        if (OPRT_OK != result) {
            return result;
        }
    }

    /* Repeated init is idempotent: skip mounting when already mounted (a second
     * mount would leak lfs buffers, and the format fallback would wipe data) */
    if (TRUE == s_lfs_mounted) {
        return OPRT_OK;
    }

    result = kv_storage_init();
    if (OPRT_OK != result) {
        return result;
    }
    s_lfs_mounted = TRUE;

    return OPRT_OK;
}

static OPERATE_RET __kv_set(const char *key, const uint8_t *value, size_t length)
{
    uint8_t *encrypted = NULL;
    uint32_t encrypted_length = 0;
    OPERATE_RET result;

    if (NULL == key || NULL == value || 0 == length) {
        return OPRT_INVALID_PARM;
    }
    /* Never touch lfs before init (medium not mounted): lfs->cfg is NULL and would crash */
    if (FALSE == s_lfs_mounted) {
        PORT_LOG_ERR("kv not mounted, set %s rejected", key);
        return OPRT_COM_ERROR;
    }

    PORT_LOG_DEBUG("key:%s, len %u", key, (unsigned int)length);
    result = kv_encrypt(value, length, &encrypted, &encrypted_length);
    if (OPRT_OK != result) {
        PORT_LOG_ERR("key %s encrypt failed: %d", key, result);
        return result;
    }

    port_mutex_lock(s_lfs_mutex);
    result = kv_storage_write(key, encrypted, encrypted_length);
    port_mutex_unlock(s_lfs_mutex);
    core_aes_free_data(encrypted);
    return result;
}

static OPERATE_RET __kv_get(const char *key, uint8_t **value, size_t *length)
{
    uint8_t *encrypted = NULL;
    uint32_t encrypted_length = 0;
    OPERATE_RET result;

    /* Contract (core_kv.h): on failure set *value to NULL and *length to 0. Zero
     * before validating arguments so direct callers get the same guarantee */
    if (NULL != value) {
        *value = NULL;
    }
    if (NULL != length) {
        *length = 0;
    }

    if (NULL == key || NULL == value || NULL == length) {
        return OPRT_INVALID_PARM;
    }
    /* Never touch lfs before init (medium not mounted): lfs->cfg is NULL and would crash */
    if (FALSE == s_lfs_mounted) {
        PORT_LOG_ERR("kv not mounted, get %s rejected", key);
        return OPRT_COM_ERROR;
    }

    port_mutex_lock(s_lfs_mutex);
    result = kv_storage_read(key, &encrypted, &encrypted_length);
    port_mutex_unlock(s_lfs_mutex);
    if (OPRT_OK != result) {
        return result;
    }

    /* The plaintext buffer allocated by core_aes128_cbc_decode (NUL-terminated)
     * is handed to the caller, who releases it with core_free (same allocator) */
    result = kv_decrypt(encrypted, encrypted_length, value, length);
    port_system_free(encrypted);
    if (OPRT_OK != result) {
        PORT_LOG_ERR("key %s decrypt failed: %d", key, result);
    }

    return result;
}

static OPERATE_RET __kv_del(const char *key)
{
    OPERATE_RET result;

    if (NULL == key) {
        return OPRT_INVALID_PARM;
    }
    /* Never touch lfs before init (medium not mounted): lfs->cfg is NULL and would crash */
    if (FALSE == s_lfs_mounted) {
        PORT_LOG_ERR("kv not mounted, del %s rejected", key);
        return OPRT_COM_ERROR;
    }

    port_mutex_lock(s_lfs_mutex);
    result = kv_storage_remove(key);
    port_mutex_unlock(s_lfs_mutex);
    return result;
}

/* ---------------------------------------------------------------------------
 * Platform ops card: assembled here; core's core_kv.c dispatches to the four
 * port_kv_* implementations through it.
 * --------------------------------------------------------------------------- */
CORE_KV_OPS_T core_kv_ops_platform = {
    .init = __kv_init,
    .set = __kv_set,
    .get = __kv_get,
    .del = __kv_del,
};

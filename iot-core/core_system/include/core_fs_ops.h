/**
 * @file core_fs_ops.h
 * @brief CORE file system platform ops contract: CORE_FS_OPS_T with 18
 *        interfaces (9 file + 4 path + 5 directory).
 *
 * The consumer assembles core_fs_ops_platform in fs_card.c, binding the members
 * to its fs_card_fopen / fs_card_mkdir / fs_card_dir_* implementations; core's
 * core_fs.c dispatches core_fs_* through core_fs_get_ops().
 *
 * The file system is an optional capability. Its callers are
 * p2p-core/local_store and the audio_player file source; platforms that do not
 * use it do not compile fs_card.c, which is gated by
 * ENABLE_TUYA_P2P || ENABLE_AI_PLAYER. The contract contains exactly the 18
 * interfaces listed below.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 *
 */
#ifndef __CORE_FS_OPS_H__
#define __CORE_FS_OPS_H__

#include "tuya_cloud_types.h"

#include "core_fs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief core_fs platform ops: members map one-to-one to the platform file
 *        system implementation assembled in fs_card.c
 *
 * @note Handle types come from tuya_cloud_types.h (TUYA_FILE / TUYA_DIR /
 *       TUYA_FILEINFO are void* aliases) and bind directly to the platform
 *       implementation. Return values follow core_fs.h one-to-one (handles:
 *       NULL on failure; read/write: byte counts; core_fileno: fd; others:
 *       0 on success, non-zero on failure).
 */
typedef struct {
    //! file
    /* Open a file; see core_fopen in core_fs.h */
    TUYA_FILE (*fopen)(const char *path, const char *mode);
    /* Close a file; see core_fclose in core_fs.h */
    int (*fclose)(TUYA_FILE file);
    /* Write a file; see core_fwrite in core_fs.h */
    int (*fwrite)(void *buf, int bytes, TUYA_FILE file);
    /* Read a file; see core_fread in core_fs.h */
    int (*fread)(void *buf, int bytes, TUYA_FILE file);
    /* Seek to an offset; see core_fseek in core_fs.h */
    int (*fseek)(TUYA_FILE file, int64_t offs, int whence);
    /* Flush the stream buffer; see core_fflush in core_fs.h */
    int (*fflush)(TUYA_FILE file);
    /* Flush to storage by fd; see core_fsync in core_fs.h */
    int (*fsync)(int fd);
    /* Get the file fd; see core_fileno in core_fs.h */
    int (*fileno)(TUYA_FILE file);
    /* Read one line; see core_fgets in core_fs.h */
    char *(*fgets)(char *buf, int len, TUYA_FILE file);
    //! path
    /* Check whether a file/directory exists; see core_fs_is_exist in core_fs.h */
    int (*fs_is_exist)(const char *path, BOOL_T *is_exist);
    /* Create a directory; see core_fs_mkdir in core_fs.h */
    int (*fs_mkdir)(const char *path);
    /* Remove a file/directory; see core_fs_remove in core_fs.h */
    int (*fs_remove)(const char *path);
    /* Rename; see core_fs_rename in core_fs.h */
    int (*fs_rename)(const char *path_old, const char *path_new);
    //! directory
    /* Open a directory; see core_dir_open in core_fs.h */
    int (*dir_open)(const char *path, TUYA_DIR *dir);
    /* Close a directory; see core_dir_close in core_fs.h */
    int (*dir_close)(TUYA_DIR dir);
    /* Read a directory entry (advances the internal pointer); see core_dir_read in core_fs.h */
    int (*dir_read)(TUYA_DIR dir, TUYA_FILEINFO *info);
    /* Get the entry name; see core_dir_name in core_fs.h */
    int (*dir_name)(TUYA_FILEINFO info, const char **name);
    /* Check whether the entry is a directory; see core_dir_is_directory in core_fs.h */
    int (*dir_is_directory)(TUYA_FILEINFO info, BOOL_T *is_dir);
} CORE_FS_OPS_T;

/**
 * @brief Get the platform ops of core_fs
 *
 * @return pointer to the platform ops, never NULL (defined in the port)
 */
CORE_FS_OPS_T *core_fs_get_ops(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif

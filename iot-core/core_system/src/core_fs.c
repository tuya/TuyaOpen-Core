/**
 * @file core_fs.c
 * @brief core_fs platform dispatch: forwards the 18 interfaces to the port's
 *        file system implementation (core_fs_ops_platform).
 *
 * iot-core holds no file system logic -- medium, path mapping and handle
 * shape all live in the port (see the core_fs_ops.h contract). Dispatch goes
 * through CORE_FS_EXEC_OP, which checks the card and the member before calling
 * them and returns the given default otherwise.
 *
 * Within this repository the only caller is p2p-core/local_store; when no
 * core_fs symbol is referenced the static library does not pull in core_fs.o
 * and the port's core_fs_ops_platform need not exist. The platform
 * implementation (fs_card.c) is supplied by the application build.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */
#include "core_fs.h"
#include "core_fs_ops.h"
#include "core_log.h"

/* Platform ops card (defined in the platform's fs_card.c), dispatched by core
 * through the getter */
extern CORE_FS_OPS_T core_fs_ops_platform;

CORE_FS_OPS_T *core_fs_get_ops(void)
{
    return &core_fs_ops_platform;
}

#define CORE_FS_EXEC_OP(op_name, default_ret, ...)                                                                      \
    do {                                                                                                               \
        CORE_FS_OPS_T *ops = core_fs_get_ops();                                                                          \
        if (NULL != ops && NULL != ops->op_name) {                                                                     \
            return ops->op_name(__VA_ARGS__);                                                                          \
        }                                                                                                              \
        PR_ERR("CORE FS operation %s not available", #op_name);                                                         \
        return default_ret;                                                                                            \
    } while (0)

/* ---------------------------------------------------------------------------
 * File
 * --------------------------------------------------------------------------- */
TUYA_FILE core_fopen(const char *path, const char *mode)
{
    CORE_FS_EXEC_OP(fopen, NULL, path, mode);
}

int core_fclose(TUYA_FILE file)
{
    CORE_FS_EXEC_OP(fclose, -1, file);
}

int core_fwrite(void *buf, int bytes, TUYA_FILE file)
{
    CORE_FS_EXEC_OP(fwrite, -1, buf, bytes, file);
}

int core_fread(void *buf, int bytes, TUYA_FILE file)
{
    CORE_FS_EXEC_OP(fread, -1, buf, bytes, file);
}

int core_fseek(TUYA_FILE file, int64_t offs, int whence)
{
    CORE_FS_EXEC_OP(fseek, -1, file, offs, whence);
}

int core_fflush(TUYA_FILE file)
{
    CORE_FS_EXEC_OP(fflush, -1, file);
}

int core_fsync(int fd)
{
    CORE_FS_EXEC_OP(fsync, -1, fd);
}

int core_fileno(TUYA_FILE file)
{
    CORE_FS_EXEC_OP(fileno, -1, file);
}

char *core_fgets(char *buf, int len, TUYA_FILE file)
{
    CORE_FS_EXEC_OP(fgets, NULL, buf, len, file);
}

/* ---------------------------------------------------------------------------
 * Path
 * --------------------------------------------------------------------------- */
int core_fs_is_exist(const char *path, BOOL_T *is_exist)
{
    CORE_FS_EXEC_OP(fs_is_exist, -1, path, is_exist);
}

int core_fs_mkdir(const char *path)
{
    CORE_FS_EXEC_OP(fs_mkdir, -1, path);
}

int core_fs_remove(const char *path)
{
    CORE_FS_EXEC_OP(fs_remove, -1, path);
}

int core_fs_rename(const char *path_old, const char *path_new)
{
    CORE_FS_EXEC_OP(fs_rename, -1, path_old, path_new);
}

/* ---------------------------------------------------------------------------
 * Directory
 * --------------------------------------------------------------------------- */
int core_dir_open(const char *path, TUYA_DIR *dir)
{
    CORE_FS_EXEC_OP(dir_open, -1, path, dir);
}

int core_dir_close(TUYA_DIR dir)
{
    CORE_FS_EXEC_OP(dir_close, -1, dir);
}

int core_dir_read(TUYA_DIR dir, TUYA_FILEINFO *info)
{
    CORE_FS_EXEC_OP(dir_read, -1, dir, info);
}

int core_dir_name(TUYA_FILEINFO info, const char **name)
{
    CORE_FS_EXEC_OP(dir_name, -1, info, name);
}

int core_dir_is_directory(TUYA_FILEINFO info, BOOL_T *is_dir)
{
    CORE_FS_EXEC_OP(dir_is_directory, -1, info, is_dir);
}

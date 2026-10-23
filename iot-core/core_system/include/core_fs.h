/**
 * @file core_fs.h
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
#ifndef __CORE_FS_H__
#define __CORE_FS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/********************************************************************************
 *********************************** file ***************************************
 ********************************************************************************/

/**
* @brief Open file
*
* @param[in] path: path of file
* @param[in] mode: file open mode: "r","w"...
*
* @note This API is used to open a file
*
* @return the file handle, NULL means failed
*/
TUYA_FILE core_fopen(const char *path, const char *mode);

/**
* @brief Close file
*
* @param[in] file: file handle
*
* @note This API is used to close a file
*
* @return 0 on success. EOF on failed
*/
int core_fclose(TUYA_FILE file);

/**
* @brief write file
*
* @param[in] buf: buffer for writing file
* @param[in] bytes: buffer size
* @param[in] file: file handle
*
* @note This API is used to write a file
*
* @return the bytes write to file
*/
int core_fwrite(void *buf, int bytes, TUYA_FILE file);

/**
* @brief Read file
*
* @param[in] buf: buffer for reading file
* @param[in] bytes: buffer size
* @param[in] file: file handle
*
* @note This API is used to read a file
*
* @return the bytes read from file
*/
int core_fread(void *buf, int bytes, TUYA_FILE file);

/**
* @brief Seek to the offset position of the file
*
* @param[in] file: file handle
* @param[in] offs: offset
* @param[in] whence: seek start point mode
*
* @note This API is used to seek to the offset position of the file.
*
* @return 0 on success, others on failed
*/
int core_fseek(TUYA_FILE file, int64_t offs, int whence);

/**
* @brief flush the IO read/write stream
*
* @param[in] file char stream
*
* @note This API is used to flush the IO read/write stream.
*
* @return 0 success,-1 failed
*/
int core_fflush(TUYA_FILE file);

/**
* @brief write buffer to flash
*
* @param[in] fd: file fd
*
* @note This API is used to write buffer to flash
* @note The parameter is an fd (obtain it with core_fileno); aligned with fs_card_fsync
*
* @return 0 on success. others on failed
*/
int core_fsync(int fd);

/**
* @brief get the file fd
*
* @param[in] file char stream
*
* @note This API is used to get the file fd.
*
* @return the file fd
*/
int core_fileno(TUYA_FILE file);

/**
* @brief Read string from file
*
* @param[in] buf: buffer for reading file
* @param[in] len: buffer size
* @param[in] file: file handle
*
* @note This API is used to read string from file
*
* @return the content get from file, NULL means failed
*/
char *core_fgets(char *buf, int len, TUYA_FILE file);

/********************************************************************************
 ************************************ path **************************************
 ********************************************************************************/

/**
* @brief Check whether the file or directory exists
*
* @param[in] path: path of directory
* @param[out] is_exist: the file or directory exists or not
*
* @note This API is used to check whether the file or directory exists.
*
* @return 0 on success. Others on failed
*/
int core_fs_is_exist(const char *path, BOOL_T *is_exist);

/**
* @brief Make directory
*
* @param[in] path: path of directory
*
* @note This API is used for making a directory
*
* @return 0 on success. Others on failed
*/
int core_fs_mkdir(const char *path);

/**
* @brief Remove file or directory
*
* @param[in] path: path of directory
*
* @note This API is used for removing a file or directory
*
* @return 0 on success. Others on failed
*/
int core_fs_remove(const char *path);

/**
* @brief File rename
*
* @param[in] path_old: old path of directory
* @param[in] path_new: new path of directory
*
* @note This API is used to rename the file.
*
* @return 0 on success. Others on failed
*/
int core_fs_rename(const char *path_old, const char *path_new);

/********************************************************************************
 ********************************** directory ***********************************
 ********************************************************************************/

/**
* @brief Open directory
*
* @param[in] path: path of directory
* @param[out] dir: handle of directory
*
* @note This API is used to open a directory
*
* @return 0 on success. Others on failed
*/
int core_dir_open(const char *path, TUYA_DIR *dir);

/**
* @brief Close directory
*
* @param[in] dir: handle of directory
*
* @note This API is used to close a directory
*
* @return 0 on success. Others on failed
*/
int core_dir_close(TUYA_DIR dir);

/**
* @brief Read directory
*
* @param[in] dir: handle of directory
* @param[out] info: file information
*
* @note This API is used to read a directory.
* Read the file information of the current node, and the internal pointer points to the next node.
*
* @return 0 on success. Others on failed
*/
int core_dir_read(TUYA_DIR dir, TUYA_FILEINFO *info);

/**
* @brief Get the name of the file node
*
* @param[in] info: file information
* @param[out] name: file name
*
* @note This API is used to get the name of the file node.
*
* @return 0 on success. Others on failed
*/
int core_dir_name(TUYA_FILEINFO info, const char **name);

/**
* @brief Check whether the node is a directory
*
* @param[in] info: file information
* @param[out] is_dir: is directory or not
*
* @note This API is used to check whether the node is a directory.
*
* @return 0 on success. Others on failed
*/
int core_dir_is_directory(TUYA_FILEINFO info, BOOL_T *is_dir);

#ifdef __cplusplus
} // extern "C"
#endif /* __cplusplus */

#endif

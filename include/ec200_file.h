/**
 * @file ec200_file.h
 * @brief Modem filesystem access (AT+QF* — UFS storage).
 *
 * Used mainly to upload TLS certificates/keys that ec200_ssl_config_t then
 * references by name, but also usable for arbitrary small-file storage in
 * the module's UFS.
 */

#ifndef EC200_FILE_H
#define EC200_FILE_H

#include "ec200_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup EC200_File Filesystem
 *  @brief Upload/list/delete files in the modem's UFS storage.
 *  @{
 */

/**
 * @brief Upload a file to the modem UFS (AT+QFUPL).
 *
 * Overwrites any existing file of the same name.  Typical use: upload a PEM
 * CA certificate, then reference it from ec200_ssl_config_t::cacert.
 *
 * @param h        Initialised handle.
 * @param name     Destination filename (e.g. "cacert.pem").
 * @param data     File contents.
 * @param len      Number of bytes (1..65535).  The upper bound is the size of
 *                 a single raw write, which is a uint16_t.
 * @param checksum Optional: receives the module's uploaded-data checksum
 *                 (may be NULL).
 *
 * @return EC200_OK, EC200_ERR_PARAM, EC200_ERR_MODULE (e.g. no space),
 *         EC200_ERR_TIMEOUT / EC200_ERR_IO.
 */
ec200_status_t ec200_file_upload(ec200_handle_t *h,
                                 const char     *name,
                                 const uint8_t  *data,
                                 uint32_t        len,
                                 uint16_t       *checksum);

/**
 * @brief Delete a file from the modem UFS (AT+QFDEL).
 *
 * @param h     Initialised handle.
 * @param name  Filename, or "*" to delete all files.
 * @return EC200_OK, EC200_ERR_PARAM, EC200_ERR_MODULE (not found), or others.
 */
ec200_status_t ec200_file_delete(ec200_handle_t *h, const char *name);

/**
 * @brief Test whether a file exists (via AT+QFLST).
 *
 * @param h       Initialised handle.
 * @param name    Filename to check.
 * @param exists  Output: true if present.
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_file_exists(ec200_handle_t *h, const char *name,
                                 bool *exists);

/**
 * @brief Get a single file's size (AT+QFLST).
 *
 * @param h     Initialised handle.
 * @param name  Filename.
 * @param size  Output: file size in bytes.
 * @return EC200_OK, or EC200_ERR_MODULE / EC200_ERR_PARSE if not found.
 */
ec200_status_t ec200_file_size(ec200_handle_t *h, const char *name,
                               uint32_t *size);

/**
 * @brief Query UFS storage space (AT+QFLDS="UFS").
 *
 * @param h   Initialised handle.
 * @param st  Output: free/total bytes.
 * @return EC200_OK or an error code.
 */
ec200_status_t ec200_file_storage(ec200_handle_t *h,
                                  ec200_file_storage_t *st);

/** @} */ /* EC200_File */

#ifdef __cplusplus
}
#endif

#endif /* EC200_FILE_H */

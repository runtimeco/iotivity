//******************************************************************
//
// Copyright 2015 Samsung Electronics All Rights Reserved.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

#ifndef IOTVT_SRM_CRLR_H
#define IOTVT_SRM_CRLR_H

#include "octypes.h"
#include "securevirtualresourcetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This function stores CRL in SRM.
 *
 * @param crl to be stored in SRM.
 *
 * @return ::OC_STACK_OK for Success, otherwise some error value.
 */
OCStackResult UpdateCRLResource(const OicSecCrl_t *crl);

/**
 * This function get encoded with base64 CRL from SRM.
 *
 * @note Caller responsible for resulting string memory (use OICFree to remove it).
 *
 * @return encoded CRL with base64 format. NULL if error occured (e.g. CRL did not set).
 */
char* GetBase64CRL();
/**
 * This function get encoded with DER CRL from SRM.
 *
 * @return encoded CRL with DER format. array len is 0 if error occured (e.g. CRL did not set).
 */
void  GetDerCrl(ByteArray crlArray);

/**
 * This function get CRL from SRM.
 *
 * @param crl is a pointer to buffer that contains crl. Shoul be not NULL. Buffer
 * will be allocated by the function and content of *crl will be ignored.
 * @param outlen is a pointer to length of the CRL buffer. Should be not NULL.
 *
 * @note Caller responsible for crl buffer memory (use OICFree to free it).
 *
 * @return ::OC_STACK_OK if success, otherwise some error value.
 */
OicSecCrl_t * JSONToCrlBin(const char * jsonStr);

/**
 * Initialize CRL resource by loading data from persistent storage.
 *
 * @return ::OC_STACK_OK for Success, otherwise some error value.
 */
OCStackResult InitCRLResource();

/**
 * Perform cleanup for CRL resources.
 */
void DeInitCRLResource();

/**
 * Get an instance of CRL resource.
 *
 * @return reference to the @ref OicSecCrl_t, holding reference to CRL resource.
 */
OicSecCrl_t *GetCRLResource();

/**
 * This function converts crl data into CBOR format.
 * Caller needs to invoke 'free' when done using returned string.
 *
 * @param crl is the pointer to instance of OicSecCrl_t structure.
 * @param cborPayload is the CBOR converted value.
 * @param cborSize is the size of the CBOR.
 *
 * @return ::OC_STACK_OK if conversion is successful, else ::OC_STACK_ERROR if unsuccessful.
 */
OCStackResult CrlToCBORPayload(const OicSecCrl_t* crl, uint8_t **cborPayload,
                                size_t *cborSize);

#ifdef __cplusplus
}
#endif

#endif //IOTVT_SRM_CRLR_H

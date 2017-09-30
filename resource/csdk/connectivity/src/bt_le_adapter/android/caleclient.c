/******************************************************************
 *
 * Copyright 2014 Samsung Electronics All Rights Reserved.
 *
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ******************************************************************/

#include <stdio.h>
#include <string.h>
#include <jni.h>
#include <unistd.h>

#include "caleclient.h"
#include "caleserver.h"
#include "caleutils.h"
#include "cainterface.h"
#include "cagattservice.h"
#include "caleinterface.h"
#include "caadapterutils.h"

#include "logger.h"
#include "oic_malloc.h"
#include "oic_string.h"
#include "cathreadpool.h" /* for thread pool */
#include "camutex.h"
#include "uarraylist.h"
#include "org_iotivity_ca_CaLeClientInterface.h"

#define TAG PCF("OIC_CA_LE_CLIENT")

#define MICROSECS_PER_SEC 1000000
#define WAIT_TIME_WRITE_CHARACTERISTIC 10 * MICROSECS_PER_SEC

/** 10 seconds: Time to wait for bond state changed callback */
#define WAIT_TIME_BOND 10 * MICROSECS_PER_SEC
/** 1 second: Time to wait after bonding to discover services */
#define WAIT_TIME_DISCOVER_SERVICES 1 * MICROSECS_PER_SEC

#define GATT_CONNECTION_PRIORITY_BALANCED   0
#define GATT_FAILURE                        257
#define GATT_INSUFFICIENT_AUTHENTICATION    5
#define GATT_INSUFFICIENT_ENCRYPTION        15
#define GATT_INVALID_ATTRIBUTE_LENGTH       13
#define GATT_INVALID_OFFSET                 7
#define GATT_READ_NOT_PERMITTED             2
#define GATT_REQUEST_NOT_SUPPORTED          6
#define GATT_WRITE_NOT_PERMITTED            3

static ca_thread_pool_t g_threadPoolHandle = NULL;

JavaVM *g_jvm;
static u_arraylist_t *g_deviceList = NULL; // device list to have same UUID
static u_arraylist_t *g_gattObjectList = NULL;
static u_arraylist_t *g_deviceStateList = NULL;

/** 
 * This is a list of response characteristics that have had their CCCD set.
 * the items in the list (know as keys in the code) are a string concatenation
 * of the device address and the characteristic uuid. 
 */
static u_arraylist_t *g_responseCharList = NULL;

static CAPacketReceiveCallback g_packetReceiveCallback = NULL;
static CABLEErrorHandleCallback g_clientErrorCallback;
static jobject g_leScanCallback = NULL;
static jobject g_leGattCallback = NULL;
static jobject g_context = NULL;
static jobjectArray g_uuidList = NULL;

// it will be prevent to start send logic when adapter has stopped.
static bool g_isStartedLEClient = false;
static bool g_isStartedScan = false;

static jbyteArray g_sendBuffer = NULL;
static uint32_t g_targetCnt = 0;
static uint32_t g_currentSentCnt = 0;
static bool g_isFinishedSendData = false;
static ca_mutex g_SendFinishMutex = NULL;
static ca_mutex g_threadMutex = NULL;
static ca_cond g_threadCond = NULL;
static ca_cond g_deviceDescCond = NULL;

static ca_mutex g_threadSendMutex = NULL;
static ca_mutex g_threadWriteCharacteristicMutex = NULL;
static ca_cond g_threadWriteCharacteristicCond = NULL;
static bool g_isSignalSetFlag = false;

static ca_mutex g_bleReqRespClientCbMutex = NULL;
static ca_mutex g_bleServerBDAddressMutex = NULL;

static ca_mutex g_deviceListMutex = NULL;
static ca_mutex g_gattObjectMutex = NULL;
static ca_mutex g_deviceStateListMutex = NULL;

static ca_mutex g_deviceScanRetryDelayMutex = NULL;
static ca_cond g_deviceScanRetryDelayCond = NULL;

static ca_mutex g_scanMutex = NULL;
static ca_mutex g_threadSendStateMutex = NULL;

// Mutex and condition for Writing CCCD before writing characteristic
static ca_mutex g_writeDescMutex = NULL;
static ca_cond g_writeDescCond = NULL;

// Mutexes and conditions for bonding
static ca_mutex g_bondMutex = NULL;
static ca_cond g_bondCond = NULL;
static ca_cond g_removeBondCond = NULL;

// Mutex for response characteristics list
static ca_mutex g_responseCharMutex = NULL;

// Mutex for waiting before discovering services
static ca_mutex g_discoverServicesDelayMutex = NULL;
static ca_cond g_discoverServicesDelayCond = NULL;

static CABLEDataReceivedCallback g_CABLEClientDataReceivedCallback = NULL;

// Store java CaLeClientInterface class in order to call removeBond
static jclass g_caLeClientInterface = NULL;

/**
 * check if retry logic for connection routine has to be stopped or not.
 * in case of error value including this method, connection routine has to be stopped.
 * since there is no retry logic for this error reason in this client.
 * @param state constant value of bluetoothgatt.
 * @return true - waiting for background connection in BT platform.
 *         false - connection routine has to be stopped.
 */
static bool CALECheckConnectionStateValue(jint state)
{
    switch(state)
    {
        case GATT_CONNECTION_PRIORITY_BALANCED:
        case GATT_FAILURE:
        case GATT_INSUFFICIENT_AUTHENTICATION:
        case GATT_INSUFFICIENT_ENCRYPTION:
        case GATT_INVALID_ATTRIBUTE_LENGTH:
        case GATT_INVALID_OFFSET:
        case GATT_READ_NOT_PERMITTED:
        case GATT_REQUEST_NOT_SUPPORTED:
        case GATT_WRITE_NOT_PERMITTED:
            return true;
    }
    return false;
}

//getting jvm
void CALEClientJniInit()
{
    OIC_LOG(DEBUG, TAG, "CALEClientJniInit");
    g_jvm = (JavaVM*) CANativeJNIGetJavaVM();
}

void CALEClientJNISetContext()
{
    OIC_LOG(DEBUG, TAG, "CALEClientJNISetContext");
    g_context = (jobject) CANativeJNIGetContext();
}

CAResult_t CALECreateJniInterfaceObject()
{
    OIC_LOG(DEBUG, TAG, "CALECreateJniInterfaceObject");

    if (!g_context)
    {
        OIC_LOG(ERROR, TAG, "g_context is null");
        return CA_STATUS_FAILED;
    }

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    jmethodID mid_getApplicationContext = CAGetJNIMethodID(env, "android/content/Context",
                                                           "getApplicationContext",
                                                           "()Landroid/content/Context;");

    if (!mid_getApplicationContext)
    {
        OIC_LOG(ERROR, TAG, "Could not get getApplicationContext method");
        return CA_STATUS_FAILED;
    }

    jobject jApplicationContext = (*env)->CallObjectMethod(env, g_context,
                                                           mid_getApplicationContext);
    if (!jApplicationContext)
    {
        OIC_LOG(ERROR, TAG, "Could not get application context");
        return CA_STATUS_FAILED;
    }

    jclass jni_LEInterface = (*env)->FindClass(env, "org/iotivity/ca/CaLeClientInterface");
    if (!jni_LEInterface)
    {
        OIC_LOG(ERROR, TAG, "Could not get CaLeClientInterface class");
        goto error_exit;
    } else {
        g_caLeClientInterface = (jclass) (*env)->NewGlobalRef(env, jni_LEInterface);
    }

    jmethodID LeInterfaceConstructorMethod = (*env)->GetMethodID(env, jni_LEInterface, "<init>",
                                                                 "(Landroid/content/Context;)V");
    if (!LeInterfaceConstructorMethod)
    {
        OIC_LOG(ERROR, TAG, "Could not get CaLeClientInterface constructor method");
        goto error_exit;
    }

    (*env)->NewObject(env, jni_LEInterface, LeInterfaceConstructorMethod, jApplicationContext);
    OIC_LOG(DEBUG, TAG, "Create instance for CaLeClientInterface");

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return CA_STATUS_OK;

error_exit:

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return CA_STATUS_FAILED;
}

CAResult_t CALEClientInitialize()
{
    OIC_LOG(DEBUG, TAG, "CALEClientInitialize");

    CALEClientJniInit();

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    CAResult_t ret = CALECheckPlatformVersion(env, 18);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "it is not supported");

        if (isAttached)
        {
            (*g_jvm)->DetachCurrentThread(g_jvm);
        }

        return ret;
    }

    ret = CALEClientInitGattMutexVaraibles();
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientInitGattMutexVaraibles has failed!");
        CALEClientTerminateGattMutexVariables();

        if (isAttached)
        {
            (*g_jvm)->DetachCurrentThread(g_jvm);
        }

        return ret;
    }

    g_deviceDescCond = ca_cond_new();

    // init mutex for send logic
    g_threadCond = ca_cond_new();
    g_threadWriteCharacteristicCond = ca_cond_new();
    g_deviceScanRetryDelayCond = ca_cond_new();

    CALEClientCreateDeviceList();
    CALEClientCreateResponseCharList(env);
    CALEClientJNISetContext();

#ifdef LE_ADAPTER
    // Set the default value for g_gattServiceUUID and characteristics
    if (!g_gattServiceUUID[0]) {
        CALESetServiceUUID(CA_DEFAULT_GATT_SERVICE_UUID); 
    }
    if (!g_gattRequestCharacteristicUUID[0]) {
        CALESetRequestCharacteristicUUID(CA_DEFAULT_GATT_REQUEST_CHRC_UUID); 
    }
    if (!g_gattResponseCharacteristicUUID[0]) {
        CALESetResponseCharacteristicUUID(CA_DEFAULT_GATT_RESPONSE_CHRC_UUID); 
    }
#endif

    ret = CALEClientCreateUUIDList();
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientCreateUUIDList has failed");

        if (isAttached)
        {
            (*g_jvm)->DetachCurrentThread(g_jvm);
        }

        return ret;
    }

    ret = CALECreateJniInterfaceObject(); /* create java caleinterface instance*/
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALECreateJniInterfaceObject has failed");

        if (isAttached)
        {
            (*g_jvm)->DetachCurrentThread(g_jvm);
        }

        return ret;
    }
    g_isStartedLEClient = true;

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return CA_STATUS_OK;
}

void CALEClientTerminate()
{
    OIC_LOG(DEBUG, TAG, "CALEClientTerminate");

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return;
        }
        isAttached = true;
    }

    if (g_leScanCallback)
    {
        (*env)->DeleteGlobalRef(env, g_leScanCallback);
        g_leScanCallback = NULL;
    }

    if (g_leGattCallback)
    {
        (*env)->DeleteGlobalRef(env, g_leGattCallback);
        g_leGattCallback = NULL;
    }

    if (g_sendBuffer)
    {
        (*env)->DeleteGlobalRef(env, g_sendBuffer);
        g_sendBuffer = NULL;
    }

    if (g_uuidList)
    {
        (*env)->DeleteGlobalRef(env, g_uuidList);
        g_uuidList = NULL;
    }

    CAResult_t ret = CALEClientRemoveAllDeviceState();
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientRemoveAllDeviceState has failed");
    }

    ret = CALEClientRemoveAllScanDevices(env);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientRemoveAllScanDevices has failed");
    }

    ret = CALEClientRemoveAllGattObjs(env);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientRemoveAllGattObjs has failed");
    }

    CALEClientSetScanFlag(false);
    CALEClientSetSendFinishFlag(true);

    CALEClientTerminateGattMutexVariables();
    CALEClientDestroyJniInterface();

    ca_cond_free(g_deviceDescCond);
    ca_cond_free(g_threadCond);
    ca_cond_free(g_threadWriteCharacteristicCond);
    ca_cond_free(g_deviceScanRetryDelayCond);

    g_deviceDescCond = NULL;
    g_threadCond = NULL;
    g_threadWriteCharacteristicCond = NULL;
    g_deviceScanRetryDelayCond = NULL;

    g_isSignalSetFlag = false;

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }
}

CAResult_t CALEClientDestroyJniInterface()
{
    OIC_LOG(DEBUG, TAG, "CALEClientDestroyJniInterface");

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    jclass jni_LeInterface = (*env)->FindClass(env, "org/iotivity/ca/CaLeClientInterface");
    if (!jni_LeInterface)
    {
        OIC_LOG(ERROR, TAG, "Could not get CaLeClientInterface class");
        goto error_exit;
    }

    jmethodID jni_InterfaceDestroyMethod = (*env)->GetStaticMethodID(env, jni_LeInterface,
                                                                     "destroyLeInterface",
                                                                     "()V");
    if (!jni_InterfaceDestroyMethod)
    {
        OIC_LOG(ERROR, TAG, "Could not get CaLeClientInterface destroy method");
        goto error_exit;
    }

    (*env)->CallStaticVoidMethod(env, jni_LeInterface, jni_InterfaceDestroyMethod);

    if ((*env)->ExceptionCheck(env))
    {
        OIC_LOG(ERROR, TAG, "destroyLeInterface has failed");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        goto error_exit;
    } else {
        (*env)->DeleteGlobalRef(env, g_caLeClientInterface);
        g_caLeClientInterface = NULL;
    }

    OIC_LOG(DEBUG, TAG, "Destroy instance for CaLeClientInterface");

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return CA_STATUS_OK;

error_exit:

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return CA_STATUS_FAILED;
}

void CALEClientSendFinish(JNIEnv *env, jobject gatt)
{
    OIC_LOG(DEBUG, TAG, "CALEClientSendFinish");
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");

    if (gatt)
    {
        // Remove bond from device if bonded 
        jobject bluetoothDevice = CALEClientGetDeviceFromGatt(env, gatt);
        if (!bluetoothDevice) {
            OIC_LOG(ERROR, TAG, "bluetootDevice is null!");
        } else if (BOND_BONDED == CALEClientGetDeviceBondState(env, bluetoothDevice)) {
            bool bondRemoved = CALEClientRemoveBond(env, bluetoothDevice);
            if (bondRemoved) {
                OIC_LOG(DEBUG, TAG, "Successfully removed bond from device");
            } else {
                OIC_LOG(ERROR, TAG, "Bond remove failed!");
            }
        }

        CAResult_t res = CALEClientDisconnect(env, gatt);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientDisconnect has failed");
        }
    }
    CALEClientUpdateSendCnt(env);
}

CAResult_t CALEClientSendNegotiationMessage(const char* address)
{
    OIC_LOG_V(DEBUG, TAG, "CALEClientSendNegotiationMessage(%s)", address);
    VERIFY_NON_NULL(address, TAG, "address is null");

    return CALEClientSendUnicastMessageImpl(address, NULL, 0);
}

CAResult_t CALEClientSendUnicastMessage(const char* address,
                                        const uint8_t* data,
                                        const uint32_t dataLen)
{
    OIC_LOG_V(DEBUG, TAG, "CALEClientSendUnicastMessage(%s, %p)", address, data);
    VERIFY_NON_NULL(address, TAG, "address is null");
    VERIFY_NON_NULL(data, TAG, "data is null");

    return CALEClientSendUnicastMessageImpl(address, data, dataLen);
}

CAResult_t CALEClientSendMulticastMessage(const uint8_t* data,
                                          const uint32_t dataLen)
{
    OIC_LOG_V(DEBUG, TAG, "CALEClientSendMulticastMessage(%p)", data);
    VERIFY_NON_NULL(data, TAG, "data is null");

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    CAResult_t ret = CALEClientSendMulticastMessageImpl(env, data, dataLen);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientSendMulticastMessageImpl has failed");
    }

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return ret;
}

CAResult_t CALEClientStartUnicastServer(const char* address)
{
    OIC_LOG_V(DEBUG, TAG, "it is not needed in this platform (%s)", address);

    return CA_NOT_SUPPORTED;
}

CAResult_t CALEClientStartMulticastServer()
{
    OIC_LOG(DEBUG, TAG, "it is not needed in this platform");

    return CA_NOT_SUPPORTED;
}

void CALEClientStopUnicastServer()
{
    OIC_LOG(DEBUG, TAG, "CALEClientStopUnicastServer");
}

void CALEClientStopMulticastServer()
{
    OIC_LOG(DEBUG, TAG, "CALEClientStopMulticastServer");
}

void CALEClientSetCallback(CAPacketReceiveCallback callback)
{
    g_packetReceiveCallback = callback;
}

void CASetBLEClientErrorHandleCallback(CABLEErrorHandleCallback callback)
{
    g_clientErrorCallback = callback;
}

CAResult_t CALEClientIsThereScannedDevices(JNIEnv *env, const char* address)
{
    VERIFY_NON_NULL(env, TAG, "env");

    if (!g_deviceList)
    {
        return CA_STATUS_FAILED;
    }

    if (0 == u_arraylist_length(g_deviceList) // multicast
            || (address && !CALEClientIsDeviceInScanDeviceList(env, address))) // unicast
    {
        // Wait for LE peripherals to be discovered.

        // Number of times to wait for discovery to complete.
        static size_t const RETRIES = 5;

        static uint64_t const TIMEOUT =
            2 * MICROSECS_PER_SEC;  // Microseconds

        bool devicesDiscovered = false;
        for (size_t i = 0; i < RETRIES; ++i)
        {
            OIC_LOG(DEBUG, TAG, "waiting for target device");
            if (ca_cond_wait_for(g_deviceDescCond,
                                 g_threadSendMutex,
                                 TIMEOUT) == CA_WAIT_SUCCESS)
            {
                ca_mutex_lock(g_deviceListMutex);
                size_t scannedDeviceLen = u_arraylist_length(g_deviceList);
                ca_mutex_unlock(g_deviceListMutex);

                if (0 < scannedDeviceLen)
                {
                    if (!address  // multicast
                        || (address && CALEClientIsDeviceInScanDeviceList(env, address))) // unicast
                    {
                      devicesDiscovered = true;
                      break;
                    }
                    else
                    {
                        if (address)
                        {
                            OIC_LOG(INFO, TAG, "waiting..");

                            ca_mutex_lock(g_deviceScanRetryDelayMutex);
                            if (ca_cond_wait_for(g_deviceScanRetryDelayCond,
                                                 g_deviceScanRetryDelayMutex,
                                                 MICROSECS_PER_SEC) == CA_WAIT_SUCCESS)
                            {
                                OIC_LOG(INFO, TAG, "finish to waiting for target device");
                                ca_mutex_unlock(g_deviceScanRetryDelayMutex);
                                break;
                            }
                            ca_mutex_unlock(g_deviceScanRetryDelayMutex);
                            // time out

                            // checking whether a target device is found while waiting for time-out.
                            if (CALEClientIsDeviceInScanDeviceList(env, address))
                            {
                                devicesDiscovered = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // time out for scanning devices
        if (!devicesDiscovered)
        {
            return CA_STATUS_FAILED;
        }
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientSendUnicastMessageImpl(const char* address, const uint8_t* data,
                                      const uint32_t dataLen)
{
    OIC_LOG_V(DEBUG, TAG, "CALEClientSendUnicastMessageImpl, address: %s, data: ", address);
    OIC_LOG_BUFFER(DEBUG, TAG, data, dataLen); 
    VERIFY_NON_NULL(address, TAG, "address is null");

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    ca_mutex_lock(g_threadSendMutex);

    CALEClientSetSendFinishFlag(false);

    CAResult_t ret = CALEClientIsThereScannedDevices(env, address);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(INFO, TAG, "there are no scanned devices");
        goto error_exit;
    }

    if (g_context && g_deviceList)
    {
        uint32_t length = u_arraylist_length(g_deviceList);
        for (uint32_t index = 0; index < length; index++)
        {
            jobject jarrayObj = (jobject) u_arraylist_get(g_deviceList, index);
            if (!jarrayObj)
            {
                OIC_LOG(ERROR, TAG, "jarrayObj is null");
                goto error_exit;
            }

            jstring jni_setAddress = CALEGetAddressFromBTDevice(env, jarrayObj);
            if (!jni_setAddress)
            {
                OIC_LOG(ERROR, TAG, "jni_setAddress is null");
                goto error_exit;
            }

            const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
            if (!setAddress)
            {
                OIC_LOG(ERROR, TAG, "setAddress is null");
                goto error_exit;
            }

            OIC_LOG_V(DEBUG, TAG, "remote device address is %s", setAddress);
            // Check if target address and list device address match 
            if (!strcmp(setAddress, address))
            {
                (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
                (*env)->DeleteLocalRef(env, jni_setAddress);
                
                // Stop scanning
                ret = CALEClientStopScan();
                if (CA_STATUS_OK != ret)
                {
                    OIC_LOG(ERROR, TAG, "CALEClientStopScan has failed");
                    goto error_exit;
                }

                if (g_sendBuffer)
                {
                    (*env)->DeleteGlobalRef(env, g_sendBuffer);
                    g_sendBuffer = NULL;
                }

                if (data && dataLen > 0) {
                    // Set data in byte array
                    jbyteArray jni_arr = (*env)->NewByteArray(env, dataLen);
                    CACheckJNIException(env);
                    (*env)->SetByteArrayRegion(env, jni_arr, 0, dataLen, (jbyte*) data);
                    CACheckJNIException(env);
                    g_sendBuffer = (jbyteArray)(*env)->NewGlobalRef(env, jni_arr);
                    CACheckJNIException(env);
                }

                // Target device to send message is just one.
                g_targetCnt = 1;
                
                // Send data
                ret = CALEClientSendData(env, jarrayObj);
                if (CA_STATUS_OK != ret)
                {
                    OIC_LOG(ERROR, TAG, "CALEClientSendData in unicast is failed");
                    goto error_exit;
                }

                OIC_LOG(INFO, TAG, "wake up");
                break;
            }
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            (*env)->DeleteLocalRef(env, jni_setAddress);
        }
    }

    OIC_LOG(DEBUG, TAG, "connection routine is finished for unicast");

    // wait for finish to send data through "CALeGattServicesDiscoveredCallback"
    // if there is no connection state.
    ca_mutex_lock(g_threadMutex);
    if (!g_isFinishedSendData)
    {
        OIC_LOG(DEBUG, TAG, "waiting send finish signal");
        ca_cond_wait(g_threadCond, g_threadMutex);
        OIC_LOG(DEBUG, TAG, "the data was sent");
    }
    ca_mutex_unlock(g_threadMutex);

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    // start LE Scan again
    ret = CALEClientStartScan();
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientStartScan has failed");
        ca_mutex_unlock(g_threadSendMutex);
        return ret;
    }

    ca_mutex_unlock(g_threadSendMutex);
    OIC_LOG(INFO, TAG, "unicast - send logic has finished");
    if (CALEClientIsValidState(address, CA_LE_SEND_STATE,
                               STATE_SEND_SUCCESS))
    {
        ret = CA_STATUS_OK;
    }
    // New MTU Negotiation 
    else if (CALEClientIsValidState(address, CA_LE_SEND_STATE,
                                    STATE_SEND_MTU_NEGO_SUCCESS))
    {
        OIC_LOG(INFO, TAG, "mtu nego success");
        ret = CA_STATUS_OK;
    }
    else
    {
        ret = CA_SEND_FAILED;
    }

    // reset send state
    ret = CALEClientUpdateDeviceState(address, CA_LE_SEND_STATE,
                                      STATE_SEND_NONE);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
    }

    return ret;

    // error label.
error_exit:

    // start LE Scan again
    ret = CALEClientStartScan();
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientStartScan has failed");
        ca_mutex_unlock(g_threadSendMutex);
        if (isAttached)
        {
            (*g_jvm)->DetachCurrentThread(g_jvm);
        }
        return ret;
    }

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    ca_mutex_unlock(g_threadSendMutex);
    return CA_SEND_FAILED;
}

CAResult_t CALEClientSendMulticastMessageImpl(JNIEnv *env, const uint8_t* data,
                                              const uint32_t dataLen)
{
    OIC_LOG_V(DEBUG, TAG, "CASendMulticastMessageImpl, send to, data: %p, %u", data, dataLen);
    VERIFY_NON_NULL(data, TAG, "data is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    if (!g_deviceList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceList is null");
        return CA_STATUS_FAILED;
    }

    ca_mutex_lock(g_threadSendMutex);

    CALEClientSetSendFinishFlag(false);

    OIC_LOG(DEBUG, TAG, "set byteArray for data");
    if (g_sendBuffer)
    {
        (*env)->DeleteGlobalRef(env, g_sendBuffer);
        g_sendBuffer = NULL;
    }

    CAResult_t res = CALEClientIsThereScannedDevices(env, NULL);
    if (CA_STATUS_OK != res)
    {
        OIC_LOG(INFO, TAG, "there is no scanned device");
        goto error_exit;
    }

    // connect to gatt server
    res = CALEClientStopScan();
    if (CA_STATUS_OK != res)
    {
        OIC_LOG(ERROR, TAG, "CALEClientStopScan has failed");
        ca_mutex_unlock(g_threadSendMutex);
        return res;
    }
    uint32_t length = u_arraylist_length(g_deviceList);
    g_targetCnt = length;

    jbyteArray jni_arr = (*env)->NewByteArray(env, dataLen);
    (*env)->SetByteArrayRegion(env, jni_arr, 0, dataLen, (jbyte*) data);
    g_sendBuffer = (jbyteArray)(*env)->NewGlobalRef(env, jni_arr);

    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_deviceList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is not available");
            continue;
        }

        res = CALEClientSendData(env, jarrayObj);
        if (res != CA_STATUS_OK)
        {
            OIC_LOG(ERROR, TAG, "BT device - send has failed");
        }
/*
        jstring jni_address = CALEGetAddressFromBTDevice(env, jarrayObj);
        if (!jni_address)
        {
            OIC_LOG(ERROR, TAG, "CALEGetAddressFromBTDevice has failed");
            continue;
        }

        const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
        if (!address)
        {
            OIC_LOG(ERROR, TAG, "address is not available");
            continue;
        }

        (*env)->ReleaseStringUTFChars(env, jni_address, address);
        */
    }

    OIC_LOG(DEBUG, TAG, "connection routine is finished for multicast");

    // wait for finish to send data through "CALeGattServicesDiscoveredCallback"
    ca_mutex_lock(g_threadMutex);
    if (!g_isFinishedSendData)
    {
        OIC_LOG(DEBUG, TAG, "waiting send finish signal");
        ca_cond_wait(g_threadCond, g_threadMutex);
        OIC_LOG(DEBUG, TAG, "the data was sent");
    }
    ca_mutex_unlock(g_threadMutex);

    // start LE Scan again
    res = CALEClientStartScan();
    if (CA_STATUS_OK != res)
    {
        OIC_LOG(ERROR, TAG, "CALEClientStartScan has failed");
        ca_mutex_unlock(g_threadSendMutex);
        return res;
    }

    ca_mutex_unlock(g_threadSendMutex);
    OIC_LOG(DEBUG, TAG, "OUT - CALEClientSendMulticastMessageImpl");
    return CA_STATUS_OK;

error_exit:
    res = CALEClientStartScan();
    if (CA_STATUS_OK != res)
    {
        OIC_LOG(ERROR, TAG, "CALEClientStartScan has failed");
        ca_mutex_unlock(g_threadSendMutex);
        return res;
    }

    ca_mutex_unlock(g_threadSendMutex);
    OIC_LOG(DEBUG, TAG, "OUT - CALEClientSendMulticastMessageImpl");
    return CA_SEND_FAILED;
}

CAResult_t CALEClientSendData(JNIEnv *env, jobject device)
{
    OIC_LOG(DEBUG, TAG, "IN - CALEClientSendData");
    VERIFY_NON_NULL(device, TAG, "device is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    // get BLE address from bluetooth device object.
    char* address = NULL;
    CALEState_t* state = NULL;
    jstring jni_address = CALEClientGetLEAddressFromBTDevice(env, device);
    if (jni_address)
    {
        OIC_LOG(INFO, TAG, "there is gatt object..it's not first connection");
        address = (char*)(*env)->GetStringUTFChars(env, jni_address, NULL);
        if (!address)
        {
            OIC_LOG(ERROR, TAG, "address is not available");
            return CA_STATUS_FAILED;
        }
        ca_mutex_lock(g_deviceStateListMutex);
        state = CALEClientGetStateInfo(address);
        ca_mutex_unlock(g_deviceStateListMutex);
    }

    if (!state)
    {
        OIC_LOG(DEBUG, TAG, "state is empty..start to connect LE");

        // cancel previous connection request before connection
        // if there is gatt object in g_gattObjectList.
        if (jni_address)
        {
            jobject gatt = CALEClientGetGattObjInList(env, address);
            if (gatt)
            {
                CAResult_t res = CALEClientDisconnect(env, gatt);
                if (CA_STATUS_OK != res)
                {
                    OIC_LOG(INFO, TAG, "there is no gatt object");
                }
            }
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
        }

        // connection request
        jobject newGatt = CALEClientConnect(env, device,
                                            JNI_FALSE);
        if (NULL == newGatt)
        {
            OIC_LOG(ERROR, TAG, "CALEClientConnect has failed");
            return CA_STATUS_FAILED;
        }
    }
    else
    {
        if (CALEClientIsValidState(address, CA_LE_CONNECTION_STATE,
                                   STATE_SERVICE_CONNECTED))
        {
            OIC_LOG(INFO, TAG, "GATT has already connected");

            jobject gatt = CALEClientGetGattObjInList(env, address);
            if (!gatt)
            {
                OIC_LOG(ERROR, TAG, "CALEClientGetGattObjInList has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                return CA_STATUS_FAILED;
            }
            // Set value and write characteristic
            CAResult_t ret = CALESetValueAndWriteCharacteristic(env, gatt);

            // WriteCharacteristic on new thread
            //CAResult_t ret = CALEClientWriteCharacteristic(env, gatt);
            if (CA_STATUS_OK != ret)
            {
                OIC_LOG(ERROR, TAG, "CALEClientSetValueAndWriteCharacteristic has failed");
                //OIC_LOG(ERROR, TAG, "CALEClientWriteCharacteristic has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                return ret;
            }
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
        }
        else if(CALEClientIsValidState(address, CA_LE_CONNECTION_STATE,
                                       STATE_CONNECTED))
        {
            OIC_LOG(INFO, TAG, "service connecting...");
        }
        else if(CALEClientIsValidState(address, CA_LE_CONNECTION_STATE,
                                       STATE_DISCONNECTED))
        {
            OIC_LOG(INFO, TAG, "STATE_DISCONNECTED - start to connect LE");

            // cancel previous connection request before connection
            // if there is gatt object in g_gattObjectList.
            if (jni_address)
            {
                jobject gatt = CALEClientGetGattObjInList(env, address);
                if (gatt)
                {
                    CAResult_t res = CALEClientDisconnect(env, gatt);
                    if (CA_STATUS_OK != res)
                    {
                        OIC_LOG(INFO, TAG, "there is no gatt object");
                    }
                }
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
            }

            jobject gatt = CALEClientConnect(env, device,
                                             CALEClientGetFlagFromState(env, jni_address,
                                             CA_LE_AUTO_CONNECT_FLAG));
            if (NULL == gatt)
            {
                OIC_LOG(ERROR, TAG, "CALEClientConnect has failed");
                return CA_STATUS_FAILED;
            }
        }
    }

    return CA_STATUS_OK;
}

jstring CALEClientGetAddressFromGattObj(JNIEnv *env, jobject gatt)
{
    VERIFY_NON_NULL_RET(gatt, TAG, "gatt is null", NULL);
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);

    
    jobject jni_obj_device = CALEClientGetDeviceFromGatt(env, gatt);
    if (!jni_obj_device) {
        OIC_LOG(ERROR, TAG, "jni_obj_device is null");
        return NULL;
    }

    jstring jni_address = CALEGetAddressFromBTDevice(env, jni_obj_device);
    if (!jni_address)
    {
        OIC_LOG(ERROR, TAG, "jni_address is null");
        return NULL;
    }

    return jni_address;
}

jobject CALEClientGetDeviceFromGatt(JNIEnv *env, jobject gatt) 
{
    VERIFY_NON_NULL_RET(gatt, TAG, "gatt is null", NULL);
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);

    jmethodID jni_mid_getDevice = CAGetJNIMethodID(env, CLASSPATH_BT_GATT, "getDevice",
                                                   "()Landroid/bluetooth/BluetoothDevice;");
    if (!jni_mid_getDevice)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDevice is null");
        return NULL;
    }

    jobject jni_obj_device = (*env)->CallObjectMethod(env, gatt, jni_mid_getDevice);
    if (!jni_obj_device)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_device is null");
        return NULL;
    }
    return jni_obj_device;
}

/**
 * BLE layer
 */
CAResult_t CALEClientGattClose(JNIEnv *env, jobject bluetoothGatt)
{
    // GATT CLOSE
    OIC_LOG(DEBUG, TAG, "Gatt Close");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    // get BluetoothGatt method
    jmethodID jni_mid_closeGatt = CAGetJNIMethodID(env, CLASSPATH_BT_GATT, "close", "()V");
    if (!jni_mid_closeGatt)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_closeGatt is null");
        return CA_STATUS_OK;
    }

    // call close gatt method
    (*env)->CallVoidMethod(env, bluetoothGatt, jni_mid_closeGatt);

    if ((*env)->ExceptionCheck(env))
    {
        OIC_LOG(ERROR, TAG, "closeGATT has failed");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientStartScan()
{
    if (!g_isStartedLEClient)
    {
        OIC_LOG(ERROR, TAG, "LE client is not started");
        return CA_STATUS_FAILED;
    }

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    if (g_isStartedScan)
    {
        OIC_LOG(INFO, TAG, "scanning is already started");
        return CA_STATUS_OK;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");

        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    OIC_LOG(DEBUG, TAG, "CALEClientStartScan");

    CAResult_t ret = CA_STATUS_OK;
    // scan gatt server with UUID
    if (g_leScanCallback && g_uuidList)
    {
#ifdef UUID_SCAN
        ret = CALEClientStartScanWithUUIDImpl(env, g_uuidList, g_leScanCallback);
#else
        ret = CALEClientStartScanImpl(env, g_leScanCallback);
#endif
        if (CA_STATUS_OK != ret)
        {
            if (CA_ADAPTER_NOT_ENABLED == ret)
            {
                OIC_LOG(DEBUG, TAG, "Adapter is disabled");
            }
            else
            {
                OIC_LOG(ERROR, TAG, "start scan has failed");
            }
        }
    }

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return ret;
}

CAResult_t CALEClientStartScanImpl(JNIEnv *env, jobject callback)
{
    VERIFY_NON_NULL(callback, TAG, "callback is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_ADAPTER_NOT_ENABLED;
    }

    // get default bt adapter class
    jclass jni_cid_BTAdapter = (*env)->FindClass(env, CLASSPATH_BT_ADAPTER);
    if (!jni_cid_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "getState From BTAdapter: jni_cid_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get remote bt adapter method
    jmethodID jni_mid_getDefaultAdapter = (*env)->GetStaticMethodID(env, jni_cid_BTAdapter,
                                                                    "getDefaultAdapter",
                                                                    METHODID_OBJECTNONPARAM);
    if (!jni_mid_getDefaultAdapter)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDefaultAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get start le scan method
    jmethodID jni_mid_startLeScan = (*env)->GetMethodID(env, jni_cid_BTAdapter, "startLeScan",
                                                        "(Landroid/bluetooth/BluetoothAdapter$"
                                                        "LeScanCallback;)Z");
    if (!jni_mid_startLeScan)
    {
        OIC_LOG(ERROR, TAG, "startLeScan: jni_mid_startLeScan is null");
        return CA_STATUS_FAILED;
    }

    // gat bt adapter object
    jobject jni_obj_BTAdapter = (*env)->CallStaticObjectMethod(env, jni_cid_BTAdapter,
                                                               jni_mid_getDefaultAdapter);
    if (!jni_obj_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "getState From BTAdapter: jni_obj_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // call start le scan method
    jboolean jni_obj_startLeScan = (*env)->CallBooleanMethod(env, jni_obj_BTAdapter,
                                                             jni_mid_startLeScan, callback);
    if (!jni_obj_startLeScan)
    {
        OIC_LOG(INFO, TAG, "startLeScan is failed");
    }
    else
    {
        OIC_LOG(DEBUG, TAG, "startLeScan is started");
        CALEClientSetScanFlag(true);
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientStartScanWithUUIDImpl(JNIEnv *env, jobjectArray uuids, jobject callback)
{
    VERIFY_NON_NULL(callback, TAG, "callback is null");
    VERIFY_NON_NULL(uuids, TAG, "uuids is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_ADAPTER_NOT_ENABLED;
    }

    jclass jni_cid_BTAdapter = (*env)->FindClass(env, CLASSPATH_BT_ADAPTER);
    if (!jni_cid_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "getState From BTAdapter: jni_cid_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get remote bt adapter method
    jmethodID jni_mid_getDefaultAdapter = (*env)->GetStaticMethodID(env, jni_cid_BTAdapter,
                                                                    "getDefaultAdapter",
                                                                    METHODID_OBJECTNONPARAM);
    if (!jni_mid_getDefaultAdapter)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDefaultAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get start le scan method
    jmethodID jni_mid_startLeScan = (*env)->GetMethodID(env, jni_cid_BTAdapter, "startLeScan",
                                                        "([Ljava/util/UUID;Landroid/bluetooth/"
                                                        "BluetoothAdapter$LeScanCallback;)Z");
    if (!jni_mid_startLeScan)
    {
        OIC_LOG(ERROR, TAG, "startLeScan: jni_mid_startLeScan is null");
        return CA_STATUS_FAILED;
    }

    // get bt adapter object
    jobject jni_obj_BTAdapter = (*env)->CallStaticObjectMethod(env, jni_cid_BTAdapter,
                                                               jni_mid_getDefaultAdapter);
    if (!jni_obj_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "getState From BTAdapter: jni_obj_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // call start le scan method
    jboolean jni_obj_startLeScan = (*env)->CallBooleanMethod(env, jni_obj_BTAdapter,
                                                             jni_mid_startLeScan, uuids, callback);
    if (!jni_obj_startLeScan)
    {
        OIC_LOG(INFO, TAG, "startLeScan With UUID is failed");
    }
    else
    {
        OIC_LOG(DEBUG, TAG, "startLeScan With UUID is started");
        CALEClientSetScanFlag(true);
    }

    return CA_STATUS_OK;
}

jobject CALEClientGetUUIDObject(JNIEnv *env, const char* uuid)
{
    VERIFY_NON_NULL_RET(uuid, TAG, "uuid is null", NULL);
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);

    // setting UUID
    jclass jni_cid_uuid = (*env)->FindClass(env, CLASSPATH_BT_UUID);
    if (!jni_cid_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_uuid is null");
        return NULL;
    }

    jmethodID jni_mid_fromString = (*env)->GetStaticMethodID(env, jni_cid_uuid, "fromString",
                                                             "(Ljava/lang/String;)"
                                                             "Ljava/util/UUID;");
    if (!jni_mid_fromString)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_fromString is null");
        return NULL;
    }

    jstring jni_uuid = (*env)->NewStringUTF(env, uuid);
    jobject jni_obj_uuid = (*env)->CallStaticObjectMethod(env, jni_cid_uuid, jni_mid_fromString,
                                                          jni_uuid);
    if (!jni_obj_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_uuid is null");
        return NULL;
    }

    return jni_obj_uuid;
}

CAResult_t CALEClientStopScan()
{
    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    if (!g_isStartedScan)
    {
        OIC_LOG(INFO, TAG, "scanning is already stopped");
        return CA_STATUS_OK;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);
        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    CAResult_t ret = CALEClientStopScanImpl(env, g_leScanCallback);
    if (CA_STATUS_OK != ret)
    {
        if (CA_ADAPTER_NOT_ENABLED == ret)
        {
            OIC_LOG(DEBUG, TAG, "Adapter is disabled");
        }
        else
        {
            OIC_LOG(ERROR, TAG, "CALEClientStopScanImpl has failed");
        }
    }
    else
    {
        CALEClientSetScanFlag(false);
    }

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return ret;
}

void CALEClientSetScanFlag(bool flag)
{
    ca_mutex_lock(g_scanMutex);
    g_isStartedScan = flag;
    ca_mutex_unlock(g_scanMutex);
}

CAResult_t CALEClientStopScanImpl(JNIEnv *env, jobject callback)
{
    OIC_LOG(DEBUG, TAG, "CALEClientStopScanImpl");
    VERIFY_NON_NULL(callback, TAG, "callback is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_ADAPTER_NOT_ENABLED;
    }

    // get default bt adapter class
    jclass jni_cid_BTAdapter = (*env)->FindClass(env, CLASSPATH_BT_ADAPTER);
    if (!jni_cid_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "getState From BTAdapter: jni_cid_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get remote bt adapter method
    jmethodID jni_mid_getDefaultAdapter = (*env)->GetStaticMethodID(env, jni_cid_BTAdapter,
                                                                    "getDefaultAdapter",
                                                                    METHODID_OBJECTNONPARAM);
    if (!jni_mid_getDefaultAdapter)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDefaultAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get stop le scan method
    jmethodID jni_mid_stopLeScan = (*env)->GetMethodID(env, jni_cid_BTAdapter, "stopLeScan",
                                                       "(Landroid/bluetooth/"
                                                       "BluetoothAdapter$LeScanCallback;)V");
    if (!jni_mid_stopLeScan)
    {
        OIC_LOG(ERROR, TAG, "stopLeScan: jni_mid_stopLeScan is null");
        return CA_STATUS_FAILED;
    }

    // gatt bt adapter object
    jobject jni_obj_BTAdapter = (*env)->CallStaticObjectMethod(env, jni_cid_BTAdapter,
                                                               jni_mid_getDefaultAdapter);
    if (!jni_obj_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // call stop le scan method
    (*env)->CallVoidMethod(env, jni_obj_BTAdapter, jni_mid_stopLeScan, callback);
    if ((*env)->ExceptionCheck(env))
    {
        OIC_LOG(ERROR, TAG, "stopLeScan has failed");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientSetFlagToState(JNIEnv *env, jstring jni_address,
                                  jint state_idx, jboolean flag)
{
    OIC_LOG(DEBUG, TAG, "IN - CALEClientSetFlagToState");
    VERIFY_NON_NULL(env, TAG, "env");
    VERIFY_NON_NULL(jni_address, TAG, "jni_address");

    ca_mutex_lock(g_deviceStateListMutex);

    char* address = (char*)(*env)->GetStringUTFChars(env, jni_address, NULL);
    if (!address)
    {
        OIC_LOG(ERROR, TAG, "address is not available");
        return CA_STATUS_FAILED;
    }

    if (CALEClientIsDeviceInList(address))
    {
        CALEState_t* curState = CALEClientGetStateInfo(address);
        if(!curState)
        {
            OIC_LOG(ERROR, TAG, "curState is null");
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
            ca_mutex_unlock(g_deviceStateListMutex);
            return CA_STATUS_FAILED;
        }
        OIC_LOG_V(INFO, TAG, "%d flag is set : %d", state_idx, flag);

        switch(state_idx)
        {
            case CA_LE_AUTO_CONNECT_FLAG:
                curState->autoConnectFlag = flag;
                break;
            default:
                break;
        }
    }

    (*env)->ReleaseStringUTFChars(env, jni_address, address);
    ca_mutex_unlock(g_deviceStateListMutex);
    OIC_LOG(DEBUG, TAG, "OUT - CALEClientSetFlagToState");
    return CA_STATUS_OK;
}

jboolean CALEClientGetFlagFromState(JNIEnv *env, jstring jni_address, jint state_idx)
{
    OIC_LOG(DEBUG, TAG, "IN - CALEClientGetFlagFromState");
    VERIFY_NON_NULL_RET(env, TAG, "env", false);
    VERIFY_NON_NULL_RET(jni_address, TAG, "jni_address", false);

    ca_mutex_lock(g_deviceStateListMutex);

    char* address = (char*)(*env)->GetStringUTFChars(env, jni_address, NULL);
    if (!address)
    {
        OIC_LOG(ERROR, TAG, "address is not available");
        return JNI_FALSE;
    }

    CALEState_t* curState = CALEClientGetStateInfo(address);
    if(!curState)
    {
        OIC_LOG(INFO, TAG, "there is no information. auto connect flag is false");
        (*env)->ReleaseStringUTFChars(env, jni_address, address);
        ca_mutex_unlock(g_deviceStateListMutex);
        return JNI_FALSE;
    }

    jboolean ret = JNI_FALSE;
    switch(state_idx)
    {
        case CA_LE_AUTO_CONNECT_FLAG:
            ret = curState->autoConnectFlag;
            break;
        default:
            break;
    }
    OIC_LOG_V(INFO, TAG, "%d flag is %d", state_idx, ret);

    (*env)->ReleaseStringUTFChars(env, jni_address, address);
    ca_mutex_unlock(g_deviceStateListMutex);

    OIC_LOG(DEBUG, TAG, "OUT - CALEClientGetFlagFromState");
    return ret;
}

CAResult_t CALEClientDirectConnect(JNIEnv *env, jobject bluetoothDevice, jboolean autoconnect)
{
    OIC_LOG(DEBUG, TAG, "CALEClientDirectConnect");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothDevice, TAG, "bluetoothDevice is null");

    ca_mutex_lock(g_threadSendMutex);

    jstring jni_address = CALEGetAddressFromBTDevice(env, bluetoothDevice);
    if (!jni_address)
    {
        OIC_LOG(ERROR, TAG, "jni_address is not available");
        ca_mutex_unlock(g_threadSendMutex);
        return CA_STATUS_FAILED;
    }

    const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
    if (!address)
    {
        OIC_LOG(ERROR, TAG, "address is not available");
        ca_mutex_unlock(g_threadSendMutex);
        return CA_STATUS_FAILED;
    }

    CAResult_t res = CA_STATUS_OK;
    if(CALEClientIsValidState(address, CA_LE_CONNECTION_STATE,
                              STATE_DISCONNECTED))
    {
        jobject newGatt = CALEClientConnect(env, bluetoothDevice, autoconnect);
        if (NULL == newGatt)
        {
            OIC_LOG(INFO, TAG, "newGatt is not available");
            res = CA_STATUS_FAILED;
        }
    }
    ca_mutex_unlock(g_threadSendMutex);

    return res;
}

jobject CALEClientConnect(JNIEnv *env, jobject bluetoothDevice, jboolean autoconnect)
{
    OIC_LOG(DEBUG, TAG, "CALEClientConnect");
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);
    VERIFY_NON_NULL_RET(bluetoothDevice, TAG, "bluetoothDevice is null", NULL);

    // get gatt object from Bluetooth Device object for closeProfileProxy(..)
    jstring jni_address = CALEClientGetLEAddressFromBTDevice(env, bluetoothDevice);
    if (jni_address)
    {
        const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
        if (!address)
        {
            OIC_LOG(ERROR, TAG, "address is not available");
            return NULL;
        }

        // Get gatt object from list
        jobject gatt = CALEClientGetGattObjInList(env, address);
        if (gatt)
        {
            // If gatt object exists, close profile proxy
            CAResult_t res = CALEClientCloseProfileProxy(env, gatt);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientCloseProfileProxy has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                return NULL;
            }

            // clean previous gatt object after close profile service
            res = CALEClientRemoveGattObjForAddr(env, jni_address);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientRemoveGattObjForAddr has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                return NULL;
            }
        }

        (*env)->ReleaseStringUTFChars(env, jni_address, address);
    }

    // Client Connect Gatt
    jobject newGatt = CALEClientGattConnect(env, bluetoothDevice, autoconnect);
    if (!newGatt)
    {
        OIC_LOG(DEBUG, TAG, "re-connection will be started");
        return NULL;
    }

    // add new gatt object into g_gattObjectList
    CAResult_t res = CALEClientAddGattobjToList(env, newGatt);
    if (CA_STATUS_OK != res)
    {
        OIC_LOG(ERROR, TAG, "CALEClientAddGattobjToList has failed");
        return NULL;
    }

    return newGatt;
}

jobject CALEClientGattConnect(JNIEnv *env, jobject bluetoothDevice, jboolean autoconnect)
{
    OIC_LOG(DEBUG, TAG, "GATT CONNECT");
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);
    VERIFY_NON_NULL_RET(bluetoothDevice, TAG, "bluetoothDevice is null", NULL);

    if (!g_leGattCallback)
    {
        OIC_LOG(INFO, TAG, "g_leGattCallback is null");
        return NULL;
    }

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return NULL;
    }

    jstring jni_address = CALEGetAddressFromBTDevice(env, bluetoothDevice);
    if (!jni_address)
    {
        OIC_LOG(ERROR, TAG, "bleConnect: CALEGetAddressFromBTDevice is null");
        return NULL;
    }

    // get connectGatt method
    jmethodID jni_mid_connectGatt = CAGetJNIMethodID(env, "android/bluetooth/BluetoothDevice",
                                                     "connectGatt",
                                                     "(Landroid/content/Context;ZLandroid/"
                                                     "bluetooth/BluetoothGattCallback;)"
                                                     "Landroid/bluetooth/BluetoothGatt;");
    if (!jni_mid_connectGatt)
    {
        OIC_LOG(ERROR, TAG, "bleConnect: jni_mid_connectGatt is null");
        return NULL;
    }
    
    // Call connectGatt on the BluetoothDevice
    jobject jni_obj_connectGatt = (*env)->CallObjectMethod(env, bluetoothDevice,
                                                           jni_mid_connectGatt,
                                                           NULL,
                                                           autoconnect, g_leGattCallback);
    if (!jni_obj_connectGatt)
    {
        // Connect gatt failed
        OIC_LOG(ERROR, TAG, "connectGatt was failed..it will be removed");
        CALEClientRemoveDeviceInScanDeviceList(env, jni_address);
        CALEClientUpdateSendCnt(env);
        return NULL;
    }
    else
    {
        // Gatt is connecting, callback will come through 
        // CALeGattConnectionStateChangeCallback
        OIC_LOG(DEBUG, TAG, "le connecting..please wait..");
    }
    return jni_obj_connectGatt;
}

bool CALEClientIsConnected(const char* address) {
    if (CALEClientIsValidState(address, CA_LE_CONNECTION_STATE,
                                STATE_SERVICE_CONNECTED))
    {
        OIC_LOG(DEBUG, TAG, "current state is connected");
        return true;
    }
    OIC_LOG(DEBUG, TAG, "current state is not connected");
    return false;
}

CAResult_t CALEClientCloseProfileProxy(JNIEnv *env, jobject gatt)
{
    OIC_LOG(DEBUG, TAG, "IN - CALEClientCloseProfileProxy");

    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(gatt, TAG, "gatt is null");

    jclass jni_cid_BTAdapter = (*env)->FindClass(env, CLASSPATH_BT_ADAPTER);
    if (!jni_cid_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get remote bt adapter method
    jmethodID jni_mid_getDefaultAdapter = (*env)->GetStaticMethodID(env, jni_cid_BTAdapter,
                                                                    "getDefaultAdapter",
                                                                    METHODID_OBJECTNONPARAM);
    if (!jni_mid_getDefaultAdapter)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDefaultAdapter is null");
        return CA_STATUS_FAILED;
    }

    // gat bt adapter object
    jobject jni_obj_BTAdapter = (*env)->CallStaticObjectMethod(env, jni_cid_BTAdapter,
                                                               jni_mid_getDefaultAdapter);
    if (!jni_obj_BTAdapter)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_BTAdapter is null");
        return CA_STATUS_FAILED;
    }

    // get closeProfileProxy method
    jmethodID jni_mid_closeProfileProxy = (*env)->GetMethodID(env, jni_cid_BTAdapter,
                                                              "closeProfileProxy",
                                                              "(ILandroid/bluetooth/"
                                                              "BluetoothProfile;)V");
    if (!jni_mid_closeProfileProxy)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_closeProfileProxy is null");
        return CA_STATUS_FAILED;
    }

    jclass jni_cid_BTProfile = (*env)->FindClass(env, CLASSPATH_BT_PROFILE);
    if (!jni_cid_BTProfile)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_BTProfile is null");
        return CA_STATUS_FAILED;
    }

    // GATT - Constant value : 7 (0x00000007)
    jfieldID id_gatt = (*env)->GetStaticFieldID(env, jni_cid_BTProfile,
                                                "GATT", "I");
    if (!id_gatt)
    {
        OIC_LOG(ERROR, TAG, "id_gatt is null");
        return CA_STATUS_FAILED;
    }

    jint jni_gatt = (*env)->GetStaticIntField(env, jni_cid_BTProfile, id_gatt);

    (*env)->CallVoidMethod(env, jni_obj_BTAdapter, jni_mid_closeProfileProxy, jni_gatt, gatt);
    if ((*env)->ExceptionCheck(env))
    {
        OIC_LOG(ERROR, TAG, "closeProfileProxy has failed");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return CA_STATUS_FAILED;
    }

    OIC_LOG(DEBUG, TAG, "OUT - CALEClientCloseProfileProxy");
    return CA_STATUS_OK;
}


CAResult_t CALEClientDisconnect(JNIEnv *env, jobject bluetoothGatt)
{
    OIC_LOG(DEBUG, TAG, "CALEClientDisconnect");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");

    // get BluetoothGatt method
    jmethodID jni_mid_disconnectGatt  = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                         "disconnect", "()V");
    if (!jni_mid_disconnectGatt)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_disconnectGatt is null");
        return CA_STATUS_FAILED;
    }

    // call disconnect gatt method
    (*env)->CallVoidMethod(env, bluetoothGatt, jni_mid_disconnectGatt);
    if ((*env)->ExceptionCheck(env))
    {
        OIC_LOG(ERROR, TAG, "disconnect has failed");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return CA_STATUS_FAILED;
    }

    OIC_LOG(DEBUG, TAG, "disconnecting Gatt...");

    return CA_STATUS_OK;
}

CAResult_t CALEClientDisconnectAll(JNIEnv *env)
{
    OIC_LOG(DEBUG, TAG, "CALEClientDisconnectAll");
    VERIFY_NON_NULL(env, TAG, "env is null");

    if (!g_gattObjectList)
    {
        OIC_LOG(DEBUG, TAG, "already removed for g_gattObjectList");
        return CA_STATUS_OK;
    }

    uint32_t length = u_arraylist_length(g_gattObjectList);
    OIC_LOG_V(DEBUG, TAG, "list length : %d", length);
    for (uint32_t index = 0; index < length; index++)
    {
        OIC_LOG(DEBUG, TAG, "start CALEClientDisconnectAll");
        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            continue;
        }
        CAResult_t res = CALEClientDisconnect(env, jarrayObj);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientDisconnect has failed");
            continue;
        }
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientDisconnectforAddress(JNIEnv *env, jstring remote_address)
{
    OIC_LOG(DEBUG, TAG, "IN-CALEClientDisconnectforAddress");
    VERIFY_NON_NULL(env, TAG, "env is null");

    if (!g_gattObjectList)
    {
        OIC_LOG(DEBUG, TAG, "already removed for g_gattObjectList");
        return CA_STATUS_OK;
    }

    char* address = (char*)(*env)->GetStringUTFChars(env, remote_address, NULL);
    if (!address)
    {
        OIC_LOG(ERROR, TAG, "address is null");
        return CA_STATUS_FAILED;
    }

    uint32_t length = u_arraylist_length(g_gattObjectList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            continue;
        }

        jstring jni_setAddress = CALEClientGetAddressFromGattObj(env, jarrayObj);
        if (!jni_setAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_setAddress is null");
            (*env)->ReleaseStringUTFChars(env, remote_address, address);
            return CA_STATUS_FAILED;
        }

        const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
        if (!setAddress)
        {
            OIC_LOG(ERROR, TAG, "setAddress is null");
            (*env)->ReleaseStringUTFChars(env, remote_address, address);
            return CA_STATUS_FAILED;
        }

        OIC_LOG_V(DEBUG, TAG, "target address : %s, set address : %s", address, setAddress);
        if (!strcmp(address, setAddress))
        {
            CAResult_t res = CALEClientDisconnect(env, jarrayObj);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientDisconnect has failed");
                (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
                (*env)->ReleaseStringUTFChars(env, remote_address, address);
                return CA_STATUS_FAILED;
            }
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            (*env)->ReleaseStringUTFChars(env, remote_address, address);
            return CA_STATUS_OK;
        }
        (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
    }
    (*env)->ReleaseStringUTFChars(env, remote_address, address);

    OIC_LOG(DEBUG, TAG, "OUT-CALEClientDisconnectforAddress");
    return CA_STATUS_OK;
}

bool CALEClientCreateBond(JNIEnv *env, jobject device)
{
    OIC_LOG(DEBUG, TAG, "CALEClientCreateBond");

    ca_mutex_lock(g_bondMutex);

    // Get createBond static method id
    jmethodID jni_mid_createBond = CAGetJNIMethodID(env,
                                            "android/bluetooth/BluetoothDevice",
                                            "createBond", 
                                            "()Z");
    if (!jni_mid_createBond)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_createBond is null!");
        ca_mutex_unlock(g_bondMutex);
        return false;
    }

    // Call createBond on the BluetoothDevice
    bool bondCreated = (jboolean) (*env)->CallBooleanMethod(env, 
            device, jni_mid_createBond);
    if (!bondCreated) {
        ca_mutex_unlock(g_bondMutex);
        return false;
    }

    // Wait for a signal from the bond state changed callback 
    OIC_LOG(DEBUG, TAG, "Waiting for bond state change callback");
    if (CA_WAIT_SUCCESS != ca_cond_wait_for(g_bondCond, g_bondMutex, 
                                                WAIT_TIME_BOND))
    {
        OIC_LOG(ERROR, TAG, "Bond callback has timed out");
        ca_mutex_unlock(g_bondMutex);
        return false;
    }

    ca_mutex_unlock(g_bondMutex);
    return true;
}

bool CALEClientRemoveBond(JNIEnv *env, jobject device)
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveBond");

    // Check that our java CaLeClientInterface class object is valid 
    if (!g_caLeClientInterface)
    {
        OIC_LOG(ERROR, TAG, "g_caLeClientInterface is null!");
        return false;
    }

    // Get removeBond static method id
    jmethodID jni_mid_removeBond = (*env)->GetStaticMethodID(env,
                                            g_caLeClientInterface,
                                            "removeBond", 
                                            "(Landroid/bluetooth/BluetoothDevice;)Z");
    if (!jni_mid_removeBond)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_removeBond is null!");
        return false;
    }

    // Call removeBond on the BluetoothDevice
    bool bondRemoved = (jboolean) (*env)->CallStaticBooleanMethod(env, 
                                                        g_caLeClientInterface,
                                                        jni_mid_removeBond,
                                                        device);
    return bondRemoved;
}

CAResult_t CALEClientRequestMTU(JNIEnv *env, jobject bluetoothGatt, jint size)
{
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_ADAPTER_NOT_ENABLED;
    }

    // get BluetoothGatt.requestMtu method
    jmethodID jni_mid_requestMtu = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                    "requestMtu", "(I)Z");
    if (!jni_mid_requestMtu)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_requestMtu is null");
        return CA_STATUS_FAILED;
    }

    // call requestMtu
    OIC_LOG(INFO, TAG, "CALL API - requestMtu");
    jboolean ret = (*env)->CallBooleanMethod(env, bluetoothGatt, jni_mid_requestMtu, size);
    if (!ret)
    {
        OIC_LOG(ERROR, TAG, "requestMtu has failed");
        //CACheckJNIException(env);
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientDiscoverServices(JNIEnv *env, jobject bluetoothGatt)
{
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_ADAPTER_NOT_ENABLED;
    }

    // get BluetoothGatt.discoverServices method
    jmethodID jni_mid_discoverServices = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                          "discoverServices", "()Z");
    if (!jni_mid_discoverServices)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_discoverServices is null");
        return CA_STATUS_FAILED;
    }

    // call disconnect gatt method
    jboolean ret = (*env)->CallBooleanMethod(env, bluetoothGatt, jni_mid_discoverServices);
    if (!ret)
    {
        OIC_LOG(ERROR, TAG, "discoverServices has not been started");
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

static void CALEWriteCharacteristicThread(void* object)
{
    VERIFY_NON_NULL_VOID(object, TAG, "object is null");

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return;
        }
        isAttached = true;
    }

    jobject gatt = (jobject)object;
    CAResult_t ret = CALESetValueAndWriteCharacteristic(env, gatt);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALESetValueAndWriteCharacteristic has failed");
        CALEClientUpdateSendCnt(env);
    }

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }
}

CAResult_t CALESetValueAndWriteCharacteristic(JNIEnv* env, jobject gatt)
{
    OIC_LOG(DEBUG, TAG, "CALESetValueAndWriteCharacteristic");

    VERIFY_NON_NULL(gatt, TAG, "gatt is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    jstring jni_address = CALEClientGetAddressFromGattObj(env, gatt);
    if (!jni_address)
    {
        CALEClientSendFinish(env, gatt);
        return CA_STATUS_FAILED;
    }

    const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
    if (!address)
    {
        CALEClientSendFinish(env, gatt);
        return CA_STATUS_FAILED;
    }

    ca_mutex_lock(g_threadSendStateMutex);

    if (CALEClientIsValidState(address, CA_LE_SEND_STATE, STATE_SENDING))
    {
        OIC_LOG(INFO, TAG, "current state is SENDING");
        (*env)->ReleaseStringUTFChars(env, jni_address, address);
        ca_mutex_unlock(g_threadSendStateMutex);
        return CA_STATUS_OK;
    }

    if (CA_STATUS_OK != CALEClientUpdateDeviceState(address, CA_LE_SEND_STATE,
                                                    STATE_SENDING))
    {
        OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
        (*env)->ReleaseStringUTFChars(env, jni_address, address);
        CALEClientSendFinish(env, gatt);
        ca_mutex_unlock(g_threadSendStateMutex);
        return CA_STATUS_FAILED;
    }

    (*env)->ReleaseStringUTFChars(env, jni_address, address);

    ca_mutex_unlock(g_threadSendStateMutex);

    // send data
    jobject jni_obj_character = CALEClientCreateGattCharacteristic(env, gatt, g_sendBuffer);
    if (!jni_obj_character)
    {
        CALEClientSendFinish(env, gatt);
        return CA_STATUS_FAILED;
    }

    // Becuase the user may have switched the target UUIDs, we must make sure we
    // have set the CCCD for the response characteristics in order to receive
    // callbacks from the response characteristic.

    // Get UUID object from current response characteristic
    jobject jUUID = CALEClientGetUUIDObject(env, g_gattResponseCharacteristicUUID);
    if (jUUID && !CALEClientIsResponseCharInList(env, gatt, jUUID)) 
    {
        // Get jstring of target response characteristic UUID
        jstring jResponseUUID = (*env)->NewStringUTF(env, g_gattResponseCharacteristicUUID);
        if (!jResponseUUID)
        {
            OIC_LOG(ERROR, TAG, "jni_uuid is null");
            CALEClientSendFinish(env, gatt);
            return CA_STATUS_FAILED; 
        }
        
        // Get the java characteristic class corresponding to the response UUID 
        jobject jResponseCharacteristic = CALEClientGetGattCharacteristic(env, gatt, jResponseUUID);
        if (!jResponseCharacteristic)
        {
            OIC_LOG(ERROR, TAG, "jni_obj_GattCharacteristic is null");
            CALEClientSendFinish(env, gatt);
            return CA_STATUS_FAILED; 
        }
        
        // Set Characteristic notification for local characteristic
        CAResult_t res = CALEClientSetCharacteristicNotification(env, gatt,
                                                             jResponseCharacteristic);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientSetCharacteristicNotification has failed");
            CALEClientSendFinish(env, gatt);
            return CA_STATUS_FAILED; 
        }

        // Write to Descriptor using the previously set characteristic
        res = CALEClientSetUUIDToDescriptor(env, gatt, jResponseCharacteristic);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "Set Descriptor has failed!");
            CALEClientSendFinish(env, gatt);
            return CA_STATUS_FAILED; 
        }
        
        // Wait for signal from write descriptor callback
        OIC_LOG(DEBUG, TAG, "waiting for onDescriptorWrite callback");
        ca_mutex_lock(g_writeDescMutex);
        if (CA_WAIT_SUCCESS != ca_cond_wait_for(g_writeDescCond,
                                  g_writeDescMutex,
                                  WAIT_TIME_WRITE_CHARACTERISTIC))
        {
            OIC_LOG(ERROR, TAG, "there is no response. write descriptor has failed?");
            CALEClientSendFinish(env, gatt);
            ca_mutex_unlock(g_writeDescMutex);
            return CA_STATUS_FAILED;
        }
        OIC_LOG(DEBUG, TAG, "Signal received. Continuing to writeCharacteristic");
        ca_mutex_unlock(g_writeDescMutex);
    }

    CAResult_t ret = CALEClientWriteCharacteristicImpl(env, gatt, jni_obj_character);
    if (CA_STATUS_OK != ret)
    {
        CALEClientSendFinish(env, gatt);
        return CA_STATUS_FAILED;
    }

    // wait for callback for write Characteristic with success to sent data
    OIC_LOG_V(DEBUG, TAG, "callback flag is %d", g_isSignalSetFlag);
    ca_mutex_lock(g_threadWriteCharacteristicMutex);
    if (!g_isSignalSetFlag)
    {
        OIC_LOG(DEBUG, TAG, "wait for callback to notify writeCharacteristic is success");
        if (CA_WAIT_SUCCESS != ca_cond_wait_for(g_threadWriteCharacteristicCond,
                                  g_threadWriteCharacteristicMutex,
                                  WAIT_TIME_WRITE_CHARACTERISTIC))
        {
            OIC_LOG(ERROR, TAG, "there is no response. write has failed");
            g_isSignalSetFlag = false;
            CALEClientSendFinish(env, gatt);
            ca_mutex_unlock(g_threadWriteCharacteristicMutex);
            return CA_STATUS_FAILED;
        }
    }
    // reset flag set by writeCharacteristic Callback
    g_isSignalSetFlag = false;
    ca_mutex_unlock(g_threadWriteCharacteristicMutex);

    OIC_LOG(INFO, TAG, "writeCharacteristic success!!");
    return CA_STATUS_OK;
}

CAResult_t CALEClientWriteCharacteristic(JNIEnv *env, jobject gatt)
{
    OIC_LOG(DEBUG, TAG, "IN - CALEClientWriteCharacteristic");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(gatt, TAG, "gatt is null");

    jobject gattParam = (*env)->NewGlobalRef(env, gatt);
    if (CA_STATUS_OK != ca_thread_pool_add_task(g_threadPoolHandle,
                                                CALEWriteCharacteristicThread, (void*)gattParam))
    {
        OIC_LOG(ERROR, TAG, "Failed to create read thread!");
        return CA_STATUS_FAILED;
    }

    OIC_LOG(DEBUG, TAG, "OUT - CALEClientWriteCharacteristic");
    return CA_STATUS_OK;
}

CAResult_t CALEClientWriteCharacteristicImpl(JNIEnv *env, jobject bluetoothGatt,
                                             jobject gattCharacteristic)
{
    OIC_LOG(DEBUG, TAG, "WRITE GATT CHARACTERISTIC");
    OIC_LOG_V(DEBUG, TAG, "\tService UUID: %s", g_gattServiceUUID);
    OIC_LOG_V(DEBUG, TAG, "\tRequest UUID: %s", g_gattRequestCharacteristicUUID);
    OIC_LOG_V(DEBUG, TAG, "\tResponse UUID: %s", g_gattResponseCharacteristicUUID);
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");
    VERIFY_NON_NULL(gattCharacteristic, TAG, "gattCharacteristic is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_STATUS_FAILED;
    }

    // get BluetoothGatt.write characteristic method
    jmethodID jni_mid_writeCharacteristic = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                             "writeCharacteristic",
                                                             "(Landroid/bluetooth/"
                                                             "BluetoothGattCharacteristic;)Z");
    if (!jni_mid_writeCharacteristic)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_writeCharacteristic is null");
        return CA_STATUS_FAILED;
    }

    // call writeCharacteristic method
    jboolean ret = (jboolean)(*env)->CallBooleanMethod(env, bluetoothGatt,
                                                       jni_mid_writeCharacteristic,
                                                       gattCharacteristic);
    if (ret)
    {
        OIC_LOG(DEBUG, TAG, "writeCharacteristic is called successfully");
    }
    else
    {
        OIC_LOG(ERROR, TAG, "writeCharacteristic has failed");
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientReadCharacteristic(JNIEnv *env, jobject bluetoothGatt)
{
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_STATUS_FAILED;
    }

    jstring jni_uuid = (*env)->NewStringUTF(env, g_gattResponseCharacteristicUUID);
    if (!jni_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_uuid is null");
        return CA_STATUS_FAILED;
    }

    jobject jni_obj_GattCharacteristic = CALEClientGetGattCharacteristic(env, bluetoothGatt, jni_uuid);
    if (!jni_obj_GattCharacteristic)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_GattCharacteristic is null");
        return CA_STATUS_FAILED;
    }

    jmethodID jni_mid_readCharacteristic = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                            "readCharacteristic",
                                                            "(Landroid/bluetooth/"
                                                            "BluetoothGattCharacteristic;)Z");
    if (!jni_mid_readCharacteristic)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_readCharacteristic is null");
        return CA_STATUS_FAILED;
    }

    jboolean ret = (*env)->CallBooleanMethod(env, bluetoothGatt, jni_mid_readCharacteristic,
                                             jni_obj_GattCharacteristic);
    if (ret)
    {
        OIC_LOG(DEBUG, TAG, "readCharacteristic success");
    }
    else
    {
        OIC_LOG(ERROR, TAG, "readCharacteristic has failed");
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

CAResult_t CALEClientSetCharacteristicNotification(JNIEnv *env, jobject bluetoothGatt,
                                                   jobject characteristic)
{
    OIC_LOG(DEBUG, TAG, "CALEClientSetCharacteristicNotification");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");
    VERIFY_NON_NULL(characteristic, TAG, "characteristic is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_ADAPTER_NOT_ENABLED;
    }

    // get BluetoothGatt.setCharacteristicNotification method
    jmethodID jni_mid_setNotification = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                         "setCharacteristicNotification",
                                                         "(Landroid/bluetooth/"
                                                         "BluetoothGattCharacteristic;Z)Z");
    if (!jni_mid_setNotification)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getService is null");
        return CA_STATUS_FAILED;
    }

    jboolean ret = (*env)->CallBooleanMethod(env, bluetoothGatt, jni_mid_setNotification,
                                             characteristic, JNI_TRUE);
    if (JNI_TRUE == ret)
    {
        OIC_LOG(DEBUG, TAG, "CALL API - setCharacteristicNotification success");
    }
    else
    {
        OIC_LOG(ERROR, TAG, "CALL API - setCharacteristicNotification has failed");
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

jobject CALEClientGetGattCharacteristic(JNIEnv *env, jobject bluetoothGatt, jstring characterUUID)
{
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);
    VERIFY_NON_NULL_RET(bluetoothGatt, TAG, "bluetoothGatt is null", NULL);
    VERIFY_NON_NULL_RET(characterUUID, TAG, "characterUUID is null", NULL);

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return NULL;
    }

    // get BluetoothGatt.getService method
    jmethodID jni_mid_getService = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                    "getService",
                                                    "(Ljava/util/UUID;)Landroid/bluetooth/"
                                                    "BluetoothGattService;");
    if (!jni_mid_getService)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getService is null");
        return NULL;
    }

    jobject jni_obj_service_uuid = CALEClientGetUUIDObject(env, g_gattServiceUUID);
    if (!jni_obj_service_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_service_uuid is null");
        return NULL;
    }

    // get bluetooth gatt service
    jobject jni_obj_gattService = (*env)->CallObjectMethod(env, bluetoothGatt, jni_mid_getService,
                                                           jni_obj_service_uuid);
    if (!jni_obj_gattService)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_gattService is null");
        return NULL;
    }

    // get service's getCharacteristic method id 
    jmethodID jni_mid_getCharacteristic = CAGetJNIMethodID(env, "android/bluetooth/"
                                                           "BluetoothGattService",
                                                           "getCharacteristic",
                                                           "(Ljava/util/UUID;)"
                                                           "Landroid/bluetooth/"
                                                           "BluetoothGattCharacteristic;");
    if (!jni_mid_getCharacteristic)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getCharacteristic is null");
        return NULL;
    }

    const char* uuid = (*env)->GetStringUTFChars(env, characterUUID, NULL);
    if (!uuid)
    {
        OIC_LOG(ERROR, TAG, "uuid is null");
        return NULL;
    }

    jobject jni_obj_tx_uuid = CALEClientGetUUIDObject(env, uuid);
    if (!jni_obj_tx_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_tx_uuid is null");
        (*env)->ReleaseStringUTFChars(env, characterUUID, uuid);
        return NULL;
    }

    jobject jni_obj_GattCharacteristic = (*env)->CallObjectMethod(env, jni_obj_gattService,
                                                                  jni_mid_getCharacteristic,
                                                                  jni_obj_tx_uuid);

    (*env)->ReleaseStringUTFChars(env, characterUUID, uuid);
    return jni_obj_GattCharacteristic;
}

jobject CALEClientCreateGattCharacteristic(JNIEnv *env, jobject bluetoothGatt, jbyteArray data)
{
    OIC_LOG(DEBUG, TAG, "CALEClientCreateGattCharacteristic");
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);
    VERIFY_NON_NULL_RET(bluetoothGatt, TAG, "bluetoothGatt is null", NULL);
    VERIFY_NON_NULL_RET(data, TAG, "data is null", NULL);

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return NULL;
    }

    jstring jni_uuid = (*env)->NewStringUTF(env, g_gattRequestCharacteristicUUID);
    if (!jni_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_uuid is null");
        return NULL;
    }

    jobject jni_obj_GattCharacteristic = CALEClientGetGattCharacteristic(env, bluetoothGatt, jni_uuid);
    if (!jni_obj_GattCharacteristic)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_GattCharacteristic is null");
        return NULL;
    }

    jclass jni_cid_BTGattCharacteristic = (*env)->FindClass(env, "android/bluetooth"
                                                            "/BluetoothGattCharacteristic");
    if (!jni_cid_BTGattCharacteristic)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_BTGattCharacteristic is null");
        return NULL;
    }

    jmethodID jni_mid_setValue = (*env)->GetMethodID(env, jni_cid_BTGattCharacteristic, "setValue",
                                                     "([B)Z");
    if (!jni_mid_setValue)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_setValue is null");
        return NULL;
    }

    jboolean ret = (*env)->CallBooleanMethod(env, jni_obj_GattCharacteristic, jni_mid_setValue,
                                             data);
    if (JNI_TRUE == ret)
    {
        OIC_LOG(DEBUG, TAG, "the locally stored value has been set");
    }
    else
    {
        OIC_LOG(ERROR, TAG, "the locally stored value hasn't been set");
        return NULL;
    }

    // set Write Type
    jmethodID jni_mid_setWriteType = (*env)->GetMethodID(env, jni_cid_BTGattCharacteristic,
                                                         "setWriteType", "(I)V");
    if (!jni_mid_setWriteType)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_setWriteType is null");
        return NULL;
    }

    jfieldID jni_fid_no_response = (*env)->GetStaticFieldID(env, jni_cid_BTGattCharacteristic,
                                                            "WRITE_TYPE_DEFAULT", "I");
    if (!jni_fid_no_response)
    {
        OIC_LOG(ERROR, TAG, "jni_fid_no_response is not available");
        return NULL;
    }

    jint jni_int_val = (*env)->GetStaticIntField(env, jni_cid_BTGattCharacteristic,
                                                 jni_fid_no_response);

    (*env)->CallVoidMethod(env, jni_obj_GattCharacteristic, jni_mid_setWriteType, jni_int_val);

    return jni_obj_GattCharacteristic;
}

jbyteArray CALEClientGetValueFromCharacteristic(JNIEnv *env, jobject characteristic)
{
    VERIFY_NON_NULL_RET(characteristic, TAG, "characteristic is null", NULL);
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return NULL;
    }

    jmethodID jni_mid_getValue  = CAGetJNIMethodID(env, "android/bluetooth/"
                                                   "BluetoothGattCharacteristic",
                                                   "getValue", "()[B");
    if (!jni_mid_getValue)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getValue is null");
        return NULL;
    }

    jbyteArray jni_obj_data_array = (*env)->CallObjectMethod(env, characteristic,
                                                             jni_mid_getValue);
    return jni_obj_data_array;
}

CAResult_t CALEClientCreateUUIDList()
{
    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return CA_STATUS_FAILED;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return CA_STATUS_FAILED;
        }
        isAttached = true;
    }

    // create new object array
    jclass jni_cid_uuid_list = (*env)->FindClass(env, CLASSPATH_BT_UUID);
    if (!jni_cid_uuid_list)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_uuid_list is null");
        goto error_exit;
    }

    jobjectArray jni_obj_uuid_list = (jobjectArray)(*env)->NewObjectArray(env, 1,
                                                                          jni_cid_uuid_list, NULL);
    if (!jni_obj_uuid_list)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_uuid_list is null");
        goto error_exit;
    }
    // make uuid list
    jobject jni_obj_uuid = CALEClientGetUUIDObject(env, g_gattServiceUUID);
    if (!jni_obj_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_uuid is null");
        goto error_exit;
    }
    (*env)->SetObjectArrayElement(env, jni_obj_uuid_list, 0, jni_obj_uuid);

    g_uuidList = (jobjectArray)(*env)->NewGlobalRef(env, jni_obj_uuid_list);

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

    return CA_STATUS_OK;

    // error label.
error_exit:

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }
    return CA_STATUS_FAILED;
}

CAResult_t CALEClientSetUUIDToDescriptor(JNIEnv *env, jobject bluetoothGatt,
                                         jobject characteristic)
{
    OIC_LOG(DEBUG, TAG, "CALEClientSetUUIDToDescriptor");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(bluetoothGatt, TAG, "bluetoothGatt is null");
    VERIFY_NON_NULL(characteristic, TAG, "characteristic is null");

    if (!CALEIsEnableBTAdapter(env))
    {
        OIC_LOG(INFO, TAG, "BT adapter is not enabled");
        return CA_ADAPTER_NOT_ENABLED;
    }

    jmethodID jni_mid_getDescriptor  = CAGetJNIMethodID(env, "android/bluetooth/"
                                                        "BluetoothGattCharacteristic",
                                                        "getDescriptor",
                                                        "(Ljava/util/UUID;)Landroid/bluetooth/"
                                                        "BluetoothGattDescriptor;");
    if (!jni_mid_getDescriptor)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDescriptor is null");
        return CA_STATUS_FAILED;
    }

    jobject jni_obj_cc_uuid = CALEClientGetUUIDObject(env, OIC_GATT_CHARACTERISTIC_CONFIG_UUID);
    if (!jni_obj_cc_uuid)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_cc_uuid is null");
        return CA_STATUS_FAILED;
    }

    jobject jni_obj_descriptor = (*env)->CallObjectMethod(env, characteristic,
                                                          jni_mid_getDescriptor, jni_obj_cc_uuid);
    if (!jni_obj_descriptor)
    {
        OIC_LOG(INFO, TAG, "jni_obj_descriptor is null");
        return CA_NOT_SUPPORTED;
    }

    jclass jni_cid_descriptor = (*env)->FindClass(env,
                                                  "android/bluetooth/BluetoothGattDescriptor");
    if (!jni_cid_descriptor)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_descriptor is null");
        return CA_STATUS_FAILED;
    }

    jmethodID jni_mid_setValue = (*env)->GetMethodID(env, jni_cid_descriptor, "setValue", "([B)Z");
    if (!jni_mid_setValue)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_setValue is null");
        return CA_STATUS_FAILED;
    }

    jfieldID jni_fid_NotiValue = (*env)->GetStaticFieldID(env, jni_cid_descriptor,
                                                          "ENABLE_NOTIFICATION_VALUE", "[B");
    if (!jni_fid_NotiValue)
    {
        OIC_LOG(ERROR, TAG, "jni_fid_NotiValue is null");
        return CA_STATUS_FAILED;
    }

    jboolean jni_setvalue = (*env)->CallBooleanMethod(
            env, jni_obj_descriptor, jni_mid_setValue,
            (jbyteArray)(*env)->GetStaticObjectField(env, jni_cid_descriptor, jni_fid_NotiValue));
    if (jni_setvalue)
    {
        OIC_LOG(DEBUG, TAG, "setValue success");
    }
    else
    {
        OIC_LOG(ERROR, TAG, "setValue has failed");
        return CA_STATUS_FAILED;
    }

    jmethodID jni_mid_writeDescriptor  = CAGetJNIMethodID(env, "android/bluetooth/BluetoothGatt",
                                                          "writeDescriptor",
                                                          "(Landroid/bluetooth/"
                                                          "BluetoothGattDescriptor;)Z");
    if (!jni_mid_writeDescriptor)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_writeDescriptor is null");
        return CA_STATUS_FAILED;
    }

    jboolean jni_ret = (*env)->CallBooleanMethod(env, bluetoothGatt, jni_mid_writeDescriptor,
                                                 jni_obj_descriptor);
    if (jni_ret)
    {
        OIC_LOG(DEBUG, TAG, "writeDescriptor success");
    }
    else
    {
        OIC_LOG(ERROR, TAG, "writeDescriptor has failed");
        return CA_STATUS_FAILED;
    }

    return CA_STATUS_OK;
}

void CALEClientCreateScanDeviceList(JNIEnv *env)
{
    OIC_LOG(DEBUG, TAG, "CALEClientCreateScanDeviceList");
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");

    ca_mutex_lock(g_deviceListMutex);
    // create new object array
    if (g_deviceList == NULL)
    {
        OIC_LOG(DEBUG, TAG, "Create device list");
        g_deviceList = u_arraylist_create();
    }
    ca_mutex_unlock(g_deviceListMutex);
}

CAResult_t CALEClientAddScanDeviceToList(JNIEnv *env, jobject device)
{
    VERIFY_NON_NULL(device, TAG, "device is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    ca_mutex_lock(g_deviceListMutex);

    if (!g_deviceList)
    {
        OIC_LOG(ERROR, TAG, "gdevice_list is null");

        CALEClientSetScanFlag(false);
        if(CA_STATUS_OK != CALEClientStopScan())
        {
            OIC_LOG(ERROR, TAG, "CALEClientStopScan has failed");
        }

        ca_mutex_unlock(g_deviceListMutex);
        return CA_STATUS_FAILED;
    }

    jstring jni_remoteAddress = CALEGetAddressFromBTDevice(env, device);
    if (!jni_remoteAddress)
    {
        OIC_LOG(ERROR, TAG, "jni_remoteAddress is null");
        ca_mutex_unlock(g_deviceListMutex);
        return CA_STATUS_FAILED;
    }

    const char* remoteAddress = (*env)->GetStringUTFChars(env, jni_remoteAddress, NULL);
    if (!remoteAddress)
    {
        OIC_LOG(ERROR, TAG, "remoteAddress is null");
        (*env)->DeleteLocalRef(env, jni_remoteAddress);
        ca_mutex_unlock(g_deviceListMutex);
        return CA_STATUS_FAILED;
    }

    if (!CALEClientIsDeviceInScanDeviceList(env, remoteAddress))
    {
        jobject gdevice = (*env)->NewGlobalRef(env, device);
        u_arraylist_add(g_deviceList, gdevice);
        ca_cond_signal(g_deviceDescCond);
        OIC_LOG_V(DEBUG, TAG, "Added this BT Device[%s] in the List", remoteAddress);
    }
    (*env)->ReleaseStringUTFChars(env, jni_remoteAddress, remoteAddress);
    (*env)->DeleteLocalRef(env, jni_remoteAddress);

    ca_mutex_unlock(g_deviceListMutex);

    return CA_STATUS_OK;
}

bool CALEClientIsDeviceInScanDeviceList(JNIEnv *env, const char* remoteAddress)
{
    VERIFY_NON_NULL_RET(env, TAG, "env is null", true);
    VERIFY_NON_NULL_RET(remoteAddress, TAG, "remoteAddress is null", true);

    if (!g_deviceList)
    {
        OIC_LOG(DEBUG, TAG, "g_deviceList is null");
        return true;
    }

    uint32_t length = u_arraylist_length(g_deviceList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_deviceList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            return true;
        }

        jstring jni_setAddress = CALEGetAddressFromBTDevice(env, jarrayObj);
        if (!jni_setAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_setAddress is null");
            return true;
        }

        const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
        if (!setAddress)
        {
            OIC_LOG(ERROR, TAG, "setAddress is null");
            (*env)->DeleteLocalRef(env, jni_setAddress);
            return true;
        }

        if (!strcmp(remoteAddress, setAddress))
        {
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            (*env)->DeleteLocalRef(env, jni_setAddress);
            return true;
        }

        (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
        (*env)->DeleteLocalRef(env, jni_setAddress);
    }

    OIC_LOG(DEBUG, TAG, "there are no the device in list. we can add");

    return false;
}

CAResult_t CALEClientRemoveAllScanDevices(JNIEnv *env)
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveAllScanDevices");
    VERIFY_NON_NULL(env, TAG, "env is null");

    ca_mutex_lock(g_deviceListMutex);

    if (!g_deviceList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceList is null");
        ca_mutex_unlock(g_deviceListMutex);
        return CA_STATUS_FAILED;
    }

    uint32_t length = u_arraylist_length(g_deviceList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_deviceList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            continue;
        }
        (*env)->DeleteGlobalRef(env, jarrayObj);
        jarrayObj = NULL;
    }

    OICFree(g_deviceList);
    g_deviceList = NULL;

    ca_mutex_unlock(g_deviceListMutex);
    return CA_STATUS_OK;
}

CAResult_t CALEClientRemoveDeviceInScanDeviceList(JNIEnv *env, jstring address)
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveDeviceInScanDeviceList");
    VERIFY_NON_NULL(address, TAG, "address is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    ca_mutex_lock(g_deviceListMutex);

    if (!g_deviceList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceList is null");
        ca_mutex_unlock(g_deviceListMutex);
        return CA_STATUS_FAILED;
    }

    uint32_t length = u_arraylist_length(g_deviceList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_deviceList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            ca_mutex_unlock(g_deviceListMutex);
            return CA_STATUS_FAILED;
        }

        jstring jni_setAddress = CALEGetAddressFromBTDevice(env, jarrayObj);
        if (!jni_setAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_setAddress is null");
            ca_mutex_unlock(g_deviceListMutex);
            return CA_STATUS_FAILED;
        }

        const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
        if (!setAddress)
        {
            OIC_LOG(ERROR, TAG, "setAddress is null");
            ca_mutex_unlock(g_deviceListMutex);
            return CA_STATUS_FAILED;
        }

        const char* remoteAddress = (*env)->GetStringUTFChars(env, address, NULL);
        if (!remoteAddress)
        {
            OIC_LOG(ERROR, TAG, "remoteAddress is null");
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            ca_mutex_unlock(g_deviceListMutex);
            return CA_STATUS_FAILED;
        }

        if (!strcmp(setAddress, remoteAddress))
        {
            OIC_LOG_V(DEBUG, TAG, "remove object : %s", remoteAddress);
            (*env)->DeleteGlobalRef(env, jarrayObj);
            jarrayObj = NULL;
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            (*env)->ReleaseStringUTFChars(env, address, remoteAddress);

            if (NULL == u_arraylist_remove(g_deviceList, index))
            {
                OIC_LOG(ERROR, TAG, "List removal failed.");
                ca_mutex_unlock(g_deviceListMutex);
                return CA_STATUS_FAILED;
            }
            ca_mutex_unlock(g_deviceListMutex);
            return CA_STATUS_OK;
        }
        (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
        (*env)->ReleaseStringUTFChars(env, address, remoteAddress);
    }

    ca_mutex_unlock(g_deviceListMutex);
    OIC_LOG(DEBUG, TAG, "There are no object in the device list");

    return CA_STATUS_OK;
}


//==============================================================================
// Response Characteristic List Methods 
//==============================================================================

jstring CALEClientCreateResponseCharListKey(JNIEnv *env, jobject gatt, jobject responseCharUUID)
{

    // Get address string from gatt object 
    jstring jAddress = CALEClientGetAddressFromGattObj(env, gatt);
    if (!jAddress) {
        OIC_LOG(ERROR, TAG, "jAddress is null!");
        return NULL;
    }

    // Get string from UUID object 
    jstring jUUIDString = CALEClientGetStringFromUUID(env, responseCharUUID);
    if (!jAddress) {
        OIC_LOG(ERROR, TAG, "jUUIDString is null!");
        return NULL;
    }

    // Get String class id
    jclass jni_cid_String = (*env)->FindClass(env, "java/lang/String");
    if (!jni_cid_String)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_String is null!");
        return NULL;
    }
    // Get concat method id
    jmethodID jni_mid_concat = (*env)->GetMethodID(env, jni_cid_String, "concat", 
            "(Ljava/lang/String;)Ljava/lang/String;");
    if (!jni_mid_concat)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_concat is null!");
        return NULL;
    }

    // Call concat method
    jstring jconcat = (jstring) (*env)->CallObjectMethod(env, jAddress, jni_mid_concat, 
            jUUIDString);
    if (!jconcat) 
    {
        OIC_LOG(ERROR, TAG, "jconcat is null!");
        return NULL;
    }   
    return jconcat;
}

void CALEClientCreateResponseCharList(JNIEnv *env)
{
    OIC_LOG(DEBUG, TAG, "CALEClientCreateResponseCharList");
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");

    ca_mutex_lock(g_responseCharMutex);
    g_responseCharList = u_arraylist_create();
    ca_mutex_unlock(g_responseCharMutex);
}

CAResult_t CALEClientAddResponseCharToList(JNIEnv *env, jobject gatt, jobject responseCharUUID)
{
    OIC_LOG(INFO, TAG, "CALEClientAddResponseCharToList");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(responseCharUUID, TAG, "responseCharUUID is null");

    ca_mutex_lock(g_responseCharMutex);

    if (!g_responseCharList)
    {
        OIC_LOG(ERROR, TAG, "g_responseCharList is not available");
        ca_mutex_unlock(g_responseCharMutex);
        return CA_STATUS_FAILED;
    }

    if (!CALEClientIsResponseCharInList(env, gatt, responseCharUUID))
    {
        jstring jKey = CALEClientCreateResponseCharListKey(env, gatt, responseCharUUID); 
        if (!jKey)
        {
            OIC_LOG(ERROR, TAG, "jKey is null!");
            ca_mutex_unlock(g_responseCharMutex);
            return CA_STATUS_FAILED;
        }
        jstring newKey = (jstring) (*env)->NewGlobalRef(env, jKey);
        u_arraylist_add(g_responseCharList, newKey);
        OIC_LOG(DEBUG, TAG, "Added response characteristic key to list");
    }
    else
    {
        OIC_LOG(DEBUG, TAG, "Response characteristic is already in list");
    }
    ca_mutex_unlock(g_responseCharMutex);
    return CA_STATUS_OK;
}

bool CALEClientIsResponseCharInList(JNIEnv *env, jobject gatt, jobject responseCharUUID)
{
    OIC_LOG(DEBUG, TAG, "CALEClientIsResponseCharInList");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL_RET(responseCharUUID, TAG, "responseCharUUID is null", false);


    jstring jKey = CALEClientCreateResponseCharListKey(env, gatt, responseCharUUID);
    if (!jKey) {
        OIC_LOG(ERROR, TAG, "jKey is null!");
        return false;
    }

        
    // Get String class id
    jclass jni_cid_String = (*env)->FindClass(env, "java/lang/String"); 
    if (!jni_cid_String) 
    {
        OIC_LOG(ERROR, TAG, "jni_cid_String is null");
        return false;
    }

    // Get equals method id
    jmethodID jni_mid_equals = (*env)->GetMethodID(env, jni_cid_String, "equals", 
            "(Ljava/lang/Object;)Z"); 
    if (!jni_mid_equals)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_equals is null!");
        return false;
    }

    uint32_t length = u_arraylist_length(g_responseCharList);
    for (uint32_t index = 0; index < length; index++)
    {
        // Get string from list 
        jstring jListKey = u_arraylist_get(g_responseCharList, index);
        if (!jListKey)
        {
            OIC_LOG(ERROR, TAG, "jListKey is null");
            return false;
        }

        // Call equals
        bool isEqual = (jboolean) (*env)->CallBooleanMethod(env, jListKey, 
                jni_mid_equals, jKey);
        if (isEqual)
        {
            // If the two keys are equal return true
            OIC_LOG(DEBUG, TAG, "The response characteristic has already been set");
            return true;
        } 
    }

    OIC_LOG(DEBUG, TAG, "The given response characteristic UUID is not in the list.");
    return false;
}

CAResult_t CALEClientRemoveResponseCharListKeysForAddress(JNIEnv *env, jstring address)
{
    OIC_LOG(INFO, TAG, "CALEClientRemoveResponseCharListKeysForAddress"); 
    VERIFY_NON_NULL_RET(env, TAG, "env is null", CA_STATUS_FAILED);
    VERIFY_NON_NULL_RET(address, TAG, "address is null", CA_STATUS_FAILED);
    
    ca_mutex_lock(g_responseCharMutex);
    if (!g_responseCharList) 
    {
        OIC_LOG(ERROR, TAG, "g_responseCharList is null!");
        ca_mutex_unlock(g_responseCharMutex);
        return CA_STATUS_FAILED;
    }
    // Get String class id
    jclass jni_cid_String = (*env)->FindClass(env, "java/lang/String");
    if (!jni_cid_String)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_String is null!");
        ca_mutex_unlock(g_responseCharMutex);
        return CA_STATUS_FAILED;
    }
    // Get startsWith method id
    jmethodID jni_mid_startsWith = (*env)->GetMethodID(env, jni_cid_String, 
            "startsWith", "(Ljava/lang/String;)Z");
    if (!jni_mid_startsWith)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_startsWith is null!");
        ca_mutex_unlock(g_responseCharMutex);
        return CA_STATUS_FAILED;
    }

    // Loop through list and remove each item that starts with the given address
    uint32_t length = u_arraylist_length(g_responseCharList);
    for (uint32_t index = 0; index < length; index++)
    {
        // Get string from list 
        jstring jListKey = u_arraylist_get(g_responseCharList, index);
        if (!jListKey)
        {
            OIC_LOG(ERROR, TAG, "jListKey is null");
            continue;
        }       
        // Call startsWith
        bool isMatch = (jboolean) (*env)->CallBooleanMethod(env, jListKey, 
                jni_mid_startsWith, address);
        // If the key in the list starts with the address remove it
        if (isMatch) 
        {
            OIC_LOG(DEBUG, TAG, "Match found, removing from list");
            // Free the reference
            (*env)->DeleteGlobalRef(env, jListKey);
            jListKey = NULL;
            // Remove the object
            if (NULL == u_arraylist_remove(g_responseCharList, index)) {
                OIC_LOG(ERROR, TAG, "List removal failed.");
            } else {
                index--;
                length--;
            }
        }
    }
    ca_mutex_unlock(g_responseCharMutex);
    return CA_STATUS_OK;
}

/**
 * Gatt Object List
 */

CAResult_t CALEClientAddGattobjToList(JNIEnv *env, jobject gatt)
{
    OIC_LOG(INFO, TAG, "CALEClientAddGattobjToList");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(gatt, TAG, "gatt is null");

    ca_mutex_lock(g_gattObjectMutex);

    if (!g_gattObjectList)
    {
        OIC_LOG(ERROR, TAG, "g_gattObjectList is not available");
        ca_mutex_unlock(g_gattObjectMutex);
        return CA_STATUS_FAILED;
    }

    jstring jni_remoteAddress = CALEClientGetAddressFromGattObj(env, gatt);
    if (!jni_remoteAddress)
    {
        OIC_LOG(ERROR, TAG, "jni_remoteAddress is null");
        ca_mutex_unlock(g_gattObjectMutex);
        return CA_STATUS_FAILED;
    }

    const char* remoteAddress = (*env)->GetStringUTFChars(env, jni_remoteAddress, NULL);
    if (!remoteAddress)
    {
        OIC_LOG(ERROR, TAG, "remoteAddress is null");
        ca_mutex_unlock(g_gattObjectMutex);
        return CA_STATUS_FAILED;
    }

    OIC_LOG_V(INFO, TAG, "remote address : %s", remoteAddress);
    if (!CALEClientIsGattObjInList(env, remoteAddress))
    {
        jobject newGatt = (*env)->NewGlobalRef(env, gatt);
        u_arraylist_add(g_gattObjectList, newGatt);
        OIC_LOG(INFO, TAG, "Set GATT Object to Array as Element");
    }

    (*env)->ReleaseStringUTFChars(env, jni_remoteAddress, remoteAddress);
    ca_mutex_unlock(g_gattObjectMutex);
    return CA_STATUS_OK;
}

bool CALEClientIsGattObjInList(JNIEnv *env, const char* remoteAddress)
{
    OIC_LOG(DEBUG, TAG, "CALEClientIsGattObjInList");
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL_RET(remoteAddress, TAG, "remoteAddress is null", true);

    uint32_t length = u_arraylist_length(g_gattObjectList);
    for (uint32_t index = 0; index < length; index++)
    {

        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            return true;
        }

        jstring jni_setAddress = CALEClientGetAddressFromGattObj(env, jarrayObj);
        if (!jni_setAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_setAddress is null");
            return true;
        }

        const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
        if (!setAddress)
        {
            OIC_LOG(ERROR, TAG, "setAddress is null");
            return true;
        }

        if (!strcmp(remoteAddress, setAddress))
        {
            OIC_LOG(DEBUG, TAG, "the device is already set");
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            return true;
        }
        else
        {
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            continue;
        }
    }

    OIC_LOG(DEBUG, TAG, "There are no GATT object in list. it can be added");
    return false;
}

jobject CALEClientGetGattObjInList(JNIEnv *env, const char* remoteAddress)
{
    OIC_LOG(DEBUG, TAG, "CALEClientGetGattObjInList");
    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);
    VERIFY_NON_NULL_RET(remoteAddress, TAG, "remoteAddress is null", NULL);

    ca_mutex_lock(g_gattObjectMutex);
    uint32_t length = u_arraylist_length(g_gattObjectList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return NULL;
        }

        jstring jni_setAddress = CALEClientGetAddressFromGattObj(env, jarrayObj);
        if (!jni_setAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_setAddress is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return NULL;
        }

        const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
        if (!setAddress)
        {
            OIC_LOG(ERROR, TAG, "setAddress is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return NULL;
        }

        if (!strcmp(remoteAddress, setAddress))
        {
            OIC_LOG(DEBUG, TAG, "the device is already set");
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            ca_mutex_unlock(g_gattObjectMutex);
            return jarrayObj;
        }
        (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
    }

    ca_mutex_unlock(g_gattObjectMutex);
    OIC_LOG(DEBUG, TAG, "There are no the gatt object in list");
    return NULL;
}

CAResult_t CALEClientRemoveAllGattObjs(JNIEnv *env)
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveAllGattObjs");
    VERIFY_NON_NULL(env, TAG, "env is null");

    ca_mutex_lock(g_gattObjectMutex);
    if (!g_gattObjectList)
    {
        OIC_LOG(DEBUG, TAG, "already removed for g_gattObjectList");
        ca_mutex_unlock(g_gattObjectMutex);
        return CA_STATUS_OK;
    }

    uint32_t length = u_arraylist_length(g_gattObjectList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            continue;
        }
        (*env)->DeleteGlobalRef(env, jarrayObj);
        jarrayObj = NULL;
    }

    OICFree(g_gattObjectList);
    g_gattObjectList = NULL;
    OIC_LOG(INFO, TAG, "g_gattObjectList is removed");
    ca_mutex_unlock(g_gattObjectMutex);
    return CA_STATUS_OK;
}

CAResult_t CALEClientRemoveGattObj(JNIEnv *env, jobject gatt)
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveGattObj");
    VERIFY_NON_NULL(gatt, TAG, "gatt is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    ca_mutex_lock(g_gattObjectMutex);
    if (!g_gattObjectList)
    {
        OIC_LOG(DEBUG, TAG, "already removed for g_gattObjectList");
        ca_mutex_unlock(g_gattObjectMutex);
        return CA_STATUS_OK;
    }

    uint32_t length = u_arraylist_length(g_gattObjectList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        jstring jni_setAddress = CALEClientGetAddressFromGattObj(env, jarrayObj);
        if (!jni_setAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_setAddress is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
        if (!setAddress)
        {
            OIC_LOG(ERROR, TAG, "setAddress is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        jstring jni_remoteAddress = CALEClientGetAddressFromGattObj(env, gatt);
        if (!jni_remoteAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_remoteAddress is null");
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        const char* remoteAddress = (*env)->GetStringUTFChars(env, jni_remoteAddress, NULL);
        if (!remoteAddress)
        {
            OIC_LOG(ERROR, TAG, "remoteAddress is null");
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        if (!strcmp(setAddress, remoteAddress))
        {
            OIC_LOG_V(DEBUG, TAG, "remove object : %s", remoteAddress);
            (*env)->DeleteGlobalRef(env, jarrayObj);
            jarrayObj = NULL;
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            (*env)->ReleaseStringUTFChars(env, jni_remoteAddress, remoteAddress);

            if (NULL == u_arraylist_remove(g_gattObjectList, index))
            {
                OIC_LOG(ERROR, TAG, "List removal failed.");
                ca_mutex_unlock(g_gattObjectMutex);
                return CA_STATUS_FAILED;
            }
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_OK;
        }
        (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
        (*env)->ReleaseStringUTFChars(env, jni_remoteAddress, remoteAddress);
    }

    ca_mutex_unlock(g_gattObjectMutex);
    OIC_LOG(DEBUG, TAG, "there are no target object");
    return CA_STATUS_OK;
}

CAResult_t CALEClientRemoveGattObjForAddr(JNIEnv *env, jstring addr)
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveGattObjForAddr");
    VERIFY_NON_NULL(addr, TAG, "addr is null");
    VERIFY_NON_NULL(env, TAG, "env is null");

    ca_mutex_lock(g_gattObjectMutex);
    if (!g_gattObjectList)
    {
        OIC_LOG(DEBUG, TAG, "already removed for g_gattObjectList");
        ca_mutex_unlock(g_gattObjectMutex);
        return CA_STATUS_OK;
    }

    uint32_t length = u_arraylist_length(g_gattObjectList);
    for (uint32_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        jstring jni_setAddress = CALEClientGetAddressFromGattObj(env, jarrayObj);
        if (!jni_setAddress)
        {
            OIC_LOG(ERROR, TAG, "jni_setAddress is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        const char* setAddress = (*env)->GetStringUTFChars(env, jni_setAddress, NULL);
        if (!setAddress)
        {
            OIC_LOG(ERROR, TAG, "setAddress is null");
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        const char* remoteAddress = (*env)->GetStringUTFChars(env, addr, NULL);
        if (!remoteAddress)
        {
            OIC_LOG(ERROR, TAG, "remoteAddress is null");
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_FAILED;
        }

        if (!strcmp(setAddress, remoteAddress))
        {
            // Found address match, remove object from g_gattObjecList
            OIC_LOG_V(DEBUG, TAG, "remove object : %s", remoteAddress);
            (*env)->DeleteGlobalRef(env, jarrayObj);
            jarrayObj = NULL;
            (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
            (*env)->ReleaseStringUTFChars(env, addr, remoteAddress);
            if (NULL == u_arraylist_remove(g_gattObjectList, index))
            {
                OIC_LOG(ERROR, TAG, "List removal failed.");
                ca_mutex_unlock(g_gattObjectMutex);
                return CA_STATUS_FAILED;
            }
            ca_mutex_unlock(g_gattObjectMutex);
            return CA_STATUS_OK;
        }
        (*env)->ReleaseStringUTFChars(env, jni_setAddress, setAddress);
        (*env)->ReleaseStringUTFChars(env, addr, remoteAddress);
    }

    ca_mutex_unlock(g_gattObjectMutex);
    OIC_LOG(DEBUG, TAG, "there is no target object");
    return CA_STATUS_FAILED;
}

int CALEClientGetDeviceBondState(JNIEnv *env, jobject btDevice)
{
    VERIFY_NON_NULL(env, TAG, "env is null");
    VERIFY_NON_NULL(btDevice, TAG, "btDevice is null");

    jclass jni_cid_BTDevice = (*env)->FindClass(env, CLASSPATH_BT_DEVICE);
    if (!jni_cid_BTDevice)
    {
        OIC_LOG(ERROR, TAG, "jni_cid_BTDevice is null");
        return -1;
    }

    jmethodID jni_mid_getBondState = (*env)->GetMethodID(env, jni_cid_BTDevice,
                                                         "getBondState", "()I");
    if (!jni_mid_getBondState)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getBondState is null");
        return -1;
    }

    jint jni_bond_state = (*env)->CallIntMethod(env, btDevice, jni_mid_getBondState);
    OIC_LOG_V(DEBUG, TAG, "CALECleintGetDeviceBondState - bond state: %d", jni_bond_state);
    return (int) jni_bond_state; 
}

jobject CALEClientGetCharFromDescriptor(JNIEnv *env, jobject descriptor)
{
    VERIFY_NON_NULL_RET(env, TAG, "env", NULL);
    VERIFY_NON_NULL_RET(descriptor, TAG, "descriptor", NULL);
    // Get BluetoothGattDescriptor class id
    jclass jni_cid_btGattDescriptor = (*env)->FindClass(env, 
            "android/bluetooth/BluetoothGattDescriptor");
    if (!jni_cid_btGattDescriptor) 
    {
        OIC_LOG(ERROR, TAG, "jni_cid_btGattDescriptor is null!");
        return NULL; 
    }
    // Get getCharacteristic method id from BluetoothGattDevice class
    jmethodID jni_mid_getCharacteristic = (*env)->GetMethodID(env, 
            jni_cid_btGattDescriptor, "getCharacteristic", 
            "()Landroid/bluetooth/BluetoothGattCharacteristic;"); 
    if (!jni_mid_getCharacteristic) {
        OIC_LOG(ERROR, TAG, "jni_mid_getCharacteristic is null!");
        return NULL;
    }
    // Call getCharacteristic method
    jobject jGattCharacteristic = (*env)->CallObjectMethod(env, descriptor, 
            jni_mid_getCharacteristic);
    if (!jGattCharacteristic)
    {
        OIC_LOG(ERROR, TAG, "getCharacteristic returned null!");
        return NULL;
    }
    return jGattCharacteristic;
}

jobject CALEClientGetUuidFromCharacteristic(JNIEnv *env, jobject characteristic)
{
    VERIFY_NON_NULL_RET(env, TAG, "env", NULL);
    VERIFY_NON_NULL_RET(characteristic, TAG, "characteristic", NULL);
    // Get BluetoothGattCharacteristic class id
    jclass jni_cid_btGattCharacteristic = (*env)->FindClass(env, 
            "android/bluetooth/BluetoothGattCharacteristic");
    if (!jni_cid_btGattCharacteristic) 
    {
        OIC_LOG(ERROR, TAG, "jni_cid_btGattCharacteristic is null!");
        return NULL; 
    }
    // Get getUuid method id
    jmethodID jni_mid_getUuid = (*env)->GetMethodID(env, 
            jni_cid_btGattCharacteristic, "getUuid", "()Ljava/util/UUID;");
    if (!jni_mid_getUuid)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getUuid is null!");
        return NULL;
    }
    // Call getUuid method
    jobject jUUID = (*env)->CallObjectMethod(env, characteristic, jni_mid_getUuid);
    if (!jUUID)
    {
        OIC_LOG(ERROR, TAG, "jUuid is null!");
        return NULL;
    }
    return jUUID;
}

jstring CALEClientGetStringFromUUID(JNIEnv *env, jobject uuid)
{
    VERIFY_NON_NULL_RET(env, TAG, "env", NULL);
    VERIFY_NON_NULL_RET(uuid, TAG, "uuid", NULL);
    // Get UUID class id
    jclass jni_cid_UUID = (*env)->FindClass(env, "java/util/UUID");
    if (!jni_cid_UUID) 
    {
        OIC_LOG(ERROR, TAG, "jni_cid_UUID is null!");
        return NULL;
    } 
    // Get toString method id
    jmethodID jni_mid_toString = (*env)->GetMethodID(env, jni_cid_UUID, 
            "toString", "()Ljava/lang/String;");
    if (!jni_mid_toString) 
    {
        OIC_LOG(ERROR, TAG, "jni_mid_toString is null!");
        return NULL;
    }
    // Call toString on uuid
    jstring jUuidString = (jstring) (*env)->CallObjectMethod(env, uuid, 
            jni_mid_toString);
    if (!jUuidString)
    {
        OIC_LOG(ERROR, TAG, "jUuidString is null!");
        return NULL;
    }
    return jUuidString;
}

jstring CALEClientGetLEAddressFromBTDevice(JNIEnv *env, jobject bluetoothDevice)
{
    VERIFY_NON_NULL_RET(env, TAG, "env", NULL);
    VERIFY_NON_NULL_RET(bluetoothDevice, TAG, "bluetoothDevice", NULL);

    // get Bluetooth Address
    jstring jni_btTargetAddress = CALEGetAddressFromBTDevice(env, bluetoothDevice);
    if (!jni_btTargetAddress)
    {
        OIC_LOG(ERROR, TAG, "CALEGetAddressFromBTDevice has failed");
        return NULL;
    }

    const char* targetAddress = (*env)->GetStringUTFChars(env, jni_btTargetAddress, NULL);
    if (!targetAddress)
    {
        OIC_LOG(ERROR, TAG, "targetAddress is not available");
        return NULL;
    }

    // get method ID of getDevice()
    jmethodID jni_mid_getDevice = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                   "getDevice", METHODID_BT_DEVICE);
    if (!jni_mid_getDevice)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDevice is null");
        (*env)->ReleaseStringUTFChars(env, jni_btTargetAddress, targetAddress);
        return NULL;
    }

    size_t length = u_arraylist_length(g_gattObjectList);
    for (size_t index = 0; index < length; index++)
    {
        jobject jarrayObj = (jobject) u_arraylist_get(g_gattObjectList, index);
        if (!jarrayObj)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            (*env)->ReleaseStringUTFChars(env, jni_btTargetAddress, targetAddress);
            return NULL;
        }

        jobject jni_obj_device = (*env)->CallObjectMethod(env, jarrayObj, jni_mid_getDevice);
        if (!jni_obj_device)
        {
            OIC_LOG(ERROR, TAG, "jni_obj_device is null");
            (*env)->ReleaseStringUTFChars(env, jni_btTargetAddress, targetAddress);
            return NULL;
        }

        jstring jni_btAddress = CALEGetAddressFromBTDevice(env, jni_obj_device);
        if (!jni_btAddress)
        {
            OIC_LOG(ERROR, TAG, "CALEGetAddressFromBTDevice has failed");
            (*env)->ReleaseStringUTFChars(env, jni_btTargetAddress, targetAddress);
            return NULL;
        }

        const char* btAddress = (*env)->GetStringUTFChars(env, jni_btAddress, NULL);
        if (!btAddress)
        {
            OIC_LOG(ERROR, TAG, "btAddress is not available");
            (*env)->ReleaseStringUTFChars(env, jni_btTargetAddress, targetAddress);
            return NULL;
        }

        if (!strcmp(targetAddress, btAddress))
        {
            // get LE address
            jstring jni_LEAddress = CALEClientGetAddressFromGattObj(env, jarrayObj);
            if (!jni_LEAddress)
            {
                OIC_LOG(ERROR, TAG, "jni_LEAddress is null");
            }
            (*env)->ReleaseStringUTFChars(env, jni_btTargetAddress, targetAddress);
            (*env)->ReleaseStringUTFChars(env, jni_btAddress, btAddress);
            (*env)->DeleteLocalRef(env, jni_btAddress);
            (*env)->DeleteLocalRef(env, jni_obj_device);
            return jni_LEAddress;
        }
        (*env)->ReleaseStringUTFChars(env, jni_btAddress, btAddress);
        (*env)->DeleteLocalRef(env, jni_btAddress);
        (*env)->DeleteLocalRef(env, jni_obj_device);
    }
    return NULL;
}

CAResult_t CALEClientSetMtuSize(const char* address, uint16_t mtuSize)
{
    VERIFY_NON_NULL(address, TAG, "address is null");

    ca_mutex_lock(g_deviceStateListMutex);
    if (CALEClientIsDeviceInList(address))
    {
        CALEState_t* curState = CALEClientGetStateInfo(address);
        if(!curState)
        {
            OIC_LOG(ERROR, TAG, "curState is null");
            ca_mutex_unlock(g_deviceStateListMutex);
            return CA_STATUS_FAILED;
        }

        curState->mtuSize = mtuSize;
        OIC_LOG_V(INFO, TAG, "update state - addr: %s, mtu: %d",
                  curState->address, curState->mtuSize);
    }
    else
    {
        OIC_LOG(ERROR, TAG, "there is no state info in the list");
    }
    ca_mutex_unlock(g_deviceStateListMutex);
    return CA_STATUS_OK;
}

uint16_t CALEClientGetMtuSize(const char* address)
{
    OIC_LOG_V(DEBUG, TAG, "IN - CALEClientGetMtuSize() addr: %s",
            address);
    VERIFY_NON_NULL_RET(address, TAG, "address is null", CA_DEFAULT_BLE_MTU_SIZE);

    ca_mutex_lock(g_deviceStateListMutex);
    if (CALEClientIsDeviceInList(address))
    {
        OIC_LOG_V(DEBUG, TAG, "Device is in list getting state");
        CALEState_t* curState = CALEClientGetStateInfo(address);
        if(!curState)
        {
            OIC_LOG(ERROR, TAG, "curState is null");
            ca_mutex_unlock(g_deviceStateListMutex);
            return CA_DEFAULT_BLE_MTU_SIZE;
        }

        OIC_LOG_V(INFO, TAG, "state - addr: %s, mtu: %d",
                  curState->address, curState->mtuSize);
        ca_mutex_unlock(g_deviceStateListMutex);
        return curState->mtuSize;
    }
    OIC_LOG_V(DEBUG, TAG, "Device is not in list, using default mtu size");
    ca_mutex_unlock(g_deviceStateListMutex);
    return CA_DEFAULT_BLE_MTU_SIZE;
}

/**
 * BT State List
 */

CAResult_t CALEClientUpdateDeviceState(const char* address, uint16_t state_type,
                                       uint16_t target_state)
{
    VERIFY_NON_NULL(address, TAG, "address is null");

    if (!g_deviceStateList)
    {
        OIC_LOG(ERROR, TAG, "gdevice_list is null");
        return CA_STATUS_FAILED;
    }

    ca_mutex_lock(g_deviceStateListMutex);

    if (CALEClientIsDeviceInList(address))
    {
        CALEState_t* curState = CALEClientGetStateInfo(address);
        if(!curState)
        {
            OIC_LOG(ERROR, TAG, "curState is null");
            ca_mutex_unlock(g_deviceStateListMutex);
            return CA_STATUS_FAILED;
        }

        switch(state_type)
        {
            case CA_LE_CONNECTION_STATE:
                curState->connectedState = target_state;
                break;
            case CA_LE_SEND_STATE:
                curState->sendState = target_state;
                break;
            default:
                break;
        }
        OIC_LOG_V(INFO, TAG, "update state - addr: %s, conn: %d, send: %d, ACFlag: %d, mtu: %d",
                  curState->address, curState->connectedState,
                  curState->sendState, curState->autoConnectFlag,
                  curState->mtuSize);
    }
    else /** state is added newly **/
    {
        if (strlen(address) > CA_MACADDR_SIZE)
        {
            OIC_LOG(ERROR, TAG, "address is not proper");
            ca_mutex_unlock(g_deviceStateListMutex);
            return CA_STATUS_INVALID_PARAM;
        }

        CALEState_t *newstate = (CALEState_t*) OICCalloc(1, sizeof(*newstate));
        if (!newstate)
        {
            OIC_LOG(ERROR, TAG, "out of memory");
            ca_mutex_unlock(g_deviceStateListMutex);
            return CA_MEMORY_ALLOC_FAILED;
        }

        OICStrcpy(newstate->address, sizeof(newstate->address), address);
        newstate->mtuSize = CA_DEFAULT_BLE_MTU_SIZE;
        switch(state_type)
        {
            case CA_LE_CONNECTION_STATE:
                newstate->connectedState = target_state;
                newstate->sendState = STATE_SEND_NONE;
                break;
            case CA_LE_SEND_STATE:
                newstate->connectedState = STATE_DISCONNECTED;
                newstate->sendState = target_state;
                break;
            default:
                break;
        }
        OIC_LOG_V(INFO, TAG, "Set newState to List - addr : %s, "
                  "conn : %d, send : %d, ACFlag : %d",
                  newstate->address, newstate->connectedState,
                  newstate->sendState, newstate->autoConnectFlag);
        u_arraylist_add(g_deviceStateList, newstate); // update new state
    }

    ca_mutex_unlock(g_deviceStateListMutex);

    return CA_STATUS_OK;
}

bool CALEClientIsDeviceInList(const char* remoteAddress)
{
    VERIFY_NON_NULL_RET(remoteAddress, TAG, "remoteAddress is null", false);

    if (!g_deviceStateList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceStateList is null");
        return false;
    }

    uint32_t length = u_arraylist_length(g_deviceStateList);
    for (uint32_t index = 0; index < length; index++)
    {
        CALEState_t* state = (CALEState_t*) u_arraylist_get(g_deviceStateList, index);
        if (!state)
        {
            OIC_LOG(ERROR, TAG, "CALEState_t object is null");
            return false;
        }

        if (!strcmp(remoteAddress, state->address))
        {
            OIC_LOG(DEBUG, TAG, "the device is already set");
            return true;
        }
        else
        {
            continue;
        }
    }

    OIC_LOG(DEBUG, TAG, "there are no the device in list.");
    return false;
}

CAResult_t CALEClientRemoveAllDeviceState()
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveAllDeviceState");

    ca_mutex_lock(g_deviceStateListMutex);
    if (!g_deviceStateList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceStateList is null");
        ca_mutex_unlock(g_deviceStateListMutex);
        return CA_STATUS_FAILED;
    }

    uint32_t length = u_arraylist_length(g_deviceStateList);
    for (uint32_t index = 0; index < length; index++)
    {
        CALEState_t* state = (CALEState_t*) u_arraylist_get(g_deviceStateList, index);
        if (!state)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            continue;
        }
        OICFree(state);
    }

    OICFree(g_deviceStateList);
    g_deviceStateList = NULL;
    ca_mutex_unlock(g_deviceStateListMutex);

    return CA_STATUS_OK;
}

CAResult_t CALEClientResetDeviceStateForAll()
{
    OIC_LOG(DEBUG, TAG, "CALEClientResetDeviceStateForAll");

    ca_mutex_lock(g_deviceStateListMutex);
    if (!g_deviceStateList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceStateList is null");
        ca_mutex_unlock(g_deviceStateListMutex);
        return CA_STATUS_FAILED;
    }

    size_t length = u_arraylist_length(g_deviceStateList);
    for (size_t index = 0; index < length; index++)
    {
        CALEState_t* state = (CALEState_t*) u_arraylist_get(g_deviceStateList, index);
        if (!state)
        {
            OIC_LOG(ERROR, TAG, "jarrayObj is null");
            continue;
        }

        // autoConnectFlag value will be not changed,
        // since it has reset only termination case.
        state->connectedState = STATE_DISCONNECTED;
        state->sendState = STATE_SEND_NONE;
    }
    ca_mutex_unlock(g_deviceStateListMutex);

    return CA_STATUS_OK;
}

CAResult_t CALEClientRemoveDeviceState(const char* remoteAddress)
{
    OIC_LOG(DEBUG, TAG, "CALEClientRemoveDeviceState");
    VERIFY_NON_NULL(remoteAddress, TAG, "remoteAddress is null");

    if (!g_deviceStateList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceStateList is null");
        return CA_STATUS_FAILED;
    }

    uint32_t length = u_arraylist_length(g_deviceStateList);
    for (uint32_t index = 0; index < length; index++)
    {
        CALEState_t* state = (CALEState_t*) u_arraylist_get(g_deviceStateList, index);
        if (!state)
        {
            OIC_LOG(ERROR, TAG, "CALEState_t object is null");
            continue;
        }

        if (!strcmp(state->address, remoteAddress))
        {
            OIC_LOG_V(DEBUG, TAG, "remove state : %s", state->address);

            CALEState_t* targetState  = (CALEState_t*)u_arraylist_remove(g_deviceStateList,
                                                                         index);
            if (NULL == targetState)
            {
                OIC_LOG(ERROR, TAG, "List removal failed.");
                return CA_STATUS_FAILED;
            }

            OICFree(targetState);
            return CA_STATUS_OK;
        }
    }

    return CA_STATUS_OK;
}

CALEState_t* CALEClientGetStateInfo(const char* remoteAddress)
{
    VERIFY_NON_NULL_RET(remoteAddress, TAG, "remoteAddress is null", NULL);

    if (!g_deviceStateList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceStateList is null");
        return NULL;
    }

    uint32_t length = u_arraylist_length(g_deviceStateList);

    for (uint32_t index = 0; index < length; index++)
    {
        CALEState_t* state = (CALEState_t*) u_arraylist_get(g_deviceStateList, index);
        if (!state)
        {
            OIC_LOG(ERROR, TAG, "CALEState_t object is null");
            continue;
        }

        if (!strcmp(state->address, remoteAddress))
        {
            return state;
        }
    }
    return NULL;
}

bool CALEClientIsValidState(const char* remoteAddress, uint16_t state_type,
                             uint16_t target_state)
{
    OIC_LOG_V(DEBUG, TAG, "CALEClientIsValidState : type[%d], target state[%d]",
              state_type, target_state);
    VERIFY_NON_NULL_RET(remoteAddress, TAG, "remoteAddress is null", false);

    ca_mutex_lock(g_deviceStateListMutex);
    if (!g_deviceStateList)
    {
        OIC_LOG(ERROR, TAG, "g_deviceStateList is null");
        ca_mutex_unlock(g_deviceStateListMutex);
        return false;
    }

    CALEState_t* state = CALEClientGetStateInfo(remoteAddress);
    if (NULL == state)
    {
        OIC_LOG(ERROR, TAG, "state is null");
        ca_mutex_unlock(g_deviceStateListMutex);
        return false;
    }

    uint16_t curValue = 0;
    switch(state_type)
    {
        case CA_LE_CONNECTION_STATE:
            curValue = state->connectedState;
            break;
        case CA_LE_SEND_STATE:
            curValue = state->sendState;
            break;
        default:
            break;
    }

    if (target_state == curValue)
    {
        ca_mutex_unlock(g_deviceStateListMutex);
        return true;
    }
    else
    {
        ca_mutex_unlock(g_deviceStateListMutex);
        return false;
    }

    ca_mutex_unlock(g_deviceStateListMutex);
    return false;
}

void CALEClientCreateDeviceList()
{
    OIC_LOG(DEBUG, TAG, "CALEClientCreateDeviceList");

    // create new object array
    if (!g_gattObjectList)
    {
        OIC_LOG(DEBUG, TAG, "Create g_gattObjectList");

        g_gattObjectList = u_arraylist_create();
    }

    if (!g_deviceStateList)
    {
        OIC_LOG(DEBUG, TAG, "Create g_deviceStateList");

        g_deviceStateList = u_arraylist_create();
    }

    if (!g_deviceList)
    {
        OIC_LOG(DEBUG, TAG, "Create g_deviceList");

        g_deviceList = u_arraylist_create();
    }
}

/**
 * Check Sent Count for remove g_sendBuffer
 */
void CALEClientUpdateSendCnt(JNIEnv *env)
{
    OIC_LOG(DEBUG, TAG, "CALEClientUpdateSendCnt");

    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    // mutex lock
    ca_mutex_lock(g_threadMutex);

    g_currentSentCnt++;

    if (g_targetCnt <= g_currentSentCnt)
    {
        g_targetCnt = 0;
        g_currentSentCnt = 0;

        if (g_sendBuffer)
        {
            (*env)->DeleteGlobalRef(env, g_sendBuffer);
            g_sendBuffer = NULL;
        }
        // notity the thread
        ca_cond_signal(g_threadCond);

        CALEClientSetSendFinishFlag(true);
        OIC_LOG(DEBUG, TAG, "set signal for send data");
    }
    // mutex unlock
    ca_mutex_unlock(g_threadMutex);
}

CAResult_t CALEClientInitGattMutexVaraibles()
{
    if (NULL == g_bleReqRespClientCbMutex)
    {
        g_bleReqRespClientCbMutex = ca_mutex_new();
        if (NULL == g_bleReqRespClientCbMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_bleServerBDAddressMutex)
    {
        g_bleServerBDAddressMutex = ca_mutex_new();
        if (NULL == g_bleServerBDAddressMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_threadMutex)
    {
        g_threadMutex = ca_mutex_new();
        if (NULL == g_threadMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_threadSendMutex)
    {
        g_threadSendMutex = ca_mutex_new();
        if (NULL == g_threadSendMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_deviceListMutex)
    {
        g_deviceListMutex = ca_mutex_new();
        if (NULL == g_deviceListMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_gattObjectMutex)
    {
        g_gattObjectMutex = ca_mutex_new();
        if (NULL == g_gattObjectMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_deviceStateListMutex)
    {
        g_deviceStateListMutex = ca_mutex_new();
        if (NULL == g_deviceStateListMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_SendFinishMutex)
    {
        g_SendFinishMutex = ca_mutex_new();
        if (NULL == g_SendFinishMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_scanMutex)
    {
        g_scanMutex = ca_mutex_new();
        if (NULL == g_scanMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_threadWriteCharacteristicMutex)
    {
        g_threadWriteCharacteristicMutex = ca_mutex_new();
        if (NULL == g_threadWriteCharacteristicMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_deviceScanRetryDelayMutex)
    {
        g_deviceScanRetryDelayMutex = ca_mutex_new();
        if (NULL == g_deviceScanRetryDelayMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_threadSendStateMutex)
    {
        g_threadSendStateMutex = ca_mutex_new();
        if (NULL == g_threadSendStateMutex)
        {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_bondMutex) {
        g_bondMutex = ca_mutex_new();
        if (NULL == g_bondMutex) {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_responseCharMutex) {
        g_responseCharMutex = ca_mutex_new();
        if (NULL == g_responseCharMutex) {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }
    
    if (NULL == g_writeDescMutex) {
        g_writeDescMutex = ca_mutex_new();
        if (NULL == g_writeDescMutex) {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }

    if (NULL == g_discoverServicesDelayMutex) {
        g_discoverServicesDelayMutex = ca_mutex_new();
        if (NULL == g_discoverServicesDelayMutex) {
            OIC_LOG(ERROR, TAG, "ca_mutex_new has failed");
            return CA_STATUS_FAILED;
        }
    }
    return CA_STATUS_OK;
}

void CALEClientTerminateGattMutexVariables()
{
    ca_mutex_free(g_bleReqRespClientCbMutex);
    g_bleReqRespClientCbMutex = NULL;

    ca_mutex_free(g_bleServerBDAddressMutex);
    g_bleServerBDAddressMutex = NULL;

    ca_mutex_free(g_threadMutex);
    g_threadMutex = NULL;

    ca_mutex_free(g_threadSendMutex);
    g_threadSendMutex = NULL;

    ca_mutex_free(g_deviceListMutex);
    g_deviceListMutex = NULL;

    ca_mutex_free(g_SendFinishMutex);
    g_SendFinishMutex = NULL;

    ca_mutex_free(g_scanMutex);
    g_scanMutex = NULL;

    ca_mutex_free(g_threadWriteCharacteristicMutex);
    g_threadWriteCharacteristicMutex = NULL;

    ca_mutex_free(g_deviceScanRetryDelayMutex);
    g_deviceScanRetryDelayMutex = NULL;

    ca_mutex_free(g_threadSendStateMutex);
    g_threadSendStateMutex = NULL;

    ca_mutex_free(g_bondMutex);
    g_bondMutex = NULL;

    ca_mutex_free(g_responseCharMutex);
    g_responseCharMutex = NULL;

    ca_mutex_free(g_writeDescMutex);
    g_writeDescMutex = NULL;

    ca_mutex_free(g_discoverServicesDelayMutex);
    g_discoverServicesDelayMutex = NULL;

}

void CALEClientSetSendFinishFlag(bool flag)
{
    OIC_LOG_V(DEBUG, TAG, "g_isFinishedSendData is %d", flag);

    ca_mutex_lock(g_SendFinishMutex);
    g_isFinishedSendData = flag;
    ca_mutex_unlock(g_SendFinishMutex);
}

/**
 * adapter common
 */

CAResult_t CAStartLEGattClient()
{
    // init mutex for send logic
    if (!g_deviceDescCond)
    {
        g_deviceDescCond = ca_cond_new();
    }

    if (!g_threadCond)
    {
        g_threadCond = ca_cond_new();
    }

    if (!g_threadWriteCharacteristicCond)
    {
        g_threadWriteCharacteristicCond = ca_cond_new();
    }

    if (!g_writeDescCond) {
        g_writeDescCond = ca_cond_new();
    }
    
    if (!g_bondCond) {
        g_bondCond = ca_cond_new();
    }
    
    if (!g_removeBondCond) {
        g_removeBondCond = ca_cond_new();
    }
    if (!g_discoverServicesDelayCond) {
        g_discoverServicesDelayCond = ca_cond_new();
    }
    
    CAResult_t ret = CALEClientStartScan();
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientStartScan has failed");
        return ret;
    }

    g_isStartedLEClient = true;
    return CA_STATUS_OK;
}

void CAStopLEGattClient()
{
    OIC_LOG(DEBUG, TAG, "CAStopBLEGattClient");

    if (!g_jvm)
    {
        OIC_LOG(ERROR, TAG, "g_jvm is null");
        return;
    }

    bool isAttached = false;
    JNIEnv* env;
    jint res = (*g_jvm)->GetEnv(g_jvm, (void**) &env, JNI_VERSION_1_6);
    if (JNI_OK != res)
    {
        OIC_LOG(INFO, TAG, "Could not get JNIEnv pointer");
        res = (*g_jvm)->AttachCurrentThread(g_jvm, &env, NULL);

        if (JNI_OK != res)
        {
            OIC_LOG(ERROR, TAG, "AttachCurrentThread has failed");
            return;
        }
        isAttached = true;
    }

    CAResult_t ret = CALEClientDisconnectAll(env);
    if (CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientDisconnectAll has failed");
    }

    ret = CALEClientStopScan();
    if(CA_STATUS_OK != ret)
    {
        OIC_LOG(ERROR, TAG, "CALEClientStopScan has failed");
    }

    ca_mutex_lock(g_threadMutex);
    OIC_LOG(DEBUG, TAG, "signal - connection cond");
    ca_cond_signal(g_threadCond);
    CALEClientSetSendFinishFlag(true);
    ca_mutex_unlock(g_threadMutex);

    ca_mutex_lock(g_threadWriteCharacteristicMutex);
    OIC_LOG(DEBUG, TAG, "signal - WriteCharacteristic cond");
    ca_cond_signal(g_threadWriteCharacteristicCond);
    ca_mutex_unlock(g_threadWriteCharacteristicMutex);

    ca_mutex_lock(g_deviceScanRetryDelayMutex);
    OIC_LOG(DEBUG, TAG, "signal - delay cond");
    ca_cond_signal(g_deviceScanRetryDelayCond);
    ca_mutex_unlock(g_deviceScanRetryDelayMutex);

    ca_mutex_lock(g_writeDescMutex);
    OIC_LOG(DEBUG, TAG, "signal - write descriptor cond");
    ca_cond_signal(g_writeDescCond);
    ca_mutex_unlock(g_writeDescMutex);

    ca_mutex_lock(g_discoverServicesDelayMutex);
    OIC_LOG(DEBUG, TAG, "signal - discover services delay cond");
    ca_cond_signal(g_discoverServicesDelayCond);
    ca_mutex_unlock(g_discoverServicesDelayMutex);


    ca_mutex_lock(g_bondMutex);
    OIC_LOG(DEBUG, TAG, "signal - bond cond");
    ca_cond_signal(g_bondCond);
    ca_cond_signal(g_removeBondCond);
    ca_mutex_unlock(g_bondMutex);

    ca_cond_free(g_deviceDescCond);
    ca_cond_free(g_threadCond);
    ca_cond_free(g_threadWriteCharacteristicCond);
    ca_cond_free(g_deviceScanRetryDelayCond);
    ca_cond_free(g_writeDescCond);
    ca_cond_free(g_discoverServicesDelayCond);
    ca_cond_free(g_bondCond);
    ca_cond_free(g_removeBondCond);

    g_deviceDescCond = NULL;
    g_threadCond = NULL;
    g_threadWriteCharacteristicCond = NULL;
    g_deviceScanRetryDelayCond = NULL;
    g_writeDescCond = NULL;
    g_discoverServicesDelayCond = NULL;
    g_bondCond = NULL;
    g_removeBondCond = NULL;

    if (isAttached)
    {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }

}

CAResult_t CAInitializeLEGattClient()
{
    OIC_LOG(DEBUG, TAG, "Initialize GATT Client");
    CALEClientInitialize();
    return CA_STATUS_OK;
}

void CATerminateLEGattClient()
{
    OIC_LOG(DEBUG, TAG, "Terminate GATT Client");
    CAStopLEGattClient();
    CALEClientTerminate();
}

CAResult_t  CAUpdateCharacteristicsToGattServer(const char *remoteAddress, const uint8_t  *data,
                                                uint32_t dataLen, CALETransferType_t type,
                                                int32_t position)
{
    OIC_LOG(DEBUG, TAG, "CAUpdateCharacteristicsToGattServer");
    VERIFY_NON_NULL(remoteAddress, TAG, "remoteAddress is null");

    if (LE_UNICAST != type || position < 0)
    {
        OIC_LOG(ERROR, TAG, "this request is not unicast");
        return CA_STATUS_INVALID_PARAM;
    }

    return CALEClientSendUnicastMessage(remoteAddress, data, dataLen);
}

CAResult_t CAUpdateCharacteristicsToAllGattServers(const uint8_t *data, uint32_t dataLen)
{
    OIC_LOG(DEBUG, TAG, "call CALEClientSendMulticastMessage");
    VERIFY_NON_NULL(data, TAG, "data is null");

    return CALEClientSendMulticastMessage(data, dataLen);
}

void CASetLEReqRespClientCallback(CABLEDataReceivedCallback callback)
{
    ca_mutex_lock(g_bleReqRespClientCbMutex);
    g_CABLEClientDataReceivedCallback = callback;
    ca_mutex_unlock(g_bleReqRespClientCbMutex);
}

void CASetLEClientThreadPoolHandle(ca_thread_pool_t handle)
{
    g_threadPoolHandle = handle;
}

CAResult_t CAGetLEAddress(char **local_address)
{
    VERIFY_NON_NULL(local_address, TAG, "local_address");
    OIC_LOG(INFO, TAG, "CAGetLEAddress is not support");
    return CA_NOT_SUPPORTED;
}

JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeRegisterLeScanCallback(JNIEnv *env, jobject obj,
                                                                    jobject callback)
{
    OIC_LOG(DEBUG, TAG, "CaLeRegisterLeScanCallback");
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(callback, TAG, "callback is null");

    g_leScanCallback = (*env)->NewGlobalRef(env, callback);
}

JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeRegisterGattCallback(JNIEnv *env, jobject obj,
                                                                  jobject callback)
{
    OIC_LOG(DEBUG, TAG, "CaLeRegisterGattCallback");
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(callback, TAG, "callback is null");

    g_leGattCallback = (*env)->NewGlobalRef(env, callback);
}

JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeScanCallback(JNIEnv *env, jobject obj,
                                                          jobject device)
{
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(device, TAG, "device is null");

    CAResult_t res = CALEClientAddScanDeviceToList(env, device);
    if (CA_STATUS_OK != res)
    {
        OIC_LOG_V(ERROR, TAG, "CALEClientAddScanDeviceToList has failed : %d", res);
    }
}

static jstring CALEClientGetAddressFromGatt(JNIEnv *env, jobject gatt)
{
    OIC_LOG(DEBUG, TAG, "IN - CAManagerGetAddressFromGatt");

    VERIFY_NON_NULL_RET(env, TAG, "env is null", NULL);
    VERIFY_NON_NULL_RET(gatt, TAG, "gatt is null", NULL);

    jmethodID jni_mid_getDevice = CAGetJNIMethodID(env, CLASSPATH_BT_GATT,
                                                   "getDevice", METHODID_BT_DEVICE);
    if (!jni_mid_getDevice)
    {
        OIC_LOG(ERROR, TAG, "jni_mid_getDevice is null");
        return NULL;
    }

    jobject jni_obj_device = (*env)->CallObjectMethod(env, gatt, jni_mid_getDevice);
    if (!jni_obj_device)
    {
        OIC_LOG(ERROR, TAG, "jni_obj_device is null");
        return NULL;
    }

    jstring jni_address = CALEGetAddressFromBTDevice(env, jni_obj_device);
    if (!jni_address)
    {
        OIC_LOG(ERROR, TAG, "jni_address is null");
        return NULL;
    }

    OIC_LOG(DEBUG, TAG, "OUT - CAManagerGetAddressFromGatt");
    return jni_address;
}

/*
 * Class:     org_iotivity_ca_jar_caleinterface
 * Method:    CALeGattConnectionStateChangeCallback
 * Signature: (Landroid/bluetooth/BluetoothGatt;II)V
 */
JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeGattConnectionStateChangeCallback(JNIEnv *env,
                                                                                jobject obj,
                                                                                jobject gatt,
                                                                                jint status,
                                                                                jint newstate)
{
    OIC_LOG_V(DEBUG, TAG, "CALeGattConnectionStateChangeCallback - status %d, newstate %d", status,
            newstate);
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(gatt, TAG, "gatt is null");

    jint state_connected = CALEGetConstantsValue(env, CLASSPATH_BT_PROFILE, "STATE_CONNECTED");
    jint state_disconnected = CALEGetConstantsValue(env, CLASSPATH_BT_PROFILE, "STATE_DISCONNECTED");
    jint gatt_success = CALEGetConstantsValue(env, CLASSPATH_BT_GATT, "GATT_SUCCESS");
    
    // Status success and state connected
    if (gatt_success == status && state_connected == newstate) // le connected
    {
        jstring jni_address = CALEClientGetAddressFromGattObj(env, gatt);
        if (!jni_address)
        {
            goto error_exit;
        }

        const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
        if (address)
        {
            // Update device state
            CAResult_t res = CALEClientUpdateDeviceState(address, CA_LE_CONNECTION_STATE,
                                                         STATE_CONNECTED);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                goto error_exit;
            }
            OIC_LOG_V(INFO, TAG, "ConnectionStateCB - remote address : %s", address);
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
        }
        // Add gatt object to list
        CAResult_t res = CALEClientAddGattobjToList(env, gatt);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientAddGattobjToList has failed");
            goto error_exit;
        }
            
        // Get bluetooth device from the gatt object
        jobject bluetoothDevice = CALEClientGetDeviceFromGatt(env, gatt);
        if (!bluetoothDevice) {
            OIC_LOG(ERROR, TAG, "bluetoothDevice is null!");
            goto error_exit;
        }
       
        // Create a bond with the bluetooth device if not already
        if (BOND_BONDED != CALEClientGetDeviceBondState(env, bluetoothDevice)) {
            OIC_LOG(DEBUG, TAG, "Device is not bonded, begin bonding process");
            bool bondCreated = CALEClientCreateBond(env, bluetoothDevice);
            if (!bondCreated)
            {
                OIC_LOG(ERROR, TAG, "CALEClientCreateBond returned false");
            }

        }
        // if we discover services too quickly after bonding, we fail to receive a callback.
        OIC_LOG(DEBUG, TAG, "Waiting 1 second before discovering services");
        ca_cond_wait_for(g_discoverServicesDelayCond, g_discoverServicesDelayMutex, 
                    WAIT_TIME_DISCOVER_SERVICES);

        // Discover services
        res = CALEClientDiscoverServices(env, gatt);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientDiscoverServices has failed");
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
            goto error_exit;
        }
    }
    // Gatt has disconnected
    else if (state_disconnected == newstate) // le disconnected
    {
        jstring jni_address = CALEClientGetAddressFromGattObj(env, gatt);
        if (!jni_address)
        {
            OIC_LOG(ERROR, TAG, "CALEClientGetAddressFromGattObj has failed");
            goto error_exit;
        }

        const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
        if (address)
        {
            // Update device state
            CAResult_t res = CALEClientUpdateDeviceState(address, CA_LE_CONNECTION_STATE,
                                                         STATE_DISCONNECTED);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                goto error_exit;
            }
            OIC_LOG_V(INFO, TAG, "ConnectionStateCB - remote address : %s", address);
            
            /** 
             * Because we have disconnected from a gatt server we have to remove the
             * response characteristic UUID key from the list for each entry corresponding
             * to the disconnected device.
             */
            res = CALEClientRemoveResponseCharListKeysForAddress(env, jni_address); 
            if (res != CA_STATUS_OK) {
                OIC_LOG(ERROR, TAG, "Failed to remove response keys from list");
            }

            // Release address string
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
        }
        
        // Close gatt
        CAResult_t res = CALEClientGattClose(env, gatt);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientGattClose has failed");
        }

        if (GATT_ERROR == status)
        {
            // when we get GATT ERROR(0x85), gatt connection can be called again.
            OIC_LOG(INFO, TAG, "retry gatt connect");

            jstring leAddress = CALEClientGetAddressFromGatt(env, gatt);
            if (!leAddress)
            {
                OIC_LOG(ERROR, TAG, "CALEClientGetAddressFromGatt has failed");
                goto error_exit;
            }

            jobject btObject = CALEGetRemoteDevice(env, leAddress);
            if (!btObject)
            {
                OIC_LOG(ERROR, TAG, "CALEGetRemoteDevice has failed");
                goto error_exit;
            }

            jobject newGatt = CALEClientConnect(env, btObject, JNI_TRUE);
            if (!newGatt)
            {
                OIC_LOG(ERROR, TAG, "CALEClientConnect has failed");
                goto error_exit;
            }

            return;
        }
        else
        {
            if (CALECheckConnectionStateValue(status))
            {
                // this state is unexpected reason to disconnect
                // if the reason is suitable, connection logic of the device will be destroyed.
                OIC_LOG(INFO, TAG, "connection logic destroy");
                goto error_exit;
            }
            else
            {
                // other reason except for gatt_success is expected to running
                // background connection in BT platform.
                OIC_LOG(INFO, TAG, "unknown state or manual disconnected state");
                CALEClientUpdateSendCnt(env);
                return;
            }
        }

        if (g_sendBuffer)
        {
            (*env)->DeleteGlobalRef(env, g_sendBuffer);
            g_sendBuffer = NULL;
        }
    }
    return;

    // error label.
error_exit:

    CALEClientSendFinish(env, gatt);
    return;
}

/*
 * Class:     org_iotivity_ca_jar_caleinterface
 * Method:    CALeGattServicesDiscoveredCallback
 * Signature: (Landroid/bluetooth/BluetoothGatt;I)V
 */
JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeGattServicesDiscoveredCallback(JNIEnv *env,
                                                                             jobject obj,
                                                                             jobject gatt,
                                                                             jint status)
{
    OIC_LOG_V(DEBUG, TAG, "CALeGattServicesDiscoveredCallback - status %d: ", status);
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(gatt, TAG, "gatt is null");

    if (0 != status) // discovery error
    {
        CALEClientSendFinish(env, gatt);
        return;
    }

    OIC_LOG(INFO, TAG, "Service Discovery is successful, requesting MTU");
    //if (g_sendBuffer)
    CAResult_t res = CALEClientRequestMTU(env, gatt, CA_SUPPORTED_BLE_MTU_SIZE); 
    if (CA_STATUS_OK != res)
    {
        /*
        CAResult_t res = CALEClientWriteCharacteristic(env, gatt);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientWriteCharacteristic has failed");
            goto error_exit;
        }
        */
        OIC_LOG(ERROR, TAG, "CALEClientRequestMTU has failed");
        goto error_exit;
    }
    return;

    // error label.
error_exit:
    OIC_LOG(ERROR, TAG, "Service Discovery has failed");
    CALEClientSendFinish(env, gatt);
    return;
}

/*
 * Class:     org_iotivity_ca_jar_caleinterface
 * Method:    CALeGattCharacteristicWritjclasseCallback
 * Signature: (Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;I)V
 */
JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeGattCharacteristicWriteCallback(
        JNIEnv *env, jobject obj, jobject gatt, jbyteArray data,
        jint status)
{
    OIC_LOG_V(DEBUG, TAG, "CALeGattCharacteristicWriteCallback - status : %d", status);
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(gatt, TAG, "gatt is null");

    // send success & signal
    jstring jni_address = CALEClientGetAddressFromGattObj(env, gatt);
    if (!jni_address)
    {
        goto error_exit;
    }

    const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
    if (!address)
    {
        goto error_exit;
    }

    jint gatt_success = CALEGetConstantsValue(env, CLASSPATH_BT_GATT, "GATT_SUCCESS");
    if (gatt_success != status) // error case
    {
        OIC_LOG(ERROR, TAG, "send failure");

        // retry to write
        CAResult_t res = CALEClientWriteCharacteristic(env, gatt);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "WriteCharacteristic has failed");
            ca_mutex_lock(g_threadWriteCharacteristicMutex);
            g_isSignalSetFlag = true;
            ca_cond_signal(g_threadWriteCharacteristicCond);
            ca_mutex_unlock(g_threadWriteCharacteristicMutex);

            CAResult_t res = CALEClientUpdateDeviceState(address, CA_LE_SEND_STATE,
                                                         STATE_SEND_FAIL);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
            }

            if (g_clientErrorCallback)
            {
                jint length = (*env)->GetArrayLength(env, data);
                g_clientErrorCallback(address, data, length, CA_SEND_FAILED);
            }

            CALEClientSendFinish(env, gatt);
            goto error_exit;
        }
    }
    else
    {
        OIC_LOG(DEBUG, TAG, "send success");
        CAResult_t res = CALEClientUpdateDeviceState(address, CA_LE_SEND_STATE,
                                                     STATE_SEND_SUCCESS);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
        }

        ca_mutex_lock(g_threadWriteCharacteristicMutex);
        OIC_LOG(DEBUG, TAG, "g_isSignalSetFlag is set true and signal");
        g_isSignalSetFlag = true;
        ca_cond_signal(g_threadWriteCharacteristicCond);
        ca_mutex_unlock(g_threadWriteCharacteristicMutex);

        CALEClientUpdateSendCnt(env);
    }

    (*env)->ReleaseStringUTFChars(env, jni_address, address);
    return;

    // error label.
error_exit:

    CALEClientSendFinish(env, gatt);
    return;
}

/*
 * Class:     org_iotivity_ca_jar_caleinterface
 * Method:    CALeGattCharacteristicChangedCallback
 * Signature: (Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattCharacteristic;)V
 */
JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeGattCharacteristicChangedCallback(
        JNIEnv *env, jobject obj, jobject gatt, jbyteArray data)
{
    OIC_LOG(DEBUG, TAG, "CALeGattCharacteristicChangedCallback");
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(gatt, TAG, "gatt is null");
    VERIFY_NON_NULL_VOID(data, TAG, "data is null");

    // get Byte Array and convert to uint8_t*
    jint length = (*env)->GetArrayLength(env, data);

    jboolean isCopy;
    jbyte *jni_byte_responseData = (jbyte*) (*env)->GetByteArrayElements(env, data, &isCopy);

    OIC_LOG_V(DEBUG, TAG, "CALeGattCharacteristicChangedCallback - raw data received : %p",
            jni_byte_responseData);

    uint8_t* receivedData = OICMalloc(length);
    if (!receivedData)
    {
        OIC_LOG(ERROR, TAG, "receivedData is null");
        return;
    }

    memcpy(receivedData, jni_byte_responseData, length);
    (*env)->ReleaseByteArrayElements(env, data, jni_byte_responseData, JNI_ABORT);

    jstring jni_address = CALEClientGetAddressFromGattObj(env, gatt);
    if (!jni_address)
    {
        OIC_LOG(ERROR, TAG, "jni_address is null");
        OICFree(receivedData);
        return;
    }

    const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
    if (!address)
    {
        OIC_LOG(ERROR, TAG, "address is null");
        OICFree(receivedData);
        return;
    }

    OIC_LOG_V(DEBUG, TAG, "CALeGattCharacteristicChangedCallback - data. : %p, %d",
              receivedData, length);

    ca_mutex_lock(g_bleServerBDAddressMutex);
    uint32_t sentLength = 0;
    g_CABLEClientDataReceivedCallback(address, receivedData, length,
                                      &sentLength);
    ca_mutex_unlock(g_bleServerBDAddressMutex);

    (*env)->ReleaseStringUTFChars(env, jni_address, address);
}

/*
 * Class:     org_iotivity_ca_jar_caleinterface
 * Method:    CALeGattDescriptorWriteCallback
 * Signature: (Landroid/bluetooth/BluetoothGatt;Landroid/bluetooth/BluetoothGattDescriptor;I)V
 */
JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeGattDescriptorWriteCallback
(JNIEnv *env, jobject obj, jobject gatt, jobject descriptor, jint status)
{
    OIC_LOG_V(DEBUG, TAG, "CALeGattDescriptorWriteCallback - status %d: ", status);
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(gatt, TAG, "gatt is null");

    jint gatt_success = CALEGetConstantsValue(env, CLASSPATH_BT_GATT, "GATT_SUCCESS");
    if (gatt_success != status && status != 28) // TODO 28 is the callback when bonding 
    {
        goto error_exit;
    }

    jstring jni_address = CALEClientGetAddressFromGattObj(env, gatt);
    if (!jni_address)
    {
        goto error_exit;
    }

    // TODO we could add a check to see if the written descriptor is in fact the
    // CCCD, but since we only ever write to the CCCD, this is sufficient for now
    
    // Get the characeristic from the descriptor 
    jobject jCharacteristic = CALEClientGetCharFromDescriptor(env, descriptor);
    if (!jCharacteristic) 
    {
        OIC_LOG(ERROR, TAG, "CALEClientGetCharFromDescriptor has failed");
        goto error_exit;
    }
    // Get the characteristic's UUID
    jobject jCharUUID = CALEClientGetUuidFromCharacteristic(env, jCharacteristic);
    if (!jCharUUID) 
    {
        OIC_LOG(ERROR, TAG, "CALEClientGetUuidFromCharacteristic has failed");
        goto error_exit;
    }
    
    CAResult_t res = CALEClientAddResponseCharToList(env, gatt, jCharUUID);
    if (CA_STATUS_OK != res)
    {
        // If adding fails, we can continue on without erroring
        OIC_LOG(ERROR, TAG, "CALEClientAddResponseCharToList has failed");
    }

    // Signal anything waiting for descriptor write
    ca_mutex_lock(g_writeDescMutex);
    ca_cond_signal(g_writeDescCond);
    ca_mutex_unlock(g_writeDescMutex);

    const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
    if (address)
    {
        CAResult_t res = CALEClientUpdateDeviceState(address, CA_LE_CONNECTION_STATE,
                                                     STATE_SERVICE_CONNECTED);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
            goto error_exit;
        }
    }
    /* 
    CALEState_t* deviceState = CALEClientGetStateInfo(address);
    if (!deviceState) 
    {
        OIC_LOG(ERROR, TAG, "CALEClientGetStateInfo has returned null");
        (*env)->ReleaseStringUTFChars(env, jni_address, address);
        goto error_exit;
    }
    if (deviceState->mtuSize == ) 
    {
        CAResult_t res = CALEClientRequestMTU(env, gatt, CA_SUPPORTED_BLE_MTU_SIZE);
        if (CA_STATUS_OK != res)
        {
            OIC_LOG(ERROR, TAG, "CALEClientRequestMTU has failed");
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
            goto error_exit;
        }
    }
    OIC_LOG_V(DEBUG, TAG, "Device sendState = %d", deviceState->sendState);
    */
    CALEClientUpdateSendCnt(env);
    (*env)->ReleaseStringUTFChars(env, jni_address, address);
    return;

// error label.
error_exit:

    CALEClientSendFinish(env, gatt);
    return;
}


JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeGattMtuChangedCallback(JNIEnv *env,
                                                                    jobject obj,
                                                                    jobject gatt,
                                                                    jint mtu,
                                                                    jint status)
{
    OIC_LOG_V(INFO, TAG, "caLeGattMtuChangedCallback - mtu[%d-including Header size 3 byte)", mtu);
    OIC_LOG_V(INFO, TAG, "caLeGattMtuChangedCallback - status %d", status);

    (void)obj;

    if (0 == status)
    {
        if (g_sendBuffer)
        {
            CAResult_t res = CALEClientWriteCharacteristic(env, gatt);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientWriteCharacteristic has failed");
            }
        }
        else
        {
            OIC_LOG(INFO, TAG, "mtu nego is done");
            jstring jni_address = CALEClientGetAddressFromGattObj(env, gatt);
            if (!jni_address)
            {
                CALEClientSendFinish(env, gatt);
                return;
            }

            const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
            if (!address)
            {
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                CALEClientSendFinish(env, gatt);
                return;
            }

            // update mtu size
            CAResult_t res = CALEClientSetMtuSize(address, mtu - CA_BLE_MTU_HEADER_SIZE);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientSetMtuSize has failed");
            }

            res = CALEClientUpdateDeviceState(address, CA_LE_SEND_STATE,
                                              STATE_SEND_MTU_NEGO_SUCCESS);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
            }
            
            /**
             * After successfully negotiating the MTU, write the CCCD of the response
             * characteristic.
             */

            jstring jni_uuid = (*env)->NewStringUTF(env, g_gattResponseCharacteristicUUID);
            if (!jni_uuid)
            {
                OIC_LOG(ERROR, TAG, "jni_uuid is null");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                CALEClientSendFinish(env, gatt);
                return;
            }

            jobject jni_obj_GattCharacteristic = CALEClientGetGattCharacteristic(env, gatt, jni_uuid);
            if (!jni_obj_GattCharacteristic)
            {
                OIC_LOG(ERROR, TAG, "jni_obj_GattCharacteristic is null");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                CALEClientSendFinish(env, gatt);
                return;
            }

            res = CALEClientSetCharacteristicNotification(env, gatt,
                                                             jni_obj_GattCharacteristic);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientSetCharacteristicNotification has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                CALEClientSendFinish(env, gatt);
                return;
            }

            res = CALEClientSetUUIDToDescriptor(env, gatt, jni_obj_GattCharacteristic);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG_V(INFO, TAG, "Descriptor is not found : %d", res);
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                CALEClientSendFinish(env, gatt);
                return;
            } 
            res = CALEClientUpdateDeviceState(address, CA_LE_CONNECTION_STATE ,
                                          STATE_SERVICE_CONNECTED);
            if (CA_STATUS_OK != res)
            {
                OIC_LOG(ERROR, TAG, "CALEClientUpdateDeviceState has failed");
                (*env)->ReleaseStringUTFChars(env, jni_address, address);
                CALEClientSendFinish(env, gatt);
                return;

            }

            //CALEClientUpdateSendCnt(env);
            (*env)->ReleaseStringUTFChars(env, jni_address, address);
            (*env)->DeleteLocalRef(env, jni_address);
        }
    }
}

JNIEXPORT void JNICALL
Java_org_iotivity_ca_CaLeClientInterface_caLeBondStateChangedCallback
(JNIEnv *env, jobject obj, jobject jBluetoothDevice, jint jState, jint jPrevState)
{
    OIC_LOG_V(DEBUG, TAG, "CaLeClientInterface - Bond State Changed: current state = %d, previous state = %d", jState, jPrevState);
    VERIFY_NON_NULL_VOID(env, TAG, "env is null");
    VERIFY_NON_NULL_VOID(obj, TAG, "obj is null");
    VERIFY_NON_NULL_VOID(jBluetoothDevice, TAG, "jBluetoothDevice is null");

    jstring jni_address = CALEClientGetLEAddressFromBTDevice(env, jBluetoothDevice);
    if (!jni_address) {
        OIC_LOG(ERROR, TAG, "jni_address is null!");
        return;
    }
    const char* address = (*env)->GetStringUTFChars(env, jni_address, NULL);
    if (!address)
    {
        OIC_LOG(ERROR, TAG, "address is null");
        (*env)->ReleaseStringUTFChars(env, jni_address, address);
        return;
    }

    CALEState_t* deviceState = CALEClientGetStateInfo(address);
    if (!deviceState) 
    {
        OIC_LOG(ERROR, TAG, "CALEClientGetStateInfo has returned null");
        (*env)->ReleaseStringUTFChars(env, jni_address, address);
        return;
    }
    //OIC_LOG_V(DEBUG, TAG, "deviceState->sendState = %d", deviceState->sendState); 

    if (jState == BOND_BONDED) {
        ca_mutex_lock(g_bondMutex);
        ca_cond_signal(g_bondCond);
        ca_mutex_unlock(g_bondMutex);
    }
    (*env)->ReleaseStringUTFChars(env, jni_address, address);
}

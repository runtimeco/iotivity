/******************************************************************
*
* Copyright 2016 Disney Inc. All Rights Reserved.
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

#include "caleinterface.h"

#include<stdio.h>
#include<stdlib.h>
#include<string.h>

#include <camutex.h>
#include <caleadapter.h>
#include <caadapterutils.h>
#include <oic_string.h>
#include <oic_malloc.h>

#include "BLEClient.h"
#include "BLEServer.h"


#define TAG "BLEMonitor"

/*
    This source file is the "front door" to the adapter. It implements C functions
    defined in caleinterface.h.
*/


/**
 * Maintains the callback to be notified on device state changed.
 */
static CALEDeviceStateChangedCallback g_bleDeviceStateChangedCallback = NULL;

/**
 * Maintains the callback to be notified on connection state changed.
 */
static CALEConnectionStateChangedCallback g_bleConnectionStateChangedCallback = NULL;


static BLEClient* g_bleClient = NULL;

static ca_thread_pool_t g_clientThreadpool = NULL;

static BLEServer* g_bleServer = NULL;

static ca_thread_pool_t g_serverThreadpool = NULL;





/// ============================ NETWORK MONITOR ============================

// This is the private callback that is given to the client and server
// It simply calls the global callback if IoTivity has given us one via a call
// to CASetLENWConnectionStateChangedCb().
static void connectionStateChangedCallback(CATransportAdapter_t adapter, const char *remoteAddress, bool connected) {
    if(g_bleConnectionStateChangedCallback != NULL) {
        OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
        g_bleConnectionStateChangedCallback(adapter, remoteAddress, connected);
    }
}

CAResult_t CAInitializeLENetworkMonitor()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    return CA_STATUS_OK;
}

CAResult_t CAInitializeLEAdapter(const ca_thread_pool_t threadPool)
{
    (void) threadPool;
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    return CA_STATUS_OK;
}

CAResult_t CASetLEAdapterStateChangedCb(CALEDeviceStateChangedCallback callback)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    g_bleDeviceStateChangedCallback = callback;

    return CA_STATUS_OK;
}

CAResult_t CASetLENWConnectionStateChangedCb(CALEConnectionStateChangedCallback callback)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    g_bleConnectionStateChangedCallback = callback;

    return CA_STATUS_OK;
}

CAResult_t CAUnSetLEAdapterStateChangedCb()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_bleDeviceStateChangedCallback = NULL;
    return CA_STATUS_OK;
}

CAResult_t CAUnsetLENWConnectionStateChangedCb()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_bleConnectionStateChangedCallback = NULL;
    return CA_STATUS_OK;
}

CAResult_t CAStartLEAdapter()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    return CA_STATUS_OK;
}

CAResult_t CAGetLEAdapterState()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    return CA_STATUS_OK;
}

CAResult_t CAStopLEAdapter()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    return CA_STATUS_OK;
}

void CATerminateLENetworkMonitor()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
}

CAResult_t CAGetLEAddress(char **local_address)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    //TODO: Implement? Not sure how this is used. I never see it get called.

    return CA_STATUS_OK;
}



/// ============================ CLIENT ============================

//NOTE: this is called _before_ CAInitializeLEGattClient
void CASetLEClientThreadPoolHandle(ca_thread_pool_t handle)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_clientThreadpool = handle;
}

CAResult_t CAInitializeLEGattClient()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    g_bleClient = [[BLEClient alloc] init];

    g_bleClient.connectionStateChangedCallback = connectionStateChangedCallback;

    return CA_STATUS_OK;
}

void CATerminateLEGattClient()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    //TODO: stop bleClient and destroy it
    g_bleClient = nil;
}


void CASetLEReqRespClientCallback(CABLEDataReceivedCallback callback)
{
    if(g_bleClient == NULL) {
        OIC_LOG_V(WARNING, TAG, "%s: cannot set callback when bleClient is null", __FUNCTION__);
        return;
    }

    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_bleClient.dataReceivedCallback = callback;
}

void CASetBLEClientErrorHandleCallback(CABLEErrorHandleCallback callback)
{
    if(g_bleClient == NULL) {
        OIC_LOG_V(WARNING, TAG, "%s: cannot set callback when bleClient is null", __FUNCTION__);
        return;
    }

    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_bleClient.errorHandleCallback = callback;
}

CAResult_t CAStartLEGattClient()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    [g_bleClient startScanning];
    return CA_STATUS_OK;
}

void CAStopLEGattClient()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    [g_bleClient stopScanning];
}

CAResult_t  CAUpdateCharacteristicsToGattServer(const char *remoteAddress,
                                                const uint8_t *data, const uint32_t dataLen,
                                                CALETransferType_t type, const int32_t position)
{
    OIC_LOG_V(DEBUG, TAG, "%s: address=%s, type=%d, pos=%u", __FUNCTION__, remoteAddress, (int)type, position);
    
    //TODO: what about using "type" or "position"

    [g_bleClient updateCharacteristicsTo:remoteAddress withData:data withLength:dataLen];

    return CA_STATUS_OK;
}

CAResult_t CAUpdateCharacteristicsToAllGattServers(const uint8_t *data, uint32_t dataLen)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    [g_bleClient updateCharacteristicsToAll:data withLength:dataLen];

    return CA_STATUS_OK;
}




/// ============================ SERVER ============================

void CASetLEServerThreadPoolHandle(ca_thread_pool_t handle)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_serverThreadpool = handle;
}

CAResult_t CAInitializeLEGattServer()
{
    NSLog(@"<<%s>>", __FUNCTION__);

    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    g_bleServer = [[BLEServer alloc] init];

    g_bleServer.connectionStateChangedCallback = connectionStateChangedCallback;

    return CA_STATUS_OK;
}

void CASetLEReqRespServerCallback(CABLEDataReceivedCallback callback)
{
    if(g_bleServer == NULL) {
        OIC_LOG_V(WARNING, TAG, "%s: cannot set callback when bleServer is null", __FUNCTION__);
        return;
    }

    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_bleServer.dataReceivedCallback = callback;
}

void CASetBLEServerErrorHandleCallback(CABLEErrorHandleCallback callback)
{
    if(g_bleServer == NULL) {
        OIC_LOG_V(WARNING, TAG, "%s: cannot set callback when bleServer is null", __FUNCTION__);
        return;
    }

    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    g_bleServer.errorHandleCallback = callback;
}

CAResult_t CAStartLEGattServer()
{
    NSLog(@"<<%s>>", __FUNCTION__);

    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    [g_bleServer startAdvertising];
    
    return CA_STATUS_OK;
}

CAResult_t CAStopLEGattServer()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    [g_bleServer stopAdvertising];

    return CA_STATUS_OK;
}

void CATerminateLEGattServer()
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);
    //TODO:
    g_bleServer = nil;
}

CAResult_t CAUpdateCharacteristicsToGattClient(const char *address, const uint8_t *charValue,
                                               uint32_t charValueLen)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    [g_bleServer updateCharacteristicsTo:address withData:charValue withLength:charValueLen];

    return CA_STATUS_OK;
}

CAResult_t CAUpdateCharacteristicsToAllGattClients(const uint8_t *charValue, uint32_t charValueLen)
{
    OIC_LOG_V(DEBUG, TAG, "%s", __FUNCTION__);

    [g_bleServer updateCharacteristicsToAll:charValue withLength:charValueLen];

    return CA_STATUS_OK;
}



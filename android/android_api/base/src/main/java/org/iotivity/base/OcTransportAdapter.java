/*
 * //******************************************************************
 * //
 * // Copyright 2015 Intel Corporation.
 * //
 * //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 * //
 * // Licensed under the Apache License, Version 2.0 (the "License");
 * // you may not use this file except in compliance with the License.
 * // You may obtain a copy of the License at
 * //
 * //      http://www.apache.org/licenses/LICENSE-2.0
 * //
 * // Unless required by applicable law or agreed to in writing, software
 * // distributed under the License is distributed on an "AS IS" BASIS,
 * // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * // See the License for the specific language governing permissions and
 * // limitations under the License.
 * //
 * //-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
 */

package org.iotivity.base;

import java.security.InvalidParameterException;
import java.util.EnumSet;

public enum OcTransportAdapter {


    /** value zero indicates discovery.*/
    OC_DEFAULT_ADAPTER          (0), 

    /** IPv4 and IPv6, including 6LoWPAN.*/
    OC_ADAPTER_IP               (1 << 0),

    /** GATT over Bluetooth LE.*/
    OC_ADAPTER_GATT_BTLE        (1 << 1),

    /** RFCOMM over Bluetooth EDR.*/
    OC_ADAPTER_RFCOMM_BTEDR     (1 << 2),
    
    /**Remote Access over XMPP.*/
    OC_ADAPTER_REMOTE_ACCESS    (1 << 3),
    
    /** CoAP over TCP.*/
    OC_ADAPTER_TCP              (1 << 4),

    /** NFC Transport for Messaging.*/
    OC_ADAPTER_NFC              (1 << 5)
    ;

    private int value;

    private OcTransportAdapter(int value) {
        this.value = value;
    }

    public int getValue() {
        return this.value;
    }

    public static EnumSet<OcTransportAdapter> convertToEnumSet(int value) {
        EnumSet<OcTrasportAdapter> typeSet = null;

        for (OcTrasportAdapter v : values()) {
            if (0 != (value & v.getValue())) {
                if (null == typeSet) {
                    typeSet = EnumSet.of(v);
                } else {
                    typeSet.add(v);
                }
            }
        }

        if (null == typeSet || typeSet.isEmpty()) {
            throw new InvalidParameterException("Unexpected OcTrasportAdapter value:" + value);
        }

        return typeSet;
    }
}

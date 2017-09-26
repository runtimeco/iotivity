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

package org.iotivity.ca;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.UUID;
import java.lang.reflect.Method;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.util.Log;

public class CaLeClientInterface {

    private static String SERVICE_UUID = "ADE3D529-C784-4F63-A987-EB69F70EE816";
    private static String TAG          = "OIC_LE_CB_INTERFACE";
    private static Context mContext;
    private static HashMap<String, BluetoothGatt> mBluetoothGatts = new HashMap<>(); 

    private CaLeClientInterface(Context context) {
        caLeRegisterLeScanCallback(mLeScanCallback);
        caLeRegisterGattCallback(mGattCallback);
        mContext = context;
        registerIntentFilter();
    }

    public static void getLeScanCallback() {
        caLeRegisterLeScanCallback(mLeScanCallback);
    }

    public static void getLeGattCallback() {
        caLeRegisterGattCallback(mGattCallback);
    }

    private static IntentFilter registerIntentFilter() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothAdapter.ACTION_STATE_CHANGED);
        filter.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        mContext.registerReceiver(mReceiver, filter);
        return filter;
    }

    public static void destroyLeInterface() {
        mContext.unregisterReceiver(mReceiver);
    }


    public static void disconnectAll() {
        ArrayList<String> hostAddresses = new ArrayList<String>(mBluetoothGatts.keySet());
        for (String hostAddress : hostAddresses) {
            disconnect(hostAddress);
        }
    }

    public static void disconnect(String hostAddress) {
        BluetoothGatt gatt = mBluetoothGatts.get(hostAddress);
        if (gatt != null) {
            gatt.disconnect();
        }
    }

    public static boolean removeBond(BluetoothDevice device) {
       try {
            Method method = device.getClass().getMethod("removeBond", (Class[]) null);
            return (Boolean) method.invoke(device, (Object[]) null);
        } catch (Exception e) {
            e.printStackTrace();
        }
        return false; 
    }

    private native static void caLeRegisterLeScanCallback(BluetoothAdapter.LeScanCallback callback);

    private native static void caLeRegisterGattCallback(BluetoothGattCallback callback);

    // BluetoothAdapter.LeScanCallback
    private native static void caLeScanCallback(BluetoothDevice device);

    // BluetoothGattCallback
    private native static void caLeGattConnectionStateChangeCallback(
            BluetoothGatt gatt, int status, int newState);

    // BluetoothGattCallback for Connection Manager
    private native static void caManagerLeGattConnectionStateChangeCB(
            BluetoothGatt gatt, int status, int newState);

    private native static void caLeGattNWConnectionStateChangeCallback(
            BluetoothGatt gatt, int status, int newState);

    private native static void caLeGattServicesDiscoveredCallback(BluetoothGatt gatt, int status);

    private native static void caLeGattCharacteristicWriteCallback(
            BluetoothGatt gatt, byte[] data, int status);

    private native static void caLeGattCharacteristicChangedCallback(
            BluetoothGatt gatt, byte[] data);

    private native static void caLeGattDescriptorWriteCallback(BluetoothGatt gatt, 
            BluetoothGattDescriptor descriptor, int status);

    private native static void caLeGattReliableWriteCompletedCallback(BluetoothGatt gatt,
                                                                     int status);

    private native static void caLeGattReadRemoteRssiCallback(BluetoothGatt gatt, int rssi,
                                                             int status);

    // Network Monitor
    private native static void caLeStateChangedCallback(int state);

    // adapter state
    private native static void caManagerAdapterStateChangedCallback(int state);

    // bond state
    private native static void caLeBondStateChangedCallback(BluetoothDevice device, 
                                                                int state, int prevState);
    private native static void caLeBondRemovedCallback(BluetoothDevice device, 
                                                                int state, int prevState);
    private native static void caManagerBondStateChangedCallback(BluetoothDevice address);

    private native static void caManagerLeServicesDiscoveredCallback(BluetoothGatt gatt,
                                                                     int status);

    private native static void caManagerLeRemoteRssiCallback(BluetoothGatt gatt, int rssi,
                                                             int status);

    private native static void caLeGattMtuChangedCallback(BluetoothGatt gatt, int mtu,
                                                                int status);

    // Callback
    private static BluetoothAdapter.LeScanCallback mLeScanCallback =
                   new BluetoothAdapter.LeScanCallback() {

        @Override
        public void onLeScan(BluetoothDevice device, int rssi, byte[] scanRecord) {

            try {
                List<UUID> uuids = getUuids(scanRecord);
                for (UUID uuid : uuids) {
                    //Log.d(TAG, "UUID : " + uuid.toString());
                    if(uuid.toString().contains(SERVICE_UUID.toLowerCase())) {
                        //Log.d(TAG, "we found that has the Device");
                        //Log.d(TAG, "scanned device address : " + device.getAddress());
                        caLeScanCallback(device);
                    }
                }
            } catch(UnsatisfiedLinkError e) {

            }
        }
    };

    private static List<UUID> getUuids(final byte[] scanRecord) {
        List<UUID> uuids = new ArrayList<UUID>();

        int offset = 0;
        while (offset < (scanRecord.length - 2)) {
            int len = scanRecord[offset++];
            if (len == 0)
                break;

            int type = scanRecord[offset++];

            switch (type) {
            case 0x02:
            case 0x03:
                while (len > 1) {
                    int uuid16 = scanRecord[offset++];
                    uuid16 += (scanRecord[offset++] << 8);
                    len -= 2;
                    uuids.add(UUID.fromString(String.format(
                            "%08x-0000-1000-8000-00805f9b34fb", uuid16)));
                }
                break;
            case 0x06:
            case 0x07:
                while (len >= 16) {
                    try {
                        ByteBuffer buffer = ByteBuffer.wrap(scanRecord, offset++, 16).
                                                            order(ByteOrder.LITTLE_ENDIAN);
                        long mostSigBits = buffer.getLong();
                        long leastSigBits = buffer.getLong();
                        uuids.add(new UUID(leastSigBits, mostSigBits));
                    } catch (IndexOutOfBoundsException e) {
                        Log.e(TAG, e.toString());
                        continue;
                    } finally {
                        offset += 15;
                        len -= 16;
                    }
                }
                break;
            default:
                offset += (len - 1);
                break;
            }
        }
        return uuids;
    }

    private static final BluetoothGattCallback mGattCallback = new BluetoothGattCallback() {

        @Override
        public void onConnectionStateChange(BluetoothGatt gatt, int status, int newState) {
            super.onConnectionStateChange(gatt, status, newState);
            Log.d(TAG, "onConnectionStateChange() - status: " + status + 
                    ", address: " + gatt.getDevice().getAddress() + 
                    ", bond state: " + gatt.getDevice().getBondState());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);

            caLeGattConnectionStateChangeCallback(gatt, status, newState);
            caManagerLeGattConnectionStateChangeCB(gatt, status, newState);
            caLeGattNWConnectionStateChangeCallback(gatt, status, newState);
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt gatt, int status) {
            super.onServicesDiscovered(gatt, status);
            Log.d(TAG, "onServicesDiscovered() - status: " + status + ", address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);

            caLeGattServicesDiscoveredCallback(gatt, status);
            caManagerLeServicesDiscoveredCallback(gatt, status);
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt gatt,
                BluetoothGattCharacteristic characteristic, int status) {
            super.onCharacteristicRead(gatt, characteristic, status);
            Log.d(TAG, "onCharacteristicRead() - status: " + status + ", address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt gatt,
                BluetoothGattCharacteristic characteristic, int status) {
            super.onCharacteristicWrite(gatt, characteristic, status);
            Log.d(TAG, "onCharacteristicWrite() - status: " + status + ", address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);

            caLeGattCharacteristicWriteCallback(gatt, characteristic.getValue(), status);
        }

        @Override
        public void onCharacteristicChanged(BluetoothGatt gatt,
                BluetoothGattCharacteristic characteristic) {
            super.onCharacteristicChanged(gatt, characteristic);
            Log.d(TAG, "onCharacteristicChanged() - address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);

            caLeGattCharacteristicChangedCallback(gatt, characteristic.getValue());
        }

        @Override
        public void onDescriptorRead(BluetoothGatt gatt, BluetoothGattDescriptor descriptor,
                int status) {
            super.onDescriptorRead(gatt, descriptor, status);
            Log.d(TAG, "onDescriptorRead() - status: " + status + ", address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt gatt, BluetoothGattDescriptor descriptor,
                int status) {
            super.onDescriptorWrite(gatt, descriptor, status);
            Log.d(TAG, "onDescriptorWrite() - status: " + status + ", address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);

            caLeGattDescriptorWriteCallback(gatt, descriptor, status);
        }

        @Override
        public void onReliableWriteCompleted(BluetoothGatt gatt, int status) {
            super.onReliableWriteCompleted(gatt, status);
            Log.d(TAG, "onReliableWriteCompleted() - status: " + status + ", address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);
        }

        @Override
        public void onReadRemoteRssi(BluetoothGatt gatt, int rssi, int status) {
            super.onReadRemoteRssi(gatt, rssi, status);
            Log.d(TAG, "onReadRemoteRssi() - status: " + status + ", rssi: " + rssi + ", address: " + gatt.getDevice().getAddress());

            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);
            caManagerLeRemoteRssiCallback(gatt, rssi, status);
        }

        @Override
        public void onMtuChanged(BluetoothGatt gatt, int mtu, int status) {
            super.onMtuChanged(gatt, mtu, status);
            Log.d(TAG, "onMtuChanged() - status: " + status + ", mtu: " + mtu + ", address: " + gatt.getDevice().getAddress());
            mBluetoothGatts.put(gatt.getDevice().getAddress(), gatt);
            caLeGattMtuChangedCallback(gatt, mtu, status);
        }
    };

    private static final BroadcastReceiver mReceiver = new BroadcastReceiver() {

        @Override
        public void onReceive(Context context, Intent intent) {

            String action = intent.getAction();

            if (action != null && action.equals(BluetoothAdapter.ACTION_STATE_CHANGED)) {

                int state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE,
                                               BluetoothAdapter.ERROR);

                if (state == BluetoothAdapter.STATE_ON || state == BluetoothAdapter.STATE_OFF
                        || state == BluetoothAdapter.STATE_TURNING_OFF)
                {
                    caLeStateChangedCallback(state);
                    caManagerAdapterStateChangedCallback(state);
                }
            }

            if (action != null && action.equals(BluetoothDevice.ACTION_BOND_STATE_CHANGED)) {
                // Get bond info from intent
                BluetoothDevice device = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                int bondState = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE,
                                                   BluetoothDevice.ERROR);
                int prevBondState = intent.getIntExtra(BluetoothDevice.EXTRA_PREVIOUS_BOND_STATE, 
                                                       BluetoothDevice.ERROR);
                
                // Call client's bond state changed callback
                caLeBondStateChangedCallback(device, bondState, prevBondState);
                
                if (bondState == BluetoothDevice.BOND_NONE && 
                    prevBondState == BluetoothDevice.BOND_BONDED) {
                    //caManagerBondStateChangedCallback(device);
                
                    // If the bond was removed, let the network manager know
                    //caLeBondRemovedCallback(device, bondState, prevBondState);
                }
            }
        }
    };
}


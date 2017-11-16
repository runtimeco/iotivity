//
//  IotivityClient.h
//  iotivity_sample
//
//  Created by Marko Kiiskila on 5/15/17.
//  Copyright © 2017 Marko Kiiskila. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <stdlib.h>

#import "octypes.h"
#import "ocstack.h"
#import "ocpayload.h"
#import "cainterface.h"

#import "Iotivity.h"
#import "OcResource.h"
#import "OcRepresentation.h"
#import "OcRepresentationValue.h"

/**
 * IotivityClient can be thought of as the Objective-C wrapper for Iotivity's
 * client side CSDK. First and foremost, this client wrapper provides the 
 * means to make CoAP calls to resources over a variety of transports. 
 * Additionally, this client can perform unicast and multicast resource 
 * discovery for OCF implementations. 
 * 
 * This class must first be initialized before doing any discovery or CoAP 
 * calls. It is reccommended that init be called as soon as possible 
 * (in AppDelegate's didFinishLaunching). Furthermore it is advisable to 
 * give IotivityClient time to initialize before immediately making calls.
 * 
 * IotivitClient is a singleton instance which can be obtained useing the
 * "shared" method. all direct calls to this class should be directed through 
 * this singleton instance.
 * 
 * IotivityClient's currently implemented operations are:
 * 
 *  - GET: Get a resource's values
 * 
 *  - PUT: Modify a resource's values
 *
 *  - OBSERVE: Observe a resource's values at a frequency determined by
 *             the server
 */
@interface IotivityClient : NSObject

/**
 * This method is the singleton instance to use when performing OCF Resource discovery
 * or one of the RESTful style coap methods included in IotivityClient. 
 */
+ (instancetype)shared;

///////////////////////////////////////////////////////////////////////////////
//  DISCOVERY
///////////////////////////////////////////////////////////////////////////////

/**
 * @discussion This method will perform multicast resource discovery over the
 transport specified in the given connectivity type.
 * @param connType The transport to perform multicast resource discovery over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param callback the callback to be called when discovery has completed.
 * @return the OCStackResult status code returned by OCDoResource.
 */
- (int) discoverMulticast:(OCConnectivityType)connType
                 callback:(DiscoverCallback)callback;

/**
 * @discussion This method will perform multicast resource discovery over the
 transport specified in the given connectivity type.
 * @param connType The transport to perform multicast resource discovery over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback the callback to be called when discovery has completed.
 * @return the OCStackResult status code returned by OCDoResource.
 */
- (int) discoverMulticast:(OCConnectivityType)connType
                      qos:(OCQualityOfService)qos
                 callback:(DiscoverCallback)callback;

/**
 * @discussion This method will perform unicast resource discovery over the
 transport specified in the the adapter parameter to the device given by the
 address.
 * @param address The address of the device to perform resource discovery on
 * @param connType The transport to perform unicast resource discovery over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) discoverUnicast:(NSString*)address
               connType:(OCConnectivityType)connType
                    qos:(OCQualityOfService)qos
               callback:(DiscoverCallback)callback;

///////////////////////////////////////////////////////////////////////////////
//  GET
///////////////////////////////////////////////////////////////////////////////

/*!
 * @discussion This method gets the values from a CoAP resource using the
 default connectivity type and quality of service.
 * @param uri The URI from of the resource to GET
 * @param address The address of the device hosting the resource
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(NSString*)uri
            address:(NSString*)address
           callback:(GetCallback)callback;

/*!
 * @discussion This method gets the values from a CoAP resource
 * @param uri The URI from of the resource to GET
 * @param address The address of the device hosting the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(NSString*)uri
            address:(NSString*)address
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(GetCallback)callback;

/*!
 * @discussion This method gets the values from a OCF resource
 * @param resource An OcResource object representing an OCF resource
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(OcResource*)resource
                qos:(OCQualityOfService)qos
           callback:(GetCallback)callback;

/*!
 * @discussion This method gets the values from a resource
 * @param uri The URI of the resource to perform this transaction on
 * @param devAddr An OCDevAddr struct representing the target device
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) getResource:(NSString*)uri
            devAddr:(OCDevAddr)devAddr
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(GetCallback)callback;

///////////////////////////////////////////////////////////////////////////////
//  PUT
///////////////////////////////////////////////////////////////////////////////

/*!
 * @discussion This method puts values to a CoAP resoruce using the default
 connectivity type and quality of service.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param values An array of values to PUT to the resource
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(NSString*)uri
            address:(NSString*)address
             values:(NSArray<OcRepresentationValue*>*)values
           callback:(PutCallback)callback;

/*!
 * @discussion This method puts values to a CoAP resoruce using the default
 connectivity type and quality of service.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param values An array of values to PUT to the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(NSString*)uri
            address:(NSString*)address
             values:(NSArray<OcRepresentationValue*>*)values
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(PutCallback)callback;

/*!
 * @discussion This method PUTs the values to an OCF resource
 * @param resource An OcResource object representing an OCF resource
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(OcResource*)resource
             values:(NSArray<OcRepresentationValue*>*)values
                qos:(OCQualityOfService)qos
           callback:(PutCallback)callback;

/*!
 * @discussion This method puts values to a CoAP resoruce using the default
 connectivity type and quality of service.
 * @param uri The URI of the resource to perform this transaction on
 * @param devAddr An OCDevAddr struct representing the target device
 * @param values An array of values to PUT to the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) putResource:(NSString*)uri
            devAddr:(OCDevAddr)devAddr
             values:(NSArray<OcRepresentationValue*>*)values
           connType:(OCConnectivityType)connType
                qos:(OCQualityOfService)qos
           callback:(PutCallback)callback;

///////////////////////////////////////////////////////////////////////////////
//  OBSERVE
///////////////////////////////////////////////////////////////////////////////

/*!
 * @discussion This method periodically observes a CoAP resource at an interval
 specified by the host.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(NSString*)uri
                address:(NSString*)address
               callback:(ObserveCallback)callback;

/*!
 * @discussion This method periodically observes a CoAP resource at an interval
 specified by the host.
 * @param uri The URI of the resource to perform this transaction on
 * @param address The address of the device hosting the resource
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(NSString*)uri
                address:(NSString*)address
               connType:(OCConnectivityType)connType
                    qos:(OCQualityOfService)qos
               callback:(ObserveCallback)callback;

/*!
 * @discussion This method periodically observes an OCF resource at an interval
 specified by the host.
 * @param resource An OcResource object representing an OCF resource.
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(OcResource*)resource
                    qos:(OCQualityOfService)qos
               callback:(ObserveCallback)callback;

/*!
 * @discussion This method periodically observes an OCF resource at an interval
 specified by the host.
 * @param uri The URI of the resource to perform this transaction on
 * @param devAddr An OCDevAddr struct representing the target device
 * @param connType The connectivity type to perform this transaction over.
 This value should include one adapter enum flag (CT_ADAPTER_XXX).
 * @param qos The quality of service to use for this transaction.
 * @param callback The callback called when the transaction has finished.
 * @return OCStackResult status code returned by OCDoResource
 */
- (int) observeResource:(NSString*)uri
                devAddr:(OCDevAddr)devAddr
       connectivityType:(OCConnectivityType)connType
                    qos:(OCQualityOfService)qos
               callback:(ObserveCallback)callback;

/*!
 * @discussion Cancel an ongoing observe transaction
 * @param uri The URI of the resource to cancel observation on
 * @param address The host address of the resource
 * @return An OCStackResult returned by OCCancel
 */
- (int) cancelObserve:(NSString*)uri
              address:(NSString*)address;

/*!
 * @discussion Cancel an ongoing observe transaction
 * @param resource The resource to cancel observation on
 * @return An OCStackResult returned by OCCancel
 */
- (int) cancelObserve:(OcResource*)resource;

// Utilities
- (int) setTargetUUIDs:(NSString*)serviceUUID requestUUID:(NSString*)requestUUID responseUUID:(NSString*)responseUUID;
@end

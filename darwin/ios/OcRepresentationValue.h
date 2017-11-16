
#import <Foundation/Foundation.h>
#import "octypes.h"
#import "Iotivity.h"

/**
 * OcRepresentationValue is used when PUTing or POSTing to a resource. These
 * values are then packaged into a CoAP payload (generally CBOR encoded) and
 * sent to the host resource over a given trasport. OcRepresentationValues 
 * can be one of the following types:
 *
 *  1. NULL
 *
 *  2. int64_t
 *
 *  3. double
 *
 *  4. bool
 *
 *  5. NSString*
 *
 *  6. uint8_t* (byte string)
 *
 *  7. OcRepresentation* (object)
 *
 *  8. NSArray<OcRepresentation*>*
 */
@interface OcRepresentationValue : NSObject

/// Name (key) of this representation value
@property (strong) NSString * name;
/// Value type of this representation value
@property OCRepPayloadPropType type;

/// Integer value
@property int64_t intValue;
/// Double value
@property double doubleValue;
/// Bool value
@property bool boolValue;
/// String value
@property (strong) NSString *stringValue;
/// Byte string (array) value 
@property uint8_t *byteStringValue;
/// Length of the byte string (in bytes)
@property int byteStringLength;
/// Object value
@property (strong) OcRepresentation* objectValue;
/// Array value
@property (strong) NSArray<OcRepresentation*> *arrayValue;

/**
 * Init a nil representation value
 */
- (instancetype)initWithName:(NSString*)name;
/**
 * Init an integer representation value
 */
- (instancetype)initWithName:(NSString*)name intValue:(int64_t)value;
/**
 * Init a double representation valiue
 */
- (instancetype)initWithName:(NSString*)name doubleValue:(double)value;
/**
 * Init a boolean representation value
 */
- (instancetype)initWithName:(NSString*)name boolValue:(bool)value;
/**
 * Init a string representation value
 */
- (instancetype)initWithName:(NSString*)name stringValue:(NSString*)value;
/**
 * Init a byte string (array) representation value
 */
- (instancetype)initWithName:(NSString*)name byteStringValue:(uint8_t*)value withLength:(int)length;
/**
 * Init a object (OcRepresentation) value
 */
- (instancetype)initWithName:(NSString*)name objectValue:(OcRepresentation*)value;
/**
 * Init an array value
 */
- (instancetype)initWithName:(NSString*)name arrayValue:(NSArray<OcRepresentationValue*>*)value;

@end

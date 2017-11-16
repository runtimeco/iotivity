
#import "OcRepresentationValue.h"

@implementation OcRepresentationValue

- (instancetype)initWithName:(NSString*) name {
    self.name = name;
    self.type = OCREP_PROP_NULL;
    return self;
}
- (instancetype)initWithName:(NSString*)name intValue:(int64_t)value {
    self.name = name;
    self.type = OCREP_PROP_INT;
    self.intValue = value;
    return self;
}
- (instancetype)initWithName:(NSString*)name doubleValue:(double)value {
    self.name = name;
    self.type = OCREP_PROP_DOUBLE;
    self.doubleValue = value;
    return self;
}
- (instancetype)initWithName:(NSString*)name boolValue:(bool)value {
    self.name = name;
    self.type = OCREP_PROP_BOOL;
    self.boolValue = value;
    return self;
}
- (instancetype)initWithName:(NSString*)name stringValue:(NSString*)value {
    self.name = name;
    self.type = OCREP_PROP_STRING;
    self.stringValue = value;
    return self;
}
- (instancetype)initWithName:(NSString*)name byteStringValue:(uint8_t*)value withLength:(int)length {
    self.name = name;
    self.type = OCREP_PROP_BYTE_STRING;
    self.byteStringValue = value;
    self.byteStringLength = length;
    return self;
}
- (instancetype)initWithName:(NSString*)name objectValue:(id)value {
    self.name = name;
    self.type = OCREP_PROP_OBJECT;
    self.objectValue = value;
    return self;
}
- (instancetype)initWithName:(NSString*)name arrayValue:(NSArray*)value {
    self.name = name;
    self.type = OCREP_PROP_ARRAY;
    self.arrayValue = value;
    return self;
}


@end

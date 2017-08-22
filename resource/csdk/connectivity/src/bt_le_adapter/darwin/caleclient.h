#ifndef CA_LECLIENT_H_
#define CA_LECLIENT_H_

#ifdef __cplusplus
extern "C"
{
#endif

uint16_t CALEClientGetMtuSize(const char* address);
void CALEClientSetTargetServiceUuid(const char* uuid);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*CA_LECLIENT_H_ */

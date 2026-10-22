//
//  RoomModule.hpp
//
//  PolygraphModular wrapper around the framework-free Room core (Room.hpp/.cpp),
//  in the same shape as ModalBankModule: marshal PM parameters into the core,
//  convert PM's octave pitch to Hz at each strike, and feed the input through.
//

#ifndef RoomModule_hpp
#define RoomModule_hpp

#include "Module.hpp"

extern const void        InitRoomModule(uint32_t count, Module* module);
extern const void        DeinitRoomModule(Module* module);
extern const void        ResetRoomModule(Module* module);
extern const void        SampleRateRoomModule(Module* module, double newSampleRate);
extern void              MIDIRoomModule(const pm::UmpMessage& message, Module* module);
extern const KPBuffPair* ProcessRoomModule(uint64_t processCallID, uint32_t count, Module* module);

#endif /* RoomModule_hpp */
